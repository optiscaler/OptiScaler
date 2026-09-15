#pragma once

#include <nvsdk_ngx_defs.h>

struct ID3D12Resource;

// The caller's parameter block may still contain DX11/Vulkan pointers. Even an
// unused optional input must be replaced before any D3D12 consumer sees it.
template <typename Parameters>
void SetOptionalDx12Inputs(Parameters* parameters, ID3D12Resource* exposure, ID3D12Resource* reactive,
                           bool autoExposure, bool disableReactive)
{
    parameters->Set(NVSDK_NGX_Parameter_ExposureTexture, static_cast<void*>(autoExposure ? nullptr : exposure));
    parameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask,
                    static_cast<void*>(disableReactive ? nullptr : reactive));
}
