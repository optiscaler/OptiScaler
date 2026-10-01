#pragma once
#include <upscalers/IFeature_Dx11wDx12.h>

class DLSSFeatureDx11On12 : public IFeature_Dx11wDx12
{
  public:
    Upscaler GetUpscalerType() const final { return Upscaler::DLSS_on12; }

    DLSSFeatureDx11On12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
};
