#pragma once
#include "SysUtils.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>

#include <array>
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
    bool DynamicResolution;
};

static auto sumOpts(const auto&... opts) -> std::optional<double>
{
    if ((opts.has_value() || ... || false))
    {
        return (opts.value_or(0.0) + ... + 0.0);
    }

    return std::nullopt;
}

// DLSS subrects, where each input and the output of this feature's view are in the game's textures
enum class SubrectSlot : uint8_t
{
    Color,
    Depth,
    MV,
    Mask, // Bias current color mask
    Output,
    Count
};

struct Subrect
{
    uint32_t X = 0;
    uint32_t Y = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;

    // A view at (0, 0) is used as is
    bool HasOffset() const { return X != 0 || Y != 0; }

    // The rest of the texture can hold other views
    bool IsPartOf(uint64_t InWidth, uint32_t InHeight) const
    {
        return HasOffset() || Width < InWidth || Height < InHeight;
    }
};

// Base X/Y parameter names, in SubrectSlot order
inline constexpr std::array<std::pair<const char*, const char*>, (size_t) SubrectSlot::Count> SubrectBaseKeys = { {
    { NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y },
    { NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y },
    { NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y },
    { NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_X,
      NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_Y },
    { NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y },
} };

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

    std::array<Subrect, (size_t) SubrectSlot::Count> _subrects {};
    bool _outputSubrects = false;
    uint32_t _subrectLogged = 0; // Bit per slot, log once per feature
    uint32_t _subrectWarned = 0;
    bool _integerMaskWarned = false;

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
    void ReadSubrects(const NVSDK_NGX_Parameter* InParameters);
    const Subrect& GetSubrect(SubrectSlot InSlot) const { return _subrects[(size_t) InSlot]; }
    // Warns once when the subrect is outside the texture, otherwise logs it once
    bool SubrectFits(SubrectSlot InSlot, const Subrect& InRect, uint64_t InWidth, uint32_t InHeight);
    // Upscalers and passes work on the view at (0, 0) of our buffers, a last pass writes it to the output base.
    // Native subrect upscalers without passes write there by themselves.
    bool PlacesOutput(const Subrect& InRect, bool InHasPasses) const;
    static void SetSubrectBase(NVSDK_NGX_Parameter* InParameters, SubrectSlot InSlot, uint32_t InX, uint32_t InY);
    // Warns once that a mask with this integer format is ignored
    void WarnIntegerMask(uint32_t InFormat);
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

    virtual void TickFrozenCheck(uint32_t presentPerEval = 1);
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
    virtual bool DynamicResolution() { return _initFlags.DynamicResolution; }
    virtual bool SharpenEnabled() { return _initFlags.SharpenEnabled; }

    virtual bool CallsUpscalerEndByItself() { return false; }

    IFeature(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters) { SetHandle(InHandleId); }

    virtual ~IFeature() {}
};
