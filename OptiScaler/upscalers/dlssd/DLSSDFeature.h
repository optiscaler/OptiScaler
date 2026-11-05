#pragma once

#include "SysUtils.h"
#include <proxies/NVNGX_Proxy.h>
#include <upscalers/IFeature.h>

class DLSSDFeature : public virtual IFeature
{
  private:
    feature_version _version = { 0, 0, 0 };

  protected:
    NVSDK_NGX_Handle _dlssdHandle = {};
    NVSDK_NGX_Handle* _p_dlssdHandle = nullptr;
    bool _dlssdInited = false;

    void ProcessEvaluateParams(NVSDK_NGX_Parameter* InParameters);
    void ProcessInitParams(NVSDK_NGX_Parameter* InParameters);
    void ReadVersion();

    float GetSharpness(const NVSDK_NGX_Parameter* InParameters);

  public:
    feature_version Version() override { return feature_version { _version.major, _version.minor, _version.patch }; }
    Upscaler GetUpscalerType() const override { return Upscaler::DLSSD; }

    DLSSDFeature(unsigned int handleId, NVSDK_NGX_Parameter* InParameters);

    ~DLSSDFeature();
};
