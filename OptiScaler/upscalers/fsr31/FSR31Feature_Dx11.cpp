#include <pch.h>
#include <Config.h>
#include <Util.h>

#include "FSR31Feature_Dx11.h"

#define ASSIGN_DESC(dest, src)                                                                                         \
    dest.Width = src.Width;                                                                                            \
    dest.Height = src.Height;                                                                                          \
    dest.Format = src.Format;                                                                                          \
    dest.BindFlags = src.BindFlags;

#define SAFE_RELEASE(p)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        if (p && p != nullptr)                                                                                         \
        {                                                                                                              \
            (p)->Release();                                                                                            \
            (p) = nullptr;                                                                                             \
        }                                                                                                              \
    } while ((void) 0, 0);

FSR31FeatureDx11::FSR31FeatureDx11(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : FSR31Feature(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters),
      IFeature(InHandleId, InParameters)
{
    _moduleLoaded = true;
}

bool FSR31FeatureDx11::Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    Device = InDevice;
    DeviceContext = InContext;

    if (InitFSR3(InParameters))
    {
        if (!Config::Instance()->OverlayMenu.value_or_default() && (Imgui == nullptr || Imgui.get() == nullptr))
            Imgui = std::make_unique<Menu_Dx11>(Util::GetProcessWindow(), Device);

        OutputScaler = std::make_unique<OS_Dx11>("Output Scaling", InDevice, (TargetWidth() < DisplayWidth()));
        RCAS = std::make_unique<RCAS_Dx11>("RCAS", InDevice);
        Bias = std::make_unique<Bias_Dx11>("Bias", InDevice);

        return true;
    }

    return false;
}

// register a DX11 resource to the backend
Fsr31::FfxResource ffxGetResource(ID3D11Resource* dx11Resource, wchar_t const* ffxResName,
                                  Fsr31::FfxResourceStates state = Fsr31::FFX_RESOURCE_STATE_COMPUTE_READ)
{
    Fsr31::FfxResource resource = {};
    resource.resource = reinterpret_cast<void*>(const_cast<ID3D11Resource*>(dx11Resource));
    resource.state = state;
    resource.description = Fsr31::GetFfxResourceDescriptionDX11(dx11Resource);

#ifdef _DEBUG
    if (ffxResName)
    {
        wcscpy_s(resource.name, ffxResName);
    }
#endif

    return resource;
}

bool FSR31FeatureDx11::CopyTexture(ID3D11Resource* InResource, D3D11_TEXTURE2D_RESOURCE_C* OutTextureRes,
                                   UINT bindFlags, bool InCopy)
{
    ID3D11Texture2D* originalTexture = nullptr;
    D3D11_TEXTURE2D_DESC desc {};

    auto result = InResource->QueryInterface(IID_PPV_ARGS(&originalTexture));

    if (result != S_OK)
        return false;

    originalTexture->GetDesc(&desc);

    if (desc.BindFlags == bindFlags)
    {
        ASSIGN_DESC(OutTextureRes->Desc, desc);
        OutTextureRes->Texture = originalTexture;
        OutTextureRes->usingOriginal = true;
        return true;
    }

    if (OutTextureRes->usingOriginal || OutTextureRes->Texture == nullptr || desc.Width != OutTextureRes->Desc.Width ||
        desc.Height != OutTextureRes->Desc.Height || desc.Format != OutTextureRes->Desc.Format ||
        desc.BindFlags != OutTextureRes->Desc.BindFlags)
    {
        if (OutTextureRes->Texture != nullptr)
        {
            if (!OutTextureRes->usingOriginal)
                OutTextureRes->Texture->Release();
            else
                OutTextureRes->Texture = nullptr;
        }

        OutTextureRes->usingOriginal = false;
        ASSIGN_DESC(OutTextureRes->Desc, desc);

        if (bindFlags != 9999)
            desc.BindFlags = bindFlags;

        result = Device->CreateTexture2D(&desc, nullptr, &OutTextureRes->Texture);

        if (result != S_OK)
        {
            LOG_ERROR("CreateTexture2D error: {0:x}", result);
            return false;
        }
    }

    if (InCopy)
        DeviceContext->CopyResource(OutTextureRes->Texture, InResource);

    return true;
}

void FSR31FeatureDx11::ReleaseResources()
{
    LOG_FUNC();

    if (!bufferColor.usingOriginal)
    {
        SAFE_RELEASE(bufferColor.Texture);
    }
}

bool FSR31FeatureDx11::Evaluate(ID3D11DeviceContext* DeviceContext, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!IsInited())
        return false;

    if (!RCAS->IsInit())
        Config::Instance()->RcasEnabled.set_volatile_value(false);

    if (!OutputScaler->IsInit())
        Config::Instance()->OutputScalingEnabled.set_volatile_value(false);

    ID3D11ShaderResourceView* restoreSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11SamplerState* restoreSamplerStates[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    ID3D11Buffer* restoreCBVs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ID3D11UnorderedAccessView* restoreUAVs[D3D11_1_UAV_SLOT_COUNT] = {};
    ID3D11RenderTargetView* restoreRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* restoreDSV = nullptr;

    // backup compute shader resources
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
    {
        restoreSRVs[i] = nullptr;
        DeviceContext->CSGetShaderResources(i, 1, &restoreSRVs[i]);
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
    {
        restoreSamplerStates[i] = nullptr;
        DeviceContext->CSGetSamplers(i, 1, &restoreSamplerStates[i]);
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
    {
        restoreCBVs[i] = nullptr;
        DeviceContext->CSGetConstantBuffers(i, 1, &restoreCBVs[i]);
    }

    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
    {
        restoreUAVs[i] = nullptr;
        DeviceContext->CSGetUnorderedAccessViews(i, 1, &restoreUAVs[i]);
    }

    DeviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, restoreRTVs, &restoreDSV);

    // Unbind RenderTargets
    ID3D11RenderTargetView* nullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    DeviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, nullRTVs, nullptr);

    Fsr31::FfxFsr3DispatchUpscaleDescription params {};

    if (Config::Instance()->FsrDebugView.value_or_default())
        params.flags = Fsr31::FFX_FSR3_UPSCALER_FLAG_DRAW_DEBUG_VIEW;

    if (Config::Instance()->FsrNonLinearPQ.value_or_default())
        params.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ;
    else if (Config::Instance()->FsrNonLinearSRGB.value_or_default())
        params.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;

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

    LOG_DEBUG("Jitter Offset: {0}x{1}", params.jitterOffset.x, params.jitterOffset.y);

    unsigned int reset;
    InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset);
    params.reset = (reset == 1);

    GetRenderResolution(InParameters, &params.renderSize.width, &params.renderSize.height);

    bool useSS = Config::Instance()->OutputScalingEnabled.value_or_default() && LowResMV();

    LOG_DEBUG("Input Resolution: {0}x{1}", params.renderSize.width, params.renderSize.height);

    params.commandList = Fsr31::ffxGetCommandListDX11(DeviceContext);

    ID3D11Resource* paramColor;
    if (InParameters->Get(NVSDK_NGX_Parameter_Color, &paramColor) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Color, (void**) &paramColor);

    if (paramColor)
    {
        LOG_DEBUG("Color exist..");

        if (!CopyTexture(paramColor, &bufferColor, 40, true))
        {
            LOG_DEBUG("Can't copy Color!");
            return false;
        }

        if (bufferColor.Texture != nullptr)
            params.color = ffxGetResource(bufferColor.Texture, L"FSR3_Input_OutputColor",
                                          Fsr31::FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        else
            params.color =
                ffxGetResource(paramColor, L"FSR3_Input_OutputColor", Fsr31::FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("Color not exist!!");
        return false;
    }

    ID3D11Resource* paramVelocity;
    if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramVelocity) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity);

    if (paramVelocity)
    {
        LOG_DEBUG("MotionVectors exist..");
        params.motionVectors =
            ffxGetResource(paramVelocity, L"FSR3_InputMotionVectors", Fsr31::FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("MotionVectors not exist!!");
        return false;
    }

    ID3D11Resource* paramOutput;
    if (InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput);

    if (paramOutput)
    {
        LOG_DEBUG("Output exist..");

        if (useSS)
        {
            if (OutputScaler->CreateBufferResource(Device, paramOutput, TargetWidth(), TargetHeight()))
            {
                params.upscaleOutput =
                    ffxGetResource(OutputScaler->Buffer(), L"FSR3_Output", Fsr31::FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            else
                params.upscaleOutput =
                    ffxGetResource(paramOutput, L"FSR3_Output", Fsr31::FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        else
            params.upscaleOutput =
                ffxGetResource(paramOutput, L"FSR3_Output", Fsr31::FFX_RESOURCE_STATE_UNORDERED_ACCESS);

        if (Config::Instance()->RcasEnabled.value_or_default() &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                                   Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
            RCAS->IsInit() && RCAS->CreateBufferResource(Device, (ID3D11Texture2D*) params.upscaleOutput.resource))
        {
            params.upscaleOutput =
                ffxGetResource(RCAS->Buffer(), L"FSR3_Output", Fsr31::FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }
    else
    {
        LOG_ERROR("Output not exist!!");
        return false;
    }

    ID3D11Resource* paramDepth;
    if (InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth);

    if (paramDepth)
    {
        LOG_DEBUG("Depth exist..");
        params.depth = ffxGetResource(paramDepth, L"FSR3_InputDepth", Fsr31::FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("Depth not exist!!");

        if (LowResMV())
            return false;
    }

    ID3D11Resource* paramExp = nullptr;
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
            params.exposure =
                ffxGetResource(paramExp, L"FSR3_InputExposure", Fsr31::FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
            LOG_DEBUG("ExposureTexture exist..");
        }
        else
        {
            LOG_DEBUG("AutoExposure disabled but ExposureTexture is not exist, it may cause problems!!");
            State::Instance().AutoExposure = true;
            State::Instance().changeBackend[Handle()->Id] = true;
            return true;
        }
    }

    ID3D11Resource* paramReactiveMask = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &paramReactiveMask) !=
        NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactiveMask);

    if (!Config::Instance()->DisableReactiveMask.value_or(paramReactiveMask == nullptr))
    {
        if (paramReactiveMask)
        {
            LOG_DEBUG("Input Bias mask exist..");
            Config::Instance()->DisableReactiveMask.set_volatile_value(false);

            if (Config::Instance()->FsrUseMaskForTransparency.value_or_default())
                params.transparencyAndComposition =
                    ffxGetResource(paramReactiveMask, L"FSR3_TransparencyAndCompositionMap",
                                   Fsr31::FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

            if (Config::Instance()->DlssReactiveMaskBias.value_or_default() > 0.0f && Bias->IsInit() &&
                Bias->CreateBufferResource(Device, paramReactiveMask) && Bias->CanRender())
            {
                if (Bias->Dispatch(Device, DeviceContext, (ID3D11Texture2D*) paramReactiveMask,
                                   Config::Instance()->DlssReactiveMaskBias.value_or_default(), Bias->Buffer()))
                {
                    params.reactive = ffxGetResource(Bias->Buffer(), L"FSR3_InputReactiveMap",
                                                     Fsr31::FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
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

    _hasColor = params.color.resource != nullptr;
    _hasDepth = params.depth.resource != nullptr;
    _hasMV = params.motionVectors.resource != nullptr;
    _hasExposure = params.exposure.resource != nullptr;
    _hasTM = params.transparencyAndComposition.resource != nullptr;
    _accessToReactiveMask = paramReactiveMask != nullptr;
    _hasOutput = params.upscaleOutput.resource != nullptr;

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
                        (float) DisplayHeight() * (float) DisplayWidth());
    else
        params.cameraFovAngleVertical = 1.0471975511966f;

    LOG_DEBUG("FsrVerticalFov: {0}", params.cameraFovAngleVertical);

    if (InParameters->Get(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, &params.frameTimeDelta) !=
            NVSDK_NGX_Result_Success ||
        params.frameTimeDelta < 1.0f)
        params.frameTimeDelta = (float) GetDeltaTime();

    LOG_DEBUG("FrameTimeDeltaInMsec: {0}", params.frameTimeDelta);

    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &params.preExposure) != NVSDK_NGX_Result_Success)
        params.preExposure = 1.0f;

    params.upscaleSize.width = TargetWidth();
    params.upscaleSize.height = TargetHeight();

    if (Version() >= feature_version { 3, 1, 1 } && _velocity != Config::Instance()->FsrVelocity.value_or_default())
    {
        _velocity = Config::Instance()->FsrVelocity.value_or_default();
        auto result = ffxFsr3SetUpscalerConstant(
            &_upscalerContext,
            Fsr31::FfxFsr3UpscalerConfigureKey::FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FVELOCITYFACTOR, &_velocity);

        if (result != Fsr31::FFX_OK)
            LOG_WARN("Velocity configure result: {}", (UINT) result);
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
    auto result = ffxFsr3ContextDispatchUpscale(&_upscalerContext, &params);

    if (result != Fsr31::FFX_OK)
    {
        LOG_ERROR("ffxFsr3ContextDispatch error: {0}", ResultToString(result));
        return false;
    }

    // apply rcas
    if (Config::Instance()->RcasEnabled.value_or_default() &&
        (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                               Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
        RCAS != nullptr && RCAS.get() != nullptr && RCAS->CanRender())
    {
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
            if (!RCAS->Dispatch(Device, DeviceContext, (ID3D11Texture2D*) params.upscaleOutput.resource,
                                (ID3D11Texture2D*) params.motionVectors.resource, rcasConstants,
                                OutputScaler->Buffer()))
            {
                Config::Instance()->RcasEnabled.set_volatile_value(false);
                return true;
            }
        }
        else
        {
            if (!RCAS->Dispatch(Device, DeviceContext, (ID3D11Texture2D*) params.upscaleOutput.resource,
                                (ID3D11Texture2D*) params.motionVectors.resource, rcasConstants,
                                (ID3D11Texture2D*) paramOutput))
            {
                Config::Instance()->RcasEnabled.set_volatile_value(false);
                return true;
            }
        }
    }

    if (useSS)
    {
        LOG_DEBUG("scaling output...");
        if (!OutputScaler->Dispatch(Device, DeviceContext, OutputScaler->Buffer(), (ID3D11Texture2D*) paramOutput))
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
                Imgui->Render(DeviceContext, paramOutput);
        }
        else
        {
            if (Imgui == nullptr || Imgui.get() == nullptr)
                Imgui = std::make_unique<Menu_Dx11>(GetForegroundWindow(), Device);
        }
    }

    // restore compute shader resources
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
    {
        if (restoreSRVs[i] != nullptr)
            DeviceContext->CSSetShaderResources(i, 1, &restoreSRVs[i]);
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
    {
        if (restoreSamplerStates[i] != nullptr)
            DeviceContext->CSSetSamplers(i, 1, &restoreSamplerStates[i]);
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
    {
        if (restoreCBVs[i] != nullptr)
            DeviceContext->CSSetConstantBuffers(i, 1, &restoreCBVs[i]);
    }

    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
    {
        if (restoreUAVs[i] != nullptr)
            DeviceContext->CSSetUnorderedAccessViews(i, 1, &restoreUAVs[i], 0);
    }

    DeviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, restoreRTVs, restoreDSV);

    _frameCount++;

    return true;
}

FSR31FeatureDx11::~FSR31FeatureDx11()
{
    if (!IsInited())
        return;

    if (!State::Instance().isShuttingDown)
    {
        auto errorCode = Fsr31::ffxFsr3ContextDestroy(&_upscalerContext);

        if (errorCode != Fsr31::FFX_OK)
            spdlog::error("FSR31FeatureDx11::~FSR31FeatureDx11 ffxFsr3ContextDestroy error: {0:x}", errorCode);

        free(_upscalerContextDesc.backendInterfaceUpscaling.scratchBuffer);
    }

    SetInit(false);
}

bool FSR31FeatureDx11::InitFSR3(const NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!ModuleLoaded())
        return false;

    if (IsInited())
        return true;

    if (Device == nullptr)
    {
        LOG_ERROR("D3D11Device is null!");
        return false;
    }

    if (Device->GetFeatureLevel() == D3D_FEATURE_LEVEL_11_0)
    {
        LOG_INFO("This D3D11 Device is running under D3D_FEATURE_LEVEL_11_0, this is incompatible with FSR 3.1 and "
                 "will probably not work.");
        LOG_INFO("If FSR3.1 doesn't work, try enabling the OptiScaler Direct3D hooks by setting OverlayMenu=true.");
    }

    State::Instance().skipSpoofing = true;
    uint64_t versionCount = 0;
    State::Instance().ffxUpscalerVersionIds.resize(versionCount);
    State::Instance().ffxUpscalerVersionNames.resize(versionCount);
    State::Instance().ffxUpscalerVersionIds.push_back(1);
    auto version_number = "3.1.2";
    State::Instance().ffxUpscalerVersionNames.push_back(version_number);

    const size_t scratchBufferSize = Fsr31::ffxGetScratchMemorySizeDX11(1);
    void* scratchBuffer = calloc(scratchBufferSize, 1);

    auto errorCode =
        Fsr31::ffxGetInterfaceDX11(&_upscalerContextDesc.backendInterfaceUpscaling,
                                   Fsr31::ffxGetDeviceDX11_Fsr31(Device), scratchBuffer, scratchBufferSize, 1);

    if (errorCode != Fsr31::FFX_OK)
    {
        LOG_ERROR("ffxGetInterfaceDX11 error when creating backendInterfaceUpscaling: {0}", ResultToString(errorCode));
        free(scratchBuffer);
        return false;
    }

    _upscalerContextDesc.fpMessage = FfxLogCallback;
    _upscalerContextDesc.flags = 0;

    _upscalerContextDesc.flags |= Fsr31::FFX_FSR3_ENABLE_UPSCALING_ONLY;
#ifdef _DEBUG
    _upscalerContextDesc.flags |= Fsr31::FFX_FSR3_ENABLE_DEBUG_CHECKING;
#endif

    if (DepthInverted())
        _upscalerContextDesc.flags |= Fsr31::FFX_FSR3_ENABLE_DEPTH_INVERTED;

    if (AutoExposure())
        _upscalerContextDesc.flags |= Fsr31::FFX_FSR3_ENABLE_AUTO_EXPOSURE;

    if (IsHdr())
        _upscalerContextDesc.flags |= Fsr31::FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;

    if (JitteredMV())
        _upscalerContextDesc.flags |= Fsr31::FFX_FSR3_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

    if (!LowResMV())
        _upscalerContextDesc.flags |= Fsr31::FFX_FSR3_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;

    if (Config::Instance()->FsrNonLinearPQ.value_or_default() ||
        Config::Instance()->FsrNonLinearSRGB.value_or_default())
    {
        _upscalerContextDesc.flags |= FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
        LOG_INFO("contextDesc.initFlags (NonLinearColorSpace) {0:b}", _upscalerContextDesc.flags);
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
        _upscalerContextDesc.maxRenderSize.width = RenderWidth();
        _upscalerContextDesc.maxRenderSize.height = RenderHeight();

        Config::Instance()->OutputScalingMultiplier.set_volatile_value(1.0f);

        // if output scaling active let it to handle downsampling
        if (Config::Instance()->OutputScalingEnabled.value_or_default() && LowResMV())
        {
            _upscalerContextDesc.maxUpscaleSize.width = _upscalerContextDesc.maxRenderSize.width;
            _upscalerContextDesc.maxUpscaleSize.height = _upscalerContextDesc.maxRenderSize.height;
            // update target res
            _targetWidth = _upscalerContextDesc.maxRenderSize.width;
            _targetHeight = _upscalerContextDesc.maxRenderSize.height;
        }
        else
        {
            _upscalerContextDesc.maxUpscaleSize.width = DisplayWidth();
            _upscalerContextDesc.maxUpscaleSize.height = DisplayHeight();
        }
    }
    else
    {
        _upscalerContextDesc.maxRenderSize.width = TargetWidth() > DisplayWidth() ? TargetWidth() : DisplayWidth();
        _upscalerContextDesc.maxRenderSize.height = TargetHeight() > DisplayHeight() ? TargetHeight() : DisplayHeight();
        _upscalerContextDesc.maxUpscaleSize.width = TargetWidth();
        _upscalerContextDesc.maxUpscaleSize.height = TargetHeight();
    }

    if (Config::Instance()->FfxUpscalerIndex.value_or_default() < 0 ||
        Config::Instance()->FfxUpscalerIndex.value_or_default() >= State::Instance().ffxUpscalerVersionIds.size())
        Config::Instance()->FfxUpscalerIndex.set_volatile_value(0);

    LOG_DEBUG("_createContext!");
    auto ret = ffxFsr3ContextCreate(&_upscalerContext, &_upscalerContextDesc);

    if (ret != Fsr31::FFX_OK)
    {
        LOG_ERROR("_createContext error: {0}", ResultToString(ret));
        return false;
    }

    LOG_INFO("_createContext success!");

    auto version = State::Instance().ffxUpscalerVersionNames[Config::Instance()->FfxUpscalerIndex.value_or_default()];
    _name = "FSR";
    parse_version(version);

    State::Instance().skipSpoofing = false;

    SetInit(true);

    return true;
}
