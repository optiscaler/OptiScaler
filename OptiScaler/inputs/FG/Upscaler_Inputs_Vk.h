#pragma once
#include "SysUtils.h"
#include <NVNGX_Parameter.h>
#include <upscalers/IFeature_Vk.h>

// OptiFG on Vulkan, the upscaler's inputs feed the FG output
class UpscalerInputsVk
{
  public:
    static void UpscaleStart(VkCommandBuffer cmdBuffer, NVSDK_NGX_Parameter* parameters, IFeature_Vk* feature);
};
