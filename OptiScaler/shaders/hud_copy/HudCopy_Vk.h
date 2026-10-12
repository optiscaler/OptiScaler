#pragma once
#include "HudCopy_Common.h"

#include <shaders/Shader_Vk.h>

#include <span>

class HudCopy_Vk : public Shader_Vk, public HudCopy_Common
{
    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

  protected:
    // For shaders with the same bindings (params, hudless, present copy, present out)
    HudCopy_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice,
               std::span<const unsigned char> InShaderCode, size_t InParamsSize);

    bool DispatchShader(VkCommandBuffer InCmdList, const VkImageInfo& InHudlessInfo, VkImageLayout HudlessState,
                        const VkImageInfo& InPresentInfo, VkImageLayout PresentState, const void* InParams,
                        size_t InParamsSize);

  public:
    HudCopy_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);
    virtual ~HudCopy_Vk() = default;

    bool Dispatch(VkCommandBuffer InCmdList, const VkImageInfo& InHudlessInfo, VkImageLayout HudlessState,
                  const VkImageInfo& InPresentInfo, VkImageLayout PresentState, float HudDetectionThreshold);
};
