#pragma once
#include "SysUtils.h"
#include "FG_Hooks.h"
#include <dxgi1_6.h>

#include "Hook_Utils.h"

class DxgiFactoryHooks
{
  public:
    static void HookToFactory(IDXGIFactory* pFactory);
    static void HookToDLSSGFactory(IDXGIFactory* pFactory);

  private:
    using PFN_EnumAdapterByGpuPreference =
        rewrite_signature<decltype(&IDXGIFactory6::EnumAdapterByGpuPreference)>::type;
    using PFN_EnumAdapterByLuid = rewrite_signature<decltype(&IDXGIFactory4::EnumAdapterByLuid)>::type;
    using PFN_EnumAdapters1 = rewrite_signature<decltype(&IDXGIFactory1::EnumAdapters1)>::type;
    using PFN_EnumAdapters = rewrite_signature<decltype(&IDXGIFactory::EnumAdapters)>::type;
    using PFN_CreateSwapChain = rewrite_signature<decltype(&IDXGIFactory::CreateSwapChain)>::type;
    using PFN_CreateSwapChainForHwnd = rewrite_signature<decltype(&IDXGIFactory2::CreateSwapChainForHwnd)>::type;
    using PFN_CreateSwapChainForCoreWindow =
        rewrite_signature<decltype(&IDXGIFactory2::CreateSwapChainForCoreWindow)>::type;
    using PFN_CreateSwapChainForComposition =
        rewrite_signature<decltype(&IDXGIFactory2::CreateSwapChainForComposition)>::type;

    inline static PFN_EnumAdapterByGpuPreference o_EnumAdapterByGpuPreference = nullptr;
    inline static PFN_EnumAdapterByLuid o_EnumAdapterByLuid = nullptr;
    inline static PFN_EnumAdapters1 o_EnumAdapters1 = nullptr;
    inline static PFN_EnumAdapters o_EnumAdapters = nullptr;
    inline static PFN_CreateSwapChain o_CreateSwapChain = nullptr;
    inline static PFN_CreateSwapChainForHwnd o_CreateSwapChainForHwnd = nullptr;
    inline static PFN_CreateSwapChainForCoreWindow o_CreateSwapChainForCoreWindow = nullptr;
    inline static PFN_CreateSwapChainForComposition o_CreateSwapChainForComposition = nullptr;

    inline static PFN_CreateSwapChain o_DLSSGCreateSwapChain = nullptr;
    inline static PFN_CreateSwapChainForHwnd o_DLSSGCreateSwapChainForHwnd = nullptr;
    inline static PFN_CreateSwapChainForCoreWindow o_DLSSGCreateSwapChainForCoreWindow = nullptr;

    static HRESULT CreateSwapChain(IDXGIFactory* realFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                   IDXGISwapChain** ppSwapChain);

    static HRESULT CreateSwapChainForHwnd(IDXGIFactory2* realFactory, IUnknown* pDevice, HWND hWnd,
                                          const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                          const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                          IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);

    static HRESULT CreateSwapChainForCoreWindow(IDXGIFactory2* realFactory, IUnknown* pDevice, IUnknown* pWindow,
                                                const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput,
                                                IDXGISwapChain1** ppSwapChain);

    static HRESULT DLSSGCreateSwapChain(IDXGIFactory* realFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                        IDXGISwapChain** ppSwapChain);
    static HRESULT CreateSwapChainForComposition(IDXGIFactory2* realFactory, IUnknown* pDevice,
                                                 const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput,
                                                 IDXGISwapChain1** ppSwapChain);

    static HRESULT DLSSGCreateSwapChainForHwnd(IDXGIFactory2* realFactory, IUnknown* pDevice, HWND hWnd,
                                               const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                               const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                               IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);

    static HRESULT DLSSGCreateSwapChainForCoreWindow(IDXGIFactory2* realFactory, IUnknown* pDevice, IUnknown* pWindow,
                                                     const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput,
                                                     IDXGISwapChain1** ppSwapChain);

    static HRESULT EnumAdapters(IDXGIFactory* realFactory, UINT Adapter, IDXGIAdapter** ppAdapter);
    static HRESULT EnumAdapters1(IDXGIFactory1* realFactory, UINT Adapter, IDXGIAdapter1** ppAdapter);
    static HRESULT EnumAdapterByLuid(IDXGIFactory4* realFactory, LUID AdapterLuid, REFIID riid, void** ppvAdapter);
    static HRESULT EnumAdapterByGpuPreference(IDXGIFactory6* realFactory, UINT Adapter,
                                              DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void** ppvAdapter);

    VALIDATE_MEMBER_HOOK(CreateSwapChain, PFN_CreateSwapChain)
    VALIDATE_MEMBER_HOOK(CreateSwapChainForHwnd, PFN_CreateSwapChainForHwnd)
    VALIDATE_MEMBER_HOOK(CreateSwapChainForCoreWindow, PFN_CreateSwapChainForCoreWindow)
    VALIDATE_MEMBER_HOOK(CreateSwapChainForComposition, PFN_CreateSwapChainForComposition)
    VALIDATE_MEMBER_HOOK(EnumAdapters, PFN_EnumAdapters)
    VALIDATE_MEMBER_HOOK(EnumAdapters1, PFN_EnumAdapters1)
    VALIDATE_MEMBER_HOOK(EnumAdapterByLuid, PFN_EnumAdapterByLuid)
    VALIDATE_MEMBER_HOOK(EnumAdapterByGpuPreference, PFN_EnumAdapterByGpuPreference)

    // To prevent recursive FG swapchain creation
    inline static bool _skipFGSwapChainCreation = false;

    class ScopedSkipFGSCCreation
    {
      private:
        bool previousState;

      public:
        ScopedSkipFGSCCreation()
        {
            previousState = DxgiFactoryHooks::_skipFGSwapChainCreation;
            DxgiFactoryHooks::_skipFGSwapChainCreation = true;
        }
        ~ScopedSkipFGSCCreation() { DxgiFactoryHooks::_skipFGSwapChainCreation = previousState; }
    };
};
