#include "pch.h"
#include "DlssNr_Dx12_State.h"

void DlssNr_Dx12::State::ReleaseEnlarger()
{
    if (!enlarger) return;
    retiredEnlargers.push_back(std::move(enlarger));
    CollectEnlargers();
}

void DlssNr_Dx12::State::CollectEnlargers()
{
    if (collectingEnlargers) return;
    collectingEnlargers = true;
    // Release NGX only after container mutation: its destruction can re-enter queue hooks.
    std::vector<std::unique_ptr<Enlarger>> completed;
    for (auto& old : retiredEnlargers)
        if (old->lifetime.Idle()) completed.push_back(std::move(old));
    std::erase_if(retiredEnlargers, [](const auto& old) { return !old; });
    completed.clear();
    collectingEnlargers = false;
}

ID3D12Resource* DlssNr_Dx12::State::EnlargeMatchedResidual(ID3D12GraphicsCommandList* cmd,
    ID3D12Device* device, ID3D12Resource* proxy, ID3D12Resource* answer, ID3D12Resource* depth,
    ID3D12Resource* motion, const DlssNrFrameInfo& frame, const DlssNrConstants& resolve,
    bool reset, ID3D12CommandQueue* timingQueue)
{
    auto say = [&](const std::string& message) -> ID3D12Resource*
    {
        if (enlargementStatus != message) LOG_INFO("NR DLSS enlargement: {}", message);
        enlargementStatus = message;
        return nullptr;
    };
    if (frame.BeforeUpscale)
    {
        ReleaseEnlarger();
        return say("Matched residual + DLSS requires NR after the game upscaler.");
    }
    // The swapchain queue can be Streamline's presentation queue, not the NR producer.
    // Native processing learns its queue from the actual creation submission below.
    // IFeature_Dx12 fills timingQueue from currentCommandQueue even for native games.
    // Only independent bridge/presentation command lists supply an authoritative producer.
    auto* queue = (frame.IndependentCommands || frame.FinishedPicture) ? timingQueue : nullptr;
    ID3D12CommandQueue* realQueue = nullptr;
    if (queue && Util::CheckForRealObject(__FUNCTION__, queue, (IUnknown**)&realQueue)) queue = realQueue;
    if (cmd->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT ||
        (queue && queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT))
        return say("Waiting for the NR direct command queue.");
    Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
    if (queue && (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != device))
        return say("NR DLSS enlargement queue/device mismatch.");
    const auto desc = proxy->GetDesc();
    const unsigned w = unsigned(desc.Width), h = desc.Height;
    if (enlarger && (enlarger->w != w || enlarger->h != h || enlarger->outW != resolve.Width ||
        enlarger->outH != resolve.Height || (queue && enlarger->queue.Get() != queue) ||
        enlarger->depthInverted != frame.DepthInverted)) ReleaseEnlarger();
    CollectEnlargers();
    if (!enlarger)
    {
        if (retiredEnlargers.size() >= 4) return say("Waiting for retired DLSS enlargement work.");
        enlarger = std::make_unique<Enlarger>();
        auto& g = *enlarger;
        g.w = w; g.h = h; g.outW = resolve.Width; g.outH = resolve.Height;
        g.depthInverted = frame.DepthInverted; g.queue = queue;
        g.input.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h));
        g.output.Attach(CreateScratch(device, DXGI_FORMAT_R16G16B16A16_FLOAT, g.outW, g.outH));
        g.depth.Attach(CreateScratch(device, DXGI_FORMAT_R32_FLOAT, w, h));
        g.motion.Attach(CreateScratch(device, DXGI_FORMAT_R32G32_FLOAT, w, h));
        g.exposure.Attach(CreateScratch(device, DXGI_FORMAT_R32_FLOAT, 1, 1));
        g.failed = true;
        if (!g.input || !g.output || !g.depth || !g.motion || !g.exposure)
            return say("DLSS enlargement resource allocation failed; use Retry.");
        lifetime.Record(cmd);
        g.lifetime.Record(cmd);
        DlssNrConstants unit {}; unit.Mode = DlssNrMode_UnitExposure; unit.Width = unit.Height = 1;
        if (!shader.DispatchPass(cmd, unit, proxy, nullptr, nullptr, nullptr, nullptr, g.exposure.Get(), nullptr))
            return say("DLSS enlargement exposure initialization failed; use Retry.");
        Barrier(cmd, g.exposure.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g.dlss = std::make_unique<DlssNr::PrivateUpscalerDx12>(DlssNr::PrivateUpscaler::DLSS);
        DlssNr::PrivateUpscalerCreateDx12 info;
        info.width = w; info.height = h; info.outputWidth = g.outW; info.outputHeight = g.outH;
        info.quality = 2; info.depthInverted = frame.DepthInverted;
        info.rayReconstruction = false; // This carrier comes from an already reconstructed image.
        if (!g.dlss->Init(device, cmd, info)) return say("Private DLSS SR: " + g.dlss->Error());
        LOG_INFO("NR matched residual: private DLSS SR created at {}x{} -> {}x{}", w, h, g.outW, g.outH);
        ID3D12GraphicsCommandList* real = nullptr;
        g.creation = Util::CheckForRealObject(__FUNCTION__, cmd, (IUnknown**)&real) ? real : cmd;
        g.failed = false;
        return say("Waiting for DLSS enlargement initialization submission.");
    }
    auto& g = *enlarger;
    if (g.failed) return nullptr;
    if (!g.submitted) return say("Waiting for DLSS enlargement initialization submission.");
    const auto dd = depth->GetDesc(), md = motion->GetDesc();
    const auto regions = DlssNr::ResolveGuideRegions({ unsigned(dd.Width), dd.Height },
        { unsigned(md.Width), md.Height }, { frame.RenderSubrectWidth, frame.RenderSubrectHeight },
        { frame.OutputWidth, frame.OutputHeight }, frame.MotionVectorsLowResolution,
        frame.DepthSubrectBaseX, frame.DepthSubrectBaseY, frame.MotionSubrectBaseX, frame.MotionSubrectBaseY);
    if (!regions.depth.valid() || !regions.motion.valid()) return say("DLSS enlargement needs valid depth and motion.");
    lifetime.Record(cmd);
    g.lifetime.Record(cmd);
    DlssNrConstants encode {}; encode.Mode = DlssNrMode_EncodeProxyResidual;
    encode.Width = w; encode.Height = h; encode.Passthrough = resolve.Passthrough;
    bool ok = shader.DispatchPass(cmd, encode, proxy, answer, nullptr, nullptr, nullptr, g.input.Get(), nullptr);
    DlssNrConstants guides {}; guides.Mode = DlssNrMode_ResizePrivateGuides;
    guides.Width = w; guides.Height = h;
    guides.GuideWidth = regions.depth.width; guides.GuideHeight = regions.depth.height;
    guides.DebugView = regions.depth.x; guides.CompareMode = regions.depth.y;
    guides.TransferStrength = float(regions.motion.width); guides.ColourStrength = float(regions.motion.height);
    guides.CompareSwap = regions.motion.x; guides.Transfer = regions.motion.y;
    const auto referenceW = frame.MotionVectorsLowResolution ? frame.RenderSubrectWidth : frame.OutputWidth;
    const auto referenceH = frame.MotionVectorsLowResolution ? frame.RenderSubrectHeight : frame.OutputHeight;
    guides.MvScaleX = frame.MvScaleX * float(w) / std::max(referenceW ? referenceW : regions.motion.width, 1u);
    guides.MvScaleY = frame.MvScaleY * float(h) / std::max(referenceH ? referenceH : regions.motion.height, 1u);
    if (Config::Instance()->DlssNrHoldFrame.value_or_default()) guides.MvScaleX = guides.MvScaleY = 0;
    ok &= shader.DispatchPass(cmd, guides, depth, motion, nullptr, nullptr, nullptr, g.depth.Get(), g.motion.Get());
    if (!ok) { g.reset = true; return say("DLSS enlargement guide/carrier preparation failed."); }
    for (auto* r : { g.input.Get(), g.depth.Get(), g.motion.Get() })
        Barrier(cmd, r, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (g.readable) Barrier(cmd, g.output.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DlssNr::PrivateUpscalerFrameDx12 f;
    f.color.resource = g.input.Get(); f.depth.resource = g.depth.Get(); f.motion.resource = g.motion.Get();
    f.exposure.resource = g.exposure.Get(); f.output.resource = g.output.Get();
    f.width = w; f.height = h; f.outputWidth = g.outW; f.outputHeight = g.outH;
    f.reset = reset || frame.Reset || g.reset || frames < g.lastFrame || frames > g.lastFrame + 1;
    // Resampled motion already measures pixels at the private input resolution.
    f.motionScaleX = f.motionScaleY = 1;
    // Post-upscale and finished-picture colour has already been de-jittered by the game.
    f.jitterX = f.jitterY = 0;
    f.frameTimeMs = std::isfinite(frame.FrameTimeMs) && frame.FrameTimeMs > 0 ? frame.FrameTimeMs : 16.67f;
    ok = g.dlss->Evaluate(cmd, f);
    if (ok && g.lastFrame == 0)
        LOG_INFO("NR matched residual: first private DLSS SR evaluation succeeded on producer queue {}",
                 (void*)g.queue.Get());
    for (auto* r : { g.input.Get(), g.depth.Get(), g.motion.Get() })
        Barrier(cmd, r, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Barrier(cmd, g.output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    g.readable = true; g.lastFrame = frames; g.reset = !ok;
    if (!ok) { g.failed = true; return say("Private DLSS SR: " + g.dlss->Error()); }
    enlargementStatus.clear();
    return g.output.Get();
}
