#include "pch.h"
#include "menu_overlay_base.h"
#include "menu_common.h"

#include <Config.h>
#include <Logger.h>
#include <resource.h>

// #include "imgui/imgui.h"
// #include "imgui/imgui_impl_win32.h"

HWND MenuOverlayBase::Handle() { return MenuCommon::Handle(); }

void MenuOverlayBase::Dx11Ready() { MenuCommon::Dx11Inited(); }

void MenuOverlayBase::Dx12Ready() { MenuCommon::Dx12Inited(); }

void MenuOverlayBase::VulkanReady() { MenuCommon::VulkanInited(); }

bool MenuOverlayBase::IsInited() { return MenuCommon::IsInited(); }

bool MenuOverlayBase::IsVisible() { return MenuCommon::IsVisible(); }

void MenuOverlayBase::Init(HWND InHandle, bool isUWP)
{
    if (!Config::Instance()->OverlayMenu.value_or_default())
        return;

    LOG_FUNC();
    MenuCommon::Init(InHandle, isUWP);
}

bool MenuOverlayBase::RenderMenu()
{
    if (!Config::Instance()->OverlayMenu.value_or_default())
        return false;

    return MenuCommon::RenderMenu();
}

void MenuOverlayBase::Present() { MenuCommon::Present(); }

void MenuOverlayBase::Shutdown() { MenuCommon::Shutdown(); }

void MenuOverlayBase::HideMenu() { MenuCommon::HideMenu(); }

void MenuOverlayBase::ApplyThemeStyle() { MenuCommon::ApplyThemeStyle(); }
