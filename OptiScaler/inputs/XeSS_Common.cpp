#include "XeSS_Common.h"

#include "XeSS_Base.h"

#include <proxies/XeSS_Proxy.h>
#include <nvsdk_ngx_vk.h>

static std::optional<float> GetQualityOverrideRatio(const xess_quality_settings_t input)
{
    std::optional<float> output;

    auto sliderLimit = Config::Instance()->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;

    if (Config::Instance()->UpscaleRatioOverrideEnabled.value_or_default() &&
        Config::Instance()->UpscaleRatioOverrideValue.value_or_default() >= sliderLimit)
    {
        output = Config::Instance()->UpscaleRatioOverrideValue.value_or_default();

        return output;
    }

    if (!Config::Instance()->QualityRatioOverrideEnabled.value_or_default())
        return output; // override not enabled

    switch (input)
    {
    case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE:
        if (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_UltraPerformance.value_or_default();

        break;

    case XESS_QUALITY_SETTING_PERFORMANCE:
        if (Config::Instance()->QualityRatio_Performance.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Performance.value_or_default();

        break;

    case XESS_QUALITY_SETTING_BALANCED:
        if (Config::Instance()->QualityRatio_Balanced.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Balanced.value_or_default();

        break;

    case XESS_QUALITY_SETTING_QUALITY:
        if (Config::Instance()->QualityRatio_Quality.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Quality.value_or_default();

        break;

    case XESS_QUALITY_SETTING_ULTRA_QUALITY:
    case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS:
        if (Config::Instance()->QualityRatio_UltraQuality.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_UltraQuality.value_or_default();

        break;

    case XESS_QUALITY_SETTING_AA:
        if (Config::Instance()->QualityRatio_DLAA.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_DLAA.value_or_default();

        break;

    default:
        LOG_WARN("Unknown quality: {0}", (int) input);
        break;
    }

    return output;
}

xess_result_t hk_xessGetVersion(xess_version_t* pVersion)
{
    LOG_DEBUG("");

    pVersion->major = XeSSProxy::Version().major;
    pVersion->minor = XeSSProxy::Version().minor;
    pVersion->patch = XeSSProxy::Version().patch;
    pVersion->reserved = XeSSProxy::Version().reserved;

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessIsOptimalDriver(xess_context_handle_t hContext)
{
    LOG_DEBUG("");
    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessSetLoggingCallback(xess_context_handle_t hContext, xess_logging_level_t loggingLevel,
                                        xess_app_log_callback_t loggingCallback)
{
    LOG_DEBUG("");
    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessGetProperties(xess_context_handle_t hContext, const xess_2d_t* pOutputResolution,
                                   xess_properties_t* pBindingProperties)
{
    LOG_DEBUG("");
    *pBindingProperties = {};

    LOG_WARN("");
    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessDestroyContext(xess_context_handle_t hContext)
{
    LOG_DEBUG("hContext: {}", (size_t) hContext);

    if (!_contexts.contains(hContext))
        return XESS_RESULT_ERROR_INVALID_CONTEXT;

    if (_d3d12InitParams.contains(hContext))
    {
        NVSDK_NGX_D3D12_ReleaseFeature(_contexts[hContext]);
        _d3d12InitParams.erase(hContext);
    }

    if (_vkInitParams.contains(hContext))
    {
        NVSDK_NGX_VULKAN_ReleaseFeature(_contexts[hContext]);
        _vkInitParams.erase(hContext);
    }

    _contexts.erase(hContext);
    _nvParams.erase(hContext);

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessSetVelocityScale(xess_context_handle_t hContext, float x, float y)
{
    LOG_DEBUG("hContext: {}, x: {}, y: {}", (size_t) hContext, x, y);

    _motionScales[hContext] = { x, y };

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessForceLegacyScaleFactors(xess_context_handle_t hContext, bool force)
{
    LOG_DEBUG("");

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessGetExposureMultiplier(xess_context_handle_t hContext, float* pScale)
{
    LOG_DEBUG("");

    if (!_nvParams.contains(hContext))
        return XESS_RESULT_ERROR_INVALID_CONTEXT;

    if (_nvParams[hContext]->Get(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, pScale) == NVSDK_NGX_Result_Success)
        return XESS_RESULT_SUCCESS;

    return XESS_RESULT_ERROR_UNKNOWN;
}

xess_result_t hk_xessGetInputResolution(xess_context_handle_t hContext, const xess_2d_t* pOutputResolution,
                                        xess_quality_settings_t qualitySettings, xess_2d_t* pInputResolution)
{
    LOG_DEBUG("");

    unsigned int OutWidth;
    unsigned int OutHeight;
    float scalingRatio = 0.0f;

    const std::optional<float> QualityRatio = GetQualityOverrideRatio(qualitySettings);

    if (QualityRatio.has_value())
    {
        OutHeight = (unsigned int) ((float) pOutputResolution->y / QualityRatio.value());
        OutWidth = (unsigned int) ((float) pOutputResolution->x / QualityRatio.value());
        scalingRatio = 1.0f / QualityRatio.value();
    }
    else
    {
        switch (qualitySettings)
        {
        case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE:
            OutHeight = (unsigned int) ((float) pOutputResolution->y / 3.0);
            OutWidth = (unsigned int) ((float) pOutputResolution->x / 3.0);
            scalingRatio = 1.0f / 3.0f;
            break;

        case XESS_QUALITY_SETTING_PERFORMANCE:
            OutHeight = (unsigned int) ((float) pOutputResolution->y / 2.0);
            OutWidth = (unsigned int) ((float) pOutputResolution->x / 2.0);
            scalingRatio = 0.5f;
            break;

        case XESS_QUALITY_SETTING_BALANCED:
            OutHeight = (unsigned int) ((float) pOutputResolution->y / 1.7);
            OutWidth = (unsigned int) ((float) pOutputResolution->x / 1.7);
            scalingRatio = 1.0f / 1.7f;
            break;

        case XESS_QUALITY_SETTING_QUALITY:
            OutHeight = (unsigned int) ((float) pOutputResolution->y / 1.5);
            OutWidth = (unsigned int) ((float) pOutputResolution->x / 1.5);
            scalingRatio = 1.0f / 1.5f;
            break;

        case XESS_QUALITY_SETTING_ULTRA_QUALITY:
        case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS:
            OutHeight = (unsigned int) ((float) pOutputResolution->y / 1.3);
            OutWidth = (unsigned int) ((float) pOutputResolution->x / 1.3);
            scalingRatio = 1.0f / 1.3f;
            break;

        case XESS_QUALITY_SETTING_AA:
            OutHeight = pOutputResolution->y;
            OutWidth = pOutputResolution->x;
            scalingRatio = 1.0f;
            break;

        default:
            OutHeight = (unsigned int) ((float) pOutputResolution->y / 1.7);
            OutWidth = (unsigned int) ((float) pOutputResolution->x / 1.7);
            scalingRatio = 1.0f / 1.7f;
            break;
        }
    }

    if (Config::Instance()->RoundInternalResolution.has_value())
    {
        OutHeight -= OutHeight % Config::Instance()->RoundInternalResolution.value();
        OutWidth -= OutWidth % Config::Instance()->RoundInternalResolution.value();
        scalingRatio = (float) OutWidth / (float) pOutputResolution->x;
    }

    pInputResolution->x = OutWidth;
    pInputResolution->y = OutHeight;

    LOG_DEBUG("Display Resolution: {0}x{1} Render Resolution: {2}x{3}, Quality: {4}", pOutputResolution->x,
              pOutputResolution->y, pInputResolution->x, pInputResolution->y, (UINT) qualitySettings);

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessGetIntelXeFXVersion(xess_context_handle_t hContext, xess_version_t* pVersion)
{
    LOG_DEBUG("");

    pVersion->major = 2;
    pVersion->minor = 0;
    pVersion->patch = 0;
    pVersion->reserved = 0;

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessGetJitterScale(xess_context_handle_t hContext, float* pX, float* pY)
{
    LOG_DEBUG("");

    if (!_jitterScales.contains(hContext))
        return XESS_RESULT_ERROR_INVALID_CONTEXT;

    auto scales = &_jitterScales[hContext];

    *pX = scales->x;
    *pY = scales->y;

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessGetOptimalInputResolution(xess_context_handle_t hContext, const xess_2d_t* pOutputResolution,
                                               xess_quality_settings_t qualitySettings,
                                               xess_2d_t* pInputResolutionOptimal, xess_2d_t* pInputResolutionMin,
                                               xess_2d_t* pInputResolutionMax)
{
    LOG_DEBUG("pOutputResolution: {}x{}", pOutputResolution->x, pOutputResolution->y);

    unsigned int OutWidth;
    unsigned int OutHeight;
    float scalingRatio = 0.0f;

    const std::optional<float> QualityRatio = GetQualityOverrideRatio(qualitySettings);

    if (QualityRatio.has_value())
    {
        OutHeight = (unsigned int) ((float) pOutputResolution->y / QualityRatio.value());
        OutWidth = (unsigned int) ((float) pOutputResolution->x / QualityRatio.value());
        scalingRatio = 1.0f / QualityRatio.value();
    }
    else
    {
        switch (qualitySettings)
        {
        case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE:
            OutHeight = (unsigned int) ((float) pOutputResolution->y / 3.0);
            OutWidth = (unsigned int) ((float) pOutputResolution->x / 3.0);
            scalingRatio = 0.33333333f;
            break;

        case XESS_QUALITY_SETTING_PERFORMANCE:
            OutHeight = (unsigned int) ((float) pOutputResolution->y / 2.0);
            OutWidth = (unsigned int) ((float) pOutputResolution->x / 2.0);
            scalingRatio = 0.5f;
            break;

        case XESS_QUALITY_SETTING_BALANCED:
            OutHeight = (unsigned int) ((float) pOutputResolution->y / 1.7);
            OutWidth = (unsigned int) ((float) pOutputResolution->x / 1.7);
            scalingRatio = 1.0f / 1.7f;
            break;

        case XESS_QUALITY_SETTING_QUALITY:
            OutHeight = (unsigned int) ((float) pOutputResolution->y / 1.5);
            OutWidth = (unsigned int) ((float) pOutputResolution->x / 1.5);
            scalingRatio = 1.0f / 1.5f;
            break;

        case XESS_QUALITY_SETTING_ULTRA_QUALITY:
        case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS:
            OutHeight = (unsigned int) ((float) pOutputResolution->y / 1.3);
            OutWidth = (unsigned int) ((float) pOutputResolution->x / 1.3);
            scalingRatio = 1.0f / 1.3f;
            break;

        case XESS_QUALITY_SETTING_AA:
            OutHeight = pOutputResolution->y;
            OutWidth = pOutputResolution->x;
            scalingRatio = 1.0f;
            break;

        default:
            OutHeight = (unsigned int) ((float) pOutputResolution->y / 1.7);
            OutWidth = (unsigned int) ((float) pOutputResolution->x / 1.7);
            scalingRatio = 1.0f / 1.7f;
            break;
        }
    }

    if (Config::Instance()->RoundInternalResolution.has_value())
    {
        OutHeight -= OutHeight % Config::Instance()->RoundInternalResolution.value();
        OutWidth -= OutWidth % Config::Instance()->RoundInternalResolution.value();
        scalingRatio = (float) OutWidth / (float) pOutputResolution->x;
    }

    pInputResolutionOptimal->x = OutWidth;
    pInputResolutionOptimal->y = OutHeight;
    pInputResolutionMin->x = (unsigned int) ((float) pOutputResolution->x / 3.0);
    pInputResolutionMin->y = (unsigned int) ((float) pOutputResolution->y / 3.0);
    pInputResolutionMax->x = pOutputResolution->x;
    pInputResolutionMax->y = pOutputResolution->y;

    LOG_DEBUG("pInputResolutionOptimal: {}x{}, pInputResolutionMin: {}x{}, pInputResolutionMax: {}x{}",
              pInputResolutionOptimal->x, pInputResolutionOptimal->y, pInputResolutionMin->x, pInputResolutionMin->y,
              pInputResolutionMax->x, pInputResolutionMax->y);

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessGetVelocityScale(xess_context_handle_t hContext, float* pX, float* pY)
{
    LOG_DEBUG("");

    if (!_motionScales.contains(hContext))
        return XESS_RESULT_ERROR_INVALID_CONTEXT;

    auto scales = &_motionScales[hContext];

    *pX = scales->x;
    *pY = scales->y;

    return XESS_RESULT_ERROR_UNKNOWN;
}

xess_result_t hk_xessSetJitterScale(xess_context_handle_t hContext, float x, float y)
{
    LOG_DEBUG("x: {}, y: {}", x, y);

    _jitterScales[hContext] = { x, y };

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessSetExposureMultiplier(xess_context_handle_t hContext, float scale)
{
    LOG_DEBUG("");

    if (!_nvParams.contains(hContext))
        return XESS_RESULT_ERROR_INVALID_CONTEXT;

    _nvParams[hContext]->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, scale);

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessSetContextParameterF()
{
    LOG_DEBUG("");
    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessAILGetDecision()
{
    LOG_DEBUG("");
    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessAILGetVersion()
{
    LOG_DEBUG("");
    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessGetPipelineBuildStatus(xess_context_handle_t hContext)
{
    LOG_DEBUG("");
    return XESS_RESULT_SUCCESS;
}
