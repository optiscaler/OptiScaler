#include "pch.h"
#include "FSR2_Dx11.h"
#include "NVNGX_DLSS.h"

#include "Util.h"
#include "Config.h"
#include "resource.h"
#include "NVNGX_Parameter.h"
#include <proxies/NVNGX_Proxy.h>

#include <proxies/KernelBase_Proxy.h>

#include "scanner/scanner.h"
#include "detours/detours.h"

#include "fsr2/ffx_fsr2.h"
#include "fsr2/dx11/ffx_fsr2_dx11.h"

typedef FfxErrorCode (*PFN_ffxFsr2ContextCreate)(FfxFsr2Context* context,
                                                 const FfxFsr2ContextDescription* contextDescription);
typedef FfxErrorCode (*PFN_ffxFsr2ContextDispatch)(FfxFsr2Context* context,
                                                   const FfxFsr2DispatchDescription* dispatchDescription);
typedef FfxErrorCode (*PFN_ffxFsr2ContextGenerateReactiveMask)(FfxFsr2Context* context,
                                                               const FfxFsr2GenerateReactiveDescription* params);
typedef FfxErrorCode (*PFN_ffxFsr2ContextDestroy)(FfxFsr2Context* context);
typedef float (*PFN_ffxFsr2GetUpscaleRatioFromQualityMode)(FfxFsr2QualityMode qualityMode);
typedef FfxErrorCode (*PFN_ffxFsr2GetRenderResolutionFromQualityMode)(uint32_t* renderWidth, uint32_t* renderHeight,
                                                                      uint32_t displayWidth, uint32_t displayHeight,
                                                                      FfxFsr2QualityMode qualityMode);

// Extras
typedef int32_t (*PFN_ffxFsr2GetJitterPhaseCount)(int32_t renderWidth, int32_t displayWidth);
typedef FfxErrorCode (*PFN_ffxFsr2ContextGenerateReactiveMask)(FfxFsr2Context* context,
                                                               const FfxFsr2GenerateReactiveDescription* params);

static PFN_ffxFsr2ContextCreate o_ffxFsr2ContextCreate_Dx11 = nullptr;
static PFN_ffxFsr2ContextDispatch o_ffxFsr2ContextDispatch_Dx11 = nullptr;
static PFN_ffxFsr2ContextDestroy o_ffxFsr2ContextDestroy_Dx11 = nullptr;
static PFN_ffxFsr2GetUpscaleRatioFromQualityMode o_ffxFsr2GetUpscaleRatioFromQualityMode_Dx11 = nullptr;
static PFN_ffxFsr2GetRenderResolutionFromQualityMode o_ffxFsr2GetRenderResolutionFromQualityMode_Dx11 = nullptr;
static PFN_ffxFsr2GetJitterPhaseCount o_ffxFsr2GetJitterPhaseCount_Dx11 = nullptr;

static std::unordered_map<FfxFsr2Context*, FfxFsr2ContextDescription> _initParams;
static std::unordered_map<FfxFsr2Context*, NVSDK_NGX_Parameter*> _nvParams;
static std::unordered_map<FfxFsr2Context*, NVSDK_NGX_Handle*> _contexts;
static ID3D11Device* _d3d11Device = nullptr;
// Device this input initialised NGX with, cleared on destroy as a new device can get its address
static std::atomic<ID3D11Device*> _nvngxDevice { nullptr };
static bool _nvngxInited = false;
static bool _skipCreate = false;
static bool _skipDispatch = false;
static bool _skipDestroy = false;
static float qualityRatios[] = { 1.0f, 1.5f, 1.7f, 2.0f, 3.0f };

static bool CreateDLSSContext(FfxFsr2Context* handle, const FfxFsr2DispatchDescription* pExecParams)
{
    LOG_DEBUG("");

    if (!_nvParams.contains(handle))
        return false;

    NVSDK_NGX_Handle* nvHandle = nullptr;
    auto params = _nvParams[handle];
    auto initParams = &_initParams[handle];
    auto commandList = (ID3D11DeviceContext*) pExecParams->commandList;

    UINT initFlags = 0;

    if (initParams->flags & FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;

    if (initParams->flags & FFX_FSR2_ENABLE_DEPTH_INVERTED)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;

    if (initParams->flags & FFX_FSR2_ENABLE_AUTO_EXPOSURE)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

    if (initParams->flags & FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;

    if ((initParams->flags & FFX_FSR2_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) == 0)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

    State::Instance().dynamicResolution = (initParams->flags & FFX_FSR2_ENABLE_DYNAMIC_RESOLUTION) != 0;

    params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, initFlags);

    params->Set(NVSDK_NGX_Parameter_Width, pExecParams->renderSize.width);
    params->Set(NVSDK_NGX_Parameter_Height, pExecParams->renderSize.height);
    params->Set(NVSDK_NGX_Parameter_OutWidth, initParams->displaySize.width);
    params->Set(NVSDK_NGX_Parameter_OutHeight, initParams->displaySize.height);

    auto ratio = (float) initParams->displaySize.width / (float) pExecParams->renderSize.width;

    if (ratio > 2.0)
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_UltraPerformance);
    else if (ratio <= 2.0 && ratio > 1.7)
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_MaxPerf);
    else if (ratio <= 1.7 && ratio > 1.5)
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_Balanced);
    else if (ratio <= 1.5 && ratio > 1.3)
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_MaxQuality);
    else if (ratio <= 1.3 && ratio > 1.0)
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_UltraQuality);
    else
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_DLAA);

    if (NVSDK_NGX_D3D11_CreateFeature(commandList, NVSDK_NGX_Feature_SuperSampling, params, &nvHandle) !=
        NVSDK_NGX_Result_Success)
        return false;

    _contexts[handle] = nvHandle;

    return true;
}

static std::optional<float> GetQualityOverrideRatioFfx(const FfxFsr2QualityMode input)
{
    LOG_DEBUG("");

    std::optional<float> output;

    auto sliderLimit = Config::Instance()->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;

    if (Config::Instance()->UpscaleRatioOverrideEnabled.value_or_default() &&
        Config::Instance()->UpscaleRatioOverrideValue.value_or_default() >= sliderLimit)
    {
        output = Config::Instance()->UpscaleRatioOverrideValue.value_or_default();

        return output;
    }

    if (!Config::Instance()->QualityRatioOverrideEnabled.value_or_default())
        return output; // override not enabled

    switch (input)
    {
    case FFX_FSR2_QUALITY_MODE_ULTRA_PERFORMANCE:
        if (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_UltraPerformance.value_or_default();

        break;

    case FFX_FSR2_QUALITY_MODE_PERFORMANCE:
        if (Config::Instance()->QualityRatio_Performance.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Performance.value_or_default();

        break;

    case FFX_FSR2_QUALITY_MODE_BALANCED:
        if (Config::Instance()->QualityRatio_Balanced.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Balanced.value_or_default();

        break;

    case FFX_FSR2_QUALITY_MODE_QUALITY:
        if (Config::Instance()->QualityRatio_Quality.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Quality.value_or_default();

        break;

    default:
        LOG_WARN("Unknown quality: {0}", (int) input);
        break;
    }

    return output;
}

// FSR2 Upscaler
static FfxErrorCode ffxFsr2ContextCreate_Dx11(FfxFsr2Context* context, FfxFsr2ContextDescription* contextDescription)
{
    LOG_DEBUG("");

    if (contextDescription == nullptr || contextDescription->device == nullptr)
        return FFX_ERROR_INVALID_ARGUMENT;

    auto& state = State::Instance();

    _skipCreate = true;

    FfxErrorCode ccResult = FFX_OK;
    {
        ScopedSkipHeapCapture skipHeapCapture {};

        ccResult = o_ffxFsr2ContextCreate_Dx11(context, contextDescription);
        _skipCreate = false;

        if (ccResult != FFX_OK)
        {
            LOG_ERROR("ccResult: {:X}", (UINT) ccResult);
            return ccResult;
        }
    }

    // Cached device was destroyed
    if (_d3d11Device != nullptr && !state.IsD3D11Device(_d3d11Device))
        _d3d11Device = nullptr;

    // check for d3d11 device
    // to prevent crashes when game is using custom interface and
    if (_d3d11Device == nullptr)
    {
        auto bDevice = (ID3D11Device*) contextDescription->device;

        if (state.IsD3D11Device(bDevice))
            _d3d11Device = bDevice;
    }

    // if still no device use latest created one
    // Might fixed TLOU but FMF2 still crashes
    if (_d3d11Device == nullptr)
        _d3d11Device = state.LastD3D11Device();

    if (_d3d11Device == nullptr)
    {
        LOG_WARN("D3D11 device not found!");
        return ccResult;
    }

    // Re-initialise NGX when the device this input initialised it with was replaced
    if (!state.nvngxDx11Inited || (_nvngxInited && _nvngxDevice != _d3d11Device))
    {
        NVSDK_NGX_FeatureCommonInfo fcInfo {};
        auto exePath = Util::ExePath().remove_filename();

        auto nvResult =
            NVNGXDx11InputInit(OPTI_GUID, state.NVNGX_Engine, OPTI_VERSION, exePath.c_str(), _d3d11Device, &fcInfo,
                               state.NVNGX_Version == 0 ? NVSDK_NGX_Version_API : state.NVNGX_Version);

        if (nvResult != NVSDK_NGX_Result_Success)
            return FFX_ERROR_BACKEND_API_ERROR;

        _nvngxDevice = _d3d11Device;
        _nvngxInited = true;
    }

    NVSDK_NGX_Parameter* params = nullptr;

    if (NVSDK_NGX_D3D11_GetCapabilityParameters(&params) != NVSDK_NGX_Result_Success)
        return FFX_ERROR_BACKEND_API_ERROR;

    _nvParams[context] = params;

    FfxFsr2ContextDescription ccd {};
    ccd.flags = contextDescription->flags;
    ccd.maxRenderSize = contextDescription->maxRenderSize;
    ccd.displaySize = contextDescription->displaySize;
    _initParams[context] = ccd;

    LOG_INFO("context created: {:X}", (size_t) context);

    return FFX_OK;
}

// FSR2.1
static FfxErrorCode ffxFsr2ContextDispatch_Dx11(FfxFsr2Context* context,
                                                const FfxFsr2DispatchDescription* dispatchDescription)
{
    LOG_DEBUG("");

    // Skip OptiScaler stuff
    if (!Config::Instance()->UseFsr2Inputs.value_or_default() || !_nvParams.contains(context))
    {
        _skipDispatch = true;
        LOG_DEBUG("UseFsr2Inputs not enabled, skipping");
        auto result = o_ffxFsr2ContextDispatch_Dx11(context, dispatchDescription);
        _skipDispatch = false;
        return result;
    }

    if (dispatchDescription == nullptr || context == nullptr || dispatchDescription->commandList == nullptr)
        return FFX_ERROR_INVALID_ARGUMENT;

    // If not in contexts list create and add context
    if (!_contexts.contains(context) &&
        (!_initParams.contains(context) || !CreateDLSSContext(context, dispatchDescription)))
        return FFX_ERROR_INVALID_ARGUMENT;

    NVSDK_NGX_Parameter* params = _nvParams[context];
    NVSDK_NGX_Handle* handle = _contexts[context];

    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, dispatchDescription->jitterOffset.x);
    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, dispatchDescription->jitterOffset.y);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_X, dispatchDescription->motionVectorScale.x);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, dispatchDescription->motionVectorScale.y);
    params->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0);
    params->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, dispatchDescription->preExposure);
    params->Set(NVSDK_NGX_Parameter_Reset, dispatchDescription->reset ? 1 : 0);
    params->Set(NVSDK_NGX_Parameter_Width, dispatchDescription->renderSize.width);
    params->Set(NVSDK_NGX_Parameter_Height, dispatchDescription->renderSize.height);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, dispatchDescription->renderSize.width);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, dispatchDescription->renderSize.height);
    params->Set(NVSDK_NGX_Parameter_Depth, dispatchDescription->depth.resource);
    params->Set(NVSDK_NGX_Parameter_ExposureTexture, dispatchDescription->exposure.resource);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, dispatchDescription->reactive.resource);
    params->Set(NVSDK_NGX_Parameter_Color, dispatchDescription->color.resource);
    params->Set(NVSDK_NGX_Parameter_MotionVectors, dispatchDescription->motionVectors.resource);
    params->Set(NVSDK_NGX_Parameter_Output, dispatchDescription->output.resource);
    params->Set("FSR.cameraNear", dispatchDescription->cameraNear);
    params->Set("FSR.cameraFar", dispatchDescription->cameraFar);
    params->Set("FSR.cameraFovAngleVertical", dispatchDescription->cameraFovAngleVertical);
    params->Set("FSR.frameTimeDelta", dispatchDescription->frameTimeDelta);
    params->Set("FSR.transparencyAndComposition", dispatchDescription->transparencyAndComposition.resource);
    params->Set("FSR.reactive", dispatchDescription->reactive.resource);
    params->Set(NVSDK_NGX_Parameter_Sharpness, dispatchDescription->sharpness);

    LOG_DEBUG("handle: {:X}, internalResolution: {}x{}", handle->Id, dispatchDescription->renderSize.width,
              dispatchDescription->renderSize.height);

    State::Instance().setInputApiName = ApiUpscalerInput::FSR2X_DX11;

    auto evalResult = NVSDK_NGX_D3D11_EvaluateFeature((ID3D11DeviceContext*) dispatchDescription->commandList, handle,
                                                      params, nullptr);

    if (evalResult == NVSDK_NGX_Result_Success)
        return FFX_OK;

    LOG_ERROR("evalResult: {:X}", (UINT) evalResult);
    return FFX_ERROR_BACKEND_API_ERROR;
}

static FfxErrorCode ffxFsr2ContextDestroy_Dx11(FfxFsr2Context* context)
{
    LOG_DEBUG("");

    if (context == nullptr)
        return FFX_ERROR_INVALID_ARGUMENT;

    if (_contexts.contains(context))
        NVSDK_NGX_D3D11_ReleaseFeature(_contexts[context]);

    _contexts.erase(context);
    if (auto it = _nvParams.find(context); it != _nvParams.end())
    {
        TryDestroyNGXParameters(it->second, NVNGXProxy::D3D11_DestroyParameters());
        _nvParams.erase(it);
    }
    _initParams.erase(context);

    _skipDestroy = true;
    auto cdResult = o_ffxFsr2ContextDestroy_Dx11(context);
    _skipDestroy = false;

    LOG_INFO("result: {:X}", (UINT) cdResult);

    return FFX_OK;
}

static int32_t ffxFsr2GetJitterPhaseCount_Dx11(int32_t renderWidth, int32_t displayWidth)
{
    LOG_DEBUG("renderWidth: {}, displayWidth: {}", renderWidth, displayWidth);

    if (State::Instance().currentFeature)
    {
        displayWidth = State::Instance().currentFeature->TargetWidth();
        renderWidth = State::Instance().currentFeature->RenderWidth();
    }

    float ratio = (float) displayWidth / (float) renderWidth;
    auto result = static_cast<int32_t>(ceil(ratio * ratio * 8.0f)); // ceil(8*n^2)
    LOG_DEBUG("Render resolution: {}, Display resolution: {}, Ratio: {}, Jitter phase count: {}", renderWidth,
              displayWidth, ratio, result);

    return result;
}

static float ffxFsr2GetUpscaleRatioFromQualityMode_Dx11(FfxFsr2QualityMode qualityMode)
{
    LOG_DEBUG("");

    auto ratio = GetQualityOverrideRatioFfx(qualityMode).value_or(qualityRatios[(UINT) qualityMode]);
    LOG_DEBUG("Quality mode: {}, Upscale ratio: {}", (UINT) qualityMode, ratio);
    return ratio;
}

static FfxErrorCode ffxFsr2GetRenderResolutionFromQualityMode_Dx11(uint32_t* renderWidth, uint32_t* renderHeight,
                                                                   uint32_t displayWidth, uint32_t displayHeight,
                                                                   FfxFsr2QualityMode qualityMode)
{
    LOG_DEBUG("");

    auto ratio = GetQualityOverrideRatioFfx(qualityMode).value_or(qualityRatios[(UINT) qualityMode]);

    if (renderHeight != nullptr)
        *renderHeight = (uint32_t) ((float) displayHeight / ratio);

    if (renderWidth != nullptr)
        *renderWidth = (uint32_t) ((float) displayWidth / ratio);

    if (renderWidth != nullptr && renderHeight != nullptr)
    {
        LOG_DEBUG("Quality mode: {}, Render resolution: {}x{}", (UINT) qualityMode, *renderWidth, *renderHeight);
        return FFX_OK;
    }

    LOG_WARN("Quality mode: {}, pOutRenderWidth or pOutRenderHeight is null!", (UINT) qualityMode);
    return FFX_ERROR_INVALID_ARGUMENT;
}

void FSR2Dx11DeviceDestroyed(IUnknown* device)
{
    ID3D11Device* expected = (ID3D11Device*) device;
    _nvngxDevice.compare_exchange_strong(expected, nullptr);
}

void HookFSR2Dx11ExeInputs()
{
    LOG_INFO("Trying to hook FSR2 Dx11 methods");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // ffxFsr2ContextCreate
    if (o_ffxFsr2ContextCreate_Dx11 == nullptr)
    {
        o_ffxFsr2ContextCreate_Dx11 =
            (PFN_ffxFsr2ContextCreate) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxFsr2ContextCreate");

        if (o_ffxFsr2ContextCreate_Dx11 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2ContextCreate_Dx11, ffxFsr2ContextCreate_Dx11);

        LOG_DEBUG("ffxFsr2ContextCreate_Dx11: {:X}", (size_t) o_ffxFsr2ContextCreate_Dx11);
    }

    // ffxFsr2ContextDispatch 2.X
    if (o_ffxFsr2ContextDispatch_Dx11 == nullptr)
    {
        o_ffxFsr2ContextDispatch_Dx11 =
            (PFN_ffxFsr2ContextDispatch) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxFsr2ContextDispatch");

        if (o_ffxFsr2ContextDispatch_Dx11 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2ContextDispatch_Dx11, ffxFsr2ContextDispatch_Dx11);

        LOG_DEBUG("ffxFsr2ContextDispatch_Dx11: {:X}", (size_t) o_ffxFsr2ContextDispatch_Dx11);
    }

    // ffxFsr2ContextDestroy
    if (o_ffxFsr2ContextDestroy_Dx11 == nullptr)
    {
        o_ffxFsr2ContextDestroy_Dx11 =
            (PFN_ffxFsr2ContextDestroy) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxFsr2ContextDestroy");

        if (o_ffxFsr2ContextDestroy_Dx11 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2ContextDestroy_Dx11, ffxFsr2ContextDestroy_Dx11);

        LOG_DEBUG("ffxFsr2ContextDestroy_Dx11: {:X}", (size_t) o_ffxFsr2ContextDestroy_Dx11);
    }

    // ffxFsr2GetUpscaleRatioFromQualityMode
    if (o_ffxFsr2GetUpscaleRatioFromQualityMode_Dx11 == nullptr)
    {
        o_ffxFsr2GetUpscaleRatioFromQualityMode_Dx11 =
            (PFN_ffxFsr2GetUpscaleRatioFromQualityMode) KernelBaseProxy::GetProcAddress_()(
                exeModule, "ffxFsr2GetUpscaleRatioFromQualityMode");

        if (o_ffxFsr2GetUpscaleRatioFromQualityMode_Dx11 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2GetUpscaleRatioFromQualityMode_Dx11,
                         ffxFsr2GetUpscaleRatioFromQualityMode_Dx11);

        LOG_DEBUG("ffxFsr2GetUpscaleRatioFromQualityMode_Dx11: {:X}",
                  (size_t) o_ffxFsr2GetUpscaleRatioFromQualityMode_Dx11);
    }

    // ffxFsr2GetRenderResolutionFromQualityMode
    if (o_ffxFsr2GetRenderResolutionFromQualityMode_Dx11 == nullptr)
    {
        o_ffxFsr2GetRenderResolutionFromQualityMode_Dx11 =
            (PFN_ffxFsr2GetRenderResolutionFromQualityMode) KernelBaseProxy::GetProcAddress_()(
                exeModule, "ffxFsr2GetRenderResolutionFromQualityMode");

        if (o_ffxFsr2GetRenderResolutionFromQualityMode_Dx11 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2GetRenderResolutionFromQualityMode_Dx11,
                         ffxFsr2GetRenderResolutionFromQualityMode_Dx11);

        LOG_DEBUG("ffxFsr2GetRenderResolutionFromQualityMode_Dx11: {:X}",
                  (size_t) o_ffxFsr2GetRenderResolutionFromQualityMode_Dx11);
    }

    // ffxFsr2GetJitterPhaseCount
    if (o_ffxFsr2GetJitterPhaseCount_Dx11 == nullptr)
    {
        o_ffxFsr2GetJitterPhaseCount_Dx11 = (PFN_ffxFsr2GetJitterPhaseCount) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxFsr2GetJitterPhaseCount");

        if (o_ffxFsr2GetJitterPhaseCount_Dx11 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2GetJitterPhaseCount_Dx11, ffxFsr2GetJitterPhaseCount_Dx11);

        LOG_DEBUG("o_ffxFsr2GetJitterPhaseCount_Dx11: {:X}", (size_t) o_ffxFsr2GetJitterPhaseCount_Dx11);
    }

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to hook FSR2 Dx11 methods, error code: {:X}", detourResult);
        o_ffxFsr2ContextCreate_Dx11 = nullptr;
        o_ffxFsr2ContextDispatch_Dx11 = nullptr;
        o_ffxFsr2ContextDestroy_Dx11 = nullptr;
        o_ffxFsr2GetUpscaleRatioFromQualityMode_Dx11 = nullptr;
        o_ffxFsr2GetRenderResolutionFromQualityMode_Dx11 = nullptr;
        o_ffxFsr2GetJitterPhaseCount_Dx11 = nullptr;
    }
    else
    {
        State::Instance().fsrHooks = o_ffxFsr2ContextCreate_Dx11 != nullptr;
    }
}
