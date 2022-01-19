// ArkaneVulkan.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "extern/stb/stb_image.h"

#include "VulkanContext.h"

int main()
{
    VulkanContext ctx;
    ctx.InitWindow();
    ctx.InitVulkan();
    ctx.DrawLoop();
    ctx.Destroy();
}
