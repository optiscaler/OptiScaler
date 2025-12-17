#include "RCAS_Dx12.h"

#include "precompile/RCAS_Shader.h"

#include <Config.h>

bool RCAS_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState)
{
    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                         D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags);

    if (result)
    {
        _buffer->SetName(L"RCAS_Buffer");
        _bufferState = InState;
    }

    return result;
}

void RCAS_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool RCAS_Dx12::Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                         ID3D12Resource* InMotionVectors, RcasConstants InConstants, ID3D12Resource* OutResource)
{
    if (!_init || InDevice == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr ||
        InMotionVectors == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % RCAS_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    auto inDesc = InResource->GetDesc();
    auto mvDesc = InMotionVectors->GetDesc();
    auto outDesc = OutResource->GetDesc();

    // Create SRV for Input Texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = Shader_Dx12::TranslateTypelessFormats(inDesc.Format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    InDevice->CreateShaderResourceView(InResource, &srvDesc, currentHeap.GetSrvCPU(0));

    // Create SRV for Motion Texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2 = {};
    srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc2.Format = Shader_Dx12::TranslateTypelessFormats(mvDesc.Format);
    srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc2.Texture2D.MipLevels = 1;

    InDevice->CreateShaderResourceView(InMotionVectors, &srvDesc2, currentHeap.GetSrvCPU(1));

    // Create UAV for Output Texture
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = Shader_Dx12::TranslateTypelessFormats(outDesc.Format);
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    InDevice->CreateUnorderedAccessView(OutResource, nullptr, &uavDesc, currentHeap.GetUavCPU(0));

    InternalConstants constants {};

    if (Config::Instance()->ContrastEnabled.value_or_default())
        constants.Contrast = Config::Instance()->Contrast.value_or_default() * -1.0f;
    else
        constants.Contrast = -100.0f;

    constants.DisplayHeight = InConstants.DisplayHeight;
    constants.DisplayWidth = InConstants.DisplayWidth;
    constants.DynamicSharpenEnabled = Config::Instance()->MotionSharpnessEnabled.value_or_default() ? 1 : 0;
    constants.MotionSharpness = Config::Instance()->MotionSharpness.value_or_default();
    constants.MvScaleX = InConstants.MvScaleX;
    constants.MvScaleY = InConstants.MvScaleY;
    constants.Sharpness = InConstants.Sharpness;
    constants.Debug = Config::Instance()->MotionSharpnessDebug.value_or_default() ? 1 : 0;
    constants.Threshold = Config::Instance()->MotionThreshold.value_or_default();
    constants.ScaleLimit = Config::Instance()->MotionScaleLimit.value_or_default();
    constants.DisplaySizeMV = InConstants.DisplaySizeMV ? 1 : 0;

    if (InConstants.RenderWidth == 0 || InConstants.DisplayWidth == 0)
        constants.MotionTextureScale = 1.0f;
    else
        constants.MotionTextureScale = (float) InConstants.RenderWidth / (float) InConstants.DisplayWidth;

    // Copy the updated constant buffer data to the constant buffer resource
    BYTE* pCBDataBegin;
    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU
    auto result = _constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCBDataBegin));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] _constantBuffer->Map error {1:x}", _name, (unsigned int) result);
        return false;
    }

    if (pCBDataBegin == nullptr)
    {
        _constantBuffer->Unmap(0, nullptr);
        LOG_ERROR("[{0}] pCBDataBegin is null!", _name);
        return false;
    }

    memcpy(pCBDataBegin, &constants, sizeof(constants));
    _constantBuffer->Unmap(0, nullptr);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = _constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = sizeof(constants);
    InDevice->CreateConstantBufferView(&cbvDesc, currentHeap.GetCbvCPU(0));

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = 0;
    UINT dispatchHeight = 0;

    dispatchWidth = static_cast<UINT>((inDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    dispatchHeight = (inDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

RCAS_Dx12::RCAS_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
        // 2 SRVs starting at register t0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0),

        // 1 UAV starting at register u0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0),

        // 1 CBV starting at register b0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0)
    };

    CD3DX12_ROOT_PARAMETER1 rootParameter {};
    rootParameter.InitAsDescriptorTable(std::size(descriptorRanges), descriptorRanges);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(1, &rootParameter);

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(InternalConstants));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    auto result =
        InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&_constantBuffer));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) result);
        return;
    }

    ID3DBlob* errorBlob;
    ID3DBlob* signatureBlob;

    do
    {
        auto hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signatureBlob, &errorBlob);

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] D3D12SerializeVersionedRootSignature error {1:x}", _name, (unsigned int) hr);
            break;
        }

        hr = InDevice->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
                                           IID_PPV_ARGS(&_rootSignature));

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateRootSignature error {1:x}", _name, (unsigned int) hr);
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
        computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(rcas_cso), sizeof(rcas_cso));
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
        ID3DBlob* _recEncodeShader = RCAS_CompileShader(rcasCode.c_str(), "CSMain", "cs_5_0");

        if (_recEncodeShader == nullptr)
        {
            LOG_ERROR("[{0}] RCAS_CompileShader error!", _name);
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

    for (int i = 0; i < RCAS_NUM_OF_HEAPS; i++)
    {
        if (!_frameHeaps[i].Initialize(InDevice, 2, 1, 1))
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

RCAS_Dx12::~RCAS_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    if (_rootSignature != nullptr)
    {
        _rootSignature->Release();
        _rootSignature = nullptr;
    }

    if (_pipelineState != nullptr)
    {
        _pipelineState->Release();
        _pipelineState = nullptr;
    }

    for (int i = 0; i < RCAS_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    if (_buffer != nullptr)
    {
        _buffer->Release();
        _buffer = nullptr;
    }

    if (_constantBuffer != nullptr)
    {
        _constantBuffer->Release();
        _constantBuffer = nullptr;
    }
}
