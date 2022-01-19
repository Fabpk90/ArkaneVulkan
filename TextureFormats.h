#pragma once

enum class TextureType
{
	Albedo,
	Normals,
	Depth,
	Count
};

enum class TextureDescription
{
	GBuffer_0,
	GBuffer_1,
	DepthStencil,
	Count
};
