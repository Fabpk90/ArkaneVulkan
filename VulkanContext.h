#pragma once
#include "vma/vk_mem_alloc.hpp"

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.hpp>
// TODO: add this when ready #include <vulkan/vulkan_raii.hpp>

#include <GLFW/glfw3.h>

#include <optional>

#include "Shader.h"


#include <glm/glm.hpp>

#include "VerticesDeclarations.h"

const std::vector<const char*> validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
	const bool enableValidationLayers = false;
#else
	const bool enableValidationLayers = true;
#endif


class Mesh;

//todo: move to eastl
//todo: cache the families indices
//todo: use vma to allocate a big chunk of memory, and sub allocate after. To do that, have a function that converts vulkan memory type to vma ones (see if .hpp has it)

using namespace glm;

static constexpr u32 MAX_FRAMES_IN_FLIGHT = 1;

class VulkanContext
{
public:
	VulkanContext() { GraphicInstance = this; };
	~VulkanContext() = default;

	void InitWindow();
	void InitVulkan();
	void DrawFrame();
	void DrawLoop();
	void Destroy();

	void CreateBuffer(vk::DeviceSize _size, vk::BufferUsageFlags _usage, vk::MemoryPropertyFlags _property, vk::Buffer& _buffer, vk::DeviceMemory& bufferMemory);
	void CreateImage(u32 _width, u32 _height, vk::Format _format, vk::ImageTiling _tiling, vk::ImageUsageFlags _usage, vk::MemoryPropertyFlags _property, vk::Image& _image, vk::DeviceMemory& _memory);
	vk::ImageView CreateImageView(const vk::Image& image, vk::Format format) const;
	[[nodiscard]] vk::Sampler CreateTextureSampler() const;
	void CopyBuffer(vk::Buffer _srcBuffer, vk::Buffer _dstBuffer, vk::DeviceSize _size) const;
	void TransitionImageLayout(const vk::Image& _image, vk::ImageLayout _oldLayout, vk::ImageLayout _newLayout) const;
	void CopyBufferToImage(vk::Buffer _buffer, vk::Image _image, u32 _width, u32 _height) const;

	[[nodiscard]] vk::CommandBuffer BeginSingleTimeCommands() const;
	void EndSingleTimeCommands(vk::CommandBuffer& _commandBuffer) const;

	static VulkanContext* GraphicInstance;
	static vma::Allocator s_allocator;

	vk::Device& GetLogicalDevice() { return logicalDevice; }
	vk::PhysicalDevice& GetPhysicalDevice() { return physicalDevice; }

private:
	bool CheckValidationSupport() const;
	void CreateInstance();
	void CreatePhysicalDevice();
	void CreateImGuiResources();
	void CreateLogicalDevice();
	void CreateMemPool() const;
	void CreateSwapChain();
	void CreateSwapChainViews();
	void CreateRenderPass();
	void CreateDescriptorSetLayout();
	void CreateGraphicsPipeline();
	void CreateFramebuffers();
	void CreateCommandPool();
	void LoadEntities();
	void CreateVertexBuffer();
	void CreateIndexBuffer();
	void CreateUniformBuffers();
	void CreateDescriptorPool();
	void CreateDescriptorSets();
	void CreateCommandBuffers();
	void CreateSyncObjects();

	void UpdateUniformBuffer(const vk::ResultValue<unsigned>& imageIndex) const;

	void CleanUpSwapChain();
	void RecreateSwapChain();

	void DestroyFramebuffers() const;
	void DestroySwapChainImageViews() const;
	void DestroySwapChain() const;
	void DestroySurface() const;
	void DestroyInstance() const;
	void DestroyDevice() const;

	struct QueueFamilies
	{
		std::optional<u32> graphicsFamily;
		std::optional<u32> presentFamily;
		std::optional<u32> transferFamily;
		std::optional<u32> computeFamily;

		bool IsComplete() const
		{
			return graphicsFamily.has_value() && presentFamily.has_value() && transferFamily.has_value()
				&& computeFamily.has_value();
		}

		static QueueFamilies FindQueueFamilies(vk::PhysicalDevice& _physicalDevice, vk::SurfaceKHR _surface = VK_NULL_HANDLE)
		{
			QueueFamilies families;

			const auto familyProp = _physicalDevice.getQueueFamilyProperties();

			// here we choose an else if combo to utilize as much queues as possible
			// good idea, it's required by the standard
			u32 i = 0;
			for (const auto& prop : familyProp)
			{
				if(!families.graphicsFamily.has_value()
					&& prop.queueFlags & vk::QueueFlagBits::eGraphics)
				{
					families.graphicsFamily = i;
				}
				else if(!families.transferFamily.has_value()
					&& prop.queueFlags & vk::QueueFlagBits::eTransfer)
				{
					families.transferFamily = i;
				}
				else if (!families.computeFamily.has_value()
					&& prop.queueFlags & vk::QueueFlagBits::eCompute)
				{
					families.computeFamily = i;
				}

				else if(_surface && !families.presentFamily.has_value())
				{
					const auto res = _physicalDevice.getSurfaceSupportKHR(i, _surface);
					if(!(prop.queueFlags & vk::QueueFlagBits::eGraphics) && res )
					{
						families.presentFamily = i;
					}
				}

				if (families.IsComplete())
					return families;

				i++;

			}

			return families;
		}
	};

	u32 FindMemoryType(u32 typeFilter, vk::MemoryPropertyFlags flags) const;
private:

	GLFWwindow* window;
	vk::Instance instance;
	vk::Device logicalDevice;
	vk::PhysicalDevice physicalDevice;

	QueueFamilies familiesAvailable;

	vk::Queue graphicsQueue;
	vk::Queue presentationQueue;
	vk::Queue transferQueue;

	vk::SurfaceKHR surface;

	vk::SwapchainKHR swapchain;
	std::vector<vk::Image> swapchainImages;
	std::vector<vk::ImageView> swapChainViews;

	std::vector<vk::Framebuffer> framebuffers;

	vk::Extent2D actualSwapChainExtent;
	vk::Format actualSwapChainFormat;

	vk::RenderPass renderPass;

	vk::DescriptorSetLayout descriptorSetLayout;
	vk::PipelineLayout pipelineLayout;

	vk::Pipeline pipeline;

	u32 currentFrame = 0;

	//used for GPU GPU sync
	std::array<vk::Semaphore, MAX_FRAMES_IN_FLIGHT> semaphoresAcquireImage;
	std::array<vk::Semaphore, MAX_FRAMES_IN_FLIGHT> semaphoresFinishedRendering;

	//used for CPU GPU sync
	std::array<vk::Fence, MAX_FRAMES_IN_FLIGHT> fenceInFlight;
	std::array<std::optional<vk::Fence>, MAX_FRAMES_IN_FLIGHT> fenceImagesInFlight;// used to sync if max images in flight is > than the nb of image of the swapchain or if out of order rendering is used

	vk::CommandPool commandPoolGraphics;
	std::vector<vk::CommandBuffer> commandBuffersGraphics;

	vk::CommandPool commandPoolTransfer;
	std::vector<vk::CommandBuffer> commandBuffersTransfer;

	vk::CommandPool commandPoolTransientResources;
	std::vector<vk::CommandBuffer> commandBuffersTransientResources;

	Shader* shaderTriangle;

	const TriangleVertexDecl triangleVertices = TriangleVertexDecl({
		{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
		{{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
		{{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
		{{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}
		});

	const std::vector<uint16_t> indices = 
	{
		0, 1, 2, 2, 3, 0
	};

	struct UBO
	{
		mat4 model;
		mat4 view;
		mat4 proj;
	};

	vk::Buffer vertexBuffer;
	//VmaAllocation vertexAlloc;
	vk::DeviceMemory vertexDeviceMemory;

	vk::Buffer indexBuffer;
	vk::DeviceMemory indexBufferDeviceMemory;

	std::vector<vk::Buffer> uboBuffers;
	std::vector<vk::DeviceMemory> uboBuffersMemory;

	vk::DescriptorPool descriptorPool;
	std::vector<vk::DescriptorSet> descriptorSets;

	Mesh* mesh;

	vk::RenderPass imguiRenderPass;
	vk::DescriptorPool imguiDescriptorPool;
};