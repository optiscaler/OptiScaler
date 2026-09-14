#pragma once
#include "HudCopy_Common.h"

#include <shaders/Shader_Vk.h>

class HudCopy_Vk : public Shader_Vk, public HudCopy_Common
{
    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

  public:
    HudCopy_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    virtual ~HudCopy_Vk() = default;

    bool Dispatch(VkCommandBuffer InCmdList, const VkImageInfo& InHudlessInfo, VkImageLayout HudlessState,
                  const VkImageInfo& InPresentInfo, VkImageLayout PresentState, float HudDetectionThreshold);
};
