#include "pch.h"
#include "Upscaler_Inputs_Dx11wDx12.h"

#include "MathUtils.h"

#include <with_dx12/with_dx12.h>
#include <hudfix/Hudfix_Dx11.h>
#include <resource_tracking/ResTrack_dx11.h>
#include "shaders/depth_scale/DS_Dx12.h"

using namespace OptiMath;
static DS_Dx12* DepthScaleDx11wDx12 = nullptr;

static bool PrepareFgResourceCache(NVSDK_NGX_Parameter* parameters, UINT64 frameKey)
{
    if (parameters == nullptr)
        return false;

    const auto mask = Dx11WithDx12::ResourceMask::Mv | Dx11WithDx12::ResourceMask::Depth;
    const auto dontUseNtShared = Config::Instance()->DontUseNTShared.value_or_default();
    const auto frameIndex = Dx11WithDx12::GetUpscalerFrameIndex();

    const auto result =
        Dx11WithDx12::PrepareUpscalerResources(parameters, mask, frameIndex, frameKey, dontUseNtShared, false, true);

    if (!result.Success)
    {
        LOG_ERROR("Dx11wDx12 FG input cache preparation failed");
        return false;
    }

    return true;
}

static bool ReusePreparedUpscalerCacheForFg(UINT64 frameKey)
{
    const auto mask = Dx11WithDx12::ResourceMask::Mv | Dx11WithDx12::ResourceMask::Depth;
    const auto resolvedFrameKey = frameKey != 0 ? frameKey : Dx11WithDx12::GetLastPreparedUpscalerFrameId();

    if (!Dx11WithDx12::HasPreparedUpscalerResources(mask, resolvedFrameKey))
    {
        LOG_WARN("Dx11wDx12 FG input cache miss");
        return false;
    }

    return true;
}

void UpscalerInputsDx11wDx12::Init(ID3D11Device* dx11Device, ID3D11DeviceContext* dx11Context, ID3D12Device* dx12Device,
                                   ID3D12CommandQueue* dx12CommandQueue)
{
    if (State::Instance().activeFgInput != FGInput::Upscaler)
        return;

    if (dx12Device != nullptr && dx12CommandQueue != nullptr && !WithDx12::IsInited())
        WithDx12::SetD3D12Objects(dx12Device, dx12CommandQueue, D3D12_COMMAND_LIST_TYPE_DIRECT);

    if (!WithDx12::IsInited() && !WithDx12::PrepareD3D12ForD3D11(dx11Device, D3D_FEATURE_LEVEL_11_0))
    {
        LOG_ERROR("UpscalerInputsDx11wDx12::Init failed to resolve D3D12 device/queue");
        return;
    }

    _dx12Device = WithDx12::GetD3D12Device();
    _dx12CommandQueue = WithDx12::GetD3D12CommandQueue();

    Dx11WithDx12::Init(dx11Device, dx11Context);
}

void UpscalerInputsDx11wDx12::Reset()
{
    ResTrack_Dx11::ClearPossibleHudless();
    Hudfix_Dx11::ResetCounters();
}

void UpscalerInputsDx11wDx12::UpscaleStart(NVSDK_NGX_Parameter* InParameters, IFeature_Dx11* feature)
{
    Hudfix_Dx11::SetSkipStatus(true);

    if (InParameters == nullptr || feature == nullptr)
    {
        Hudfix_Dx11::SetSkipStatus(false);
        return;
    }

    // FSR Camera values
    float cameraNear = 0.0f;
    float cameraFar = 0.0f;
    float cameraVFov = 0.0f;
    float meterFactor = 0.0f;
    float mvScaleX = 0.0f;
    float mvScaleY = 0.0f;
    float jitterX = 0.0f;
    float jitterY = 0.0f;

    float tempCameraNear = 0.0f;
    float tempCameraFar = 0.0f;

    auto& cfg = *Config::Instance();
    const auto& ngxParams = *InParameters;

    ngxParams.Get(OptiKeys::FSR_NearPlane, &tempCameraNear);
    ngxParams.Get(OptiKeys::FSR_FarPlane, &tempCameraFar);

    if (!cfg.FsrUseFsrInputValues.value_or_default() || (tempCameraNear == 0.0f && tempCameraFar == 0.0f))
    {
        if (feature->DepthInverted())
        {
            cameraFar = cfg.FsrCameraNear.value_or_default();
            cameraNear = cfg.FsrCameraFar.value_or_default();
        }
        else
        {
            cameraFar = cfg.FsrCameraFar.value_or_default();
            cameraNear = cfg.FsrCameraNear.value_or_default();
        }
    }
    else
    {
        cameraNear = tempCameraNear;
        cameraFar = tempCameraFar;
    }

    if (!cfg.FsrUseFsrInputValues.value_or_default() ||
        ngxParams.Get(OptiKeys::FSR_CameraFovVertical, &cameraVFov) != NVSDK_NGX_Result_Success)
    {
        if (cfg.FsrVerticalFov.has_value())
            cameraVFov = GetRadiansFromDeg(cfg.FsrVerticalFov.value());
        else if (cfg.FsrHorizontalFov.value_or_default() > 0.0f)
        {
            const float hFovRad = GetRadiansFromDeg(cfg.FsrHorizontalFov.value());
            cameraVFov =
                GetVerticalFovFromHorizontal(hFovRad, (float) feature->TargetWidth(), (float) feature->TargetHeight());
        }
        else
            cameraVFov = GetRadiansFromDeg(60);
    }

    if (!cfg.FsrUseFsrInputValues.value_or_default())
        ngxParams.Get(OptiKeys::FSR_ViewSpaceToMetersFactor, &meterFactor);

    State::Instance().lastFsrCameraFar = cameraFar;
    State::Instance().lastFsrCameraNear = cameraNear;

    if (State::Instance().activeFgInput != FGInput::Upscaler || _dx12Device == nullptr)
        return;

    if (feature->IsWithDx12())
    {
        const auto cacheFrameKey = Dx11WithDx12::GetLastPreparedUpscalerFrameId();

        if (!ReusePreparedUpscalerCacheForFg(cacheFrameKey))
            return;
    }
    else if (!PrepareFgResourceCache(InParameters, Dx11WithDx12::NextUpscalerFrameId()))
    {
        LOG_ERROR("Dx11wDx12 FG input cache preparation failed");
        return;
    }

    auto fg = State::Instance().currentFG;

    if (fg == nullptr)
        return;

    FG_Constants fgConstants {};
    fgConstants.displayWidth = feature->DisplayWidth();
    fgConstants.displayHeight = feature->DisplayHeight();

    if (feature->IsHdr())
        fgConstants.flags |= FG_Flags::Hdr;

    if (feature->DepthInverted())
        fgConstants.flags |= FG_Flags::InvertedDepth;

    if (feature->JitteredMV())
        fgConstants.flags |= FG_Flags::JitteredMVs;

    if (!feature->LowResMV())
        fgConstants.flags |= FG_Flags::DisplayResolutionMVs;

    if (Config::Instance()->FGAsync.value_or_default())
        fgConstants.flags |= FG_Flags::Async;

    fg->EvaluateState(_dx12Device, fgConstants);

    int reset = 0;
    InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset);

    InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &mvScaleX);
    InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &mvScaleY);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &jitterX);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &jitterY);

    fg->StartNewFrame();

    auto aspectRatio = (float) feature->DisplayWidth() / (float) feature->DisplayHeight();
    fg->SetCameraValues(cameraNear, cameraFar, cameraVFov, aspectRatio, meterFactor);
    fg->SetFrameTimeDelta(State::Instance().lastFGFrameTime);
    fg->SetMVScale(mvScaleX, mvScaleY);
    fg->SetJitter(jitterX, jitterY);
    fg->SetReset(reset);
    fg->SetInterpolationRect(feature->DisplayWidth(), feature->DisplayHeight());

    Hudfix_Dx11::UpscaleStart();

    if (State::Instance().isShuttingDown || !fg->IsActive() || !Config::Instance()->FGEnabled.value_or_default() ||
        State::Instance().currentSwapchain == nullptr)
    {
        return;
    }

    if (fg->Mutex.getOwner() == 2)
    {
        LOG_TRACE("Waiting for present!");
        fg->Mutex.lock(4);
        fg->Mutex.unlockThis(4);
    }

    auto& cache = Dx11WithDx12::GetUpscalerResourceCache();
    const auto frameIndex = fg->GetIndex();

    LOG_DEBUG("(FG Dx11wDx12) using cached inputs for fgUpscaledImage[{}], frame: {}", frameIndex, fg->FrameCount());

    ID3D12Resource* paramVelocity = cache.Mv.Dx12Resource;
    ID3D12Resource* paramDepth = cache.Depth.Dx12Resource;

    auto cmdList = fg->GetUICommandList();

    if (paramVelocity != nullptr)
    {
        Dx12Resource setResource {};
        setResource.type = FG_ResourceType::Velocity;
        setResource.cmdList = cmdList;
        setResource.resource = paramVelocity;
        setResource.state = (D3D12_RESOURCE_STATES) Config::Instance()->MVResourceBarrier.value_or(
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        setResource.validity = FG_ResourceValidity::ValidNow;

        if (feature->LowResMV())
        {
            setResource.width = feature->RenderWidth();
            setResource.height = feature->RenderHeight();
        }
        else
        {
            setResource.width = feature->TargetWidth();
            setResource.height = feature->TargetHeight();
        }

        fg->SetResource(&setResource);
    }

    if (paramDepth != nullptr)
    {
        auto done = false;

        if (Config::Instance()->FGEnableDepthScale.value_or_default())
        {
            if (DepthScaleDx11wDx12 == nullptr)
                DepthScaleDx11wDx12 = new DS_Dx12("Depth Scale Dx11wDx12", _dx12Device);

            if (DepthScaleDx11wDx12->CreateBufferResource(_dx12Device, paramDepth, feature->DisplayWidth(),
                                                          feature->DisplayHeight(),
                                                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
                DepthScaleDx11wDx12->Buffer() != nullptr)
            {
                DepthScaleDx11wDx12->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                if (DepthScaleDx11wDx12->Dispatch(cmdList, paramDepth, DepthScaleDx11wDx12->Buffer()))
                {
                    Dx12Resource setResource {};
                    setResource.type = FG_ResourceType::Depth;
                    setResource.cmdList = cmdList;
                    setResource.resource = DepthScaleDx11wDx12->Buffer();
                    setResource.width = feature->RenderWidth();
                    setResource.height = feature->RenderHeight();
                    setResource.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    setResource.validity = FG_ResourceValidity::UntilPresent;

                    fg->SetResource(&setResource);
                    done = true;
                }
            }
        }

        if (!done)
        {
            Dx12Resource setResource {};
            setResource.type = FG_ResourceType::Depth;
            setResource.cmdList = cmdList;
            setResource.resource = paramDepth;
            setResource.width = feature->RenderWidth();
            setResource.height = feature->RenderHeight();
            setResource.state = (D3D12_RESOURCE_STATES) Config::Instance()->DepthResourceBarrier.value_or(
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            setResource.validity = FG_ResourceValidity::ValidNow;

            fg->SetResource(&setResource);
        }
    }

    LOG_DEBUG("(FG Dx11wDx12) D3D11 input preparation done, frame: {}", fg->FrameCount());
}

void UpscalerInputsDx11wDx12::UpscaleEnd(NVSDK_NGX_Parameter* InParameters, IFeature_Dx11* feature)
{
    Hudfix_Dx11::SetSkipStatus(false);

    if (InParameters == nullptr || feature == nullptr)
        return;

    auto fg = State::Instance().currentFG;

    if (fg == nullptr || State::Instance().activeFgInput != FGInput::Upscaler || _dx12Device == nullptr)
        return;

    if (fg->IsActive() && Config::Instance()->FGEnabled.value_or_default() &&
        State::Instance().currentSwapchain != nullptr)
    {
        if (Config::Instance()->FGHUDFix.value_or_default())
        {
            Hudfix_Dx11::UpscaleEnd(feature->FrameCount(), State::Instance().lastFGFrameTime);

            ID3D11Resource* output = nullptr;
            if (InParameters->Get(NVSDK_NGX_Parameter_Output, &output) != NVSDK_NGX_Result_Success)
                InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &output);

            if (output != nullptr)
            {
                ID3D11Texture2D* outputTexture = nullptr;
                if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&outputTexture))) && outputTexture != nullptr)
                {
                    Dx11ResourceInfo info = {};
                    if (Hudfix_Dx11::FillResourceInfo(outputTexture, Dx11ResourceType::UAV, Dx11CaptureInfo::Upscaler,
                                                      &info))
                    {
                        Hudfix_Dx11::CheckForHudless(Dx11WithDx12::GetD3D11DeviceContext(), &info, true);
                    }

                    outputTexture->Release();
                }
            }
        }
        else
        {
            LOG_DEBUG("(FG Dx11wDx12) running, frame: {}", feature->FrameCount());
        }
    }
}
