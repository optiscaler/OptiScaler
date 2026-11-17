#pragma once

#include "SysUtils.h"
#include <proxies/Nvngx_Proxy.h>
#include <upscalers/IFeature.h>
#include <proxies/Nvngx_RRDllProxy.h>

class DLSSDFeature : public virtual IFeature
{
  private:
    feature_version _version = { 0, 0, 0 };

  protected:
    NVSDK_NGX_Handle _dlssdHandle = {};
    NVSDK_NGX_Handle* _p_dlssdHandle = nullptr;
    bool _dlssdInited = false;

    // Custom DLSSD, nullptr for NVIDIA's
    Nvngx_RRDllProxy* _custom = nullptr;
    std::string _customName;

    void ProcessEvaluateParams(NVSDK_NGX_Parameter* InParameters);
    void ProcessInitParams(NVSDK_NGX_Parameter* InParameters);
    void ReadVersion();

    float GetSharpness(const NVSDK_NGX_Parameter* InParameters);

  public:
    feature_version Version() override { return feature_version { _version.major, _version.minor, _version.patch }; }
    Upscaler GetUpscalerType() const override { return Upscaler::DLSSD; }

    // Empty for NVIDIA's
    const std::string& CustomName() const { return _customName; }

    DLSSDFeature(unsigned int handleId, NVSDK_NGX_Parameter* InParameters);

    ~DLSSDFeature();
};
