#pragma once
#include <upscalers/IFeature_VkwDx12.h>

class DLSSFeatureVkOnDx12 : public IFeature_VkwDx12
{
  public:
    Upscaler GetUpscalerType() const final { return Upscaler::DLSS_on12; }

    DLSSFeatureVkOnDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
};
