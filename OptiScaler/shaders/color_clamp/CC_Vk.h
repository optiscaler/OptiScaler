#pragma once
#include <pch.h>
#include <shaders/Shader_Vk.h>

// Copies color into an RGBA16F image with Inf clamped to the fp16 range and NaN zeroed,
// see ColorClamp_Dx12 for why FSR 4 needs it
class ColorClamp_Vk : public Shader_Vk
{
    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

  public:
    ColorClamp_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    ~ColorClamp_Vk() = default;

    // The image of GetImage(), same size as the color, always RGBA16F
    bool CreateImage(uint32_t width, uint32_t height);

    // InResourceView is read in SHADER_READ_ONLY_OPTIMAL, the image is written in GENERAL
    bool Dispatch(VkCommandBuffer InCmdList, VkImageView InResourceView);

    // Only float formats can hold Inf or NaN
    static bool NeedsClamp(VkFormat InFormat);

    void SetImageLayout(VkCommandBuffer cmdBuffer, VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        Shader_Vk::SetImageLayout(cmdBuffer, _intermediateImage, oldLayout, newLayout,
                                  { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
    }
};
