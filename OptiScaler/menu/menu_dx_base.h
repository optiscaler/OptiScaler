#pragma once
#include "SysUtils.h"
#include <dxgi.h>

class MenuDxBase
{
  private:
    HWND _handle = nullptr;
    bool _baseInit = false;
    int _width = 400;
    int _height = 400;
    int _top = 10;
    int _left = 10;

  protected:
    long frameCounter = 0;
    static bool RenderMenu();

    static DXGI_FORMAT TranslateTypelessFormats(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R32G32B32_TYPELESS:
            return DXGI_FORMAT_R32G32B32_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R16G16_TYPELESS:
            return DXGI_FORMAT_R16G16_FLOAT;
        case DXGI_FORMAT_R32G32_TYPELESS:
            return DXGI_FORMAT_R32G32_FLOAT;
        default:
            return format;
        }
    }

  public:
    static bool IsVisible();
    bool IsInited() const { return _baseInit; }
    int Width() { return _width; }
    int Height() { return _height; }
    int Top() { return _top; }
    int Left() { return _left; }
    HWND Handle() { return _handle; }
    bool IsHandleDifferent();
    static void Dx11Ready();
    static void Dx12Ready();

    explicit MenuDxBase(HWND handle);

    ~MenuDxBase();
};
