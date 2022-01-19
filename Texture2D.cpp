#include "Texture2D.h"

#include "VulkanContext.h"
#include "extern/stb/stb_image.h"

Texture2D::Texture2D(Texture2D&& tex) : size(tex.size), imageMemory(tex.imageMemory), image(tex.image), sampler(tex.sampler),
                                        imageView(tex.imageView),
                                        layout(tex.layout), path(tex.path)
{
	tex.isMoved = false;
}

void Texture2D::LoadFrom(const char* _path, vk::ImageLayout _layout)
{
	path = _path;
	this->layout = _layout;

	int x, y, channels;
	const auto data = stbi_load(_path, &x, &y, &channels, STBI_rgb_alpha);

	if(!data)
	{
		assert(0);
	}

	const vk::DeviceSize texSize = y * x * 4;

	size.width = x;
	size.height = y;
	size.depth = 1;

	vk::Buffer stagingBuffer;
	vk::DeviceMemory stagingMemory;

	auto* instance = VulkanContext::GraphicInstance;
	const auto& logicalDevice = instance->GetLogicalDevice();

	instance->CreateBuffer(texSize, vk::BufferUsageFlagBits::eTransferSrc
		, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, stagingBuffer, stagingMemory);

	void* mappedBuffer = logicalDevice.mapMemory(stagingMemory, 0, texSize);

	assert(mappedBuffer);

	memcpy(mappedBuffer, data, texSize);

	logicalDevice.unmapMemory(stagingMemory);
	stbi_image_free(data);

	instance->CreateImage(size.width, size.height, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal
		, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst
		, vk::MemoryPropertyFlagBits::eDeviceLocal, image, imageMemory);

	instance->TransitionImageLayout(image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
	instance->CopyBufferToImage(stagingBuffer, image, size.width, size.height);
	instance->TransitionImageLayout(image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

	logicalDevice.destroyBuffer(stagingBuffer);
	logicalDevice.freeMemory(stagingMemory);

	imageView = instance->CreateImageView(image, vk::Format::eR8G8B8A8Srgb);

	sampler = instance->CreateTextureSampler();
}
