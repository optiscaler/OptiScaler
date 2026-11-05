#pragma once

#include "SysUtils.h"
#include <OwnedMutex.h>
#include <Config.h>

#include "dxgi1_6.h"

class DECLSPEC_UUID("1af622a3-82d0-49cd-124f-cce05122c222") WrappedIDXGIFactory7 final : public IDXGIFactory7
{
  private:
    IDXGIFactory* _real = nullptr;
    IDXGIFactory1* _real1 = nullptr;
    IDXGIFactory2* _real2 = nullptr;
    IDXGIFactory3* _real3 = nullptr;
    IDXGIFactory4* _real4 = nullptr;
    IDXGIFactory5* _real5 = nullptr;
    IDXGIFactory6* _real6 = nullptr;
    IDXGIFactory7* _real7 = nullptr;
    ULONG _refCount = 0;
    UINT _id = 0;

    // Real factory's identity, key of the wrapper registry
    IUnknown* _identity = nullptr;

    bool TryAddRef();

  public:
    WrappedIDXGIFactory7(IDXGIFactory* real);
    virtual ~WrappedIDXGIFactory7();

    // Replaces a GetParent result with the factory's wrapper
    static void WrapParent(REFIID riid, void** ppParent, void* returnAddress);

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();

    // IDXGIObject
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData);
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown);
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData);
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent);

    // IDXGIFactory
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT Adapter, IDXGIAdapter** ppAdapter);
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND WindowHandle, UINT Flags);
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND* pWindowHandle);
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                              IDXGISwapChain** ppSwapChain);
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter** ppAdapter);

    // IDXGIFactory1
    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT Adapter, IDXGIAdapter1** ppAdapter);
    BOOL STDMETHODCALLTYPE IsCurrent();

    // IDXGIFactory2
    BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled();
    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                     const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                                     IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);
    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown* pDevice, IUnknown* pWindow,
                                                           const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                           IDXGIOutput* pRestrictToOutput,
                                                           IDXGISwapChain1** ppSwapChain);
    HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE hResource, LUID* pLuid);
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND WindowHandle, UINT wMsg, DWORD* pdwCookie);
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE hEvent, DWORD* pdwCookie);
    void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD dwCookie);
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND WindowHandle, UINT wMsg, DWORD* pdwCookie);
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD* pdwCookie);
    void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD dwCookie);
    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                            IDXGIOutput* pRestrictToOutput,
                                                            IDXGISwapChain1** ppSwapChain);

    // IDXGIFactory3
    UINT STDMETHODCALLTYPE GetCreationFlags();

    // IDXGIFactory4
    HRESULT STDMETHODCALLTYPE EnumAdapterByLuid(LUID AdapterLuid, REFIID riid, void** ppvAdapter);
    HRESULT STDMETHODCALLTYPE EnumWarpAdapter(REFIID riid, void** ppvAdapter);

    // IDXGIFactory5
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DXGI_FEATURE Feature, void* pFeatureSupportData,
                                                  UINT FeatureSupportDataSize);

    // IDXGIFactory6
    HRESULT STDMETHODCALLTYPE EnumAdapterByGpuPreference(UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference, REFIID riid,
                                                         void** ppvAdapter);

    // IDXGIFactory7
    HRESULT STDMETHODCALLTYPE RegisterAdaptersChangedEvent(HANDLE hEvent, DWORD* pdwCookie);
    HRESULT STDMETHODCALLTYPE UnregisterAdaptersChangedEvent(DWORD dwCookie);
};
