#include <pch.h>

#include "FFXFeature_Dx11On12.h"
#include "FFXFeature_Dx12.h"

static NVSDK_NGX_Parameter* SetParameters(NVSDK_NGX_Parameter* InParameters)
{
    InParameters->Set("OptiScaler.SupportsUpscaleSize", true);
    return InParameters;
}

FFXFeatureDx11on12::FFXFeatureDx11on12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters,
                                       const std::string& customUpscaler)
    : IFeature_Dx11wDx12(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters),
      IFeature(InHandleId, SetParameters(InParameters))
{
    dx12Feature = std::make_unique<FFXFeatureDx12>(InHandleId, InParameters, customUpscaler);
}
