#include "pch.h"
#include <dlssnr/PassProfiles.h>

#include <set>
#include <list>
#include <wrl/client.h>
#include <resource_tracking/ResTrack_Dx12.h>
#include <dlssnr/DlssNr_FinishedPictureBridge_Dx11.h>
#include <dlssnr/DlssNr_HoldParameters_Dx12.h>
#include <upscalers/ShaderPipeline_Dx12.h>

#include <dlssnr/DlssNr.h>

#include <dlssnr/DlssNr_Capture.h>
#include <dlssnr/DlssNr_Proxy.h>
#include <dlssnr/DlssNr_GpuLifetime.h>
#include <dlssnr/DlssNr_ExposureScan.h>

#include "DlssNr_Dx12_State.h"
#include "DlssNr_ActiveColor.h"
#include "DlssNr_Upscaler_Dx12.h"
#include <dlssnr/DlssNr_Pipeline_Dx12.h>
#include "DlssNr_Guides.h"
#include "DlssNr_SeamClock.h"

#include <Config.h>
#include <State.h>
#include <Util.h>

#include <proxies/NVNGX_Proxy.h>
#include <hooks/D3D12_Hooks.h>
#include <gpu_time/GpuTime_Dx12.h>
#include "DlssNr_GpuTime.h"

#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstring>
#include "precompile/DlssNr_Shader.h"
#include "precompile/dlssnr_residual_Shader.h"
#include "precompile/dlssnr_finished_color_Shader.h"
#include "DlssNr_ResidualPair.h"
#include "../output_scaling/OS_Dx12.h"

using DlssNr::Profiles::NrPassTuning;
using DlssNr::Profiles::PassPreset;
using DlssNr::Profiles::PassStyle;
using DlssNr::Profiles::PassTuning;

using DlssNr::CalibrationReading;

namespace
{
std::recursive_mutex nrOwnersMutex;
std::vector<DlssNr_Dx12*> nrOwners;
std::atomic_uint nrCaptureOutstanding { 0 };
// Only unresolved work at process teardown is intentionally retained. During the session
// retired owners stay registered for submission/reset callbacks until they can be reclaimed.
auto& RetiredNrOwners()
{
    static auto* owners = new std::list<std::unique_ptr<DlssNr_Dx12>>;
    return *owners;
}
unsigned nrNotificationDepth = 0;
void CollectRetiredNrOwners()
{
    static bool collecting = false;
    if (collecting || nrNotificationDepth) return;
    collecting = true;
    auto& owners = RetiredNrOwners();
    for (auto it = owners.begin(); it != owners.end();)
    {
        if (!(*it)->ReadyToDestroy()) { ++it; continue; }
        auto finished = std::move(*it);
        it = owners.erase(it);
        finished.reset(); // May enqueue a child codec; list iterators remain valid.
        LOG_INFO("DLSS-NR: reclaimed retired GPU owner; {} waiting", owners.size());
    }
    collecting = false;
}
struct NrNotificationScope
{
    NrNotificationScope() { ++nrNotificationDepth; }
    ~NrNotificationScope() { --nrNotificationDepth; CollectRetiredNrOwners(); }
};
DlssNr_Dx12* activeNrOwner = nullptr;
void ActivateNrOwner(DlssNr_Dx12* owner)
{
    if (std::find(nrOwners.begin(), nrOwners.end(), owner) == nrOwners.end())
        nrOwners.push_back(owner);
    activeNrOwner = owner;
}
} // namespace



// ---------------------------------------------------------------------------------------------
// The pass itself. Everything above is what it is made of; everything below is the shape the rest
// of OptiScaler sees.
// ---------------------------------------------------------------------------------------------

DlssNr_Dx12::DlssNr_Dx12(std::string InName, ID3D12Device* InDevice)
    : Shader_Dx12(InName, InDevice), _state(std::make_unique<State>(*this))
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    // Five inputs, two outputs, one constant buffer, and a clamped linear sampler.
    //
    // The sampler exists because the model may be run below full resolution, in which case its answer
    // has to be read back at a different size from the frame it is being transferred onto.
    D3D12_STATIC_SAMPLER_DESC sampler {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    if (!SetupRootSignature(InDevice, kSrvCount, kUavCount, 1, 0, 0, 1, &sampler))
    {
        LOG_ERROR("[{0}] Failed to setup root signature", _name);
        return;
    }

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(DlssNrConstants));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    for (uint32_t i = 0; i < DLSSNR_NUM_OF_HEAPS; ++i)
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

    // Precompiled, with no source fallback. The shader used to be compiled at runtime from a string,
    // which would have meant no shader at all for anyone leaving UsePrecompiledShaders at its
    // default.
    if (!CreateComputePipeline(InDevice, &_pipelineState, DlssNr_cso, sizeof(DlssNr_cso), nullptr))
    {
        LOG_ERROR("[{0}] Failed to create the compute pipeline", _name);
        return;
    }

    // Second PSO for the ResidualAcrossRR v2 accumulator (its own blob, same root signature).
    // A failure here is not fatal to the class -- only that experimental mode goes unavailable.
    if (!CreateComputePipeline(InDevice, &_residualPipelineState, dlssnr_residual_cso, sizeof(dlssnr_residual_cso),
                               nullptr))
    {
        _residualPipelineState = nullptr;
        LOG_WARN("[{0}] ResidualAcrossRR compute pipeline unavailable", _name);
    }

    _init = InitHeaps(InDevice, _frameHeaps, DLSSNR_NUM_OF_HEAPS);
    if (_init)
        ResTrack_Dx12::HookLateNrQueue(InDevice); // Observe feature-creation submissions too, before the first Run.
    // Codec-only instances never call Dispatch/ProcessSeam, but still record GPU work.
    std::lock_guard lock(nrOwnersMutex);
    nrOwners.push_back(this);
}

bool DlssNr_Dx12::DispatchPass(ID3D12GraphicsCommandList* InCmdList, const DlssNrConstants& InConstants,
                               ID3D12Resource* InSource, ID3D12Resource* InModel, ID3D12Resource* InOriginal,
                               ID3D12Resource* InMotion, ID3D12Resource* InPrevEdit, ID3D12Resource* OutTarget,
                               ID3D12Resource* OutKeep, uint32_t* immutableSlot)
{
    _state->lifetime.Record(InCmdList);
    if (!_init || InCmdList == nullptr || _device == nullptr || InSource == nullptr || OutTarget == nullptr)
        return false;

    const bool reuse = immutableSlot && *immutableSlot != UINT32_MAX;
    const uint32_t slot = reuse ? *immutableSlot : _heapIndex;
    if (!reuse)
        _heapIndex = (_heapIndex + 1) % DLSSNR_NUM_OF_HEAPS;

    FrameDescriptorHeap& currentHeap = _frameHeaps[slot];
    if (!reuse)
    {

        // Every slot in the table gets a view, whether the mode reads it or not. An unbound descriptor is
        // not an empty read; it is a read from nothing, and the source stands in wherever a mode has
        // nothing of its own to put there.
        ID3D12Resource* const srvs[kSrvCount] = {
            InSource,
            InModel != nullptr ? InModel : InSource,
            InOriginal != nullptr ? InOriginal : InSource,
            InMotion != nullptr ? InMotion : InSource,
            InPrevEdit != nullptr ? InPrevEdit : InSource,
        };

        for (uint32_t i = 0; i < kSrvCount; ++i)
            CreateShaderResourceView(_device, srvs[i], currentHeap.GetSrvCPU(i));

        ID3D12Resource* const uavs[kUavCount] = {
            OutTarget,
            OutKeep != nullptr ? OutKeep : OutTarget,
        };

        for (uint32_t i = 0; i < kUavCount; ++i)
            CreateUnorderedAccessView(_device, uavs[i], currentHeap.GetUavCPU(i), 0);

        if (!CreateConstantsBuffer(_device, _constantBuffers[slot], InConstants, currentHeap.GetCbvCPU(0)))
        {
            LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
            return false;
        }

        if (immutableSlot)
            *immutableSlot = slot;
    }

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);
    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    // Sized from the constants rather than from a resource, because the pass that shrinks the proxy
    // writes fewer pixels than its source has.
    const UINT dispatchWidth = (InConstants.Width + _numThreadsX - 1) / _numThreadsX;
    const UINT dispatchHeight = (InConstants.Height + _numThreadsY - 1) / _numThreadsY;
    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

void DlssNr_Dx12::Retire(std::unique_ptr<DlssNr_Dx12> owner)
{
    if (!owner) return;
    std::lock_guard lock(nrOwnersMutex);
    if (activeNrOwner == owner.get()) activeNrOwner = nullptr;
    DlssNr::ClearStatus(owner.get());
    {
        std::lock_guard stateLock(owner->_state->mutex);
        owner->_state->late.Cancel();
        // These lists belong to NR and cannot be replayed after retirement. Closing
        // their logical recordings retains every submitted fence, without resetting GPU allocators.
        for (auto& slot : owner->_state->late.slots)
            if (slot.commands) owner->_state->FinishedPictureResetCommandList(slot.commands.Get());
    }
    RetiredNrOwners().push_back(std::move(owner));
    LOG_INFO("DLSS-NR: retaining retired GPU owner until recordings finish; {} waiting", RetiredNrOwners().size());
}

bool DlssNr_Dx12::ReadyToDestroy()
{
    std::lock_guard lock(_state->mutex);
    _state->CollectEnlargers();
    if (_state->collectingEnlargers) return false;
    if (!_state->retiredEnlargers.empty() || (_state->enlarger && !_state->enlarger->lifetime.Idle())) return false;
    if (!_state->lifetime.Idle()) return false;
    for (auto& model : _state->nr.models)
        if (!model.Idle()) return false;
    for (const auto& slot : _state->late.slots)
        if (slot.submitted && !_state->late.Finished(slot)) return false;
    return true;
}

DlssNr_Dx12::~DlssNr_Dx12()
{
    std::lock_guard lock(nrOwnersMutex);
    std::erase(nrOwners, this);
    if (activeNrOwner == this)
        activeNrOwner = nullptr;
    DlssNr::ClearStatus(this);
    const bool finished = _state->WaitForFinishedPicture();
    if (!finished || !_state->lifetime.Idle())
    {
        LOG_WARN("DLSS-NR: abandoning GPU ownership with unresolved command recordings at teardown");
        _state.release();
        for (auto& heap : _frameHeaps)
        {
            if (heap.GetHeapCSU()) heap.GetHeapCSU()->AddRef();
            if (heap.GetHeapRtv()) heap.GetHeapRtv()->AddRef();
        }
        _rootSignature = nullptr;
        _pipelineState = nullptr;
        _constantBuffer = nullptr;
        GpuTime.release();
        return;
    }
    _state.reset();
    for (auto& heap : _frameHeaps)
        heap.ReleaseHeaps();
    if (_finishedColorPipelineState)
        _finishedColorPipelineState->Release();
    for (auto& buffer : _constantBuffers)
    {
        if (buffer != nullptr)
        {
            buffer->Release();
            buffer = nullptr;
        }
    }

    if (_residualPipelineState != nullptr)
    {
        _residualPipelineState->Release();
        _residualPipelineState = nullptr;
    }
}

bool DlssNr_Dx12::DispatchResidualPass(ID3D12GraphicsCommandList* InCmdList, const DlssNrConstants& InConstants,
                                       ID3D12Resource* InSource, ID3D12Resource* InModel, ID3D12Resource* InOriginal,
                                       ID3D12Resource* InMotion, ID3D12Resource* OutTarget, bool finishedColor)
{
    _state->lifetime.Record(InCmdList);
    if (finishedColor && !_finishedColorPipelineState && _init)
        CreateComputePipeline(_device, &_finishedColorPipelineState, dlssnr_finished_color_cso,
                              sizeof(dlssnr_finished_color_cso), nullptr);
    auto* pipeline = finishedColor ? _finishedColorPipelineState : _residualPipelineState;
    if (!_init || pipeline == nullptr || InCmdList == nullptr || _device == nullptr || InSource == nullptr ||
        OutTarget == nullptr)
        return false;

    const uint32_t slot = _heapIndex;
    _heapIndex = (_heapIndex + 1) % DLSSNR_NUM_OF_HEAPS;

    FrameDescriptorHeap& currentHeap = _frameHeaps[slot];

    // Same table shape as DispatchPass: the residual shader reads t0..t3 + u0, and t4/u1 get the
    // source as a stand-in so no descriptor in the table is left unbound.
    ID3D12Resource* const srvs[kSrvCount] = {
        InSource,
        InModel != nullptr ? InModel : InSource,
        InOriginal != nullptr ? InOriginal : InSource,
        InMotion != nullptr ? InMotion : InSource,
        InSource,
    };

    for (uint32_t i = 0; i < kSrvCount; ++i)
        CreateShaderResourceView(_device, srvs[i], currentHeap.GetSrvCPU(i));

    ID3D12Resource* const uavs[kUavCount] = { OutTarget, OutTarget };

    for (uint32_t i = 0; i < kUavCount; ++i)
        CreateUnorderedAccessView(_device, uavs[i], currentHeap.GetUavCPU(i), 0);

    if (!CreateConstantsBuffer(_device, _constantBuffers[slot], InConstants, currentHeap.GetCbvCPU(0)))
    {
        LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(pipeline);
    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    const UINT dispatchWidth = (InConstants.Width + _numThreadsX - 1) / _numThreadsX;
    const UINT dispatchHeight = (InConstants.Height + _numThreadsY - 1) / _numThreadsY;
    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

bool DlssNr_Dx12::CreateBufferResource(ID3D12Device* device, ID3D12Resource* source, D3D12_RESOURCE_STATES state)
{
    if (device == nullptr || source == nullptr)
        return false;
    auto desc = source->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 ||
        desc.DepthOrArraySize != 1 || desc.MipLevels != 1)
        return false;
    desc.Flags = (desc.Flags | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) & ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    if (_state->buffer != nullptr)
    {
        const auto previous = _state->buffer->GetDesc();
        if (previous.Width == desc.Width && previous.Height == desc.Height && previous.Format == desc.Format &&
            previous.Flags == desc.Flags)
            return true;
        _state->ParkNrResource(_state->buffer);
    }
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                               IID_PPV_ARGS(&_state->buffer))))
        return false;
    _state->bufferState = state;
    return true;
}

void DlssNr_Dx12::SetBufferState(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES state)
{
    _state->lifetime.Record(cmdList);
    Shader_Dx12::SetBufferState(cmdList, state, _state->buffer, &_state->bufferState);
}

ID3D12Resource* DlssNr_Dx12::Buffer() { return _state->buffer; }
bool DlssNr_Dx12::CanRender() const { return _init && _state->buffer != nullptr; }

bool DlssNr_Dx12::Dispatch(ID3D12GraphicsCommandList* cmd, ID3D12Resource* colour, ID3D12Resource* depth,
                           ID3D12Resource* motion, ID3D12Resource* output, const DlssNrFrameInfo& frame,
                           ID3D12CommandQueue* queue)
{
    std::lock_guard ownersLock(nrOwnersMutex);
    ActivateNrOwner(this);
    std::lock_guard stateLock(_state->mutex);
    _state->ConsumeControls();
    struct Publish
    {
        State& s;
        ~Publish() { s.Publish(); }
    } publish { *_state };
    if (!_init || !cmd || !colour || !depth || !motion || !output)
        return false;
    auto info = frame;
    info.PipelineManagedStates = true;
    info.PrivateColorCopy = true;
    if (!info.RenderSubrectWidth)
        info.RenderSubrectWidth = info.Width;
    if (!info.RenderSubrectHeight)
        info.RenderSubrectHeight = info.Height;
    if (colour != output)
    {
        const auto source = colour->GetDesc(), target = output->GetDesc();
        if (source.Width != target.Width || source.Height != target.Height || source.Format != target.Format)
            return false;
        _state->Barrier(cmd, colour, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        _state->Barrier(cmd, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(output, colour);
        _state->Barrier(cmd, colour, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        _state->Barrier(cmd, output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    _state->nr.exposureOfferedNow = info.ExposureTexture != nullptr;
    _state->nr.exposureEverOffered |= _state->nr.exposureOfferedNow;
    ++_state->nr.exposureFrames;
    const auto before = _state->nr.successfulDispatches;
    _state->Run(cmd, output, depth, motion, output, info, queue);
    return _state->nr.successfulDispatches != before;
}

void DlssNr_Dx12::BeginInputHold(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params,
                                const D3D12_RESOURCE_STATES* inputStates)
{
    std::lock_guard lock(_state->mutex);
    _state->BeginInputHold(cmd, params, inputStates);
}

void DlssNr_Dx12::EndInputHold(NVSDK_NGX_Parameter* params)
{
    std::lock_guard lock(_state->mutex);
    _state->inputHold.parameters.Restore(params);
}

bool DlssNr_Dx12::ProcessSeam(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params, bool beforeUpscale,
                              ID3D12CommandQueue* queue, bool rayReconstruction, unsigned long long submissionEpoch,
                              bool interop, uint32_t featureFlags)
{
    std::lock_guard ownersLock(nrOwnersMutex);
    ActivateNrOwner(this);
    std::lock_guard stateLock(_state->mutex);
    _state->ConsumeControls();
    _state->featureFlags = featureFlags;
    const auto& cfg = *Config::Instance();
    // Both seams reach this scheduler; ordinary passes remain in the shared shader pipeline.
    const auto placement = DlssNr::ResolvePlacement(
        cfg.DlssNrRunBeforeSr.value_or_default(), cfg.DlssNrDeferredDlss.value_or_default(),
        cfg.DlssNrResidualAcrossRr.value_or_default(), cfg.DlssNrFinishedPicture.value_or_default());
    const bool special = placement.finished || placement.deferred;
    if (special)
        _state->EvaluateInternal(cmd, params, beforeUpscale, queue, rayReconstruction, submissionEpoch, interop);
    else
    {
        if (_state->lastFinishedMode != 0)
        {
            _state->lastFinishedMode = 0;
            _state->nr.reset = true;
            if (_state->gpuTime)
                _state->gpuTime->ClearLast();
            if (_state->ngxTime)
                _state->ngxTime->ClearLast();
            _state->lastGpuTime.reset();
            _state->lastNgxTime.reset();
        }
        _state->late.Cancel();
        _state->deferredSr.Cancel();
    }
    _state->Publish();
    return special;
}
void DlssNr_Dx12::DiagnosePipeline(unsigned stage, ID3D12GraphicsCommandList* cmd,
                                  NVSDK_NGX_Parameter* params, ID3D12Resource* color,
                                  uint32_t flags, bool rr, bool success)
{
    std::lock_guard ownersLock(nrOwnersMutex);
    std::lock_guard stateLock(_state->mutex);
    auto& state = *_state;
    if (stage == 0)
    {
        state.lifetime.Collect();
        static bool previousGameplay = false, previousPhoto = false;
        static unsigned runs = 0;
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        const bool control = foregroundProcess == GetCurrentProcessId() &&
                             (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool gameplay = control && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        const bool photo = control && (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        const char* label = gameplay && !previousGameplay ? "gameplay" :
                            photo && !previousPhoto ? "photomode" : nullptr;
        previousGameplay = gameplay; previousPhoto = photo;
        if (label && !state.pipelineCaptureRemaining && !nrCaptureOutstanding)
        {
            if (runs >= 2)
                LOG_WARN("NR pipeline capture: two-run limit reached; restart to capture again");
            else
            {
                ++runs;
                SYSTEMTIME time {}; GetLocalTime(&time);
                char folder[100];
                std::snprintf(folder, sizeof(folder), "%04u%02u%02u-%02u%02u%02u-%03u-%s-%u",
                    time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                    time.wMilliseconds, label, runs);
                state.pipelineCaptureDirectory = Util::DllPath().parent_path() / "nr-pipeline-captures" / folder;
                state.pipelineCaptureRemaining = 4;
                LOG_INFO("NR pipeline capture armed: {} (four frames)", state.pipelineCaptureDirectory.string());
            }
        }
        if (!state.pipelineCaptureRemaining || state.pipelineCapture) return;
        auto job = std::make_unique<DlssNr::PipelineCaptureFrame>();
        if (!job->Init(_device))
        { state.pipelineCaptureRemaining = 0; LOG_ERROR("NR pipeline capture allocation failed"); return; }
        job->directory = state.pipelineCaptureDirectory / std::to_string(4 - state.pipelineCaptureRemaining);
        job->metadata << "stage_semantics before_nr=scene_linear_input after_nr=NR_composed_RR_input "
                         "after_rr=upscaler_output_before_postprocessing\n"
                      << "game_frame " << ::State::Instance().frameCount << " command_list " << cmd
                      << " parameters " << params << " rr " << rr << " feature_flags " << flags << '\n';
        const auto& cfg = *Config::Instance();
        job->metadata << "nr_passes " << cfg.DlssNrPasses.value_or_default()
                      << " working_scale " << cfg.DlssNrWorkingScale.value_or_default()
                      << " nr_history_reset " << state.nr.reset
                      << " hold " << cfg.DlssNrHoldFrame.value_or_default() << '\n';
        for (const char* key : { NVSDK_NGX_Parameter_Reset, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,
             NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, NVSDK_NGX_Parameter_OutWidth,
             NVSDK_NGX_Parameter_OutHeight, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X,
             NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X,
             NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y })
        {
            unsigned value = 0; auto result = params->Get(key, &value);
            job->metadata << key << ' ' << value << " get_result " << unsigned(result) << '\n';
        }
        for (const char* key : { NVSDK_NGX_Parameter_MV_Scale_X, NVSDK_NGX_Parameter_MV_Scale_Y,
             NVSDK_NGX_Parameter_Jitter_Offset_X, NVSDK_NGX_Parameter_Jitter_Offset_Y,
             NVSDK_NGX_Parameter_DLSS_Pre_Exposure, NVSDK_NGX_Parameter_DLSS_Exposure_Scale,
             NVSDK_NGX_Parameter_FrameTimeDeltaInMsec })
        {
            float value = 0; auto result = params->Get(key, &value);
            job->metadata << key << ' ' << value << " get_result " << unsigned(result) << '\n';
        }
        // Record descriptors of optional RR inputs without assuming their presence.
        for (const char* key : { "DLSS.Input.DiffuseAlbedo", "DLSS.Input.SpecularAlbedo", "GBuffer.Normals",
             "GBuffer.Roughness", "MotionVectorsReflection", "DLSSD.SpecularHitDistance",
             "DLSS.Input.ColorBeforeParticles", "DLSSD.DiffuseHitDistance" })
        {
            auto* resource = state.GetResource(params, key, key);
            job->metadata << key << " resource " << resource;
            if (resource)
            { auto desc = resource->GetDesc(); job->metadata << " width " << desc.Width << " height " << desc.Height
                                                           << " format " << desc.Format; }
            job->metadata << '\n';
        }
        state.pipelineCapture = job.release();
        ++nrCaptureOutstanding;
        state.lifetime.Record(cmd);
        const auto inputs = DlssNr::ResolveInputStates_Dx12(false);
        state.pipelineCapture->Copy(cmd, _device, "before_nr", color, inputs.color);
        state.pipelineCapture->Copy(cmd, _device, "motion", state.GetResource(params,
            NVSDK_NGX_Parameter_MotionVectors, "DLSSD.MotionVectors"), inputs.motion);
        state.pipelineCapture->Copy(cmd, _device, "depth", state.GetResource(params,
            NVSDK_NGX_Parameter_Depth, "DLSSD.Depth"), inputs.depth);
        state.pipelineCapture->Copy(cmd, _device, "exposure", state.GetResource(params,
            NVSDK_NGX_Parameter_ExposureTexture, "DLSSD.ExposureTexture"), inputs.exposure);
        return;
    }
    auto* job = state.pipelineCapture;
    if (!job) return;
    job->metadata << "stage " << stage << " success " << success << '\n';
    if (stage == 1)
    {
        job->metadata << "nr_model_evaluated " << state.modelRunning << '\n';
        job->Copy(cmd, _device, "after_nr", color, DlssNr::ResolveInputStates_Dx12(false).color);
        return;
    }
    if (success) job->Copy(cmd, _device, "after_rr", color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    job->End(cmd);
    state.pipelineCapture = nullptr;
    --state.pipelineCaptureRemaining;
    state.lifetime.Retire([job]
    {
        if (job->Write()) LOG_INFO("NR pipeline capture saved: {}", job->directory.string());
        else LOG_WARN("NR pipeline capture discarded or write failed: {}", job->directory.string());
        delete job;
        --nrCaptureOutstanding;
    });
}

void DlssNr_Dx12::ResetFinishedCommands(ID3D12CommandList* cmd) { _state->FinishedPictureResetCommandList(cmd); }
void DlssNr_Dx12::SubmitFinishedCommands(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    _state->FinishedPictureSubmitted(queue, count, lists);
}
bool DlssNr_Dx12::WaitFinished() { return _state->WaitForFinishedPicture(); }
void DlssNr_Dx12::ApplyFinished(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue)
{
    _state->ApplyToFinishedPicture(swapchain, queue);
    _state->Publish();
}
void DlssNr_Dx12::ApplyFinishedDx11(IDXGISwapChain* swapchain)
{
    _state->ApplyToFinishedPictureDx11(swapchain);
    _state->Publish();
}
std::string DlssNr_Dx12::FinishedStatus() { return _state->FinishedPictureStatus(); }
std::string DlssNr_Dx12::DeferredStatus() { return _state->DeferredDlssStatus(); }
DlssNr::CalibrationReading DlssNr_Dx12::CalibrationStatus()
{
    std::lock_guard lock(_state->mutex);
    return _state->Calibration();
}

namespace DlssNr
{
void FinishedPictureResetCommandList(ID3D12CommandList* cmd)
{
    std::lock_guard lock(nrOwnersMutex);
    NrNotificationScope notification;
    const auto owners = nrOwners;
    for (auto* owner : owners)
        owner->ResetFinishedCommands(cmd);
}
void FinishedPictureSubmitted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    std::lock_guard lock(nrOwnersMutex);
    NrNotificationScope notification;
    const auto owners = nrOwners;
    for (auto* owner : owners)
        owner->SubmitFinishedCommands(queue, count, lists);
}
bool WaitForFinishedPicture()
{
    std::lock_guard lock(nrOwnersMutex);
    NrNotificationScope notification;
    bool ready = true;
    const auto owners = nrOwners;
    for (auto* owner : owners)
        ready = owner->WaitFinished() && ready;
    return ready;
}
void ApplyToFinishedPicture(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue)
{
    std::lock_guard lock(nrOwnersMutex);
    if (activeNrOwner)
        activeNrOwner->ApplyFinished(swapchain, queue);
}
void ApplyToStreamlinePicture(IDXGISwapChain* swapchain, ID3D12Resource* picture, ID3D12CommandQueue* queue)
{
    std::lock_guard lock(nrOwnersMutex);
    if (activeNrOwner)
        activeNrOwner->ApplyStreamlineFinished(swapchain, picture, queue);
}
void ApplyToFinishedPictureDx11(IDXGISwapChain* swapchain)
{
    std::lock_guard lock(nrOwnersMutex);
    if (activeNrOwner)
        activeNrOwner->ApplyFinishedDx11(swapchain);
}
void FinishedPictureColorSpace(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE colorSpace)
{
    static constexpr GUID key = { 0x34a31e7b, 0x84c5, 0x44ef, { 0xa7, 0x4d, 0x6b, 0xd3, 0x60, 0x8c, 0xe5, 0x22 } };
    if (swapchain)
        swapchain->SetPrivateData(key, sizeof(colorSpace), &colorSpace);
}
std::string FinishedPictureStatus()
{
    std::lock_guard lock(nrOwnersMutex);
    return activeNrOwner ? activeNrOwner->FinishedStatus() : "Waiting for a finished picture.";
}
std::string DeferredDlssStatus()
{
    std::lock_guard lock(nrOwnersMutex);
    return activeNrOwner ? activeNrOwner->DeferredStatus() : "not started";
}
CalibrationReading Calibration()
{
    std::lock_guard lock(nrOwnersMutex);
    return activeNrOwner ? activeNrOwner->CalibrationStatus() : CalibrationReading {};
}

void Shutdown() { WaitForFinishedPicture(); }
} // namespace DlssNr
