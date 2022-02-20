#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>

class Texture2D;
class VerticesDeclarations;

enum class EShaderBindgType
{
	FLOAT,
	FLOAT2,
	FLOAT3,
	FLOAT4,
	MAT4,
	INT,
	IMAGE,
	UNIFORM,
	STORAGE
};

//todo: make the binding bind themselves, to use polymorphism

class ShaderBinding
{
public:
	virtual ~ShaderBinding() = default;
	virtual void Bind(vk::ShaderModule& shaderModule) = 0;
};

class VertexBinding : public ShaderBinding
{
public:
	VerticesDeclarations* vertexDecl;
};

class TextureBinding : ShaderBinding
{
public:
	Texture2D* texture; // todo: make that a ITexture, to support 3D tex
};

class Buffer : public ShaderBinding
{
	
};

class Shader
{
public:
	Shader(vk::Device _logicalDevice, const char* _path);

	void Reflect() const;

	void SetBinding(EShaderBindgType type, uint32_t set, uint32_t binding);

	vk::ShaderModule shaderModuleVert;
	vk::ShaderModule shaderModuleFrag;
	std::vector<uint8_t> spirvVert;
	std::vector<uint8_t> spirvFrag;
private:

	std::vector<ShaderBinding> bindings;

	static std::vector<uint8_t> readEntireFile(const char* _path);
	static vk::ShaderModule createModuleFromString(vk::Device _device, std::vector<uint8_t>& _bytecode);
};

