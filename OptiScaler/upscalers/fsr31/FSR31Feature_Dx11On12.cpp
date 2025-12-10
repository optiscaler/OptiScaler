#include <pch.h>
#include <Config.h>
#include <Util.h>

#include <proxies/FfxApi_Proxy.h>

#include "FSR31Feature_Dx11On12.h"

NVSDK_NGX_Parameter* FSR31FeatureDx11on12::SetParameters(NVSDK_NGX_Parameter* InParameters)
{
    InParameters->Set("OptiScaler.SupportsUpscaleSize", true);
    return InParameters;
}

FSR31FeatureDx11on12::FSR31FeatureDx11on12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : FSR31Feature(InHandleId, InParameters), IFeature_Dx11wDx12(InHandleId, InParameters),
      IFeature_Dx11(InHandleId, InParameters), IFeature(InHandleId, SetParameters(InParameters))
{
    FfxApiProxy::InitFfxDx12();

    _moduleLoaded = FfxApiProxy::IsSRReady();

    if (_moduleLoaded)
        LOG_INFO("amd_fidelityfx_dx12.dll methods loaded!");
    else
        LOG_ERROR("can't load amd_fidelityfx_dx12.dll methods!");
}

bool FSR31FeatureDx11on12::Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext,
                                NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    Device = InDevice;
    DeviceContext = InContext;

    _baseInit = false;
    return _moduleLoaded;
}

bool FSR31FeatureDx11on12::Evaluate(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!_baseInit)
    {
        // to prevent creation dx12 device if we are going to recreate feature
        ID3D11Resource* paramVelocity = nullptr;
        if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramVelocity) != NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity);

        if (AutoExposure())
        {
            LOG_DEBUG("AutoExposure enabled!");
        }
        else
        {
            ID3D11Resource* paramExpo = nullptr;
            if (InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, &paramExpo) != NVSDK_NGX_Result_Success)
                InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, (void**) &paramExpo);

            if (paramExpo == nullptr)
            {
                LOG_WARN("ExposureTexture is not exist, enabling AutoExposure!!");
                State::Instance().AutoExposure = true;
            }
        }

        ID3D11Resource* paramReactiveMask = nullptr;
        if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &paramReactiveMask) !=
            NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactiveMask);
        _accessToReactiveMask = paramReactiveMask != nullptr;

        if (!Config::Instance()->DisableReactiveMask.has_value())
        {
            if (!paramReactiveMask)
            {
                LOG_WARN("Bias mask not exist, enabling DisableReactiveMask!!");
                Config::Instance()->DisableReactiveMask.set_volatile_value(true);
            }
        }

        if (!BaseInit(Device, InDeviceContext, InParameters))
        {
            LOG_DEBUG("BaseInit failed!");
            return false;
        }

        _baseInit = true;

        LOG_DEBUG("calling InitFSR3");

        if (State::Instance().currentD3D12Device == nullptr)
        {
            LOG_ERROR("Dx12on11Device is null!");
            return false;
        }

        if (!InitFSR3(InParameters))
        {
            LOG_ERROR("InitFSR2 fail!");
            return false;
        }

        if (!Config::Instance()->OverlayMenu.value_or_default() && (Imgui == nullptr || Imgui.get() == nullptr))
            Imgui = std::make_unique<Menu_Dx11>(GetForegroundWindow(), Device);

        if (Config::Instance()->Dx11DelayedInit.value_or_default())
        {
            LOG_TRACE("sleeping after FSRContext creation for 1500ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }

        OutputScaler = std::make_unique<OS_Dx12>("Output Scaling", State::Instance().currentD3D12Device,
                                                 (TargetWidth() < DisplayWidth()));
        RCAS = std::make_unique<RCAS_Dx12>("RCAS", State::Instance().currentD3D12Device);
        Bias = std::make_unique<Bias_Dx12>("Bias", State::Instance().currentD3D12Device);
    }

    if (!IsInited())
        return false;

    if (!RCAS->IsInit())
        Config::Instance()->RcasEnabled.set_volatile_value(false);

    if (!OutputScaler->IsInit())
        Config::Instance()->OutputScalingEnabled.set_volatile_value(false);

    ID3D11DeviceContext4* dc;
    auto result = InDeviceContext->QueryInterface(IID_PPV_ARGS(&dc));

    if (result != S_OK)
    {
        LOG_ERROR("QueryInterface error: {0:x}", result);
        return false;
    }

    if (dc != Dx11DeviceContext)
    {
        LOG_WARN("Dx11DeviceContext changed!");
        ReleaseSharedResources();
        Dx11DeviceContext = dc;
    }

    if (dc != nullptr)
        dc->Release();

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

    unsigned int reset;
    InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset);
    params.reset = (reset == 1);

    GetRenderResolution(InParameters, &params.renderSize.width, &params.renderSize.height);

    bool useSS = Config::Instance()->OutputScalingEnabled.value_or_default() && LowResMV();

    LOG_DEBUG("Input Resolution: {0}x{1}", params.renderSize.width, params.renderSize.height);

    auto frame = _frameCount % 2;
    auto cmdList = Dx12CommandList[frame];

    params.commandList = cmdList;

    ffxReturnCode_t ffxresult = FFX_API_RETURN_ERROR;

    uint8_t state = 0;

    do
    {
        if (!ProcessDx11Textures(InParameters))
        {
            LOG_ERROR("Can't process Dx11 textures!");
            break;
        }

        if (State::Instance().changeBackend[Handle()->Id])
        {
            break;
        }

        params.color = ffxApiGetResourceDX12(dx11Color.Dx12Resource, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        params.motionVectors = ffxApiGetResourceDX12(dx11Mv.Dx12Resource, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        params.depth = ffxApiGetResourceDX12(dx11Depth.Dx12Resource, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        params.exposure = ffxApiGetResourceDX12(dx11Exp.Dx12Resource, FFX_API_RESOURCE_STATE_COMPUTE_READ);

        if (dx11Reactive.Dx12Resource != nullptr)
        {
            if (Config::Instance()->FsrUseMaskForTransparency.value_or_default())
                params.transparencyAndComposition =
                    ffxApiGetResourceDX12(dx11Reactive.Dx12Resource, FFX_API_RESOURCE_STATE_COMPUTE_READ);

            if (Config::Instance()->DlssReactiveMaskBias.value_or_default() > 0.0f && Bias->IsInit() &&
                Bias->CreateBufferResource(State::Instance().currentD3D12Device, dx11Reactive.Dx12Resource,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
                Bias->CanRender())
            {
                state = 1;
                Bias->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                if (Bias->Dispatch(State::Instance().currentD3D12Device, cmdList, dx11Reactive.Dx12Resource,
                                   Config::Instance()->DlssReactiveMaskBias.value_or_default(), Bias->Buffer()))
                {
                    Bias->SetBufferState(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    params.reactive = ffxApiGetResourceDX12(Bias->Buffer(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
                }
            }
        }

        // Output Scaling
        if (useSS)
        {
            if (OutputScaler->CreateBufferResource(State::Instance().currentD3D12Device, dx11Out.Dx12Resource,
                                                   TargetWidth(), TargetHeight(),
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            {
                state = 1;

                OutputScaler->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                params.output = ffxApiGetResourceDX12(OutputScaler->Buffer(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            else
                params.output = ffxApiGetResourceDX12(dx11Out.Dx12Resource, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        else
            params.output = ffxApiGetResourceDX12(dx11Out.Dx12Resource, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

        // RCAS
        if (Config::Instance()->RcasEnabled.value_or_default() &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                                   Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
            RCAS->IsInit() &&
            RCAS->CreateBufferResource(State::Instance().currentD3D12Device, (ID3D12Resource*) params.output.resource,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
        {
            state = 1;
            RCAS->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            params.output = ffxApiGetResourceDX12(RCAS->Buffer(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        _hasColor = params.color.resource != nullptr;
        _hasDepth = params.depth.resource != nullptr;
        _hasMV = params.motionVectors.resource != nullptr;
        _hasExposure = params.exposure.resource != nullptr;
        _hasTM = params.transparencyAndComposition.resource != nullptr;
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

        if (DepthInverted())
        {
            params.cameraFar = Config::Instance()->FsrCameraNear.value_or_default();
            params.cameraNear = Config::Instance()->FsrCameraFar.value_or_default();
        }
        else
        {
            params.cameraFar = Config::Instance()->FsrCameraFar.value_or_default();
            params.cameraNear = Config::Instance()->FsrCameraNear.value_or_default();
        }

        State::Instance().lastFsrCameraFar = params.cameraFar;
        State::Instance().lastFsrCameraNear = params.cameraNear;

        if (Config::Instance()->FsrVerticalFov.has_value())
            params.cameraFovAngleVertical = Config::Instance()->FsrVerticalFov.value() * 0.0174532925199433f;
        else if (Config::Instance()->FsrHorizontalFov.value_or_default() > 0.0f)
            params.cameraFovAngleVertical =
                2.0f * atan((tan(Config::Instance()->FsrHorizontalFov.value() * 0.0174532925199433f) * 0.5f) /
                            (float) TargetHeight() * (float) TargetWidth());
        else
            params.cameraFovAngleVertical = 1.0471975511966f;

        if (InParameters->Get(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, &params.frameTimeDelta) !=
                NVSDK_NGX_Result_Success ||
            params.frameTimeDelta < 1.0f)
            params.frameTimeDelta = (float) GetDeltaTime();

        if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &params.preExposure) != NVSDK_NGX_Result_Success)
            params.preExposure = 1.0f;

        params.viewSpaceToMetersFactor = 1.0f;

        // Version 3.1.1 check
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
        ffxresult = FfxApiProxy::D3D12_Dispatch(&_context, &params.header);
        state = 1;

        if (ffxresult != FFX_API_RETURN_OK)
        {
            LOG_ERROR("ffxFsr2ContextDispatch error: {0}", FfxApiProxy::ReturnCodeToString(ffxresult));
            break;
        }

        // apply rcas
        if (Config::Instance()->RcasEnabled.value_or_default() &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                                   Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
            RCAS->CanRender())
        {
            LOG_DEBUG("Apply CAS");
            if (params.output.resource != RCAS->Buffer())
                ResourceBarrier(cmdList, (ID3D12Resource*) params.output.resource,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            RCAS->SetBufferState(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

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
                if (!RCAS->Dispatch(
                        State::Instance().currentD3D12Device, cmdList, (ID3D12Resource*) params.output.resource,
                        (ID3D12Resource*) params.motionVectors.resource, rcasConstants, OutputScaler->Buffer()))
                {
                    Config::Instance()->RcasEnabled.set_volatile_value(false);
                    break;
                }
            }
            else
            {
                if (!RCAS->Dispatch(
                        State::Instance().currentD3D12Device, cmdList, (ID3D12Resource*) params.output.resource,
                        (ID3D12Resource*) params.motionVectors.resource, rcasConstants, dx11Out.Dx12Resource))
                {
                    Config::Instance()->RcasEnabled.set_volatile_value(false);
                    break;
                }
            }
        }

        if (useSS)
        {
            LOG_DEBUG("downscaling output...");
            OutputScaler->SetBufferState(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            if (!OutputScaler->Dispatch(State::Instance().currentD3D12Device, cmdList, OutputScaler->Buffer(),
                                        dx11Out.Dx12Resource))
            {
                Config::Instance()->OutputScalingEnabled.set_volatile_value(false);
                State::Instance().changeBackend[Handle()->Id] = true;

                break;
            }
        }

        state = 2;

    } while (false);

    if (state > 0)
    {
        cmdList->Close();
        ID3D12CommandList* ppCommandLists[] = { cmdList };
        Dx12CommandQueue->ExecuteCommandLists(1, ppCommandLists);
        Dx12CommandQueue->Signal(dx12FenceTextureCopy, _fenceValue);
    }

    auto evalResult = false;

    do
    {
        if (state != 2)
            break;

        if (ffxresult != FFX_API_RETURN_OK)
            break;

        if (!CopyBackOutput())
        {
            LOG_ERROR("Can't copy output texture back!");
            break;
        }

        // imgui - legacy menu disabled for now
        // if (!Config::Instance()->OverlayMenu.value_or_default() && _frameCount > 30)
        //{
        //    if (Imgui != nullptr && Imgui.get() != nullptr)
        //    {
        //        LOG_DEBUG("Apply Imgui");

        //        if (Imgui->IsHandleDifferent())
        //        {
        //            LOG_DEBUG("Imgui handle different, reset()!");
        //            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        //            Imgui.reset();
        //        }
        //        else
        //            Imgui->Render(InDeviceContext, paramOutput[_frameCount % 2]);
        //    }
        //    else
        //    {
        //        if (Imgui == nullptr || Imgui.get() == nullptr)
        //            Imgui = std::make_unique<Menu_Dx11>(GetForegroundWindow(), Device);
        //    }
        //}

        evalResult = true;

    } while (false);

    _frameCount++;
    Dx12CommandQueue->Signal(Dx12Fence, _frameCount);

    return evalResult;
}

bool FSR31FeatureDx11on12::InitFSR3(const NVSDK_NGX_Parameter* InParameters)
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
    versionQuery.device = State::Instance().currentD3D12Device; // only for DirectX 12 applications
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

    _contextDesc.flags = 0;
    _contextDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;

#ifdef _DEBUG
    LOG_INFO("Debug checking enabled!");
    _contextDesc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
    _contextDesc.fpMessage = FfxLogCallback;
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

    if (Config::Instance()->Fsr4EnableDebugView.value_or_default())
    {
        LOG_INFO("Debug view enabled!");
        _contextDesc.flags |= 512; // FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION
    }

    if (Config::Instance()->FsrNonLinearColorSpace.value_or_default())
    {
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
        LOG_INFO("contextDesc.initFlags (NonLinearColorSpace) {0:b}", _contextDesc.flags);
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
    backendDesc.device = State::Instance().currentD3D12Device;
    _contextDesc.header.pNext = &backendDesc.header;

    if (Config::Instance()->FfxUpscalerIndex.value_or_default() < 0 ||
        Config::Instance()->FfxUpscalerIndex.value_or_default() >= State::Instance().ffxUpscalerVersionIds.size())
        Config::Instance()->FfxUpscalerIndex.set_volatile_value(0);

    ffxOverrideVersion ov = { 0 };
    ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    ov.versionId = State::Instance().ffxUpscalerVersionIds[Config::Instance()->FfxUpscalerIndex.value_or_default()];
    backendDesc.header.pNext = &ov.header;

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

    LOG_TRACE("sleeping after _createContext creation for 500ms");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    State::Instance().skipSpoofing = false;

    SetInit(true);

    return true;
}
