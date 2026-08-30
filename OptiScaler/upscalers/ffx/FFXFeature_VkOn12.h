#pragma once
#include <upscalers/IFeature_VkwDx12.h>

class FFXFeatureVkOn12 : public IFeature_VkwDx12
{
  public:
    Upscaler GetUpscalerType() const final { return Upscaler::FFX_on12; }

    FFXFeatureVkOn12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
};
