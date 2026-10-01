#include <pch.h>

#include "DLSSFeature_VkOnDx12.h"
#include <upscalers/dlss/DLSSFeature_Dx12.h>

DLSSFeatureVkOnDx12::DLSSFeatureVkOnDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature_VkwDx12(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters),
      IFeature(InHandleId, InParameters)
{
    dx12Feature = std::make_unique<DLSSFeatureDx12>(InHandleId, InParameters);
}
