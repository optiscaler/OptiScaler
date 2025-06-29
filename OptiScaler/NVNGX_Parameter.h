#pragma once
#include "pch.h"
#include "Config.h"
#include "DLSSG_Mod.h"

#include <ankerl/unordered_dense.h>

// Use real NVNGX params encapsulated in custom one
// Which is not working correctly
// #define ENABLE_ENCAPSULATED_PARAMS

// Log NVParam Set/Get operations
// #define LOG_PARAMS_VALUES

#ifdef LOG_PARAMS_VALUES
#define LOG_PARAM(msg, ...) spdlog::trace(__FUNCTION__ " " msg, ##__VA_ARGS__)
#else
#define LOG_PARAM(msg, ...)
#endif

inline static std::optional<float> GetQualityOverrideRatio(const NVSDK_NGX_PerfQuality_Value input)
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
    case NVSDK_NGX_PerfQuality_Value_UltraPerformance:
        if (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_UltraPerformance.value_or_default();

        break;

    case NVSDK_NGX_PerfQuality_Value_MaxPerf:
        if (Config::Instance()->QualityRatio_Performance.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Performance.value_or_default();

        break;

    case NVSDK_NGX_PerfQuality_Value_Balanced:
        if (Config::Instance()->QualityRatio_Balanced.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Balanced.value_or_default();

        break;

    case NVSDK_NGX_PerfQuality_Value_MaxQuality:
        if (Config::Instance()->QualityRatio_Quality.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Quality.value_or_default();

        break;

    case NVSDK_NGX_PerfQuality_Value_UltraQuality:
        if (Config::Instance()->QualityRatio_UltraQuality.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_UltraQuality.value_or_default();

        break;

    case NVSDK_NGX_PerfQuality_Value_DLAA:
        if (Config::Instance()->QualityRatio_DLAA.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_DLAA.value_or_default();

        break;

    default:
        LOG_WARN("Unknown quality: {0}", (int) input);
        break;
    }

    return output;
}

inline static NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_DLSS_GetOptimalSettingsCallback(NVSDK_NGX_Parameter* InParams)
{
    unsigned int Width;
    unsigned int Height;
    unsigned int OutWidth;
    unsigned int OutHeight;
    float scalingRatio = 0.0f;
    int PerfQualityValue;

    if (InParams->Get(NVSDK_NGX_Parameter_Width, &Width) != NVSDK_NGX_Result_Success ||
        InParams->Get(NVSDK_NGX_Parameter_Height, &Height) != NVSDK_NGX_Result_Success ||
        InParams->Get(NVSDK_NGX_Parameter_PerfQualityValue, &PerfQualityValue) != NVSDK_NGX_Result_Success)
        return NVSDK_NGX_Result_Fail;

    auto enumPQValue = (NVSDK_NGX_PerfQuality_Value) PerfQualityValue;

    LOG_DEBUG("Display Resolution: {0}x{1}", Width, Height);

    const std::optional<float> QualityRatio = GetQualityOverrideRatio(enumPQValue);

    if (QualityRatio.has_value())
    {
        OutHeight = (unsigned int) ((float) Height / QualityRatio.value());
        OutWidth = (unsigned int) ((float) Width / QualityRatio.value());
        scalingRatio = 1.0f / QualityRatio.value();
    }
    else
    {
        LOG_DEBUG("Quality: {0}", PerfQualityValue);

        switch (enumPQValue)
        {
        case NVSDK_NGX_PerfQuality_Value_UltraPerformance:
            OutHeight = (unsigned int) ((float) Height / 3.0);
            OutWidth = (unsigned int) ((float) Width / 3.0);
            scalingRatio = 0.33333333f;
            break;

        case NVSDK_NGX_PerfQuality_Value_MaxPerf:
            OutHeight = (unsigned int) ((float) Height / 2.0);
            OutWidth = (unsigned int) ((float) Width / 2.0);
            scalingRatio = 0.5f;
            break;

        case NVSDK_NGX_PerfQuality_Value_Balanced:
            OutHeight = (unsigned int) ((float) Height / 1.7);
            OutWidth = (unsigned int) ((float) Width / 1.7);
            scalingRatio = 1.0f / 1.7f;
            break;

        case NVSDK_NGX_PerfQuality_Value_MaxQuality:
            OutHeight = (unsigned int) ((float) Height / 1.5);
            OutWidth = (unsigned int) ((float) Width / 1.5);
            scalingRatio = 1.0f / 1.5f;
            break;

        case NVSDK_NGX_PerfQuality_Value_UltraQuality:
            OutHeight = (unsigned int) ((float) Height / 1.3);
            OutWidth = (unsigned int) ((float) Width / 1.3);
            scalingRatio = 1.0f / 1.3f;
            break;

        case NVSDK_NGX_PerfQuality_Value_DLAA:
            OutHeight = Height;
            OutWidth = Width;
            scalingRatio = 1.0f;
            break;

        default:
            OutHeight = (unsigned int) ((float) Height / 1.7);
            OutWidth = (unsigned int) ((float) Width / 1.7);
            scalingRatio = 1.0f / 1.7f;
            break;
        }
    }

    if (Config::Instance()->RoundInternalResolution.has_value())
    {
        OutHeight -= OutHeight % Config::Instance()->RoundInternalResolution.value();
        OutWidth -= OutWidth % Config::Instance()->RoundInternalResolution.value();
        scalingRatio = (float) OutWidth / (float) Width;
    }

    InParams->Set(NVSDK_NGX_Parameter_Scale, scalingRatio);
    InParams->Set(NVSDK_NGX_Parameter_SuperSampling_ScaleFactor, scalingRatio);
    InParams->Set(NVSDK_NGX_Parameter_OutWidth, OutWidth);
    InParams->Set(NVSDK_NGX_Parameter_OutHeight, OutHeight);

    // DRS minimum resolution
    if (Config::Instance()->DrsMinOverrideEnabled.value_or_default() || enumPQValue == NVSDK_NGX_PerfQuality_Value_DLAA)
    {
        InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, OutWidth);
        InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, OutHeight);
    }
    else
    {
        if (Config::Instance()->ExtendedLimits.value_or_default() && OutWidth > Width)
        {
            InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, OutWidth);
            InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, OutHeight);
        }
        else
        {
            // DLSS normally only supports DRS in range of 0.5 and 1.0
            auto drsMinWidth = (unsigned int) ((float) Width * 0.5f);
            auto drsMinHeight = (unsigned int) ((float) Height * 0.5f);

            if (OutWidth < drsMinWidth || OutHeight < drsMinHeight)
            {
                InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, OutWidth);
                InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, OutHeight);
            }
            else
            {
                InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, drsMinWidth);
                InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, drsMinHeight);
            }
        }
    }

    // DRS maximum resolution

    if (Config::Instance()->DrsMaxOverrideEnabled.value_or_default())
    {
        InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width, OutWidth);
        InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height, OutHeight);
    }
    else
    {
        if (Config::Instance()->ExtendedLimits.value_or_default() && OutWidth > Width)
        {
            InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width, OutWidth);
            InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height, OutHeight);
        }
        else
        {
            InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width, Width);
            InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height, Height);
        }
    }

    InParams->Set(NVSDK_NGX_Parameter_SizeInBytes, Width * Height * 31);
    InParams->Set(NVSDK_NGX_Parameter_DLSSMode, NVSDK_NGX_DLSS_Mode_DLSS_DLISP);

    InParams->Set(NVSDK_NGX_EParameter_Scale, scalingRatio);
    InParams->Set(NVSDK_NGX_EParameter_OutWidth, OutWidth);
    InParams->Set(NVSDK_NGX_EParameter_OutHeight, OutHeight);
    InParams->Set(NVSDK_NGX_EParameter_SizeInBytes, Width * Height * 31);
    InParams->Set(NVSDK_NGX_EParameter_DLSSMode, NVSDK_NGX_DLSS_Mode_DLSS_DLISP);

    LOG_DEBUG("NVSDK_NGX_DLSS_GetOptimalSettingsCallback: Display Resolution: {0}x{1} Render Resolution: {2}x{3}",
              Width, Height, OutWidth, OutHeight);
    return NVSDK_NGX_Result_Success;
}

inline static NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_DLSSD_GetOptimalSettingsCallback(NVSDK_NGX_Parameter* InParams)
{
    unsigned int Width;
    unsigned int Height;
    unsigned int OutWidth;
    unsigned int OutHeight;
    float scalingRatio = 0.0f;
    int PerfQualityValue;

    if (InParams->Get(NVSDK_NGX_Parameter_Width, &Width) != NVSDK_NGX_Result_Success ||
        InParams->Get(NVSDK_NGX_Parameter_Height, &Height) != NVSDK_NGX_Result_Success ||
        InParams->Get(NVSDK_NGX_Parameter_PerfQualityValue, &PerfQualityValue) != NVSDK_NGX_Result_Success)
        return NVSDK_NGX_Result_Fail;

    auto enumPQValue = (NVSDK_NGX_PerfQuality_Value) PerfQualityValue;

    LOG_DEBUG("Display Resolution: {0}x{1}", Width, Height);

    const std::optional<float> QualityRatio = GetQualityOverrideRatio(enumPQValue);

    if (QualityRatio.has_value())
    {
        OutHeight = (unsigned int) ((float) Height / QualityRatio.value());
        OutWidth = (unsigned int) ((float) Width / QualityRatio.value());
        scalingRatio = 1.0f / QualityRatio.value();
    }
    else
    {
        LOG_DEBUG("Quality: {0}", PerfQualityValue);

        switch (enumPQValue)
        {
        case NVSDK_NGX_PerfQuality_Value_UltraPerformance:
            OutHeight = (unsigned int) ((float) Height / 3.0);
            OutWidth = (unsigned int) ((float) Width / 3.0);
            scalingRatio = 0.33333333f;
            break;

        case NVSDK_NGX_PerfQuality_Value_MaxPerf:
            OutHeight = (unsigned int) ((float) Height / 2.0);
            OutWidth = (unsigned int) ((float) Width / 2.0);
            scalingRatio = 0.5f;
            break;

        case NVSDK_NGX_PerfQuality_Value_Balanced:
            OutHeight = (unsigned int) ((float) Height / 1.7);
            OutWidth = (unsigned int) ((float) Width / 1.7);
            scalingRatio = 1.0f / 1.7f;
            break;

        case NVSDK_NGX_PerfQuality_Value_MaxQuality:
            OutHeight = (unsigned int) ((float) Height / 1.5);
            OutWidth = (unsigned int) ((float) Width / 1.5);
            scalingRatio = 1.0f / 1.5f;
            break;

        case NVSDK_NGX_PerfQuality_Value_UltraQuality:
            OutHeight = (unsigned int) ((float) Height / 1.3);
            OutWidth = (unsigned int) ((float) Width / 1.3);
            scalingRatio = 1.0f / 1.3f;
            break;

        case NVSDK_NGX_PerfQuality_Value_DLAA:
            OutHeight = Height;
            OutWidth = Width;
            scalingRatio = 1.0f;
            break;

        default:
            OutHeight = (unsigned int) ((float) Height / 1.7);
            OutWidth = (unsigned int) ((float) Width / 1.7);
            scalingRatio = 1.0f / 1.7f;
            break;
        }
    }

    if (Config::Instance()->RoundInternalResolution.has_value())
    {
        OutHeight -= OutHeight % Config::Instance()->RoundInternalResolution.value();
        OutWidth -= OutWidth % Config::Instance()->RoundInternalResolution.value();
    }

    InParams->Set(NVSDK_NGX_Parameter_Scale, scalingRatio);
    InParams->Set(NVSDK_NGX_Parameter_SuperSampling_ScaleFactor, scalingRatio);
    InParams->Set(NVSDK_NGX_Parameter_OutWidth, OutWidth);
    InParams->Set(NVSDK_NGX_Parameter_OutHeight, OutHeight);

    // DRS minimum resolution
    if (Config::Instance()->DrsMinOverrideEnabled.value_or_default())
    {
        InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, OutWidth);
        InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, OutHeight);
    }
    else if (enumPQValue == NVSDK_NGX_PerfQuality_Value_DLAA)
    {
        InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, Width);
        InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, Height);
    }
    else
    {
        // DLSS normally only supports DRS in range of 0.5 and 1.0
        auto drsMinWidth = (unsigned int) ((float) Width * 0.5f);
        auto drsMinHeight = (unsigned int) ((float) Height * 0.5f);

        if (OutWidth < drsMinWidth || OutHeight < drsMinHeight)
        {
            InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, OutWidth);
            InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, OutHeight);
        }
        else
        {
            InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, drsMinWidth);
            InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, drsMinHeight);
        }
    }

    // DRS maximum resolution
    if (Config::Instance()->DrsMaxOverrideEnabled.value_or_default())
    {
        InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width, OutWidth);
        InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height, OutHeight);
    }
    else
    {
        InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width, Width);
        InParams->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height, Height);
    }

    InParams->Set(NVSDK_NGX_Parameter_SizeInBytes, Width * Height * 31);
    InParams->Set(NVSDK_NGX_Parameter_DLSSMode, NVSDK_NGX_DLSS_Mode_DLSS_DLISP);

    InParams->Set(NVSDK_NGX_EParameter_Scale, scalingRatio);
    InParams->Set(NVSDK_NGX_EParameter_OutWidth, OutWidth);
    InParams->Set(NVSDK_NGX_EParameter_OutHeight, OutHeight);
    InParams->Set(NVSDK_NGX_EParameter_SizeInBytes, Width * Height * 31);
    InParams->Set(NVSDK_NGX_EParameter_DLSSMode, NVSDK_NGX_DLSS_Mode_DLSS_DLISP);

    LOG_DEBUG("Display Resolution: {0}x{1} Render Resolution: {2}x{3}", Width, Height, OutWidth, OutHeight);
    return NVSDK_NGX_Result_Success;
}

inline static NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_DLSS_GetStatsCallback(NVSDK_NGX_Parameter* InParams)
{
    LOG_DEBUG("NVSDK_NGX_DLSS_GetStatsCallback");

    if (!InParams)
        return NVSDK_NGX_Result_Success;

    unsigned int Width = 1920;
    unsigned int Height = 1080;

    InParams->Get(NVSDK_NGX_Parameter_Width, &Width);
    InParams->Get(NVSDK_NGX_Parameter_Height, &Height);
    InParams->Set(NVSDK_NGX_Parameter_SizeInBytes, Width * Height * 31);

    return NVSDK_NGX_Result_Success;
}

inline static void InitNGXParameters(NVSDK_NGX_Parameter* InParams)
{
    InParams->Set(NVSDK_NGX_Parameter_SuperSampling_Available, 1);

    if (State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
        State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine)
    {
        InParams->Set(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, 10);
        InParams->Set(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, 10);
    }
    else
    {
        InParams->Set(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, 0);
        InParams->Set(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, 0);
    }

    InParams->Set(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, 0);
    InParams->Set(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, 1);
    InParams->Set(NVSDK_NGX_Parameter_OptLevel, 0);
    InParams->Set(NVSDK_NGX_Parameter_IsDevSnippetBranch, 0);
    InParams->Set(NVSDK_NGX_Parameter_DLSSOptimalSettingsCallback, NVSDK_NGX_DLSS_GetOptimalSettingsCallback);
    InParams->Set("DLSSDOptimalSettingsCallback", NVSDK_NGX_DLSSD_GetOptimalSettingsCallback);
    InParams->Set(NVSDK_NGX_Parameter_DLSSGetStatsCallback, NVSDK_NGX_DLSS_GetStatsCallback);
    InParams->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
    InParams->Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f);
    InParams->Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);
    InParams->Set(NVSDK_NGX_Parameter_MV_Offset_X, 0.0f);
    InParams->Set(NVSDK_NGX_Parameter_MV_Offset_Y, 0.0f);
    InParams->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0f);
    InParams->Set(NVSDK_NGX_Parameter_PerfQualityValue, 0);
    InParams->Set(NVSDK_NGX_Parameter_SizeInBytes, 1920 * 1080 * 31);

    InParams->Set(NVSDK_NGX_EParameter_SuperSampling_Available, 1);
    InParams->Set(NVSDK_NGX_EParameter_OptLevel, 0);
    InParams->Set(NVSDK_NGX_Parameter_FreeMemOnReleaseFeature, 0);
    InParams->Set(NVSDK_NGX_EParameter_IsDevSnippetBranch, 0);
    InParams->Set(NVSDK_NGX_EParameter_DLSSOptimalSettingsCallback, NVSDK_NGX_DLSS_GetOptimalSettingsCallback);
    InParams->Set(NVSDK_NGX_EParameter_Sharpness, 0.0f);
    InParams->Set(NVSDK_NGX_EParameter_MV_Scale_X, 1.0f);
    InParams->Set(NVSDK_NGX_EParameter_MV_Scale_Y, 1.0f);
    InParams->Set(NVSDK_NGX_EParameter_MV_Offset_X, 0.0f);
    InParams->Set(NVSDK_NGX_EParameter_MV_Offset_Y, 0.0f);

    InParams->Set("RayReconstruction.Hint.Render.Preset.DLAA",
                  (unsigned int) NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
    InParams->Set("RayReconstruction.Hint.Render.Preset.UltraQuality",
                  (unsigned int) NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
    InParams->Set("RayReconstruction.Hint.Render.Preset.Quality",
                  (unsigned int) NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
    InParams->Set("RayReconstruction.Hint.Render.Preset.Balanced",
                  (unsigned int) NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
    InParams->Set("RayReconstruction.Hint.Render.Preset.Performance",
                  (unsigned int) NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
    InParams->Set("RayReconstruction.Hint.Render.Preset.UltraPerformance",
                  (unsigned int) NVSDK_NGX_DLSS_Hint_Render_Preset_Default);

    InParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
                  (unsigned int) NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
    InParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality,
                  (unsigned int) NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
    InParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                  (unsigned int) NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
    InParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
                  (unsigned int) NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
    InParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                  (unsigned int) NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
    InParams->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
                  (unsigned int) NVSDK_NGX_DLSS_Hint_Render_Preset_Default);

    InParams->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1);
    InParams->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1);
    InParams->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 1);
    InParams->Set(NVSDK_NGX_Parameter_RTXValue, 0);

    // not ideal as it doesn't take different APIs into account
    if (DLSSGMod::isLoaded())
    {
        InParams->Set("FrameGeneration.Available", 1);
        InParams->Set("FrameInterpolation.Available", 1);
        InParams->Set(NVSDK_NGX_Parameter_FrameInterpolation_NeedsUpdatedDriver, 0);
        InParams->Set(NVSDK_NGX_Parameter_FrameInterpolation_FeatureInitResult, 1);
        InParams->Set(NVSDK_NGX_Parameter_FrameInterpolation_MinDriverVersionMajor, 0);
    }
}

struct Parameter
{
    template <typename T> void operator=(T value)
    {
        key = typeid(T).hash_code();
        if constexpr (std::is_same<T, float>::value)
            values.f = value;
        else if constexpr (std::is_same<T, int>::value)
            values.i = value;
        else if constexpr (std::is_same<T, unsigned int>::value)
            values.ui = value;
        else if constexpr (std::is_same<T, double>::value)
            values.d = value;
        else if constexpr (std::is_same<T, unsigned long long>::value)
            values.ull = value;
        else if constexpr (std::is_same<T, void*>::value)
            values.vp = value;
        else if constexpr (std::is_same<T, ID3D11Resource*>::value)
            values.d11r = value;
        else if constexpr (std::is_same<T, ID3D12Resource*>::value)
            values.d12r = value;
    }

    template <typename T> operator T() const
    {
        T v = {};
        if constexpr (std::is_same<T, float>::value)
        {
            if (key == typeid(unsigned long long).hash_code())
                v = (T) values.ull;
            else if (key == typeid(float).hash_code())
                v = (T) values.f;
            else if (key == typeid(double).hash_code())
                v = (T) values.d;
            else if (key == typeid(unsigned int).hash_code())
                v = (T) values.ui;
            else if (key == typeid(int).hash_code())
                v = (T) values.i;
        }
        else if constexpr (std::is_same<T, int>::value)
        {
            if (key == typeid(unsigned long long).hash_code())
                v = (T) values.ull;
            else if (key == typeid(float).hash_code())
                v = (T) values.f;
            else if (key == typeid(double).hash_code())
                v = (T) values.d;
            else if (key == typeid(int).hash_code())
                v = (T) values.i;
            else if (key == typeid(unsigned int).hash_code())
                v = (T) values.ui;
        }
        else if constexpr (std::is_same<T, unsigned int>::value)
        {
            if (key == typeid(unsigned long long).hash_code())
                v = (T) values.ull;
            else if (key == typeid(float).hash_code())
                v = (T) values.f;
            else if (key == typeid(double).hash_code())
                v = (T) values.d;
            else if (key == typeid(int).hash_code())
                v = (T) values.i;
            else if (key == typeid(unsigned int).hash_code())
                v = (T) values.ui;
        }
        else if constexpr (std::is_same<T, double>::value)
        {
            if (key == typeid(unsigned long long).hash_code())
                v = (T) values.ull;
            else if (key == typeid(float).hash_code())
                v = (T) values.f;
            else if (key == typeid(double).hash_code())
                v = (T) values.d;
            else if (key == typeid(int).hash_code())
                v = (T) values.i;
            else if (key == typeid(unsigned int).hash_code())
                v = (T) values.ui;
        }
        else if constexpr (std::is_same<T, unsigned long long>::value)
        {
            if (key == typeid(unsigned long long).hash_code())
                v = (T) values.ull;
            else if (key == typeid(float).hash_code())
                v = (T) values.f;
            else if (key == typeid(double).hash_code())
                v = (T) values.d;
            else if (key == typeid(int).hash_code())
                v = (T) values.i;
            else if (key == typeid(unsigned int).hash_code())
                v = (T) values.ui;
            else if (key == typeid(void*).hash_code())
                v = (T) values.vp;
        }
        else if constexpr (std::is_same<T, void*>::value)
        {
            if (key == typeid(void*).hash_code())
                v = values.vp;
        }
        else if constexpr (std::is_same<T, ID3D11Resource*>::value)
        {
            if (key == typeid(ID3D11Resource*).hash_code())
                v = values.d11r;
            else if (key == typeid(void*).hash_code())
                v = (T) values.vp;
        }
        else if constexpr (std::is_same<T, ID3D12Resource*>::value)
        {
            if (key == typeid(ID3D12Resource*).hash_code())
                v = values.d12r;
            else if (key == typeid(void*).hash_code())
                v = (T) values.vp;
        }

        return v;
    }

    union
    {
        float f;
        double d;
        int i;
        unsigned int ui;
        unsigned long long ull;
        void* vp;
        ID3D11Resource* d11r;
        ID3D12Resource* d12r;
    } values;

    size_t key = 0;
};

struct NVNGX_Parameters : public NVSDK_NGX_Parameter
{
    std::string Name;

#ifdef ENABLE_ENCAPSULATED_PARAMS
    NVSDK_NGX_Parameter* OriginalParam = nullptr;
#endif // ENABLE_ENCAPSULATED_PARAMS

    void Set(const char* key, unsigned long long value) override
    {
        LOG_PARAM("ulong('{0}', {1})", key, value);
        setT(key, value);
    }
    void Set(const char* key, float value) override
    {
        LOG_PARAM("float('{0}', {1})", key, value);
        setT(key, value);
    }
    void Set(const char* key, double value) override
    {
        LOG_PARAM("double('{0}', {1})", key, value);
        setT(key, value);
    }
    void Set(const char* key, unsigned int value) override
    {
        LOG_PARAM("uint('{0}', {1})", key, value);
        setT(key, value);
    }
    void Set(const char* key, int value) override
    {
        LOG_PARAM("int('{0}', {1})", key, value);
        setT(key, value);
    }
    void Set(const char* key, void* value) override
    {
        LOG_PARAM("void('{0}', '{1}null')", key, value == nullptr ? "" : "not ");
        setT(key, value);
    }
    void Set(const char* key, ID3D11Resource* value) override
    {
        LOG_PARAM("d3d11('{0}', '{1}null')", key, value == nullptr ? "" : "not ");
        setT(key, value);
    }
    void Set(const char* key, ID3D12Resource* value) override
    {
        LOG_PARAM("d3d12('{0}', '{1}null')", key, value == nullptr ? "" : "not ");
        setT(key, value);
    }

    NVSDK_NGX_Result Get(const char* key, unsigned long long* value) const override
    {
        auto result = getT(key, value);
        if (result == NVSDK_NGX_Result_Success)
        {
            LOG_PARAM("ulong('{0}', {1})", key, *value);
            return NVSDK_NGX_Result_Success;
        }

#ifdef ENABLE_ENCAPSULATED_PARAMS
        if (OriginalParam != nullptr)
        {
            LOG_PARAM("calling original ulong('{0}')", key);
            result = OriginalParam->Get(key, value);
            LOG_PARAM("calling original ulong('{0}') result: {1:X}", key, (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
            {
                LOG_PARAM("from original ulong('{0}', {1})", key, *value);
                return result;
            }
        }
#endif // ENABLE_ENCAPSULATED_PARAMS

        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char* key, float* value) const override
    {
        auto result = getT(key, value);
        if (result == NVSDK_NGX_Result_Success)
        {
            LOG_PARAM("float('{0}', {1})", key, *value);
            return NVSDK_NGX_Result_Success;
        }

#ifdef ENABLE_ENCAPSULATED_PARAMS
        if (OriginalParam != nullptr)
        {
            LOG_PARAM("calling original float('{0}')", key);
            result = OriginalParam->Get(key, value);
            LOG_PARAM("calling original float('{0}') result: {1:X}", key, (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
            {
                LOG_PARAM("from original float('{0}', {1})", key, *value);
                return result;
            }
        }
#endif // ENABLE_ENCAPSULATED_PARAMS

        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char* key, double* value) const override
    {
        auto result = getT(key, value);
        if (result == NVSDK_NGX_Result_Success)
        {
            LOG_PARAM("double('{0}', {1})", key, *value);
            return NVSDK_NGX_Result_Success;
        }

#ifdef ENABLE_ENCAPSULATED_PARAMS
        if (OriginalParam != nullptr)
        {
            LOG_PARAM("calling original double('{0}')", key);
            result = OriginalParam->Get(key, value);
            LOG_PARAM("calling original double('{0}') result: {1:X}", key, (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
            {
                LOG_PARAM("from original double('{0}', {1})", key, *value);
                return result;
            }
        }
#endif // ENABLE_ENCAPSULATED_PARAMS

        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char* key, unsigned int* value) const override
    {
        auto result = getT(key, value);
        if (result == NVSDK_NGX_Result_Success)
        {
            LOG_PARAM("uint('{0}', {1})", key, *value);
            return NVSDK_NGX_Result_Success;
        }

#ifdef ENABLE_ENCAPSULATED_PARAMS
        if (OriginalParam != nullptr)
        {
            LOG_PARAM("calling original uint('{0}')", key);
            result = OriginalParam->Get(key, value);
            LOG_PARAM("calling original uint('{0}') result: {1:X}", key, (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
            {
                LOG_PARAM("from original uint('{0}', {1})", key, *value);
                return result;
            }
        }
#endif // ENABLE_ENCAPSULATED_PARAMS

        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char* key, int* value) const override
    {
        auto result = getT(key, value);
        if (result == NVSDK_NGX_Result_Success)
        {
            LOG_PARAM("int('{0}', {1})", key, *value);
            return NVSDK_NGX_Result_Success;
        }

#ifdef ENABLE_ENCAPSULATED_PARAMS
        if (OriginalParam != nullptr)
        {
            LOG_PARAM("calling original int('{0}')", key);
            result = OriginalParam->Get(key, value);
            LOG_PARAM("calling original int('{0}') result: {1:X}", key, (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
            {
                LOG_PARAM("from original int('{0}', {1})", key, *value);
                return result;
            }
        }
#endif // ENABLE_ENCAPSULATED_PARAMS

        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char* key, void** value) const override
    {
        auto result = getT(key, value);
        if (result == NVSDK_NGX_Result_Success)
        {
            LOG_PARAM("void('{0}')", key);
            return NVSDK_NGX_Result_Success;
        }

#ifdef ENABLE_ENCAPSULATED_PARAMS
        if (OriginalParam != nullptr)
        {
            LOG_PARAM("calling original void('{0}')", key);
            result = OriginalParam->Get(key, value);
            LOG_PARAM("calling original void('{0}') result: {1:X}", key, (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
            {
                LOG_PARAM("from original void('{0}')", key);
                return result;
            }
        }
#endif // ENABLE_ENCAPSULATED_PARAMS

        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char* key, ID3D11Resource** value) const override
    {
        auto result = getT(key, value);
        if (result == NVSDK_NGX_Result_Success)
        {
            LOG_PARAM("d3d11('{0}')", key);
            return NVSDK_NGX_Result_Success;
        }

#ifdef ENABLE_ENCAPSULATED_PARAMS
        if (OriginalParam != nullptr)
        {
            LOG_PARAM("calling original d3d11('{0}')", key);
            result = OriginalParam->Get(key, value);
            LOG_PARAM("calling original d3d11('{0}') result: {1:X}", key, (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
            {
                LOG_PARAM("from original d3d11('{0}')", key);
                return result;
            }
        }
#endif // ENABLE_ENCAPSULATED_PARAMS

        return NVSDK_NGX_Result_Fail;
    }

    NVSDK_NGX_Result Get(const char* key, ID3D12Resource** value) const override
    {
        auto result = getT(key, value);
        if (result == NVSDK_NGX_Result_Success)
        {
            LOG_PARAM("d3d12('{0}')", key);
            return NVSDK_NGX_Result_Success;
        }

#ifdef ENABLE_ENCAPSULATED_PARAMS
        if (OriginalParam != nullptr)
        {
            LOG_PARAM("calling original d3d12('{0}')", key);
            result = OriginalParam->Get(key, value);
            LOG_PARAM("calling original d3d12('{0}') result: {1:X}", key, (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
            {
                LOG_PARAM("from original d3d12('{0}')", key);
                return result;
            }
        }
#endif // ENABLE_ENCAPSULATED_PARAMS

        return NVSDK_NGX_Result_Fail;
    }

    void Reset() override
    {
        if (!m_values.empty())
            m_values.clear();

        LOG_DEBUG("Start");

        InitNGXParameters(this);

        LOG_DEBUG("End");
    }

    std::vector<std::string> enumerate() const
    {
        std::vector<std::string> keys;
        for (auto& value : m_values)
        {
            keys.push_back(value.first);
        }
        return keys;
    }

  private:
    ankerl::unordered_dense::map<std::string, Parameter> m_values;
    mutable std::mutex m_mutex;

    template <typename T> void setT(const char* key, T& value)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_values[key] = value;
    }

    template <typename T> NVSDK_NGX_Result getT(const char* key, T* value) const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        auto k = m_values.find(key);

        if (k == m_values.end())
        {
            LOG_TRACE("('{0}', FAIL)", key);
            return NVSDK_NGX_Result_Fail;
        };

        const Parameter& p = (*k).second;
        *value = p;

        return NVSDK_NGX_Result_Success;
    }
};

inline static NVNGX_Parameters* GetNGXParameters(std::string InName)
{
    auto params = new NVNGX_Parameters();
    params->Name = InName;
    InitNGXParameters(params);
    params->Set("OptiScaler", 1);

    return params;
}
