#include "pch.h"
#include "DS_Dx12.h"

#include "DS_Common.h"

#include <Config.h>
#include <State.h>
#include "precompiled/DS_Shader.h"

bool DS_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, uint32_t InWidth,
                                   uint32_t InHeight, D3D12_RESOURCE_STATES InState)
{
    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                         D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    auto resourceFormat = DXGI_FORMAT_R32_FLOAT;

    auto oldBuffer = _buffer;
    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags, InWidth,
                                                    InHeight, resourceFormat);

    if (result && _buffer != oldBuffer)
    {
        _buffer->SetName(L"Upscaled_Depth_Buffer");
        _bufferState = InState;
    }

    return result;

    if (InDevice == nullptr || InSource == nullptr)
        return false;
}

void DS_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool DS_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource)
{
    if (State::Instance().currentFeature == nullptr)
        return false;

    UINT width = 0;
    UINT height = 0;

    if ((State::Instance().currentFeature->GetFeatureFlags() & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) == 0)
    {
        width = State::Instance().currentFeature->DisplayWidth();
        height = State::Instance().currentFeature->DisplayHeight();
    }
    else
    {
        width = State::Instance().currentFeature->RenderWidth();
        height = State::Instance().currentFeature->RenderHeight();
    }

    return Dispatch(InCmdList, InResource, OutResource, Config::Instance()->FGDepthScaleMax.value_or_default(), width,
                    height);
}

bool DS_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource,
                       float InDepthScale, UINT InWidth, UINT InHeight)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    ScopedGpuTime_Dx12 scopedGpuTime(GpuTime.get(), InCmdList);

    _counter++;
    _counter = _counter % DS_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    CreateShaderResourceView(_device, InResource, currentHeap.GetSrvCPU(0));
    CreateUnorderedAccessView(_device, OutResource, currentHeap.GetUavCPU(0), 0);

    DSConstants constants {};
    constants.DepthScale = InDepthScale;

    if (!CreateConstantsBuffer(_device, currentHeap, constants, currentHeap.GetCbvCPU(0)))
    {
        LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = (InWidth + InNumThreadsX - 1) / InNumThreadsX;
    UINT dispatchHeight = (InHeight + InNumThreadsY - 1) / InNumThreadsY;

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

DS_Dx12::DS_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    if (!SetupRootSignature(InDevice, 1, 1, 1))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    if (!CreateComputePipeline(InDevice, &_pipelineState, DS_cso, sizeof(DS_cso), shaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline", _name);
        return;
    }

    _init = InitHeaps(InDevice, _frameHeaps, DS_NUM_OF_HEAPS);
}

DS_Dx12::~DS_Dx12()
{
    if (State::Instance().isShuttingDown)
        return;

    for (int i = 0; i < DS_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    DeferredRelease::Release(_buffer);
}
