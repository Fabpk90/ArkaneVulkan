// ArkaneVulkan.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "extern/stb/stb_image.h"

#include "systems/SceneGraph.h"
#include "systems/SceneManager.h"
#include "systems/SystemManager.h"
#include "systems/VulkanContext.h"

int main()
{
    SystemManager manager;
    manager.AddSystem(new VulkanContext());
    manager.AddSystem(new SceneGraph());
    manager.AddSystem(new SceneManager());


    manager.Start();

    while (manager.ShouldContinue())
    {
        manager.Update();
    }
}
