#pragma once
#include "SysUtils.h"
#include <NVNGX_Parameter.h>
#include <upscalers/IFeature_Vk.h>

#include <nvsdk_ngx_vk.h>

// OptiFG on Vulkan, the upscaler's inputs feed the FG output
class UpscalerInputsVk
{
  public:
    static void UpscaleStart(VkCommandBuffer cmdBuffer, NVSDK_NGX_Parameter* parameters, IFeature_Vk* feature);
    // output is read before the upscale, the DX12 interop upscalers clear it
    static void UpscaleEnd(VkCommandBuffer cmdBuffer, const NVSDK_NGX_Resource_VK* output);
};
