#pragma once

#include "pch.h"

#include "State.h"

#include <optional>
#include <filesystem>

enum HasDefaultValue
{
    WithDefault,
    NoDefault,
    SoftDefault // Change always gets saved to the config
};

template <class T, HasDefaultValue defaultState = WithDefault> class CustomOptional : public std::optional<T>
{
  private:
    T _defaultValue;
    std::optional<T> _configIni;
    bool _volatile;

  public:
    CustomOptional(T defaultValue)
        requires(defaultState != NoDefault)
        : std::optional<T>(), _defaultValue(std::move(defaultValue)), _configIni(std::nullopt), _volatile(false)
    {
    }

    CustomOptional()
        requires(defaultState == NoDefault)
        : std::optional<T>(), _defaultValue(T {}), _configIni(std::nullopt), _volatile(false)
    {
    }

    // Prevents a change from being saved to ini
    constexpr void set_volatile_value(const T& value)
    {
        if (!_volatile)
        { // make sure the previously set value is saved
            if (this->has_value())
                _configIni = this->value();
            else
                _configIni = std::nullopt;
        }
        _volatile = true;
        std::optional<T>::operator=(value);
    }

    // Use this when first setting a CustomOptional
    constexpr void set_from_config(const std::optional<T>& opt)
    {
        if (!this->has_value())
        {
            _configIni = opt;
            std::optional<T>::operator=(opt);
        }
    }

    constexpr CustomOptional& operator=(const T& value)
    {
        _volatile = false;
        std::optional<T>::operator=(value);
        return *this;
    }

    constexpr CustomOptional& operator=(T&& value)
    {
        _volatile = false;
        std::optional<T>::operator=(std::move(value));
        return *this;
    }

    constexpr CustomOptional& operator=(const std::optional<T>& opt)
    {
        _volatile = false;
        std::optional<T>::operator=(opt);
        return *this;
    }

    constexpr CustomOptional& operator=(std::optional<T>&& opt)
    {
        _volatile = false;
        std::optional<T>::operator=(std::move(opt));
        return *this;
    }

    // Needed for string literals for some reason
    constexpr CustomOptional& operator=(const char* value)
        requires std::same_as<T, std::string>
    {
        _volatile = false;
        std::optional<T>::operator=(T(value));
        return *this;
    }

    constexpr T value_or_default() const&
        requires(defaultState != NoDefault)
    {
        return this->has_value() ? this->value() : _defaultValue;
    }

    constexpr T value_or_default() &&
        requires(defaultState != NoDefault) {
            return this->has_value() ? std::move(this->value()) : std::move(_defaultValue);
        }

        constexpr std::optional<T> value_for_config(bool forceSave = false)
            requires(defaultState == WithDefault)
    {
        if (_volatile)
        {
            if (_configIni != _defaultValue)
                return _configIni;

            return std::nullopt;
        }

        if (!this->has_value() || (!forceSave && *this == _defaultValue))
            return std::nullopt;

        return this->value();
    }

    constexpr std::optional<T> value_for_config_ignore_default()
        requires(defaultState == WithDefault)
    {
        if (_volatile)
            return _configIni;

        if (this->has_value())
            return this->value();

        return std::nullopt;
    }

    constexpr std::optional<T> value_for_config()
        requires(defaultState != WithDefault)
    {
        if (_volatile)
            return _configIni;

        if (this->has_value())
            return this->value();

        return std::nullopt;
    }

    constexpr T value_for_config_or(T other)
    {
        auto option = value_for_config();

        if (option.has_value())
            return option.value();
        else
            return other;
    }
};

constexpr inline int UnboundKey = -1;

enum FpsOverlay : uint32_t
{
    FpsOverlay_JustFPS,
    FpsOverlay_Simple,
    FpsOverlay_Detailed,
    FpsOverlay_DetailedGraph,
    FpsOverlay_Full,
    FpsOverlay_FullGraph,
    FpsOverlay_ReflexTimings,
    FpsOverlay_COUNT,
};

class Config
{
  public:
    Config();

    // Init flags
    CustomOptional<bool, NoDefault> DepthInverted;
    CustomOptional<bool, NoDefault> AutoExposure;
    CustomOptional<bool, NoDefault> HDR;
    CustomOptional<bool, NoDefault> JitterCancellation;
    CustomOptional<bool, NoDefault> DisplayResolution;
    CustomOptional<bool, NoDefault> DisableReactiveMask;
    CustomOptional<float> DlssReactiveMaskBias { 0.45f };

    // Logging
    CustomOptional<bool> LogToFile { false };
    CustomOptional<bool> LogToConsole { false };
    CustomOptional<bool> LogToDebug { false };
    CustomOptional<bool> LogToNGX { false };
    CustomOptional<bool> OpenConsole { false };
    CustomOptional<bool> DebugWait { false }; // not in ini
    CustomOptional<int> LogLevel { 1 };
    CustomOptional<std::wstring> LogFileName { L"OptiScaler.log" };
    CustomOptional<bool> LogSingleFile { true };
    CustomOptional<bool> LogAsync { false };
    CustomOptional<int> LogAsyncThreads { 4 };

    // XeSS
    CustomOptional<bool> BuildPipelines { true };
    CustomOptional<int32_t> NetworkModel { 0 };
    CustomOptional<bool> CreateHeaps { true };
    CustomOptional<std::wstring, NoDefault> XeSSLibrary;
    CustomOptional<std::wstring, NoDefault> XeSSDx11Library;

    // DLSS
    CustomOptional<bool> DLSSEnabled { true };
    CustomOptional<std::wstring, NoDefault> NvngxPath;
    CustomOptional<std::wstring, NoDefault> NVNGX_DLSS_Library;
    CustomOptional<std::wstring, NoDefault> DLSSFeaturePath;
    CustomOptional<bool> RenderPresetOverride { false };
    CustomOptional<uint32_t> RenderPresetForAll { 0 };
    CustomOptional<uint32_t> RenderPresetDLAA { 0 };
    CustomOptional<uint32_t> RenderPresetUltraQuality { 0 };
    CustomOptional<uint32_t> RenderPresetQuality { 0 };
    CustomOptional<uint32_t> RenderPresetBalanced { 0 };
    CustomOptional<uint32_t> RenderPresetPerformance { 0 };
    CustomOptional<uint32_t> RenderPresetUltraPerformance { 0 };

    // Nukems
    CustomOptional<bool> MakeDepthCopy { false };

    // CAS
    CustomOptional<bool> RcasEnabled { false };
    CustomOptional<bool> MotionSharpnessEnabled { false };
    CustomOptional<bool> MotionSharpnessDebug { false };
    CustomOptional<float> MotionSharpness { 0.4f };
    CustomOptional<float> MotionThreshold { 0.0f };
    CustomOptional<float> MotionScaleLimit { 10.0f };

    // Sharpness
    CustomOptional<bool> OverrideSharpness { false };
    CustomOptional<float> Sharpness { 0.3f };
    CustomOptional<bool> ContrastEnabled { false };
    CustomOptional<float> Contrast { 0.0f };

    // Menu
    CustomOptional<float> MenuScale { 1.0f };
    CustomOptional<bool> OverlayMenu { true };
    CustomOptional<int> ShortcutKey { VK_INSERT };
    CustomOptional<bool> ExtendedLimits { false };
    CustomOptional<bool> ShowFps { false };
    /// 0 Top Left, 1 Top Right, 2 Bottom Left, 3 Bottom Right
    CustomOptional<int> FpsOverlayPos { 0 };
    /// 0 Only FPS, 1 +Avg FPS & Upscaler info 2 +Frame Time,
    /// 3 +Upscaler Time, 4 +Frame Time Graph, 5 +Upscaler Time Graph
    /// 6 +Reflex timings
    CustomOptional<FpsOverlay> FpsOverlayType { FpsOverlay_JustFPS };
    CustomOptional<int> FpsShortcutKey { VK_PRIOR };
    CustomOptional<int> FpsCycleShortcutKey { VK_NEXT };
    CustomOptional<bool> FpsOverlayHorizontal { false };
    CustomOptional<float> FpsOverlayAlpha { 0.4f };
    CustomOptional<float, NoDefault> FpsScale; // No value means same as MenuScale
    CustomOptional<bool> UseHQFont { true };
    CustomOptional<bool> DisableSplash { false };
    CustomOptional<std::wstring, NoDefault> TTFFontPath;
    CustomOptional<int> FGShortcutKey { VK_END };

    // Hooks
    CustomOptional<bool> HookOriginalNvngxOnly { false };
    CustomOptional<bool> EarlyHooking { false };
    CustomOptional<bool> UseNtdllHooks { true };

    // Upscale Ratio Override
    CustomOptional<bool> UpscaleRatioOverrideEnabled { false };
    CustomOptional<float> UpscaleRatioOverrideValue { 1.3f };

    // DRS
    CustomOptional<bool> DrsMinOverrideEnabled { false };
    CustomOptional<bool> DrsMaxOverrideEnabled { false };

    // Quality Overrides
    CustomOptional<bool> QualityRatioOverrideEnabled { false };
    CustomOptional<float> QualityRatio_DLAA { 1.0f };
    CustomOptional<float> QualityRatio_UltraQuality { 1.3f };
    CustomOptional<float> QualityRatio_Quality { 1.5f };
    CustomOptional<float> QualityRatio_Balanced { 1.7f };
    CustomOptional<float> QualityRatio_Performance { 2.0f };
    CustomOptional<float> QualityRatio_UltraPerformance { 3.0f };

    // Hotfixes
    CustomOptional<bool> CheckForUpdate { true };
    CustomOptional<bool> DisableOverlays { false };

    CustomOptional<float, NoDefault> MipmapBiasOverride; // disabled by default
    CustomOptional<bool> MipmapBiasFixedOverride { false };
    CustomOptional<bool> MipmapBiasScaleOverride { false };
    CustomOptional<bool> MipmapBiasOverrideAll { false };

    CustomOptional<int, NoDefault> AnisotropyOverride; // disabled by default
    CustomOptional<bool> OverrideShaderSampler { true };
    CustomOptional<bool> AnisotropyModifyComp { true };
    CustomOptional<bool> AnisotropyModifyMinMax { true };
    CustomOptional<bool> AnisotropySkipPointFilter { true };

    CustomOptional<int, NoDefault> RoundInternalResolution; // disabled by default

    CustomOptional<int, NoDefault> SkipFirstFrames; // disabled by default
    CustomOptional<bool> RestoreComputeSignature { false };
    CustomOptional<bool> RestoreGraphicSignature { false };

    CustomOptional<bool> UsePrecompiledShaders { true };

    CustomOptional<bool> UseGenericAppIdWithDlss { false };
    CustomOptional<bool> PreferDedicatedGpu { false };
    CustomOptional<bool> PreferFirstDedicatedGpu { false };

    CustomOptional<int32_t, NoDefault> ColorResourceBarrier;    // disabled by default
    CustomOptional<int32_t, NoDefault> MVResourceBarrier;       // disabled by default
    CustomOptional<int32_t, NoDefault> DepthResourceBarrier;    // disabled by default
    CustomOptional<int32_t, NoDefault> ExposureResourceBarrier; // disabled by default
    CustomOptional<int32_t, NoDefault> MaskResourceBarrier;     // disabled by default
    CustomOptional<int32_t, NoDefault> OutputResourceBarrier;   // disabled by default

    CustomOptional<bool> DontCreateD3D12DeviceForLuma { false };

    // Upscalers
    CustomOptional<std::string, SoftDefault> Dx11Upscaler { "fsr22" };
    CustomOptional<std::string, SoftDefault> Dx12Upscaler { "xess" };
    CustomOptional<std::string, SoftDefault> VulkanUpscaler { "fsr21" };

    // Output Scaling
    CustomOptional<bool> OutputScalingEnabled { false };
    CustomOptional<float> OutputScalingMultiplier { 1.5f };
    CustomOptional<bool> OutputScalingUseFsr { true };
    CustomOptional<uint32_t> OutputScalingDownscaler { 0 }; // 0 = Bicubic | 1 = Lanczos | 2 = Catmull-Rom | 3 = MAGC

    // FSR
    CustomOptional<bool> FsrDebugView { false };
    CustomOptional<int> FfxUpscalerIndex { 0 };
    CustomOptional<int> FfxFGIndex { 0 };
    CustomOptional<bool> FsrUseMaskForTransparency { true };
    CustomOptional<bool> Fsr4Update { false };
    CustomOptional<uint32_t, NoDefault> Fsr4Model;
    CustomOptional<bool> Fsr4EnableDebugView { false };
    CustomOptional<bool> Fsr4EnableWatermark { false };
    CustomOptional<bool> FsrNonLinearColorSpace { false };
    CustomOptional<bool> FsrNonLinearSRGB { false };
    CustomOptional<bool> FsrNonLinearPQ { false };
    CustomOptional<bool> FsrAgilitySDKUpgrade { false };
    CustomOptional<float> FsrVelocity { 1.0f };
    CustomOptional<float> FsrReactiveScale { 1.0f };
    CustomOptional<float> FsrShadingScale { 1.0f };
    CustomOptional<float> FsrAccAddPerFrame { 0.333f };
    CustomOptional<float> FsrMinDisOccAcc { -0.333f };

    // FSR Common
    CustomOptional<float> FsrVerticalFov { 60.0f };
    CustomOptional<float> FsrHorizontalFov { 0.0f }; // off by default
    CustomOptional<float> FsrCameraNear { 0.1f };
    CustomOptional<float> FsrCameraFar { 100000.0f };
    CustomOptional<bool> FsrUseFsrInputValues { true };

    CustomOptional<std::wstring, NoDefault> FfxDx12Path;
    CustomOptional<std::wstring, NoDefault> FfxVkPath;

    // dx11wdx12
    CustomOptional<bool> Dx11DelayedInit { false };
    CustomOptional<bool> DontUseNTShared { false };

    // NVAPI Override
    CustomOptional<bool> OverrideNvapiDll { false };
    CustomOptional<std::wstring, NoDefault> NvapiDllPath;
    CustomOptional<bool> DisableFlipMetering { false };

    // Spoofing
    CustomOptional<bool> DxgiSpoofing { true };
    CustomOptional<bool> DxgiFactoryWrapping { false };
    CustomOptional<bool> StreamlineSpoofing { true };
    CustomOptional<std::string, NoDefault> DxgiBlacklist; // disabled by default
    CustomOptional<int, NoDefault> DxgiVRAM;              // disabled by default
    CustomOptional<bool> VulkanSpoofing { false };
    CustomOptional<bool> VulkanExtensionSpoofing { false };
    CustomOptional<int, NoDefault> VulkanVRAM; // disabled by default
    CustomOptional<bool> SpoofHAGS { false };
    CustomOptional<bool> SpoofFeatureLevel { false };
    CustomOptional<uint32_t> SpoofedVendorId { VendorId::Nvidia };
    CustomOptional<uint32_t> SpoofedDeviceId { 0x2684 };
    CustomOptional<uint32_t, NoDefault> TargetVendorId;
    CustomOptional<uint32_t, NoDefault> TargetDeviceId;
    CustomOptional<std::wstring> SpoofedGPUName { L"NVIDIA GeForce RTX 4090" };
    CustomOptional<bool> UESpoofIntelAtomics64 { false };

    // Plugins
    CustomOptional<std::wstring> PluginPath { L"plugins" };
    CustomOptional<bool> LoadSpecialK { false };
    CustomOptional<bool> LoadReShade { false };
    CustomOptional<bool> LoadAsiPlugins { false };

    // Frame Generation
    CustomOptional<FGInput> FGInput { FGInput::NoFG };
    CustomOptional<FGOutput> FGOutput { FGOutput::NoFG };
    CustomOptional<bool> FGDrawUIOverFG { false };
    CustomOptional<bool> FGUIPremultipliedAlpha { true };
    CustomOptional<bool> FGDisableHudless { false };
    CustomOptional<bool> FGDisableUI { true };
    CustomOptional<bool> FGSkipReset { false };
    CustomOptional<int> FGAllowedFrameAhead { 1 };

    // OptiFG
    CustomOptional<bool> FGEnabled { false };
    CustomOptional<bool> FGUseMutexForSwapchain { true };
    CustomOptional<bool> FGMakeMVCopy { true };
    CustomOptional<bool> FGMakeDepthCopy { true };
    CustomOptional<bool> FGResourceFlip { false };
    CustomOptional<bool> FGResourceFlipOffset { false };
    CustomOptional<bool> FGAlwaysCaptureFSRFGSwapchain { false };

    CustomOptional<int, NoDefault> FGRectLeft;
    CustomOptional<int, NoDefault> FGRectTop;
    CustomOptional<int, NoDefault> FGRectWidth;
    CustomOptional<int, NoDefault> FGRectHeight;

    // OptiFG - Hudfix
    CustomOptional<bool> FGHUDFix { false };
    CustomOptional<int> FGHUDLimit { 1 };
    CustomOptional<bool> FGHUDFixExtended { false };
    CustomOptional<bool> FGImmediateCapture { false };
    CustomOptional<bool> FGDontUseSwapchainBuffers { false };
    CustomOptional<bool> FGRelaxedResolutionCheck { false };
    CustomOptional<bool> FGHudfixDisableRTV { false };
    CustomOptional<bool> FGHudfixDisableSRV { false };
    CustomOptional<bool> FGHudfixDisableUAV { false };
    CustomOptional<bool> FGHudfixDisableOM { false };
    CustomOptional<bool> FGHudfixDisableDispatch { false };
    CustomOptional<bool> FGHudfixDisableDI { false };
    CustomOptional<bool> FGHudfixDisableDII { false };
    CustomOptional<bool> FGHudfixDisableSCR { true };
    CustomOptional<bool> FGHudfixDisableSGR { true };

    // OptiFG - Resource Tracking
    CustomOptional<bool> FGAlwaysTrackHeaps { true };
    CustomOptional<bool> FGResourceBlocking { false };

    // OptiFG - DLSS-D Depth scale
    CustomOptional<bool> FGEnableDepthScale { false };
    CustomOptional<float> FGDepthScaleMax { 10000.0f };

    // FSR-FG
    CustomOptional<bool> FGDebugView { false };
    CustomOptional<bool> FGDebugResetLines { false };
    CustomOptional<bool> FGDebugTearLines { false };
    CustomOptional<bool> FGDebugPacingLines { false };
    CustomOptional<bool> FGAsync { false };
    CustomOptional<bool> FGFramePacingTuning { true };
    CustomOptional<float> FGFPTSafetyMarginInMs { 0.01f };
    CustomOptional<float> FGFPTVarianceFactor { 0.3f };
    CustomOptional<bool> FGFPTAllowHybridSpin { false };
    CustomOptional<int> FGFPTHybridSpinTime { 2 };
    CustomOptional<bool> FGFPTAllowWaitForSingleObjectOnFence { false };

    CustomOptional<bool> FSRFGSkipConfigForHudless { false };
    CustomOptional<bool> FSRFGSkipDispatchForHudless { false };
    CustomOptional<bool> FSRFGDepthAndVelocityValidNow { false };
    CustomOptional<bool> FSRFGEnableWatermark { false };

    // OptiFG - XeFG
    CustomOptional<bool> FGXeFGIgnoreInitChecks { false };
    CustomOptional<bool> FGXeFGDepthInverted { false };
    CustomOptional<bool> FGXeFGJitteredMV { false };
    CustomOptional<bool> FGXeFGHighResMV { false };
    CustomOptional<bool> FGXeFGDebugView { false };
    CustomOptional<bool> FGXeFGForceBorderless { false };
    CustomOptional<bool> FGXeFGSkipResizeBuffers { true };
    CustomOptional<bool> FGXeFGModifyBufferState { false };
    CustomOptional<bool> FGXeFGModifySCIndex { false };

    // fakenvapi
    CustomOptional<bool> FN_EnableLogs { true };
    CustomOptional<bool> FN_EnableTraceLogs { false };
    CustomOptional<bool> FN_ForceLatencyFlex { false };
    CustomOptional<uint32_t> FN_LatencyFlexMode { 0 }; // conservative - aggressive - reflex ids
    CustomOptional<uint32_t> FN_ForceReflex { 0 };     // in-game - force disable - force enable

    // Inputs
    CustomOptional<bool> EnableDlssInputs { true };
    CustomOptional<bool> EnableXeSSInputs { true };
    CustomOptional<bool> UseFsr2Inputs { true };
    CustomOptional<bool> Fsr2Pattern { false };
    CustomOptional<bool> UseFsr3Inputs { true };
    CustomOptional<bool> Fsr3Pattern { false };
    CustomOptional<bool> UseFfxInputs { true };
    CustomOptional<bool> EnableHotSwapping { false };
    CustomOptional<bool> EnableFsr2Inputs { true };
    CustomOptional<bool> EnableFsr3Inputs { true };
    CustomOptional<bool> EnableFfxInputs { true };

    // Framerate
    CustomOptional<float> FramerateLimit { 0.0f };

    // HDR
    CustomOptional<bool> ForceHDR { false };
    CustomOptional<bool> UseHDR10 { false };
    CustomOptional<bool> SkipColorSpace { false };

    // V-Sync
    CustomOptional<bool> OverrideVsync { false };
    CustomOptional<bool, NoDefault> ForceVsync;
    CustomOptional<UINT> VsyncInterval { 0 };

    bool LoadFromPath(const wchar_t* InPath);
    bool SaveIni();
    bool SaveXeFG();

    bool ReloadFakenvapi();
    bool SaveFakenvapiIni();

    void CheckUpscalerFiles();

    std::vector<std::string> GetConfigLog();

    static Config* Instance();

  private:
    inline static Config* _config;
    inline static std::vector<std::string> _log;

    std::filesystem::path absoluteFileName;
    std::wstring fileName = L"OptiScaler.ini";

    bool Reload(std::filesystem::path iniPath);

    std::optional<std::string> readString(std::string section, std::string key, bool lowercase = false);
    std::optional<std::wstring> readWString(std::string section, std::string key, bool lowercase = false);
    std::optional<float> readFloat(std::string section, std::string key);
    std::optional<int> readInt(std::string section, std::string key);
    std::optional<uint32_t> readUInt(std::string section, std::string key);
    std::optional<bool> readBool(std::string section, std::string key);
};
