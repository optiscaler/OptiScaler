#include <pch.h>

#include "with_dx12.h"

#include <proxies/DXGI_Proxy.h>
#include <proxies/D3D12_Proxy.h>

HRESULT CreateD3D12DeviceOnAdapter(IDXGIAdapter* adapter, D3D_FEATURE_LEVEL featureLevel, ID3D12Device** device)
{
    if (device == nullptr)
        return E_POINTER;

    *device = nullptr;

    if (D3d12Proxy::Module() == nullptr)
        return D3D12CreateDevice(adapter, featureLevel, IID_PPV_ARGS(device));

    return D3d12Proxy::D3D12CreateDevice_()(adapter, featureLevel, IID_PPV_ARGS(device));
}

HRESULT TestD3D12DeviceOnAdapter(IDXGIAdapter* adapter, D3D_FEATURE_LEVEL featureLevel)
{
    if (D3d12Proxy::Module() == nullptr)
        return D3D12CreateDevice(adapter, featureLevel, _uuidof(ID3D12Device), nullptr);

    return D3d12Proxy::D3D12CreateDevice_()(adapter, featureLevel, _uuidof(ID3D12Device), nullptr);
}

void WithDx12::UpdateStateObjects()
{
    State::Instance().currentD3D12Device = _d3d12Device;
    State::Instance().currentCommandQueue = _d3d12CommandQueue;
}

void WithDx12::SetD3D12Objects(ID3D12Device* InDevice, ID3D12CommandQueue* InCommandQueue,
                               D3D12_COMMAND_LIST_TYPE InCommandListType)
{
    _d3d12Device = InDevice;
    _d3d12CommandQueue = InCommandQueue;
    _commandListType = InCommandListType;

    UpdateStateObjects();

    LOG_DEBUG("Using D3D12 objects, device: {:X}, queue: {:X}", (size_t) _d3d12Device, (size_t) _d3d12CommandQueue);
}

bool WithDx12::PrepareD3D12ForD3D11(ID3D11Device* InDx11Device, D3D_FEATURE_LEVEL InFeatureLevel)
{
    if (IsInited())
    {
        UpdateStateObjects();
        return true;
    }

    if (State::Instance().currentCommandQueue != nullptr)
    {
        ID3D12Device* queueDevice = nullptr;
        auto deviceResult = State::Instance().currentCommandQueue->GetDevice(IID_PPV_ARGS(&queueDevice));

        if (SUCCEEDED(deviceResult) && queueDevice != nullptr)
        {
            SetD3D12Objects(queueDevice, State::Instance().currentCommandQueue, D3D12_COMMAND_LIST_TYPE_DIRECT);
            return IsInited();
        }

        LOG_WARN("EnsureD3D12ForDx11: current command queue GetDevice failed: {:X}", (UINT) deviceResult);
    }

    if (State::Instance().currentD3D12Device != nullptr)
    {
        auto queue = CreateD3D12CommandQueue(State::Instance().currentD3D12Device);
        if (queue != nullptr)
        {
            SetD3D12Objects(State::Instance().currentD3D12Device, queue, D3D12_COMMAND_LIST_TYPE_DIRECT);
            return IsInited();
        }
    }

    ID3D12Device* dx12Device = nullptr;
    ID3D12CommandQueue* dx12CommandQueue = nullptr;
    if (CreateD3D12DeviceAndQueueFromD3D11(InDx11Device, InFeatureLevel, &dx12Device, &dx12CommandQueue))
        return IsInited();

    return CreateDx12Device(InFeatureLevel);
}

void WithDx12::GetHardwareAdapter(IDXGIFactory1* InFactory, IDXGIAdapter** InAdapter, D3D_FEATURE_LEVEL InFeatureLevel,
                                  bool InRequestHighPerformanceAdapter)
{
    LOG_FUNC();

    *InAdapter = nullptr;

    IDXGIAdapter1* adapter = nullptr;

    IDXGIFactory6* factory6 = nullptr;
    if (SUCCEEDED(InFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        LOG_DEBUG("Using IDXGIFactory6 & EnumAdapterByGpuPreference");

        for (UINT adapterIndex = 0;
             DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(adapterIndex,
                                                                          InRequestHighPerformanceAdapter == true
                                                                              ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                                                                              : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                                                                          IID_PPV_ARGS(&adapter));
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                adapter->Release();
                adapter = nullptr;
                continue;
            }

            *InAdapter = adapter;
            break;
        }

        factory6->Release();
    }
    else
    {
        LOG_DEBUG("Using InFactory & EnumAdapters1");
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != InFactory->EnumAdapters1(adapterIndex, &adapter);
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                adapter->Release();
                adapter = nullptr;
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the actual device yet.
            auto result = TestD3D12DeviceOnAdapter(adapter, InFeatureLevel);

            if (result == S_FALSE)
            {
                LOG_DEBUG("D3D12CreateDevice test result: {:X}", (UINT) result);
                *InAdapter = adapter;
                break;
            }

            adapter->Release();
            adapter = nullptr;
        }
    }
}

ID3D12Device* WithDx12::RequestD3D12Device(D3D_FEATURE_LEVEL InFeatureLevel, IDXGIAdapter* adapter)
{
    if (CreateDx12Device(InFeatureLevel, adapter))
        return _d3d12Device;

    return nullptr;
}

ID3D12Device* WithDx12::RequestD3D12Device(D3D_FEATURE_LEVEL InFeatureLevel)
{
    return RequestD3D12Device(InFeatureLevel, nullptr);
}

ID3D12Device* WithDx12::GetD3D12Device() { return _d3d12Device; }

ID3D12CommandQueue* WithDx12::GetD3D12CommandQueue() { return _d3d12CommandQueue; }

D3D12_COMMAND_LIST_TYPE WithDx12::GetD3D12CommandListType() { return _commandListType; }

ID3D12Device* WithDx12::GetD3D12DeviceFromD3D11(ID3D11Device* dx11Device, D3D_FEATURE_LEVEL featureLevel)
{
    if (dx11Device == nullptr)
        return nullptr;

    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
    ScopedSkipVulkanHooks skipVulkanHooks {};

    IDXGIDevice* dxgiDevice = nullptr;
    auto result = dx11Device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(result) || dxgiDevice == nullptr)
    {
        LOG_ERROR("QueryInterface IDXGIDevice failed: {:X}", (UINT) result);
        return nullptr;
    }

    IDXGIAdapter* adapter = nullptr;
    result = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();

    if (FAILED(result) || adapter == nullptr)
    {
        LOG_ERROR("GetAdapter failed: {:X}", (UINT) result);
        return nullptr;
    }

    ID3D12Device* dx12Device = nullptr;
    result = CreateD3D12DeviceOnAdapter(adapter, featureLevel, &dx12Device);

    if (FAILED(result) || dx12Device == nullptr)
    {
        LOG_ERROR("D3D12CreateDevice failed: {:X}", (UINT) result);
        adapter->Release();
        return nullptr;
    }

    DXGI_ADAPTER_DESC desc = {};
    if (SUCCEEDED(adapter->GetDesc(&desc)))
        LOG_INFO("D3D12 interop device created on D3D11 adapter: {}", wstring_to_string(desc.Description));

    adapter->Release();
    return dx12Device;
}

ID3D12CommandQueue* WithDx12::CreateD3D12CommandQueue(ID3D12Device* dx12Device)
{
    if (dx12Device == nullptr)
        return nullptr;

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = _commandListType;

    ID3D12CommandQueue* commandQueue = nullptr;
    auto result = dx12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    if (FAILED(result) || commandQueue == nullptr)
    {
        LOG_ERROR("CreateD3D12CommandQueue failed: {:X}", (UINT) result);
        return nullptr;
    }

    return commandQueue;
}

bool WithDx12::CreateD3D12DeviceAndQueueFromD3D11(ID3D11Device* dx11Device, D3D_FEATURE_LEVEL featureLevel,
                                                  ID3D12Device** dx12Device, ID3D12CommandQueue** dx12CommandQueue)
{
    if (dx12Device == nullptr || dx12CommandQueue == nullptr)
        return false;

    *dx12Device = nullptr;
    *dx12CommandQueue = nullptr;

    ID3D12Device* localDevice = GetD3D12DeviceFromD3D11(dx11Device, featureLevel);
    if (localDevice == nullptr)
        return false;

    ID3D12CommandQueue* localQueue = CreateD3D12CommandQueue(localDevice);
    if (localQueue == nullptr)
    {
        localDevice->Release();
        return false;
    }

    _d3d12Device = localDevice;
    _d3d12CommandQueue = localQueue;
    _commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;

    *dx12Device = localDevice;
    *dx12CommandQueue = localQueue;

    UpdateStateObjects();

    return true;
}

bool WithDx12::CreateDx12Device(D3D_FEATURE_LEVEL InFeatureLevel, IDXGIAdapter* InAdapter)
{
    LOG_FUNC();

    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
    ScopedSkipVulkanHooks skipVulkanHooks {};

    HRESULT result = S_OK;

    const bool forceCreate = static_cast<bool>(State::Instance().gameQuirks & GameQuirk::ForceCreateD3D12Device);

    if (_d3d12Device == nullptr && State::Instance().currentD3D12Device != nullptr && !forceCreate)
    {
        _d3d12Device = State::Instance().currentD3D12Device;
        LOG_DEBUG("Using captured currentD3D12Device as WithDx12 device");
    }

    if (_d3d12Device == nullptr || forceCreate)
    {
        IDXGIAdapter* hwAdapter = InAdapter;
        if (hwAdapter == nullptr)
        {
            IDXGIFactory2* factory = nullptr;

            if (DxgiProxy::Module() == nullptr)
                result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
            else
                result = DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory), &factory);

            if (FAILED(result) || factory == nullptr)
            {
                LOG_ERROR("Can't create factory: {0:x}", result);
                return false;
            }

            GetHardwareAdapter(factory, &hwAdapter, InFeatureLevel, true);

            if (hwAdapter == nullptr)
                LOG_WARN("Can't get hwAdapter, will try nullptr!");

            factory->Release();
        }

        ID3D12Device* newDevice = nullptr;
        result = CreateD3D12DeviceOnAdapter(hwAdapter, InFeatureLevel, &newDevice);

        if (FAILED(result) || newDevice == nullptr)
        {
            LOG_ERROR("Can't create device: {:X}", (UINT) result);

            if (InAdapter == nullptr && hwAdapter != nullptr)
            {
                hwAdapter->Release();
                hwAdapter = nullptr;
            }

            return false;
        }

        SAFE_RELEASE(_d3d12CommandQueue);
        SAFE_RELEASE(_d3d12Device);
        _d3d12Device = newDevice;

        if (hwAdapter != nullptr)
        {
            ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
            DXGI_ADAPTER_DESC desc = {};
            if (hwAdapter->GetDesc(&desc) == S_OK)
            {
                auto adapterDesc = wstring_to_string(desc.Description);
                LOG_INFO("D3D12Device created with adapter: {}", adapterDesc);
            }
        }

        if (InAdapter == nullptr && hwAdapter != nullptr)
        {
            hwAdapter->Release();
            hwAdapter = nullptr;
        }
    }

    if (_d3d12CommandQueue == nullptr)
    {
        _d3d12CommandQueue = CreateD3D12CommandQueue(_d3d12Device);

        if (_d3d12CommandQueue == nullptr)
            return false;
    }

    UpdateStateObjects();

    return true;
}
