#include "pch.h"

#include "RCAS_Dx12.h"

#include "precompile/RCAS_Shader.h"
#include "precompile/da_das_sharpen_Shader.h"
#include "precompile/da_rcas_sharpen_Shader.h"

#include <Config.h>

bool RCAS_Dx12::DispatchRCAS(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                             ID3D12Resource* InMotionVectors, RcasConstants InConstants, ID3D12Resource* OutResource,
                             FrameDescriptorHeap& currentHeap)
{
    if (InMotionVectors == nullptr || _device == nullptr)
        return false;

    CreateShaderResourceView(_device, InResource, currentHeap.GetSrvCPU(0));
    CreateShaderResourceView(_device, InMotionVectors, currentHeap.GetSrvCPU(1));
    CreateUnorderedAccessView(_device, OutResource, currentHeap.GetUavCPU(0), 0);

    InternalConstants constants {};

    auto outDesc = OutResource->GetDesc();
    auto mvsDesc = InMotionVectors->GetDesc();

    constants.OutputWidth = (uint32_t) outDesc.Width;
    constants.OutputHeight = outDesc.Height;
    constants.MotionWidth = (uint32_t) mvsDesc.Width;
    constants.MotionHeight = mvsDesc.Height;

    FillMotionConstants(constants, InConstants);

    if (!CreateConstantsBuffer(_device, _constantBuffer, constants, currentHeap.GetCbvCPU(0)))
    {
        LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);
    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    auto inDesc = InResource->GetDesc();
    UINT dispatchWidth = static_cast<UINT>((inDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    UINT dispatchHeight = (inDesc.Height + InNumThreadsY - 1) / InNumThreadsY;
    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

bool RCAS_Dx12::DispatchDepthAdaptive(ID3D12PipelineState* pipelineState, ID3D12GraphicsCommandList* InCmdList,
                                      ID3D12Resource* InResource, ID3D12Resource* InMotionVectors,
                                      ID3D12Resource* InDepth, RcasConstants InConstants, ID3D12Resource* OutResource,
                                      FrameDescriptorHeap& currentHeap)
{
    if (InDepth == nullptr || pipelineState == nullptr || _device == nullptr)
        return false;

    CreateShaderResourceView(_device, InResource, currentHeap.GetSrvCPU(0));
    CreateShaderResourceView(_device, InMotionVectors, currentHeap.GetSrvCPU(1));
    CreateShaderResourceView(_device, InDepth, currentHeap.GetSrvCPU(2));
    CreateUnorderedAccessView(_device, OutResource, currentHeap.GetUavCPU(0), 0);

    InternalConstantsDA constants {};

    auto outDesc = OutResource->GetDesc();
    auto mvsDesc = InMotionVectors->GetDesc();
    auto depthDesc = InDepth->GetDesc();

    constants.OutputWidth = (uint32_t) outDesc.Width;
    constants.OutputHeight = outDesc.Height;
    constants.MotionWidth = (uint32_t) mvsDesc.Width;
    constants.MotionHeight = mvsDesc.Height;
    constants.DepthWidth = (uint32_t) depthDesc.Width;
    constants.DepthHeight = depthDesc.Height;

    FillMotionConstants(constants, InConstants);

    if (!CreateConstantsBuffer(_device, _constantBuffer, constants, currentHeap.GetCbvCPU(0)))
    {
        LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(pipelineState);
    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = static_cast<UINT>((constants.OutputWidth + InNumThreadsX - 1) / InNumThreadsX);
    UINT dispatchHeight = (constants.OutputHeight + InNumThreadsY - 1) / InNumThreadsY;
    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

bool RCAS_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState)
{
    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                         D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags);

    if (result)
    {
        _buffer->SetName(L"RCAS_DA_Buffer");
        _bufferState = InState;
    }

    return result;
}

void RCAS_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool RCAS_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                         ID3D12Resource* InMotionVectors, RcasConstants InConstants, ID3D12Resource* OutResource,
                         ID3D12Resource* InDepth)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr ||
        InMotionVectors == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    ScopedGpuTime_Dx12 scopedGpuTime(GpuTime.get(), InCmdList);

    _counter++;
    _counter = _counter % RCAS_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    auto sharpnessShader = Config::Instance()->SharpnessShader.value_or_default();

    if (sharpnessShader == SharpenShader::LocalContrastDepthAware)
    {
        return DispatchDepthAdaptive(_pipelineStateDASDA, InCmdList, InResource, InMotionVectors, InDepth, InConstants,
                                     OutResource, currentHeap);
    }
    else if (sharpnessShader == SharpenShader::DepthAware)
    {
        return DispatchDepthAdaptive(_pipelineStateDA, InCmdList, InResource, InMotionVectors, InDepth, InConstants,
                                     OutResource, currentHeap);
    }
    else if (sharpnessShader == SharpenShader::RCAS)
    {
        return DispatchRCAS(InCmdList, InResource, InMotionVectors, InConstants, OutResource, currentHeap);
    }
    else
    {
        return false;
    }
}

RCAS_Dx12::RCAS_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    if (!SetupRootSignature(InDevice, 3, 1, 1))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(InternalConstantsDA));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    auto result =
        InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&_constantBuffer));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) result);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_pipelineState, rcas_cso, sizeof(rcas_cso), rcasCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_pipelineStateDA, da_rcas_sharpen_cso, sizeof(da_rcas_sharpen_cso),
                               daRcasSharpenCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline DA", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_pipelineStateDASDA, da_das_sharpen_cso, sizeof(da_das_sharpen_cso),
                               dasDASharpenCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline DAS DA", _name);
        return;
    }

    _init = InitHeaps(InDevice, _frameHeaps, RCAS_NUM_OF_HEAPS);
}

RCAS_Dx12::~RCAS_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    SAFE_RELEASE(_pipelineStateDA);
    SAFE_RELEASE(_pipelineStateDASDA);

    for (int i = 0; i < RCAS_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    SAFE_RELEASE(_buffer);
}
