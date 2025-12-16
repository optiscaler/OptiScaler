#pragma once

#include <d3dx/d3dx12.h>
#include <vector>
#include <stdexcept>

class FrameDescriptorHeap
{
    static inline CD3DX12_CPU_DESCRIPTOR_HANDLE getEmpty()
    {
        LOG_ERROR("Trying to get a handle outside the range");
        static CD3DX12_CPU_DESCRIPTOR_HANDLE empty {};
        return empty;
    }

  public:
    ID3D12DescriptorHeap* Heap = nullptr;
    UINT descriptorSize = 0;

    UINT totalDescriptors = 0;
    UINT srvOffset = 0;
    UINT uavOffset = 0;
    UINT cbvOffset = 0;

    // Initialize the heap based on counts
    bool Initialize(ID3D12Device* device, UINT numSrv, UINT numUav, UINT numCbv)
    {
        totalDescriptors = numSrv + numUav + numCbv;
        descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        srvOffset = 0;
        uavOffset = numSrv;
        cbvOffset = numSrv + numUav;

        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = totalDescriptors;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&Heap))))
            return false;

        return true;
    }

    // Get CPU Handle by specific index (e.g., SRV[0], SRV[1])
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetSrvCPU(UINT index)
    {
        if (srvOffset + index >= uavOffset)
            return getEmpty();

        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(Heap->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(srvOffset + index, descriptorSize);
        return handle;
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetUavCPU(UINT index)
    {
        if (uavOffset + index >= cbvOffset)
            return getEmpty();

        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(Heap->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(uavOffset + index, descriptorSize);
        return handle;
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetCbvCPU(UINT index)
    {
        if (cbvOffset + index >= totalDescriptors)
            return getEmpty();

        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(Heap->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(cbvOffset + index, descriptorSize);
        return handle;
    }

    // Get the GPU handle for the ENTIRE table (starts at SRV 0)
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetTableGPUStart()
    {
        return CD3DX12_GPU_DESCRIPTOR_HANDLE(Heap->GetGPUDescriptorHandleForHeapStart());
    }

    ~FrameDescriptorHeap()
    {
        if (Heap)
            Heap->Release();
    }
};

namespace ShaderDx12Utils
{
    static DXGI_FORMAT TranslateTypelessFormats(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R32G32B32_TYPELESS:
            return DXGI_FORMAT_R32G32B32_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            return DXGI_FORMAT_R10G10B10A2_UINT;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R16G16_TYPELESS:
            return DXGI_FORMAT_R16G16_FLOAT;
        case DXGI_FORMAT_R32G32_TYPELESS:
            return DXGI_FORMAT_R32G32_FLOAT;

        // Some shaders didn't have those conversions and I'm not 100% sure if it's fine to do for them
        case DXGI_FORMAT_R24G8_TYPELESS:
            return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_R32G8X24_TYPELESS:
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_R32_TYPELESS:
            return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
            return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        default:
            return format;
        }
    }

    static bool CreateComputeShader(ID3D12Device* device, ID3D12RootSignature* rootSignature,
                                    ID3D12PipelineState** pipelineState, ID3DBlob* shaderBlob)
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = rootSignature;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

        HRESULT hr =
            device->CreateComputePipelineState(&psoDesc, __uuidof(ID3D12PipelineState*), (void**) pipelineState);

        if (FAILED(hr))
        {
            LOG_ERROR("CreateComputePipelineState error {0:x}", hr);
            return false;
        }

        return true;
    }

    static bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InResource, D3D12_RESOURCE_STATES InState,
                                     ID3D12Resource** OutResource, D3D12_RESOURCE_FLAGS ResourceFlags,
                                     uint64_t InWidth = 0, uint32_t InHeight = 0)
    {
        if (InDevice == nullptr || InResource == nullptr)
            return false;

        auto inDesc = InResource->GetDesc();

        if (InWidth == 0 && InHeight == 0)
        {
            InWidth = inDesc.Width;
            InHeight = inDesc.Height;
        }

        if (*OutResource != nullptr)
        {
            auto bufDesc = (*OutResource)->GetDesc();

            if (bufDesc.Width != InWidth || bufDesc.Height != InHeight || bufDesc.Format != inDesc.Format)
            {
                (*OutResource)->Release();
                (*OutResource) = nullptr;
                LOG_WARN("Release {}x{}, new one: {}x{}", bufDesc.Width, bufDesc.Height, InWidth, InHeight);
            }
            else
            {
                return true;
            }
        }

        D3D12_HEAP_PROPERTIES heapProperties;
        D3D12_HEAP_FLAGS heapFlags;
        HRESULT hr = InResource->GetHeapProperties(&heapProperties, &heapFlags);

        if (hr != S_OK)
        {
            LOG_ERROR("GetHeapProperties result: {:X}", (UINT64) hr);
            return false;
        }

        inDesc.Flags = ResourceFlags;

        hr = InDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &inDesc, InState, nullptr,
                                               IID_PPV_ARGS(OutResource));

        if (hr != S_OK)
        {
            LOG_ERROR("CreateCommittedResource result: {:X}", (UINT64) hr);
            return false;
        }

        LOG_DEBUG("Created new one: {}x{}", InWidth, InHeight);
        return true;
    }

    static void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState,
                                 ID3D12Resource* Buffer, D3D12_RESOURCE_STATES* BufferState)
    {
        if (BufferState == nullptr || *BufferState == InState)
            return;

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = Buffer;
        barrier.Transition.StateBefore = *BufferState;
        barrier.Transition.StateAfter = InState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        InCommandList->ResourceBarrier(1, &barrier);

        *BufferState = InState;
    }
};