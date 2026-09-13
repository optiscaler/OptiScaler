#include "pch.h"
#include "DlssNr_Dx12_State.h"

auto DlssNr_Dx12::State::LateContext::Say(const char* message) -> void
{
    if (status != message)
    {
        status = message;
        LOG_INFO("DLSS-NR finished picture: {}", message);
    }
}

auto DlssNr_Dx12::State::LateContext::Finished(const Slot& slot) -> bool
{
    return !slot.fence ||
           (slot.fence->GetCompletedValue() != UINT64_MAX && slot.fence->GetCompletedValue() >= slot.done);
}

auto DlssNr_Dx12::State::LateContext::Cancel() -> void
{
    // Submitted copies may still be in flight; their fences still protect reuse.
    for (auto& slot : slots)
        if (slot.submitted)
            slot.pending = false;
        else if (slot.pending)
            slot.frame.OutputWidth = 0; // Invalidate presentation, retaining unsubmitted recording ownership.
    reset = true;
}

auto DlssNr_Dx12::State::LateContext::Clone(ComPtr<ID3D12Resource>& copy, ID3D12Resource* source) -> bool
{
    const auto want = source->GetDesc();
    if (want.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || want.SampleDesc.Count != 1 ||
        want.DepthOrArraySize != 1 || want.MipLevels != 1)
        return false;
    if (copy)
    {
        const auto have = copy->GetDesc();
        if (have.Width != want.Width || have.Height != want.Height ||
            have.Format != owner.TypedGuideFormat(want.Format))
            copy.Reset(); // caller checked the previous GPU fence
    }
    if (!copy)
        copy.Attach(owner.CreateGuideClone(device.Get(), source));
    return copy != nullptr;
}

auto DlssNr_Dx12::State::LateContext::Acquire(ID3D12GraphicsCommandList* cmd) -> Slot*
{
    if (!cmd)
    {
        Say("Waiting for the DirectX 12 bridge commands.");
        return nullptr;
    }
    ComPtr<ID3D12Device> currentDevice;
    if (FAILED(cmd->GetDevice(IID_PPV_ARGS(&currentDevice))))
        return nullptr;
    if (device && device != currentDevice)
    {
        Say("The graphics device changed. Restart the game to use this option.");
        return nullptr;
    }
    device = currentDevice;
    tracking.store(true);
    // Only the lightweight submission hook is needed, including when FG is disabled.
    ResTrack_Dx12::HookLateNrQueue(device.Get());
    Slot* next = nullptr;
    for (auto& slot : slots)
        if (!slot.pending && Finished(slot))
        {
            next = &slot;
            break;
        }
    if (!next)
    {
        Say("Waiting for the previous picture to finish.");
        return nullptr;
    }
    auto& slot = *next;
    if (!Config::Instance()->DlssNrHdrTransfer.value_or_default())
    {
        // Acquire has verified this slot's fence; never release an in-flight reference.
        slot.cleanScene.Reset();
        slot.response[0].Reset();
        slot.response[1].Reset();
        slot.responseValid = slot.cleanSceneValid = false;
    }
    if (!slot.commands)
    {
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&slot.fence))) ||
            FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&slot.allocator))) ||
            FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&slot.commands))) ||
            FAILED(slot.commands->Close()))
        {
            slot.commands.Reset();
            slot.allocator.Reset();
            slot.fence.Reset();
            Say("Could not prepare the finished-picture option.");
            return nullptr;
        }
    }
    return next;
}

auto DlssNr_Dx12::State::LateContext::Arm(Slot& slot, ID3D12GraphicsCommandList* cmd) -> void
{
    owner.lifetime.Record(cmd);
    slot.producer = cmd;
    ID3D12GraphicsCommandList* real = nullptr;
    if (Util::CheckForRealObject(__FUNCTION__, cmd, (IUnknown**) &real))
        slot.producer = real;
    slot.serial = ++serial;
    slot.ready = slot.done + 1;
    slot.done = slot.ready;
    slot.submitted = false;
    slot.pending = true;
}

auto DlssNr_Dx12::State::LateContext::Capture(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params, bool rr) -> void
{
    if (!params)
        return;
    auto* depth = owner.GetResource(params, NVSDK_NGX_Parameter_Depth, "DLSSD.Depth");
    auto* motion = owner.GetResource(params, NVSDK_NGX_Parameter_MotionVectors, "DLSSD.MotionVectors");
    auto* output = owner.GetResource(params, NVSDK_NGX_Parameter_Output, "DLSSD.Output");
    if (!depth || !motion || !output)
    {
        Cancel();
        Say("Waiting for the game's depth and movement data.");
        return;
    }
    auto* next = Acquire(cmd);
    if (!next)
        return;
    auto& slot = *next;
    if (!Clone(slot.depth, depth) || !Clone(slot.motion, motion))
    {
        Say("The game's depth or movement data is not supported.");
        return;
    }
    slot.residualOnly = false;
    slot.responseValid = slot.cleanSceneValid = false;
    // Copy at the NGX seam, where guide states and lifetimes are defined. Keep typed,
    // shader-readable copies until both the producing queue and NR have finished.
    for (auto pair : { std::pair { depth, slot.depth.Get() }, std::pair { motion, slot.motion.Get() } })
    {
        owner.Barrier(cmd, pair.first, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                      D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmd->CopyResource(pair.second, pair.first);
        owner.Barrier(cmd, pair.first, D3D12_RESOURCE_STATE_COPY_SOURCE,
                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        // The copy returns to COPY_DEST after the late dispatch.
    }
    slot.frame = {};
    auto& frame = slot.frame;
    unsigned flags = 0, gameReset = 0;
    params->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &flags);
    params->Get(NVSDK_NGX_Parameter_Reset, &gameReset);
    frame.Reset = gameReset != 0;
    params->Get(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, &frame.FrameTimeMs);
    frame.DepthInverted = (flags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0;
    frame.MotionVectorsLowResolution = (flags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) != 0;
    frame.RayReconstruction = rr;
    frame.OutputWidth = (unsigned) output->GetDesc().Width;
    frame.OutputHeight = output->GetDesc().Height;
    params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &frame.RenderSubrectWidth);
    params->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &frame.RenderSubrectHeight);
    params->Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, &frame.DepthSubrectBaseX);
    params->Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, &frame.DepthSubrectBaseY);
    params->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, &frame.MotionSubrectBaseX);
    params->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, &frame.MotionSubrectBaseY);
    params->Get(NVSDK_NGX_Parameter_MV_Scale_X, &frame.MvScaleX);
    params->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &frame.MvScaleY);
    frame.ColourIsLinearHdr = false;
    frame.IndependentCommands = true;
    frame.FinishedPicture = true;
    frame.OutputArrivalState = D3D12_RESOURCE_STATE_PRESENT;
    frame.SubmissionEpoch = ::State::Instance().frameCount;
    Arm(slot, cmd);
}

auto DlssNr_Dx12::State::LateContext::CaptureResidual(ID3D12GraphicsCommandList* cmd, ID3D12Resource* clean, ID3D12Resource* residual,
                             float scale, bool sceneLinear, bool reset) -> bool
{
    auto* next = Acquire(cmd);
    if (!next)
        return false;
    auto& slot = *next;
    if (!Clone(slot.residual, residual))
    {
        Say("The upscaled changes could not be saved.");
        return false;
    }
    owner.Barrier(cmd, residual, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->CopyResource(slot.residual.Get(), residual);
    owner.Barrier(cmd, residual, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    slot.cleanSceneValid = false;
    if (Config::Instance()->DlssNrHdrTransfer.value_or_default() && sceneLinear &&
        Clone(slot.cleanScene, clean))
    {
        const auto arrival = Config::Instance()->OutputResourceBarrier.has_value()
                                 ? (D3D12_RESOURCE_STATES) Config::Instance()->OutputResourceBarrier.value()
                                 : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        owner.Barrier(cmd, clean, arrival, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmd->CopyResource(slot.cleanScene.Get(), clean);
        owner.Barrier(cmd, clean, D3D12_RESOURCE_STATE_COPY_SOURCE, arrival);
        slot.cleanSceneValid = true;
    }
    if (!slot.cleanSceneValid)
        slot.responseValid = false;
    slot.frame = {};
    slot.frame.Reset = reset;
    slot.frame.OutputWidth = (unsigned) clean->GetDesc().Width;
    slot.frame.OutputHeight = clean->GetDesc().Height;
    slot.frame.PreExposure = scale;
    slot.frame.SubmissionEpoch = ::State::Instance().frameCount;
    slot.residualOnly = true;
    slot.sceneLinear = sceneLinear;
    Arm(slot, cmd);
    return true;
}
