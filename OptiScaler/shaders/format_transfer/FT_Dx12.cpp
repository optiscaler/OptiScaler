#include "FT_Dx12.h"

#include "FT_Common.h"

#include "precompile/R10G10B10A2_Shader.h"
#include "precompile/R8G8B8A8_Shader.h"
#include "precompile/B8R8G8A8_Shader.h"

#include <Config.h>

bool FT_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState)
{
    if (InSource != nullptr)
        LOG_INFO("Texture Format: {}", (UINT) InSource->GetDesc().Format);

    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto result = ShaderDx12Utils::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags);

    if (result)
    {
        _buffer->SetName(L"FT_Buffer");
        _bufferState = InState;
    }

    return result;
}

void FT_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return ShaderDx12Utils::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool FT_Dx12::Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                       ID3D12Resource* OutResource)
{
    if (!_init || InDevice == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % FT_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    auto inDesc = InResource->GetDesc();
    auto outDesc = OutResource->GetDesc();

    // Create SRV for Input Texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = ShaderDx12Utils::TranslateTypelessFormats(inDesc.Format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    InDevice->CreateShaderResourceView(InResource, &srvDesc, currentHeap.GetSrvCPU(0));

    // Create UAV for Output Texture
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_UINT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    InDevice->CreateUnorderedAccessView(OutResource, nullptr, &uavDesc, currentHeap.GetUavCPU(0));

    ID3D12DescriptorHeap* heaps[] = { currentHeap.Heap };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = 0;
    UINT dispatchHeight = 0;

    dispatchWidth = static_cast<UINT>((bufferWidth + InNumThreadsX - 1) / InNumThreadsX);
    dispatchHeight = (bufferHeight + InNumThreadsY - 1) / InNumThreadsY;

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

FT_Dx12::FT_Dx12(std::string InName, ID3D12Device* InDevice, DXGI_FORMAT InFormat)
    : _name(InName), _device(InDevice), format(InFormat)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    // Describe and create the root signature
    // ---------------------------------------------------
    D3D12_DESCRIPTOR_RANGE descriptorRanges[2];

    // SRV Range (Input Texture)
    descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRanges[0].NumDescriptors = 1;
    descriptorRanges[0].BaseShaderRegister = 0; // Assuming t0 register in HLSL for SRV
    descriptorRanges[0].RegisterSpace = 0;
    descriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // UAV Range (Output Texture)
    descriptorRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptorRanges[1].NumDescriptors = 1;
    descriptorRanges[1].BaseShaderRegister = 0; // Assuming u0 register in HLSL for UAV
    descriptorRanges[1].RegisterSpace = 0;
    descriptorRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Define ONE root parameter (descriptor table)
    D3D12_ROOT_PARAMETER rootParameter = {};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = std::size(descriptorRanges);
    rootParameter.DescriptorTable.pDescriptorRanges = descriptorRanges;
    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.NumParameters = 1;
    rootSigDesc.pParameters = &rootParameter;
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

    // Compile shader blobs
    ID3DBlob* _recEncodeShader = nullptr;

    if (Config::Instance()->UsePrecompiledShaders.value_or_default())
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
        computePsoDesc.pRootSignature = _rootSignature;
        computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        if (InFormat == DXGI_FORMAT_R10G10B10A2_UNORM || InFormat == DXGI_FORMAT_R10G10B10A2_TYPELESS ||
            InFormat == DXGI_FORMAT_R16G16B16A16_FLOAT || InFormat == DXGI_FORMAT_R16G16B16A16_TYPELESS ||
            InFormat == DXGI_FORMAT_R11G11B10_FLOAT || InFormat == DXGI_FORMAT_R32G32B32A32_FLOAT ||
            InFormat == DXGI_FORMAT_R32G32B32A32_TYPELESS || InFormat == DXGI_FORMAT_R32G32B32_FLOAT ||
            InFormat == DXGI_FORMAT_R32G32B32_TYPELESS)
        {
            computePsoDesc.CS =
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(r10g10b10a2_cso), sizeof(r10g10b10a2_cso));
        }
        else if (InFormat == DXGI_FORMAT_R8G8B8A8_TYPELESS || InFormat == DXGI_FORMAT_R8G8B8A8_UNORM ||
                 InFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        {
            computePsoDesc.CS =
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(r8g8b8a8_cso), sizeof(r8g8b8a8_cso));
        }
        else if (InFormat == DXGI_FORMAT_B8G8R8A8_TYPELESS || InFormat == DXGI_FORMAT_B8G8R8A8_UNORM ||
                 InFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        {
            computePsoDesc.CS =
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(b8r8g8a8_cso), sizeof(b8r8g8a8_cso));
        }
        else
        {
            LOG_ERROR("[{0}] texture format is not found!", _name);
            return;
        }

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
        if (InFormat == DXGI_FORMAT_R10G10B10A2_UNORM)
        {
            _recEncodeShader = FT_CompileShader(ftR10G10B10A2Code.c_str(), "CSMain", "cs_5_0");
        }
        else if (InFormat == DXGI_FORMAT_R8G8B8A8_UNORM || InFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        {
            _recEncodeShader = FT_CompileShader(ftR8G8B8A8Code.c_str(), "CSMain", "cs_5_0");
        }
        else if (InFormat == DXGI_FORMAT_B8G8R8A8_UNORM)
        {
            _recEncodeShader = FT_CompileShader(ftB8G8R8A8Code.c_str(), "CSMain", "cs_5_0");
        }
        else
        {
            LOG_ERROR("[{0}] texture format is not found!", _name);
            return;
        }

        if (_recEncodeShader == nullptr)
        {
            LOG_ERROR("[{0}] CompileShader error!", _name);
            return;
        }

        // create pso objects
        if (!ShaderDx12Utils::CreateComputeShader(InDevice, _rootSignature, &_pipelineState, _recEncodeShader))
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

    for (int i = 0; i < FT_NUM_OF_HEAPS; ++i)
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

bool FT_Dx12::IsFormatCompatible(DXGI_FORMAT InFormat)
{
    if (format == DXGI_FORMAT_R10G10B10A2_UNORM || format == DXGI_FORMAT_R10G10B10A2_TYPELESS)
    {
        return InFormat == DXGI_FORMAT_R10G10B10A2_UNORM || InFormat == DXGI_FORMAT_R10G10B10A2_TYPELESS;
    }
    else if (format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
             format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
    {
        return InFormat == DXGI_FORMAT_R8G8B8A8_UNORM || InFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
               InFormat == DXGI_FORMAT_R8G8B8A8_TYPELESS;
    }
    else if (format == DXGI_FORMAT_B8G8R8A8_UNORM)
    {
        return InFormat == DXGI_FORMAT_B8G8R8A8_UNORM;
    }

    return false;
}

FT_Dx12::~FT_Dx12()
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

    for (int i = 0; i < FT_NUM_OF_HEAPS; ++i)
    {
        if (_frameHeaps[i].Heap != nullptr)
        {
            _frameHeaps[i].Heap->Release();
            _frameHeaps[i].Heap = nullptr;
        }
    }

    if (_buffer != nullptr)
    {
        _buffer->Release();
        _buffer = nullptr;
    }
}
