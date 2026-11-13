#pragma once
#include <upscalers/IFeature_VkwDx12.h>

class FFXFeatureVkOn12 : public IFeature_VkwDx12
{
  public:
    Upscaler GetUpscalerType() const final { return Upscaler::FFX_on12; }

    // customUpscaler is the name of a custom upscaler run by the DX12 feature, empty for the regular one
    FFXFeatureVkOn12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters,
                     const std::string& customUpscaler = "");
};
