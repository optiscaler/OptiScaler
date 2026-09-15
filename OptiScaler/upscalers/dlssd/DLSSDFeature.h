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
    // One flag per API, not one flag for all three.
    //
    // This was a single shared inline static, and sharing it is a bug with a precise victim: a D3D11
    // game that uses native Ray Reconstruction and is then switched onto the D3D12 bridge. The D3D11 feature
    // initialises first and sets the flag; the D3D12 feature then sees it already set, skips
    // NVNGXProxy::InitDx12 entirely, and every D3D12 accessor on the proxy gates on _dx12Inited and
    // returns nullptr. The symptom is "_CreateFeature is nullptr" logged in the same microsecond as
    // "Creating DLSS feature" -- no 500ms init delay in between, because no init happened -- and a
    // silent fall back to FSR.
    //
    // Found on the DLSS side, where Bannerlord hit it; identical here. The proxy's own InitDx11/InitDx12/InitVulkan are already
    // idempotent and already keep a flag apiece, so these exist only to skip the deliberate delay
    // between init and feature creation, and they have to be counted the same way the proxy counts.
    inline static bool _dlssdInitedDx11 = false;
    inline static bool _dlssdInitedDx12 = false;
    inline static bool _dlssdInitedVk = false;

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
