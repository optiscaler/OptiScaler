#pragma once
#include "SysUtils.h"

#include <framegen/IFGFeature_Vk.h>

// Vulkan FG inputs of games presenting through VkwDx12SC, for every FG input. Each one is copied into its frame's
// texture shared with D3D12 and tagged on the DX12 FG, which copies it again once D3D12 waited for the Vulkan frame.
class VkwDx12Inputs
{
  public:
    // The DX12 FG takes the Vulkan inputs
    static bool Active();

    // Copies the input with its command buffer when it's only valid now, inputs valid until present are copied by
    // the present
    static bool SetResource(const VkFGResource& input);

    // Records the copies the present's frame still needs, the command buffer runs after the game's frame
    static void RecordPresentCopies(VkCommandBuffer cmdBuffer, uint32_t queueFamily);

    static void Shutdown();
};
