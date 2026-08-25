#include <pch.h>
#include <Config.h>
#include <Util.h>
#include "MathUtils.h"
#include "FSR2Feature_Dx11.h"

using namespace OptiMath;

#define ASSIGN_DESC(dest, src)                                                                                         \
    dest.Width = src.Width;                                                                                            \
    dest.Height = src.Height;                                                                                          \
    dest.Format = src.Format;                                                                                          \
    dest.BindFlags = src.BindFlags;

bool FSR2FeatureDx11::InitInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    return InitFSR2(InParameters);
}

bool FSR2FeatureDx11::CopyTexture(ID3D11Resource* InResource, D3D11_TEXTURE2D_RESOURCE_C* OutTextureRes, UINT bindFlags,
                                  bool InCopy)
{
    ID3D11Texture2D* originalTexture = nullptr;
    D3D11_TEXTURE2D_DESC desc {};

    auto result = InResource->QueryInterface(IID_PPV_ARGS(&originalTexture));

    if (result != S_OK)
        return false;

    originalTexture->Release();
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

void FSR2FeatureDx11::ReleaseResources()
{
    if (!bufferColor.usingOriginal)
    {
        SAFE_RELEASE(bufferColor.Texture);
    }
}

bool FSR2FeatureDx11::InitFSR2(const NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    if (Device == nullptr)
    {
        LOG_ERROR("D3D12Device is null!");
        return false;
    }

    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

        const size_t scratchBufferSize = ffxFsr2GetScratchMemorySizeDX11();
        void* scratchBuffer = calloc(scratchBufferSize, 1);

        auto errorCode = ffxFsr2GetInterfaceDX11(&_contextDesc.callbacks, Device, scratchBuffer, scratchBufferSize);

        if (errorCode != FFX_OK)
        {
            LOG_ERROR("ffxGetInterfaceDX12 error: {0}", ResultToString(errorCode));
            free(scratchBuffer);
            return false;
        }

        _contextDesc.device = ffxGetDeviceDX11(Device);
        _contextDesc.flags = 0;

        if (DepthInverted())
            _contextDesc.flags |= FFX_FSR2_ENABLE_DEPTH_INVERTED;

        if (AutoExposure())
            _contextDesc.flags |= FFX_FSR2_ENABLE_AUTO_EXPOSURE;

        if (IsHdr())
            _contextDesc.flags |= FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;

        if (JitteredMV())
            _contextDesc.flags |= FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

        if (!LowResMV())
            _contextDesc.flags |= FFX_FSR2_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;

        // output scaling
        if (Config::Instance()->OutputScalingEnabled.value_or_default() &&
            (LowResMV() || RenderWidth() == DisplayWidth()))
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
            if (Config::Instance()->OutputScalingEnabled.value_or_default() &&
                (LowResMV() || RenderWidth() == DisplayWidth()))
            {

                _contextDesc.displaySize.width = _contextDesc.maxRenderSize.width;
                _contextDesc.displaySize.height = _contextDesc.maxRenderSize.height;

                // update target res
                _targetWidth = _contextDesc.maxRenderSize.width;
                _targetHeight = _contextDesc.maxRenderSize.height;
            }
            else
            {
                _contextDesc.displaySize.width = DisplayWidth();
                _contextDesc.displaySize.height = DisplayHeight();
            }
        }
        else
        {
            _contextDesc.maxRenderSize.width = TargetWidth() > DisplayWidth() ? TargetWidth() : DisplayWidth();
            _contextDesc.maxRenderSize.height = TargetHeight() > DisplayHeight() ? TargetHeight() : DisplayHeight();
            _contextDesc.displaySize.width = TargetWidth();
            _contextDesc.displaySize.height = TargetHeight();
        }

#if _DEBUG
        _contextDesc.flags |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
        _contextDesc.fpMessage = FfxLogCallback;
#endif

        LOG_DEBUG("ffxFsr2ContextCreate!");

        errorCode = ffxFsr2ContextCreate(&_context, &_contextDesc);

        if (errorCode != FFX_OK)
        {
            LOG_ERROR("ffxFsr2ContextCreate error: {0}", ResultToString(errorCode));
            return false;
        }
    }

    SetInit(true);

    return true;
}

FSR2FeatureDx11::FSR2FeatureDx11(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters), FSR2Feature(InHandleId, InParameters)
{
}

bool FSR2FeatureDx11::EvaluateInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!IsInited())
        return false;

    auto& state = State::Instance();
    auto& cfg = *Config::Instance();
    const auto& ngxParams = *InParameters;

    FfxFsr2DispatchDescription params {};
    params.commandList = InContext;

    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &params.jitterOffset.x);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &params.jitterOffset.y);

    unsigned int reset;
    InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset);
    params.reset = (reset == 1);

    GetRenderResolution(InParameters, &params.renderSize.width, &params.renderSize.height);

    LOG_DEBUG("Input Resolution: {0}x{1}", params.renderSize.width, params.renderSize.height);

    params.enableSharpening = _sharpness > 0.0f;
    params.sharpness = _sharpness;

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
            params.color = ffxGetResourceDX11(&_context, bufferColor.Texture, (wchar_t*) L"FSR2_Color");
        else
            params.color = ffxGetResourceDX11(&_context, paramColor, (wchar_t*) L"FSR2_Color");
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
        params.motionVectors = ffxGetResourceDX11(&_context, paramVelocity, (wchar_t*) L"FSR2_MotionVectors");
    }
    else
    {
        LOG_ERROR("MotionVectors not exist!!");
        return false;
    }

    auto outIndex = _frameCount % 2;

    ID3D11Resource* paramOutput;
    if (InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput);

    if (paramOutput)
    {
        LOG_DEBUG("Output exist..");
        params.output =
            ffxGetResourceDX11(&_context, paramOutput, (wchar_t*) L"FSR2_Output", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
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
        params.depth = ffxGetResourceDX11(&_context, paramDepth, (wchar_t*) L"FSR2_Depth");
    }
    else
    {
        LOG_ERROR("Depth not exist!!");
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
            params.exposure = ffxGetResourceDX11(&_context, paramExp, (wchar_t*) L"FSR2_Exposure");
            LOG_DEBUG("ExposureTexture exist..");
        }
        else
        {
            LOG_DEBUG("AutoExposure disabled but ExposureTexture is not exist, it may cause problems!!");
            State::Instance().autoExposure = true;
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
                params.transparencyAndComposition = ffxGetResourceDX11(
                    &_context, paramReactiveMask, (wchar_t*) L"FSR2_Transparency", FFX_RESOURCE_STATE_COMPUTE_READ);

            if (Config::Instance()->DlssReactiveMaskBias.value_or_default() > 0.0f && Bias->IsInit() &&
                Bias->CreateBufferResource(Device, paramReactiveMask) && Bias->CanRender())
            {
                if (Bias->Dispatch(Device, InContext, (ID3D11Texture2D*) paramReactiveMask,
                                   Config::Instance()->DlssReactiveMaskBias.value_or_default(), Bias->Buffer()))
                {
                    params.reactive = ffxGetResourceDX11(&_context, Bias->Buffer(), (wchar_t*) L"FSR2_Reactive",
                                                         FFX_RESOURCE_STATE_COMPUTE_READ);
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
    _hasOutput = params.output.resource != nullptr;

    params.motionVectorScale.x = 1.0f;
    params.motionVectorScale.y = 1.0f;

    if (InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &params.motionVectorScale.x) != NVSDK_NGX_Result_Success ||
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &params.motionVectorScale.y) != NVSDK_NGX_Result_Success)
    {
        LOG_WARN("Can't get motion vector scales!");
    }

    if (DepthInverted())
    {
        params.cameraFar = cfg.FsrCameraNear.value_or_default();
        params.cameraNear = cfg.FsrCameraFar.value_or_default();
    }
    else
    {
        params.cameraFar = cfg.FsrCameraFar.value_or_default();
        params.cameraNear = cfg.FsrCameraNear.value_or_default();
    }

    if (cfg.FsrVerticalFov.has_value())
        params.cameraFovAngleVertical = GetRadiansFromDeg(cfg.FsrVerticalFov.value());
    else if (cfg.FsrHorizontalFov.value_or_default() > 0.0f)
    {
        const float hFovRad = GetRadiansFromDeg(cfg.FsrHorizontalFov.value());
        params.cameraFovAngleVertical =
            GetVerticalFovFromHorizontal(hFovRad, (float) TargetWidth(), (float) TargetHeight());
    }
    else
        params.cameraFovAngleVertical = GetRadiansFromDeg(60);

    if (InParameters->Get(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, &params.frameTimeDelta) !=
            NVSDK_NGX_Result_Success ||
        params.frameTimeDelta < 1.0f)
        params.frameTimeDelta = (float) GetDeltaTime();

    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &params.preExposure) != NVSDK_NGX_Result_Success)
        params.preExposure = 1.0f;

    LOG_DEBUG("Dispatch!!");
    auto result = ffxFsr2ContextDispatch(&_context, &params);

    if (result != FFX_OK)
    {
        LOG_ERROR("ffxFsr2ContextDispatch error: {0}", ResultToString(result));
        return false;
    }

    return true;
}

FSR2FeatureDx11::~FSR2FeatureDx11() { ReleaseResources(); }
