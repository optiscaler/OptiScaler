#pragma once
#include "SysUtils.h"

#include <vulkan/vulkan.h>

// OptiFG on Vulkan, finds the hudless image of games that don't provide one.
// After upscaling, the swapchain sized images used next are candidates, the FGHUDLimit-th one is copied as hudless.
// Candidates are the upscaler's output, the color attachments loaded by or stored at the end of a render pass
// and the storage images bound to compute dispatches.
class Hudfix_Vk
{
  public:
    static void HookDevice(VkDevice device);

    static void UpscaleEnd(VkCommandBuffer cmdBuffer);
    static void PresentStart();

    static void CheckImage(VkCommandBuffer cmdBuffer, VkImage image, VkFormat format, VkExtent2D extent,
                           VkImageLayout layout);
    static void AddSwapchainImages(const VkImage* images, uint32_t count);
};
