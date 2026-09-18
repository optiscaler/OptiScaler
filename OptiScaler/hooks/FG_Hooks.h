#pragma once
#include "SysUtils.h"
#include <dxgi1_6.h>

#include "Hook_Utils.h"

class FGHooks
{
  public:
    static HRESULT CreateSwapChain(IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                   IDXGISwapChain** ppSwapChain);

    static HRESULT CreateSwapChainForHwnd(IDXGIFactory* This, IUnknown* pDevice, HWND hWnd,
                                          DXGI_SWAP_CHAIN_DESC1* pDesc,
                                          DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                          IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);

    // Registers a real FG swapchain created through FGHooks::CreateSwapChain*().
    static void SetFGSwapchain(IDXGISwapChain* pSwapChain, HWND hWnd);

    // Registers a plain/external DX12 presenting swapchain used by DX11->DX12 interop fallback.
    // This must not mark State::currentFGSwapchain and must not install FG present hooks.
    static void SetDx12InteropPresentSC(IDXGISwapChain* pSwapChain, HWND hWnd);
    static void ClearDx12InteropPresentSC(IUnknown* pSwapChain);
    static bool IsDx12InteropPresentSC(IUnknown* pSwapChain);

  private:
    using PFN_Present = rewrite_signature<decltype(&IDXGISwapChain::Present)>::type;
    using PFN_Present1 = rewrite_signature<decltype(&IDXGISwapChain1::Present1)>::type;
    using PFN_SetFullscreenState = rewrite_signature<decltype(&IDXGISwapChain::SetFullscreenState)>::type;
    using PFN_GetFullscreenState = rewrite_signature<decltype(&IDXGISwapChain::GetFullscreenState)>::type;
    using PFN_GetFullscreenDesc = rewrite_signature<decltype(&IDXGISwapChain1::GetFullscreenDesc)>::type;
    using PFN_ResizeBuffers = rewrite_signature<decltype(&IDXGISwapChain::ResizeBuffers)>::type;
    using PFN_ResizeBuffers1 = rewrite_signature<decltype(&IDXGISwapChain3::ResizeBuffers1)>::type;
    using PFN_ResizeTarget = rewrite_signature<decltype(&IDXGISwapChain::ResizeTarget)>::type;
    using PFN_GetFrameLatencyWaitableObject =
        rewrite_signature<decltype(&IDXGISwapChain2::GetFrameLatencyWaitableObject)>::type;
    using PFN_Release = rewrite_signature<decltype(&IUnknown::Release)>::type;

    inline static PFN_ResizeBuffers o_FGSCResizeBuffers = nullptr;
    inline static PFN_ResizeTarget o_FGSCResizeTarget = nullptr;
    inline static PFN_ResizeBuffers1 o_FGSCResizeBuffers1 = nullptr;
    inline static PFN_SetFullscreenState o_FGSCSetFullscreenState = nullptr;
    inline static PFN_GetFullscreenState o_FGSCGetFullscreenState = nullptr;
    inline static PFN_GetFullscreenDesc o_FGSCGetFullscreenDesc = nullptr;
    inline static PFN_Present o_FGSCPresent = nullptr;
    inline static PFN_Present1 o_FGSCPresent1 = nullptr;
    inline static PFN_Release o_FGRelease = nullptr;
    inline static PFN_GetFrameLatencyWaitableObject o_FGSCGetFrameLatencyWaitableObject = nullptr;
    inline static HWND _hwnd = nullptr;
    inline static IDXGISwapChain* _dx12InteropPresentSC = nullptr;
    inline static HWND _dx12InteropPresentHwnd = nullptr;
    inline static bool _skipResize = false;
    inline static bool _skipResize1 = false;
    inline static bool _skipPresent = false;
    inline static bool _skipPresent1 = false;
    inline static UINT _lastPresentFlags = 0;
    inline static double _lastFGFrameTime = -1.0;
    inline static std::shared_mutex _resizeMutex;

    static void HookFGSwapchain(IDXGISwapChain* pSwapChain);

    static HRESULT hkSetFullscreenState(IDXGISwapChain* This, BOOL Fullscreen, IDXGIOutput* pTarget);
    static HRESULT hkGetFullscreenDesc(IDXGISwapChain1* This, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc);
    static HRESULT hkGetFullscreenState(IDXGISwapChain* This, BOOL* pFullscreen, IDXGIOutput** ppTarget);
    static HRESULT hkResizeBuffers(IDXGISwapChain* This, UINT BufferCount, UINT Width, UINT Height,
                                   DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    static HRESULT hkResizeTarget(IDXGISwapChain* This, const DXGI_MODE_DESC* pNewTargetParameters);
    static HRESULT hkResizeBuffers1(IDXGISwapChain3* This, UINT BufferCount, UINT Width, UINT Height,
                                    DXGI_FORMAT Format, UINT SwapChainFlags, const UINT* pCreationNodeMask,
                                    IUnknown* const* ppPresentQueue);
    static ULONG hkFGRelease(IUnknown* This);

    static HRESULT hkFGPresent(IDXGISwapChain* This, UINT SyncInterval, UINT Flags);
    static HRESULT hkFGPresent1(IDXGISwapChain1* This, UINT SyncInterval, UINT Flags,
                                const DXGI_PRESENT_PARAMETERS* pPresentParameters);
    static HRESULT FGPresent(IDXGISwapChain* This, UINT SyncInterval, UINT Flags,
                             const DXGI_PRESENT_PARAMETERS* pPresentParameters);

    static HANDLE hkGetFrameLatencyWaitableObject(IDXGISwapChain2* This);

    VALIDATE_MEMBER_HOOK(hkFGPresent, PFN_Present)
    VALIDATE_MEMBER_HOOK(hkFGPresent1, PFN_Present1)
    VALIDATE_MEMBER_HOOK(hkSetFullscreenState, PFN_SetFullscreenState)
    VALIDATE_MEMBER_HOOK(hkGetFullscreenState, PFN_GetFullscreenState)
    VALIDATE_MEMBER_HOOK(hkGetFullscreenDesc, PFN_GetFullscreenDesc)
    VALIDATE_MEMBER_HOOK(hkResizeBuffers, PFN_ResizeBuffers)
    VALIDATE_MEMBER_HOOK(hkResizeBuffers1, PFN_ResizeBuffers1)
    VALIDATE_MEMBER_HOOK(hkResizeTarget, PFN_ResizeTarget)
    VALIDATE_MEMBER_HOOK(hkGetFrameLatencyWaitableObject, PFN_GetFrameLatencyWaitableObject)
    VALIDATE_MEMBER_HOOK(hkFGRelease, PFN_Release)
};
