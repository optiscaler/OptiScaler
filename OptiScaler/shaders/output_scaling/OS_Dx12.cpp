#include "OS_Dx12.h"

#include "OS_Common.h"

#define A_CPU
// FSR compute shader is from : https://github.com/fholger/vrperfkit/

#include "precompile/BCDS_lanczos_Shader.h"
#include "precompile/BCDS_catmull_Shader.h"
#include "precompile/BCDS_bicubic_Shader.h"
#include "precompile/BCDS_magc_Shader.h"

#include "precompile/BCUS_Shader.h"

#include "fsr1/ffx_fsr1.h"
#include "fsr1/FSR_EASU_Shader.h"

#include <Config.h>

#pragma warning(disable : 4244)

bool OS_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, uint32_t InWidth,
                                   uint32_t InHeight, D3D12_RESOURCE_STATES InState)
{
    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                         D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    auto result =
        ShaderDx12Utils::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags, InWidth, InHeight);

    if (result)
    {
        _buffer->SetName(L"OS_Buffer");
        _bufferState = InState;
    }

    return result;
}

void OS_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return ShaderDx12Utils::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool OS_Dx12::Dispatch(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                       ID3D12Resource* OutResource)
{
    if (!_init || InDevice == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % OS_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    auto inDesc = InResource->GetDesc();
    auto outDesc = OutResource->GetDesc();

    // Create SRV for Input Texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = ShaderDx12Utils::TranslateTypelessFormats(inDesc.Format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    InDevice->CreateShaderResourceView(InResource, &srvDesc, currentHeap.GetSrvCPU(0));

    // Create UAV for Output Texture
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = ShaderDx12Utils::TranslateTypelessFormats(outDesc.Format);
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    InDevice->CreateUnorderedAccessView(OutResource, nullptr, &uavDesc, currentHeap.GetUavCPU(0));

    // Create CBV for Constants
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};

    // fsr upscaling
    if (Config::Instance()->OutputScalingUseFsr.value_or_default())
    {
        UpscaleShaderConstants constants {};

        FsrEasuCon(constants.const0, constants.const1, constants.const2, constants.const3,
                   State::Instance().currentFeature->TargetWidth(), State::Instance().currentFeature->TargetHeight(),
                   inDesc.Width, inDesc.Height, State::Instance().currentFeature->DisplayWidth(),
                   State::Instance().currentFeature->DisplayHeight());

        // Copy the updated constant buffer data to the constant buffer resource
        UINT8* pCBDataBegin;
        CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU
        _constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCBDataBegin));
        memcpy(pCBDataBegin, &constants, sizeof(constants));
        _constantBuffer->Unmap(0, nullptr);

        cbvDesc.BufferLocation = _constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = sizeof(constants);
    }
    else
    {
        Constants constants {};
        constants.srcWidth = State::Instance().currentFeature->TargetWidth();
        constants.srcHeight = State::Instance().currentFeature->TargetHeight();
        constants.destWidth = State::Instance().currentFeature->DisplayWidth();
        constants.destHeight = State::Instance().currentFeature->DisplayHeight();

        // Copy the updated constant buffer data to the constant buffer resource
        UINT8* pCBDataBegin;
        CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU
        _constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCBDataBegin));
        memcpy(pCBDataBegin, &constants, sizeof(constants));
        _constantBuffer->Unmap(0, nullptr);

        cbvDesc.BufferLocation = _constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = sizeof(constants);
    }

    InDevice->CreateConstantBufferView(&cbvDesc, currentHeap.GetCbvCPU(0));

    ID3D12DescriptorHeap* heaps[] = { currentHeap.Heap };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = 0;
    UINT dispatchHeight = 0;

    dispatchWidth =
        static_cast<UINT>((State::Instance().currentFeature->DisplayWidth() + InNumThreadsX - 1) / InNumThreadsX);
    dispatchHeight = (State::Instance().currentFeature->DisplayHeight() + InNumThreadsY - 1) / InNumThreadsY;

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

OS_Dx12::OS_Dx12(std::string InName, ID3D12Device* InDevice, bool InUpsample)
    : _name(InName), _device(InDevice), _upsample(InUpsample)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    // Describe and create the root signature
    // ---------------------------------------------------
    D3D12_DESCRIPTOR_RANGE descriptorRanges[3];

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

    // CBV Range (Params)
    descriptorRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    descriptorRanges[2].NumDescriptors = 1;
    descriptorRanges[2].BaseShaderRegister = 0; // Assuming b0 register in HLSL for CBV
    descriptorRanges[2].RegisterSpace = 0;
    descriptorRanges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

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

    CD3DX12_STATIC_SAMPLER_DESC samplers[1];

    // fsr upscaling
    if (Config::Instance()->OutputScalingUseFsr.value_or_default())
    {
        samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
        samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].ShaderRegister = 0;

        rootSigDesc.NumStaticSamplers = 1;
        rootSigDesc.pStaticSamplers = samplers;
    }
    else
    {
        rootSigDesc.NumStaticSamplers = 0;
        rootSigDesc.pStaticSamplers = nullptr;
    }

    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(Constants));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                      nullptr, IID_PPV_ARGS(&_constantBuffer));

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

    // don't wanna compile fsr easu on runtime :)
    if (Config::Instance()->UsePrecompiledShaders.value_or_default() ||
        Config::Instance()->OutputScalingUseFsr.value_or_default())
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
        computePsoDesc.pRootSignature = _rootSignature;
        computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        // fsr upscaling
        if (Config::Instance()->OutputScalingUseFsr.value_or_default())
        {
            computePsoDesc.CS =
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(fsr_easu_cso), sizeof(fsr_easu_cso));
        }
        else
        {
            if (_upsample)
            {
                computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(BCUS_cso), sizeof(BCUS_cso));
            }
            else
            {
                switch (Config::Instance()->OutputScalingDownscaler.value_or_default())
                {
                case 0:
                    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_bicubic_cso),
                                                                sizeof(bcds_bicubic_cso));
                    break;

                case 1:
                    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_lanczos_cso),
                                                                sizeof(bcds_lanczos_cso));
                    break;

                case 2:
                    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_catmull_cso),
                                                                sizeof(bcds_catmull_cso));
                    break;

                case 3:
                    computePsoDesc.CS =
                        CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_magc_cso), sizeof(bcds_magc_cso));
                    break;

                default:
                    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_bicubic_cso),
                                                                sizeof(bcds_bicubic_cso));
                    break;
                }
            }
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
        // Compile shader blobs
        ID3DBlob* _recEncodeShader = nullptr;

        if (_upsample)
        {
            _recEncodeShader = OS_CompileShader(upsampleCode.c_str(), "CSMain", "cs_5_0");
        }
        else
        {
            switch (Config::Instance()->OutputScalingDownscaler.value_or_default())
            {
            case 0:
                _recEncodeShader = OS_CompileShader(downsampleCodeBC.c_str(), "CSMain", "cs_5_0");
                break;

            case 1:
                _recEncodeShader = OS_CompileShader(downsampleCodeLanczos.c_str(), "CSMain", "cs_5_0");
                break;

            case 2:
                _recEncodeShader = OS_CompileShader(downsampleCodeCatmull.c_str(), "CSMain", "cs_5_0");
                break;

            case 3:
                _recEncodeShader = OS_CompileShader(downsampleCodeMAGIC.c_str(), "CSMain", "cs_5_0");
                break;

            default:
                _recEncodeShader = OS_CompileShader(downsampleCodeBC.c_str(), "CSMain", "cs_5_0");
                break;
            }
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

    for (int i = 0; i < OS_NUM_OF_HEAPS; ++i)
    {
        if (!_frameHeaps[i].Initialize(InDevice, 1, 1, 1))
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

OS_Dx12::~OS_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    // ID3D12Fence* d3d12Fence = nullptr;

    // do
    //{
    //     if (_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&d3d12Fence)) != S_OK)
    //         break;

    //    d3d12Fence->Signal(999);

    //    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    //    if (fenceEvent != NULL && d3d12Fence->SetEventOnCompletion(999, fenceEvent) == S_OK)
    //    {
    //        WaitForSingleObject(fenceEvent, INFINITE);
    //        CloseHandle(fenceEvent);
    //    }

    //} while (false);

    // if (d3d12Fence != nullptr)
    //{
    //     d3d12Fence->Release();
    //     d3d12Fence = nullptr;
    // }

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

    for (int i = 0; i < OS_NUM_OF_HEAPS; ++i)
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

    if (_constantBuffer != nullptr)
    {
        _constantBuffer->Release();
        _constantBuffer = nullptr;
    }
}
