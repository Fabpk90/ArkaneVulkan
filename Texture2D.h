#pragma once

#include <assimp/material.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "TextureFormats.h"
#include "systems/VulkanContext.h"

static vk::ImageLayout GetTextureLayout(aiTextureType& texType)
{
	return vk::ImageLayout::eGeneral;

	switch (texType)
	{
		case aiTextureType_NONE: break;
	case aiTextureType_DIFFUSE:
		break;
		case aiTextureType_SPECULAR: break;
		case aiTextureType_AMBIENT: break;
		case aiTextureType_EMISSIVE: break;
		case aiTextureType_HEIGHT: break;
		case aiTextureType_NORMALS: break;
		case aiTextureType_SHININESS: break;
		case aiTextureType_OPACITY: break;
		case aiTextureType_DISPLACEMENT: break;
		case aiTextureType_LIGHTMAP: break;
		case aiTextureType_REFLECTION: break;
		case aiTextureType_BASE_COLOR: break;
		case aiTextureType_NORMAL_CAMERA: break;
		case aiTextureType_EMISSION_COLOR: break;
		case aiTextureType_METALNESS: break;
		case aiTextureType_DIFFUSE_ROUGHNESS: break;
		case aiTextureType_AMBIENT_OCCLUSION: break;
		case aiTextureType_UNKNOWN: break;
		case _aiTextureType_Force32Bit: break;
		default: ;
	}
}

//todo: fix me ! The handling of moving constructor is an absolute hack for now

class Texture2D
{
public:
	Texture2D(const Texture2D& other)
		: size(other.size),
		  imageMemory(other.imageMemory),
		  image(other.image),
		  sampler(other.sampler),
		  imageView(other.imageView),
		  layout(other.layout),
		  path(other.path)
	{
	}

	Texture2D& operator=(const Texture2D& other)
	{
		if (this == &other)
			return *this;
		size = other.size;
		imageMemory = other.imageMemory;
		image = other.image;
		sampler = other.sampler;
		imageView = other.imageView;
		layout = other.layout;
		path = other.path;
		return *this;
	}

	Texture2D& operator=(Texture2D&& other) noexcept
	{
		if (this == &other)
			return *this;
		size = std::move(other.size);
		imageMemory = std::move(other.imageMemory);
		image = std::move(other.image);
		sampler = std::move(other.sampler);
		imageView = std::move(other.imageView);
		layout = other.layout;
		path = std::move(other.path);
		return *this;
	}

	Texture2D() = default;
	~Texture2D()
	{
		if(isMoved) // i think the bug comes from the image view being wrong, maybe investigate that
		{
			auto* instance = VulkanContext::GraphicInstance;

			instance->GetLogicalDevice().destroyImageView(imageView);
			instance->GetLogicalDevice().destroyImage(image);
			instance->GetLogicalDevice().destroySampler(sampler);
			instance->GetLogicalDevice().freeMemory(imageMemory);
		}
	}

	Texture2D(Texture2D&& tex);

	void LoadFrom(const char* _path, vk::ImageLayout _layout);

	vk::ImageView& GetView() { return imageView; }
	vk::Extent3D& GetSize() { return size; }

	vk::ImageLayout& GetLayout() { return layout; }
	vk::Sampler& GetSampler() { return sampler; }

private:
	vk::Extent3D size;

	//vma::Allocation memory;
	vk::DeviceMemory imageMemory;
	vk::Image image;

	vk::Sampler sampler;
	vk::ImageView imageView;
	vk::ImageLayout layout;

	//for debug only
	std::string path;

	bool isMoved = true;
};

