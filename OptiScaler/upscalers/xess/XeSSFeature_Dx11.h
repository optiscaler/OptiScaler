#pragma once

#include "SysUtils.h"

#include <proxies/XeSS_Proxy.h>
#include <upscalers/IFeature_Dx11.h>

class XeSSFeature_Dx11 : public virtual IFeature_Dx11
{
  private:
    std::string _version = "1.3.0";

  protected:
    xess_context_handle_t _xessContext = nullptr;

    uint32_t _xessInitFlags = 0;
    int dumpCount = 0;

  public:
    // version is above 1.3 if we can use vulkan
    feature_version Version() final
    {
        return feature_version { XeSSProxy::VersionDx11().major, XeSSProxy::VersionDx11().minor,
                                 XeSSProxy::VersionDx11().patch };
    }

    bool InitInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(ID3D11DeviceContext* DeviceContext, NVSDK_NGX_Parameter* InParameters) override;
    Upscaler GetUpscalerType() const final { return Upscaler::XeSS; }
    API Api() const override { return IFeature_Dx11::Api(); }

    bool IsWithDx12() final { return false; }

    XeSSFeature_Dx11(unsigned int handleId, NVSDK_NGX_Parameter* InParameters);
    ~XeSSFeature_Dx11();
};
