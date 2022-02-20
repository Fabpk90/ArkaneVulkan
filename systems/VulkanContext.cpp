#include "VulkanContext.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <iostream>
#include <optional>

#include "../Camera.h"
#include "../Mesh.h"
#include "../Shader.h"
#include "SystemManager.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_vulkan.h"
#include "vma/vk_mem_alloc.hpp"

vma::Allocator VulkanContext::s_allocator = nullptr;
VulkanContext* VulkanContext::GraphicInstance = nullptr;

//TODO: handle resizing with glfw callbacks

void VulkanContext::Init()
{
    InitWindow();
    InitVulkan();
}

void VulkanContext::Start()
{

}

void VulkanContext::Update()
{
    DrawLoop();
}

VulkanContext::~VulkanContext()
{
    CleanUpSwapChain();

    delete m_camera;

    m_logicalDevice.destroyDescriptorSetLayout(m_descriptorSetLayout);

    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        m_logicalDevice.destroySemaphore(m_semaphoresAcquireImage[i]);
        m_logicalDevice.destroySemaphore(m_semaphoresFinishedRendering[i]);
        m_logicalDevice.destroyFence(m_fenceInFlight[i]);
    }

    m_logicalDevice.destroyCommandPool(m_commandPoolGraphics);
    m_logicalDevice.destroyCommandPool(m_commandPoolTransfer);
    m_logicalDevice.destroyCommandPool(m_commandPoolTransientResources);
    m_logicalDevice.destroyCommandPool(m_commandPoolOneTimeCmd);

    //imgui
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_logicalDevice.destroyRenderPass(m_imguiRenderPass);
    m_logicalDevice.destroyDescriptorPool(m_imguiDescriptorPool);

    DestroySurface();

    DestroyDevice();
    DestroyInstance();

    vmaDestroyAllocator(s_allocator);

    glfwDestroyWindow(m_window);

    glfwTerminate();
}

void VulkanContext::InitWindow()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_window = glfwCreateWindow(1280, 720, "Vulkan Render", nullptr, nullptr);

    const auto extensions = vk::enumerateInstanceExtensionProperties();

    for (const auto& extension : extensions)
    {
        std::cout << extension.extensionName << " available" << std::endl;
    }

    m_camera = new Camera(vec3(0, 0, 0));
}

void VulkanContext::InitVulkan()
{
    CreateInstance();
    CreatePhysicalDevice();
    CreateLogicalDevice();

    CreateMemPool();
    CreateCommandPool();

    LoadEntities();

    CreateSwapChain();
    CreateSwapChainViews();
    CreateDepthResources();
    m_renderPass = CreateRenderPass(true, true, false, true);
    CreateDescriptorSetLayout();
    CreateGraphicsPipeline();
    CreateFramebuffers();

    CreateImGuiResources();

    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateCommandBuffers(0);
    CreateSyncObjects();
}

void VulkanContext::DrawFrame()
{
    //wait for fences, to make sure we will not use res from fb[0] when going back (with %)
    auto resFence = m_logicalDevice.waitForFences(m_fenceInFlight[m_currentFrame], true, UINT64_MAX);

    //1) acquire image from swapchain
    //2) execute command buffer
    //3 send result to swap chain

    //1)
    const auto imageIndex = m_logicalDevice.acquireNextImageKHR(m_swapchain, UINT64_MAX, m_semaphoresAcquireImage[m_currentFrame]);

    if(imageIndex.result == vk::Result::eErrorOutOfDateKHR)
    {
        RecreateSwapChain();
        return;
    }

    //if we want to use an image still in flight
    if (m_fenceImagesInFlight[imageIndex.value].has_value())
    {
        //we make sure it's not in flight anymore
        const auto res = m_logicalDevice.waitForFences(m_fenceImagesInFlight[imageIndex.value].value(), true, UINT64_MAX);

        assert(res == vk::Result::eSuccess);
    }

    m_fenceImagesInFlight[imageIndex.value] = m_fenceInFlight[m_currentFrame];

    UpdateUniformBuffer(imageIndex);

    vk::SubmitInfo submitInfo;

    const vk::Semaphore waitSemaphore[] = {m_semaphoresAcquireImage[m_currentFrame]};
    constexpr vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphore;

    submitInfo.pWaitDstStageMask = waitStages;

    ImGui::Render();

    //todo: check why docking is crashing
    //i think it's because we need a render pass only for it
    // yes ?

    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();

    const std::array cmds = { m_commandBuffersGraphics[imageIndex.value] };

    submitInfo.commandBufferCount = cmds.size();
    submitInfo.pCommandBuffers = cmds.data();

    const vk::Semaphore semaphoreToSignal[] = {m_semaphoresFinishedRendering[m_currentFrame]};

    //this will send a signal once executed
    submitInfo.pSignalSemaphores = semaphoreToSignal;
    submitInfo.signalSemaphoreCount = 1;

    m_logicalDevice.resetFences(m_fenceInFlight[m_currentFrame]);
    CreateCommandBuffers(m_currentFrame);
    //2)
    m_graphicsQueue.submit(submitInfo, m_fenceInFlight[m_currentFrame]);

    vk::PresentInfoKHR presentInfo;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = semaphoreToSignal;

    const vk::SwapchainKHR swapchains[] = {m_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex.value;
    //3)
    const auto res = m_presentationQueue.presentKHR(presentInfo);

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
    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();

        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE))
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);

        auto movement = vec3(0);

        if(glfwGetKey(m_window, GLFW_KEY_Z))
        {
            movement.z += 1.0f;
        }
        else if(glfwGetKey(m_window, GLFW_KEY_S))
        {
            movement.z -= 1.0f;
        }

        if (glfwGetKey(m_window, GLFW_KEY_Q))
        {
            movement.x += 1.0f;
        }
        else if (glfwGetKey(m_window, GLFW_KEY_S))
        {
            movement.x -= 1.0f;
        }

        if(glfwGetKey(m_window, GLFW_KEY_SPACE))
        {
            movement.y += 1;
        }

        m_camera->Move(movement * 0.16f);

        //imgui new frame
        ImGui_ImplGlfw_NewFrame();
        ImGui_ImplVulkan_NewFrame();

        ImGui::NewFrame();

        //imgui commands
        ImGui::ShowDemoWindow();

        DrawFrame();

        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    m_logicalDevice.waitIdle();

    SystemManager::instance->SetContinueLooping(false);
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

    m_instance = vk::createInstance(instanceInfo);

    VkSurfaceKHR _surface;
    glfwCreateWindowSurface(m_instance, m_window, nullptr, &_surface);

    m_surface = vk::SurfaceKHR(_surface);
}

void VulkanContext::CreatePhysicalDevice()
{
    //enumerate and pick physical GPU
    const auto physicalDevices = m_instance.enumeratePhysicalDevices();
    m_physicalDevice = physicalDevices[0];
    //todo: should check here for swapchain support

    const auto features = m_physicalDevice.getFeatures();
    assert(features.samplerAnisotropy);
}

void VulkanContext::CreateImGuiResources()
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
   // io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    ImGui::embraceTheDarkness();
    ImGui_ImplGlfw_InitForVulkan(m_window, true);

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

    m_imguiDescriptorPool = m_logicalDevice.createDescriptorPool(descriptorPoolCreateInfo);

    //this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_instance;
    init_info.PhysicalDevice = m_physicalDevice;
    init_info.Device = m_logicalDevice;
    init_info.Queue = m_graphicsQueue;
    init_info.DescriptorPool = m_imguiDescriptorPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    //todo: create a dedicated pass for imgui
    ImGui_ImplVulkan_Init(&init_info, m_renderPass);

    vk::CommandBuffer cmd = BeginSingleTimeCommands();
    ImGui_ImplVulkan_CreateFontsTexture(cmd);
    EndSingleTimeCommands(cmd);

    ImGui_ImplVulkan_DestroyFontUploadObjects();

    m_imguiRenderPass = CreateRenderPass(true, true, true, false);
}

void VulkanContext::CreateLogicalDevice()
{
    m_familiesAvailable = QueueFamilies::FindQueueFamilies(m_physicalDevice, m_surface);

    constexpr float priority = 1.0f;

    std::array<vk::DeviceQueueCreateInfo, 3> infos;

    //graphics queue
	infos[0].pQueuePriorities = &priority;
    infos[0].queueCount = 1;
    infos[0].queueFamilyIndex = m_familiesAvailable.graphicsFamily.value_or(-1);

    // presentation queue
    infos[1].pQueuePriorities = &priority;
    infos[1].queueCount = 1;
    infos[1].queueFamilyIndex = m_familiesAvailable.presentFamily.value_or(-1);

    // transfer queue
    infos[2].pQueuePriorities = &priority;
    infos[2].queueCount = 1;
    infos[2].queueFamilyIndex = m_familiesAvailable.transferFamily.value_or(-1);

    const std::vector extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    const auto layers = m_physicalDevice.enumerateDeviceExtensionProperties();

	for (const auto& layer : layers)
	{
        std::cout << layer.extensionName << std::endl;
	}

    m_logicalDevice = m_physicalDevice.createDevice(vk::DeviceCreateInfo(vk::DeviceCreateFlags(), infos, nullptr, extensions));

    m_graphicsQueue = m_logicalDevice.getQueue(m_familiesAvailable.graphicsFamily.value_or(-1), 0);
    m_presentationQueue = m_logicalDevice.getQueue(m_familiesAvailable.presentFamily.value_or(-1), 0);
    m_transferQueue = m_logicalDevice.getQueue(m_familiesAvailable.transferFamily.value_or(-1), 0);
}

void VulkanContext::CreateMemPool() const
{
    vma::AllocatorCreateInfo allocatorInfo;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_logicalDevice;
    allocatorInfo.instance = m_instance;

    s_allocator = vma::createAllocator(allocatorInfo);
}

void VulkanContext::CreateSwapChain()
{
    //check for capabilities first

    const vk::SurfaceCapabilitiesKHR surfaceCapabilities = m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface);

    const auto formats = m_physicalDevice.getSurfaceFormatsKHR(m_surface);
    const auto presentModes = m_physicalDevice.getSurfacePresentModesKHR(m_surface);

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

    m_actualSwapChainFormat = surfaceFormat.format;

    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);

    m_actualSwapChainExtent = vk::Extent2D{ static_cast<u32>(width), static_cast<u32>(height)};
    m_actualSwapChainExtent.width = std::clamp(m_actualSwapChainExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
    m_actualSwapChainExtent.height = std::clamp(m_actualSwapChainExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);

    u32 imgCount = MAX_FRAMES_IN_FLIGHT;

    if (surfaceCapabilities.maxImageCount > 0 && imgCount > surfaceCapabilities.maxImageCount)
        imgCount = surfaceCapabilities.maxImageCount;

    vk::SwapchainCreateInfoKHR swapchainInfo;
    swapchainInfo.surface = m_surface;
    swapchainInfo.minImageCount = imgCount;

    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace= surfaceFormat.colorSpace;

    swapchainInfo.presentMode = present.value();
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.imageExtent = m_actualSwapChainExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    const u32 indicesFamilies[] = { m_familiesAvailable.graphicsFamily.value_or(-1), m_familiesAvailable.presentFamily.value_or(-1) };

    if(m_familiesAvailable.graphicsFamily.value() != m_familiesAvailable.presentFamily.value())
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

    m_swapchain = m_logicalDevice.createSwapchainKHR(swapchainInfo);

    m_swapchainImages = m_logicalDevice.getSwapchainImagesKHR(m_swapchain);
}

void VulkanContext::CreateSwapChainViews()
{
    m_swapChainViews.resize(m_swapchainImages.size());

    for (u32 i = 0; i < m_swapChainViews.size(); ++i)
    {
        vk::ImageViewCreateInfo info;
        info.image = m_swapchainImages[i];
        info.viewType = vk::ImageViewType::e2D;
        info.format = m_actualSwapChainFormat;

        info.components.a = vk::ComponentSwizzle::eIdentity;
        info.components.r = vk::ComponentSwizzle::eIdentity;
        info.components.g = vk::ComponentSwizzle::eIdentity;
        info.components.b = vk::ComponentSwizzle::eIdentity;

        info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.layerCount = 1;

        m_swapChainViews[i] = m_logicalDevice.createImageView(info);
    }
}

void VulkanContext::CreateDepthResources()
{
    m_swapchainDepthImages.resize(m_swapchainImages.size());
    m_swapchainDepthImagesMemory.resize(m_swapchainImages.size());
    m_swapchainDepthImagesViews.resize(m_swapchainImages.size());

    for (int i = 0; i < m_swapchainDepthImages.size(); ++i)
    {
        CreateImage(m_actualSwapChainExtent.width, m_actualSwapChainExtent.height, vk::Format::eD32Sfloat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment
            , vk::MemoryPropertyFlagBits::eDeviceLocal, m_swapchainDepthImages[i], m_swapchainDepthImagesMemory[i]);

        m_swapchainDepthImagesViews[i] = CreateImageView(m_swapchainDepthImages[i], vk::Format::eD32Sfloat, vk::ImageAspectFlagBits::eDepth);
    }

    
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

    m_descriptorSetLayout = m_logicalDevice.createDescriptorSetLayout(info);
}

void VulkanContext::CreateGraphicsPipeline()
{
    m_shaderTriangle = new Shader(m_logicalDevice, "shaders/Mesh");
    m_shaderTriangle->Reflect();
    vk::PipelineShaderStageCreateInfo infoVert;

    infoVert.module = m_shaderTriangle->shaderModuleVert;
    infoVert.stage = vk::ShaderStageFlagBits::eVertex;
    infoVert.pName = "main";

    vk::PipelineShaderStageCreateInfo infoFrag;

    infoFrag.module = m_shaderTriangle->shaderModuleFrag;
    infoFrag.stage = vk::ShaderStageFlagBits::eFragment;
    infoFrag.pName = "main";
    //info.pSpecializationInfo todo: this is to add shader constants at runtime aka shader defines

    vk::PipelineShaderStageCreateInfo shaderStages[] = { infoVert, infoFrag };

    vk::PipelineVertexInputStateCreateInfo vertexInfo;

    auto vertexAttribs = m_mesh->GetSubMeshes()[0]->vertices.GetAttributesDescription();
    vertexInfo.pVertexAttributeDescriptions = vertexAttribs.data();
    vertexInfo.vertexAttributeDescriptionCount = static_cast<u32>(vertexAttribs.size());

    auto vertexBinding = m_mesh->GetSubMeshes()[0]->vertices.GetBindingDescription();
    vertexInfo.pVertexBindingDescriptions = &vertexBinding;
    vertexInfo.vertexBindingDescriptionCount = 1;

    vk::PipelineInputAssemblyStateCreateInfo assemblyInfo;
    assemblyInfo.topology = vk::PrimitiveTopology::eTriangleList;
    assemblyInfo.primitiveRestartEnable = VK_FALSE;

    vk::Viewport viewport;
    viewport.maxDepth = 1.0f;
    viewport.width = static_cast<float>(m_actualSwapChainExtent.width);
    viewport.height = static_cast<float>(m_actualSwapChainExtent.height);

    vk::Rect2D scissor;
    scissor.extent = m_actualSwapChainExtent;

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
    rasterizerInfo.frontFace = vk::FrontFace::eCounterClockwise;

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

    vk::PipelineDepthStencilStateCreateInfo depthStencilInfo;
    depthStencilInfo.depthTestEnable = VK_TRUE;
    depthStencilInfo.depthWriteEnable = VK_TRUE;
    depthStencilInfo.depthCompareOp = vk::CompareOp::eLess;
    depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
    depthStencilInfo.setStencilTestEnable(false);

    //todo: add stencil also here: make sure the image has stencil as well !

    vk::PipelineLayoutCreateInfo layoutInfo;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.setLayoutCount = 1;

    m_pipelineLayout = m_logicalDevice.createPipelineLayout(layoutInfo);

    vk::GraphicsPipelineCreateInfo info;

    info.stageCount = 2;
    info.pStages = shaderStages;

    info.pVertexInputState = &vertexInfo;
    info.pInputAssemblyState = &assemblyInfo;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &rasterizerInfo;
    info.pMultisampleState = &msaaInfo;
    info.pDepthStencilState = &depthStencilInfo; 
    info.pColorBlendState = &blendInfo;
    info.pDynamicState = nullptr;

    info.layout = m_pipelineLayout;

    info.renderPass = m_renderPass;
    info.subpass = 0;

    info.basePipelineHandle = nullptr;
    info.basePipelineIndex = 0;

    m_pipeline = m_logicalDevice.createGraphicsPipeline(nullptr, info).value;
}

void VulkanContext::CreateFramebuffers()
{
    m_framebuffers.resize(m_swapChainViews.size());

    //TODO: refactor this !!!
    m_commandBuffersGraphics.resize(m_framebuffers.size());

    vk::CommandBufferAllocateInfo info;

    info.commandPool = m_commandPoolGraphics;
    info.commandBufferCount = static_cast<u32>(m_commandBuffersGraphics.size());
    info.level = vk::CommandBufferLevel::ePrimary;

    m_commandBuffersGraphics = m_logicalDevice.allocateCommandBuffers(info);

    info.commandPool = m_commandPoolTransfer;
    info.commandBufferCount = 1;

    m_commandBuffersTransfer = m_logicalDevice.allocateCommandBuffers(info);


    for (u32 i = 0; i < m_swapChainViews.size(); ++i)
    {
        const std::array views = { m_swapChainViews[i], m_swapchainDepthImagesViews[i] };

        vk::FramebufferCreateInfo info;

        info.renderPass = m_renderPass;
        info.attachmentCount = views.size();
        info.pAttachments = views.data();
        info.layers = 1;
        info.height = m_actualSwapChainExtent.height;
        info.width = m_actualSwapChainExtent.width;

        m_framebuffers[i] = m_logicalDevice.createFramebuffer(info);
    }
}

void VulkanContext::CreateCommandPool()
{
    vk::CommandPoolCreateInfo info;

    info.queueFamilyIndex = m_familiesAvailable.graphicsFamily.value_or(-1);
    info.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    m_commandPoolGraphics = m_logicalDevice.createCommandPool(info);

    info.queueFamilyIndex = m_familiesAvailable.transferFamily.value_or(-1);
    m_commandPoolTransfer = m_logicalDevice.createCommandPool(info);

    info.flags = vk::CommandPoolCreateFlagBits::eTransient;
    m_commandPoolTransientResources = m_logicalDevice.createCommandPool(info);

    info.queueFamilyIndex = m_familiesAvailable.graphicsFamily.value_or(-1);
    info.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

    m_commandPoolOneTimeCmd = m_logicalDevice.createCommandPool(info);
}

void VulkanContext::LoadEntities()
{
    m_mesh = new Mesh("assets/meshdesc/mesh.json");
}

std::pair<vk::Buffer, vk::DeviceMemory> VulkanContext::CreateVertexBuffer(const VerticesDeclarations& _decl)
{
    const vk::DeviceSize size = static_cast<vk::DeviceSize>(_decl.GetElementCount()) * _decl.GetByteSize();

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;

    CreateBuffer(size, vk::BufferUsageFlagBits::eTransferSrc
        , vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

    const auto mappedMem = static_cast<VerticesDeclarations*>(m_logicalDevice.mapMemory(stagingBufferMemory, 0, size));

    memcpy(mappedMem, _decl.GetData(), size);

    m_logicalDevice.unmapMemory(stagingBufferMemory);

    vk::Buffer buffer;
    vk::DeviceMemory memory;

    CreateBuffer(size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer
        , vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, buffer, memory);

    CopyBuffer(stagingBuffer, buffer, size);

    m_logicalDevice.destroyBuffer(stagingBuffer);
    m_logicalDevice.freeMemory(stagingBufferMemory);

    return { buffer, memory };
}

std::pair<vk::Buffer, vk::DeviceMemory> VulkanContext::CreateIndexBuffer(const std::vector<u16>& indices)
{
	const vk::DeviceSize size = sizeof(u16) * indices.size();

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingBufferMemory;

    vk::Buffer indexBuffer;
    vk::DeviceMemory indexBufferMemory;

    CreateBuffer(size, vk::BufferUsageFlagBits::eTransferSrc
        , vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingBufferMemory);

    u16* mappedMem = static_cast<u16*>(m_logicalDevice.mapMemory(stagingBufferMemory, 0, size));

    memcpy(mappedMem, indices.data(), size);

    m_logicalDevice.unmapMemory(stagingBufferMemory);

    CreateBuffer(size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer
        , vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, indexBuffer, indexBufferMemory);

    CopyBuffer(stagingBuffer, indexBuffer, size);

    m_logicalDevice.destroyBuffer(stagingBuffer);
    m_logicalDevice.freeMemory(stagingBufferMemory);

    return { indexBuffer, indexBufferMemory };
}

void VulkanContext::CreateUniformBuffers()
{
	m_uboBuffers.resize(m_swapchainImages.size() * m_mesh->GetSubMeshes().size());
    m_uboBuffersMemory.resize(m_swapchainImages.size() * m_mesh->GetSubMeshes().size());

    for (u32 i = 0; i < m_uboBuffers.size(); ++i)
    {
	    constexpr vk::DeviceSize size = sizeof(UBO);
	    CreateBuffer(size, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
	                 m_uboBuffers[i], m_uboBuffersMemory[i]);
    }
}

void VulkanContext::CreateDescriptorPool()
{
    vk::DescriptorPoolSize poolSizeUbo;
    poolSizeUbo.type = vk::DescriptorType::eUniformBuffer;
    poolSizeUbo.descriptorCount = m_swapchainImages.size() * m_mesh->GetSubMeshes().size();

    vk::DescriptorPoolSize poolSizeSampler;
    poolSizeSampler.type = vk::DescriptorType::eCombinedImageSampler;
    poolSizeSampler.descriptorCount = m_swapchainImages.size() * m_mesh->GetSubMeshes().size();

    const std::array poolSizes = { poolSizeUbo, poolSizeSampler};

    vk::DescriptorPoolCreateInfo info;
    info.pPoolSizes = poolSizes.data();
    info.poolSizeCount = poolSizes.size();
    info.maxSets = m_swapchainImages.size() * m_mesh->GetSubMeshes().size();

    m_descriptorPool = m_logicalDevice.createDescriptorPool(info);
}

void VulkanContext::CreateDescriptorSets()
{
    const std::vector<vk::DescriptorSetLayout> layouts(m_swapchainImages.size() * m_mesh->GetSubMeshes().size(), m_descriptorSetLayout);
    vk::DescriptorSetAllocateInfo info;
    info.pSetLayouts = layouts.data();
    info.descriptorPool = m_descriptorPool;
    info.descriptorSetCount = m_swapchainImages.size() * m_mesh->GetSubMeshes().size();

    m_descriptorSets = m_logicalDevice.allocateDescriptorSets(info);

    for (u32 i = 0; i < m_swapchainImages.size(); ++i)
    {
	    for (u32 j = 0; j < m_mesh->GetSubMeshes().size(); ++j)
	    {
            const auto index = i * m_mesh->GetSubMeshes().size() + j;
            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo.offset = 0;
            bufferInfo.buffer = m_uboBuffers[index];
            bufferInfo.range = VK_WHOLE_SIZE; // or sizeof(UBO)

            vk::DescriptorImageInfo imageInfo;
            imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            //todo: refactor this
            imageInfo.imageView = m_mesh->GetSubMeshes()[j]->textures[0].GetView();
            imageInfo.sampler = m_mesh->GetSubMeshes()[j]->textures[0].GetSampler();

            vk::WriteDescriptorSet writeBuffer;
            writeBuffer.dstSet = m_descriptorSets[index];
            writeBuffer.dstBinding = 0;
            writeBuffer.dstArrayElement = 0;

            writeBuffer.descriptorType = vk::DescriptorType::eUniformBuffer;
            writeBuffer.descriptorCount = 1;

            writeBuffer.pBufferInfo = &bufferInfo;

            vk::WriteDescriptorSet writeImage;
            writeImage.dstSet = m_descriptorSets[index];
            writeImage.dstBinding = 1;
            writeImage.dstArrayElement = 0;

            writeImage.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writeImage.descriptorCount = 1;

            writeImage.pImageInfo = &imageInfo;

            std::array writes = { writeImage, writeBuffer };

            m_logicalDevice.updateDescriptorSets(writes.size(), writes.data(), 0, nullptr);
	    }
        
    }
}

void VulkanContext::CreateCommandBuffers(u32 _frameIndex)
{
    std::array<vk::ClearValue, 2> clearValues;
    clearValues[0].color.setFloat32({ 0.0f, 0.0f, 0.0f, 1.0f }); //color
    clearValues[1].depthStencil.depth = 1.0f; // 1.0 here because vulkan goes [0;1] 1 == far

    //for (u32 i = 0; i < commandBuffersGraphics.size(); ++i)
    {
	    const auto i = _frameIndex;
	    vk::CommandBufferBeginInfo infoBuffer;
        //infoBuffer.setFlags()

        m_commandBuffersGraphics[i].begin(infoBuffer);

        vk::RenderPassBeginInfo renderPassBeginInfo;

        renderPassBeginInfo.renderPass = m_renderPass;
        renderPassBeginInfo.framebuffer = m_framebuffers[i];
        renderPassBeginInfo.renderArea.extent = m_actualSwapChainExtent;
        //renderPassBeginInfo.renderArea.offset

        renderPassBeginInfo.clearValueCount = clearValues.size();
        renderPassBeginInfo.pClearValues = clearValues.data();

        m_commandBuffersGraphics[i].beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        m_commandBuffersGraphics[i].bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline);

        for (int j = 0; j < m_mesh->GetSubMeshes().size(); ++j)
        {
            const vk::Buffer vertexBuffers[] = { m_mesh->GetSubMeshes()[j]->vertices.GetBuffer()};
            constexpr vk::DeviceSize offsets[] = { 0 };

            m_commandBuffersGraphics[i].bindVertexBuffers(0, 1, vertexBuffers, offsets);
            m_commandBuffersGraphics[i].bindIndexBuffer(m_mesh->GetSubMeshes()[j]->indices.GetBuffer(), 0, vk::IndexType::eUint16);
            m_commandBuffersGraphics[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout, 0, 1
                , &m_descriptorSets[i * m_mesh->GetSubMeshes().size() + j], 0, nullptr);

            m_commandBuffersGraphics[i].drawIndexed(m_mesh->GetSubMeshes()[j]->indices.GetSize(), 1, 0, 0, 0);
        }


        if (ImGui::GetDrawData())
			ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_commandBuffersGraphics[i]);

        m_commandBuffersGraphics[i].endRenderPass();
        m_commandBuffersGraphics[i].end();
    }
}

void VulkanContext::CreateSyncObjects()
{
	vk::FenceCreateInfo infoFence;

	m_fenceImagesInFlight.fill(std::optional<vk::Fence>());

    //to not wait the first time we wait for it
    infoFence.setFlags(vk::FenceCreateFlagBits::eSignaled);

    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
	    constexpr vk::SemaphoreCreateInfo infoSemaphore;
	    m_semaphoresAcquireImage[i] = m_logicalDevice.createSemaphore(infoSemaphore);
        m_semaphoresFinishedRendering[i] = m_logicalDevice.createSemaphore(infoSemaphore);
        m_fenceInFlight[i] = m_logicalDevice.createFence(infoFence);
    }
}

void VulkanContext::UpdateUniformBuffer(const vk::ResultValue<unsigned>& imageIndex) const
{
    const auto startTime = std::chrono::high_resolution_clock::now();

    const auto currentTime = std::chrono::high_resolution_clock::now();

    const float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    UBO ubo{};
    ubo.model = mat4(1.0f);//glm::rotate(glm::mat4(1.0), glm::radians(90.0f), glm::vec3(1, 0, 0));
    ubo.view = m_camera->GetView(); //glm::lookAt(glm::vec3(15.0, 30.0, 15.0), glm::vec3(0), glm::vec3(0, 1, 0));
    ubo.proj = m_camera->GetProjection();//glm::perspective(glm::radians(65.0f),
                                //static_cast<float>(actualSwapChainExtent.width) / static_cast<float>(actualSwapChainExtent.height), 0.1f,
                                //100.0f);

    //flipped y for vulkan
    ubo.proj[1][1] *= -1;

    //todo: add update for all descriptors (one for each submesh), probably use some push constants/ push once and update once

    for (u32 i = 0; i < m_mesh->GetSubMeshes().size(); ++i)
    {
        auto* data = m_logicalDevice.mapMemory(m_uboBuffersMemory[imageIndex.value * m_mesh->GetSubMeshes().size() + i], 0, sizeof(UBO));

        memcpy(data, &ubo, sizeof(UBO));

        m_logicalDevice.unmapMemory(m_uboBuffersMemory[imageIndex.value * m_mesh->GetSubMeshes().size() + i]);
    }
}

void VulkanContext::DestroyDepthResources()
{
	for (int i = 0; i < m_swapchainDepthImages.size(); ++i)
	{
		m_logicalDevice.destroyImageView(m_swapchainDepthImagesViews[i]);
        m_logicalDevice.freeMemory(m_swapchainDepthImagesMemory[i]);
        m_logicalDevice.destroyImage(m_swapchainDepthImages[i]);
	}

    m_swapchainDepthImages.clear();
    m_swapchainDepthImagesMemory.clear();
    m_swapchainDepthImagesViews.clear();
}

void VulkanContext::CleanUpSwapChain()
{
	for (u32 i = 0; i < m_uboBuffers.size(); ++i)
	{
        m_logicalDevice.destroyBuffer(m_uboBuffers[i]);
        m_logicalDevice.freeMemory(m_uboBuffersMemory[i]);
	}

    m_logicalDevice.destroyDescriptorPool(m_descriptorPool);

    m_logicalDevice.destroyShaderModule(m_shaderTriangle->shaderModuleFrag);
    m_logicalDevice.destroyShaderModule(m_shaderTriangle->shaderModuleVert);

    delete m_shaderTriangle;

    m_logicalDevice.freeCommandBuffers(m_commandPoolGraphics, m_commandBuffersGraphics);
    m_logicalDevice.freeCommandBuffers(m_commandPoolTransfer, m_commandBuffersTransfer);

    DestroyFramebuffers();
    m_logicalDevice.destroyPipeline(m_pipeline);
    m_logicalDevice.destroyPipelineLayout(m_pipelineLayout);
    m_logicalDevice.destroyRenderPass(m_renderPass);

    DestroySwapChainImageViews();
    DestroySwapChain();
    DestroyDepthResources();
}

void VulkanContext::RecreateSwapChain()
{
    int width, height;

    glfwGetFramebufferSize(m_window, &width, &height);

    //handle minimized window
    while(width == 0 || height == 0)
    {
        glfwWaitEvents();
        glfwGetFramebufferSize(m_window, &width, &height);
    }

    m_logicalDevice.waitIdle();

    CleanUpSwapChain();

    CreateSwapChain();
    CreateSwapChainViews();
    CreateDepthResources();
    m_renderPass = CreateRenderPass(true, true, false, true);
    CreateGraphicsPipeline();
    CreateFramebuffers();
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateCommandBuffers(m_currentFrame);
}

void VulkanContext::DestroyFramebuffers()const
{
	for (const auto& framebuffer : m_framebuffers)
	{
        m_logicalDevice.destroyFramebuffer(framebuffer);
	}
}

void VulkanContext::DestroySwapChainImageViews() const 
{
	for (const auto& view : m_swapChainViews)
	{
        m_logicalDevice.destroyImageView(view);
	}
}

void VulkanContext::DestroySwapChain() const
{
    vkDestroySwapchainKHR(m_logicalDevice, m_swapchain, nullptr);
}

void VulkanContext::DestroyInstance() const
{
    m_instance.destroy();
}

void VulkanContext::DestroyDevice() const
{
    m_logicalDevice.destroy();
}

auto VulkanContext::FindMemoryType(u32 _typeFilter, vk::MemoryPropertyFlags _flags) const -> u32
{
	const auto memProperties = m_physicalDevice.getMemoryProperties();

    for (u32 i = 0; i < memProperties.memoryTypeCount; ++i)
    {
	    if(_typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & _flags) == _flags)
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

    _buffer = m_logicalDevice.createBuffer(bufferInfo);

   /* vma::AllocationCreateInfo info;
    info.usage = vma::MemoryUsage::eCpuCopy;
	VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    vmaCreateBuffer(s_allocator, reinterpret_cast<VkBufferCreateInfo*>(&bufferInfo), &allocInfo
        , reinterpret_cast<VkBuffer*>(&vertexBuffer), &vertexAlloc, nullptr);*/


    const auto memReq = m_logicalDevice.getBufferMemoryRequirements(_buffer);

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, _property);

    bufferMemory = m_logicalDevice.allocateMemory(allocInfo);
    m_logicalDevice.bindBufferMemory(_buffer, bufferMemory, 0);
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

    _image = m_logicalDevice.createImage(imageInfo);

    const auto memRequirement = m_logicalDevice.getImageMemoryRequirements(_image);

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memRequirement.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirement.memoryTypeBits, _property);

    _memory = m_logicalDevice.allocateMemory(allocInfo);
    m_logicalDevice.bindImageMemory(_image, _memory, 0);
}

vk::ImageView VulkanContext::CreateImageView(const vk::Image& image, vk::Format format, vk::ImageAspectFlagBits aspectFlag) const
{
    vk::ImageViewCreateInfo info;
    info.format = format;
    info.image = image;
    info.viewType = vk::ImageViewType::e2D;
    info.subresourceRange.aspectMask = aspectFlag;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.layerCount = 1;
    info.subresourceRange.levelCount = 1;

    return m_logicalDevice.createImageView(info);
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

    const auto properties = m_physicalDevice.getProperties();
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

    return m_logicalDevice.createSampler(info);
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

vk::RenderPass VulkanContext::CreateRenderPass(bool _useColor, bool _useDepth, bool _blend, bool _isLastRenderPass)
{
    vk::AttachmentDescription colorAttachment;
    colorAttachment.format = m_actualSwapChainFormat;
    colorAttachment.samples = vk::SampleCountFlagBits::e1;

    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;

    // we are not using stencil for now
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = _isLastRenderPass ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference colorAttachmentRef;
    colorAttachmentRef.attachment = 0; // refers to attachment Description
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef; //this is used in the shader (layout location = 0)

    std::vector<vk::AttachmentDescription> attachments;
    attachments.emplace_back(colorAttachment);

    vk::AttachmentDescription depthAttachment;
    if(_useDepth)
    {
        depthAttachment.format = vk::Format::eD32Sfloat;
        depthAttachment.samples = vk::SampleCountFlagBits::e1;

        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;

        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

        depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthAttachmentRef;
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        attachments.emplace_back(depthAttachment);
    }

    vk::RenderPassCreateInfo info;
    info.attachmentCount = attachments.size();
    info.pAttachments = attachments.data();

    info.subpassCount = 1;
    info.pSubpasses = &subpass;

    vk::SubpassDependency dependency;
    dependency.dstSubpass = 0;
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;

    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | (_useDepth ? vk::PipelineStageFlagBits::eEarlyFragmentTests : vk::PipelineStageFlagBits::eNoneKHR);
    dependency.srcAccessMask = vk::AccessFlagBits::eNoneKHR;

    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | (_useDepth ? vk::PipelineStageFlagBits::eEarlyFragmentTests : vk::PipelineStageFlagBits::eNoneKHR);
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | ( _useDepth ? vk::AccessFlagBits::eDepthStencilAttachmentWrite : vk::AccessFlagBits::eNoneKHR);

    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    return m_logicalDevice.createRenderPass(info);
}

vk::CommandBuffer VulkanContext::BeginSingleTimeCommands() const
{
    vk::CommandBufferAllocateInfo infoBufferAlloc;
    infoBufferAlloc.commandPool = m_commandPoolOneTimeCmd;
    infoBufferAlloc.commandBufferCount = 1;

    const auto cmd = m_logicalDevice.allocateCommandBuffers(infoBufferAlloc);

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

    m_graphicsQueue.submit(submitInfo);
    m_graphicsQueue.waitIdle();

    m_logicalDevice.freeCommandBuffers(m_commandPoolOneTimeCmd, _commandBuffer);
}

void VulkanContext::DestroySurface() const
{
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
}
