#include <pch.h>
#include <Config.h>
#include "IFeature.h"

#include <magic_enum.hpp>

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
        if (std::optional<bool> hdr = Config::Instance()->HDR; hdr.has_value())
        {
            LOG_INFO("HDR flag overrided by user: {}", hdr.value());
            _initFlags.IsHdr = hdr.value();
        }
        else
        {
            _initFlags.IsHdr = _featureFlags & NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
        }

        if (std::optional<bool> overrideSharpness = Config::Instance()->OverrideSharpness;
            overrideSharpness.has_value())
        {
            LOG_INFO("SharpenEnabled flag overrided by user: {}", overrideSharpness.value());
            _initFlags.SharpenEnabled = overrideSharpness.value();
        }
        else
        {
            _initFlags.SharpenEnabled = _featureFlags & NVSDK_NGX_DLSS_Feature_Flags_DoSharpening;
        }

        if (std::optional<bool> depthInverted = Config::Instance()->DepthInverted; depthInverted.has_value())
        {
            LOG_INFO("DepthInverted flag overrided by user: {}", depthInverted.value());
            _initFlags.DepthInverted = depthInverted.value();
        }
        else
        {
            _initFlags.DepthInverted = _featureFlags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
        }

        if (std::optional<bool> jitterCancellation = Config::Instance()->JitterCancellation;
            jitterCancellation.has_value())
        {
            LOG_INFO("JitteredMV flag overrided by user: {}", jitterCancellation.value());
            _initFlags.JitteredMV = jitterCancellation.value();
        }
        else
        {
            _initFlags.JitteredMV = _featureFlags & NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
        }

        if (std::optional<bool> displayResolution = Config::Instance()->DisplayResolution;
            displayResolution.has_value())
        {
            LOG_INFO("LowResMV flag overrided by user: {}", !displayResolution.value());
            _initFlags.LowResMV = !displayResolution.value();
        }
        else
        {
            _initFlags.LowResMV = _featureFlags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        }

        // First check state to prevent upscaler re-init loops
        if (std::optional<bool> stateAutoExposure = State::Instance().autoExposure; stateAutoExposure.has_value())
        {
            LOG_INFO("AutoExposure flag overrided by OptiScaler: {}", stateAutoExposure.value());
            _initFlags.AutoExposure = stateAutoExposure.value();
        }
        else if (std::optional<bool> autoExposure = Config::Instance()->AutoExposure; autoExposure.has_value())
        {
            LOG_INFO("AutoExposure flag overrided by user: {}", autoExposure.value());
            _initFlags.AutoExposure = autoExposure.value();
        }
        else
        {
            _initFlags.AutoExposure = _featureFlags & NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
        }

        if (std::optional<bool> dynamicResolution = Config::Instance()->DynamicResolution;
            dynamicResolution.has_value())
        {
            LOG_INFO("DynamicResolution flag overrided by user: {}", dynamicResolution.value());
            _initFlags.DynamicResolution = dynamicResolution.value();
        }
        else
        {
            _initFlags.DynamicResolution = State::Instance().dynamicResolution;
        }

        LOG_INFO("Init Flag AutoExposure: {}", _initFlags.AutoExposure);
        LOG_INFO("Init Flag DepthInverted: {}", _initFlags.DepthInverted);
        LOG_INFO("Init Flag IsHdr: {}", _initFlags.IsHdr);
        LOG_INFO("Init Flag JitteredMV: {}", _initFlags.JitteredMV);
        LOG_INFO("Init Flag LowResMV: {}", _initFlags.LowResMV);
        LOG_INFO("Init Flag SharpenEnabled: {}", _initFlags.SharpenEnabled);
        LOG_INFO("Init Flag DynamicResolution: {}", _initFlags.DynamicResolution);

        if (State::Instance().activeFgInput == FGInput::Upscaler)
        {
            Config::Instance()->FGXeFGDepthInverted = _initFlags.DepthInverted;
            Config::Instance()->FGXeFGJitteredMV = _initFlags.JitteredMV;
            Config::Instance()->FGXeFGHighResMV = !_initFlags.LowResMV;
            LOG_DEBUG("XeFG DepthInverted: {}", Config::Instance()->FGXeFGDepthInverted.value_or_default());
            LOG_DEBUG("XeFG JitteredMV: {}", Config::Instance()->FGXeFGJitteredMV.value_or_default());
            LOG_DEBUG("XeFG HighResMV: {}", Config::Instance()->FGXeFGHighResMV.value_or_default());
            Config::Instance()->SaveXeFG();
        }
    }

    // Like DLSS, the output base is only used when enabled at creation
    int outputSubrects = 0;
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, &outputSubrects);
    _outputSubrects = outputSubrects != 0;

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

        // If output scaling is enabled and render res is equal to display res, enable low res MVs
        // because we will use display res MV as render res and upscale with it
        if (Config::Instance()->OutputScalingEnabled.value_or_default() && !_initFlags.LowResMV &&
            _renderWidth == _displayWidth)
        {
            LOG_INFO("Output Scaling is active with render size equal to display size, enabling low res MVs");
            _initFlags.LowResMV = true;
        }

        return true;
    }

    LOG_ERROR("Can't set parameters!");
    return false;
}

void IFeature::GetRenderResolution(const NVSDK_NGX_Parameter* InParameters, unsigned int* OutWidth,
                                   unsigned int* OutHeight)
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

    ReadSubrects(InParameters);

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

void IFeature::ReadSubrects(const NVSDK_NGX_Parameter* InParameters)
{
    for (size_t i = 0; i < SubrectBaseKeys.size(); i++)
    {
        auto& rect = _subrects[i];
        rect = { 0, 0, _renderWidth, _renderHeight };
        InParameters->Get(SubrectBaseKeys[i].first, &rect.X);
        InParameters->Get(SubrectBaseKeys[i].second, &rect.Y);
    }

    // Display resolution motion vectors and the output keep the creation size, even with dynamic resolution
    if (!LowResMV())
    {
        _subrects[(size_t) SubrectSlot::MV].Width = DisplayWidth();
        _subrects[(size_t) SubrectSlot::MV].Height = DisplayHeight();
    }

    auto& output = _subrects[(size_t) SubrectSlot::Output];
    output.Width = DisplayWidth();
    output.Height = DisplayHeight();

    if (!_outputSubrects)
        output.X = output.Y = 0;
}

bool IFeature::PlacesOutput(const Subrect& InRect, bool InHasPasses) const
{
    const auto upscaler = GetUpscalerType();
    const bool nativeSubrects = upscaler == Upscaler::XeSS || upscaler == Upscaler::DLSS || upscaler == Upscaler::DLSSD;
    return InRect.HasOffset() && (!nativeSubrects || InHasPasses);
}

void IFeature::SetSubrectBase(NVSDK_NGX_Parameter* InParameters, SubrectSlot InSlot, uint32_t InX, uint32_t InY)
{
    InParameters->Set(SubrectBaseKeys[(size_t) InSlot].first, InX);
    InParameters->Set(SubrectBaseKeys[(size_t) InSlot].second, InY);
}

bool IFeature::SubrectFits(SubrectSlot InSlot, const Subrect& InRect, uint64_t InWidth, uint32_t InHeight)
{
    const uint32_t slotBit = 1u << (uint32_t) InSlot;

    // An unknown render size, copy buffers would take the source size
    if (InRect.Width == 0 || InRect.Height == 0)
        return false;

    if ((uint64_t) InRect.X + InRect.Width > InWidth || (uint64_t) InRect.Y + InRect.Height > InHeight)
    {
        if (!(_subrectWarned & slotBit))
        {
            LOG_WARN("{} subrect {}x{} at ({}, {}) is outside the {}x{} texture, using it as is",
                     magic_enum::enum_name(InSlot), InRect.Width, InRect.Height, InRect.X, InRect.Y, InWidth, InHeight);
            _subrectWarned |= slotBit;
        }

        return false;
    }

    if (!(_subrectLogged & slotBit))
    {
        LOG_INFO("{} subrect {}x{} at ({}, {}) of the {}x{} texture, copying it for {}", magic_enum::enum_name(InSlot),
                 InRect.Width, InRect.Height, InRect.X, InRect.Y, InWidth, InHeight, Name());
        _subrectLogged |= slotBit;
    }

    return true;
}

void IFeature::WarnIntegerMask(uint32_t InFormat)
{
    if (_integerMaskWarned)
        return;

    LOG_WARN("Ignoring the reactive mask, its integer format {} can't be read as float", InFormat);
    _integerMaskWarned = true;
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

void IFeature::TickFrozenCheck(uint32_t presentPerEval)
{
    static uint32_t updatesWithoutFramecountChange = 0;

    if (_isInited)
    {
        static auto lastFrameCount = _frameCount;

        if (_frameCount == lastFrameCount)
            updatesWithoutFramecountChange++;
        else
            updatesWithoutFramecountChange = 0;

        lastFrameCount = _frameCount;

        _featureFrozen = updatesWithoutFramecountChange > (10 * presentPerEval);
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
        if (_targetWidth == fsrDynamicOutputWidth && _targetHeight == fsrDynamicOutputHeight)
            return false;

        if (fsrDynamicOutputWidth > 0 && fsrDynamicOutputHeight > 0 &&
            ((unsigned int) (fsrDynamicOutputWidth * Config::Instance()->OutputScalingMultiplier.value_or_default()) !=
                 _targetWidth ||
             fsrDynamicOutputWidth != _displayWidth ||
             (unsigned int) (fsrDynamicOutputHeight * Config::Instance()->OutputScalingMultiplier.value_or_default()) !=
                 _targetHeight ||
             fsrDynamicOutputHeight != _displayHeight))
        {
            _targetWidth = static_cast<unsigned int>(fsrDynamicOutputWidth *
                                                     Config::Instance()->OutputScalingMultiplier.value_or_default());
            _displayWidth = fsrDynamicOutputWidth;
            _targetHeight = static_cast<unsigned int>(fsrDynamicOutputHeight *
                                                      Config::Instance()->OutputScalingMultiplier.value_or_default());
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
