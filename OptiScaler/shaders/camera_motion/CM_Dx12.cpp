#include "pch.h"
#include "CM_Dx12.h"

#include <State.h>

#include "precompiled/CM_Shader.h"

static constexpr D3D12_RESOURCE_STATES ReadState =
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

// Inputs in a state compute shaders can't read are moved to one for the dispatch and back
static void Transition(std::vector<D3D12_RESOURCE_BARRIER>& barriers, ID3D12Resource* resource,
                       D3D12_RESOURCE_STATES state, bool toRead)
{
    if ((state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) != 0 || state == D3D12_RESOURCE_STATE_GENERIC_READ)
        return;

    barriers.push_back(
        CD3DX12_RESOURCE_BARRIER::Transition(resource, toRead ? state : ReadState, toRead ? ReadState : state));
}

ID3D12Resource* CM_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, int InIndex, ID3D12Resource* InVelocity,
                                  D3D12_RESOURCE_STATES InVelocityState, ID3D12Resource* InDepth,
                                  D3D12_RESOURCE_STATES InDepthState, const CMConstants& InConstants)
{
    if (!_init || InCmdList == nullptr || InVelocity == nullptr || InDepth == nullptr || InIndex < 0 ||
        InIndex >= BUFFER_COUNT)
    {
        return nullptr;
    }

    auto& buffer = _buffers[InIndex];
    auto previous = buffer;

    if (!CreateBufferResource(_device, InVelocity, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &buffer,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 0, 0, DXGI_FORMAT_R16G16_FLOAT))
    {
        LOG_ERROR("[{0}] Failed to create the output", _name);
        return nullptr;
    }

    if (buffer != previous)
        buffer->SetName(L"CameraMotion_Output");

    ScopedGpuTime_Dx12 scopedGpuTime(GpuTime.get(), InCmdList);

    FrameDescriptorHeap& currentHeap = _frameHeaps[InIndex];

    CreateShaderResourceView(_device, InVelocity, currentHeap.GetSrvCPU(0));
    CreateShaderResourceView(_device, InDepth, currentHeap.GetSrvCPU(1));
    CreateUnorderedAccessView(_device, buffer, currentHeap.GetUavCPU(0), 0);

    if (!CreateConstantsBuffer(_device, _constantBuffers[InIndex], InConstants, currentHeap.GetCbvCPU(0)))
    {
        LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
        return nullptr;
    }

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    Transition(barriers, InVelocity, InVelocityState, true);
    Transition(barriers, InDepth, InDepthState, true);

    if (!barriers.empty())
        InCmdList->ResourceBarrier((UINT) barriers.size(), barriers.data());

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    InCmdList->Dispatch((InConstants.Width + InNumThreadsX - 1) / InNumThreadsX,
                        (InConstants.Height + InNumThreadsY - 1) / InNumThreadsY, 1);

    barriers.clear();
    Transition(barriers, InVelocity, InVelocityState, false);
    Transition(barriers, InDepth, InDepthState, false);
    barriers.push_back(CD3DX12_RESOURCE_BARRIER::UAV(buffer));
    InCmdList->ResourceBarrier((UINT) barriers.size(), barriers.data());

    return buffer;
}

CM_Dx12::CM_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    if (!SetupRootSignature(InDevice, 2, 1, 1))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(CMConstants));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    for (auto& constantBuffer : _constantBuffers)
    {
        auto result = InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(&constantBuffer));

        if (result != S_OK)
        {
            LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) result);
            return;
        }
    }

    if (!CreateComputePipeline(InDevice, &_pipelineState, CM_cso, sizeof(CM_cso), cmCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline", _name);
        return;
    }

    _init = InitHeaps(InDevice, _frameHeaps, BUFFER_COUNT);
}

CM_Dx12::~CM_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    for (int i = 0; i < BUFFER_COUNT; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
        SAFE_RELEASE(_constantBuffers[i]);
        SAFE_RELEASE(_buffers[i]);
    }
}
