#pragma once
#include "vma/vk_mem_alloc.hpp"

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.hpp>
// TODO: add this when ready #include <vulkan/vulkan_raii.hpp>

#include <GLFW/glfw3.h>

#include <optional>

#include <glm/glm.hpp>

#include "ISystem.h"

class Shader;
class Camera;
class VerticesDeclarations;
const std::vector validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
	const bool enableValidationLayers = false;
#else
	const bool enableValidationLayers = true;
#endif


class Mesh;

//todo: cache the families indices
//todo: use vma to allocate a big chunk of memory, and sub allocate after. To do that, have a function that converts vulkan memory type to vma ones (see if .hpp has it)

using namespace glm;

static constexpr u32 MAX_FRAMES_IN_FLIGHT = 3;

class VulkanContext : public ISystem
{
public:
	void Init() override;
	void Start() override;
	void Update() override;
	VulkanContext() { GraphicInstance = this; };
	~VulkanContext() override;

	void InitWindow();
	void InitVulkan();
	void DrawFrame();
	void DrawLoop();

	void CreateBuffer(vk::DeviceSize _size, vk::BufferUsageFlags _usage, vk::MemoryPropertyFlags _property, vk::Buffer& _buffer, vk::DeviceMemory& bufferMemory);
	void CreateImage(u32 _width, u32 _height, vk::Format _format, vk::ImageTiling _tiling, vk::ImageUsageFlags _usage, vk::MemoryPropertyFlags _property, vk::Image& _image, vk::DeviceMemory& _memory);
	[[nodiscard]] vk::ImageView CreateImageView(const vk::Image& image, vk::Format format, vk::ImageAspectFlagBits aspectFlag = vk::ImageAspectFlagBits::eColor) const;
	[[nodiscard]] vk::Sampler CreateTextureSampler() const;
	void CopyBuffer(vk::Buffer _srcBuffer, vk::Buffer _dstBuffer, vk::DeviceSize _size) const;
	void TransitionImageLayout(const vk::Image& _image, vk::ImageLayout _oldLayout, vk::ImageLayout _newLayout) const;
	void CopyBufferToImage(vk::Buffer _buffer, vk::Image _image, u32 _width, u32 _height) const;

	[[nodiscard]] vk::RenderPass CreateRenderPass(bool _useColor, bool _useDepth, bool _blend, bool _isLastRenderPass);

	[[nodiscard]] std::pair<vk::Buffer, vk::DeviceMemory> CreateVertexBuffer(const VerticesDeclarations& _decl);
	[[nodiscard]] std::pair<vk::Buffer, vk::DeviceMemory> CreateIndexBuffer(const std::vector<u16>& indices);

	[[nodiscard]] vk::CommandBuffer BeginSingleTimeCommands() const;
	void EndSingleTimeCommands(vk::CommandBuffer& _commandBuffer) const;

	static VulkanContext* GraphicInstance;
	static vma::Allocator s_allocator;

	vk::Device& GetLogicalDevice() { return m_logicalDevice; }
	vk::PhysicalDevice& GetPhysicalDevice() { return m_physicalDevice; }

	[[nodiscard]] vk::Extent2D GetWindowSize() const { return m_actualSwapChainExtent; }

private:
	[[nodiscard]] bool CheckValidationSupport() const;
	void CreateInstance();
	void CreatePhysicalDevice();
	void CreateImGuiResources();
	void CreateLogicalDevice();
	void CreateMemPool() const;
	void CreateSwapChain();
	void CreateSwapChainViews();
	void CreateDepthResources();
	void CreateDescriptorSetLayout();
	void CreateGraphicsPipeline();
	void CreateFramebuffers();
	void CreateCommandPool();
	void LoadEntities();
	void CreateUniformBuffers();
	void CreateDescriptorPool();
	void CreateDescriptorSets();
	void CreateCommandBuffers(u32 _frameIndex);
	void CreateSyncObjects();

	void UpdateUniformBuffer(const vk::ResultValue<unsigned>& imageIndex) const;

	void DestroyDepthResources();
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

	[[nodiscard]] u32 FindMemoryType(u32 _typeFilter, vk::MemoryPropertyFlags _flags) const;
private:

	GLFWwindow* m_window{};
	vk::Instance m_instance;
	vk::Device m_logicalDevice;
	vk::PhysicalDevice m_physicalDevice;

	QueueFamilies m_familiesAvailable;

	vk::Queue m_graphicsQueue;
	vk::Queue m_presentationQueue;
	vk::Queue m_transferQueue;

	vk::SurfaceKHR m_surface;

	vk::SwapchainKHR m_swapchain;
	std::vector<vk::Image> m_swapchainImages;
	std::vector<vk::ImageView> m_swapChainViews;

	//this could be without a vector of them
	//because only une subpass is running, due to the semaphores used
	std::vector<vk::Image> m_swapchainDepthImages;
	std::vector<vk::ImageView> m_swapchainDepthImagesViews;
	std::vector<vk::DeviceMemory> m_swapchainDepthImagesMemory;

	std::vector<vk::Framebuffer> m_framebuffers;

	vk::Extent2D m_actualSwapChainExtent;
	vk::Format m_actualSwapChainFormat;

	vk::RenderPass m_renderPass;

	vk::DescriptorSetLayout m_descriptorSetLayout;
	vk::PipelineLayout m_pipelineLayout;

	vk::Pipeline m_pipeline;

	u32 m_currentFrame = 0;

	//used for GPU GPU sync
	std::array<vk::Semaphore, MAX_FRAMES_IN_FLIGHT> m_semaphoresAcquireImage;
	std::array<vk::Semaphore, MAX_FRAMES_IN_FLIGHT> m_semaphoresFinishedRendering;

	//used for CPU GPU sync
	std::array<vk::Fence, MAX_FRAMES_IN_FLIGHT> m_fenceInFlight;
	std::array<std::optional<vk::Fence>, MAX_FRAMES_IN_FLIGHT> m_fenceImagesInFlight;// used to sync if max images in flight is > than the nb of image of the swapchain or if out of order rendering is used

	vk::CommandPool m_commandPoolGraphics;
	std::vector<vk::CommandBuffer> m_commandBuffersGraphics;

	vk::CommandPool m_commandPoolTransfer;
	std::vector<vk::CommandBuffer> m_commandBuffersTransfer;

	vk::CommandPool m_commandPoolTransientResources;
	std::vector<vk::CommandBuffer> m_commandBuffersTransientResources;

	vk::CommandPool m_commandPoolOneTimeCmd;
	std::vector<vk::CommandBuffer> m_commandBuffersOneTime;

	Shader* m_shaderTriangle{};

	struct UBO
	{
		mat4 model;
		mat4 view;
		mat4 proj;
	};

	std::vector<vk::Buffer> m_uboBuffers;
	std::vector<vk::DeviceMemory> m_uboBuffersMemory;

	vk::DescriptorPool m_descriptorPool;
	std::vector<vk::DescriptorSet> m_descriptorSets;

	Mesh* m_mesh{};

	Camera* m_camera{};

	vk::RenderPass m_imguiRenderPass;
	vk::DescriptorPool m_imguiDescriptorPool;
};