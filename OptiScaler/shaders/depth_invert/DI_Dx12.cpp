#include "DI_Dx12.h"

#include "DI_Common.h"

#include <Config.h>
#include <State.h>
#include "precompiled/DI_Shader.h"

bool DI_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, uint64_t InWidth,
                                   uint32_t InHeight, D3D12_RESOURCE_STATES InState)
{
    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                         D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    auto resourceFormat = DXGI_FORMAT_R32_FLOAT;

    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags, InWidth,
                                                    InHeight, resourceFormat);

    if (result)
    {
        _buffer->SetName(L"DI_Buffer");
        _bufferState = InState;
    }

    return result;
}

void DI_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool DI_Dx12::Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                       ID3D12Resource* OutResource)
{
    if (!_init || InDevice == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % DI_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    auto inDesc = InResource->GetDesc();
    auto outDesc = OutResource->GetDesc();

    // Create SRV for Input Texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = Shader_Dx12::TranslateTypelessFormats(inDesc.Format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    InDevice->CreateShaderResourceView(InResource, &srvDesc, currentHeap.GetSrvCPU(0));

    // Create UAV for Output Texture
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    InDevice->CreateUnorderedAccessView(OutResource, nullptr, &uavDesc, currentHeap.GetUavCPU(0));

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = 0;
    UINT dispatchHeight = 0;

    dispatchWidth = static_cast<UINT>((outDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    dispatchHeight = (outDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

DI_Dx12::DI_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
        // 1 SRV starting at register t0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0),

        // 1 UAV starting at register u0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0),
    };

    CD3DX12_ROOT_PARAMETER1 rootParameter {};
    rootParameter.InitAsDescriptorTable(std::size(descriptorRanges), descriptorRanges);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(1, &rootParameter);

    ID3DBlob* errorBlob;
    ID3DBlob* signatureBlob;

    do
    {
        auto hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signatureBlob, &errorBlob);

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] D3D12SerializeVersionedRootSignature error {1:x}", _name, hr);
            break;
        }

        hr = InDevice->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
                                           IID_PPV_ARGS(&_rootSignature));

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateRootSignature error {1:x}", _name, hr);
            break;
        }

    } while (false);

    if (errorBlob != nullptr)
    {
        errorBlob->Release();
        errorBlob = nullptr;
    }

    if (signatureBlob != nullptr)
    {
        signatureBlob->Release();
        signatureBlob = nullptr;
    }

    if (_rootSignature == nullptr)
    {
        LOG_ERROR("[{0}] _rootSignature is null!", _name);
        return;
    }

    if (Config::Instance()->UsePrecompiledShaders.value_or_default())
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
        computePsoDesc.pRootSignature = _rootSignature;
        computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(DI_cso), sizeof(DI_cso));
        auto hr = InDevice->CreateComputePipelineState(&computePsoDesc, __uuidof(ID3D12PipelineState*),
                                                       (void**) &_pipelineState);

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateComputePipelineState error: {1:X}", _name, hr);
            return;
        }
    }
    else
    {
        // Compile shader blobs
        ID3DBlob* _recEncodeShader = nullptr;

        _recEncodeShader = DI_CompileShader(shaderCode.c_str(), "CSMain", "cs_5_0");

        if (_recEncodeShader == nullptr)
        {
            LOG_ERROR("[{0}] CompileShader error!", _name);
            return;
        }

        // create pso objects
        if (!Shader_Dx12::CreateComputeShader(InDevice, _rootSignature, &_pipelineState, _recEncodeShader))
        {
            LOG_ERROR("[{0}] CreateComputeShader error!", _name);
            return;
        }

        if (_recEncodeShader != nullptr)
        {
            _recEncodeShader->Release();
            _recEncodeShader = nullptr;
        }
    }

    State::Instance().skipHeapCapture = true;

    for (int i = 0; i < DI_NUM_OF_HEAPS; i++)
    {
        if (!_frameHeaps[i].Initialize(InDevice, 1, 1, 0))
        {
            LOG_ERROR("[{0}] Failed to init heap", _name);
            _init = false;
            State::Instance().skipHeapCapture = false;
            return;
        }
    }

    State::Instance().skipHeapCapture = false;

    _init = true;
}

DI_Dx12::~DI_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    if (_pipelineState != nullptr)
    {
        _pipelineState->Release();
        _pipelineState = nullptr;
    }

    if (_rootSignature != nullptr)
    {
        _rootSignature->Release();
        _rootSignature = nullptr;
    }

    for (int i = 0; i < DI_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    if (_buffer != nullptr)
    {
        _buffer->Release();
        _buffer = nullptr;
    }
}
