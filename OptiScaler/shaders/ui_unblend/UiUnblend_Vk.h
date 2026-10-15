#pragma once

#include <shaders/hud_copy/HudCopy_Vk.h>

#include "precompile/UiUnblend_Shader_Vk.h"

// Rebuilds a hudless frame by removing the game's UI texture from it, same bindings as HudCopy
class UiUnblend_Vk : public HudCopy_Vk
{
    struct alignas(256) InternalUnblendParams
    {
        float OpaqueAlpha = 0.99f;
        uint32_t Premultiplied = 1;
    };

  public:
    UiUnblend_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
        : HudCopy_Vk(InName, InDevice, InPhysicalDevice, UiUnblend_spv, sizeof(InternalUnblendParams))
    {
    }

    // InFrameInfo holds the presented frame and gets the frame without UI
    bool Dispatch(VkCommandBuffer InCmdList, const VkImageInfo& InUiInfo, VkImageLayout UiState,
                  const VkImageInfo& InFrameInfo, VkImageLayout FrameState, bool Premultiplied)
    {
        InternalUnblendParams constants {};
        constants.Premultiplied = Premultiplied ? 1 : 0;

        return DispatchShader(InCmdList, InUiInfo, UiState, InFrameInfo, FrameState, &constants, sizeof(constants));
    }
};
