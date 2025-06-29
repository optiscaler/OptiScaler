#include <pch.h>
#include <Config.h>
#include "IFeature.h"

void IFeature::SetHandle(unsigned int InHandleId)
{
    _handle = new NVSDK_NGX_Handle { InHandleId };
    LOG_INFO("Handle: {0}", _handle->Id);
}

bool IFeature::SetInitParameters(NVSDK_NGX_Parameter* InParameters)
{
    unsigned int width = 0;
    unsigned int outWidth = 0;
    unsigned int height = 0;
    unsigned int outHeight = 0;
    int pqValue = 0;

    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &_featureFlags) == NVSDK_NGX_Result_Success)
    {
        if (Config::Instance()->HDR.has_value())
        {
            LOG_INFO("HDR flag overrided by user: {}", Config::Instance()->HDR.value());
            _initFlags.IsHdr = Config::Instance()->HDR.value();
        }
        else
        {
            _initFlags.IsHdr = _featureFlags & NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
        }

        if (Config::Instance()->OverrideSharpness.has_value())
        {
            LOG_INFO("SharpenEnabled flag overrided by user: {}", Config::Instance()->OverrideSharpness.value());
            _initFlags.SharpenEnabled = Config::Instance()->OverrideSharpness.value();
        }
        else
        {
            _initFlags.SharpenEnabled = _featureFlags & NVSDK_NGX_DLSS_Feature_Flags_DoSharpening;
        }

        if (Config::Instance()->DepthInverted.has_value())
        {
            LOG_INFO("DepthInverted flag overrided by user: {}", Config::Instance()->DepthInverted.value());
            _initFlags.DepthInverted = Config::Instance()->DepthInverted.value();
        }
        else
        {
            _initFlags.DepthInverted = _featureFlags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
        }

        if (Config::Instance()->JitterCancellation.has_value())
        {
            LOG_INFO("JitteredMV flag overrided by user: {}", Config::Instance()->JitterCancellation.value());
            _initFlags.JitteredMV = Config::Instance()->JitterCancellation.value();
        }
        else
        {
            _initFlags.JitteredMV = _featureFlags & NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
        }

        if (Config::Instance()->DisplayResolution.has_value())
        {
            LOG_INFO("LowResMV flag overrided by user: {}", !Config::Instance()->DisplayResolution.value());
            _initFlags.LowResMV = !Config::Instance()->DisplayResolution.value();
        }
        else
        {

            _initFlags.LowResMV = _featureFlags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        }

        // First check state to prevent upscaler re-init loops
        if (State::Instance().AutoExposure.has_value())
        {
            LOG_INFO("AutoExposure flag overrided by OptiScaler: {}", State::Instance().AutoExposure.value());
            _initFlags.AutoExposure = State::Instance().AutoExposure.value();
        }
        else if (Config::Instance()->AutoExposure.has_value())
        {
            LOG_INFO("AutoExposure flag overrided by user: {}", Config::Instance()->AutoExposure.value());
            _initFlags.AutoExposure = Config::Instance()->AutoExposure.value();
        }
        else if ((State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
                  State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine) &&
                 Name()[0] == 'X')
        {
            LOG_INFO("AutoExposure flag overrided by OptiScaler (UE+XeSS): true");
            _initFlags.AutoExposure = true;
        }
        else
        {
            _initFlags.AutoExposure = _featureFlags & NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
        }

        LOG_INFO("Init Flag AutoExposure: {}", _initFlags.AutoExposure);
        LOG_INFO("Init Flag DepthInverted: {}", _initFlags.DepthInverted);
        LOG_INFO("Init Flag IsHdr: {}", _initFlags.IsHdr);
        LOG_INFO("Init Flag JitteredMV: {}", _initFlags.JitteredMV);
        LOG_INFO("Init Flag LowResMV: {}", _initFlags.LowResMV);
        LOG_INFO("Init Flag SharpenEnabled: {}", _initFlags.SharpenEnabled);
    }

    if (InParameters->Get(NVSDK_NGX_Parameter_OutWidth, &outWidth) == NVSDK_NGX_Result_Success &&
        InParameters->Get(NVSDK_NGX_Parameter_OutHeight, &outHeight) == NVSDK_NGX_Result_Success)
    {
        InParameters->Get(NVSDK_NGX_Parameter_Width, &width);
        InParameters->Get(NVSDK_NGX_Parameter_Height, &height);
        InParameters->Get(NVSDK_NGX_Parameter_PerfQualityValue, &pqValue);

        GetDynamicOutputResolution(InParameters, &outWidth, &outHeight);

        // Thanks to Crytek added these checks
        if (width > 16384 || width < 20)
            width = 0;

        if (height > 16384 || height < 20)
            height = 0;

        if (outWidth > 16384 || outWidth < 20)
            outWidth = 0;

        if (outHeight > 16384 || outHeight < 20)
            outHeight = 0;

        if (pqValue > 5 || pqValue < 0)
            pqValue = 1;

        // When using extended limits render res might be bigger than display res
        // it might create rendering issues but extending limits is an advanced option after all
        if (!Config::Instance()->ExtendedLimits.value_or_default())
        {
            _displayWidth = width > outWidth ? width : outWidth;
            _displayHeight = height > outHeight ? height : outHeight;
            _targetWidth = _displayWidth;
            _targetHeight = _displayHeight;
            _renderWidth = width < outWidth ? width : outWidth;
            _renderHeight = height < outHeight ? height : outHeight;
        }
        else
        {
            _displayWidth = outWidth;
            _displayHeight = outHeight;
            _targetWidth = _displayWidth;
            _targetHeight = _displayHeight;
            _renderWidth = width;
            _renderHeight = height;
        }

        _perfQualityValue = (NVSDK_NGX_PerfQuality_Value) pqValue;

        LOG_INFO("Render Resolution: {0}x{1}, Display Resolution {2}x{3}, Quality: {4}", _renderWidth, _renderHeight,
                 _displayWidth, _displayHeight, pqValue);

        return true;
    }

    LOG_ERROR("Can't set parameters!");
    return false;
}

void IFeature::GetRenderResolution(NVSDK_NGX_Parameter* InParameters, unsigned int* OutWidth, unsigned int* OutHeight)
{
    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, OutWidth) !=
            NVSDK_NGX_Result_Success ||
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, OutHeight) !=
            NVSDK_NGX_Result_Success)
    {
        LOG_WARN("No subrect dimension info!");

        unsigned int width;
        unsigned int height;
        unsigned int outWidth;
        unsigned int outHeight;

        do
        {
            if (InParameters->Get(NVSDK_NGX_Parameter_Width, &width) == NVSDK_NGX_Result_Success &&
                InParameters->Get(NVSDK_NGX_Parameter_Height, &height) == NVSDK_NGX_Result_Success)
            {
                if (InParameters->Get(NVSDK_NGX_Parameter_OutWidth, &outWidth) == NVSDK_NGX_Result_Success &&
                    InParameters->Get(NVSDK_NGX_Parameter_OutHeight, &outHeight) == NVSDK_NGX_Result_Success)
                {
                    if (width < outWidth)
                    {
                        *OutWidth = width;
                        *OutHeight = height;
                        break;
                    }

                    *OutWidth = outWidth;
                    *OutHeight = outHeight;
                }
                else
                {
                    if (width < RenderWidth())
                    {
                        *OutWidth = width;
                        *OutHeight = height;
                        break;
                    }

                    *OutWidth = RenderWidth();
                    *OutHeight = RenderHeight();
                    return;
                }
            }

            *OutWidth = RenderWidth();
            *OutHeight = RenderHeight();

        } while (false);
    }

    _renderWidth = *OutWidth;
    _renderHeight = *OutHeight;

    // Should not be needed but who knows
    // if (_renderHeight == _displayHeight && _renderWidth == _displayWidth && _perfQualityValue !=
    // NVSDK_NGX_PerfQuality_Value_DLAA)
    //{
    //	InParameters->Set(NVSDK_NGX_Parameter_PerfQualityValue, 5);
    //	InParameters->Set(NVSDK_NGX_Parameter_Scale, 1.0f);
    //	InParameters->Set(NVSDK_NGX_Parameter_SuperSampling_ScaleFactor, 1.0f);
    // }

    JitterInfo ji {};
    if (_jitterInfo.size() < 350 &&
        InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &ji.x) == NVSDK_NGX_Result_Success &&
        InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &ji.y) == NVSDK_NGX_Result_Success)
    {
        _jitterInfo.insert(std::make_pair(ji.x, ji.y));
    }
}

float IFeature::GetSharpness(const NVSDK_NGX_Parameter* InParameters)
{
    if (Config::Instance()->OverrideSharpness.value_or_default())
        return Config::Instance()->Sharpness.value_or_default();

    float sharpness = 0.0f;

    if (InParameters->Get(NVSDK_NGX_Parameter_Sharpness, &sharpness) == NVSDK_NGX_Result_Success)
    {
        if (sharpness < 0.0f)
            sharpness = 0.0f;
        else if (sharpness > 1.0f)
            sharpness = 1.0f;
    }

    return sharpness;
}

void IFeature::TickFrozenCheck()
{
    static long updatesWithoutFramecountChange = 0;

    if (_isInited)
    {
        static auto lastFrameCount = _frameCount;

        if (_frameCount == lastFrameCount)
            updatesWithoutFramecountChange++;
        else
            updatesWithoutFramecountChange = 0;

        lastFrameCount = _frameCount;

        _featureFrozen = updatesWithoutFramecountChange > 10;
    }
}

bool IFeature::UpdateOutputResolution(const NVSDK_NGX_Parameter* InParameters)
{
    // Check for FSR's dynamic resolution output
    auto fsrDynamicOutputWidth = 0;
    auto fsrDynamicOutputHeight = 0;

    InParameters->Get("FSR.upscaleSize.width", &fsrDynamicOutputWidth);
    InParameters->Get("FSR.upscaleSize.height", &fsrDynamicOutputHeight);

    if (Config::Instance()->OutputScalingEnabled.value_or_default())
    {
        if (_targetWidth == fsrDynamicOutputWidth || _targetHeight == fsrDynamicOutputHeight)
            return false;

        if (fsrDynamicOutputWidth > 0 && fsrDynamicOutputHeight > 0 &&
            ((unsigned int) (fsrDynamicOutputWidth * Config::Instance()->OutputScalingMultiplier.value_or_default()) !=
                 _targetWidth ||
             fsrDynamicOutputWidth != _displayWidth ||
             (unsigned int) (fsrDynamicOutputHeight * Config::Instance()->OutputScalingMultiplier.value_or_default()) !=
                 _targetHeight ||
             fsrDynamicOutputHeight != _displayHeight))
        {
            _targetWidth = fsrDynamicOutputWidth * Config::Instance()->OutputScalingMultiplier.value_or_default();
            _displayWidth = fsrDynamicOutputWidth;
            _targetHeight = fsrDynamicOutputHeight * Config::Instance()->OutputScalingMultiplier.value_or_default();
            _displayHeight = fsrDynamicOutputHeight;

            return true;
        }
    }
    else
    {
        if (fsrDynamicOutputWidth > 0 && fsrDynamicOutputHeight > 0 &&
            (fsrDynamicOutputWidth != _targetWidth || fsrDynamicOutputWidth != _displayWidth ||
             fsrDynamicOutputHeight != _targetHeight || fsrDynamicOutputHeight != _displayHeight))
        {
            _targetWidth = fsrDynamicOutputWidth;
            _displayWidth = fsrDynamicOutputWidth;
            _targetHeight = fsrDynamicOutputHeight;
            _displayHeight = fsrDynamicOutputHeight;

            return true;
        }
    }

    return false;
}

void IFeature::GetDynamicOutputResolution(NVSDK_NGX_Parameter* InParameters, unsigned int* width, unsigned int* height)
{
    // FSR 3.1 uses upscaleSize for this, max size should stay the same
    int supportsUpscaleSize = 0;
    InParameters->Get("OptiScaler.SupportsUpscaleSize", &supportsUpscaleSize);
    if (supportsUpscaleSize)
    {
        InParameters->Set("OptiScaler.SupportsUpscaleSize", 0);
        return;
    }

    // Check for FSR's dynamic resolution output
    auto fsrDynamicOutputWidth = 0;
    auto fsrDynamicOutputHeight = 0;

    InParameters->Get("FSR.upscaleSize.width", &fsrDynamicOutputWidth);
    InParameters->Get("FSR.upscaleSize.height", &fsrDynamicOutputHeight);

    if (fsrDynamicOutputWidth > 0 && fsrDynamicOutputHeight > 0)
    {
        *width = fsrDynamicOutputWidth;
        *height = fsrDynamicOutputHeight;
    }
}
