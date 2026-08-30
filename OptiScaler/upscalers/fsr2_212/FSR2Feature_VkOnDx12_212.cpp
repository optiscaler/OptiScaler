#include <pch.h>

#include "FSR2Feature_VkOnDx12_212.h"
#include "FSR2Feature_Dx12_212.h"

FSR2FeatureVkOnDx12_212::FSR2FeatureVkOnDx12_212(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature_VkwDx12(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters),
      IFeature(InHandleId, InParameters)
{
    dx12Feature = std::make_unique<FSR2FeatureDx12_212>(InHandleId, InParameters);
}
