#include "Shader.h"

#include <fstream>
#include <iostream>
#include <spirvReflect/spirv_reflect.h>

Shader::Shader(vk::Device _device, const char* _path)
{
	const std::string baseStr = _path;
	const std::string vertexStr = baseStr + "_vert.spv";
	const std::string fragStr = baseStr + "_frag.spv";

	spirvVert = readEntireFile(vertexStr.c_str());
	spirvFrag = readEntireFile(fragStr.c_str());

	shaderModuleVert = createModuleFromString(_device , spirvVert);
	shaderModuleFrag = createModuleFromString(_device , spirvFrag);
}

void Shader::Reflect() const
{
	const spv_reflect::ShaderModule module(spirvVert);

	// Enumerate and extract shader's input variables
	uint32_t var_count = 0;
	auto result = module.EnumerateInputVariables(&var_count, nullptr);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);
	auto** input_vars =	static_cast<SpvReflectInterfaceVariable**>(malloc(var_count * sizeof(SpvReflectInterfaceVariable*)));
	result = module.EnumerateInputVariables(&var_count, input_vars);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);

	uint32_t descriptorCount = 0;
	module.EnumerateDescriptorSets(&descriptorCount, nullptr);

	uint32_t bindingIndex = 0;

	for (uint32_t i = 0; i < descriptorCount; ++i)
	{
		const auto* descriptor = module.GetDescriptorSet(i);

		for (uint32_t j = 0; j < descriptor->binding_count; ++j)
		{
			switch (descriptor->bindings[j]->descriptor_type)
			{
				case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: break;
				case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: break;
				default: ;
			}

			++bindingIndex;
		}
	}

	for (auto i = 0; i < var_count; ++i)
	{
		std::cout << input_vars[i]->format << std::endl;
	}

	// Output variables, descriptor bindings, descriptor sets, and push constants
	// can be enumerated and extracted using a similar mechanism.

	free(input_vars);
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
