#pragma once

#include "SysUtils.h"

#include <proxies/Dxgi_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

struct dxgi_dll
{
    HMODULE dll = nullptr;

    using PFN_ApplyCompatResolutionQuirking = HRESULT(WINAPI*)(void* p1, void* p2);
    using PFN_CompatString = BOOL(WINAPI*)(LPCSTR szName, ULONG* pSize, LPSTR lpData, bool flag);
    using PFN_CompatValue = BOOL(WINAPI*)(LPCSTR szName, UINT64* pValue);
    using PFN_DXGIDisableVBlankVirtualization = HRESULT(WINAPI*)();
    using PFN_DXGIDumpJournal = HRESULT(WINAPI*)(ULONG_PTR p1);
    using PFN_DXGIReportAdapterConfiguration = HRESULT(WINAPI*)(ULONG_PTR p1);
    using PFN_DXGID3D10CreateDevice = HRESULT(WINAPI*)(HMODULE hModule, IDXGIFactory* pFactory, IDXGIAdapter* pAdapter,
                                                       UINT flags, void* pUnknown, void** ppDevice);
    using PFN_DXGID3D10CreateLayeredDevice = HRESULT(WINAPI*)(IDXGIAdapter* pAdapter, UINT flags, void* pUnknown,
                                                              REFIID riid, void** ppDevice);
    using PFN_DXGID3D10GetLayeredDeviceSize = SIZE_T(WINAPI*)(const void* pLayers, UINT numLayers);
    using PFN_DXGID3D10RegisterLayers = HRESULT(WINAPI*)(const void* pLayers, UINT numLayers);
    using PFN_DXGID3D10ETWRundown = void(WINAPI*)();
    using PFN_PIXBeginCapture = HRESULT(WINAPI*)(DWORD captureFlags, const void* captureParameters);
    using PFN_PIXEndCapture = HRESULT(WINAPI*)(BOOL discard);
    using PFN_PIXGetCaptureState = DWORD(WINAPI*)();
    using PFN_SetAppCompatStringPointer = HRESULT(WINAPI*)(ULONG_PTR p1, ULONG_PTR p2);
    using PFN_UpdateHMDEmulationStatus = HRESULT(WINAPI*)(ULONG_PTR p1);

    PFN_ApplyCompatResolutionQuirking ApplyCompatResolutionQuirking = nullptr;
    PFN_CompatString CompatString = nullptr;
    PFN_CompatValue CompatValue = nullptr;

    PFN_DXGID3D10CreateDevice D3D10CreateDevice = nullptr;
    PFN_DXGID3D10CreateLayeredDevice D3D10CreateLayeredDevice = nullptr;
    PFN_DXGID3D10GetLayeredDeviceSize D3D10GetLayeredDeviceSize = nullptr;
    PFN_DXGID3D10RegisterLayers D3D10RegisterLayers = nullptr;
    PFN_DXGID3D10ETWRundown D3D10ETWRundown = nullptr;

    PFN_DXGIDumpJournal DumpJournal = nullptr;
    PFN_DXGIReportAdapterConfiguration ReportAdapterConfiguration = nullptr;

    PFN_PIXBeginCapture PIXBeginCapture = nullptr;
    PFN_PIXEndCapture PIXEndCapture = nullptr;
    PFN_PIXGetCaptureState PIXGetCaptureState = nullptr;

    PFN_SetAppCompatStringPointer SetAppCompatStringPointer = nullptr;
    PFN_UpdateHMDEmulationStatus UpdateHMDEmulationStatus = nullptr;

    PFN_DXGIDisableVBlankVirtualization DisableVBlankVirtualization = nullptr;

    void LoadOriginalLibrary(HMODULE module)
    {
        dll = module;

        ApplyCompatResolutionQuirking = reinterpret_cast<PFN_ApplyCompatResolutionQuirking>(
            KernelBaseProxy::GetProcAddress_()(module, "ApplyCompatResolutionQuirking"));
        CompatString = reinterpret_cast<PFN_CompatString>(KernelBaseProxy::GetProcAddress_()(module, "CompatString"));
        CompatValue = reinterpret_cast<PFN_CompatValue>(KernelBaseProxy::GetProcAddress_()(module, "CompatValue"));
        D3D10CreateDevice = reinterpret_cast<PFN_DXGID3D10CreateDevice>(
            KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10CreateDevice"));
        D3D10CreateLayeredDevice = reinterpret_cast<PFN_DXGID3D10CreateLayeredDevice>(
            KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10CreateLayeredDevice"));
        D3D10GetLayeredDeviceSize = reinterpret_cast<PFN_DXGID3D10GetLayeredDeviceSize>(
            KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10GetLayeredDeviceSize"));
        D3D10RegisterLayers = reinterpret_cast<PFN_DXGID3D10RegisterLayers>(
            KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10RegisterLayers"));
        D3D10ETWRundown = reinterpret_cast<PFN_DXGID3D10ETWRundown>(
            KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10ETWRundown"));
        DumpJournal =
            reinterpret_cast<PFN_DXGIDumpJournal>(KernelBaseProxy::GetProcAddress_()(module, "DXGIDumpJournal"));
        ReportAdapterConfiguration = reinterpret_cast<PFN_DXGIReportAdapterConfiguration>(
            KernelBaseProxy::GetProcAddress_()(module, "DXGIReportAdapterConfiguration"));
        PIXBeginCapture =
            reinterpret_cast<PFN_PIXBeginCapture>(KernelBaseProxy::GetProcAddress_()(module, "PIXBeginCapture"));
        PIXEndCapture =
            reinterpret_cast<PFN_PIXEndCapture>(KernelBaseProxy::GetProcAddress_()(module, "PIXEndCapture"));
        PIXGetCaptureState =
            reinterpret_cast<PFN_PIXGetCaptureState>(KernelBaseProxy::GetProcAddress_()(module, "PIXGetCaptureState"));
        SetAppCompatStringPointer = reinterpret_cast<PFN_SetAppCompatStringPointer>(
            KernelBaseProxy::GetProcAddress_()(module, "SetAppCompatStringPointer"));
        UpdateHMDEmulationStatus = reinterpret_cast<PFN_UpdateHMDEmulationStatus>(
            KernelBaseProxy::GetProcAddress_()(module, "UpdateHMDEmulationStatus"));
        DisableVBlankVirtualization = reinterpret_cast<PFN_DXGIDisableVBlankVirtualization>(
            KernelBaseProxy::GetProcAddress_()(module, "DXGIDisableVBlankVirtualization"));
    }
} dxgi;

HRESULT WINAPI _CreateDXGIFactory(REFIID riid, IDXGIFactory** ppFactory)
{
    return DxgiProxy::CreateDxgiFactory_()(riid, ppFactory);
}

HRESULT WINAPI _CreateDXGIFactory1(REFIID riid, IDXGIFactory1** ppFactory)
{
    return DxgiProxy::CreateDxgiFactory1_()(riid, ppFactory);
}

HRESULT WINAPI _CreateDXGIFactory2(UINT flags, REFIID riid, IDXGIFactory2** ppFactory)
{
    return DxgiProxy::CreateDxgiFactory2_Hooked()(flags, riid, ppFactory);
}

HRESULT WINAPI _DXGIDeclareAdapterRemovalSupport() { return DxgiProxy::DeclareAdepterRemovalSupport_()(); }

HRESULT WINAPI _DXGIGetDebugInterface1(UINT flags, REFIID riid, void** pDebug)
{
    return DxgiProxy::GetDebugInterface_()(flags, riid, pDebug);
}

HRESULT WINAPI _DXGIDisableVBlankVirtualization()
{
    LOG_FUNC();

    if (dxgi.DisableVBlankVirtualization == nullptr)
        return E_NOTIMPL;

    return dxgi.DisableVBlankVirtualization();
}

HRESULT WINAPI _ApplyCompatResolutionQuirking(void* p1, void* p2)
{
    LOG_FUNC();

    if (dxgi.ApplyCompatResolutionQuirking == nullptr)
        return E_NOTIMPL;

    return dxgi.ApplyCompatResolutionQuirking(p1, p2);
}

BOOL WINAPI _CompatString(LPCSTR szName, ULONG* pSize, LPSTR lpData, bool flag)
{
    LOG_FUNC();

    if (dxgi.CompatString == nullptr)
        return FALSE;

    return dxgi.CompatString(szName, pSize, lpData, flag);
}

BOOL WINAPI _CompatValue(LPCSTR szName, UINT64* pValue)
{
    LOG_FUNC();

    if (dxgi.CompatValue == nullptr)
        return FALSE;

    return dxgi.CompatValue(szName, pValue);
}

HRESULT WINAPI _DXGID3D10CreateDevice(HMODULE hModule, IDXGIFactory* pFactory, IDXGIAdapter* pAdapter, UINT flags,
                                      void* pUnknown, void** ppDevice)
{
    LOG_FUNC();

    if (dxgi.D3D10CreateDevice == nullptr)
        return E_NOTIMPL;

    return dxgi.D3D10CreateDevice(hModule, pFactory, pAdapter, flags, pUnknown, ppDevice);
}

HRESULT WINAPI _DXGID3D10CreateLayeredDevice(IDXGIAdapter* pAdapter, UINT flags, void* pUnknown, REFIID riid,
                                             void** ppDevice)
{
    LOG_FUNC();

    if (dxgi.D3D10CreateLayeredDevice == nullptr)
        return E_NOTIMPL;

    return dxgi.D3D10CreateLayeredDevice(pAdapter, flags, pUnknown, riid, ppDevice);
}

SIZE_T WINAPI _DXGID3D10GetLayeredDeviceSize(const void* pLayers, UINT numLayers)
{
    LOG_FUNC();

    if (dxgi.D3D10GetLayeredDeviceSize == nullptr)
        return 0;

    return dxgi.D3D10GetLayeredDeviceSize(pLayers, numLayers);
}

HRESULT WINAPI _DXGID3D10RegisterLayers(const void* pLayers, UINT numLayers)
{
    LOG_FUNC();

    if (dxgi.D3D10RegisterLayers == nullptr)
        return E_NOTIMPL;

    return dxgi.D3D10RegisterLayers(pLayers, numLayers);
}

void WINAPI _DXGID3D10ETWRundown()
{
    LOG_FUNC();

    if (dxgi.D3D10ETWRundown != nullptr)
        dxgi.D3D10ETWRundown();
}

HRESULT WINAPI _DXGIDumpJournal(ULONG_PTR p1)
{
    LOG_FUNC();

    if (dxgi.DumpJournal == nullptr)
        return E_NOTIMPL;

    return dxgi.DumpJournal(p1);
}

HRESULT WINAPI _DXGIReportAdapterConfiguration(ULONG_PTR p1)
{
    LOG_FUNC();

    if (dxgi.ReportAdapterConfiguration == nullptr)
        return E_NOTIMPL;

    return dxgi.ReportAdapterConfiguration(p1);
}

HRESULT WINAPI _PIXBeginCapture(DWORD captureFlags, const void* captureParameters)
{
    LOG_FUNC();

    if (dxgi.PIXBeginCapture == nullptr)
        return E_NOTIMPL;

    return dxgi.PIXBeginCapture(captureFlags, captureParameters);
}

HRESULT WINAPI _PIXEndCapture(BOOL discard)
{
    LOG_FUNC();

    if (dxgi.PIXEndCapture == nullptr)
        return E_NOTIMPL;

    return dxgi.PIXEndCapture(discard);
}

DWORD WINAPI _PIXGetCaptureState()
{
    LOG_FUNC();

    if (dxgi.PIXGetCaptureState == nullptr)
        return 0;

    return dxgi.PIXGetCaptureState();
}

HRESULT WINAPI _SetAppCompatStringPointer(ULONG_PTR p1, ULONG_PTR p2)
{
    LOG_FUNC();

    if (dxgi.SetAppCompatStringPointer == nullptr)
        return E_NOTIMPL;

    return dxgi.SetAppCompatStringPointer(p1, p2);
}

HRESULT WINAPI _UpdateHMDEmulationStatus(ULONG_PTR p1)
{
    LOG_FUNC();

    if (dxgi.UpdateHMDEmulationStatus == nullptr)
        return E_NOTIMPL;

    return dxgi.UpdateHMDEmulationStatus(p1);
}
