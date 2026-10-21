#include <pch.h>
#include "VC_Vk.h"
#include "precompile/vc_Shader_Vk.h"

VelocityConvert_Vk::VelocityConvert_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : ResourceCopy_Vk(InName, InDevice, InPhysicalDevice, std::vector<char>(vc_spv, vc_spv + sizeof(vc_spv)))
{
}

bool VelocityConvert_Vk::Dispatch(VkCommandBuffer InCmdList, VkImageView InResourceView, VkExtent2D InExtent)
{
    if (!_init || InCmdList == VK_NULL_HANDLE || InResourceView == VK_NULL_HANDLE)
        return false;

    if (!CreateImageResource(_device, _physicalDevice, InExtent.width, InExtent.height, VK_FORMAT_R16G16_SFLOAT,
                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
    {
        LOG_ERROR("[{0}] Failed to create intermediate image!", _name);
        return false;
    }

    VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = GetImage();
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    // Whole image gets overwritten, previous contents can be discarded.
    // Only an execution dependency is needed against the previous frame's reads
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(InCmdList, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    if (!ResourceCopy_Vk::Dispatch(_device, InCmdList, InResourceView, GetImageView(), InExtent))
        return false;

    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(InCmdList, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    return true;
}
