#pragma once

#include "SysUtils.h"

#include <shaders/Shader_Vk.h>

// Forward declaration (scoped enum with a fixed underlying type -> complete type) so this header need
// not include Config.h.
enum class Scaler : uint32_t;

class OS_Vk : public Shader_Vk
{
    bool _upsample = false;

    // Scaler::Count uses the global filter. Explicit filters belong to the calling pass.
    Scaler _scalerOverride;
    Scaler ActiveScaler() const;
    bool DispatchWithSize(VkCommandBuffer commandList, const VkImageInfo& source, const VkImageInfo& output,
                          uint32_t sourceWidth, uint32_t sourceHeight, uint32_t outputWidth, uint32_t outputHeight);

  public:
    OS_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice, bool InUpsample);
    OS_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice, bool InUpsample,
          Scaler InScalerOverride);
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
    bool DispatchResources(VkCommandBuffer commandList, const VkImageInfo& source, const VkImageInfo& output);
};
