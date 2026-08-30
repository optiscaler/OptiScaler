#include <pch.h>

#include "FFXFeature_VkOn12.h"
#include "FFXFeature_Dx12.h"

static NVSDK_NGX_Parameter* SetParameters(NVSDK_NGX_Parameter* InParameters)
{
    InParameters->Set("OptiScaler.SupportsUpscaleSize", true);
    return InParameters;
}

FFXFeatureVkOn12::FFXFeatureVkOn12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature_VkwDx12(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters),
      IFeature(InHandleId, SetParameters(InParameters))
{
    dx12Feature = std::make_unique<FFXFeatureDx12>(InHandleId, InParameters);
}
