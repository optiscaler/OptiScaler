#pragma once
#include "SysUtils.h"
#include <NVNGX_Parameter.h>
#include <upscalers/IFeature_Vk.h>

#include <nvsdk_ngx_vk.h>

// OptiFG on Vulkan with a DX12 FG output, the upscaler's inputs are copied to textures shared with D3D12
class UpscalerInputsVkwDx12
{
  public:
    static void UpscaleStart(VkCommandBuffer cmdBuffer, NVSDK_NGX_Parameter* parameters, IFeature_Vk* feature);
    static void Shutdown();
};
