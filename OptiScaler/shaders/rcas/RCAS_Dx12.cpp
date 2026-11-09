#include "pch.h"

#include "RCAS_Dx12.h"

#include "precompile/RCAS_Shader.h"
#include "precompile/da_das_sharpen_Shader.h"
#include "precompile/da_rcas_sharpen_Shader.h"

#include <Config.h>
#include <misc/DeferredRelease.h>

bool RCAS_Dx12::CreatePipelineState(ID3D12Device* InDevice, const void* InShaderData, size_t InShaderSize,
                                    ID3D12PipelineState** OutPipelineState)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = _rootSignature;
    computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(InShaderData, InShaderSize);
    auto hr = InDevice->CreateComputePipelineState(&computePsoDesc, __uuidof(ID3D12PipelineState*),
                                                   (void**) OutPipelineState);

    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateComputePipelineState error: {1:X}", _name, hr);
        return false;
    }

    return true;
}

bool RCAS_Dx12::CreatePipelineState(ID3D12Device* InDevice, const std::string& InShaderCode,
                                    ID3D12PipelineState** OutPipelineState, D3D12_SHADER_BYTECODE byteCode)
{
    ID3DBlob* shaderBlob = RCAS_CompileShader(InShaderCode.c_str(), "CSMain", "cs_5_0");

    auto result = Shader_Dx12::CreateComputeShader(InDevice, _rootSignature, OutPipelineState, shaderBlob, byteCode);

    if (shaderBlob != nullptr)
        shaderBlob->Release();

    return result;
}

void RCAS_Dx12::FillMotionConstants(InternalConstants& OutConstants, const RcasConstants& InConstants)
{
    if (Config::Instance()->ContrastEnabled.value_or_default())
        OutConstants.Contrast = Config::Instance()->Contrast.value_or_default();
    else
        OutConstants.Contrast = 0.0f;

    auto feature = State::Instance().currentFeature;

    OutConstants.Sharpness = InConstants.Sharpness;
    OutConstants.MvScaleX = InConstants.MvScaleX;
    OutConstants.MvScaleY = InConstants.MvScaleY;
    OutConstants.DisplaySizeMV = feature->LowResMV() ? 0 : 1;

    OutConstants.DisplayWidth = feature->TargetWidth();
    OutConstants.DisplayHeight = feature->TargetHeight();
    OutConstants.DynamicSharpenEnabled = Config::Instance()->MotionSharpnessEnabled.value_or_default() ? 1 : 0;
    OutConstants.MotionSharpness = Config::Instance()->MotionSharpness.value_or_default();
    OutConstants.Debug = Config::Instance()->MotionSharpnessDebug.value_or_default() ? 1 : 0;
    OutConstants.Threshold = Config::Instance()->MotionThreshold.value_or_default();
    OutConstants.ScaleLimit = Config::Instance()->MotionScaleLimit.value_or_default();

    if (feature->LowResMV())
    {
        OutConstants.MotionTextureScale = (float) feature->RenderWidth() / (float) feature->TargetWidth();
    }
    else
    {
        OutConstants.MotionTextureScale = 1.0f;
    }
}

void RCAS_Dx12::FillMotionConstants(InternalConstantsDA& OutConstants, const RcasConstants& InConstants)
{
    auto feature = State::Instance().currentFeature;

    OutConstants.Sharpness = InConstants.Sharpness * 2.0f;
    OutConstants.MvScaleX = InConstants.MvScaleX;
    OutConstants.MvScaleY = InConstants.MvScaleY;
    OutConstants.DisplaySizeMV = feature->LowResMV() ? 0 : 1;

    OutConstants.DynamicSharpenEnabled = Config::Instance()->MotionSharpnessEnabled.value_or_default() ? 1 : 0;
    OutConstants.Debug = Config::Instance()->MotionSharpnessDebug.value_or_default() ? 3 : 0;
    OutConstants.MotionSharpness = Config::Instance()->MotionSharpness.value_or_default();
    OutConstants.MotionThreshold = Config::Instance()->MotionThreshold.value_or_default();
    OutConstants.MotionScaleLimit = Config::Instance()->MotionScaleLimit.value_or_default();
    OutConstants.DisplayWidth = feature->TargetWidth();
    OutConstants.DisplayHeight = feature->TargetHeight();

    OutConstants.DepthIsLinear = Config::Instance()->DADepthIsLinear.value_or_default() ? 1 : 0;
    OutConstants.DepthIsReversed = feature->DepthInverted() ? 1 : 0;
    OutConstants.DepthScale =
        Config::Instance()->DADepthScale.value_or(OutConstants.DepthIsLinear == 0 ? 35.0f : 250.0f);
    OutConstants.DepthBias =
        Config::Instance()->DADepthBias.value_or(OutConstants.DepthIsLinear == 0 ? 0.001f : 0.0015f);

    OutConstants.DepthLinearA = InConstants.CameraNear * InConstants.CameraFar;
    OutConstants.DepthLinearB = InConstants.CameraFar;
    OutConstants.DepthLinearC = InConstants.CameraFar - InConstants.CameraNear;

    OutConstants.DepthTextureScale = (float) feature->RenderWidth() / (float) feature->TargetWidth();
    OutConstants.ClampOutput = Config::Instance()->DAClampOutput.value_or(feature->IsHdr()) ? 0 : 1;

    if (feature->LowResMV())
    {
        OutConstants.MotionWidth = feature->RenderWidth();
        OutConstants.MotionHeight = feature->RenderHeight();
        OutConstants.MotionTextureScale = (float) OutConstants.MotionWidth / (float) feature->TargetWidth();
    }
    else
    {
        OutConstants.MotionWidth = feature->TargetWidth();
        OutConstants.MotionHeight = feature->TargetHeight();
        OutConstants.MotionTextureScale = 1.0f;
    }

    OutConstants.DepthWidth = feature->RenderWidth();
    OutConstants.DepthHeight = feature->RenderHeight();
}

bool RCAS_Dx12::DispatchRCAS(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                             ID3D12Resource* InMotionVectors, RcasConstants InConstants, ID3D12Resource* OutResource,
                             FrameDescriptorHeap& currentHeap)
{
    auto inDesc = InResource->GetDesc();
    auto mvDesc = InMotionVectors->GetDesc();
    auto outDesc = OutResource->GetDesc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = Shader_Dx12::TranslateTypelessFormats(inDesc.Format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    InDevice->CreateShaderResourceView(InResource, &srvDesc, currentHeap.GetSrvCPU(0));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2 = {};
    srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc2.Format = Shader_Dx12::TranslateTypelessFormats(mvDesc.Format);
    srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc2.Texture2D.MipLevels = 1;
    InDevice->CreateShaderResourceView(InMotionVectors, &srvDesc2, currentHeap.GetSrvCPU(1));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = Shader_Dx12::TranslateTypelessFormats(outDesc.Format);
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    InDevice->CreateUnorderedAccessView(OutResource, nullptr, &uavDesc, currentHeap.GetUavCPU(0));

    InternalConstants constants {};
    FillMotionConstants(constants, InConstants);

    // The GPU can still read the previous heaps' constants
    auto constantBuffer = _constantBuffers[_counter];

    BYTE* pCBDataBegin;
    CD3DX12_RANGE readRange(0, 0);
    auto result = constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCBDataBegin));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] _constantBuffer->Map error {1:x}", _name, (unsigned int) result);

        if (result == DXGI_ERROR_DEVICE_REMOVED && _device != nullptr)
            Util::GetDeviceRemovedReason(_device);

        return false;
    }

    if (pCBDataBegin == nullptr)
    {
        constantBuffer->Unmap(0, nullptr);
        LOG_ERROR("[{0}] pCBDataBegin is null!", _name);
        return false;
    }

    memcpy(pCBDataBegin, &constants, sizeof(constants));
    constantBuffer->Unmap(0, nullptr);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = sizeof(constants);
    InDevice->CreateConstantBufferView(&cbvDesc, currentHeap.GetCbvCPU(0));

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);
    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = static_cast<UINT>((inDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    UINT dispatchHeight = (inDesc.Height + InNumThreadsY - 1) / InNumThreadsY;
    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

bool RCAS_Dx12::DispatchDepthAdaptive(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList,
                                      ID3D12Resource* InResource, ID3D12Resource* InMotionVectors,
                                      ID3D12Resource* InDepth, RcasConstants InConstants, ID3D12Resource* OutResource,
                                      FrameDescriptorHeap& currentHeap)
{
    if (InDepth == nullptr || _pipelineStateDA == nullptr)
        return false;

    auto inDesc = InResource->GetDesc();
    auto mvDesc = InMotionVectors->GetDesc();
    auto depthDesc = InDepth->GetDesc();
    auto outDesc = OutResource->GetDesc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = Shader_Dx12::TranslateTypelessFormats(inDesc.Format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    InDevice->CreateShaderResourceView(InResource, &srvDesc, currentHeap.GetSrvCPU(0));

    D3D12_SHADER_RESOURCE_VIEW_DESC mvSrvDesc = {};
    mvSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    mvSrvDesc.Format = Shader_Dx12::TranslateTypelessFormats(mvDesc.Format);
    mvSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    mvSrvDesc.Texture2D.MipLevels = 1;
    InDevice->CreateShaderResourceView(InMotionVectors, &mvSrvDesc, currentHeap.GetSrvCPU(1));

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Format = Shader_Dx12::TranslateTypelessFormats(depthDesc.Format);
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MipLevels = 1;
    InDevice->CreateShaderResourceView(InDepth, &depthSrvDesc, currentHeap.GetSrvCPU(2));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = Shader_Dx12::TranslateTypelessFormats(outDesc.Format);
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    InDevice->CreateUnorderedAccessView(OutResource, nullptr, &uavDesc, currentHeap.GetUavCPU(0));

    InternalConstantsDA constants {};
    FillMotionConstants(constants, InConstants);

    // The GPU can still read the previous heaps' constants
    auto constantBuffer = _constantBuffers[_counter];

    BYTE* pCBDataBegin;
    CD3DX12_RANGE readRange(0, 0);
    auto result = constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCBDataBegin));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] _constantBuffer->Map error {1:x}", _name, (unsigned int) result);

        if (result == DXGI_ERROR_DEVICE_REMOVED && _device != nullptr)
            Util::GetDeviceRemovedReason(_device);

        return false;
    }

    if (pCBDataBegin == nullptr)
    {
        constantBuffer->Unmap(0, nullptr);
        LOG_ERROR("[{0}] pCBDataBegin is null!", _name);
        return false;
    }

    memcpy(pCBDataBegin, &constants, sizeof(constants));
    constantBuffer->Unmap(0, nullptr);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = sizeof(constants);
    InDevice->CreateConstantBufferView(&cbvDesc, currentHeap.GetCbvCPU(0));

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineStateDA);
    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = static_cast<UINT>((constants.DisplayWidth + InNumThreadsX - 1) / InNumThreadsX);
    UINT dispatchHeight = (constants.DisplayHeight + InNumThreadsY - 1) / InNumThreadsY;
    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

bool RCAS_Dx12::DispatchDASDepthAdaptive(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList,
                                         ID3D12Resource* InResource, ID3D12Resource* InMotionVectors,
                                         ID3D12Resource* InDepth, RcasConstants InConstants,
                                         ID3D12Resource* OutResource, FrameDescriptorHeap& currentHeap)
{
    if (InDepth == nullptr || _pipelineStateLCDA == nullptr)
        return false;

    auto inDesc = InResource->GetDesc();
    auto mvDesc = InMotionVectors->GetDesc();
    auto depthDesc = InDepth->GetDesc();
    auto outDesc = OutResource->GetDesc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = Shader_Dx12::TranslateTypelessFormats(inDesc.Format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    InDevice->CreateShaderResourceView(InResource, &srvDesc, currentHeap.GetSrvCPU(0));

    D3D12_SHADER_RESOURCE_VIEW_DESC mvSrvDesc = {};
    mvSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    mvSrvDesc.Format = Shader_Dx12::TranslateTypelessFormats(mvDesc.Format);
    mvSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    mvSrvDesc.Texture2D.MipLevels = 1;
    InDevice->CreateShaderResourceView(InMotionVectors, &mvSrvDesc, currentHeap.GetSrvCPU(1));

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Format = Shader_Dx12::TranslateTypelessFormats(depthDesc.Format);
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MipLevels = 1;
    InDevice->CreateShaderResourceView(InDepth, &depthSrvDesc, currentHeap.GetSrvCPU(2));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = Shader_Dx12::TranslateTypelessFormats(outDesc.Format);
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    InDevice->CreateUnorderedAccessView(OutResource, nullptr, &uavDesc, currentHeap.GetUavCPU(0));

    InternalConstantsDA constants {};
    FillMotionConstants(constants, InConstants);

    // The GPU can still read the previous heaps' constants
    auto constantBuffer = _constantBuffers[_counter];

    BYTE* pCBDataBegin;
    CD3DX12_RANGE readRange(0, 0);
    auto result = constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCBDataBegin));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] _constantBuffer->Map error {1:x}", _name, (unsigned int) result);

        if (result == DXGI_ERROR_DEVICE_REMOVED && _device != nullptr)
            Util::GetDeviceRemovedReason(_device);

        return false;
    }

    if (pCBDataBegin == nullptr)
    {
        constantBuffer->Unmap(0, nullptr);
        LOG_ERROR("[{0}] pCBDataBegin is null!", _name);
        return false;
    }

    memcpy(pCBDataBegin, &constants, sizeof(constants));
    constantBuffer->Unmap(0, nullptr);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = sizeof(constants);
    InDevice->CreateConstantBufferView(&cbvDesc, currentHeap.GetCbvCPU(0));

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineStateLCDA);
    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = static_cast<UINT>((constants.DisplayWidth + InNumThreadsX - 1) / InNumThreadsX);
    UINT dispatchHeight = (constants.DisplayHeight + InNumThreadsY - 1) / InNumThreadsY;
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

bool RCAS_Dx12::Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                         ID3D12Resource* InMotionVectors, RcasConstants InConstants, ID3D12Resource* OutResource,
                         ID3D12Resource* InDepth)
{
    if (!_init || InDevice == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr ||
        InMotionVectors == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % RCAS_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    const bool useDepthAdaptive = Config::Instance()->UseDepthAwareSharpen.value_or_default() && InDepth != nullptr;
    const bool useDASDepthAdaptive =
        Config::Instance()->UseDASDepthAwareSharpen.value_or_default() && InDepth != nullptr;

    if (useDepthAdaptive)
        return DispatchDepthAdaptive(InDevice, InCmdList, InResource, InMotionVectors, InDepth, InConstants,
                                     OutResource, currentHeap);

    if (useDASDepthAdaptive)
        return DispatchDASDepthAdaptive(InDevice, InCmdList, InResource, InMotionVectors, InDepth, InConstants,
                                        OutResource, currentHeap);

    return DispatchRCAS(InDevice, InCmdList, InResource, InMotionVectors, InConstants, OutResource, currentHeap);
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
        // 3 SRVs starting at register t0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0),

        // 1 UAV starting at register u0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0),

        // 1 CBV starting at register b0, space 0
        CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0)
    };

    CD3DX12_ROOT_PARAMETER1 rootParameter {};
    rootParameter.InitAsDescriptorTable(std::size(descriptorRanges), descriptorRanges);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(1, &rootParameter);

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(InternalConstantsDA));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    for (int i = 0; i < RCAS_NUM_OF_HEAPS; i++)
    {
        auto result = InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(&_constantBuffers[i]));

        if (result != S_OK)
        {
            LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) result);
            return;
        }
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
        if (!CreatePipelineState(InDevice, reinterpret_cast<const void*>(rcas_cso), sizeof(rcas_cso), &_pipelineState))
            return;

        if (!CreatePipelineState(InDevice, reinterpret_cast<const void*>(da_rcas_sharpen_cso),
                                 sizeof(da_rcas_sharpen_cso), &_pipelineStateDA))
            return;

        if (!CreatePipelineState(InDevice, reinterpret_cast<const void*>(da_das_sharpen_cso),
                                 sizeof(da_das_sharpen_cso), &_pipelineStateLCDA))
            return;
    }
    else
    {
        if (!CreatePipelineState(InDevice, rcasCode, &_pipelineState,
                                 CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(rcas_cso), sizeof(rcas_cso))))
        {
            LOG_ERROR("[{0}] CreateComputeShader error!", _name);
            return;
        }

        if (!CreatePipelineState(InDevice, daRcasSharpenCode, &_pipelineStateDA,
                                 CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(da_rcas_sharpen_cso),
                                                         sizeof(da_rcas_sharpen_cso))))
        {
            LOG_ERROR("[{0}] CreateComputeShader error for depth aware shader!", _name);
            return;
        }

        if (!CreatePipelineState(
                InDevice, dasDASharpenCode, &_pipelineStateLCDA,
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(da_das_sharpen_cso), sizeof(da_das_sharpen_cso))))
        {
            LOG_ERROR("[{0}] CreateComputeShader error for DAS depth aware shader!", _name);
            return;
        }
    }

    ScopedSkipHeapCapture skipHeapCapture {};

    for (int i = 0; i < RCAS_NUM_OF_HEAPS; i++)
    {
        if (!_frameHeaps[i].Initialize(InDevice, 3, 1, 1))
        {
            LOG_ERROR("[{0}] Failed to init heap", _name);
            _init = false;
            return;
        }
    }

    _init = true;
}

RCAS_Dx12::~RCAS_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    // Command lists in flight can still reference these
    DeferredRelease::Release(_rootSignature);
    DeferredRelease::Release(_pipelineState);
    DeferredRelease::Release(_pipelineStateDA);
    DeferredRelease::Release(_pipelineStateLCDA);

    for (int i = 0; i < RCAS_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
        DeferredRelease::Release(_constantBuffers[i]);
    }

    DeferredRelease::Release(_buffer);
}
