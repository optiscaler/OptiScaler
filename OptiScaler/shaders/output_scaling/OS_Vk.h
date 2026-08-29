#pragma once

#include "SysUtils.h"

#include <shaders/Shader_Vk.h>

class OS_Vk : public Shader_Vk
{
    bool _upsample = false;

  public:
    OS_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice, bool InUpsample);
    ~OS_Vk() = default;

    // Wrappers to maintain the original public API while using the generalized base methods
    bool CreateImageResource(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
                             VkFormat format, VkImageUsageFlags usage)
    {
        return Shader_Vk::CreateImageResource(width, height, format, usage);
    }
    void SetImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                        VkImageSubresourceRange subresourceRange)
    {
        Shader_Vk::SetImageLayout(cmdBuffer, image, oldLayout, newLayout, subresourceRange);
    }

    bool Dispatch(VkCommandBuffer InCmdList, const VkImageInfo& InResourceView, const VkImageInfo& OutResourceView);
};
