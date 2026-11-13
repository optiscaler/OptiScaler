#include "pch.h"
#include "RC_Dx12.h"

#include "RC_Common.h"
#include "precompile/rc_offset_Shader.h"

bool RC_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, uint32_t InWidth,
                                   uint32_t InHeight, D3D12_RESOURCE_STATES InState, bool InGrow)
{
    if (InSource == nullptr)
        return false;

    auto format = RC_Formats::BufferFormat(InSource->GetDesc().Format, _depth);
    uint64_t width = InWidth;
    uint32_t height = InHeight;

    if (_buffer != nullptr && InGrow)
    {
        auto bufDesc = _buffer->GetDesc();

        if (bufDesc.Format == format)
        {
            width = std::max(width, bufDesc.Width);
            height = std::max(height, bufDesc.Height);
        }
    }

    auto oldBuffer = _buffer;
    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer,
                                                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, width, height, format);

    if (result && _buffer != oldBuffer)
    {
        _buffer->SetName(L"RC_Buffer");
        _bufferState = InState;
    }

    return result;
}

void RC_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool RC_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource,
                       uint32_t InSrcX, uint32_t InSrcY, uint32_t InDstX, uint32_t InDstY, uint32_t InWidth,
                       uint32_t InHeight)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % RC_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    CreateShaderResourceView(_device, InResource, currentHeap.GetSrvCPU(0),
                             RC_Formats::SrvFormat(InResource->GetDesc().Format, _depth));
    CreateUnorderedAccessView(_device, OutResource, currentHeap.GetUavCPU(0), 0);

    InternalConstants constants {};
    constants.SrcOffset[0] = InSrcX;
    constants.SrcOffset[1] = InSrcY;
    constants.DstOffset[0] = InDstX;
    constants.DstOffset[1] = InDstY;
    constants.Size[0] = InWidth;
    constants.Size[1] = InHeight;

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

RC_Dx12::RC_Dx12(std::string InName, ID3D12Device* InDevice, bool InDepth)
    : Shader_Dx12(InName, InDevice), _depth(InDepth)
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

    if (!CreateComputePipeline(InDevice, &_pipelineState, rc_offset_cso, sizeof(rc_offset_cso), nullptr))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline", _name);
        return;
    }

    _init = InitHeaps(InDevice, _frameHeaps, RC_NUM_OF_HEAPS);
}

RC_Dx12::~RC_Dx12()
{
    if (State::Instance().isShuttingDown)
        return;

    for (int i = 0; i < RC_NUM_OF_HEAPS; i++)
        _frameHeaps[i].ReleaseHeaps();

    DeferredRelease::Release(_buffer);
}
