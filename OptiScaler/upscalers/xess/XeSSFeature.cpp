#pragma once
#include "XeSSFeature.h"

#include <pch.h>
#include <Config.h>
#include <Util.h>

#include <include/detours/detours.h>
#include <include/d3dx/d3dx12.h>

inline void XeSSLogCallback(const char* Message, xess_logging_level_t Level)
{
    auto logLevel = (int) Level + 1;
    spdlog::log((spdlog::level::level_enum) logLevel, "XeSSFeature::LogCallback XeSS Runtime ({0})", Message);
}

bool XeSSFeature::InitXeSS(ID3D12Device* device, const NVSDK_NGX_Parameter* InParameters)
{

    return true;
}

XeSSFeature::XeSSFeature(unsigned int handleId, NVSDK_NGX_Parameter* InParameters) : IFeature(handleId, InParameters)
{
    _initParameters = SetInitParameters(InParameters);
}

bool XeSSFeature::Init(ApiContext* context, const NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();
    if (IsInited())
        return true;

    if (!_moduleLoaded)
    {
        LOG_ERROR("libxess.dll not loaded!");
        return false;
    }

    if (!CheckInitializationContext(context))
    {
        LOG_ERROR("XeSSFeature::Init context is not valid!");
        return false;
    }

    State::Instance().skipSpoofing = true;

    auto ret = CreateXessContext(context, &_xessContext);

    if (ret != XESS_RESULT_SUCCESS)
    {
        LOG_ERROR("xessD3D12CreateContext error: {0}", ResultToString(ret));
        return false;
    }

    ret = XeSSProxy::IsOptimalDriver()(_xessContext);
    LOG_DEBUG("xessIsOptimalDriver : {0}", ResultToString(ret));

    ret = XeSSProxy::SetLoggingCallback()(_xessContext, XESS_LOGGING_LEVEL_DEBUG, XeSSLogCallback);
    LOG_DEBUG("xessSetLoggingCallback : {0}", ResultToString(ret));

    uint32_t initFlags = GetInitFlags();

    xess_quality_settings_t qualitySetting = GetQualitySetting();

    if (Config::Instance()->OutputScalingEnabled.value_or(false) && LowResMV())
    {
        float ssMulti = Config::Instance()->OutputScalingMultiplier.value_or(1.5f);

        if (ssMulti < 0.5f)
        {
            ssMulti = 0.5f;
            Config::Instance()->OutputScalingMultiplier = ssMulti;
        }
        else if (ssMulti > 3.0f)
        {
            ssMulti = 3.0f;
            Config::Instance()->OutputScalingMultiplier = ssMulti;
        }

        _targetWidth = DisplayWidth() * ssMulti;
        _targetHeight = DisplayHeight() * ssMulti;
    }
    else
    {
        _targetWidth = DisplayWidth();
        _targetHeight = DisplayHeight();
    }

    if (Config::Instance()->ExtendedLimits.value_or(false) && RenderWidth() > DisplayWidth())
    {
        _targetWidth = RenderWidth();
        _targetHeight = RenderHeight();

        // enable output scaling to restore image
        if (LowResMV())
        {
            Config::Instance()->OutputScalingMultiplier = 1.0f;
            Config::Instance()->OutputScalingEnabled = true;
        }
    }

    xess_2d_t outputResolution = {TargetWidth(), TargetHeight()};

    XessInitParams xessParams = CreateInitParams(outputResolution, qualitySetting, initFlags);

    if (Config::Instance()->NetworkModel.has_value() && Config::Instance()->NetworkModel.value() >= 0 &&
        Config::Instance()->NetworkModel.value() <= 5)
    {
        ret = XeSSProxy::SelectNetworkModel()(_xessContext,
                                              (xess_network_model_t) Config::Instance()->NetworkModel.value());
        LOG_ERROR("xessSelectNetworkModel result: {0}", ResultToString(ret));
    }

    ret = ApiInit(context, &xessParams);

    State::Instance().skipSpoofing = false;

    if (ret != XESS_RESULT_SUCCESS)
    {
        LOG_ERROR("xessD3D12Init error: {0}", ResultToString(ret));
        return false;
    }

    SetInit(true);

    InitMenuAndOutput(context);

    return true;
}

bool XeSSFeature::Evaluate(EvalContext* context, const NVSDK_NGX_Parameter* InParameters,
                           const NVSDK_NGX_Parameter* OutParameters)
{
    return false;
}

uint32_t XeSSFeature::GetInitFlags()
{
    uint32_t initFlags = XESS_INIT_FLAG_NONE;
    if (DepthInverted())
        initFlags |= XESS_INIT_FLAG_INVERTED_DEPTH;

    if (AutoExposure())
        initFlags |= XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
    else
        initFlags |= XESS_INIT_FLAG_EXPOSURE_SCALE_TEXTURE;

    if (!IsHdr())
        initFlags |= XESS_INIT_FLAG_LDR_INPUT_COLOR;

    if (JitteredMV())
        initFlags |= XESS_INIT_FLAG_JITTERED_MV;

    if (!LowResMV())
        initFlags |= XESS_INIT_FLAG_HIGH_RES_MV;

    if (!Config::Instance()->DisableReactiveMask.value_or(true))
    {
        initFlags |= XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;
        LOG_DEBUG("xessParams.initFlags (ReactiveMaskActive) {0:b}", initFlags);
    }
    return initFlags;
}

xess_quality_settings_t XeSSFeature::GetQualitySetting()
{
    switch (PerfQualityValue())
    {
    case NVSDK_NGX_PerfQuality_Value_UltraPerformance:
        if (Version().major >= 1 && Version().minor >= 3)
            return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
        else
            return XESS_QUALITY_SETTING_PERFORMANCE;
        break;
    case NVSDK_NGX_PerfQuality_Value_MaxPerf:
        if (Version().major >= 1 && Version().minor >= 3)
            return XESS_QUALITY_SETTING_BALANCED;
        else
            return XESS_QUALITY_SETTING_PERFORMANCE;
        break;
    case NVSDK_NGX_PerfQuality_Value_Balanced:
        if (Version().major >= 1 && Version().minor >= 3)
            return XESS_QUALITY_SETTING_QUALITY;
        else
            return XESS_QUALITY_SETTING_BALANCED;
        break;
    case NVSDK_NGX_PerfQuality_Value_MaxQuality:
        if (Version().major >= 1 && Version().minor >= 3)
            return XESS_QUALITY_SETTING_ULTRA_QUALITY;
        else
            return XESS_QUALITY_SETTING_QUALITY;
        break;
    case NVSDK_NGX_PerfQuality_Value_UltraQuality:
        if (Version().major >= 1 && Version().minor >= 3)
            return XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS;
        else
            return XESS_QUALITY_SETTING_ULTRA_QUALITY;
        break;

    case NVSDK_NGX_PerfQuality_Value_DLAA:
        if (Version().major >= 1 && Version().minor >= 3)
            return XESS_QUALITY_SETTING_AA;
        else
            return XESS_QUALITY_SETTING_ULTRA_QUALITY;
        break;
    }
    return XESS_QUALITY_SETTING_BALANCED;
}

XeSSFeature::~XeSSFeature()
{
    if (State::Instance().isShuttingDown || _xessContext == nullptr)
        return;

    if (_xessContext)
    {
        XeSSProxy::DestroyContext()(_xessContext);
        _xessContext = nullptr;
    }
}
