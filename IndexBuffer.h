#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "systems/VulkanContext.h"

class IndexBuffer
{
public:
	IndexBuffer(std::vector<u16>&& _indices)
		: m_size(_indices.size()),
	      m_buffer(nullptr),
	      m_memory(nullptr)
	{
		const auto [buf, mem] = VulkanContext::GraphicInstance->CreateIndexBuffer(_indices);

		m_buffer = buf;
		m_memory = mem;
	}

	~IndexBuffer()
	{
		VulkanContext::GraphicInstance->GetLogicalDevice().destroyBuffer(m_buffer);
		VulkanContext::GraphicInstance->GetLogicalDevice().freeMemory(m_memory);
	}

	[[nodiscard]] const vk::Buffer& GetBuffer() const { return m_buffer; }

	[[nodiscard]] u32 GetSize() const { return m_size; }
private:
	u32 m_size;
	vk::Buffer m_buffer;
	vk::DeviceMemory m_memory;
};

