#pragma once
#include <pch.h>
#include <shaders/resource_copy/RC_Vk.h>

class VelocityConvert_Vk : public ResourceCopy_Vk
{
  public:
    VelocityConvert_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);

    // Copies InResourceView into the R16G16_SFLOAT intermediate image (GetImage()),
    // left in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    bool Dispatch(VkCommandBuffer InCmdList, VkImageView InResourceView, VkExtent2D InExtent);
};
