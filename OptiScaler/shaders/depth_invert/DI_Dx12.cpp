#include "DI_Dx12.h"

#include "DI_Common.h"

#include <Config.h>
#include <State.h>
#include "precompiled/DI_Shader.h"

static bool CreateComputeShader(ID3D12Device* device, ID3D12RootSignature* rootSignature,
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

bool DI_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, uint64_t InWidth,
                                   uint32_t InHeight, D3D12_RESOURCE_STATES InState)
{
    if (InDevice == nullptr || InSource == nullptr)
        return false;

    LOG_FUNC();

    D3D12_RESOURCE_DESC texDesc = InSource->GetDesc();

    if (_buffer != nullptr)
    {
        auto bufDesc = _buffer->GetDesc();

        if (bufDesc.Width != InWidth || bufDesc.Height != InHeight)
        {
            _buffer->Release();
            _buffer = nullptr;
        }
        else
            return true;
    }

    LOG_DEBUG("[{0}] Start!", _name);

    LOG_INFO("Texture Format: {}", (UINT) texDesc.Format);

    D3D12_HEAP_PROPERTIES heapProperties;
    D3D12_HEAP_FLAGS heapFlags;
    HRESULT hr = InSource->GetHeapProperties(&heapProperties, &heapFlags);

    if (hr != S_OK)
    {
        LOG_ERROR("[{0}] GetHeapProperties result: {1:x}", _name.c_str(), hr);
        return false;
    }

    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                    D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    texDesc.Width = InWidth;
    texDesc.Height = InHeight;

    hr = InDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &texDesc, InState, nullptr,
                                           IID_PPV_ARGS(&_buffer));

    if (hr != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource result: {1:x}", _name, hr);
        return false;
    }

    _buffer->SetName(L"Upscaled_Depth_Buffer");
    _bufferState = InState;

    return true;
}

void DI_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    if (_bufferState == InState)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = _buffer;
    barrier.Transition.StateBefore = _bufferState;
    barrier.Transition.StateAfter = InState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    InCommandList->ResourceBarrier(1, &barrier);

    _bufferState = InState;
}

bool DI_Dx12::Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                       ID3D12Resource* OutResource)
{
    if (!_init || InDevice == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % 2;

    if (_cpuSrvHandle[_counter].ptr == NULL)
        _cpuSrvHandle[_counter] = _srvHeap[_counter]->GetCPUDescriptorHandleForHeapStart();

    if (_cpuUavHandle[_counter].ptr == NULL)
    {
        _cpuUavHandle[_counter] = _cpuSrvHandle[_counter];
        _cpuUavHandle[_counter].ptr +=
            InDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    if (_gpuSrvHandle[_counter].ptr == NULL)
        _gpuSrvHandle[_counter] = _srvHeap[_counter]->GetGPUDescriptorHandleForHeapStart();

    if (_gpuUavHandle[_counter].ptr == NULL)
    {
        _gpuUavHandle[_counter] = _gpuSrvHandle[_counter];
        _gpuUavHandle[_counter].ptr +=
            InDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    auto inDesc = InResource->GetDesc();
    auto outDesc = OutResource->GetDesc();

    // Create SRV for Input Texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = ShaderDx12Utils::TranslateTypelessFormats(inDesc.Format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    InDevice->CreateShaderResourceView(InResource, &srvDesc, _cpuSrvHandle[_counter]);

    // Create UAV for Output Texture
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    InDevice->CreateUnorderedAccessView(OutResource, nullptr, &uavDesc, _cpuUavHandle[_counter]);

    ID3D12DescriptorHeap* heaps[] = { _srvHeap[_counter] };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    InCmdList->SetComputeRootDescriptorTable(0, _gpuSrvHandle[_counter]);
    InCmdList->SetComputeRootDescriptorTable(1, _gpuUavHandle[_counter]);

    UINT dispatchWidth = 0;
    UINT dispatchHeight = 0;

    dispatchWidth = static_cast<UINT>((outDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    dispatchHeight = (outDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

DI_Dx12::DI_Dx12(std::string InName, ID3D12Device* InDevice) : _name(InName), _device(InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    // Describe and create the root signature
    // ---------------------------------------------------
    D3D12_DESCRIPTOR_RANGE descriptorRange[2];

    // SRV Range (Input Texture)
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].BaseShaderRegister = 0; // Assuming t0 register in HLSL for SRV
    descriptorRange[0].RegisterSpace = 0;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // UAV Range (Output Texture)
    descriptorRange[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptorRange[1].NumDescriptors = 1;
    descriptorRange[1].BaseShaderRegister = 0; // Assuming u0 register in HLSL for UAV
    descriptorRange[1].RegisterSpace = 0;
    descriptorRange[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Define the root parameter (descriptor table)
    // ---------------------------------------------------
    D3D12_ROOT_PARAMETER rootParameters[2];

    // Root Parameter for SRV
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;                 // One range (SRV)
    rootParameters[0].DescriptorTable.pDescriptorRanges = &descriptorRange[0]; // Point to the SRV range
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Root Parameter for UAV
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;                 // One range (UAV)
    rootParameters[1].DescriptorTable.pDescriptorRanges = &descriptorRange[1]; // Point to the UAV range
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // A root signature is an array of root parameters
    // ---------------------------------------------------
    D3D12_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParameters;
    rootSigDesc.NumStaticSamplers = 0;
    rootSigDesc.pStaticSamplers = nullptr;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* errorBlob;
    ID3DBlob* signatureBlob;

    do
    {
        auto hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] D3D12SerializeRootSignature error {1:x}", _name, hr);
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
        if (!CreateComputeShader(InDevice, _rootSignature, &_pipelineState, _recEncodeShader))
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

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 3; // SRV + UAV + CBV
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    State::Instance().skipHeapCapture = true;

    auto hr = InDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_srvHeap[0]));

    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateDescriptorHeap[0] error {1:x}", _name, hr);
        return;
    }

    hr = InDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_srvHeap[1]));

    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateDescriptorHeap[1] error {1:x}", _name, hr);
        return;
    }

    hr = InDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_srvHeap[2]));

    State::Instance().skipHeapCapture = false;

    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateDescriptorHeap[2] error {1:x}", _name, hr);
        return;
    }

    _init = _srvHeap[2] != nullptr;
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

    if (_srvHeap[0] != nullptr)
    {
        _srvHeap[0]->Release();
        _srvHeap[0] = nullptr;
    }

    if (_srvHeap[1] != nullptr)
    {
        _srvHeap[1]->Release();
        _srvHeap[1] = nullptr;
    }

    if (_srvHeap[2] != nullptr)
    {
        _srvHeap[2]->Release();
        _srvHeap[2] = nullptr;
    }

    if (_buffer != nullptr)
    {
        _buffer->Release();
        _buffer = nullptr;
    }
}
