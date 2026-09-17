#include <pch.h>

#include "DLSSFeature_VkOn12.h"
#include "DLSSFeature_Dx12.h"

// No SetParameters here, deliberately -- the same omission as the D3D11 variant, for the same reason.
//
// The FFX bridge variants set "OptiScaler.SupportsUpscaleSize" in their constructors. That flag is an
// FSR 3.1 escape hatch: GetDynamicOutputResolution consumes it and returns without resolving the
// output size, which is right for a feature that carries its own upscaleSize keys and meaningless for
// one that does not. No DLSS feature sets it anywhere else in the tree.
//
// It was once blamed for the posterised picture on the D3D11 bridge. That was a misattribution -- on
// the NGX input paths the flag is inert either way, because nothing sets the FSR.upscaleSize keys it
// gates. The real cause was a closed command list handed to CreateFeature, fixed in the bridge base.
DLSSFeatureVkOn12::DLSSFeatureVkOn12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature_VkwDx12(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters),
      IFeature(InHandleId, InParameters)
{
    dx12Feature = std::make_unique<DLSSFeatureDx12>(InHandleId, InParameters);
}
