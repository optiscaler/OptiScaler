#include "Shader_Dx12.h"
#include <d3dx/d3dx12.h>

Shader_Dx12::Shader_Dx12(std::string InName, ID3D12Device* InDevice) : _name(InName), _device(InDevice) {}

DXGI_FORMAT Shader_Dx12::TranslateTypelessFormats(DXGI_FORMAT format)
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

bool Shader_Dx12::CreateComputeShader(ID3D12Device* device, ID3D12RootSignature* rootSignature,
                                      ID3D12PipelineState** pipelineState, ID3DBlob* shaderBlob)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    psoDesc.CS = CD3DX12_SHADER_BYTECODE(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

    HRESULT hr = device->CreateComputePipelineState(&psoDesc, __uuidof(ID3D12PipelineState*), (void**) pipelineState);

    if (FAILED(hr))
    {
        LOG_ERROR("CreateComputePipelineState error {0:x}", hr);
        return false;
    }

    return true;
}

bool Shader_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InResource,
                                       D3D12_RESOURCE_STATES InState, ID3D12Resource** OutResource,
                                       D3D12_RESOURCE_FLAGS ResourceFlags, uint64_t InWidth, uint32_t InHeight,
                                       DXGI_FORMAT InFormat)
{
    if (InDevice == nullptr || InResource == nullptr)
        return false;

    auto inDesc = InResource->GetDesc();

    if (InWidth != 0 && InHeight != 0)
    {
        inDesc.Width = InWidth;
        inDesc.Height = InHeight;
    }

    if (*OutResource != nullptr)
    {
        auto bufDesc = (*OutResource)->GetDesc();

        if (bufDesc.Width != inDesc.Width || bufDesc.Height != inDesc.Height || bufDesc.Format != inDesc.Format)
        {
            (*OutResource)->Release();
            (*OutResource) = nullptr;
            LOG_WARN("Release {}x{}, new one: {}x{}", bufDesc.Width, bufDesc.Height, inDesc.Width, inDesc.Height);
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

    inDesc.Flags |= ResourceFlags;
    if (InFormat != DXGI_FORMAT_UNKNOWN)
        inDesc.Format = InFormat;

    hr = InDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &inDesc, InState, nullptr,
                                           IID_PPV_ARGS(OutResource));

    if (hr != S_OK)
    {
        LOG_ERROR("CreateCommittedResource result: {:X}", (UINT64) hr);
        return false;
    }

    LOG_DEBUG("Created new one: {}x{}", inDesc.Width, inDesc.Height);
    return true;
}

void Shader_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState,
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