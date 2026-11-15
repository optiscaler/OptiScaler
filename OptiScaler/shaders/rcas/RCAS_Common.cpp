#include "pch.h"

#include "RCAS_Common.h"

void RCAS_Common::FillMotionConstants(InternalConstants& OutConstants, const RcasConstants& InConstants)
{
    if (Config::Instance()->ContrastEnabled.value_or_default())
        OutConstants.Contrast = Config::Instance()->Contrast.value_or_default();
    else
        OutConstants.Contrast = 0.0f;

    OutConstants.Sharpness = InConstants.Sharpness;
    OutConstants.MvScaleX = InConstants.MvScaleX;
    OutConstants.MvScaleY = InConstants.MvScaleY;

    OutConstants.DisplaySizeMV = OutConstants.MotionWidth == OutConstants.OutputWidth ? 1 : 0;

    OutConstants.DynamicSharpenEnabled = Config::Instance()->MotionSharpnessEnabled.value_or_default() ? 1 : 0;
    OutConstants.MotionSharpness = Config::Instance()->MotionSharpness.value_or_default();
    OutConstants.Debug = Config::Instance()->MotionSharpnessDebug.value_or_default() ? 1 : 0;
    OutConstants.Threshold = Config::Instance()->MotionThreshold.value_or_default();
    OutConstants.ScaleLimit = Config::Instance()->MotionScaleLimit.value_or_default();

    if (OutConstants.DisplaySizeMV)
        OutConstants.MotionTextureScale = 1.0f;
    else
        OutConstants.MotionTextureScale = (float) OutConstants.MotionWidth / (float) OutConstants.OutputWidth;
}

void RCAS_Common::FillMotionConstants(InternalConstantsDA& OutConstants, const RcasConstants& InConstants)
{
    OutConstants.Sharpness = InConstants.Sharpness * 2.0f;
    OutConstants.MvScaleX = InConstants.MvScaleX;
    OutConstants.MvScaleY = InConstants.MvScaleY;

    OutConstants.DynamicSharpenEnabled = Config::Instance()->MotionSharpnessEnabled.value_or_default() ? 1 : 0;
    OutConstants.Debug = Config::Instance()->MotionSharpnessDebug.value_or_default() ? 3 : 0;
    OutConstants.MotionSharpness = Config::Instance()->MotionSharpness.value_or_default();
    OutConstants.MotionThreshold = Config::Instance()->MotionThreshold.value_or_default();
    OutConstants.MotionScaleLimit = Config::Instance()->MotionScaleLimit.value_or_default();

    OutConstants.DepthIsLinear = InConstants.DepthIsLinear ? 1 : 0;
    OutConstants.DepthIsReversed = InConstants.DepthIsReversed ? 1 : 0;
    OutConstants.DepthScale =
        Config::Instance()->DADepthScale.value_or(OutConstants.DepthIsLinear == 0 ? 35.0f : 250.0f);
    OutConstants.DepthBias =
        Config::Instance()->DADepthBias.value_or(OutConstants.DepthIsLinear == 0 ? 0.001f : 0.0015f);

    OutConstants.DepthLinearA = InConstants.CameraNear * InConstants.CameraFar;
    OutConstants.DepthLinearB = InConstants.CameraFar;
    OutConstants.DepthLinearC = InConstants.CameraFar - InConstants.CameraNear;

    OutConstants.DepthTextureScale = (float) OutConstants.DepthWidth / (float) OutConstants.OutputWidth;
    OutConstants.ClampOutput = Config::Instance()->DAClampOutput.value_or(InConstants.IsHdr) ? 0 : 1;

    OutConstants.DisplaySizeMV = OutConstants.MotionWidth == OutConstants.OutputWidth ? 1 : 0;

    if (OutConstants.DisplaySizeMV)
        OutConstants.MotionTextureScale = 1.0f;
    else
        OutConstants.MotionTextureScale = (float) OutConstants.MotionWidth / (float) OutConstants.OutputWidth;
}

void RCAS_Common::FillMotionConstants(InternalConstantsBaba& OutConstants, const RcasConstants& InConstants)
{
    // Same mapping as the depth aware shaders, 0.5 strength is the 1.0 Intensity of the ReShade shader
    OutConstants.Sharpness = InConstants.Sharpness * 2.0f;

    OutConstants.EdgeResponse = Config::Instance()->BabaNeuralEdgeResponse.value_or_default();
    OutConstants.Model = Config::Instance()->BabaNeuralModel.value_or_default();
    OutConstants.ViewMode = Config::Instance()->BabaNeuralDebugView.value_or_default();

    // LDR pipelines are limited to [0, 1] like the render target of the ReShade version
    OutConstants.ClampOutput = InConstants.IsHdr ? 0 : 1;

    OutConstants.MvScaleX = InConstants.MvScaleX;
    OutConstants.MvScaleY = InConstants.MvScaleY;

    OutConstants.DynamicSharpenEnabled = Config::Instance()->MotionSharpnessEnabled.value_or_default() ? 1 : 0;
    OutConstants.Debug = Config::Instance()->MotionSharpnessDebug.value_or_default() ? 1 : 0;
    OutConstants.MotionSharpness = Config::Instance()->MotionSharpness.value_or_default();
    OutConstants.MotionThreshold = Config::Instance()->MotionThreshold.value_or_default();
    OutConstants.MotionScaleLimit = Config::Instance()->MotionScaleLimit.value_or_default();

    OutConstants.DisplaySizeMV = OutConstants.MotionWidth == OutConstants.OutputWidth ? 1 : 0;

    if (OutConstants.DisplaySizeMV)
        OutConstants.MotionTextureScale = 1.0f;
    else
        OutConstants.MotionTextureScale = (float) OutConstants.MotionWidth / (float) OutConstants.OutputWidth;
}
