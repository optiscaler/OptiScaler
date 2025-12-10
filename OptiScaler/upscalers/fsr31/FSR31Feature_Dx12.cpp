#include <pch.h>
#include <Config.h>
#include <Util.h>

#include <proxies/FfxApi_Proxy.h>

#include "FSR31Feature_Dx12.h"

NVSDK_NGX_Parameter* FSR31FeatureDx12::SetParameters(NVSDK_NGX_Parameter* InParameters)
{
    InParameters->Set("OptiScaler.SupportsUpscaleSize", true);
    return InParameters;
}

FSR31FeatureDx12::FSR31FeatureDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : FSR31Feature(InHandleId, InParameters), IFeature_Dx12(InHandleId, InParameters),
      IFeature(InHandleId, SetParameters(InParameters))
{
    FfxApiProxy::InitFfxDx12();

    _moduleLoaded = FfxApiProxy::IsSRReady();

    if (_moduleLoaded)
        LOG_INFO("amd_fidelityfx_dx12.dll methods loaded!");
    else
        LOG_ERROR("can't load amd_fidelityfx_dx12.dll methods!");
}

bool FSR31FeatureDx12::Init(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCommandList,
                            NVSDK_NGX_Parameter* InParameters)
{
    LOG_DEBUG("FSR31FeatureDx12::Init");

    if (IsInited())
        return true;

    Device = InDevice;

    if (InitFSR3(InParameters))
    {
        if (!Config::Instance()->OverlayMenu.value_or_default() && (Imgui == nullptr || Imgui.get() == nullptr))
            Imgui = std::make_unique<Menu_Dx12>(Util::GetProcessWindow(), InDevice);

        OutputScaler = std::make_unique<OS_Dx12>("Output Scaling", InDevice, (TargetWidth() < DisplayWidth()));
        RCAS = std::make_unique<RCAS_Dx12>("RCAS", InDevice);
        Bias = std::make_unique<Bias_Dx12>("Bias", InDevice);

        return true;
    }

    return false;
}

bool FSR31FeatureDx12::Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!IsInited())
        return false;

    if (!RCAS->IsInit())
        Config::Instance()->RcasEnabled.set_volatile_value(false);

    if (!OutputScaler->IsInit())
        Config::Instance()->OutputScalingEnabled.set_volatile_value(false);

    struct ffxDispatchDescUpscale params = { 0 };
    params.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;

    params.flags = 0;

    if (Config::Instance()->FsrDebugView.value_or_default() &&
        (Version() < feature_version { 4, 0, 0 } || Config::Instance()->Fsr4EnableDebugView.value_or_default()))
    {
        params.flags |= FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW;
    }

    if (Config::Instance()->FsrNonLinearPQ.value_or_default())
        params.flags |= FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ;
    else if (Config::Instance()->FsrNonLinearSRGB.value_or_default())
        params.flags |= FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;

    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &params.jitterOffset.x);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &params.jitterOffset.y);

    if (Config::Instance()->OverrideSharpness.value_or_default())
        _sharpness = Config::Instance()->Sharpness.value_or_default();
    else
        _sharpness = GetSharpness(InParameters);

    if (Config::Instance()->RcasEnabled.value_or_default())
    {
        params.enableSharpening = false;
        params.sharpness = 0.0f;
    }
    else
    {
        if (_sharpness > 1.0f)
            _sharpness = 1.0f;

        params.enableSharpening = _sharpness > 0.0f;
        params.sharpness = _sharpness;
    }

    // Force enable RCAS when in FSR4 debug view mode
    // it crashes when sharpening is disabled
    // Debug view expects RCAS output (now sure why)
    if (Version() >= feature_version { 4, 0, 2 } && Config::Instance()->FsrDebugView.value_or_default() &&
        Config::Instance()->Fsr4EnableDebugView.value_or_default() && !params.enableSharpening)
    {
        params.enableSharpening = true;
        params.sharpness = 0.01f;
    }

    LOG_DEBUG("Jitter Offset: {0}x{1}", params.jitterOffset.x, params.jitterOffset.y);

    unsigned int reset;
    InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset);
    params.reset = (reset == 1);

    GetRenderResolution(InParameters, &params.renderSize.width, &params.renderSize.height);

    bool useSS = Config::Instance()->OutputScalingEnabled.value_or_default() && LowResMV();

    LOG_DEBUG("Input Resolution: {0}x{1}", params.renderSize.width, params.renderSize.height);

    params.commandList = InCommandList;

    ID3D12Resource* paramColor;
    if (InParameters->Get(NVSDK_NGX_Parameter_Color, &paramColor) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Color, (void**) &paramColor);

    if (paramColor)
    {
        LOG_DEBUG("Color exist..");

        if (Config::Instance()->ColorResourceBarrier.has_value())
        {
            ResourceBarrier(InCommandList, paramColor,
                            (D3D12_RESOURCE_STATES) Config::Instance()->ColorResourceBarrier.value(),
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        else if (State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
                 State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine)
        {
            Config::Instance()->ColorResourceBarrier.set_volatile_value(D3D12_RESOURCE_STATE_RENDER_TARGET);
            ResourceBarrier(InCommandList, paramColor, D3D12_RESOURCE_STATE_RENDER_TARGET,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        params.color = ffxApiGetResourceDX12(paramColor, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("Color not exist!!");
        return false;
    }

    ID3D12Resource* paramVelocity;
    if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramVelocity) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity);

    if (paramVelocity)
    {
        LOG_DEBUG("MotionVectors exist..");

        if (Config::Instance()->MVResourceBarrier.has_value())
            ResourceBarrier(InCommandList, paramVelocity,
                            (D3D12_RESOURCE_STATES) Config::Instance()->MVResourceBarrier.value(),
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        else if (State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
                 State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine)
        {
            Config::Instance()->MVResourceBarrier.set_volatile_value(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            ResourceBarrier(InCommandList, paramVelocity, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        params.motionVectors = ffxApiGetResourceDX12(paramVelocity, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("MotionVectors not exist!!");
        return false;
    }

    ID3D12Resource* paramOutput;
    if (InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput);

    if (paramOutput)
    {
        LOG_DEBUG("Output exist..");

        if (Config::Instance()->OutputResourceBarrier.has_value())
            ResourceBarrier(InCommandList, paramOutput,
                            (D3D12_RESOURCE_STATES) Config::Instance()->OutputResourceBarrier.value(),
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        if (useSS)
        {
            if (OutputScaler->CreateBufferResource(Device, paramOutput, TargetWidth(), TargetHeight(),
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            {
                OutputScaler->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                params.output = ffxApiGetResourceDX12(OutputScaler->Buffer(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            else
                params.output = ffxApiGetResourceDX12(paramOutput, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        else
            params.output = ffxApiGetResourceDX12(paramOutput, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

        if (Config::Instance()->RcasEnabled.value_or_default() &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                                   Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
            RCAS->IsInit() &&
            RCAS->CreateBufferResource(Device, (ID3D12Resource*) params.output.resource,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
        {
            RCAS->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            params.output = ffxApiGetResourceDX12(RCAS->Buffer(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }
    else
    {
        LOG_ERROR("Output not exist!!");
        return false;
    }

    ID3D12Resource* paramDepth;
    if (InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth);

    if (paramDepth)
    {
        LOG_DEBUG("Depth exist..");

        if (Config::Instance()->DepthResourceBarrier.has_value())
            ResourceBarrier(InCommandList, paramDepth,
                            (D3D12_RESOURCE_STATES) Config::Instance()->DepthResourceBarrier.value(),
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        params.depth = ffxApiGetResourceDX12(paramDepth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("Depth not exist!!");

        if (LowResMV())
            return false;
    }

    ID3D12Resource* paramExp = nullptr;
    if (AutoExposure())
    {
        LOG_DEBUG("AutoExposure enabled!");
    }
    else
    {
        if (InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, &paramExp) != NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, (void**) &paramExp);

        if (paramExp)
        {
            LOG_DEBUG("ExposureTexture exist..");

            if (Config::Instance()->ExposureResourceBarrier.has_value())
                ResourceBarrier(InCommandList, paramExp,
                                (D3D12_RESOURCE_STATES) Config::Instance()->ExposureResourceBarrier.value(),
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            params.exposure = ffxApiGetResourceDX12(paramExp, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        }
        else
        {
            LOG_DEBUG("AutoExposure disabled but ExposureTexture is not exist, it may cause problems!!");
            State::Instance().AutoExposure = true;
            State::Instance().changeBackend[Handle()->Id] = true;
            return true;
        }
    }

    ID3D12Resource* paramTransparency = nullptr;
    if (InParameters->Get("FSR.transparencyAndComposition", &paramTransparency) == NVSDK_NGX_Result_Success)
        InParameters->Get("FSR.transparencyAndComposition", (void**) &paramTransparency);

    ID3D12Resource* paramReactiveMask = nullptr;
    if (InParameters->Get("FSR.reactive", &paramReactiveMask) == NVSDK_NGX_Result_Success)
        InParameters->Get("FSR.reactive", (void**) &paramReactiveMask);

    ID3D12Resource* paramReactiveMask2 = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &paramReactiveMask2) !=
        NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactiveMask2);

    if (!Config::Instance()->DisableReactiveMask.value_or(paramReactiveMask == nullptr &&
                                                          paramReactiveMask2 == nullptr))
    {
        if (paramTransparency != nullptr)
        {
            LOG_DEBUG("Using FSR transparency mask..");
            params.transparencyAndComposition =
                ffxApiGetResourceDX12(paramTransparency, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        }

        if (paramReactiveMask != nullptr)
        {
            LOG_DEBUG("Using FSR reactive mask..");
            params.reactive = ffxApiGetResourceDX12(paramReactiveMask, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        }
        else
        {
            if (paramReactiveMask2 != nullptr)
            {
                LOG_DEBUG("Input Bias mask exist..");
                Config::Instance()->DisableReactiveMask.set_volatile_value(false);

                if (Config::Instance()->MaskResourceBarrier.has_value())
                    ResourceBarrier(InCommandList, paramReactiveMask2,
                                    (D3D12_RESOURCE_STATES) Config::Instance()->MaskResourceBarrier.value(),
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                if (paramTransparency == nullptr && Config::Instance()->FsrUseMaskForTransparency.value_or_default())
                    params.transparencyAndComposition =
                        ffxApiGetResourceDX12(paramReactiveMask2, FFX_API_RESOURCE_STATE_COMPUTE_READ);

                if (Config::Instance()->DlssReactiveMaskBias.value_or_default() > 0.0f && Bias->IsInit() &&
                    Bias->CreateBufferResource(Device, paramReactiveMask2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
                    Bias->CanRender())
                {
                    Bias->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                    if (Bias->Dispatch(Device, InCommandList, paramReactiveMask2,
                                       Config::Instance()->DlssReactiveMaskBias.value_or_default(), Bias->Buffer()))
                    {
                        Bias->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                        params.reactive = ffxApiGetResourceDX12(Bias->Buffer(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
                    }
                }
                else
                {
                    LOG_DEBUG("Skipping reactive mask, Bias: {0}, Bias Init: {1}, Bias CanRender: {2}",
                              Config::Instance()->DlssReactiveMaskBias.value_or_default(), Bias->IsInit(),
                              Bias->CanRender());
                }
            }
        }
    }

    _hasColor = params.color.resource != nullptr;
    _hasDepth = params.depth.resource != nullptr;
    _hasMV = params.motionVectors.resource != nullptr;
    _hasExposure = params.exposure.resource != nullptr;
    _hasTM = params.transparencyAndComposition.resource != nullptr;
    _accessToReactiveMask = paramReactiveMask != nullptr;
    _hasOutput = params.output.resource != nullptr;

    // For FSR 4 as it seems to be missing some conversions from typeless
    // transparencyAndComposition and exposure might be unnecessary here
    if (Version().major >= 4)
    {
        params.color.description.format = ffxResolveTypelessFormat(params.color.description.format);
        params.depth.description.format = ffxResolveTypelessFormat(params.depth.description.format);
        params.motionVectors.description.format = ffxResolveTypelessFormat(params.motionVectors.description.format);
        params.exposure.description.format = ffxResolveTypelessFormat(params.exposure.description.format);
        params.transparencyAndComposition.description.format =
            ffxResolveTypelessFormat(params.transparencyAndComposition.description.format);
        params.output.description.format = ffxResolveTypelessFormat(params.output.description.format);
    }

    float MVScaleX = 1.0f;
    float MVScaleY = 1.0f;

    if (InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &MVScaleX) == NVSDK_NGX_Result_Success &&
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &MVScaleY) == NVSDK_NGX_Result_Success)
    {
        params.motionVectorScale.x = MVScaleX;
        params.motionVectorScale.y = MVScaleY;
    }
    else
    {
        LOG_WARN("Can't get motion vector scales!");

        params.motionVectorScale.x = MVScaleX;
        params.motionVectorScale.y = MVScaleY;
    }

    LOG_DEBUG("Sharpness: {0}", params.sharpness);

    if (!Config::Instance()->FsrUseFsrInputValues.value_or_default() ||
        InParameters->Get("FSR.cameraNear", &params.cameraNear) != NVSDK_NGX_Result_Success)
    {
        if (DepthInverted())
            params.cameraFar = Config::Instance()->FsrCameraNear.value_or_default();
        else
            params.cameraNear = Config::Instance()->FsrCameraNear.value_or_default();
    }

    if (!Config::Instance()->FsrUseFsrInputValues.value_or_default() ||
        InParameters->Get("FSR.cameraFar", &params.cameraFar) != NVSDK_NGX_Result_Success)
    {
        if (DepthInverted())
            params.cameraNear = Config::Instance()->FsrCameraFar.value_or_default();
        else
            params.cameraFar = Config::Instance()->FsrCameraFar.value_or_default();
    }

    if (!Config::Instance()->FsrUseFsrInputValues.value_or_default() ||
        InParameters->Get("FSR.cameraFovAngleVertical", &params.cameraFovAngleVertical) != NVSDK_NGX_Result_Success)
    {
        if (Config::Instance()->FsrVerticalFov.has_value())
            params.cameraFovAngleVertical = Config::Instance()->FsrVerticalFov.value() * 0.0174532925199433f;
        else if (Config::Instance()->FsrHorizontalFov.value_or_default() > 0.0f)
            params.cameraFovAngleVertical =
                2.0f * atan((tan(Config::Instance()->FsrHorizontalFov.value() * 0.0174532925199433f) * 0.5f) /
                            (float) TargetHeight() * (float) TargetWidth());
        else
            params.cameraFovAngleVertical = 1.0471975511966f;
    }

    if (!Config::Instance()->FsrUseFsrInputValues.value_or_default() ||
        InParameters->Get("FSR.frameTimeDelta", &params.frameTimeDelta) != NVSDK_NGX_Result_Success)
    {
        if (InParameters->Get(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, &params.frameTimeDelta) !=
                NVSDK_NGX_Result_Success ||
            params.frameTimeDelta < 1.0f)
            params.frameTimeDelta = (float) GetDeltaTime();
    }

    LOG_DEBUG("FrameTimeDeltaInMsec: {0}", params.frameTimeDelta);

    if (!Config::Instance()->FsrUseFsrInputValues.value_or_default() ||
        InParameters->Get("FSR.viewSpaceToMetersFactor", &params.viewSpaceToMetersFactor) != NVSDK_NGX_Result_Success)
        params.viewSpaceToMetersFactor = 0.0f;

    params.upscaleSize.width = TargetWidth();
    params.upscaleSize.height = TargetHeight();

    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &params.preExposure) != NVSDK_NGX_Result_Success)
        params.preExposure = 1.0f;

    if (Version() >= feature_version { 3, 1, 1 } && _velocity != Config::Instance()->FsrVelocity.value_or_default())
    {
        _velocity = Config::Instance()->FsrVelocity.value_or_default();
        ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
        m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
        m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FVELOCITYFACTOR;
        m_upscalerKeyValueConfig.ptr = &_velocity;
        auto result = FfxApiProxy::D3D12_Configure(&_context, &m_upscalerKeyValueConfig.header);

        if (result != FFX_API_RETURN_OK)
            LOG_WARN("Velocity configure result: {}", (UINT) result);
    }

    if (Version() >= feature_version { 3, 1, 4 })
    {
        if (_reactiveScale != Config::Instance()->FsrReactiveScale.value_or_default())
        {
            _reactiveScale = Config::Instance()->FsrReactiveScale.value_or_default();
            ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
            m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
            m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FREACTIVENESSSCALE;
            m_upscalerKeyValueConfig.ptr = &_reactiveScale;
            auto result = FfxApiProxy::D3D12_Configure(&_context, &m_upscalerKeyValueConfig.header);

            if (result != FFX_API_RETURN_OK)
                LOG_WARN("Reactive Scale configure result: {}", (UINT) result);
        }

        if (_shadingScale != Config::Instance()->FsrShadingScale.value_or_default())
        {
            _shadingScale = Config::Instance()->FsrShadingScale.value_or_default();
            ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
            m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
            m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FSHADINGCHANGESCALE;
            m_upscalerKeyValueConfig.ptr = &_shadingScale;
            auto result = FfxApiProxy::D3D12_Configure(&_context, &m_upscalerKeyValueConfig.header);

            if (result != FFX_API_RETURN_OK)
                LOG_WARN("Shading Scale configure result: {}", (UINT) result);
        }

        if (_accAddPerFrame != Config::Instance()->FsrAccAddPerFrame.value_or_default())
        {
            _accAddPerFrame = Config::Instance()->FsrAccAddPerFrame.value_or_default();
            ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
            m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
            m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FACCUMULATIONADDEDPERFRAME;
            m_upscalerKeyValueConfig.ptr = &_accAddPerFrame;
            auto result = FfxApiProxy::D3D12_Configure(&_context, &m_upscalerKeyValueConfig.header);

            if (result != FFX_API_RETURN_OK)
                LOG_WARN("Acc. Add Per Frame configure result: {}", (UINT) result);
        }

        if (_minDisOccAcc != Config::Instance()->FsrMinDisOccAcc.value_or_default())
        {
            _minDisOccAcc = Config::Instance()->FsrMinDisOccAcc.value_or_default();
            ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
            m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
            m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FMINDISOCCLUSIONACCUMULATION;
            m_upscalerKeyValueConfig.ptr = &_minDisOccAcc;
            auto result = FfxApiProxy::D3D12_Configure(&_context, &m_upscalerKeyValueConfig.header);

            if (result != FFX_API_RETURN_OK)
                LOG_WARN("Minimum Disocclusion Acc. configure result: {}", (UINT) result);
        }
    }

    if (InParameters->Get("FSR.upscaleSize.width", &params.upscaleSize.width) == NVSDK_NGX_Result_Success &&
        Config::Instance()->OutputScalingEnabled.value_or_default())
    {
        params.upscaleSize.width *=
            static_cast<uint32_t>(Config::Instance()->OutputScalingMultiplier.value_or_default());
    }

    if (InParameters->Get("FSR.upscaleSize.height", &params.upscaleSize.height) == NVSDK_NGX_Result_Success &&
        Config::Instance()->OutputScalingEnabled.value_or_default())
    {
        params.upscaleSize.height *=
            static_cast<uint32_t>(Config::Instance()->OutputScalingMultiplier.value_or_default());
    }

    LOG_DEBUG("Dispatch!!");
    auto result = FfxApiProxy::D3D12_Dispatch(&_context, &params.header);

    if (result != FFX_API_RETURN_OK)
    {
        LOG_ERROR("_dispatch error: {0}", FfxApiProxy::ReturnCodeToString(result));

        if (result == FFX_API_RETURN_ERROR_RUNTIME_ERROR)
        {
            LOG_WARN("Trying to recover by recreating the feature");
            State::Instance().changeBackend[Handle()->Id] = true;
        }

        return false;
    }

    // apply rcas
    if (Config::Instance()->RcasEnabled.value_or_default() &&
        (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                               Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
        RCAS->CanRender())
    {
        if (params.output.resource != RCAS->Buffer())
            ResourceBarrier(InCommandList, (ID3D12Resource*) params.output.resource,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        RCAS->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        RcasConstants rcasConstants {};

        rcasConstants.Sharpness = _sharpness;
        rcasConstants.DisplayWidth = TargetWidth();
        rcasConstants.DisplayHeight = TargetHeight();
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &rcasConstants.MvScaleX);
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &rcasConstants.MvScaleY);
        rcasConstants.DisplaySizeMV = !(GetFeatureFlags() & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes);
        rcasConstants.RenderHeight = RenderHeight();
        rcasConstants.RenderWidth = RenderWidth();

        if (useSS)
        {
            if (!RCAS->Dispatch(Device, InCommandList, (ID3D12Resource*) params.output.resource,
                                (ID3D12Resource*) params.motionVectors.resource, rcasConstants, OutputScaler->Buffer()))
            {
                Config::Instance()->RcasEnabled.set_volatile_value(false);
                return true;
            }
        }
        else
        {
            if (!RCAS->Dispatch(Device, InCommandList, (ID3D12Resource*) params.output.resource,
                                (ID3D12Resource*) params.motionVectors.resource, rcasConstants, paramOutput))
            {
                Config::Instance()->RcasEnabled.set_volatile_value(false);
                return true;
            }
        }
    }

    if (useSS)
    {
        LOG_DEBUG("scaling output...");
        OutputScaler->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        if (!OutputScaler->Dispatch(Device, InCommandList, OutputScaler->Buffer(), paramOutput))
        {
            Config::Instance()->OutputScalingEnabled.set_volatile_value(false);
            State::Instance().changeBackend[Handle()->Id] = true;
            return true;
        }
    }

    // imgui
    if (!Config::Instance()->OverlayMenu.value_or_default() && _frameCount > 30)
    {
        if (Imgui != nullptr && Imgui.get() != nullptr)
        {
            if (Imgui->IsHandleDifferent())
            {
                Imgui.reset();
            }
            else
                Imgui->Render(InCommandList, paramOutput);
        }
        else
        {
            if (Imgui == nullptr || Imgui.get() == nullptr)
                Imgui = std::make_unique<Menu_Dx12>(GetForegroundWindow(), Device);
        }
    }

    // restore resource states
    if (paramColor && Config::Instance()->ColorResourceBarrier.has_value())
        ResourceBarrier(InCommandList, paramColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        (D3D12_RESOURCE_STATES) Config::Instance()->ColorResourceBarrier.value());

    if (paramVelocity && Config::Instance()->MVResourceBarrier.has_value())
        ResourceBarrier(InCommandList, paramVelocity, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        (D3D12_RESOURCE_STATES) Config::Instance()->MVResourceBarrier.value());

    if (paramOutput && Config::Instance()->OutputResourceBarrier.has_value())
        ResourceBarrier(InCommandList, paramOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        (D3D12_RESOURCE_STATES) Config::Instance()->OutputResourceBarrier.value());

    if (paramDepth && Config::Instance()->DepthResourceBarrier.has_value())
        ResourceBarrier(InCommandList, paramDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        (D3D12_RESOURCE_STATES) Config::Instance()->DepthResourceBarrier.value());

    if (paramExp && Config::Instance()->ExposureResourceBarrier.has_value())
        ResourceBarrier(InCommandList, paramExp, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        (D3D12_RESOURCE_STATES) Config::Instance()->ExposureResourceBarrier.value());

    if (paramReactiveMask && Config::Instance()->MaskResourceBarrier.has_value())
        ResourceBarrier(InCommandList, paramReactiveMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        (D3D12_RESOURCE_STATES) Config::Instance()->MaskResourceBarrier.value());

    _frameCount++;

    return true;
}

bool FSR31FeatureDx12::InitFSR3(const NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!ModuleLoaded())
        return false;

    if (IsInited())
        return true;

    if (Device == nullptr)
    {
        LOG_ERROR("D3D12Device is null!");
        return false;
    }

    State::Instance().skipSpoofing = true;

    ffxQueryDescGetVersions versionQuery {};
    versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    versionQuery.device = Device; // only for DirectX 12 applications
    uint64_t versionCount = 0;
    versionQuery.outputCount = &versionCount;
    // get number of versions for allocation
    FfxApiProxy::D3D12_Query(nullptr, &versionQuery.header);

    State::Instance().ffxUpscalerVersionIds.resize(versionCount);
    State::Instance().ffxUpscalerVersionNames.resize(versionCount);
    versionQuery.versionIds = State::Instance().ffxUpscalerVersionIds.data();
    versionQuery.versionNames = State::Instance().ffxUpscalerVersionNames.data();
    // fill version ids and names arrays.
    FfxApiProxy::D3D12_Query(nullptr, &versionQuery.header);

    _contextDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;

    _contextDesc.flags = 0;

#ifdef _DEBUG
    LOG_INFO("Debug checking enabled!");
    _contextDesc.fpMessage = FfxLogCallback;
    _contextDesc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
#endif

    if (DepthInverted())
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED;

    if (AutoExposure())
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;

    if (IsHdr())
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;

    if (JitteredMV())
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

    if (!LowResMV())
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;

    if (Config::Instance()->FsrNonLinearColorSpace.value_or_default())
    {
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
        LOG_INFO("contextDesc.initFlags (NonLinearColorSpace) {0:b}", _contextDesc.flags);
    }

    if (Config::Instance()->Fsr4EnableDebugView.value_or_default())
    {
        LOG_INFO("Debug view enabled!");
        _contextDesc.flags |= 512; // FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION
    }

    if (Config::Instance()->OutputScalingEnabled.value_or_default() && LowResMV())
    {
        float ssMulti = Config::Instance()->OutputScalingMultiplier.value_or_default();

        if (ssMulti < 0.5f)
        {
            ssMulti = 0.5f;
            Config::Instance()->OutputScalingMultiplier.set_volatile_value(ssMulti);
        }
        else if (ssMulti > 3.0f)
        {
            ssMulti = 3.0f;
            Config::Instance()->OutputScalingMultiplier.set_volatile_value(ssMulti);
        }

        _targetWidth = static_cast<unsigned int>(DisplayWidth() * ssMulti);
        _targetHeight = static_cast<unsigned int>(DisplayHeight() * ssMulti);
    }
    else
    {
        _targetWidth = DisplayWidth();
        _targetHeight = DisplayHeight();
    }

    // extended limits changes how resolution
    if (Config::Instance()->ExtendedLimits.value_or_default() && RenderWidth() > DisplayWidth())
    {
        _contextDesc.maxRenderSize.width = RenderWidth();
        _contextDesc.maxRenderSize.height = RenderHeight();

        Config::Instance()->OutputScalingMultiplier.set_volatile_value(1.0f);

        // if output scaling active let it to handle downsampling
        if (Config::Instance()->OutputScalingEnabled.value_or_default() && LowResMV())
        {
            _contextDesc.maxUpscaleSize.width = _contextDesc.maxRenderSize.width;
            _contextDesc.maxUpscaleSize.height = _contextDesc.maxRenderSize.height;

            // update target res
            _targetWidth = _contextDesc.maxRenderSize.width;
            _targetHeight = _contextDesc.maxRenderSize.height;
        }
        else
        {
            _contextDesc.maxUpscaleSize.width = DisplayWidth();
            _contextDesc.maxUpscaleSize.height = DisplayHeight();
        }
    }
    else
    {
        _contextDesc.maxRenderSize.width = TargetWidth() > DisplayWidth() ? TargetWidth() : DisplayWidth();
        _contextDesc.maxRenderSize.height = TargetHeight() > DisplayHeight() ? TargetHeight() : DisplayHeight();
        _contextDesc.maxUpscaleSize.width = TargetWidth();
        _contextDesc.maxUpscaleSize.height = TargetHeight();
    }

    ffxCreateBackendDX12Desc backendDesc = { 0 };
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.device = Device;

    _contextDesc.header.pNext = &backendDesc.header;

    if (Config::Instance()->FfxUpscalerIndex.value_or_default() < 0 ||
        Config::Instance()->FfxUpscalerIndex.value_or_default() >= State::Instance().ffxUpscalerVersionIds.size())
        Config::Instance()->FfxUpscalerIndex.set_volatile_value(0);

    ffxOverrideVersion override = { 0 };
    override.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    override.versionId =
        State::Instance().ffxUpscalerVersionIds[Config::Instance()->FfxUpscalerIndex.value_or_default()];
    backendDesc.header.pNext = &override.header;

    LOG_DEBUG("_createContext!");

    State::Instance().skipHeapCapture = true;
    auto ret = FfxApiProxy::D3D12_CreateContext(&_context, &_contextDesc.header, NULL);
    State::Instance().skipHeapCapture = false;

    if (ret != FFX_API_RETURN_OK)
    {
        LOG_ERROR("_createContext error: {0}", FfxApiProxy::ReturnCodeToString(ret));
        return false;
    }

    auto version = State::Instance().ffxUpscalerVersionNames[Config::Instance()->FfxUpscalerIndex.value_or_default()];
    _name = "FSR";
    parse_version(version);

    State::Instance().skipSpoofing = false;

    SetInit(true);

    return true;
}
