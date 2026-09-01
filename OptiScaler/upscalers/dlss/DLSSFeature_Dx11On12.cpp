#include <pch.h>

#include "DLSSFeature_Dx11On12.h"
#include "DLSSFeature_Dx12.h"

// No SetParameters here, deliberately.
//
// The FFX bridge variants set "OptiScaler.SupportsUpscaleSize" in their constructors, and copying
// that across was what made this path render like a pastel painting. It is an FSR 3.1 escape hatch:
// GetDynamicOutputResolution sees it, returns immediately without resolving the output size, and
// leaves DLSS to be driven at the wrong resolution. No DLSS feature sets it anywhere else in the
// tree, and this one must not either.
DLSSFeatureDx11on12::DLSSFeatureDx11on12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature_Dx11wDx12(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters),
      IFeature(InHandleId, InParameters)
{
    dx12Feature = std::make_unique<DLSSFeatureDx12>(InHandleId, InParameters);
}
