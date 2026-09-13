#include "pch.h"
#include "DlssNr_Dx12_State.h"

auto DlssNr_Dx12::State::DeferredSrContext::Say(const std::string& text) -> void
{
    if (status == text)
        return;
    status = text;
    LOG_INFO("DLSS-NR deferred upscale: {}", text);
}

auto DlssNr_Dx12::State::DeferredSrContext::Cancel() -> void
{
    pending = {};
    if (current)
        current->reset = true;
}

auto DlssNr_Dx12::State::DeferredSrContext::Collect() -> void
{
    std::erase_if(retired, [](const auto& g) { return g->Idle(); });
}

auto DlssNr_Dx12::State::DeferredSrContext::UInt(NVSDK_NGX_Parameter* p, const char* key, unsigned fallback) -> unsigned
{
    unsigned value = fallback;
    p->Get(key, &value);
    return value;
}

auto DlssNr_Dx12::State::DeferredSrContext::Float(NVSDK_NGX_Parameter* p, const char* key, float fallback) -> float
{
    float value = fallback;
    p->Get(key, &value);
    return std::isfinite(value) ? value : fallback;
}

auto DlssNr_Dx12::State::DeferredSrContext::Allocate(Generation& g) -> bool
{
    g.edited = owner.CreateScratch(g.device, g.inputFormat, g.w, g.h);
    g.residualInput = owner.CreateScratch(g.device, DXGI_FORMAT_R16G16B16A16_FLOAT, g.w, g.h);
    g.residualOutput = owner.CreateScratch(g.device, DXGI_FORMAT_R16G16B16A16_FLOAT, g.outW, g.outH);
    g.clean = owner.CreateScratch(g.device, g.outputFormat, g.outW, g.outH);
    g.composed = owner.CreateScratch(g.device, g.outputFormat, g.outW, g.outH);
    g.exposure = owner.CreateScratch(g.device, DXGI_FORMAT_R32_FLOAT, 1, 1);
    if (!g.edited || !g.residualInput || !g.residualOutput || !g.clean || !g.composed || !g.exposure)
        return false;

    g.codec = std::make_unique<DlssNr_Dx12>("Deferred NR contribution", g.device);
    if (!g.codec->IsInit())
        return false;
    D3D12_QUERY_HEAP_DESC query {};
    query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query.Count = MarkerCount;
    if (FAILED(g.device->CreateQueryHeap(&query, IID_PPV_ARGS(&g.queries))))
        return false;
    auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(MarkerCount * sizeof(UINT64));
    if (FAILED(g.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&g.readback))))
        return false;
    void* mapped = nullptr;
    if (FAILED(g.readback->Map(0, nullptr, &mapped)))
        return false;
    g.completed = static_cast<volatile UINT64*>(mapped);
    for (unsigned i = 0; i < MarkerCount; ++i)
        g.completed[i] = 0;
    return true;
}

auto DlssNr_Dx12::State::DeferredSrContext::Before(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* source, unsigned long long epoch,
                    unsigned long long submittedEpoch, ID3D12CommandQueue* queue, bool interop, bool rayReconstruction) -> void
{
    if (pending.cmd && current)
    {
        LOG_DEBUG(
            "DLSS-NR deferred: Before entry with a stale pending (previous After never ran) -> reset. epoch {}",
            epoch);
        current->reset = true; // abandoned/failed main SR call
    }
    pending = {};
    struct ResetOnGap
    {
        DeferredSrContext& state;
        unsigned long long epoch;
        ~ResetOnGap()
        {
            if (!state.pending.cmd && state.current)
            {
                LOG_DEBUG("DLSS-NR deferred: Before returned without arming a seam; reset history. epoch {}",
                          epoch);
                state.current->reset = true;
            }
        }
    } resetOnGap { *this, epoch };
    Collect();
    const auto& cfg = *Config::Instance();
    if (cfg.DlssNrDebugView.value_or_default() != 0 ||
        cfg.DlssNrCompare.value_or_default() != 0 || cfg.DlssNrShowSkinMask.value_or_default() ||
        (!cfg.DlssNrApplyModel.value_or_default() && !cfg.DlssNrFinishedPicture.value_or_default()))
    {
        Say("Disable Debug/Compare and enable Apply model.");
        return;
    }
    if (cmd->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT ||
        ((cfg.RestoreComputeSignature.value_or_default() || cfg.RestoreGraphicSignature.value_or_default()) &&
         !D3D12Hooks::CanRestoreRootSignature(cmd)))
    {
        Say("inactive: requires a direct command list with restorable game state");
        return;
    }
    auto* color = owner.GetResource(source, NVSDK_NGX_Parameter_Color, "DLSSD.Color");
    auto* output = owner.GetResource(source, NVSDK_NGX_Parameter_Output, "DLSSD.Output");
    auto* depth = owner.GetResource(source, NVSDK_NGX_Parameter_Depth, "DLSSD.Depth");
    auto* motion = owner.GetResource(source, NVSDK_NGX_Parameter_MotionVectors, "DLSSD.MotionVectors");
    if (!color || !output || !depth || !motion || color == output)
    {
        Say("inactive: distinct Color/Output, depth and motion are required");
        return;
    }
    for (const char* key :
         { NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,
           NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,
           NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X,
           NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X,
           NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X,
           NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y })
        if (UInt(source, key) != 0)
        {
            Say("inactive: non-zero colour/guide/output offsets");
            return;
        }
    const auto inDesc = color->GetDesc(), outDesc = output->GetDesc();
    const auto active =
        DlssNr::PreSrColorExtent(inDesc, UInt(source, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width),
                                 UInt(source, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height));
    if (!active || !DlssNr::PreSrColorExtent(outDesc, 0, 0) || inDesc.MipLevels != 1 ||
        outDesc.MipLevels != 1 || active->width > outDesc.Width || active->height > outDesc.Height)
    {
        Say("inactive: unsupported active input/output dimensions");
        return;
    }
    ID3D12Device* device = nullptr;
    if (FAILED(cmd->GetDevice(IID_PPV_ARGS(&device))))
        return;
    auto* ownerQueue = queue ? queue : (ID3D12CommandQueue*) ::State::Instance().currentCommandQueue;
    ID3D12Device* queueDevice = nullptr;
    if (!ownerQueue || ownerQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
        FAILED(ownerQueue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice != device)
    {
        if (queueDevice)
            queueDevice->Release();
        device->Release();
        Say("waiting for a same-device direct queue identity");
        return;
    }
    queueDevice->Release();
    unsigned flags = (owner.featureFlags ? owner.featureFlags
                                         : UInt(source, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags)) &
                     (NVSDK_NGX_DLSS_Feature_Flags_DepthInverted | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                      NVSDK_NGX_DLSS_Feature_Flags_MVJittered);
    const auto backend = DlssNr::GetPrivateUpscaler(cfg.DlssNrPrivateUpscaler.value_or_default());
    // Only native DX12 RR parameters contain DX12 material/reflection guides. Bridges
    // do not transfer these resources, so keep their existing private SR backend.
    auto rrInputs = DlssNr::PrivateRrInputsDx12 {};
    const bool privateRr = rrInputs.valid;
    if (current && (current->rayReconstruction != rayReconstruction ||
                    current->privateRr != privateRr ||
                    (privateRr && (current->frame.rr.roughnessMode != rrInputs.roughnessMode ||
                                   current->frame.rr.hardwareDepth != rrInputs.hardwareDepth)) ||
                    current->finishedPicture != cfg.DlssNrFinishedPicture.value_or_default() ||
                    current->backend != backend || current->device != device || current->queue != ownerQueue ||
                    current->w != active->width ||
                    current->h != active->height || current->outW != outDesc.Width ||
                    current->outH != outDesc.Height || current->inputFormat != inDesc.Format ||
                    current->outputFormat != outDesc.Format || current->flags != flags))
    {
        owner.late.Cancel();
        retired.push_back(std::move(current));
    }
    if (!current)
    {
        if (retired.size() >= 4)
        {
            device->Release();
            Say("waiting for retired GPU work; clean SR frame retained");
            return;
        }
        current = std::make_unique<Generation>();
        current->device = device; // take the GetDevice reference
        current->queue = ownerQueue;
        ownerQueue->AddRef();
        current->w = active->width;
        current->h = active->height;
        current->outW = (unsigned) outDesc.Width;
        current->outH = outDesc.Height;
        current->inputFormat = inDesc.Format;
        current->outputFormat = outDesc.Format;
        current->flags = flags;
        current->backend = backend;
        current->rayReconstruction = rayReconstruction;
        current->privateRr = privateRr;
        if (backend == DlssNr::PrivateUpscaler::DLSS)
            LOG_INFO("DLSS-NR private residual upscaler: {} ({})", privateRr ? "DLSS RR" : "DLSS SR",
                     privateRr ? "game RR guides available" : "game RR guides unavailable for this API/extent");
        current->finishedPicture = cfg.DlssNrFinishedPicture.value_or_default();
        if (!Allocate(*current))
        {
            current->failed = true;
            Say("allocation failed; clean SR frame retained");
            return;
        }
    }
    else
        device->Release();
    auto& g = *current;
    g.frame.rr = rrInputs; // Own matrix values before the game's evaluate can rewrite its parameter table.
    // These guides arrive in the game's NGX readable state. Account for aliases of the basic inputs.
    const auto rrStates = DlssNr::ResolveInputStates_Dx12(interop);
    for (auto& guide : g.frame.rr.guides)
    {
        if (guide.resource == color) guide.state = rrStates.color;
        else if (guide.resource == depth) guide.state = rrStates.depth;
        else if (guide.resource == motion) guide.state = rrStates.motion;
    }
    if (g.failed)
        return;
    // Native seams have a logical per-evaluate identity. Bridges retain the submitted epoch
    // so a second upscale in the same bridge submission is still rejected.
    if (g.began && g.lastBeginEpoch == epoch)
    {
        g.reset = true;
        Say("inactive: more than one upscale in a submission epoch");
        return;
    }
    g.began = true;
    g.lastBeginEpoch = epoch;
    owner.lifetime.Record(cmd);
    Use use(g, cmd);
    if (!use.valid)
    {
        Say("waiting for GPU completion slots; clean SR frame retained");
        return;
    }
    if (!g.upscaler)
    {
        ScopedNrStateEnvelope envelope(cmd);
        DlssNr::PrivateUpscalerCreateDx12 info {};
        info.width = g.w;
        info.height = g.h;
        info.outputWidth = g.outW;
        info.outputHeight = g.outH;
        info.quality = (int) UInt(source, NVSDK_NGX_Parameter_PerfQualityValue,
                                  NVSDK_NGX_PerfQuality_Value_MaxPerf);
        info.depthInverted = (g.flags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0;
        info.jitteredMotion = (g.flags & NVSDK_NGX_DLSS_Feature_Flags_MVJittered) != 0;
        info.lowResolutionMotion = (g.flags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) != 0;
        info.rayReconstruction = g.privateRr;
        info.roughnessMode = g.frame.rr.roughnessMode;
        info.hardwareDepth = g.frame.rr.hardwareDepth;
        g.upscaler = std::make_unique<DlssNr::PrivateUpscalerDx12>(g.backend);
        LOG_INFO("DLSS-NR private creation: {} {}x{} -> {}x{}, quality {}, inverted {}, jittered MV {}, low-res MV {}, roughness {}, HW depth {}",
                 g.privateRr ? "DLSS RR" : DlssNr::PrivateUpscalerName(g.backend),
                 info.width, info.height, info.outputWidth, info.outputHeight, info.quality,
                 info.depthInverted, info.jitteredMotion, info.lowResolutionMotion, info.roughnessMode, info.hardwareDepth);
        if (!g.upscaler->Init(g.device, cmd, info))
        {
            g.failed = true;
            Say(std::string("private ") + g.upscaler->Name() +
                " creation failed: " + g.upscaler->Error() + "; clean SR frame retained");
            return;
        }
        DlssNrConstants unit {};
        unit.Mode = DlssNrMode_UnitExposure;
        unit.Width = unit.Height = 1;
        if (!g.codec->DispatchPass(cmd, unit, g.edited, nullptr, nullptr, nullptr, nullptr, g.exposure,
                                   nullptr))
        {
            g.failed = true;
            Say("private exposure initialization failed");
            return;
        }
        owner.Barrier(cmd, g.exposure, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g.createEpoch = submittedEpoch;
        Say(std::string("private ") + g.upscaler->Name() +
            " created; waiting for a later submission epoch");
        return;
    }
    // Synthetic seam ticks cannot prove that a feature's creation commands were submitted.
    if (submittedEpoch == g.createEpoch)
    {
        LOG_DEBUG("DLSS-NR deferred: waiting after feature creation at submitted epoch {}", submittedEpoch);
        return;
    }

    const auto inputStates = DlssNr::ResolveInputStates_Dx12(interop);
    const auto arrival = inputStates.color;
    owner.Barrier(cmd, color, arrival, D3D12_RESOURCE_STATE_COPY_SOURCE);
    owner.Barrier(cmd, g.edited, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    CopyActiveColor(cmd, g.edited, color, *active);
    owner.Barrier(cmd, color, D3D12_RESOURCE_STATE_COPY_SOURCE, arrival);
    owner.Barrier(cmd, g.edited, D3D12_RESOURCE_STATE_COPY_DEST,
                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    DlssNrFrameInfo frame {};
    frame.BeforeUpscale = frame.PrivateColorCopy = true;
    frame.RayReconstruction = rayReconstruction;
    frame.SubmissionEpoch = submittedEpoch;
    frame.RenderSubrectWidth = g.w;
    frame.RenderSubrectHeight = g.h;
    frame.OutputWidth = g.outW;
    frame.OutputHeight = g.outH;
    frame.MotionVectorsLowResolution = (flags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) != 0;
    frame.DepthInverted = (flags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0;
    frame.ColourIsLinearHdr =
        ((owner.featureFlags ? owner.featureFlags
                             : UInt(source, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags)) &
         NVSDK_NGX_DLSS_Feature_Flags_IsHDR) != 0 &&
        owner.FormatCanHoldLinearHdr(outDesc.Format);
    frame.Reset = UInt(source, NVSDK_NGX_Parameter_Reset) != 0 || g.reset;
    frame.MvScaleX = Float(source, NVSDK_NGX_Parameter_MV_Scale_X, 1);
    frame.MvScaleY = Float(source, NVSDK_NGX_Parameter_MV_Scale_Y, 1);
    frame.PreExposure = std::max(Float(source, NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1), 1e-4f);
    frame.ExposureTexture = owner.GetResource(source, NVSDK_NGX_Parameter_ExposureTexture, "ExposureTexture");
    owner.nr.exposureOfferedNow = frame.ExposureTexture != nullptr;
    owner.nr.exposureEverOffered = owner.nr.exposureEverOffered || owner.nr.exposureOfferedNow;
    ++owner.nr.exposureFrames;
    const auto before = owner.nr.successfulDispatches;
    {
        // Run consumes readable guides; restore their arrival states even when NR declines the frame.
        struct RestoreGuides
        {
            State& owner;
            ID3D12GraphicsCommandList* cmd;
            std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> resources;
            void Read(ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
            {
                if (!resource || std::any_of(resources.begin(), resources.end(),
                                             [resource](const auto& entry) { return entry.first == resource; }))
                    return;
                resources.emplace_back(resource, state);
                owner.Barrier(cmd, resource, state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            ~RestoreGuides()
            {
                for (auto it = resources.rbegin(); it != resources.rend(); ++it)
                    owner.Barrier(cmd, it->first, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, it->second);
            }
        } restore { owner, cmd };
        auto read = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
        {
            // A guide may alias Color. The game Color has already returned to its colour arrival state;
            // the private edited colour is readable and Run preserves it independently.
            if (resource != g.edited)
                restore.Read(resource, resource == color ? arrival : state);
        };
        read(depth, inputStates.depth);
        read(motion, inputStates.motion);
        read(static_cast<ID3D12Resource*>(frame.ExposureTexture), inputStates.exposure);
        owner.Run(cmd, g.edited, depth, motion, g.edited, frame, queue);
    }
    const bool evaluated = owner.nr.successfulDispatches != before;
    if (evaluated)
    {
        ScopedNrStateEnvelope envelope(cmd);
        if (g.smallReadable)
            owner.Barrier(cmd, g.residualInput, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        owner.Barrier(cmd, color, arrival, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        bool accumulated = true;

        DlssNrConstants encode {};
        encode.Mode = DlssNrMode_EncodeResidual;
        encode.Width = g.w;
        encode.Height = g.h;
        encode.ExposurePreMul = frame.PreExposure;
        bool ok;
        if (!accumulated)
        {
            ok = false;
            g.reset = true;
            Say("waiting for RR residual accumulation; clean game frame retained");
        }
        else if (cfg.DlssNrFinishedPicture.value_or_default())
        {
            encode.Mode = 5; // finished-colour shader: encode relative changes before FP16 storage
            encode.WhitePoint = frame.PreExposure;
            encode.TransferStrength = frame.ColourIsLinearHdr ? 1.0f : 0.0f;
            encode.MaxRatio = std::clamp(cfg.DlssNrMaxRatio.value_or_default(), 1.0f, 8.0f);
            ok = g.codec->DispatchResidualPass(cmd, encode, color, g.edited, nullptr, nullptr, g.residualInput,
                                               true);
        }
        else
            ok = g.codec->DispatchPass(cmd, encode, color, g.edited, nullptr, nullptr, nullptr, g.residualInput,
                                       nullptr);
        owner.Barrier(cmd, color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, arrival);
        owner.Barrier(cmd, g.residualInput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g.smallReadable = true;
        if (ok)
        {
            // Snapshot before the main SR call can rewrite its parameter table.
            auto& f = g.frame;
            f.color = { g.residualInput, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
            f.output = { g.residualOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
            f.depth = { depth, depth == color ? arrival : inputStates.depth };
            f.motion = { motion, motion == color ? arrival : inputStates.motion };
            f.exposure = { g.exposure, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
            f.width = g.w;
            f.height = g.h;
            f.outputWidth = g.outW;
            f.outputHeight = g.outH;
            f.reset = frame.Reset || g.reset;
            f.jitterX = Float(source, NVSDK_NGX_Parameter_Jitter_Offset_X, 0);
            f.jitterY = Float(source, NVSDK_NGX_Parameter_Jitter_Offset_Y, 0);
            f.motionScaleX = frame.MvScaleX;
            f.motionScaleY = frame.MvScaleY;
            f.frameTimeMs = Float(source, NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, 16.67f);
            float nearPlane = cfg.FsrCameraNear.value_or_default();
            float farPlane = cfg.FsrCameraFar.value_or_default();
            if (frame.DepthInverted)
                std::swap(nearPlane, farPlane);
            f.cameraNear = Float(source, "FSR.cameraNear", nearPlane);
            f.cameraFar = Float(source, "FSR.cameraFar", farPlane);
            constexpr float radians = 0.01745329252f;
            const float fov = cfg.FsrVerticalFov.has_value() ? cfg.FsrVerticalFov.value() * radians
                : cfg.FsrHorizontalFov.value_or_default() > 0.0f
                    ? 2.0f * std::atan(std::tan(cfg.FsrHorizontalFov.value() * radians * 0.5f) *
                                       (float) g.outH / g.outW)
                    : 60.0f * radians;
            f.cameraFovVertical = Float(source, OptiKeys::FSR_CameraFovVertical, fov);
            f.viewSpaceToMeters = Float(source, "FSR.viewSpaceToMetersFactor", 1.0f);
            pending = { cmd, source, output, frame.PreExposure };
            LOG_DEBUG("DLSS-NR deferred: Before armed epoch {} preExp {:.4f} reset-carried {}", epoch,
                      frame.PreExposure, frame.Reset);
        }
    }
    else
    {
        g.reset = true;
        Say("waiting for NR evaluation; clean SR frame retained");
    }
    owner.Barrier(cmd, g.edited, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

auto DlssNr_Dx12::State::DeferredSrContext::After(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* source, unsigned long long epoch) -> void
{
    const auto pair = pending;
    pending = {}; // Consume only the immediately matching successful upscale.
    if (!current || current->failed || pair.cmd != cmd || pair.caller != source ||
        pair.output != owner.GetResource(source, NVSDK_NGX_Parameter_Output, "DLSSD.Output"))
    {
        if (current)
            current->reset = true;
        return;
    }
    auto& g = *current;
    const auto& cfg = *Config::Instance();
    if ((cfg.RestoreComputeSignature.value_or_default() || cfg.RestoreGraphicSignature.value_or_default()) &&
        !D3D12Hooks::CanRestoreRootSignature(cmd))
    {
        g.reset = true;
        Say("inactive: game state cannot be restored after SR");
        return;
    }
    owner.lifetime.Record(cmd);
    Use use(g, cmd);
    if (!use.valid)
    {
        g.reset = true;
        Say("waiting for GPU completion slots; clean SR frame retained");
        return;
    }
    ScopedNrStateEnvelope envelope(cmd);
    if (!g.upscaler->Evaluate(cmd, g.frame))
    {
        g.failed = true;
        Say(std::string("private ") + g.upscaler->Name() +
            " evaluation failed: " + g.upscaler->Error() + "; clean SR frame retained");
        return;
    }
    LOG_TRACE("DLSS-NR deferred: applied current-frame contribution at epoch {} (reset {})", epoch, g.reset);
    g.reset = false;
    if (cfg.DlssNrFinishedPicture.value_or_default())
    {
        const bool sceneLinear =
            ((owner.featureFlags ? owner.featureFlags
                                 : UInt(source, NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags)) &
             NVSDK_NGX_DLSS_Feature_Flags_IsHDR) != 0 &&
            owner.FormatCanHoldLinearHdr(g.outputFormat);
        if (owner.late.CaptureResidual(cmd, pair.output, g.residualOutput, pair.scale, sceneLinear,
                                       UInt(source, NVSDK_NGX_Parameter_Reset) != 0))
            Say(std::string("running: model -> private ") + g.upscaler->Name() + "; changes saved for the finished picture");
        else
        {
            g.reset = true;
            Say("waiting to save the upscaled changes for the finished picture");
        }
        return; // The finished-picture path owns composition; the game's SR output stays clean.
    }

    owner.Barrier(cmd, g.residualOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const auto arrival = cfg.OutputResourceBarrier.has_value()
                             ? (D3D12_RESOURCE_STATES) cfg.OutputResourceBarrier.value()
                             : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    owner.Barrier(cmd, pair.output, arrival, D3D12_RESOURCE_STATE_COPY_SOURCE);
    owner.Barrier(cmd, g.clean, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyResource(g.clean, pair.output);
    owner.Barrier(cmd, g.clean, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    DlssNrConstants apply {};
    apply.Mode = DlssNrMode_ApplyResidual;
    apply.Width = g.outW;
    apply.Height = g.outH;
    apply.ExposurePreMul = pair.scale;
    const bool ok = g.codec->DispatchPass(cmd, apply, g.clean, g.residualOutput, nullptr, nullptr, nullptr,
                                          g.composed, nullptr);
    if (ok)
    {
        owner.Barrier(cmd, g.composed, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        owner.Barrier(cmd, pair.output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(pair.output, g.composed);
        owner.Barrier(cmd, pair.output, D3D12_RESOURCE_STATE_COPY_DEST, arrival);
        owner.Barrier(cmd, g.composed, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Say("running: " + std::to_string(g.w) + "x" + std::to_string(g.h) +
            " contribution -> private " + g.upscaler->Name() + " -> " +
            std::to_string(g.outW) + "x" + std::to_string(g.outH) +
            "; applied after SR");
    }
    else
    {
        owner.Barrier(cmd, pair.output, D3D12_RESOURCE_STATE_COPY_SOURCE, arrival);
        g.reset = true;
        Say("composition failed; clean frame retained");
    }
    owner.Barrier(cmd, g.clean, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    owner.Barrier(cmd, g.residualOutput, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

auto DlssNr_Dx12::State::DeferredSrContext::ReleaseResources() -> void
{
    Cancel();
    if (owner.lifetime.Idle())
    {
        // Also covers discarded recordings whose GPU timestamp was never written.
        current.reset();
        retired.clear();
        return;
    }
    if (current)
        retired.push_back(std::move(current));
    Collect();
    // Never free feature histories, descriptors or surfaces referenced by an unsubmitted/in-flight
    // list. At shutdown only, retain uncompleted generations for process teardown rather than UAF.
    for (auto& g : retired)
        (void) g.release();
    retired.clear();
}
