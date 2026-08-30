#pragma once
#include <upscalers/IFeature_VkwDx12.h>

class FSR2FeatureVkOnDx12_212 : public IFeature_VkwDx12
{
  public:
    Upscaler GetUpscalerType() const final { return Upscaler::FSR21_on12; }

    FSR2FeatureVkOnDx12_212(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
};
