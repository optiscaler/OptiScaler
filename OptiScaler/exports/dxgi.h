#pragma once

#include "SysUtils.h"

#include <proxies/Dxgi_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

struct dxgi_dll
{
    HMODULE dll = nullptr;

    FARPROC ApplyCompatResolutionQuirking = nullptr;
    FARPROC CompatString = nullptr;
    FARPROC CompatValue = nullptr;
    FARPROC D3D10CreateDevice = nullptr;
    FARPROC D3D10CreateLayeredDevice = nullptr;
    FARPROC D3D10GetLayeredDeviceSize = nullptr;
    FARPROC D3D10RegisterLayers = nullptr;
    FARPROC D3D10ETWRundown = nullptr;
    FARPROC DumpJournal = nullptr;
    FARPROC ReportAdapterConfiguration = nullptr;
    FARPROC PIXBeginCapture = nullptr;
    FARPROC PIXEndCapture = nullptr;
    FARPROC PIXGetCaptureState = nullptr;
    FARPROC SetAppCompatStringPointer = nullptr;
    FARPROC UpdateHMDEmulationStatus = nullptr;
    FARPROC DisableVBlankVirtualization = nullptr;

    void LoadOriginalLibrary(HMODULE module)
    {
        dll = module;

        ApplyCompatResolutionQuirking = KernelBaseProxy::GetProcAddress_()(module, "ApplyCompatResolutionQuirking");
        CompatString = KernelBaseProxy::GetProcAddress_()(module, "CompatString");
        CompatValue = KernelBaseProxy::GetProcAddress_()(module, "CompatValue");
        D3D10CreateDevice = KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10CreateDevice");
        D3D10CreateLayeredDevice = KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10CreateLayeredDevice");
        D3D10GetLayeredDeviceSize = KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10GetLayeredDeviceSize");
        D3D10RegisterLayers = KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10RegisterLayers");
        D3D10ETWRundown = KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10ETWRundown");
        DumpJournal = KernelBaseProxy::GetProcAddress_()(module, "DXGIDumpJournal");
        ReportAdapterConfiguration = KernelBaseProxy::GetProcAddress_()(module, "DXGIReportAdapterConfiguration");
        PIXBeginCapture = KernelBaseProxy::GetProcAddress_()(module, "PIXBeginCapture");
        PIXEndCapture = KernelBaseProxy::GetProcAddress_()(module, "PIXEndCapture");
        PIXGetCaptureState = KernelBaseProxy::GetProcAddress_()(module, "PIXGetCaptureState");
        SetAppCompatStringPointer = KernelBaseProxy::GetProcAddress_()(module, "SetAppCompatStringPointer");
        UpdateHMDEmulationStatus = KernelBaseProxy::GetProcAddress_()(module, "UpdateHMDEmulationStatus");
        DisableVBlankVirtualization = KernelBaseProxy::GetProcAddress_()(module, "DXGIDisableVBlankVirtualization");
    }
} dxgi;

HRESULT _CreateDXGIFactory(REFIID riid, IDXGIFactory** ppFactory)
{
    return DxgiProxy::CreateDxgiFactory_()(riid, ppFactory);
}

HRESULT _CreateDXGIFactory1(REFIID riid, IDXGIFactory1** ppFactory)
{
    return DxgiProxy::CreateDxgiFactory1_()(riid, ppFactory);
}

HRESULT _CreateDXGIFactory2(UINT Flags, REFIID riid, IDXGIFactory2** ppFactory)
{
    return DxgiProxy::CreateDxgiFactory2_Hooked()(Flags, riid, ppFactory);
}

HRESULT _DXGIDeclareAdapterRemovalSupport() { return DxgiProxy::DeclareAdepterRemovalSupport_()(); }

HRESULT _DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug)
{
    return DxgiProxy::GetDebugInterface_()(Flags, riid, pDebug);
}

void _DXGIDisableVBlankVirtualization()
{
    LOG_FUNC();
    dxgi.DisableVBlankVirtualization();
}

void _ApplyCompatResolutionQuirking()
{
    LOG_FUNC();
    dxgi.ApplyCompatResolutionQuirking();
}

void _CompatString()
{
    LOG_FUNC();
    dxgi.CompatString();
}

void _CompatValue()
{
    LOG_FUNC();
    dxgi.CompatValue();
}

void _DXGID3D10CreateDevice()
{
    LOG_FUNC();
    dxgi.D3D10CreateDevice();
}

void _DXGID3D10CreateLayeredDevice()
{
    LOG_FUNC();
    dxgi.D3D10CreateLayeredDevice();
}

void _DXGID3D10GetLayeredDeviceSize()
{
    LOG_FUNC();
    dxgi.D3D10GetLayeredDeviceSize();
}

void _DXGID3D10RegisterLayers()
{
    LOG_FUNC();
    dxgi.D3D10RegisterLayers();
}

void _DXGID3D10ETWRundown()
{
    LOG_FUNC();
    dxgi.D3D10ETWRundown();
}

void _DXGIDumpJournal()
{
    LOG_FUNC();
    dxgi.DumpJournal();
}

void _DXGIReportAdapterConfiguration()
{
    LOG_FUNC();
    dxgi.ReportAdapterConfiguration();
}

void _PIXBeginCapture()
{
    LOG_FUNC();
    dxgi.PIXBeginCapture();
}

void _PIXEndCapture()
{
    LOG_FUNC();
    dxgi.PIXEndCapture();
}

void _PIXGetCaptureState()
{
    LOG_FUNC();
    dxgi.PIXGetCaptureState();
}

void _SetAppCompatStringPointer()
{
    LOG_FUNC();
    dxgi.SetAppCompatStringPointer();
}

void _UpdateHMDEmulationStatus()
{
    LOG_FUNC();
    dxgi.UpdateHMDEmulationStatus();
}
