#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>

class Shader
{
public:
	Shader(vk::Device _logicalDevice, const char* _path);

	void Reflect();

	vk::ShaderModule shaderModuleVert;
	vk::ShaderModule shaderModuleFrag;
	std::vector<uint8_t> spirvVert;
	std::vector<uint8_t> spirvFrag;
private:
	static std::vector<uint8_t> readEntireFile(const char* _path);
	static vk::ShaderModule createModuleFromString(vk::Device _device, std::vector<uint8_t>& _bytecode);
};

