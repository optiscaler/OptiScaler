#pragma once
#include "SysUtils.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>

#include <unordered_set>
#include <Util.h>

#define DLSS_MOD_ID_OFFSET 1000000

inline static unsigned int handleCounter = DLSS_MOD_ID_OFFSET;

struct InitFlags
{
    bool IsHdr;
    bool SharpenEnabled;
    bool LowResMV;
    bool AutoExposure;
    bool DepthInverted;
    bool JitteredMV;
};

static auto sumOpts(const auto&... opts) -> std::optional<double>
{
    if ((opts.has_value() || ... || false))
    {
        return (opts.value_or(0.0) + ... + 0.0);
    }

    return std::nullopt;
}

struct DetailedGpuTime
{
    std::string name;
    double time = 0.0;
    bool includedInUpscalerTime = false;
};

class IFeature
{
  private:
    bool _isInited = false;
    int _featureFlags = 0;
    InitFlags _initFlags = {};

    NVSDK_NGX_PerfQuality_Value _perfQualityValue;

    struct JitterInfo
    {
        float x;
        float y;
    };

    struct hashFunction
    {
        size_t operator()(const std::pair<float, float>& p) const
        {
            size_t h1 = std::hash<float>()(p.first);
            size_t h2 = std::hash<float>()(p.second);
            return h1 ^ (h2 << 1);
        }
    };

    std::unordered_set<std::pair<float, float>, hashFunction> _jitterInfo;

  protected:
    // D3D11with12
    inline static ID3D12Device* _dx11on12Device = nullptr;
    inline static ID3D12Device* _localDx11on12Device = nullptr;

    bool _initParameters = false;
    NVSDK_NGX_Handle* _handle = nullptr;

    float _sharpness = 0; // Used by the feature itself, might get spoofed to 0 when RCAS is used
    std::optional<float> _actualSharpness = std::nullopt;
    bool _hasColor = false;
    bool _hasDepth = false;
    bool _hasMV = false;
    bool _hasTM = false;
    bool _accessToReactiveMask = false;
    bool _hasExposure = false;
    bool _hasOutput = false;
    bool _depthLinear = false;

    unsigned int _renderWidth = 0;
    unsigned int _renderHeight = 0;
    unsigned int _targetWidth = 0;
    unsigned int _targetHeight = 0;
    unsigned int _displayWidth = 0;
    unsigned int _displayHeight = 0;

    long _frameCount = 0;
    bool _featureFrozen = false;
    bool _moduleLoaded = false;

    std::optional<double> lastUpscalerTime {};
    std::optional<double> lastRcasTime {};
    std::optional<double> lastOutputScalingTime {};

    void SetHandle(unsigned int InHandleId);
    bool SetInitParameters(NVSDK_NGX_Parameter* InParameters);
    void GetRenderResolution(const NVSDK_NGX_Parameter* InParameters, unsigned int* OutWidth, unsigned int* OutHeight);
    void GetDynamicOutputResolution(NVSDK_NGX_Parameter* InParameters, unsigned int* width, unsigned int* height);
    float GetSharpness(const NVSDK_NGX_Parameter* InParameters);

    virtual void SetInit(bool InValue) { _isInited = InValue; }

  public:
    NVSDK_NGX_Handle* Handle() const { return _handle; };
    static unsigned int GetNextHandleId() { return handleCounter++; }
    int GetFeatureFlags() const { return _featureFlags; }

    virtual bool IsWithDx12() = 0;
    virtual feature_version Version() = 0;
    virtual Upscaler GetUpscalerType() const = 0;
    virtual API Api() const = 0;
    std::string Name() const { return UpscalerDisplayName(GetUpscalerType()); };
    std::string ShortName() const { return UpscalerShortName(GetUpscalerType()); }; // Without the version
    virtual std::optional<double> ReadUpscalerTime(void* commandQueue) { return std::nullopt; }
    virtual void ReadDetailedGpuTimes(void* commandQueue, std::vector<DetailedGpuTime>& detailedGpuTimes) {};

    virtual size_t JitterCount() { return _jitterInfo.size(); }

    virtual void TickFrozenCheck();
    virtual bool IsFrozen() { return _featureFrozen; };
    virtual bool UpdateOutputResolution(const NVSDK_NGX_Parameter* InParameters);
    virtual unsigned int DisplayWidth() { return _displayWidth; };
    virtual unsigned int DisplayHeight() { return _displayHeight; };
    virtual unsigned int TargetWidth() { return _targetWidth; };
    virtual unsigned int TargetHeight() { return _targetHeight; };
    virtual unsigned int RenderWidth() { return _renderWidth; };
    virtual unsigned int RenderHeight() { return _renderHeight; };
    virtual NVSDK_NGX_PerfQuality_Value PerfQualityValue() { return _perfQualityValue; }
    virtual bool IsInitParameters() { return _initParameters; };
    virtual bool IsInited() { return _isInited; }
    virtual float Sharpness()
    {
        if (_actualSharpness.has_value())
            return _actualSharpness.value();
        return _sharpness;
    }
    virtual bool HasColor() { return _hasColor; }
    virtual bool HasDepth() { return _hasDepth; }
    virtual bool HasMV() { return _hasMV; }
    virtual bool HasTM() { return _hasTM; }
    virtual bool AccessToReactiveMask() { return _accessToReactiveMask; }
    virtual bool HasExposure() { return _hasExposure; }
    virtual bool HasOutput() { return _hasOutput; }
    virtual bool ModuleLoaded() { return _moduleLoaded; }
    virtual long FrameCount() { return _frameCount; }
    virtual bool DepthLinear() { return _depthLinear; }

    virtual bool AutoExposure() { return _initFlags.AutoExposure; }
    virtual bool DepthInverted() { return _initFlags.DepthInverted; }
    virtual bool IsHdr() { return _initFlags.IsHdr; }
    virtual bool JitteredMV() { return _initFlags.JitteredMV; }
    virtual bool LowResMV() { return _initFlags.LowResMV; }
    virtual bool SharpenEnabled() { return _initFlags.SharpenEnabled; }

    virtual bool CallsUpscalerEndByItself() { return false; }

    IFeature(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters) { SetHandle(InHandleId); }

    virtual ~IFeature() {}
};
