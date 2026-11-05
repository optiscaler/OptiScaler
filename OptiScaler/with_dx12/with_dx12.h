#pragma once

#include <d3d12.h>
#include <d3d11.h>
#include <dxgi.h>

HRESULT CreateD3D12DeviceOnAdapter(IDXGIAdapter* adapter, D3D_FEATURE_LEVEL featureLevel, ID3D12Device** device);

class WithDx12
{
  private:
    inline static D3D12_COMMAND_LIST_TYPE _commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;

    inline static ID3D12Device* _d3d12Device = nullptr;
    inline static ID3D12CommandQueue* _d3d12CommandQueue = nullptr;

    inline static bool CreateDx12Device(D3D_FEATURE_LEVEL InFeatureLevel, IDXGIAdapter* InAdapter = nullptr);
    inline static void GetHardwareAdapter(IDXGIFactory1* InFactory, IDXGIAdapter** InAdapter,
                                          D3D_FEATURE_LEVEL InFeatureLevel, bool InRequestHighPerformanceAdapter);

    inline static void UpdateStateObjects();

    inline static ID3D12CommandQueue* CreateD3D12CommandQueue(ID3D12Device* dx12Device);

    inline static bool CreateD3D12DeviceAndQueueFromD3D11(ID3D11Device* dx11Device, D3D_FEATURE_LEVEL featureLevel,
                                                          ID3D12Device** dx12Device,
                                                          ID3D12CommandQueue** dx12CommandQueue);

    static ID3D12Device* GetD3D12DeviceFromD3D11(ID3D11Device* dx11Device, D3D_FEATURE_LEVEL featureLevel);

  public:
    static bool IsInited() { return _d3d12Device != nullptr && _d3d12CommandQueue != nullptr; }

    static ID3D12Device* RequestD3D12Device(D3D_FEATURE_LEVEL InFeatureLevel);
    static ID3D12Device* RequestD3D12Device(D3D_FEATURE_LEVEL InFeatureLevel, IDXGIAdapter* adapter = nullptr);

    static bool PrepareD3D12ForD3D11(ID3D11Device* InDx11Device,
                                     D3D_FEATURE_LEVEL InFeatureLevel = D3D_FEATURE_LEVEL_11_0);

    static void SetD3D12Objects(ID3D12Device* InDevice, ID3D12CommandQueue* InCommandQueue,
                                D3D12_COMMAND_LIST_TYPE InCommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT);

    static ID3D12Device* GetD3D12Device();
    static ID3D12CommandQueue* GetD3D12CommandQueue();
    static D3D12_COMMAND_LIST_TYPE GetD3D12CommandListType();
};
