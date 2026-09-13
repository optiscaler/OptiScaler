#pragma once
#include <upscalers/IFeature_Dx11wDx12.h>

// DLSS in a DirectX 11 game, run on the D3D12 side of the bridge.
//
// Every other upscaler already had one of these; DLSS did not, so a D3D11 game could have DLSS
// natively or it could have the bridge, never both. That mattered more than it looks: Neural
// Rendering only exists on the bridge -- the model refuses to initialise on a D3D11 device at all,
// answering FAIL_FeatureNotSupported -- so choosing DLSS meant giving up the pass, and choosing the
// pass meant dropping to FSR or XeSS.
//
// The bridge base already converts the game's D3D11 textures into D3D12 resources and runs whatever
// D3D12 feature it is handed, so this is the same seventeen lines the FFX variant is: build a
// DLSSFeatureDx12 and give it to the bridge.
class DLSSFeatureDx11on12 : public IFeature_Dx11wDx12
{
  public:
    Upscaler GetUpscalerType() const final { return Upscaler::DLSS_on12; }

    DLSSFeatureDx11on12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
};
