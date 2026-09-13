#include "pch.h"
#include "DlssNr_Dx12_State.h"

auto DlssNr_Dx12::State::SameHoldShape(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b) -> bool
{
    return a.Dimension == b.Dimension && a.Width == b.Width && a.Height == b.Height &&
           a.DepthOrArraySize == b.DepthOrArraySize && a.MipLevels == b.MipLevels &&
           a.Format == b.Format && a.SampleDesc.Count == b.SampleDesc.Count;
}

auto DlssNr_Dx12::State::ReleaseInputHold() -> void
{
    for (auto& texture : inputHold.textures)
        ParkNrResource(texture.frozen);
    inputHold.active = false;
    inputHold.captureCommands = nullptr;
}

auto DlssNr_Dx12::State::BeginInputHold(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params,
                        const D3D12_RESOURCE_STATES* states) -> void
{
    if (!cmd || !params)
        return;
    const auto& cfg = *Config::Instance();
    const unsigned route = (cfg.DlssNrRunBeforeSr.value_or_default() ? 1u : 0u) |
                           (cfg.DlssNrDeferredDlss.value_or_default() ? 2u : 0u) |
                           (cfg.DlssNrFinishedPicture.value_or_default() ? 4u : 0u);
    const bool requested = cfg.DlssNrEnabled.value_or_default() && cfg.DlssNrHoldFrame.value_or_default() &&
                           (route & 3u) != 0;
    if (!requested)
    {
        if (inputHold.active)
        {
            ReleaseInputHold();
            inputHold.parameters.Apply(params, false); // reset SR once when returning to live input
            nr.reset = true;
        }
        return;
    }
    lifetime.Record(cmd);
    ID3D12Resource* live[4] {};
    auto* output = GetResource(params, NVSDK_NGX_Parameter_Output, "DLSSD.Output");
    if (!output)
        return;
    bool capture = !inputHold.active || inputHold.route != route ||
                   !SameHoldShape(inputHold.outputDesc, output->GetDesc());
    for (size_t i = 0; i < inputHold.textures.size(); ++i)
    {
        const auto& saved = inputHold.textures[i];
        live[i] = GetResource(params, saved.name, saved.alias);
        capture |= (live[i] != nullptr) != (saved.frozen != nullptr) ||
                   (live[i] && !SameHoldShape(saved.desc, live[i]->GetDesc()));
    }
    if (!live[0] || !live[1] || !live[2])
        return;
    if (capture)
    {
        ReleaseInputHold();
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(cmd->GetDevice(IID_PPV_ARGS(&device))))
            return;
        ResTrack_Dx12::HookLateNrQueue(device.Get());
        // Allocate all snapshots before touching the live resources.
        for (size_t i = 0; i < inputHold.textures.size(); ++i)
        {
            if (!live[i])
                continue;
            auto& saved = inputHold.textures[i];
            saved.desc = live[i]->GetDesc();
            if (saved.desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || saved.desc.MipLevels != 1 ||
                saved.desc.DepthOrArraySize != 1 || saved.desc.SampleDesc.Count != 1 ||
                !(saved.frozen = CreateGuideClone(device.Get(), live[i])))
            {
                ReleaseInputHold();
                LOG_WARN("NR hold: unsupported input snapshot {}", saved.name);
                return;
            }
        }
        inputHold.parameters.Capture(params);
        inputHold.outputDesc = output->GetDesc();
        inputHold.route = route;
        ++inputHold.generation;
        inputHold.captureCommands = cmd;
        ID3D12GraphicsCommandList* real = nullptr;
        if (Util::CheckForRealObject(__FUNCTION__, cmd, (IUnknown**) &real))
            inputHold.captureCommands = real;
        nr.heldActive = false;
        ParkNrResource(nr.heldColor);
    }
    for (size_t i = 0; i < inputHold.textures.size(); ++i)
    {
        auto* frozen = inputHold.textures[i].frozen;
        if (!live[i] || !frozen)
            continue;
        // Preserve the original resource identities and arrival states for the game's upscaler.
        const auto copyState = capture ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_COPY_DEST;
        Barrier(cmd, live[i], states[i], copyState);
        if (capture)
        {
            cmd->CopyResource(frozen, live[i]);
            Barrier(cmd, frozen, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }
        else
            cmd->CopyResource(live[i], frozen);
        Barrier(cmd, live[i], copyState, states[i]);
    }
    inputHold.active = true;
    inputHold.parameters.Apply(params);
}

auto DlssNr_Dx12::State::CheckCaptureTrigger() -> void
{
    if ((frames % 60) != 0)
        return;

    std::error_code ec;
    const auto trigger = Util::DllPath().remove_filename() / "dlssnr-capture.trigger";

    if (std::filesystem::exists(trigger, ec))
    {
        std::filesystem::remove(trigger, ec);
        captureFrames.request(capture::kMaxFrames);
        LOG_INFO("DLSS-NR capture requested by trigger file");
    }
}
