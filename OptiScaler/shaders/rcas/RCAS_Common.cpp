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

    OutConstants.DisplaySizeMV =
        (OutConstants.MotionWidth == OutConstants.OutputWidth && OutConstants.MotionHeight == OutConstants.OutputHeight)
            ? 1
            : 0;

    OutConstants.DynamicSharpenEnabled = Config::Instance()->MotionSharpnessEnabled.value_or_default() ? 1 : 0;
    OutConstants.MotionSharpness = Config::Instance()->MotionSharpness.value_or_default();
    OutConstants.Debug = Config::Instance()->MotionSharpnessDebug.value_or_default() ? 1 : 0;
    OutConstants.Threshold = Config::Instance()->MotionThreshold.value_or_default();
    OutConstants.ScaleLimit = Config::Instance()->MotionScaleLimit.value_or_default();
}

int RCAS_Common::OptiModelSetting()
{
    return std::clamp(Config::Instance()->OptiSharpenModel.value_or_default(), 0, 3);
}

OptiVariant RCAS_Common::GetOptiVariant(int InModel, bool InDx11)
{
    // Dx11 has the fp32 ones only, Natural / Crisp is kept
    if (InDx11)
        InModel &= 1;

    return (OptiVariant) std::clamp(InModel, 0, (int) OptiVariant::Count - 1);
}

const char* RCAS_Common::OptiVariantName(OptiVariant InVariant)
{
    static const char* names[] = { "Natural fp32", "Crisp fp32", "Natural fp16 packed", "Crisp fp16 packed" };
    static_assert(std::size(names) == (size_t) OptiVariant::Count);
    return names[(size_t) InVariant];
}

void RCAS_Common::SetTimedShader(SharpenShader InShader, OptiVariant InVariant)
{
    // Same names as the Method combo of the menu
    const char* name = "RCAS";

    switch (InShader)
    {
    case SharpenShader::DepthAware:
        name = "Depth Aware (RCAS)";
        break;
    case SharpenShader::DepthAwareDAS:
        name = "Depth Aware (DAS)";
        break;
    case SharpenShader::BabaNeural:
        name = "Neural (BaBa)";
        break;
    case SharpenShader::OptiSharpen:
    {
        // The variant is part of the name, timings are compared between them
        static const char* names[] = { "OptiSharpen (Natural fp32)", "OptiSharpen (Crisp fp32)",
                                       "OptiSharpen (Natural fp16 packed)", "OptiSharpen (Crisp fp16 packed)" };
        static_assert(std::size(names) == (size_t) OptiVariant::Count);
        name = names[(size_t) InVariant];
        break;
    }
    default:
        break;
    }

    _timedShaderName = name;
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
    // Off by default, games flagged SDR can still feed pre-tonemap HDR color (Witcher 3)
    OutConstants.ClampOutput = Config::Instance()->DAClampOutput.value_or(false) ? 1 : 0;

    OutConstants.DisplaySizeMV =
        (OutConstants.MotionWidth == OutConstants.OutputWidth && OutConstants.MotionHeight == OutConstants.OutputHeight)
            ? 1
            : 0;
}

void RCAS_Common::FillMotionConstants(InternalConstantsBaba& OutConstants, const RcasConstants& InConstants,
                                      SharpenShader InShader)
{
    // Same mapping as the depth aware shaders, 0.5 strength is the 1.0 Intensity of the ReShade shader
    // and the trained strength of OptiSharpen
    OutConstants.Sharpness = InConstants.Sharpness * 2.0f;

    if (InShader == SharpenShader::OptiSharpen)
    {
        // Natural / Crisp, the precompiled pipelines have it compiled in and ignore it
        OutConstants.Model = _optiModel & 1;
        OutConstants.ViewMode = Config::Instance()->OptiSharpenDebugView.value_or_default();
    }
    else
    {
        OutConstants.EdgeResponse = Config::Instance()->BabaNeuralEdgeResponse.value_or_default();
        OutConstants.Model = Config::Instance()->BabaNeuralModel.value_or_default();
        OutConstants.ViewMode = Config::Instance()->BabaNeuralDebugView.value_or_default();
    }

    OutConstants.ClampOutput = Config::Instance()->DAClampOutput.value_or(false) ? 1 : 0;
    OutConstants.IsHdr = InConstants.IsHdr ? 1 : 0;

    OutConstants.MvScaleX = InConstants.MvScaleX;
    OutConstants.MvScaleY = InConstants.MvScaleY;

    OutConstants.DynamicSharpenEnabled = (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                                          OutConstants.MotionWidth > 0 && OutConstants.MotionHeight > 0)
                                             ? 1
                                             : 0;
    OutConstants.Debug = Config::Instance()->MotionSharpnessDebug.value_or_default() ? 1 : 0;
    OutConstants.MotionSharpness = Config::Instance()->MotionSharpness.value_or_default();
    OutConstants.MotionThreshold = Config::Instance()->MotionThreshold.value_or_default();
    OutConstants.MotionScaleLimit = Config::Instance()->MotionScaleLimit.value_or_default();

    OutConstants.DisplaySizeMV =
        (OutConstants.MotionWidth == OutConstants.OutputWidth && OutConstants.MotionHeight == OutConstants.OutputHeight)
            ? 1
            : 0;
}
