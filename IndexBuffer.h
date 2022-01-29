#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "VulkanContext.h"

class IndexBuffer
{
public:
	IndexBuffer(const std::vector<glm::u16>& indices)
		: size(indices.size())
	{
		const auto res = VulkanContext::GraphicInstance->CreateIndexBuffer(indices);

		buffer = res.first;
		memory = res.second;
	}

	/*IndexBuffer(const std::vector<glm::u32>& indices)
		: size(indices.size())
	{
		const auto res = VulkanContext::GraphicInstance->CreateIndexBuffer(indices);

		buffer = res.first;
		memory = res.second;
	}*/

	~IndexBuffer()
	{
		VulkanContext::GraphicInstance->GetLogicalDevice().destroyBuffer(buffer);
		VulkanContext::GraphicInstance->GetLogicalDevice().freeMemory(memory);
	}

	[[nodiscard]] const vk::Buffer& GetBuffer() const { return buffer; }

	u32 GetSize() const { return size; }
private:
	u32 size;
	vk::Buffer buffer;
	vk::DeviceMemory memory;
};

