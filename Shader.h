#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>

class Shader
{
public:
	Shader(vk::Device _logicalDevice, const char* _path);

	vk::ShaderModule shaderModuleVert;
	vk::ShaderModule shaderModuleFrag;
private:
	static std::vector<char> readEntireFile(const char* _path);
	static vk::ShaderModule createModuleFromString(vk::Device _logicalDevice, std::vector<char>& _binary);
};

