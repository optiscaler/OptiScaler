#pragma once
#include "XeSSFeature.h"

#include <upscalers/IFeature_Dx12.h>

class XeSSFeatureDx12 : public XeSSFeature, public IFeature_Dx12
{
  private:
  protected:
  public:
    feature_version Version() override { return XeSSFeature::Version(); }
    Upscaler GetUpscalerType() const final { return Upscaler::XeSS; }
    API Api() const override { return IFeature_Dx12::Api(); }
    bool CallsUpscalerEndByItself() override { return IFeature_Dx12::CallsUpscalerEndByItself(); }

    XeSSFeatureDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
        : IFeature(InHandleId, InParameters), IFeature_Dx12(InHandleId, InParameters),
          XeSSFeature(InHandleId, InParameters)
    {
        if (XeSSProxy::Module() == nullptr)
            XeSSProxy::InitXeSS();

        _moduleLoaded = XeSSProxy::Module() != nullptr && XeSSProxy::D3D12CreateContext() != nullptr;
    }

    bool InitInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;

    bool IsWithDx12() final { return false; }

    ~XeSSFeatureDx12();
};
