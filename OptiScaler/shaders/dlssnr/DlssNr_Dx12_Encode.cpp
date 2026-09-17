#include "pch.h"
#include "DlssNr_Dx12_State.h"

void DlssNr_Dx12::State::EncodeInput(EncodeContext& context)
{
    const auto& cfg = *Config::Instance();
    const auto& frame = context.frame;
    auto* cmdList = context.cmdList;
    auto* device = context.device;
    auto* target = context.target;
    auto& targetState = context.targetState;
    auto& whitePoint = context.whitePoint;
    auto& exposureTex = context.exposureTex;
    auto& useGameExposure = context.useGameExposure;
    auto& exposurePreMul = context.exposurePreMul;
    auto& modelInput = context.modelInput;
    const auto width = nr.width, height = nr.height;
    const auto workWidth = nr.workWidth, workHeight = nr.workHeight;
    const auto workScale = context.workScale;
    const bool targetSupportsUav = context.targetSupportsUav;
    const bool reduced = workWidth != width || workHeight != height;
    const bool isHdrBuffer = frame.ColourIsLinearHdr;
    const auto TransitionTarget = [&](D3D12_RESOURCE_STATES to)
    {
        Barrier(cmdList, target, targetState, to);
        targetState = to;
    };
    // Fetch the game's exposure, where the game supplies one and the user asked for it.
    //
    // This used to measure the white point off the frame as well, over a 64x64 grid of tile
    // luminances. That is gone: the pass writes the frame it was measuring, so the divisor chased its
    // own output -- one Enshrouded session walked it from 0.010 to 97.910, and toggling NR at a fixed
    // spot read 41.31 off against 0.46 on. What remains dispatches a single thread to copy the game's
    // 1x1 exposure texture into tile 0. That is a courier, not a measurement, and cannot feed back.
    // Gated on the source the menu actually writes. This read the retired WhitePointFromExposure
    // flag while consumption keyed on WhitePointSource == 1, so choosing "the game's own exposure"
    // never dispatched the meter and the white point silently fell back to the slider.
    const bool exposureSettingOn = cfg.DlssNrWhitePointSource.value_or_default() == 1;

    // Nothing held from before the option was switched off may survive switching it back on. See
    // InvalidateExposureMeter for what froze and why it read as a colour cast.
    if (exposureSettingOn && !nr.exposureSettingWasOn)
    {
        InvalidateExposureMeter();
        LOG_INFO("DLSS-NR exposure: option switched on, held reading discarded");
    }

    nr.exposureSettingWasOn = exposureSettingOn;

    const bool wantExposure = exposureSettingOn && frame.ExposureTexture != nullptr;

    if (nr.meter != nullptr && wantExposure)
    {
        DlssNrConstants meterParams {};
        meterParams.Mode = DlssNrMode_Meter;

        // One pixel. Only tile (0,0) is read back, and the tile-mean branch below it in the shader is
        // dead code the dispatch simply never reaches.
        meterParams.Width = 1;
        meterParams.Height = 1;

        const D3D12_RESOURCE_STATES priorTargetState = targetState;
        TransitionTarget(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        shader.DispatchPass(cmdList, meterParams, target, nullptr, nullptr, (ID3D12Resource*) frame.ExposureTexture,
                            nullptr, nr.meter, nullptr);
        TransitionTarget(priorTargetState);

        CopyMeterToReadback(cmdList, device, true);
        ConsumeMeterReadback();
    }

    nr.gamePreExposure = frame.PreExposure;

    whitePoint =
        frame.WhitePointOverride > 0.0f ? frame.WhitePointOverride : ResolveWhitePoint(cfg, isHdrBuffer);

    // Zero-latency exposure (D3D12, source 1): when the game hands us a live exposure texture, the
    // white point is recomputed in-shader every frame from it (ExposurePreMul / exposure) instead of
    // the 3-4 frame CPU meter readback. whitePoint above still rides along in gWhitePoint as the
    // fallback the shader uses if the live sample is missing or absurd. Bound at t4 (InPrevEdit) below.



    if (cfg.DlssNrWhitePointSource.value_or_default() == 1 && frame.ExposureTexture != nullptr)
    {
        exposureTex = (ID3D12Resource*) frame.ExposureTexture;
        useGameExposure = 1;
        const float trim = std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);
        exposurePreMul = nr.gamePreExposure * trim;
    }

    // Frame hold. Freeze the encode's input so a live setting change re-renders the same frame. This
    // is self-contained on purpose: it copies the output aside on hold-on and copies it BACK over the
    // live output before the encode reads it while held, so the encode's own path and barriers below
    // are untouched and the default (hold off) is byte-identical. See design/frame-hold.md.
    //
    // `target` is UAV here (normalised at entry, restored by the meter block above). The held copy is
    // left in COPY_SOURCE after capture and stays there for every restore.
    {
        const bool hold = cfg.DlssNrHoldFrame.value_or_default();

        if (hold)
        {
            const D3D12_RESOURCE_DESC td = target->GetDesc();
            const bool needCapture = !nr.heldActive || nr.heldColor == nullptr ||
                                     (unsigned int) td.Width != nr.heldWidth || td.Height != nr.heldHeight ||
                                     td.Format != nr.heldFormat;

            if (needCapture)
            {
                // Hold-on (or the output changed shape under a hold): capture THIS frame, do not
                // restore -- target already holds the frame to freeze, and the pass runs on it.
                if (nr.heldColor != nullptr)
                    ParkNrResource(nr.heldColor);

                nr.heldColor = CreateScratch(device, td.Format, (unsigned int) td.Width, td.Height);

                if (nr.heldColor != nullptr)
                {
                    const D3D12_RESOURCE_STATES priorTargetState = targetState;
                    TransitionTarget(D3D12_RESOURCE_STATE_COPY_SOURCE);
                    Barrier(cmdList, nr.heldColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COPY_DEST);
                    cmdList->CopyResource(nr.heldColor, target);
                    Barrier(cmdList, nr.heldColor, D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_STATE_COPY_SOURCE);
                    TransitionTarget(priorTargetState);

                    nr.heldActive = true;
                    nr.heldWidth = (unsigned int) td.Width;
                    nr.heldHeight = td.Height;
                    nr.heldFormat = td.Format;
                    nr.heldWhitePoint = whitePoint;
                }
            }
            else
            {
                // Held: restore the frozen frame onto the live output before the encode reads it.
                const D3D12_RESOURCE_STATES priorTargetState = targetState;
                TransitionTarget(D3D12_RESOURCE_STATE_COPY_DEST);
                cmdList->CopyResource(target, nr.heldColor);
                TransitionTarget(priorTargetState);
            }

            // Suspend white-point measurement while held: use the snapshot so it cannot drift and
            // confound the comparison. (No-op on the capture frame, where the snapshot IS whitePoint.)
            if (nr.heldActive)
            {
                whitePoint = nr.heldWhitePoint;
                useGameExposure = 0;
                exposureTex = nullptr;
            }
        }
        else if (nr.heldActive)
        {
            // Released: let go of the frozen frame and resume live input next frame.
            if (nr.heldColor != nullptr)
                ParkNrResource(nr.heldColor);
            nr.heldActive = false;
        }
    }

    DlssNrConstants encodeParams {};
    encodeParams.Mode = DlssNrMode_Encode;
    // A frame that is already display-referred is handed over untouched: the encode becomes a copy and
    // the resolve adds the model's edit back at full scale.
    encodeParams.Passthrough = isHdrBuffer ? 0u : 1u;
    encodeParams.WhitePoint = whitePoint;
    encodeParams.UseGameExposure = useGameExposure;
    encodeParams.ExposurePreMul = exposurePreMul;
    encodeParams.ReversibleMode = cfg.DlssNrReversibleMode.value_or_default();
    // Match only takes effect once a fit exists; until then the table is empty and the shader would
    // read a curve of zeros, so it falls back to the plain proxy.
    encodeParams.Width = width;
    encodeParams.Height = height;

    TransitionTarget(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    shader.DispatchPass(cmdList, encodeParams, target, nullptr, nullptr, nullptr, exposureTex, nr.colorCopy,
                        nr.hdrCopy);

    if (targetSupportsUav)
        TransitionTarget(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // The transitions double as the wait for the encode's writes.
    Barrier(cmdList, nr.colorCopy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // Measure the buffer's scale from the copy the encode just kept -- untouched, so there is no path
    // (Calibration pass removed: it produced only a menu suggestion nothing consumed, at the cost
    // of a 4096-thread dispatch, a readback and an nth_element every frame.)

    Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Below full resolution the model is shown a filtered shrink of the proxy; the edit it returns is
    // enlarged during the resolve while the frame underneath stays full size and untouched.
    modelInput = nr.colorCopy;

    if (reduced && nr.colorSmall != nullptr)
    {
        bool built = false;

        if (workScale > 1.0f)
        {
            // Supersample: enlarge the proxy to the larger working size with a real upscaling filter
            // (the Output Scaling upsampler) so the model sees a clean super-native input, rather than
            // the box minifier which only makes sense going down. colorCopy is NON_PIXEL_SHADER_RESOURCE
            // from the encode (SRV-ready); colorSmall is UNORDERED_ACCESS from last frame's resolve.
            // (Re)build the supersample scalers when missing or when the NR downscaler changed (the
            // filter is baked at construction). Both use NR's own DlssNrScalingDownscaler, independent
            // of Output Scaling, so the two can run different filters at once. superDown is built here
            // and used after the model (the down-leg below).
            const Scaler nrScaler = cfg.DlssNrScalingDownscaler.value_or_default();
            if (nr.nrScaler != nrScaler)
            {
                if (nr.superUp != nullptr)
                {
                    lifetime.Retire([up = nr.superUp] { delete up; });
                    nr.superUp = nullptr;
                }
                if (nr.superDown != nullptr)
                {
                    lifetime.Retire([down = nr.superDown] { delete down; });
                    nr.superDown = nullptr;
                }
                nr.nrScaler = nrScaler;
            }
            if (nr.superUp == nullptr)
                nr.superUp = new OS_Dx12("DLSS-NR supersample up", device, true, nrScaler);
            if (nr.superDown == nullptr)
                nr.superDown = new OS_Dx12("DLSS-NR supersample down", device, false, nrScaler);

            if (nr.superUp != nullptr && nr.superUp->DispatchResources(cmdList, nr.colorCopy, nr.colorSmall))
            {
                Barrier(cmdList, nr.colorSmall, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                built = true;
            }
        }

        if (!built)
        {
            if (workScale > 1.0f)
            {
                // Wanted to supersample but the upscaler was not available -- warn once; the box path
                // below can only enlarge blockily, so the user should know the clean path is off.

                if (!warnedSuper)
                {
                    warnedSuper = true;
                    LOG_WARN("DLSS-NR supersample: upscaler unavailable, falling back to a blocky enlarge.");
                }
            }

            // Sub-native (or the upsampler could not be built): box-resample the proxy to the work size.
            DlssNrConstants down {};
            down.Mode = DlssNrMode_Downsample;
            down.Width = workWidth;
            down.Height = workHeight;
            shader.DispatchPass(cmdList, down, modelInput, nullptr, nullptr, nullptr, nullptr, nr.colorSmall,
                                nullptr);
            Barrier(cmdList, nr.colorSmall, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        modelInput = nr.colorSmall;
    }

}

DlssNrConstants DlssNr_Dx12::State::MakeResolveConstants(const EncodeContext& context, unsigned int effectivePasses)
{
    const auto& cfg = *Config::Instance();
    const auto whitePoint = context.whitePoint;
    const auto useGameExposure = context.useGameExposure;
    const auto exposurePreMul = context.exposurePreMul;
    const auto width = nr.width, height = nr.height;
    const bool isHdrBuffer = context.frame.ColourIsLinearHdr;
    DlssNrConstants resolveParams {};
    resolveParams.Mode = DlssNrMode_Resolve;
    resolveParams.WhitePoint = whitePoint;
    resolveParams.UseGameExposure = useGameExposure;
    resolveParams.ExposurePreMul = exposurePreMul;
    resolveParams.Width = width;
    resolveParams.Height = height;
    resolveParams.TransferStrength = cfg.DlssNrTransferStrength.value_or_default();
    const auto strength = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 1.0f; };
    resolveParams.SkinProtection = cfg.DlssNrSkinProtection.value_or_default();
    resolveParams.ShowSkinMask = cfg.DlssNrShowSkinMask.value_or_default();
    resolveParams.SkinDetail = strength(cfg.DlssNrSkinDetail.value_or_default());
    resolveParams.SkinColour =
        cfg.DlssNrSkinToneEnabled.value_or_default() ? strength(cfg.DlssNrSkinColour.value_or_default()) : 0.0f;
    resolveParams.EnvironmentDetail = strength(cfg.DlssNrEnvironmentDetail.value_or_default());
    resolveParams.EnvironmentColour = strength(cfg.DlssNrEnvironmentColour.value_or_default());
    resolveParams.ColourStrength = cfg.DlssNrColourStrength.value_or_default();
    resolveParams.DebugView = cfg.DlssNrDebugView.value_or_default();
    resolveParams.MaxRatio = cfg.DlssNrMaxRatio.value_or_default();
    resolveParams.Transfer = std::min(cfg.DlssNrTransfer.value_or_default(), 1u);
    resolveParams.DebugScale = cfg.DlssNrWhitePointScale.value_or_default();
    resolveParams.Passthrough = isHdrBuffer ? 0u : 1u;
    resolveParams.ReversibleMode = cfg.DlssNrReversibleMode.value_or_default();
    resolveParams.ApplyModel = cfg.DlssNrApplyModel.value_or_default() ? 1u : 0u;
    resolveParams.CompareMode = cfg.DlssNrCompare.value_or_default();
    resolveParams.CompareSplit = cfg.DlssNrCompareSplit.value_or_default();
    resolveParams.CompareZoom = std::max(1.0f, cfg.DlssNrCompareZoom.value_or_default());
    resolveParams.CompareSwap = cfg.DlssNrCompareSwap.value_or_default() ? 1u : 0u;

    // Report the effective composition settings when they change.

    // Quantised to the precision it is printed at. Comparing raw floats logged 2376 lines in one
    // Enshrouded session, because a measured white point drifts continuously and every drift was a
    // change. A line per meaningful change is the point; a line per frame is a different problem.
    const ComposeReport composeNow { true,
                                     std::round(resolveParams.WhitePoint * 100.0f) / 100.0f,
                                     resolveParams.TransferStrength,
                                     resolveParams.ColourStrength,
                                     resolveParams.MaxRatio,
                                     resolveParams.Passthrough,
                                     resolveParams.DebugView,
                                     resolveParams.CompareMode,
                                     resolveParams.Transfer,
                                     nr.workWidth,
                                     nr.workHeight,
                                     effectivePasses };

    if (!loggedCompose.valid || loggedCompose.whitePoint != composeNow.whitePoint ||
        loggedCompose.transfer != composeNow.transfer || loggedCompose.colour != composeNow.colour ||
        loggedCompose.maxRatio != composeNow.maxRatio || loggedCompose.passthrough != composeNow.passthrough ||
        loggedCompose.debugView != composeNow.debugView ||
        loggedCompose.compareMode != composeNow.compareMode || loggedCompose.residual != composeNow.residual ||
        loggedCompose.workW != composeNow.workW || loggedCompose.workH != composeNow.workH ||
        loggedCompose.passes != composeNow.passes)
    {
        loggedCompose = composeNow;
        LOG_INFO("DLSS-NR composition: paper white {:.2f}x, detail {:.2f}, colour {:.2f}, guard "
                 "{:.1f}x, colour transform {}, transfer {}, model {}x{}, passes {}, debug view {}, compare {}",
                 composeNow.whitePoint, composeNow.transfer, composeNow.colour, composeNow.maxRatio,
                 composeNow.passthrough != 0 ? "off (frame already tone mapped)" : "on (linear HDR)",
                 composeNow.residual == 1 ? "matched residual" : "classic", composeNow.workW, composeNow.workH,
                 composeNow.passes, composeNow.debugView, composeNow.compareMode);
    }

    return resolveParams;
}
