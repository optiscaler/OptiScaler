#include "pch.h"
#include "DlssNr_Dx12_State.h"

DlssNr::Proxy::Settings DlssNr_Dx12::State::ModelSettings(const Config& cfg, unsigned int pass)
{
    const auto tuning = PassTuning(cfg, pass);
    return { PassPreset(cfg, pass), PassStyle(cfg, pass), tuning.intensity, tuning.structure,
             tuning.tone, tuning.skin, tuning.autoMask };
}

bool DlssNr_Dx12::State::PrepareRunModels(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device,
                                        const DlssNrFrameInfo& frame, const D3D12_RESOURCE_DESC& desc,
                                        DlssNr::ColorExtent native, DlssNr::ColorExtent work,
                                        float workScale, unsigned int requestedPasses)
{
    const auto& cfg = *Config::Instance();
    const auto width = native.width, height = native.height;
    const auto workWidth = work.width, workHeight = work.height;
    const bool cropColor = frame.BeforeUpscale && (width != desc.Width || height != desc.Height);
    const bool reduced = workWidth != width || workHeight != height;
    ReleaseSurfacesIfFormatChanged(desc.Format);

    const bool resolutionChanged =
        nr.width != width || nr.height != height || nr.workWidth != workWidth || nr.workHeight != workHeight;
    const bool placementChanged = nr.width != 0 && (nr.beforeUpscale != frame.BeforeUpscale ||
                                                    nr.rayReconstruction != frame.RayReconstruction);

    // The model reads its tuning once, while the feature is built, so a changed setting only takes
    // effect when the feature is rebuilt. TuningMatchesFeature was written to notice that and then
    // never called, which is why every one of these controls appeared to do nothing until something
    // else -- a resolution change -- happened to force a rebuild by accident.
    const bool tuningChanged = !TuningMatchesFeature(cfg, requestedPasses);

    if (resolutionChanged || placementChanged || (nr.models[0].HasFeature() && tuningChanged))
    {
        // Parked rather than released: with frame generation the GPU can still be several frames
        // deep in work that references all of it.
        for (auto& model : nr.models)
            model.RetryAfterFailure();
        std::fill(std::begin(nr.passCreateFailed), std::end(nr.passCreateFailed), false);
        nr.reset = true;
        modelRunning = false;

        // Resolution and seam changes invalidate the scratch state. Tuning does not, and throwing
        // resources away for it would mean a reallocation every time a slider moves.
        if (resolutionChanged || placementChanged)
        {
            if (placementChanged)
                ForgetCalibration();

            ParkNrResource(nr.output);
            ParkNrResource(nr.passScratch);
            ParkNrResource(nr.passClamp);
            ParkNrResource(nr.colorCopy);
            ParkNrResource(nr.hdrCopy);
            ParkNrResource(nr.colorSmall);
            ParkNrResource(nr.outputNative);
            ParkNrResource(nr.activeColor);
            nr.passScratchFailed = false;
        }
    }

    if (nr.output == nullptr)
    {
        nr.output = CreateScratch(device, desc.Format, workWidth, workHeight);
        nr.colorCopy = CreateScratch(device, desc.Format, width, height);
        nr.hdrCopy = CreateScratch(device, desc.Format, width, height);
        nr.workWidth = workWidth;
        nr.workHeight = workHeight;
        nr.width = width;
        nr.height = height;
        nr.beforeUpscale = frame.BeforeUpscale;
        nr.rayReconstruction = frame.RayReconstruction;
        nr.reset = true;
    }

    if (cropColor && nr.activeColor == nullptr)
        nr.activeColor = CreateScratch(device, desc.Format, width, height);
    if (cropColor && nr.activeColor == nullptr)
    {
        nr.failed = true;
        nr.reason = "the pre-SR active colour staging texture could not be allocated";
        LOG_ERROR("DLSS-NR unavailable: {}", nr.reason);
        return false;
    }

    if (requestedPasses == 1)
    {
        // Reclaim the extra raster and clear its failure latch. Raising the count later gets one fresh
        // allocation attempt; holding a failing allocation at two must not retry it every frame.
        ParkNrResource(nr.passScratch);
        ParkNrResource(nr.passClamp);
        nr.passScratchFailed = false;
    }
    else if (nr.passScratch == nullptr && !nr.passScratchFailed)
    {
        nr.passScratch = CreateScratch(device, desc.Format, workWidth, workHeight);
        nr.passClamp = CreateScratch(device, desc.Format, workWidth, workHeight);
        nr.passScratchFailed = nr.passScratch == nullptr || nr.passClamp == nullptr;
        if (nr.passScratchFailed)
        {
            ParkNrResource(nr.passScratch);
            ParkNrResource(nr.passClamp);
        }

        if (nr.passScratchFailed)
            LOG_ERROR("DLSS-NR: could not allocate the multipass textures; extra passes are disabled");
    }

    if (reduced && nr.colorSmall == nullptr)
        nr.colorSmall = CreateScratch(device, desc.Format, workWidth, workHeight);

    // The down-leg target is native (the answer is brought back to frame size before the resolve).
    if (workScale > 1.0f && nr.outputNative == nullptr)
        nr.outputNative = CreateScratch(device, desc.Format, width, height);

    if (nr.meter == nullptr)
    {
        nr.meter = CreateScratch(device, DXGI_FORMAT_R32_FLOAT, kDlssNrMeterGrid, kDlssNrMeterGrid);

        D3D12_HEAP_PROPERTIES readback {};
        readback.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC bufferDesc {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = kMeterBytes;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        for (auto& rb : nr.meterReadback)
        {
            if (FAILED(device->CreateCommittedResource(&readback, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb))))
            {
                rb = nullptr;
                LOG_WARN("DLSS-NR: the white point meter could not allocate its readback; falling back "
                         "to the paper white slider");
            }
        }

        if (nr.meter != nullptr)
            LOG_INFO("DLSS-NR: white point meter up, {}x{} tiles", kDlssNrMeterGrid, kDlssNrMeterGrid);
    }

    if (!nr.output || !nr.colorCopy || !nr.hdrCopy)
    {
        nr.failed = true;
        nr.reason = "the Neural Rendering staging textures could not be allocated";
        return false;
    }

    // Prepare at most one missing layer per submission. Repeated CPU calls in the same epoch
    // cannot evaluate creation work or create another layer before the first one is submitted.
    for (unsigned int pass = requestedPasses; pass < DlssNr::MaxPassCount; ++pass)
    {
        nr.models[pass].RetryAfterFailure();
        nr.passCreateFailed[pass] = false;
    }
    const unsigned int buildPasses = nr.passScratch ? requestedPasses : 1;
    for (unsigned int pass = 0; pass < buildPasses; ++pass)
    {
        if (nr.passCreateFailed[pass])
            break;
        bool ready = false;
        const auto prepared = nr.models[pass].Prepare(cmdList, device, workWidth, workHeight, ModelSettings(cfg, pass),
                                                      frame.SubmissionEpoch, &ready);
        if (prepared != NVSDK_NGX_Result_Success)
        {
            nr.passCreateFailed[pass] = true;
            if (pass == 0)
            {
                nr.failed = true;
                nr.reason = "the NVIDIA NGX driver could not create Neural Rendering";
            }
            LOG_ERROR("DLSS-NR driver creation for pass {} failed: 0x{:X} ({})", pass + 1, prepared,
                      NgxResultName(prepared));
                return false;
        }
        nr.builtPreset[pass] = PassPreset(cfg, pass);
        nr.builtPassTuning[pass] = PassTuning(cfg, pass);
        nr.builtStyle[pass] = PassStyle(cfg, pass);
        if (!ready)
        {
                return false;
        }
    }

    return true;
}

auto DlssNr_Dx12::State::NgxResultName(unsigned int r) -> const char*
{
    switch (r)
    {
    case 0x1:
        return "Success";
    case 0xBAD00001:
        return "FAIL_FeatureNotSupported";
    case 0xBAD00002:
        return "FAIL_PlatformError";
    case 0xBAD00003:
        return "FAIL_FeatureAlreadyExists";
    case 0xBAD00004:
        return "FAIL_FeatureNotFound";
    case 0xBAD00005:
        return "FAIL_InvalidParameter";
    case 0xBAD00006:
        return "FAIL_ScratchBufferTooSmall";
    case 0xBAD00007:
        return "FAIL_NotInitialized";
    case 0xBAD00008:
        return "FAIL_UnsupportedInputFormat";
    case 0xBAD00009:
        return "FAIL_RWFlagMissing";
    case 0xBAD0000A:
        return "FAIL_MissingInput";
    case 0xBAD0000B:
        return "FAIL_UnableToInitializeFeature";
    case 0xBAD0000C:
        return "FAIL_OutOfDate";
    case 0xBAD0000D:
        return "FAIL_OutOfGPUMemory";
    case 0xBAD0000E:
        return "FAIL_UnsupportedFormat";
    case 0xBAD0000F:
        return "FAIL_UnableToWriteToAppDataPath";
    case 0xBAD00010:
        return "FAIL_UnsupportedParameter";
    case 0xBAD00011:
        return "FAIL_Denied";
    case 0xBAD00012:
        return "FAIL_NotImplemented";
    default:
        return "unknown";
    }
}

auto DlssNr_Dx12::State::TuningMatchesFeature(const Config& cfg, unsigned int requestedPasses) -> bool
{
    for (unsigned int pass = 0; pass < requestedPasses; ++pass)
    {
        // A profile cannot be stale until its feature exists. This lets a user prepare pass 2 or 3
        // while running fewer layers without needlessly rebuilding pass 1.
        if (!nr.models[pass].HasFeature())
            continue;

        if (nr.builtPassTuning[pass] != PassTuning(cfg, pass) || nr.builtPreset[pass] != PassPreset(cfg, pass) ||
            nr.builtStyle[pass] != PassStyle(cfg, pass))
            return false;
    }

    return true;
}
