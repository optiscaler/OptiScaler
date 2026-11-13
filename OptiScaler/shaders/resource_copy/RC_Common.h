#pragma once

#include <dxgiformat.h>

// DXGI formats for copying textures with the resource copy shaders
class RC_Formats
{
  public:
    // Typeless formats TranslateTypelessFormats doesn't cover, R16 is D16 depth or the half float FFX assumes
    static DXGI_FORMAT SrvFormat(DXGI_FORMAT format, bool depth)
    {
        switch (format)
        {
        case DXGI_FORMAT_R16_TYPELESS:
            return depth ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R16_FLOAT;

        case DXGI_FORMAT_R8_TYPELESS:
            return DXGI_FORMAT_R8_UNORM;

        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            return DXGI_FORMAT_B8G8R8X8_UNORM;

        default:
            return DXGI_FORMAT_UNKNOWN;
        }
    }

    // The copy must be writable through a UAV and read back the same values
    static DXGI_FORMAT BufferFormat(DXGI_FORMAT format, bool depth)
    {
        // Depth formats can't be UAVs, the shader reads the depth plane as float
        if (depth)
            return DXGI_FORMAT_R32_FLOAT;

        switch (format)
        {
        // sRGB can't be a UAV, the shader reads them decoded so keep the precision
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;

        // BGRA UAV stores are optional
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;

        default:
            if (auto srvFormat = SrvFormat(format, depth); srvFormat != DXGI_FORMAT_UNKNOWN)
                return srvFormat;

            return format;
        }
    }

    // Integer formats can't be read as float
    static bool IsInteger(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R32G32B32A32_SINT:
        case DXGI_FORMAT_R32G32B32_UINT:
        case DXGI_FORMAT_R32G32B32_SINT:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SINT:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_SINT:
        case DXGI_FORMAT_R10G10B10A2_UINT:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_R16G16_UINT:
        case DXGI_FORMAT_R16G16_SINT:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R32_SINT:
        case DXGI_FORMAT_R8G8_UINT:
        case DXGI_FORMAT_R8G8_SINT:
        case DXGI_FORMAT_R16_UINT:
        case DXGI_FORMAT_R16_SINT:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_SINT:
            return true;

        default:
            return false;
        }
    }
};
