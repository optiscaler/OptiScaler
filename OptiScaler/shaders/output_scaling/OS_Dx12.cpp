#include "pch.h"
#include "OS_Dx12.h"

#include "OS_Common.h"

#define A_CPU
// FSR compute shader is from : https://github.com/fholger/vrperfkit/

#include "precompile/BCDS_bicubic_Shader.h"
#include "precompile/BCDS_catmull_Shader.h"
#include "precompile/BCDS_lanczos2_Shader.h"
#include "precompile/BCDS_lanczos3_Shader.h"
#include "precompile/BCDS_kaiser2_Shader.h"
#include "precompile/BCDS_kaiser3_Shader.h"
#include "precompile/BCDS_magc_Shader.h"

#include "precompile/BCUS_Shader.h"

#include "fsr1/ffx_fsr1.h"
#include "fsr1/FSR_EASU_Shader.h"

#include <Config.h>

static Constants constants {};
static UpscaleShaderConstants fsr1Constants {};

#pragma warning(disable : 4244)

bool OS_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, uint32_t InWidth,
                                   uint32_t InHeight, D3D12_RESOURCE_STATES InState)
{
    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                         D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    auto result =
        Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags, InWidth, InHeight);

    if (result)
    {
        _buffer->SetName(L"OS_Buffer");
        _bufferState = InState;
    }

    return result;
}

void OS_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool OS_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    ScopedGpuTime_Dx12 scopedGpuTime(GpuTime.get(), InCmdList);

    _counter++;
    _counter = _counter % OS_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    CreateShaderResourceView(_device, InResource, currentHeap.GetSrvCPU(0));
    CreateUnorderedAccessView(_device, OutResource, currentHeap.GetUavCPU(0), 0);

    FsrEasuCon(fsr1Constants.const0, fsr1Constants.const1, fsr1Constants.const2, fsr1Constants.const3,
               State::Instance().currentFeature->TargetWidth(), State::Instance().currentFeature->TargetHeight(),
               State::Instance().currentFeature->TargetWidth(), State::Instance().currentFeature->TargetHeight(),
               State::Instance().currentFeature->DisplayWidth(), State::Instance().currentFeature->DisplayHeight());

    constants.srcWidth = State::Instance().currentFeature->TargetWidth();
    constants.srcHeight = State::Instance().currentFeature->TargetHeight();
    constants.destWidth = State::Instance().currentFeature->DisplayWidth();
    constants.destHeight = State::Instance().currentFeature->DisplayHeight();

    // fsr upscaling
    bool createdConstantsBuffer = false;
    if (Config::Instance()->OutputScalingDownscaler.value_or_default() == Scaler::FSR1)
    {
        createdConstantsBuffer =
            CreateConstantsBuffer(_device, _constantBuffer, fsr1Constants, currentHeap.GetCbvCPU(0));
    }
    else
    {
        createdConstantsBuffer = CreateConstantsBuffer(_device, _constantBuffer, constants, currentHeap.GetCbvCPU(0));
    }

    if (!createdConstantsBuffer)
    {
        LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
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
    : Shader_Dx12(InName, InDevice), _upsample(InUpsample)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0);
    sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; // no sampler.AddressW ???

    if (!SetupRootSignature(InDevice, 1, 1, 1, 0, 0, 1, &sampler))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(Constants));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                      nullptr, IID_PPV_ARGS(&_constantBuffer));

    // don't wanna compile fsr easu on runtime :)
    if (Config::Instance()->UsePrecompiledShaders.value_or_default() ||
        Config::Instance()->OutputScalingDownscaler.value_or_default() == Scaler::FSR1)
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
        computePsoDesc.pRootSignature = _rootSignature;
        computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        // fsr upscaling
        if (Config::Instance()->OutputScalingDownscaler.value_or_default() == Scaler::FSR1)
        {
            computePsoDesc.CS =
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(fsr_easu_cso), sizeof(fsr_easu_cso));
        }
        else
        {
            if (_upsample)
            {
                computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcus_cso), sizeof(bcus_cso));
            }
            else
            {
                InNumThreadsY = 8;
                InNumThreadsX = 8;

                switch (Config::Instance()->OutputScalingDownscaler.value_or_default())
                {
                case Scaler::Bicubic:
                    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_bicubic_cso),
                                                                sizeof(bcds_bicubic_cso));

                    break;

                case Scaler::CatmullRom:
                    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_catmull_cso),
                                                                sizeof(bcds_catmull_cso));
                    break;

                case Scaler::Lanczos2:
                    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_lanczos2_cso),
                                                                sizeof(bcds_lanczos2_cso));
                    break;

                case Scaler::Lanczos3:
                    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_lanczos3_cso),
                                                                sizeof(bcds_lanczos3_cso));
                    break;

                case Scaler::Kaiser2:
                    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_kaiser2_cso),
                                                                sizeof(bcds_kaiser2_cso));
                    break;

                case Scaler::Kaiser3:
                    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_kaiser3_cso),
                                                                sizeof(bcds_kaiser3_cso));
                    break;

                case Scaler::Magic:
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
        D3D12_SHADER_BYTECODE byteCode = {};

        if (_upsample)
        {
            _recEncodeShader = CompileShader(upsampleCode.c_str(), "CSMain", "cs_5_0");
            byteCode = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcus_cso), sizeof(bcus_cso));
        }
        else
        {
            InNumThreadsY = 8;
            InNumThreadsX = 8;

            switch (Config::Instance()->OutputScalingDownscaler.value_or_default())
            {
            case Scaler::Bicubic:
                _recEncodeShader = CompileShader(downsampleCodeBC.c_str(), "CSMain", "cs_5_0");
                byteCode =
                    CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_bicubic_cso), sizeof(bcds_bicubic_cso));

                break;

            case Scaler::CatmullRom:
                _recEncodeShader = CompileShader(downsampleCodeCatmull.c_str(), "CSMain", "cs_5_0");
                byteCode =
                    CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_catmull_cso), sizeof(bcds_catmull_cso));

                break;

            case Scaler::Lanczos2:
                _recEncodeShader = CompileShader(downsampleCodeLanczos2.c_str(), "CSMain", "cs_5_0");
                byteCode = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_lanczos2_cso),
                                                   sizeof(bcds_lanczos2_cso));

                break;

            case Scaler::Lanczos3:
                _recEncodeShader = CompileShader(downsampleCodeLanczos3.c_str(), "CSMain", "cs_5_0");
                byteCode = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_lanczos3_cso),
                                                   sizeof(bcds_lanczos3_cso));

                break;

            case Scaler::Kaiser2:
                _recEncodeShader = CompileShader(downsampleCodeKaiser2.c_str(), "CSMain", "cs_5_0");
                byteCode =
                    CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_kaiser2_cso), sizeof(bcds_kaiser2_cso));
                break;

            case Scaler::Kaiser3:
                _recEncodeShader = CompileShader(downsampleCodeKaiser3.c_str(), "CSMain", "cs_5_0");
                byteCode =
                    CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_kaiser3_cso), sizeof(bcds_kaiser3_cso));

                break;

            case Scaler::Magic:
                _recEncodeShader = CompileShader(downsampleCodeMAGIC.c_str(), "CSMain", "cs_5_0");
                byteCode = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_magc_cso), sizeof(bcds_magc_cso));

                break;

            default:
                _recEncodeShader = CompileShader(downsampleCodeBC.c_str(), "CSMain", "cs_5_0");
                byteCode =
                    CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bcds_bicubic_cso), sizeof(bcds_bicubic_cso));

                break;
            }
        }

        if (_recEncodeShader == nullptr)
            LOG_ERROR("[{0}] CompileShader error!", _name);

        // create pso objects
        if (!Shader_Dx12::CreateComputeShader(InDevice, _rootSignature, &_pipelineState, _recEncodeShader, byteCode))
        {
            LOG_ERROR("[{0}] CreateComputeShader error!", _name);
            return;
        }

        SAFE_RELEASE(_recEncodeShader);
    }

    _init = InitHeaps(InDevice, _frameHeaps, OS_NUM_OF_HEAPS);
}

OS_Dx12::~OS_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    for (int i = 0; i < OS_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    SAFE_RELEASE(_buffer);
}
