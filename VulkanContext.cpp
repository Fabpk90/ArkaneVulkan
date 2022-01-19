#include "VulkanContext.h"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <iostream>
#include <optional>

#include "Mesh.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_vulkan.h"
#include "vma/vk_mem_alloc.hpp"

vma::Allocator VulkanContext::s_allocator = nullptr;
VulkanContext* VulkanContext::GraphicInstance = nullptr;

//TODO: handle resizing with glfw callbacks

void VulkanContext::InitWindow()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = glfwCreateWindow(1280, 720, "Vulkan Render", nullptr, nullptr);

    const auto extensions = vk::enumerateInstanceExtensionProperties();

    for (const auto& extension : extensions)
    {
        std::cout << extension.extensionName << " available" << std::endl;
    }
}

void VulkanContext::InitVulkan()
{
    CreateInstance();
    CreatePhysicalDevice();
    CreateLogicalDevice();
    CreateMemPool();
    CreateSwapChain();
    CreateSwapChainViews();
    CreateRenderPass();
    CreateDescriptorSetLayout();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateCommandPool();

    CreateImGuiResources();

    LoadEntities();
    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateCommandBuffers();
    CreateSyncObjects();
}

void VulkanContext::DrawFrame()
{
    ImGui::Render();

    //wait for fences, to make sure we will not use res from fb[0] when going back (with %)
    auto resFence = logicalDevice.waitForFences(fenceInFlight[currentFrame], true, UINT64_MAX);

    //1) acquire image from swapchain
    //2) execute command buffer
    //3 send result to swap chain

    //1)
    const auto imageIndex = logicalDevice.acquireNextImageKHR(swapchain, UINT64_MAX, semaphoresAcquireImage[currentFrame]);

    if(imageIndex.result == vk::Result::eErrorOutOfDateKHR)
    {
        RecreateSwapChain();
        return;
    }

    //if we want to use an image still in flight
    if (fenceImagesInFlight[imageIndex.value].has_value())
    {
        //we make sure it's not in flight anymore
        const auto res = logicalDevice.waitForFences(fenceImagesInFlight[imageIndex.value].value(), true, UINT64_MAX);

        assert(res == vk::Result::eSuccess);
    }

    fenceImagesInFlight[imageIndex.value] = fenceInFlight[currentFrame];

    UpdateUniformBuffer(imageIndex);

    vk::SubmitInfo submitInfo;

    const vk::Semaphore waitSemaphore[] = {semaphoresAcquireImage[currentFrame]};
    constexpr vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphore;

    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffersGraphics[imageIndex.value];

    const vk::Semaphore semaphoreToSignal[] = {semaphoresFinishedRendering[currentFrame]};

    //this will send a signal once executed
    submitInfo.pSignalSemaphores = semaphoreToSignal;
    submitInfo.signalSemaphoreCount = 1;

    logicalDevice.resetFences(fenceInFlight[currentFrame]);
    //2)
    graphicsQueue.submit(submitInfo, fenceInFlight[currentFrame]);

    vk::PresentInfoKHR presentInfo;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = semaphoreToSignal;

    const vk::SwapchainKHR swapchains[] = {swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex.value;
    //3)
    const auto res = presentationQueue.presentKHR(presentInfo);

    if(res == vk::Result::eErrorOutOfDateKHR 
        || res == vk::Result::eSuboptimalKHR)
    {
        RecreateSwapChain();
        return;
    }

    assert(res == vk::Result::eSuccess);
}

void VulkanContext::DrawLoop()
{
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE))
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        //imgui new frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        ImGui::NewFrame();

        //imgui commands
        ImGui::ShowDemoWindow();


        DrawFrame();

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    logicalDevice.waitIdle();
}

void VulkanContext::Destroy()
{
    CleanUpSwapChain();

    delete mesh;

    logicalDevice.destroyDescriptorSetLayout(descriptorSetLayout);

    logicalDevice.destroyBuffer(vertexBuffer);
    logicalDevice.freeMemory(vertexDeviceMemory);

    logicalDevice.destroyBuffer(indexBuffer);
    logicalDevice.freeMemory(indexBufferDeviceMemory);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        logicalDevice.destroySemaphore(semaphoresAcquireImage[i]);
        logicalDevice.destroySemaphore(semaphoresFinishedRendering[i]);
        logicalDevice.destroyFence(fenceInFlight[i]);
    }

    logicalDevice.destroyCommandPool(commandPoolGraphics);
    logicalDevice.destroyCommandPool(commandPoolTransfer);
    logicalDevice.destroyCommandPool(commandPoolTransientResources);

    //imgui
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    logicalDevice.destroyRenderPass(imguiRenderPass);
    logicalDevice.destroyDescriptorPool(imguiDescriptorPool);

	DestroySurface();

	DestroyDevice();
	DestroyInstance();

    vmaDestroyAllocator(s_allocator);

    glfwDestroyWindow(window);

    glfwTerminate();
}

bool VulkanContext::CheckValidationSupport() const
{
	const auto properties = vk::enumerateInstanceLayerProperties();

    for (const char* layerName : validationLayers) 
    {
        bool layerFound = false;

        for (const auto& layerProperties : properties) 
        {
            if (strcmp(layerName, layerProperties.layerName) == 0) 
            {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) 
        {
            return false;
        }
    }

    return true;
}

void VulkanContext::CreateInstance()
{
    if (enableValidationLayers && !CheckValidationSupport())
        assert(0);

    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = "Vulkan Renderer";
    appInfo.pEngineName = "Vulkan Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    vk::InstanceCreateInfo instanceInfo;
    instanceInfo.pApplicationInfo = &appInfo;

    u32 glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    instanceInfo.enabledExtensionCount = glfwExtensionCount;
    instanceInfo.ppEnabledExtensionNames = glfwExtensions;

    if(enableValidationLayers)
    {
        instanceInfo.enabledLayerCount = validationLayers.size();
        instanceInfo.ppEnabledLayerNames = validationLayers.data();
    }

    instance = vk::createInstance(instanceInfo);

    VkSurfaceKHR _surface;
    glfwCreateWindowSurface(instance, window, nullptr, &_surface);

    surface = vk::SurfaceKHR(_surface);
}

void VulkanContext::CreatePhysicalDevice()
{
    //enumerate and pick physical GPU
    const auto physicalDevices = instance.enumeratePhysicalDevices();
    physicalDevice = physicalDevices[0];
    //todo: should check here for swapchain support

    const auto features = physicalDevice.getFeatures();
    assert(features.samplerAnisotropy);
}

void VulkanContext::CreateImGuiResources()
{
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(window, true);

    const std::vector<vk::DescriptorPoolSize> pool_sizes =
    {
        { vk::DescriptorType::eSampler, 1000 },
        { vk::DescriptorType::eCombinedImageSampler, 1000 },
        { vk::DescriptorType::eSampledImage, 1000 },
        { vk::DescriptorType::eStorageImage, 1000 },
        { vk::DescriptorType::eUniformTexelBuffer, 1000 },
        { vk::DescriptorType::eStorageTexelBuffer, 1000 },
        { vk::DescriptorType::eUniformBuffer, 1000 },
        { vk::DescriptorType::eStorageBuffer, 1000 },
        { vk::DescriptorType::eUniformBufferDynamic, 1000 },
        { vk::DescriptorType::eStorageBufferDynamic, 1000 },
        { vk::DescriptorType::eInputAttachment, 1000 }
    };

    vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
    descriptorPoolCreateInfo.pPoolSizes = pool_sizes.data();
    descriptorPoolCreateInfo.poolSizeCount = pool_sizes.size();
    descriptorPoolCreateInfo.maxSets = 100;
    descriptorPoolCreateInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

    imguiDescriptorPool = logicalDevice.createDescriptorPool(descriptorPoolCreateInfo);

    //this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = logicalDevice;
    init_info.Queue = graphicsQueue;
    init_info.DescriptorPool = imguiDescriptorPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info, renderPass);

    vk::CommandBuffer cmd = BeginSingleTimeCommands();
    ImGui_ImplVulkan_CreateFontsTexture(cmd);
    EndSingleTimeCommands(cmd);

    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

void VulkanContext::CreateLogicalDevice()
{
    familiesAvailable = QueueFamilies::FindQueueFamilies(physicalDevice, surface);

    constexpr float priority = 1.0f;

    std::array<vk::DeviceQueueCreateInfo, 3> infos;

    //graphics queue
	infos[0].pQueuePriorities = &priority;
    infos[0].queueCount = 1;
    infos[0].queueFamilyIndex = familiesAvailable.graphicsFamily.value_or(-1);

    // presentation queue
    infos[1].pQueuePriorities = &priority;
    infos[1].queueCount = 1;
    infos[1].queueFamilyIndex = familiesAvailable.presentFamily.value_or(-1);

    // transfer queue
    infos[2].pQueuePriorities = &priority;
    infos[2].queueCount = 1;
    infos[2].queueFamilyIndex = familiesAvailable.transferFamily.value_or(-1);

    const std::vector extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    const auto layers = physicalDevice.enumerateDeviceExtensionProperties();

	for (const auto& layer : layers)
	{
        std::cout << layer.extensionName << std::endl;
	}

    logicalDevice = physicalDevice.createDevice(vk::DeviceCreateInfo(vk::DeviceCreateFlags(), infos, nullptr, extensions));

    graphicsQueue = logicalDevice.getQueue(familiesAvailable.graphicsFamily.value_or(-1), 0);
    presentationQueue = logicalDevice.getQueue(familiesAvailable.presentFamily.value_or(-1), 0);
    transferQueue = logicalDevice.getQueue(familiesAvailable.transferFamily.value_or(-1), 0);
}

void VulkanContext::CreateMemPool() const
{
    vma::AllocatorCreateInfo allocatorInfo;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = logicalDevice;
    allocatorInfo.instance = instance;

    s_allocator = vma::createAllocator(allocatorInfo);
}

void VulkanContext::CreateSwapChain()
{
    //check for capabilities first

    const vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);

    const auto formats = physicalDevice.getSurfaceFormatsKHR(surface);
    const auto presentModes = physicalDevice.getSurfacePresentModesKHR(surface);

    //check if something is available
    assert(!formats.empty() || !presentModes.empty());

    vk::SurfaceFormatKHR surfaceFormat = vk::Format::eUndefined;
    std::optional<vk::PresentModeKHR> present;

    for (const auto& format : formats)
    {
	    if(format.format == vk::Format::eB8G8R8A8Srgb)
	    {
            surfaceFormat = format;
            break;
	    }
    }

    for (const auto& mode : presentModes)
    {
        if (mode == vk::PresentModeKHR::eFifoRelaxed)
        {
            present = mode;
            break;
        }
    }

    assert(surfaceFormat == vk::Format::eUndefined || present.has_value());

    actualSwapChainFormat = surfaceFormat.format;

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    actualSwapChainExtent = vk::Extent2D{ static_cast<u32>(width), static_cast<u32>(height)};
    actualSwapChainExtent.width = std::clamp(actualSwapChainExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
    actualSwapChainExtent.height = std::clamp(actualSwapChainExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);

    u32 imgCount = MAX_FRAMES_IN_FLIGHT;

    if (surfaceCapabilities.maxImageCount > 0 && imgCount > surfaceCapabilities.maxImageCount)
        imgCount = surfaceCapabilities.maxImageCount;

    vk::SwapchainCreateInfoKHR swapchainInfo;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = imgCount;

    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace= surfaceFormat.colorSpace;

    swapchainInfo.presentMode = present.value();
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.imageExtent = actualSwapChainExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    const u32 indicesFamilies[] = { familiesAvailable.graphicsFamily.value_or(-1), familiesAvailable.presentFamily.value_or(-1) };

    if(familiesAvailable.graphicsFamily.value() != familiesAvailable.presentFamily.value())
    {
        swapchainInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        swapchainInfo.queueFamilyIndexCount = 2;
        swapchainInfo.pQueueFamilyIndices = indicesFamilies;
    }
    else
    {
        swapchainInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }

    swapchainInfo.preTransform = surfaceCapabilities.currentTransform;
    swapchainInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque; // don't blend with other windows

    swapchain = logicalDevice.createSwapchainKHR(swapchainInfo);

    swapchainImages = logicalDevice.getSwapchainImagesKHR(swapchain);
}

void VulkanContext::CreateSwapChainViews()
{
    swapChainViews.resize(swapchainImages.size());

    for (u32 i = 0; i < swapChainViews.size(); ++i)
    {
        vk::ImageViewCreateInfo info;
        info.image = swapchainImages[i];
        info.viewType = vk::ImageViewType::e2D;
        info.format = actualSwapChainFormat;

        info.components.a = vk::ComponentSwizzle::eIdentity;
        info.components.r = vk::ComponentSwizzle::eIdentity;
        info.components.g = vk::ComponentSwizzle::eIdentity;
        info.components.b = vk::ComponentSwizzle::eIdentity;

        info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.layerCount = 1;

        swapChainViews[i] = logicalDevice.createImageView(info);
    }
}

void VulkanContext::CreateRenderPass()
{
    vk::AttachmentDescription colorAttachment;
    colorAttachment.format = actualSwapChainFormat;
    colorAttachment.samples = vk::SampleCountFlagBits::e1;

    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;

    // we are not using stencil for now
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR; // todo: change this when you want to use ImGui

    vk::AttachmentReference colorAttachmentRef;
    colorAttachmentRef.attachment = 0; // refers to attachment Description
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef; //this is used in the shader (layout location = 0)

    vk::RenderPassCreateInfo info;
    info.attachmentCount = 1;
    info.pAttachments = &colorAttachment;

    info.subpassCount = 1;
    info.pSubpasses = &subpass;

    vk::SubpassDependency dependency;
    dependency.dstSubpass = 0;
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;

    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask = vk::AccessFlagBits::eNoneKHR;

    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    info.dependencyCount = 1;
    info.pDependencies = &dependency;

	renderPass = logicalDevice.createRenderPass(info);
}

void VulkanContext::CreateDescriptorSetLayout()
{
    vk::DescriptorSetLayoutBinding bindingMatrices;
    bindingMatrices.binding = 0;
    bindingMatrices.descriptorCount = 1;
    bindingMatrices.descriptorType = vk::DescriptorType::eUniformBuffer;
    bindingMatrices.stageFlags = vk::ShaderStageFlagBits::eVertex;

    vk::DescriptorSetLayoutBinding bindingSampler;
    bindingSampler.binding = 1;
    bindingSampler.descriptorCount = 1;
    bindingSampler.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    bindingSampler.stageFlags = vk::ShaderStageFlagBits::eFragment;
    //todo: check if pImmutableSamplers = nullptr is necessary

    const std::array bindings = { bindingSampler, bindingMatrices };

    vk::DescriptorSetLayoutCreateInfo info;
    info.bindingCount = bindings.size();
    info.pBindings = bindings.data();
    //info.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR;

    descriptorSetLayout = logicalDevice.createDescriptorSetLayout(info);
}

void VulkanContext::CreateGraphicsPipeline()
{
    shaderTriangle = new Shader(logicalDevice, "shaders/Triangle");
    vk::PipelineShaderStageCreateInfo infoVert;

    infoVert.module = shaderTriangle->shaderModuleVert;
    infoVert.stage = vk::ShaderStageFlagBits::eVertex;
    infoVert.pName = "main";

    vk::PipelineShaderStageCreateInfo infoFrag;

    infoFrag.module = shaderTriangle->shaderModuleFrag;
    infoFrag.stage = vk::ShaderStageFlagBits::eFragment;
    infoFrag.pName = "main";
    //info.pSpecializationInfo todo: this is to add shader constants at runtime aka shader defines

    vk::PipelineShaderStageCreateInfo shaderStages[] = { infoVert, infoFrag };

    vk::PipelineVertexInputStateCreateInfo vertexInfo;
    //todo: insert here the real data, for now no need as it is hardcoded in the shader

    auto vertexAttribs = triangleVertices.GetAttributesDescription();
    vertexInfo.pVertexAttributeDescriptions = vertexAttribs.data();
    vertexInfo.vertexAttributeDescriptionCount = static_cast<u32>(vertexAttribs.size());

    auto vertexBinding = triangleVertices.GetBindingDescription();
    vertexInfo.pVertexBindingDescriptions = &vertexBinding;
    vertexInfo.vertexBindingDescriptionCount = 1;

    vk::PipelineInputAssemblyStateCreateInfo assemblyInfo;
    assemblyInfo.topology = vk::PrimitiveTopology::eTriangleList;
    assemblyInfo.primitiveRestartEnable = VK_FALSE;

    vk::Viewport viewport;
    viewport.maxDepth = 1.0f;
    viewport.width = static_cast<float>(actualSwapChainExtent.width);
    viewport.height = static_cast<float>(actualSwapChainExtent.height);

    vk::Rect2D scissor;
    scissor.extent = actualSwapChainExtent;

    vk::PipelineViewportStateCreateInfo viewportState;
    viewportState.pScissors = &scissor;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.viewportCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizerInfo;
    rasterizerInfo.depthClampEnable = VK_FALSE;
    rasterizerInfo.rasterizerDiscardEnable = VK_FALSE;

    rasterizerInfo.polygonMode = vk::PolygonMode::eFill;
    rasterizerInfo.lineWidth = 1.0f;

    rasterizerInfo.cullMode = vk::CullModeFlagBits::eNone;
    rasterizerInfo.frontFace = vk::FrontFace::eClockwise;

    rasterizerInfo.depthBiasEnable = VK_FALSE;

    vk::PipelineMultisampleStateCreateInfo msaaInfo;
    //msaaInfo.minSampleShading = 1.0f;
    msaaInfo.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState colorBlendAttachment;
    colorBlendAttachment.colorWriteMask = 
		vk::ColorComponentFlagBits::eA
	|   vk::ColorComponentFlagBits::eB
	| vk::ColorComponentFlagBits::eR
	| vk::ColorComponentFlagBits::eG;
    colorBlendAttachment.blendEnable = VK_FALSE; //we don't blend, no transparent objects are allowed for now

    vk::PipelineColorBlendStateCreateInfo blendInfo;
    blendInfo.attachmentCount = 1;
    blendInfo.logicOpEnable = VK_FALSE;
    blendInfo.logicOp = vk::LogicOp::eCopy;
    blendInfo.pAttachments = &colorBlendAttachment;

    vk::PipelineLayoutCreateInfo layoutInfo;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.setLayoutCount = 1;

    pipelineLayout = logicalDevice.createPipelineLayout(layoutInfo);

    vk::GraphicsPipelineCreateInfo info;

    info.stageCount = 2;
    info.pStages = shaderStages;

    info.pVertexInputState = &vertexInfo;
    info.pInputAssemblyState = &assemblyInfo;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &rasterizerInfo;
    info.pMultisampleState = &msaaInfo;
    info.pDepthStencilState = nullptr; //todo: add this
    info.pColorBlendState = &blendInfo;
    info.pDynamicState = nullptr;

    info.layout = pipelineLayout;

    info.renderPass = renderPass;
    info.subpass = 0;

    info.basePipelineHandle = nullptr;
    info.basePipelineIndex = 0;

    pipeline = logicalDevice.createGraphicsPipeline(nullptr, info).value;
}

void VulkanContext::CreateFramebuffers()
{
    framebuffers.resize(swapChainViews.size());

    for (u32 i = 0; i < swapChainViews.size(); ++i)
    {
        vk::ImageView view = swapChainViews[i];

        vk::FramebufferCreateInfo info;

        info.attachmentCount = 1;
        info.renderPass = renderPass;
        info.pAttachments = &view;
        info.layers = 1;
        info.height = actualSwapChainExtent.height;
        info.width = actualSwapChainExtent.width;

        framebuffers[i] = logicalDevice.createFramebuffer(info);
    }
}

void VulkanContext::CreateCommandPool()
{
    vk::CommandPoolCreateInfo info;

    info.queueFamilyIndex = familiesAvailable.graphicsFamily.value_or(-1);
    commandPoolGraphics = logicalDevice.createCommandPool(info);

    info.queueFamilyIndex = familiesAvailable.transferFamily.value_or(-1);
    commandPoolTransfer = logicalDevice.createCommandPool(info);

    info.flags = vk::CommandPoolCreateFlagBits::eTransient;
    commandPoolTransientResources = logicalDevice.createCommandPool(info);
}

void VulkanContext::LoadEntities()
{
    mesh = new Mesh("models/nanosuit/nanosuit.obj");
}

void VulkanContext::CreateVertexBuffer()
{
    vk::DeviceSize size = sizeof(VerticesDeclarations) * triangleVertices.GetByteSize();

	vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;

    CreateBuffer(size, vk::BufferUsageFlagBits::eTransferSrc
        , vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

    VerticesDeclarations* mappedMem = static_cast<VerticesDeclarations*>(logicalDevice.mapMemory(stagingBufferMemory, 0, size));

    memcpy(mappedMem, triangleVertices.GetData(), size);

    logicalDevice.unmapMemory(stagingBufferMemory);

    CreateBuffer(size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer
        , vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, vertexBuffer, vertexDeviceMemory);

    CopyBuffer(stagingBuffer, vertexBuffer, size);

    logicalDevice.destroyBuffer(stagingBuffer);
    logicalDevice.freeMemory(stagingBufferMemory);
}

void VulkanContext::CreateIndexBuffer()
{
	const vk::DeviceSize size = sizeof(u16) * indices.size();

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;

    CreateBuffer(size, vk::BufferUsageFlagBits::eTransferSrc
        , vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

    u16* mappedMem = static_cast<u16*>(logicalDevice.mapMemory(stagingBufferMemory, 0, size));

    memcpy(mappedMem, indices.data(), size);

    logicalDevice.unmapMemory(stagingBufferMemory);

    CreateBuffer(size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer
        , vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, indexBuffer, indexBufferDeviceMemory);

    CopyBuffer(stagingBuffer, indexBuffer, size);

    logicalDevice.destroyBuffer(stagingBuffer);
    logicalDevice.freeMemory(stagingBufferMemory);
}

void VulkanContext::CreateUniformBuffers()
{
	uboBuffers.resize(swapchainImages.size());
    uboBuffersMemory.resize(swapchainImages.size());

    for (u32 i = 0; i < swapchainImages.size(); ++i)
    {
	    constexpr vk::DeviceSize size = sizeof(UBO);
	    CreateBuffer(size, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
	                 uboBuffers[i], uboBuffersMemory[i]);
    }
}

void VulkanContext::CreateDescriptorPool()
{
    vk::DescriptorPoolSize poolSizeUbo;
    poolSizeUbo.type = vk::DescriptorType::eUniformBuffer;
    poolSizeUbo.descriptorCount = swapchainImages.size();

    vk::DescriptorPoolSize poolSizeSampler;
    poolSizeSampler.type = vk::DescriptorType::eCombinedImageSampler;
    poolSizeSampler.descriptorCount = swapchainImages.size() * mesh->GetSubMeshes().size();

    const std::array poolSizes = { poolSizeUbo, poolSizeSampler};

    vk::DescriptorPoolCreateInfo info;
    info.pPoolSizes = poolSizes.data();
    info.poolSizeCount = poolSizes.size();
    info.maxSets = swapchainImages.size() * mesh->GetSubMeshes().size();

    descriptorPool = logicalDevice.createDescriptorPool(info);
}

void VulkanContext::CreateDescriptorSets()
{
    const std::vector<vk::DescriptorSetLayout> layouts(swapchainImages.size(), descriptorSetLayout);
    vk::DescriptorSetAllocateInfo info;
    info.pSetLayouts = layouts.data();
    info.descriptorPool = descriptorPool;
    info.descriptorSetCount = swapchainImages.size();

    descriptorSets = logicalDevice.allocateDescriptorSets(info);

    for (u32 i = 0; i < swapchainImages.size(); ++i)
    {
        vk::DescriptorBufferInfo bufferInfo;
        bufferInfo.offset = 0;
        bufferInfo.buffer = uboBuffers[i];
        bufferInfo.range = VK_WHOLE_SIZE; // or sizeof(UBO)

        vk::DescriptorImageInfo imageInfo;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        //todo: refactor this
        imageInfo.imageView = mesh->GetSubMeshes()[0]->textures[0].GetView();
        imageInfo.sampler = mesh->GetSubMeshes()[0]->textures[0].GetSampler();

        vk::WriteDescriptorSet writeBuffer;
        writeBuffer.dstSet = descriptorSets[i];
        writeBuffer.dstBinding = 0;
        writeBuffer.dstArrayElement = 0;

        writeBuffer.descriptorType = vk::DescriptorType::eUniformBuffer;
        writeBuffer.descriptorCount = 1;

        writeBuffer.pBufferInfo = &bufferInfo;

        vk::WriteDescriptorSet writeImage;
        writeImage.dstSet = descriptorSets[i];
        writeImage.dstBinding = 1;
        writeImage.dstArrayElement = 0;

        writeImage.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writeImage.descriptorCount = 1;

        writeImage.pImageInfo = &imageInfo;

        std::array writes = { writeImage, writeBuffer };

        logicalDevice.updateDescriptorSets(writes.size(), writes.data(), 0, nullptr);
    }
}

void VulkanContext::CreateCommandBuffers()
{
    commandBuffersGraphics.resize(framebuffers.size());

    vk::CommandBufferAllocateInfo info;

    info.commandPool = commandPoolGraphics;
    info.commandBufferCount = static_cast<u32>(commandBuffersGraphics.size());
    info.level = vk::CommandBufferLevel::ePrimary;

    commandBuffersGraphics = logicalDevice.allocateCommandBuffers(info);

    info.commandPool = commandPoolTransfer;
    info.commandBufferCount = 1;

    commandBuffersTransfer = logicalDevice.allocateCommandBuffers(info);

    for (u32 i = 0; i < commandBuffersGraphics.size(); ++i)
    {
        vk::CommandBufferBeginInfo infoBuffer;
        //infoBuffer.setFlags()

        commandBuffersGraphics[i].begin(infoBuffer);

        vk::RenderPassBeginInfo renderPassBeginInfo;

        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.framebuffer = framebuffers[i];
        renderPassBeginInfo.renderArea.extent = actualSwapChainExtent;
        //renderPassBeginInfo.renderArea.offset

        vk::ClearValue clearValue;
        clearValue.color.setFloat32({ 0.0f, 0.0f, 0.0f, 1.0f });

        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearValue;

        commandBuffersGraphics[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        commandBuffersGraphics[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        const vk::Buffer vertexBuffers[] = { vertexBuffer };
        constexpr vk::DeviceSize offsets[] = { 0 };

        commandBuffersGraphics[i].bindVertexBuffers(0, 1, vertexBuffers, offsets);
        commandBuffersGraphics[i].bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint16);
        commandBuffersGraphics[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, 1
            , &descriptorSets[i], 0, nullptr);

        commandBuffersGraphics[i].drawIndexed(static_cast<u32>(indices.size()), 1, 0, 0, 0);

        //ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffersGraphics[i]);

        commandBuffersGraphics[i].endRenderPass();
        commandBuffersGraphics[i].end();
    }
}

void VulkanContext::CreateSyncObjects()
{
	vk::FenceCreateInfo infoFence;

	fenceImagesInFlight.fill(std::optional<vk::Fence>());

    //to not wait the first time we wait for it
    infoFence.setFlags(vk::FenceCreateFlagBits::eSignaled);

    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
	    constexpr vk::SemaphoreCreateInfo infoSemaphore;
	    semaphoresAcquireImage[i] = logicalDevice.createSemaphore(infoSemaphore);
        semaphoresFinishedRendering[i] = logicalDevice.createSemaphore(infoSemaphore);
        fenceInFlight[i] = logicalDevice.createFence(infoFence);
    }
}

void VulkanContext::UpdateUniformBuffer(const vk::ResultValue<unsigned>& imageIndex) const
{
    const auto startTime = std::chrono::high_resolution_clock::now();

    const auto currentTime = std::chrono::high_resolution_clock::now();

    const float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    UBO ubo;
    ubo.model = glm::rotate(glm::mat4(1.0), glm::radians(90.0f), glm::vec3(1, 0, 0));
    ubo.view = glm::lookAt(glm::vec3(2.0, 2.0, 2.0), glm::vec3(0), glm::vec3(0, 1, 0));
    ubo.proj = glm::perspective(glm::radians(45.0f),
                                static_cast<float>(actualSwapChainExtent.width) / static_cast<float>(actualSwapChainExtent.height), 0.1f,
                                100.0f);

    //flipped y for vulkan
    ubo.proj[1][1] *= -1;

    auto* data = logicalDevice.mapMemory(uboBuffersMemory[imageIndex.value], 0, sizeof(UBO));

    memcpy(data, &ubo, sizeof(UBO));

    logicalDevice.unmapMemory(uboBuffersMemory[imageIndex.value]);
}

void VulkanContext::CleanUpSwapChain()
{
	for (u32 i = 0; i < swapchainImages.size(); ++i)
	{
        logicalDevice.destroyBuffer(uboBuffers[i]);
        logicalDevice.freeMemory(uboBuffersMemory[i]);
	}

    logicalDevice.destroyDescriptorPool(descriptorPool);

    logicalDevice.destroyShaderModule(shaderTriangle->shaderModuleFrag);
    logicalDevice.destroyShaderModule(shaderTriangle->shaderModuleVert);

    delete shaderTriangle;

    logicalDevice.freeCommandBuffers(commandPoolGraphics, commandBuffersGraphics);
    logicalDevice.freeCommandBuffers(commandPoolTransfer, commandBuffersTransfer);

    DestroyFramebuffers();
    logicalDevice.destroyPipeline(pipeline);
    logicalDevice.destroyPipelineLayout(pipelineLayout);
    logicalDevice.destroyRenderPass(renderPass);

    DestroySwapChainImageViews();
    DestroySwapChain();
}

void VulkanContext::RecreateSwapChain()
{
    int width, height;

    glfwGetFramebufferSize(window, &width, &height);

    //handle minimized window
    while(width == 0 || height == 0)
    {
        glfwWaitEvents();
        glfwGetFramebufferSize(window, &width, &height);
    }

    logicalDevice.waitIdle();

    CleanUpSwapChain();

    CreateSwapChain();
    CreateSwapChainViews();
    CreateRenderPass();
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateCommandBuffers();
}

void VulkanContext::DestroyFramebuffers()const
{
	for (const auto& framebuffer : framebuffers)
	{
        logicalDevice.destroyFramebuffer(framebuffer);
	}
}

void VulkanContext::DestroySwapChainImageViews() const 
{
	for (const auto& view : swapChainViews)
	{
        logicalDevice.destroyImageView(view);
	}
}

void VulkanContext::DestroySwapChain() const
{
    vkDestroySwapchainKHR(logicalDevice, swapchain, nullptr);
}

void VulkanContext::DestroyInstance() const
{
    instance.destroy();
}

void VulkanContext::DestroyDevice() const
{
    logicalDevice.destroy();
}

auto VulkanContext::FindMemoryType(u32 typeFilter, vk::MemoryPropertyFlags flags) const -> u32
{
	const auto memProperties = physicalDevice.getMemoryProperties();

    for (u32 i = 0; i < memProperties.memoryTypeCount; ++i)
    {
	    if(typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & flags) == flags)
	    {
            return i;
	    }
    }

    assert(false);

    return -1;
}

void VulkanContext::CreateBuffer(vk::DeviceSize _size, vk::BufferUsageFlags _usage, vk::MemoryPropertyFlags _property,
                                 vk::Buffer& _buffer, vk::DeviceMemory& bufferMemory)
{
    vk::BufferCreateInfo bufferInfo;

    bufferInfo.size = _size;
    bufferInfo.usage = _usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    _buffer = logicalDevice.createBuffer(bufferInfo);

   /* vma::AllocationCreateInfo info;
    info.usage = vma::MemoryUsage::eCpuCopy;
	VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    vmaCreateBuffer(s_allocator, reinterpret_cast<VkBufferCreateInfo*>(&bufferInfo), &allocInfo
        , reinterpret_cast<VkBuffer*>(&vertexBuffer), &vertexAlloc, nullptr);*/


    const auto memReq = logicalDevice.getBufferMemoryRequirements(_buffer);

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, _property);

    bufferMemory = logicalDevice.allocateMemory(allocInfo);
    logicalDevice.bindBufferMemory(_buffer, bufferMemory, 0);
}

void VulkanContext::CreateImage(u32 _width, u32 _height, vk::Format _format, vk::ImageTiling _tiling,
	vk::ImageUsageFlags _usage, vk::MemoryPropertyFlags _property, vk::Image& _image, vk::DeviceMemory& _memory)
{
    vk::ImageCreateInfo imageInfo;
    imageInfo.extent.height = _height;
    imageInfo.extent.width = _width;
    imageInfo.extent.depth = 1;

    //todo: handle different type of textures
    imageInfo.format = _format;
    imageInfo.tiling = _tiling;
    //imageInfo.flags = vk::ImageCreateFlagBits::
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;

    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.usage = _usage;

    _image = logicalDevice.createImage(imageInfo);

    const auto memRequirement = logicalDevice.getImageMemoryRequirements(_image);

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memRequirement.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirement.memoryTypeBits, _property);

    _memory = logicalDevice.allocateMemory(allocInfo);
    logicalDevice.bindImageMemory(_image, _memory, 0);
}

vk::ImageView VulkanContext::CreateImageView(const vk::Image& image, vk::Format format) const
{
    vk::ImageViewCreateInfo info;
    info.format = format;
    info.image = image;
    info.viewType = vk::ImageViewType::e2D;
    info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.layerCount = 1;
    info.subresourceRange.levelCount = 1;

    return logicalDevice.createImageView(info);
}

vk::Sampler VulkanContext::CreateTextureSampler() const
{
    vk::SamplerCreateInfo info;
    info.magFilter = vk::Filter::eLinear;
    info.minFilter = vk::Filter::eLinear;
    info.addressModeU = vk::SamplerAddressMode::eRepeat;
    info.addressModeV = vk::SamplerAddressMode::eRepeat;
    info.addressModeW = vk::SamplerAddressMode::eRepeat;
    info.anisotropyEnable = true;

    const auto properties = physicalDevice.getProperties();
    const auto maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    info.maxAnisotropy = maxAnisotropy;
    info.anisotropyEnable = VK_FALSE; //todo: enable this, maybe in the logicaldevice creation ?

    info.borderColor = vk::BorderColor::eFloatOpaqueBlack;

    info.unnormalizedCoordinates = false;

    info.compareEnable = false;
    info.compareOp = vk::CompareOp::eAlways;

    info.mipmapMode = vk::SamplerMipmapMode::eLinear;
    info.mipLodBias = 0.0f;
    info.minLod = 0;
    info.maxLod = 0;

    return logicalDevice.createSampler(info);
}

void VulkanContext::CopyBuffer(vk::Buffer _srcBuffer, vk::Buffer _dstBuffer, vk::DeviceSize _size) const
{
    auto cmd = BeginSingleTimeCommands();

    vk::BufferCopy copy;
    copy.size = _size;
    copy.dstOffset = 0; //todo: add the possibility to copy sub parts of buffers
    copy.srcOffset = 0;

    cmd.copyBuffer(_srcBuffer, _dstBuffer, copy);

    EndSingleTimeCommands(cmd);
}

void VulkanContext::TransitionImageLayout(const vk::Image& _image, vk::ImageLayout _oldLayout,
                                          vk::ImageLayout _newLayout) const
{
    vk::CommandBuffer cmd = BeginSingleTimeCommands();

    vk::ImageMemoryBarrier barrier;
    barrier.image = _image;
    barrier.oldLayout = _oldLayout;
    barrier.newLayout = _newLayout;

    //used to transfer queue ownership
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    vk::PipelineStageFlagBits sourceStage;
    vk::PipelineStageFlagBits destinationStage;

    if(_oldLayout == vk::ImageLayout::eUndefined && _newLayout == vk::ImageLayout::eTransferDstOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eNoneKHR;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if(_oldLayout == vk::ImageLayout::eTransferDstOptimal && _newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    else
    {
        assert(0);
    }

    cmd.pipelineBarrier(sourceStage, destinationStage
        , vk::DependencyFlagBits::eByRegion, 0, 0, barrier);

    EndSingleTimeCommands(cmd);
}

void VulkanContext::CopyBufferToImage(vk::Buffer _buffer, vk::Image _image, u32 _width, u32 _height) const
{
    vk::CommandBuffer cmd = BeginSingleTimeCommands();

    vk::BufferImageCopy copyInfo;
    copyInfo.bufferImageHeight = 0;
    copyInfo.bufferRowLength = 0;
    copyInfo.bufferOffset = 0;

    copyInfo.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    copyInfo.imageSubresource.baseArrayLayer = 0;
    copyInfo.imageSubresource.layerCount = 1;
    copyInfo.imageSubresource.mipLevel = 0;

    copyInfo.imageExtent = vk::Extent3D(_width, _height, 1);
    copyInfo.imageOffset = vk::Offset3D( 0, 0, 0 );

    cmd.copyBufferToImage(_buffer, _image, vk::ImageLayout::eTransferDstOptimal, copyInfo);

    EndSingleTimeCommands(cmd);
}

vk::CommandBuffer VulkanContext::BeginSingleTimeCommands() const
{
    vk::CommandBufferAllocateInfo infoBufferAlloc;
    infoBufferAlloc.commandPool = commandPoolGraphics;
    infoBufferAlloc.commandBufferCount = 1;

    const auto cmd = logicalDevice.allocateCommandBuffers(infoBufferAlloc);

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    cmd[0].begin(beginInfo);

    return cmd[0];
}

void VulkanContext::EndSingleTimeCommands(vk::CommandBuffer& _commandBuffer) const
{
    _commandBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_commandBuffer;

    graphicsQueue.submit(submitInfo);
    graphicsQueue.waitIdle();

    logicalDevice.freeCommandBuffers(commandPoolGraphics, _commandBuffer);
}

void VulkanContext::DestroySurface() const
{
    vkDestroySurfaceKHR(instance, surface, nullptr);
}
