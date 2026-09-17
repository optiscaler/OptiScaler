#pragma once
#include <upscalers/IFeature_VkwDx12.h>

// DLSS in a Vulkan game, run on the D3D12 side of the bridge.
//
// The Vulkan counterpart of DLSSFeature_Dx11on12, and it exists for the same reason. Every other
// upscaler already had a VkOn12 variant; DLSS did not, so a Vulkan game could have DLSS natively or
// it could have the bridge, never both.
//
// That matters less than it did for D3D11, because Neural Rendering now runs natively on Vulkan --
// so a Vulkan game does not have to choose. It still matters where the native Vulkan path is
// unavailable or misbehaving: the bridge remains the fallback, and until now taking it meant
// dropping DLSS for FSR or XeSS.
//
// The bridge base already shares the game's Vulkan images into D3D12 and runs whatever D3D12 feature
// it is handed, so this is the same few lines the FFX variant is: build a DLSSFeatureDx12 and give
// it to the bridge.
class DLSSFeatureVkOn12 : public IFeature_VkwDx12
{
  public:
    Upscaler GetUpscalerType() const final { return Upscaler::DLSS_on12; }

    DLSSFeatureVkOn12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
};
