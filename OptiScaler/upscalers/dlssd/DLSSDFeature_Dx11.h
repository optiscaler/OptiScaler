#pragma once
#include <upscalers/IFeature_Dx11.h>
#include "DLSSDFeature.h"
#include <string>

class DLSSDFeatureDx11 : public DLSSDFeature, public IFeature_Dx11
{
  private:
  protected:
  public:
    bool InitInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters) override;

    feature_version Version() override { return DLSSDFeature::Version(); }
    Upscaler GetUpscalerType() const final { return DLSSDFeature::GetUpscalerType(); }
    API Api() const override { return IFeature_Dx11::Api(); }
    bool CallsUpscalerEndByItself() override { return IFeature_Dx11::CallsUpscalerEndByItself(); }

    bool IsWithDx12() override { return false; }

    DLSSDFeatureDx11(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
    ~DLSSDFeatureDx11();
};
