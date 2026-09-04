#pragma once

#include "SysUtils.h"

#include <d3d12.h>
#include <dxgi1_6.h>

class MenuOverlayBase
{
  public:
    static HWND Handle();

    static void Dx11Ready();
    static void Dx12Ready();
    static void VulkanReady();

    static bool IsInited();
    static bool IsVisible();

    static void Init(HWND InHandle, bool isUWP);
    static bool RenderMenu();
    static void Shutdown();
    static void HideMenu();
    static void Present();
    static void ApplyThemeStyle();
};
