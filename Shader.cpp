#include "Shader.h"

#include <fstream>

Shader::Shader(vk::Device _device, const char* _path)
{
	std::string baseStr = _path;
	std::string vertexStr = baseStr + "_vert.spv";
	std::string fragStr = baseStr + "_frag.spv";

	auto spirvVert = readEntireFile(vertexStr.c_str());
	auto spirvFrag = readEntireFile(fragStr.c_str());

	shaderModuleVert = createModuleFromString(_device , spirvVert);
	shaderModuleFrag = createModuleFromString(_device , spirvFrag);
}

std::vector<char> Shader::readEntireFile(const char* path)
{
	std::ifstream file(path, std::ios::ate | std::ios::binary);

	assert(file.is_open());

	const size_t fileSize = file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();

	return buffer;
}

vk::ShaderModule Shader::createModuleFromString(vk::Device _device, std::vector<char>& binary)
{
	vk::ShaderModuleCreateInfo info;

	info.codeSize = binary.size();
	info.pCode = reinterpret_cast<uint32_t*>(binary.data());

	return _device.createShaderModule(info);
}
