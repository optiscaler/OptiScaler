#pragma once
#include <pch.h>
#include <shaders/Shader_Vk.h>

// Vertically flips depth or motion vectors (negating Y), for Unity games
class RF_Vk : public Shader_Vk
{
    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

  public:
    RF_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    ~RF_Vk() = default;

    // In is read in SHADER_READ_ONLY_OPTIMAL, Out written in GENERAL
    bool Dispatch(VkCommandBuffer InCmdList, VkImageView InResourceView, VkImageView OutResourceView,
                  VkExtent2D InImageExtent, uint32_t width, uint32_t height, bool velocity);
};
