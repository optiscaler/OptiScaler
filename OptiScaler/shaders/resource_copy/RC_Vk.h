#pragma once
#include <pch.h>
#include <shaders/Shader_Vk.h>

// Copies a rectangle between images, In is read in SHADER_READ_ONLY_OPTIMAL, Out written in GENERAL
class ResourceCopy_Vk : public Shader_Vk
{
    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

    // Last BufferFormat query, the source format rarely changes
    VkFormat _querySource = VK_FORMAT_MAX_ENUM;
    VkFormat _queryResult = VK_FORMAT_UNDEFINED;

  public:
    ResourceCopy_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    ~ResourceCopy_Vk() = default;

    // Copies OutExtent from InSrc in the input to InDst in the output
    bool Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, VkImageView InResourceView, VkImageView OutResourceView,
                  VkExtent2D OutExtent, VkOffset2D InSrc = {}, VkOffset2D InDst = {});

    // Storage image format for a copy of InFormat, depth is copied to R32_SFLOAT
    VkFormat BufferFormat(VkFormat InFormat, bool InDepth);

    // Integer formats can't be read as float
    static bool IsInteger(VkFormat InFormat);

    // Only grows, so dynamic resolution doesn't recreate it every frame
    bool CreateGrowingImage(uint32_t width, uint32_t height, VkFormat format);
    VkExtent2D ImageExtent() const { return { _width, _height }; }

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
