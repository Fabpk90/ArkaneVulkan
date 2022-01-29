#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include "VulkanContext.h"

using namespace glm;

struct Attribute
{
	u32 elementSize; // in bytes
	u32 numOfElements;
	vk::Format format;

	Attribute(u32 elementSize, u32 numOfElements, vk::Format format)
		: elementSize(elementSize), numOfElements(numOfElements), format(format) {};
};


//TODO: allocate vkBuffer here ! (and destroy it eheh)
//TODO: rename this and see if a more flexible class isn't better
class VerticesDeclarations
{
public:

	VerticesDeclarations() = default;

	VerticesDeclarations(const VerticesDeclarations& other) = delete;

	VerticesDeclarations(VerticesDeclarations&& other) = delete;

	VerticesDeclarations& operator=(const VerticesDeclarations& other) = delete;

	VerticesDeclarations& operator=(VerticesDeclarations&& other) = delete;

	~VerticesDeclarations()
	{
		delete[] data;

		VulkanContext::GraphicInstance->GetLogicalDevice().destroyBuffer(buffer);
		VulkanContext::GraphicInstance->GetLogicalDevice().freeMemory(memory);
	}

	[[nodiscard]] void const* GetData() const
	{
		return data;
	}

	[[nodiscard]] u32 GetByteSize() const
	{
		u32 size = 0;
		for (const auto& attribute : attributes)
		{
			size += attribute.elementSize * attribute.numOfElements;
		}

		return size;
	}

	[[nodiscard]] vk::Buffer GetBuffer() const
	{
		return buffer;
	}

	uint GetElementCount() const { return nbOfElements; }

	void addAttribute(const Attribute& _attrib)
	{
		attributes.emplace_back(_attrib);
	}

	void addAttribute(u32 _elementSize, u32 _numOfElements, vk::Format _format)
	{
		attributes.emplace_back(Attribute{ _elementSize, _numOfElements, _format });
	}

	[[nodiscard]] vk::VertexInputBindingDescription GetBindingDescription() const
	{
		vk::VertexInputBindingDescription desc;

		desc.binding = 0;
		desc.stride = GetByteSize();
		desc.inputRate = vk::VertexInputRate::eVertex;

		return desc;
	}

	[[nodiscard]] std::vector<vk::VertexInputAttributeDescription> GetAttributesDescription() const
	{
		std::vector<vk::VertexInputAttributeDescription> attribs;
		u32 offset = 0;
		u32 location = 0;

		for (const auto& attribute : attributes)
		{
			vk::VertexInputAttributeDescription desc;
			const u32 sizeOfElement = attribute.elementSize * attribute.numOfElements;

			desc.binding = 0;
			desc.format = attribute.format;
			desc.offset = offset;
			desc.location = location++;

			attribs.emplace_back(desc);

			offset += sizeOfElement;
		}

		return attribs;
	}

protected:
	std::vector<Attribute> attributes;
	u8* data;
	uint nbOfElements;

	vk::Buffer buffer;
	vk::DeviceMemory memory;
};

struct MeshVertexDecl : VerticesDeclarations
{
	struct Decl
	{
		vec3 pos;
		vec3 normal;
		vec2 uv;
	};

	MeshVertexDecl(std::vector<Decl> decls)
	{
		//position
		addAttribute(sizeof(float), 3, vk::Format::eR32G32B32Sfloat);

		//normal
		addAttribute(sizeof(float), 3, vk::Format::eR32G32B32Sfloat);

		//uv
		addAttribute(sizeof(float), 2, vk::Format::eR32G32Sfloat);

		data = new u8[4 * sizeof(Decl) * decls.size()]; // u8 == 1 byte, so 4 == u32

		nbOfElements = decls.size();

		memcpy(data, decls.data(), sizeof(Decl) * decls.size());

		const auto res = VulkanContext::GraphicInstance->CreateVertexBuffer(*this);

		buffer = res.first;
		memory = res.second;
	};
};

struct TriangleVertexDecl : VerticesDeclarations
{
	struct Decl
	{
		vec2 pos;
		vec3 color;
		vec2 uv;
	};

	TriangleVertexDecl(const std::vector<Decl>& decls)
	{
		//position
		addAttribute(sizeof(float), 2, vk::Format::eR32G32Sfloat);

		//color
		addAttribute(sizeof(float), 3, vk::Format::eR32G32B32Sfloat);

		//uv
		addAttribute(sizeof(float), 2, vk::Format::eR32G32Sfloat);


		data = new u8[4 * sizeof(Decl) * decls.size()]; // u8 == 1 byte, so 4 == u32

		nbOfElements = decls.size();

		memcpy(data, decls.data(), sizeof(Decl) * decls.size());
	};
};