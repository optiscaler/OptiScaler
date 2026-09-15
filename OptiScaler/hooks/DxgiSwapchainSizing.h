#pragma once

#include <windows.h>
#include <dxgi.h>

// Resolve DXGI's 0x0 "use the window's client size" descriptor before the small-swapchain
// heuristic and before FG/interop can substitute a different HWND. Only modify our local copy.
// Window visibility is not a size test: games often create the swapchain before showing the window.
inline bool ResolveWindowSizedSwapchain(DXGI_SWAP_CHAIN_DESC& desc)
{
    if (desc.BufferDesc.Width != 0 || desc.BufferDesc.Height != 0 || desc.OutputWindow == nullptr)
        return false;

    RECT rect {};
    if (!GetClientRect(desc.OutputWindow, &rect))
        return false;
    const LONG width = rect.right - rect.left;
    const LONG height = rect.bottom - rect.top;
    if (width < 100 || height < 100)
        return false;

    desc.BufferDesc.Width = (UINT) width;
    desc.BufferDesc.Height = (UINT) height;
    return true;
}

inline bool IsCompositionWindow(HWND window)
{
    DWORD pid = 0;
    RECT rect {};
    return window != nullptr && window != GetConsoleWindow() && GetWindow(window, GW_OWNER) == nullptr &&
           GetWindowThreadProcessId(window, &pid) != 0 && pid == GetCurrentProcessId() &&
           GetClientRect(window, &rect) && rect.right - rect.left >= 100 && rect.bottom - rect.top >= 100;
}

inline HWND FindCompositionWindow()
{
    const HWND foreground = GetForegroundWindow();
    if (IsCompositionWindow(foreground))
        return foreground;
    struct Search { HWND window = nullptr; unsigned int count = 0; } search;
    EnumWindows([](HWND window, LPARAM context) -> BOOL {
        auto& found = *reinterpret_cast<Search*>(context);
        if (IsCompositionWindow(window)) { found.window = window; ++found.count; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.count == 1 ? search.window : nullptr;
}
