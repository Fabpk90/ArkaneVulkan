#include "Shader.h"

#include <fstream>

#include "SPIRV-Reflect/spirv_reflect.h"

Shader::Shader(vk::Device _device, const char* _path)
{
	std::string baseStr = _path;
	std::string vertexStr = baseStr + "_vert.spv";
	std::string fragStr = baseStr + "_frag.spv";

	spirvVert = readEntireFile(vertexStr.c_str());
	spirvFrag = readEntireFile(fragStr.c_str());

	shaderModuleVert = createModuleFromString(_device , spirvVert);
	shaderModuleFrag = createModuleFromString(_device , spirvFrag);
}

void Shader::Reflect()
{
	spv_reflect::ShaderModule module(spirvVert);

	// Enumerate and extract shader's input variables
	uint32_t var_count = 0;
	auto result = module.EnumerateInputVariables(&var_count, nullptr);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);
	auto** input_vars =	static_cast<SpvReflectInterfaceVariable**>(malloc(var_count * sizeof(SpvReflectInterfaceVariable*)));
	result = module.EnumerateInputVariables(&var_count, input_vars);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);

	// Output variables, descriptor bindings, descriptor sets, and push constants
	// can be enumerated and extracted using a similar mechanism.

}

std::vector<uint8_t> Shader::readEntireFile(const char* path)
{
	std::ifstream file(path, std::ios::ate | std::ios::binary);

	assert(file.is_open());

	const size_t fileSize = file.tellg();
	std::vector<uint8_t> buffer(fileSize);

	file.seekg(0);
	file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

	file.close();

	return buffer;
}

vk::ShaderModule Shader::createModuleFromString(vk::Device _device, std::vector<uint8_t>& _bytecode)
{
	vk::ShaderModuleCreateInfo info;

	info.codeSize = _bytecode.size();
	info.pCode = reinterpret_cast<uint32_t*>(_bytecode.data());

	return _device.createShaderModule(info);
}
