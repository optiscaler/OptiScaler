#pragma once
#include <pch.h>
#include <shaders/Shader_Vk.h>

class ResourceCopy_Vk : public Shader_Vk
{
    VkImageView _currentInResourceView = VK_NULL_HANDLE;
    VkImageView _currentOutResourceView = VK_NULL_HANDLE;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    bool InitializeViews(VkImageView InResourceView, VkImageView OutResourceView);

  public:
    ResourceCopy_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    ~ResourceCopy_Vk() = default;

    bool Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, VkImageView InResourceView, VkImageView OutResourceView,
                  VkExtent2D OutExtent);

    // Wrappers to maintain the public API while leveraging base class implementation
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
};
