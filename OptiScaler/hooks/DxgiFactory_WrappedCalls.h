#pragma once
#include "SysUtils.h"
#include <wrapped/wrapped_factory.h>
#include <dxgi1_6.h>

class DxgiFactoryWrappedCalls
{
  public:
    static HRESULT CreateSwapChain(IDXGIFactory* realFactory, IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC* pDesc,
                                   IDXGISwapChain** ppSwapChain);

    static HRESULT CreateSwapChainForHwnd(IDXGIFactory2* realFactory, IUnknown* pDevice, HWND hWnd,
                                          const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                          const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                          IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);

    static HRESULT CreateSwapChainForCoreWindow(IDXGIFactory2* realFactory, IUnknown* pDevice, IUnknown* pWindow,
                                                const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput,
                                                IDXGISwapChain1** ppSwapChain);

    static HRESULT EnumAdapters(IDXGIFactory* realFactory, UINT Adapter, IDXGIAdapter** ppAdapter);
    static HRESULT EnumAdapters1(IDXGIFactory1* realFactory, UINT Adapter, IDXGIAdapter1** ppAdapter);
    static HRESULT EnumAdapterByLuid(IDXGIFactory4* realFactory, LUID AdapterLuid, REFIID riid, void** ppvAdapter);
    static HRESULT EnumAdapterByGpuPreference(IDXGIFactory6* realFactory, UINT Adapter,
                                              DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void** ppvAdapter);

  private:
    // To prevent recursive FG swapchain creation
    inline static bool _skipFGSwapChainCreation = false;
    inline static bool _skipHighPerfCheck = false;

    static void CheckAdapter(IUnknown* unkAdapter);

    class ScopedSkipFGSCCreation
    {
      private:
        bool previousState;

      public:
        ScopedSkipFGSCCreation()
        {
            previousState = DxgiFactoryWrappedCalls::_skipFGSwapChainCreation;
            DxgiFactoryWrappedCalls::_skipFGSwapChainCreation = true;
        }
        ~ScopedSkipFGSCCreation() { DxgiFactoryWrappedCalls::_skipFGSwapChainCreation = previousState; }
    };

    class ScopedSkipHighPerfCheck
    {
      private:
        bool previousState;

      public:
        ScopedSkipHighPerfCheck()
        {
            previousState = DxgiFactoryWrappedCalls::_skipHighPerfCheck;
            DxgiFactoryWrappedCalls::_skipHighPerfCheck = true;
        }
        ~ScopedSkipHighPerfCheck() { DxgiFactoryWrappedCalls::_skipHighPerfCheck = previousState; }
    };
};
