#include "pch.h"
#include "CC_Dx12.h"

#include "CC_Common.h"
#include "precompile/color_clamp_Shader.h"

bool ColorClamp_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource,
                                           D3D12_RESOURCE_STATES InState)
{
    auto oldBuffer = _buffer;
    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer,
                                                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 0, 0,
                                                    DXGI_FORMAT_R16G16B16A16_FLOAT);

    if (result && _buffer != oldBuffer)
    {
        _buffer->SetName(L"ColorClamp_Buffer");
        _bufferState = InState;
    }

    return result;
}

void ColorClamp_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool ColorClamp_Dx12::NeedsClamp(DXGI_FORMAT InFormat)
{
    switch (TranslateTypelessFormats(InFormat))
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return true;

    default:
        return false;
    }
}

bool ColorClamp_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InResource == nullptr || _buffer == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    ScopedGpuTime_Dx12 scopedGpuTime(GpuTime.get(), InCmdList);

    _counter++;
    _counter = _counter % CC_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    CreateShaderResourceView(_device, InResource, currentHeap.GetSrvCPU(0));
    CreateUnorderedAccessView(_device, _buffer, currentHeap.GetUavCPU(0), 0);

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    auto bufferDesc = _buffer->GetDesc();
    auto dispatchWidth = static_cast<UINT>((bufferDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    auto dispatchHeight = (bufferDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

ColorClamp_Dx12::ColorClamp_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    if (!SetupRootSignature(InDevice, 1, 1, 0))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    if (!CreateComputePipeline(InDevice, &_pipelineState, color_clamp_cso, sizeof(color_clamp_cso),
                               colorClampShader.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline", _name);
        return;
    }

    _init = InitHeaps(InDevice, _frameHeaps, CC_NUM_OF_HEAPS);
}

ColorClamp_Dx12::~ColorClamp_Dx12()
{
    if (State::Instance().isShuttingDown)
        return;

    for (int i = 0; i < CC_NUM_OF_HEAPS; i++)
        _frameHeaps[i].ReleaseHeaps();

    DeferredRelease::Release(_buffer);
}
