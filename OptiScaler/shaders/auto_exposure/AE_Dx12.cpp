#include "pch.h"
#include "AE_Dx12.h"

#include "AE_Common.h"
#include "precompile/AE_Shader.h"

#include <Config.h>

using Microsoft::WRL::ComPtr;

static constexpr uint32_t AE_THREADS = 256;
static constexpr uint32_t AE_TILE = 32;

bool AE_Dx12::CreateRootSignature(ID3D12Device* InDevice)
{
    CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);

    // Root constants, so every dispatch has its own values without a constant buffer per frame
    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
    rootParameters[0].InitAsConstants(sizeof(InternalConstants) / sizeof(uint32_t), 0);
    rootParameters[1].InitAsDescriptorTable(_countof(ranges), ranges);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc {};
    rootSigDesc.Init_1_1(_countof(rootParameters), rootParameters);

    ComPtr<ID3DBlob> errorBlob;
    ComPtr<ID3DBlob> signatureBlob;

    auto hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signatureBlob, &errorBlob);

    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] D3D12SerializeVersionedRootSignature error {1:x}", _name, hr);
        return false;
    }

    hr = InDevice->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
                                       IID_PPV_ARGS(&_rootSignature));

    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateRootSignature error {1:x}", _name, hr);
        return false;
    }

    return true;
}

bool AE_Dx12::EnsureDataBuffer(uint32_t InElements)
{
    if (_data != nullptr && _dataElements >= InElements)
        return true;

    // Previous frames can still be using it
    DeferredRelease::Release(_data);
    _dataElements = 0;
    _historyValid = false;

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(InElements * sizeof(float), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto hr = _device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&_data));

    if (hr != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) hr);
        return false;
    }

    _data->SetName(L"AE_Data");
    _dataElements = InElements;

    return true;
}

bool AE_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InColor, uint32_t InRenderWidth,
                       uint32_t InRenderHeight, float InPreExposure, float InDeltaTimeSeconds, bool InReset)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InColor == nullptr || InRenderWidth == 0 ||
        InRenderHeight == 0)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    const uint32_t groupCountX = (InRenderWidth + AE_TILE - 1) / AE_TILE;
    const uint32_t groupCountY = (InRenderHeight + AE_TILE - 1) / AE_TILE;
    const uint32_t groupCount = groupCountX * groupCountY;

    if (!EnsureDataBuffer(groupCount + 1))
        return false;

    ScopedGpuTime_Dx12 scopedGpuTime(GpuTime.get(), InCmdList);

    _counter++;
    _counter = _counter % AE_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    CreateShaderResourceView(_device, InColor, currentHeap.GetSrvCPU(0));

    D3D12_UNORDERED_ACCESS_VIEW_DESC dataUav {};
    dataUav.Format = DXGI_FORMAT_UNKNOWN;
    dataUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    dataUav.Buffer.NumElements = _dataElements;
    dataUav.Buffer.StructureByteStride = sizeof(float);
    _device->CreateUnorderedAccessView(_data, nullptr, &dataUav, currentHeap.GetUavCPU(0));

    CreateUnorderedAccessView(_device, _exposure, currentHeap.GetUavCPU(1), 0);

    SetBufferState(InCmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, _exposure, &_exposureState);

    // Previous pass 1 may still be reading the sums
    auto dataBarrier = CD3DX12_RESOURCE_BARRIER::UAV(_data);
    InCmdList->ResourceBarrier(1, &dataBarrier);

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);
    InCmdList->SetComputeRootDescriptorTable(1, currentHeap.GetTableGPUStart());

    InternalConstants constants {};
    constants.RenderWidth = InRenderWidth;
    constants.RenderHeight = InRenderHeight;
    constants.GroupCountX = groupCountX;
    constants.GroupCount = groupCount;
    constants.DeltaTime = std::clamp(InDeltaTimeSeconds, 0.0f, 1.0f);
    constants.PreExposure = InPreExposure > 0.0f ? InPreExposure : 1.0f;
    // Without history the scene luma is taken as is instead of adapting from a stale value
    constants.Reset = (InReset || !_historyValid) ? 1 : 0;

    // Per tile log luma sums
    constants.PassIndex = 0;
    InCmdList->SetComputeRoot32BitConstants(0, sizeof(constants) / sizeof(uint32_t), &constants, 0);
    InCmdList->Dispatch(groupCountX, groupCountY, 1);

    InCmdList->ResourceBarrier(1, &dataBarrier);

    // Average, temporal adaptation and exposure
    constants.PassIndex = 1;
    InCmdList->SetComputeRoot32BitConstants(0, sizeof(constants) / sizeof(uint32_t), &constants, 0);
    InCmdList->Dispatch(1, 1, 1);

    SetBufferState(InCmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, _exposure, &_exposureState);

    _historyValid = true;

    return true;
}

AE_Dx12::AE_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    if (!CreateRootSignature(InDevice))
    {
        LOG_ERROR("Failed to create root signature");
        return;
    }

    if (!CreateComputePipeline(InDevice, &_pipelineState, AE_cso, sizeof(AE_cso), aeShader.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline", _name);
        return;
    }

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT, 1, 1, 1, 1, 1, 0,
                                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto hr = InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, _exposureState, nullptr,
                                                IID_PPV_ARGS(&_exposure));

    if (hr != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) hr);
        return;
    }

    _exposure->SetName(L"AE_Exposure");

    ScopedSkipHeapCapture skipHeapCapture {};

    for (int i = 0; i < AE_NUM_OF_HEAPS; i++)
    {
        if (!_frameHeaps[i].Initialize(InDevice, 1, 2, 0))
        {
            LOG_ERROR("[{0}] Failed to init heap", _name);
            return;
        }
    }

    _init = true;
}

AE_Dx12::~AE_Dx12()
{
    if (State::Instance().isShuttingDown)
        return;

    for (int i = 0; i < AE_NUM_OF_HEAPS; i++)
        _frameHeaps[i].ReleaseHeaps();

    DeferredRelease::Release(_data);
    DeferredRelease::Release(_exposure);
}
