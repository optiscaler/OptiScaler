#include <pch.h>
#include "wrapped_factory.h"

#include <hooks/DxgiFactory_WrappedCalls.h>

#include <detours/detours.h>

typedef HRESULT (*PFN_GetParent)(IDXGIAdapter* This, REFIID riid, void** ppParent);

static PFN_GetParent o_GetParent = nullptr;

static HRESULT hkGetParent(IDXGIAdapter* This, REFIID riid, void** ppParent)
{
    auto result = o_GetParent(This, riid, ppParent);

    if (State::Instance().skipParentWrapping)
        return result;

    if (result != S_OK)
        return result;

    auto f = reinterpret_cast<IDXGIFactory*>(*ppParent);
    if (f == nullptr)
        return result;

    auto wf = new WrappedIDXGIFactory7(f);
    *ppParent = wf;

    return result;
}

static void AttachToAdapter(IDXGIAdapter* adapter)
{
    if (o_GetParent != nullptr)
        return;

    // Get the vtable pointer
    PVOID* pVTable = *(PVOID**) adapter;

    o_GetParent = (PFN_GetParent) pVTable[6];

    // Apply the detour
    if (o_GetParent != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_GetParent, hkGetParent);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to attach GetParent detour: {:X}", detourResult);
            o_GetParent = nullptr;
        }
    }
}

static UINT fCount = 0;

WrappedIDXGIFactory7::WrappedIDXGIFactory7(IDXGIFactory* real)
{
    if (real == nullptr)
        return;

    fCount++;
    _id = fCount;

    LOG_WARN("{}", _id);

    _real = real;
    _refCount = 1;

    if (real->QueryInterface(IID_PPV_ARGS(&_real1)) == S_OK)
        _real1->Release();

    if (real->QueryInterface(IID_PPV_ARGS(&_real2)) == S_OK)
        _real2->Release();

    if (real->QueryInterface(IID_PPV_ARGS(&_real3)) == S_OK)
        _real3->Release();

    if (real->QueryInterface(IID_PPV_ARGS(&_real4)) == S_OK)
        _real4->Release();

    if (real->QueryInterface(IID_PPV_ARGS(&_real5)) == S_OK)
        _real5->Release();

    if (real->QueryInterface(IID_PPV_ARGS(&_real6)) == S_OK)
        _real6->Release();

    if (real->QueryInterface(IID_PPV_ARGS(&_real7)) == S_OK)
        _real7->Release();
}

WrappedIDXGIFactory7::~WrappedIDXGIFactory7() {}

HRESULT __stdcall WrappedIDXGIFactory7::QueryInterface(REFIID riid, void** ppvObject)
{
    if (riid == __uuidof(IDXGIFactory))
    {
        AddRef();
        *ppvObject = (IDXGIFactory*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGIFactory1) && _real1 != nullptr)
    {
        AddRef();
        *ppvObject = (IDXGIFactory1*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGIFactory2) && _real2 != nullptr)
    {
        AddRef();
        *ppvObject = (IDXGIFactory2*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGIFactory3) && _real3 != nullptr)
    {
        AddRef();
        *ppvObject = (IDXGIFactory3*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGIFactory4) && _real4 != nullptr)
    {
        AddRef();
        *ppvObject = (IDXGIFactory4*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGIFactory5) && _real5 != nullptr)
    {
        AddRef();
        *ppvObject = (IDXGIFactory5*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGIFactory6) && _real6 != nullptr)
    {
        AddRef();
        *ppvObject = (IDXGIFactory6*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGIFactory7) && _real7 != nullptr)
    {
        AddRef();
        *ppvObject = (IDXGIFactory7*) this;
        return S_OK;
    }
    else if (riid == __uuidof(this))
    {
        AddRef();
        *ppvObject = this;
        return S_OK;
    }
    else if (riid == __uuidof(IUnknown))
    {
        AddRef();
        *ppvObject = (IUnknown*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGIObject))
    {
        AddRef();
        *ppvObject = (IDXGIObject*) this;
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG __stdcall WrappedIDXGIFactory7::AddRef()
{
    InterlockedIncrement(&_refCount);
    return _refCount;
}

ULONG __stdcall WrappedIDXGIFactory7::Release()
{
    ULONG now = InterlockedDecrement(&_refCount);
    if (now == 0)
    {
        LOG_WARN("id: {}", _id);
        _real->Release();
        delete this;
    }

    return now;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGIFactory7::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData)
{
    return _real->SetPrivateData(Name, DataSize, pData);
}

HRESULT __stdcall WrappedIDXGIFactory7::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown)
{
    return _real->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT __stdcall WrappedIDXGIFactory7::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData)
{
    return _real->GetPrivateData(Name, pDataSize, pData);
}

HRESULT __stdcall WrappedIDXGIFactory7::GetParent(REFIID riid, void** ppParent)
{
    return _real->GetParent(riid, ppParent);
}

HRESULT __stdcall WrappedIDXGIFactory7::EnumAdapters(UINT Adapter, IDXGIAdapter** ppAdapter)
{
    auto result = DxgiFactoryWrappedCalls::EnumAdapters(_real, Adapter, ppAdapter);

    if (result == S_OK)
        AttachToAdapter((IDXGIAdapter*) *ppAdapter);

    return result;
}

HRESULT __stdcall WrappedIDXGIFactory7::MakeWindowAssociation(HWND WindowHandle, UINT Flags)
{
    return _real->MakeWindowAssociation(WindowHandle, Flags);
}

HRESULT __stdcall WrappedIDXGIFactory7::GetWindowAssociation(HWND* pWindowHandle)
{
    return _real->GetWindowAssociation(pWindowHandle);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGIFactory7::CreateSwapChain(IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                                                IDXGISwapChain** ppSwapChain)
{
    auto result = DxgiFactoryWrappedCalls::CreateSwapChain(_real, this, pDevice, pDesc, ppSwapChain);

    if (SUCCEEDED(result))
        return result;

    return _real->CreateSwapChain(pDevice, pDesc, ppSwapChain);
}

HRESULT __stdcall WrappedIDXGIFactory7::CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter** ppAdapter)
{
    return _real->CreateSoftwareAdapter(Module, ppAdapter);
}

HRESULT __stdcall WrappedIDXGIFactory7::EnumAdapters1(UINT Adapter, IDXGIAdapter1** ppAdapter)
{
    if (_real1 == nullptr)
        return E_NOTIMPL;

    auto result = DxgiFactoryWrappedCalls::EnumAdapters1(_real1, Adapter, ppAdapter);

    if (result == S_OK)
        AttachToAdapter((IDXGIAdapter*) *ppAdapter);

    return result;
}

BOOL __stdcall WrappedIDXGIFactory7::IsCurrent()
{
    if (_real1 == nullptr)
        return 0;

    return _real1->IsCurrent();
}

BOOL __stdcall WrappedIDXGIFactory7::IsWindowedStereoEnabled()
{
    if (_real2 == nullptr)
        return 0;

    return _real2->IsWindowedStereoEnabled();
}

HRESULT STDMETHODCALLTYPE
WrappedIDXGIFactory7::CreateSwapChainForHwnd(IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                             const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                             IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)
{
    if (_real2 == nullptr)
        return E_NOTIMPL;

    HRESULT result;
    DXGI_SWAP_CHAIN_DESC1 localDesc = *pDesc;

    if (pFullscreenDesc != nullptr)
    {
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC localFSDesc = *pFullscreenDesc;

        result = DxgiFactoryWrappedCalls::CreateSwapChainForHwnd(_real2, this, pDevice, hWnd, &localDesc, &localFSDesc,
                                                                 pRestrictToOutput, ppSwapChain);
    }
    else
    {
        result = DxgiFactoryWrappedCalls::CreateSwapChainForHwnd(_real2, this, pDevice, hWnd, &localDesc, nullptr,
                                                                 pRestrictToOutput, ppSwapChain);
    }

    if (SUCCEEDED(result))
        return result;

    return _real2->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
}

HRESULT __stdcall WrappedIDXGIFactory7::CreateSwapChainForCoreWindow(IUnknown* pDevice, IUnknown* pWindow,
                                                                     const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                                     IDXGIOutput* pRestrictToOutput,
                                                                     IDXGISwapChain1** ppSwapChain)
{
    if (_real2 == nullptr)
        return E_NOTIMPL;

    DXGI_SWAP_CHAIN_DESC1 localDesc = *pDesc;

    auto result = DxgiFactoryWrappedCalls::CreateSwapChainForCoreWindow(_real2, pDevice, pWindow, &localDesc,
                                                                        pRestrictToOutput, ppSwapChain);

    if (SUCCEEDED(result))
        return result;

    return _real2->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
}

HRESULT __stdcall WrappedIDXGIFactory7::GetSharedResourceAdapterLuid(HANDLE hResource, LUID* pLuid)
{
    if (_real2 == nullptr)
        return E_NOTIMPL;

    return _real2->GetSharedResourceAdapterLuid(hResource, pLuid);
}

HRESULT __stdcall WrappedIDXGIFactory7::RegisterStereoStatusWindow(HWND WindowHandle, UINT wMsg, DWORD* pdwCookie)
{
    if (_real2 == nullptr)
        return E_NOTIMPL;

    return _real2->RegisterStereoStatusWindow(WindowHandle, wMsg, pdwCookie);
}

HRESULT __stdcall WrappedIDXGIFactory7::RegisterStereoStatusEvent(HANDLE hEvent, DWORD* pdwCookie)
{
    if (_real2 == nullptr)
        return E_NOTIMPL;

    return _real2->RegisterStereoStatusEvent(hEvent, pdwCookie);
}

void __stdcall WrappedIDXGIFactory7::UnregisterStereoStatus(DWORD dwCookie)
{
    if (_real2 == nullptr)
        return;

    _real2->UnregisterStereoStatus(dwCookie);
}

HRESULT __stdcall WrappedIDXGIFactory7::RegisterOcclusionStatusWindow(HWND WindowHandle, UINT wMsg, DWORD* pdwCookie)
{
    if (_real2 == nullptr)
        return E_NOTIMPL;

    return _real2->RegisterOcclusionStatusWindow(WindowHandle, wMsg, pdwCookie);
}

HRESULT __stdcall WrappedIDXGIFactory7::RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD* pdwCookie)
{
    if (_real2 == nullptr)
        return E_NOTIMPL;

    return _real2->RegisterOcclusionStatusEvent(hEvent, pdwCookie);
}

void __stdcall WrappedIDXGIFactory7::UnregisterOcclusionStatus(DWORD dwCookie)
{
    if (_real2 == nullptr)
        return;

    _real2->UnregisterOcclusionStatus(dwCookie);
}

HRESULT __stdcall WrappedIDXGIFactory7::CreateSwapChainForComposition(IUnknown* pDevice,
                                                                      const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                                      IDXGIOutput* pRestrictToOutput,
                                                                      IDXGISwapChain1** ppSwapChain)
{
    if (_real2 == nullptr)
        return E_NOTIMPL;

    return _real2->CreateSwapChainForComposition(pDevice, pDesc, pRestrictToOutput, ppSwapChain);
}

UINT __stdcall WrappedIDXGIFactory7::GetCreationFlags()
{
    if (_real3 == nullptr)
        return 0;

    return _real3->GetCreationFlags();
}

HRESULT __stdcall WrappedIDXGIFactory7::EnumAdapterByLuid(LUID AdapterLuid, REFIID riid, void** ppvAdapter)
{
    if (_real4 == nullptr)
        return E_NOTIMPL;

    return DxgiFactoryWrappedCalls::EnumAdapterByLuid(_real4, AdapterLuid, riid, ppvAdapter);
}

HRESULT __stdcall WrappedIDXGIFactory7::EnumWarpAdapter(REFIID riid, void** ppvAdapter)
{
    if (_real4 == nullptr)
        return E_NOTIMPL;

    auto result = _real4->EnumWarpAdapter(riid, ppvAdapter);

    if (result == S_OK)
        AttachToAdapter((IDXGIAdapter*) *ppvAdapter);

    return result;
}

HRESULT __stdcall WrappedIDXGIFactory7::CheckFeatureSupport(DXGI_FEATURE Feature, void* pFeatureSupportData,
                                                            UINT FeatureSupportDataSize)
{
    if (_real5 == nullptr)
        return E_NOTIMPL;

    return _real5->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
}

HRESULT __stdcall WrappedIDXGIFactory7::EnumAdapterByGpuPreference(UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference,
                                                                   REFIID riid, void** ppvAdapter)
{
    if (_real6 == nullptr)
        return E_NOTIMPL;

    auto result = DxgiFactoryWrappedCalls::EnumAdapterByGpuPreference(_real6, Adapter, GpuPreference, riid, ppvAdapter);

    if (result == S_OK)
        AttachToAdapter((IDXGIAdapter*) *ppvAdapter);

    return result;
}

HRESULT __stdcall WrappedIDXGIFactory7::RegisterAdaptersChangedEvent(HANDLE hEvent, DWORD* pdwCookie)
{
    if (_real7 == nullptr)
        return E_NOTIMPL;

    return _real7->RegisterAdaptersChangedEvent(hEvent, pdwCookie);
}

HRESULT __stdcall WrappedIDXGIFactory7::UnregisterAdaptersChangedEvent(DWORD dwCookie)
{
    if (_real7 == nullptr)
        return E_NOTIMPL;

    return _real7->UnregisterAdaptersChangedEvent(dwCookie);
}
