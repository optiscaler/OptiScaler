#include "pch.h"

#include "OptiTypes.h"
#include <misc/IdentifyGpu.h>

std::string ApiUpscalerInputName(ApiUpscalerInput upscaler)
{
    switch (upscaler)
    {
    case ApiUpscalerInput::DLSS_DX11:
    case ApiUpscalerInput::DLSS_DX12:
    case ApiUpscalerInput::DLSS_VK:
        return "DLSS";
    case ApiUpscalerInput::DLSSD_DX12:
    case ApiUpscalerInput::DLSSD_VK:
        return "DLSSD";
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
        // Native Vulkan gets FSR 4 from the driver
        if (fsr4Capable && (api == API::DX12 || api == API::Vulkan))
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

    case Upscaler::DLSS_on12:
        return "DLSS w/Dx12";

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
    case Upscaler::DLSS_on12:
        return "DLSS";

    case Upscaler::DLSSD:
        return "DLSSD";
    }

    return "????";
}
