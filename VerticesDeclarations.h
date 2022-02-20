#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

using namespace glm;

struct Attribute
{
	u32 elementSize; // in bytes
	u32 numOfElements;
	vk::Format format;
	std::string name;
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
		delete[] m_data;

		VulkanContext::GraphicInstance->GetLogicalDevice().destroyBuffer(buffer);
		VulkanContext::GraphicInstance->GetLogicalDevice().freeMemory(memory);
	}

	[[nodiscard]] void const* GetData() const
	{
		return m_data;
	}

	[[nodiscard]] u32 GetByteSize() const
	{
		u32 size = 0;
		for (const auto& attribute : m_attributes)
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
		m_attributes.emplace_back(_attrib);
	}

	void addAttribute(Attribute&& attrib)
	{
		m_attributes.emplace_back(std::move(attrib));
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

		for (const auto& attribute : m_attributes)
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
	std::vector<Attribute> m_attributes;
	u8* m_data;
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
		vec3 tangent;
		vec3 bitangent;
	};

	MeshVertexDecl(std::vector<Decl>&& _decls)
	{
		//position
		addAttribute({sizeof(float), 3, vk::Format::eR32G32B32Sfloat, "position" });

		//normal
		addAttribute({ sizeof(float), 3, vk::Format::eR32G32B32Sfloat, "normal"});

		//uv
		addAttribute({ sizeof(float), 2, vk::Format::eR32G32Sfloat, "uv"});

		addAttribute({ sizeof(float), 3, vk::Format::eR32G32B32Sfloat, "tangent" });

		addAttribute({ sizeof(float), 3, vk::Format::eR32G32B32Sfloat, "bitangent" });

		m_data = new u8[4 * sizeof(Decl) * _decls.size()]; // u8 == 1 byte, so 4 == u32

		nbOfElements = _decls.size();

		memcpy(m_data, _decls.data(), sizeof(Decl) * _decls.size());

		const auto [buff, mem] = VulkanContext::GraphicInstance->CreateVertexBuffer(*this);

		buffer = buff;
		memory = mem;
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

	TriangleVertexDecl(std::vector<Decl>&& _decls)
	{
		//position
		addAttribute({ sizeof(float), 2, vk::Format::eR32G32Sfloat, "position"});

		//color
		addAttribute({ sizeof(float), 3, vk::Format::eR32G32B32Sfloat, "color"});

		//uv
		addAttribute({ sizeof(float), 2, vk::Format::eR32G32Sfloat, "uv"});


		m_data = new u8[4 * sizeof(Decl) * _decls.size()]; // u8 == 1 byte, so 4 == u32

		nbOfElements = _decls.size();

		memcpy(m_data, _decls.data(), sizeof(Decl) * _decls.size());
	};
};