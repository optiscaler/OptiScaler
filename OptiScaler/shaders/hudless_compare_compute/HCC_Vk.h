#pragma once

#include <shaders/hud_copy/HudCopy_Vk.h>

#include "precompile/HCC_Shader_Vk.h"

// Tints what differs between hudless and present pink, same bindings as HudCopy
class HCC_Vk : public HudCopy_Vk
{
    struct alignas(256) InternalCompareParams
    {
        float DiffThreshold = 0.003f;
        float PinkAmount = 0.6f;
    };

  public:
    HCC_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
        : HudCopy_Vk(InName, InDevice, InPhysicalDevice, HCC_spv, sizeof(InternalCompareParams))
    {
    }

    bool Dispatch(VkCommandBuffer InCmdList, const VkImageInfo& InHudlessInfo, VkImageLayout HudlessState,
                  const VkImageInfo& InPresentInfo, VkImageLayout PresentState)
    {
        InternalCompareParams constants {};
        return DispatchShader(InCmdList, InHudlessInfo, HudlessState, InPresentInfo, PresentState, &constants,
                              sizeof(constants));
    }
};
