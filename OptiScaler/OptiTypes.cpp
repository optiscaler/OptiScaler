#include "pch.h"

#include "OptiTypes.h"
#include <misc/IdentifyGpu.h>
#include <unordered_map>

std::string ApiUpscalerInputName(ApiUpscalerInput upscaler)
{
    switch (upscaler)
    {
    case ApiUpscalerInput::DLSS_DX11:
    case ApiUpscalerInput::DLSS_DX12:
    case ApiUpscalerInput::DLSS_VK:
        return "DLSS";
    case ApiUpscalerInput::XeSS_DX11:
    case ApiUpscalerInput::XeSS_DX12:
    case ApiUpscalerInput::XeSS_VK:
        return "XeSS";
    case ApiUpscalerInput::FFX_DX12:
    case ApiUpscalerInput::FFX_VK:
        return "FFX";
    case ApiUpscalerInput::FSR20_DX12:
        return "FSR 2.0";
    case ApiUpscalerInput::FSR2X_DX11:
    case ApiUpscalerInput::FSR2X_DX12:
    case ApiUpscalerInput::FSR2X_VK:
        return "FSR 2.X";
    case ApiUpscalerInput::FSR2_TinyTina:
        return "FSR 2.TT";
    case ApiUpscalerInput::FSR3_DX12:
        return "FSR 3.0";
    default:
        return "????";
    }
}

std::string UpscalerDisplayName(Upscaler upscaler, API api)
{
    bool fsr4Capable = IdentifyGpu::getPrimaryGpu().fsr4Support != FSR4Support::None;

    switch (upscaler)
    {
    case Upscaler::FSR21:
        return "FSR 2.1.2";

    case Upscaler::FSR22:
        return "FSR 2.2.1";

    case Upscaler::FSR31:
        return "FSR 3.1";

    case Upscaler::FFX:
        if (fsr4Capable && api == API::DX12)
            return "FSR 3.X/4";
        else
            return "FSR 3.X";

    case Upscaler::FSR21_on12:
        return "FSR 2.1.2 w/Dx12";

    case Upscaler::FSR22_on12:
        return "FSR 2.2.1 w/Dx12";

    case Upscaler::FFX_on12:
        if (fsr4Capable)
            return "FSR 3.X/4 w/Dx12";
        else
            return "FSR 3.X w/Dx12";

    case Upscaler::XeSS:
        return "XeSS";

    case Upscaler::XeSS_on12:
        return "XeSS w/Dx12";

    case Upscaler::DLSS:
        return "DLSS";

    case Upscaler::DLSSD:
        return "DLSSD";
    }

    return "????";
}

bool IsFsr(Upscaler upscaler)
{
    switch (upscaler)
    {
    case Upscaler::FSR21:
    case Upscaler::FSR22:
    case Upscaler::FSR31:
    case Upscaler::FFX:
    case Upscaler::FSR21_on12:
    case Upscaler::FSR22_on12:
    case Upscaler::FFX_on12:
        return true;
    default:
        return false;
    }
}

std::string UpscalerShortName(Upscaler upscaler)
{
    if (IsFsr(upscaler))
        return "FSR";

    switch (upscaler)
    {
    case Upscaler::XeSS:
    case Upscaler::XeSS_on12:
        return "XeSS";

    case Upscaler::DLSS:
        return "DLSS";

    case Upscaler::DLSSD:
        return "DLSSD";
    }

    return "????";
}

// Converts enum to the string codes for config
std::string UpscalerToCode(Upscaler upscaler)
{
    switch (upscaler)
    {
    case Upscaler::XeSS:
        return "xess";
    case Upscaler::XeSS_on12:
        return "xess_12";
    case Upscaler::FSR21:
        return "fsr21";
    case Upscaler::FSR21_on12:
        return "fsr21_12";
    case Upscaler::FSR22:
        return "fsr22";
    case Upscaler::FSR22_on12:
        return "fsr22_12";
    case Upscaler::FFX:
        return "ffx";
    case Upscaler::FFX_on12:
        return "ffx_12";
    case Upscaler::DLSS:
        return "dlss";
    case Upscaler::DLSSD:
        return "dlssd";
    case Upscaler::FSR31: // DX11 only
        return "fsr31";
    default: // Upscaler::Reset and unknown
        return "";
    }
}

// Converts string codes into enum for config
Upscaler CodeToUpscaler(const std::string& code)
{
    static const std::unordered_map<std::string, Upscaler> mapping = {
        { "xess", Upscaler::XeSS },   { "xess_12", Upscaler::XeSS_on12 },
        { "fsr21", Upscaler::FSR21 }, { "fsr21_12", Upscaler::FSR21_on12 },
        { "fsr22", Upscaler::FSR22 }, { "fsr22_12", Upscaler::FSR22_on12 },
        { "ffx", Upscaler::FFX },     { "ffx_12", Upscaler::FFX_on12 },
        { "dlss", Upscaler::DLSS },   { "dlssd", Upscaler::DLSSD },
        { "fsr31", Upscaler::FSR31 }, { "fsr31_12", Upscaler::FFX_on12 }, // for compat reasons
    };

    auto it = mapping.find(code);
    return (it != mapping.end()) ? it->second : Upscaler::Reset;
}

// Upscalers that use FFX got renamed from fsr31 to ffx
// Needs this function for compatibility for now
// DX11: fsr31 -> fsr31, fsr31_12 -> ffx_12
// DX12: fsr31 -> ffx
// VK:   fsr31 -> ffx, fsr31_12 -> ffx_12
Upscaler CodeToUpscalerFfx(const std::string& code)
{
    if (code == "fsr31")
        return Upscaler::FFX;

    return CodeToUpscaler(code);
}

// Converts enum to the string codes for config
std::string SharpnessShaderToCode(SharpenShader sharpenShader)
{
    switch (sharpenShader)
    {
    case SharpenShader::RCAS:
        return "rcas";
    case SharpenShader::DepthAware:
        return "da";
    case SharpenShader::LocalContrastDepthAware:
        return "lcda";
    default:
        return "";
    }
}

// Converts string codes into enum for config
SharpenShader CodeToSharpnessShader(const std::string& code)
{
    static const std::unordered_map<std::string, SharpenShader> mapping = {
        { "rcas", SharpenShader::RCAS },
        { "da", SharpenShader::DepthAware },
        { "lcda", SharpenShader::LocalContrastDepthAware },
    };

    auto it = mapping.find(code);
    return (it != mapping.end()) ? it->second : SharpenShader::RCAS;
}
