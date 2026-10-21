#include "pch.h"
#include "FSR3_Dx12.h"

#include "Config.h"
#include "Util.h"

#include "resource.h"
#include "NVNGX_Parameter.h"

#include <proxies/KernelBase_Proxy.h>

#include <scanner/scanner.h>

#include "detours/detours.h"
#include "fsr3/ffx_fsr3upscaler.h"
#include "fsr3/dx12/ffx_dx12.h"

// FSR3
typedef Fsr3::FfxErrorCode (*PFN_ffxFsr3UpscalerContextCreate)(
    Fsr3::FfxFsr3UpscalerContext* pContext, const Fsr3::FfxFsr3UpscalerContextDescription* pContextDescription);
typedef Fsr3::FfxErrorCode (*PFN_ffxFsr3UpscalerContextDispatch)(
    Fsr3::FfxFsr3UpscalerContext* pContext, const Fsr3::FfxFsr3UpscalerDispatchDescription* pDispatchDescription);
typedef Fsr3::FfxErrorCode (*PFN_ffxFsr3UpscalerContextDestroy)(Fsr3::FfxFsr3UpscalerContext* pContext);
typedef float (*PFN_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode)(Fsr3::FfxFsr3UpscalerQualityMode qualityMode);
typedef Fsr3::FfxErrorCode (*PFN_ffxFsr3UpscalerGetRenderResolutionFromQualityMode)(
    uint32_t* pRenderWidth, uint32_t* pRenderHeight, uint32_t displayWidth, uint32_t displayHeight,
    Fsr3::FfxFsr3UpscalerQualityMode qualityMode);

// DX12
typedef Fsr3::FfxErrorCode (*PFN_ffxFSR3GetInterfaceDX12)(Fsr3::FfxInterface* backendInterface, Fsr3::FfxDevice device,
                                                          void* scratchBuffer, size_t scratchBufferSize,
                                                          uint32_t maxContexts);

static PFN_ffxFsr3UpscalerContextCreate o_ffxFsr3UpscalerContextCreate_Dx12 = nullptr;
static PFN_ffxFsr3UpscalerContextDispatch o_ffxFsr3UpscalerContextDispatch_Dx12 = nullptr;
static PFN_ffxFsr3UpscalerContextDestroy o_ffxFsr3UpscalerContextDestroy_Dx12 = nullptr;
static PFN_ffxFsr3UpscalerContextCreate o_ffxFsr3UpscalerContextCreate_Pattern_Dx12 = nullptr;
static PFN_ffxFsr3UpscalerContextDispatch o_ffxFsr3UpscalerContextDispatch_Pattern_Dx12 = nullptr;
static PFN_ffxFsr3UpscalerContextDestroy o_ffxFsr3UpscalerContextDestroy_Pattern_Dx12 = nullptr;
static PFN_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12 = nullptr;
static PFN_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Pattern_Dx12 =
    nullptr;
static PFN_ffxFsr3UpscalerGetRenderResolutionFromQualityMode o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12 =
    nullptr;
static PFN_ffxFSR3GetInterfaceDX12 o_ffxFSR3GetInterfaceDX12 = nullptr;

static std::unordered_map<Fsr3::FfxFsr3UpscalerContext*, Fsr3::FfxFsr3UpscalerContextDescription> _initParams;
static std::unordered_map<Fsr3::FfxFsr3UpscalerContext*, NVSDK_NGX_Parameter*> _nvParams;
static std::unordered_map<Fsr3::FfxFsr3UpscalerContext*, NVSDK_NGX_Handle*> _contexts;
static ID3D12Device* _d3d12Device = nullptr;
static bool _nvnxgInited = false;
static bool _skipCreate = false;
static bool _skipDispatch = false;
static bool _skipDestroy = false;
static float qualityRatios[] = { 1.0f, 1.5f, 1.7f, 2.0f, 3.0f };

static D3D12_RESOURCE_STATES GetD3D12State(Fsr3::FfxResourceStates state)
{
    switch (state)
    {
    case Fsr3::FFX_RESOURCE_STATE_COMMON:
        return D3D12_RESOURCE_STATE_COMMON;
    case Fsr3::FFX_RESOURCE_STATE_UNORDERED_ACCESS:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case Fsr3::FFX_RESOURCE_STATE_COMPUTE_READ:
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case Fsr3::FFX_RESOURCE_STATE_PIXEL_READ:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case Fsr3::FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ:
        return (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    case Fsr3::FFX_RESOURCE_STATE_COPY_SRC:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case Fsr3::FFX_RESOURCE_STATE_COPY_DEST:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case Fsr3::FFX_RESOURCE_STATE_GENERIC_READ:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    case Fsr3::FFX_RESOURCE_STATE_INDIRECT_ARGUMENT:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case Fsr3::FFX_RESOURCE_STATE_RENDER_TARGET:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    default:
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

static bool CreateDLSSContext(Fsr3::FfxFsr3UpscalerContext* handle,
                              const Fsr3::FfxFsr3UpscalerDispatchDescription* pExecParams)
{
    LOG_DEBUG("");

    if (!_nvParams.contains(handle))
        return false;

    NVSDK_NGX_Handle* nvHandle = nullptr;
    auto params = _nvParams[handle];
    auto initParams = &_initParams[handle];
    auto commandList = (ID3D12GraphicsCommandList*) pExecParams->commandList;

    UINT initFlags = 0;

    if (initParams->flags & Fsr3::FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;

    if (initParams->flags & Fsr3::FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;

    if (initParams->flags & Fsr3::FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

    if (initParams->flags & Fsr3::FFX_FSR3UPSCALER_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;

    if ((initParams->flags & Fsr3::FFX_FSR3UPSCALER_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) == 0)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

    State::Instance().dynamicResolution = (initParams->flags & Fsr3::FFX_FSR3UPSCALER_ENABLE_DYNAMIC_RESOLUTION) != 0;

    params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, initFlags);

    params->Set(NVSDK_NGX_Parameter_Width, pExecParams->renderSize.width);
    params->Set(NVSDK_NGX_Parameter_Height, pExecParams->renderSize.height);
    params->Set(NVSDK_NGX_Parameter_OutWidth, initParams->displaySize.width);
    params->Set(NVSDK_NGX_Parameter_OutHeight, initParams->displaySize.height);

    auto ratio = (float) initParams->displaySize.width / (float) pExecParams->renderSize.width;

    if (ratio <= 3.0 && ratio > 2.0)
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

    if (NVSDK_NGX_D3D12_CreateFeature(commandList, NVSDK_NGX_Feature_SuperSampling, params, &nvHandle) !=
        NVSDK_NGX_Result_Success)
        return false;

    _contexts[handle] = nvHandle;

    return true;
}

static std::optional<float> GetQualityOverrideRatioFfx(const Fsr3::FfxFsr3UpscalerQualityMode input)
{
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
    case Fsr3::FFX_FSR3UPSCALER_QUALITY_MODE_ULTRA_PERFORMANCE:
        if (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_UltraPerformance.value_or_default();

        break;

    case Fsr3::FFX_FSR3UPSCALER_QUALITY_MODE_PERFORMANCE:
        if (Config::Instance()->QualityRatio_Performance.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Performance.value_or_default();

        break;

    case Fsr3::FFX_FSR3UPSCALER_QUALITY_MODE_BALANCED:
        if (Config::Instance()->QualityRatio_Balanced.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Balanced.value_or_default();

        break;

    case Fsr3::FFX_FSR3UPSCALER_QUALITY_MODE_QUALITY:
        if (Config::Instance()->QualityRatio_Quality.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Quality.value_or_default();

        break;

    case Fsr3::FFX_FSR3UPSCALER_QUALITY_MODE_NATIVEAA:
        if (Config::Instance()->QualityRatio_Quality.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Quality.value_or_default();

        break;

    default:
        LOG_WARN("Unknown quality: {0}", (int) input);
        break;
    }

    return output;
}

struct dummyDevice
{
    uint32_t dummy0[5];
    size_t dummy1[32];
};

// FSR3 Upscaler
static Fsr3::FfxErrorCode ffxFsr3ContextCreate_Dx12(Fsr3::FfxFsr3UpscalerContext* pContext,
                                                    Fsr3::FfxFsr3UpscalerContextDescription* pContextDescription)
{
    if (pContext == nullptr || pContextDescription->backendInterface.device == nullptr)
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    auto& state = State::Instance();
    Fsr3::FfxErrorCode ccResult = Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    {
        ScopedSkipHeapCapture skipHeapCapture {};

        _skipCreate = true;
        ccResult = o_ffxFsr3UpscalerContextCreate_Dx12(pContext, pContextDescription);
        _skipCreate = false;

        if (ccResult != Fsr3::FFX_OK)
        {
            LOG_ERROR("create error: {}", (UINT) ccResult);
            return ccResult;
        }
    }

    // check for d3d12 device
    // to prevent crashes when game is using custom interface and
    if (_d3d12Device == nullptr)
    {
        auto bDevice = (ID3D12Device*) pContextDescription->backendInterface.device;

        for (size_t i = 0; i < state.d3d12Devices.size(); i++)
        {
            if (state.d3d12Devices[i] == bDevice)
            {
                _d3d12Device = bDevice;
                break;
            }
        }
    }

    // if still no device use latest created one
    // Might fixed TLOU but FMF2 still crashes
    if (_d3d12Device == nullptr && state.d3d12Devices.size() > 0)
        _d3d12Device = state.d3d12Devices[state.d3d12Devices.size() - 1];

    if (_d3d12Device == nullptr)
        _d3d12Device = state.currentD3D12Device;

    if (_d3d12Device == nullptr)
    {
        LOG_ERROR("D3D12 device not found!");
        return ccResult;
    }

    if (!state.nvngxDx12Inited)
    {
        NVSDK_NGX_FeatureCommonInfo fcInfo {};
        auto exePath = Util::ExePath().remove_filename();

        auto nvResult = NVSDK_NGX_D3D12_Init_with_ProjectID(
            OPTI_GUID, state.NVNGX_Engine, OPTI_VERSION, exePath.c_str(), _d3d12Device, &fcInfo,
            state.NVNGX_Version == 0 ? NVSDK_NGX_Version_API : state.NVNGX_Version);

        if (nvResult != NVSDK_NGX_Result_Success)
            return Fsr3::FFX_ERROR_BACKEND_API_ERROR;

        _nvnxgInited = true;
    }

    NVSDK_NGX_Parameter* params = nullptr;

    if (NVSDK_NGX_D3D12_GetCapabilityParameters(&params) != NVSDK_NGX_Result_Success)
        return Fsr3::FFX_ERROR_BACKEND_API_ERROR;

    _nvParams[pContext] = params;

    Fsr3::FfxFsr3UpscalerContextDescription ccd {};
    ccd.flags = pContextDescription->flags;
    ccd.maxRenderSize = pContextDescription->maxRenderSize;
    ccd.displaySize = pContextDescription->displaySize;
    ccd.backendInterface.device = pContextDescription->backendInterface.device;
    _initParams[pContext] = ccd;

    LOG_INFO("context created: {:X}", (size_t) pContext);

    return Fsr3::FFX_OK;
}

static Fsr3::FfxErrorCode ffxFsr3ContextDispatch_Dx12(Fsr3::FfxFsr3UpscalerContext* pContext,
                                                      Fsr3::FfxFsr3UpscalerDispatchDescription* pDispatchDescription)
{
    // Skip OptiScaler stuff
    if (!Config::Instance()->UseFsr3Inputs.value_or_default())
    {
        _skipDispatch = true;
        auto result = o_ffxFsr3UpscalerContextDispatch_Dx12(pContext, pDispatchDescription);
        _skipDispatch = false;
        return result;
    }

    if (_d3d12Device == nullptr)
    {
        _skipDispatch = true;
        auto result = o_ffxFsr3UpscalerContextDispatch_Dx12(pContext, pDispatchDescription);
        _skipDispatch = false;
        return result;
    }

    if (pDispatchDescription == nullptr || pContext == nullptr || pDispatchDescription->commandList == nullptr)
        return Fsr3::FFX_ERROR_BACKEND_API_ERROR;

    // If not in contexts list create and add context
    if (!_contexts.contains(pContext) && _initParams.contains(pContext) &&
        !CreateDLSSContext(pContext, pDispatchDescription))
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    NVSDK_NGX_Parameter* params = _nvParams[pContext];
    NVSDK_NGX_Handle* handle = _contexts[pContext];

    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, pDispatchDescription->jitterOffset.x);
    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, pDispatchDescription->jitterOffset.y);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_X, pDispatchDescription->motionVectorScale.x);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, pDispatchDescription->motionVectorScale.y);
    params->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0);
    params->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, pDispatchDescription->preExposure);
    params->Set(NVSDK_NGX_Parameter_Reset, pDispatchDescription->reset ? 1 : 0);
    params->Set(NVSDK_NGX_Parameter_Width, pDispatchDescription->renderSize.width);
    params->Set(NVSDK_NGX_Parameter_Height, pDispatchDescription->renderSize.height);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, pDispatchDescription->renderSize.width);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, pDispatchDescription->renderSize.height);
    params->Set(NVSDK_NGX_Parameter_Depth, pDispatchDescription->depth.resource);
    params->Set(NVSDK_NGX_Parameter_ExposureTexture, pDispatchDescription->exposure.resource);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, pDispatchDescription->reactive.resource);
    params->Set(NVSDK_NGX_Parameter_Color, pDispatchDescription->color.resource);
    params->Set(NVSDK_NGX_Parameter_MotionVectors, pDispatchDescription->motionVectors.resource);
    params->Set(NVSDK_NGX_Parameter_Output, pDispatchDescription->output.resource);
    params->Set("FSR.cameraNear", pDispatchDescription->cameraNear);
    params->Set("FSR.cameraFar", pDispatchDescription->cameraFar);
    params->Set("FSR.cameraFovAngleVertical", pDispatchDescription->cameraFovAngleVertical);
    params->Set("FSR.frameTimeDelta", pDispatchDescription->frameTimeDelta);
    params->Set("FSR.transparencyAndComposition", pDispatchDescription->transparencyAndComposition.resource);
    params->Set("FSR.reactive", pDispatchDescription->reactive.resource);
    params->Set("FSR.viewSpaceToMetersFactor", pDispatchDescription->viewSpaceToMetersFactor);
    params->Set(NVSDK_NGX_Parameter_Sharpness, pDispatchDescription->sharpness);

    if (pDispatchDescription->color.resource != nullptr && pDispatchDescription->color.state > 0)
        Config::Instance()->ColorResourceBarrier.set_volatile_value(GetD3D12State(pDispatchDescription->color.state));

    if (pDispatchDescription->depth.resource != nullptr && pDispatchDescription->depth.state > 0)
        Config::Instance()->DepthResourceBarrier.set_volatile_value(GetD3D12State(pDispatchDescription->depth.state));

    if (pDispatchDescription->exposure.resource != nullptr && pDispatchDescription->exposure.state > 0)
        Config::Instance()->ExposureResourceBarrier.set_volatile_value(
            GetD3D12State(pDispatchDescription->exposure.state));

    if (pDispatchDescription->motionVectors.resource != nullptr && pDispatchDescription->motionVectors.state > 0)
        Config::Instance()->MVResourceBarrier.set_volatile_value(
            GetD3D12State(pDispatchDescription->motionVectors.state));

    if (pDispatchDescription->output.resource != nullptr && pDispatchDescription->output.state > 0)
        Config::Instance()->OutputResourceBarrier.set_volatile_value(GetD3D12State(pDispatchDescription->output.state));

    if (pDispatchDescription->reactive.resource != nullptr && pDispatchDescription->reactive.state > 0)
        Config::Instance()->MaskResourceBarrier.set_volatile_value(GetD3D12State(pDispatchDescription->reactive.state));

    LOG_DEBUG("handle: {:X}, internalResolution: {}x{}", handle->Id, pDispatchDescription->renderSize.width,
              pDispatchDescription->renderSize.height);

    State::Instance().setInputApiName = ApiUpscalerInput::FSR3_DX12;

    auto evalResult = NVSDK_NGX_D3D12_EvaluateFeature((ID3D12GraphicsCommandList*) pDispatchDescription->commandList,
                                                      handle, params, nullptr);

    if (evalResult == NVSDK_NGX_Result_Success)
        return Fsr3::FFX_OK;

    LOG_ERROR("evalResult: {:X}", (UINT) evalResult);
    return Fsr3::FFX_ERROR_BACKEND_API_ERROR;
}

static Fsr3::FfxErrorCode ffxFsr3ContextDestroy_Dx12(Fsr3::FfxFsr3UpscalerContext* pContext)
{
    if (pContext == nullptr)
        return Fsr3::FFX_ERROR_BACKEND_API_ERROR;

    LOG_DEBUG("context: {:X}", (size_t) pContext);

    if (_contexts.contains(pContext))
        NVSDK_NGX_D3D12_ReleaseFeature(_contexts[pContext]);

    _contexts.erase(pContext);
    _nvParams.erase(pContext);
    _initParams.erase(pContext);

    _skipDestroy = true;
    auto cdResult = o_ffxFsr3UpscalerContextDestroy_Dx12(pContext);
    _skipDestroy = false;

    LOG_INFO("result: {:X}", (UINT) cdResult);

    return Fsr3::FFX_OK;
}

static Fsr3::FfxErrorCode
ffxFsr3ContextCreate_Pattern_Dx12(Fsr3::FfxFsr3UpscalerContext* pContext,
                                  Fsr3::FfxFsr3UpscalerContextDescription* pContextDescription)
{
    if (pContext == nullptr || pContextDescription->backendInterface.device == nullptr)
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    auto& state = State::Instance();

    Fsr3::FfxErrorCode ccResult = Fsr3::FFX_OK;
    {
        ScopedSkipHeapCapture skipHeapCapture {};

        ccResult = o_ffxFsr3UpscalerContextCreate_Pattern_Dx12(pContext, pContextDescription);

        if (_skipCreate)
            return ccResult;

        if (ccResult != Fsr3::FFX_OK)
        {
            LOG_ERROR("create error: {}", (UINT) ccResult);
            return ccResult;
        }
    }

    // check for d3d12 device
    // to prevent crashes when game is using custom interface and
    if (_d3d12Device == nullptr)
    {
        auto bDevice = (ID3D12Device*) pContextDescription->backendInterface.device;

        for (size_t i = 0; i < state.d3d12Devices.size(); i++)
        {
            if (state.d3d12Devices[i] == bDevice)
            {
                _d3d12Device = bDevice;
                break;
            }
        }
    }

    // if still no device use latest created one
    // Might fixed TLOU but FMF2 still crashes
    if (_d3d12Device == nullptr && state.d3d12Devices.size() > 0)
        _d3d12Device = state.d3d12Devices[state.d3d12Devices.size() - 1];

    if (_d3d12Device == nullptr)
        _d3d12Device = state.currentD3D12Device;

    if (_d3d12Device == nullptr)
    {
        LOG_ERROR("D3D12 device not found!");
        return ccResult;
    }

    if (!state.nvngxDx12Inited)
    {
        NVSDK_NGX_FeatureCommonInfo fcInfo {};

        auto exePath = Util::ExePath().remove_filename();

        auto nvResult = NVSDK_NGX_D3D12_Init_with_ProjectID(
            OPTI_GUID, state.NVNGX_Engine, OPTI_VERSION, exePath.c_str(), _d3d12Device, &fcInfo,
            state.NVNGX_Version == 0 ? NVSDK_NGX_Version_API : state.NVNGX_Version);

        if (nvResult != NVSDK_NGX_Result_Success)
            return Fsr3::FFX_ERROR_BACKEND_API_ERROR;

        _nvnxgInited = true;
    }

    NVSDK_NGX_Parameter* params = nullptr;

    if (NVSDK_NGX_D3D12_GetCapabilityParameters(&params) != NVSDK_NGX_Result_Success)
        return Fsr3::FFX_ERROR_BACKEND_API_ERROR;

    _nvParams[pContext] = params;

    Fsr3::FfxFsr3UpscalerContextDescription ccd {};
    ccd.flags = pContextDescription->flags;
    ccd.maxRenderSize = pContextDescription->maxRenderSize;
    ccd.displaySize = pContextDescription->displaySize;
    ccd.backendInterface.device = pContextDescription->backendInterface.device;
    _initParams[pContext] = ccd;

    LOG_INFO("context created: {:X}", (size_t) pContext);

    return Fsr3::FFX_OK;
}

static Fsr3::FfxErrorCode
ffxFsr3ContextDispatch_Pattern_Dx12(Fsr3::FfxFsr3UpscalerContext* pContext,
                                    Fsr3::FfxFsr3UpscalerDispatchDescription* pDispatchDescription)
{
    // Skip OptiScaler stuff
    if (!Config::Instance()->UseFsr3Inputs.value_or_default() || _skipDispatch)
        return o_ffxFsr3UpscalerContextDispatch_Dx12(pContext, pDispatchDescription);

    if (_d3d12Device == nullptr)
        return o_ffxFsr3UpscalerContextDispatch_Dx12(pContext, pDispatchDescription);

    if (pDispatchDescription == nullptr || pContext == nullptr || pDispatchDescription->commandList == nullptr)
        return Fsr3::FFX_ERROR_BACKEND_API_ERROR;

    // If not in contexts list create and add context
    if (!_contexts.contains(pContext) && _initParams.contains(pContext) &&
        !CreateDLSSContext(pContext, pDispatchDescription))
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    NVSDK_NGX_Parameter* params = _nvParams[pContext];
    NVSDK_NGX_Handle* handle = _contexts[pContext];

    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, pDispatchDescription->jitterOffset.x);
    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, pDispatchDescription->jitterOffset.y);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_X, pDispatchDescription->motionVectorScale.x);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, pDispatchDescription->motionVectorScale.y);
    params->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0);
    params->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, pDispatchDescription->preExposure);
    params->Set(NVSDK_NGX_Parameter_Reset, pDispatchDescription->reset ? 1 : 0);
    params->Set(NVSDK_NGX_Parameter_Width, pDispatchDescription->renderSize.width);
    params->Set(NVSDK_NGX_Parameter_Height, pDispatchDescription->renderSize.height);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, pDispatchDescription->renderSize.width);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, pDispatchDescription->renderSize.height);
    params->Set(NVSDK_NGX_Parameter_Depth, pDispatchDescription->depth.resource);
    params->Set(NVSDK_NGX_Parameter_ExposureTexture, pDispatchDescription->exposure.resource);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, pDispatchDescription->reactive.resource);
    params->Set(NVSDK_NGX_Parameter_Color, pDispatchDescription->color.resource);
    params->Set(NVSDK_NGX_Parameter_MotionVectors, pDispatchDescription->motionVectors.resource);
    params->Set(NVSDK_NGX_Parameter_Output, pDispatchDescription->output.resource);
    params->Set("FSR.cameraNear", pDispatchDescription->cameraNear);
    params->Set("FSR.cameraFar", pDispatchDescription->cameraFar);
    params->Set("FSR.cameraFovAngleVertical", pDispatchDescription->cameraFovAngleVertical);
    params->Set("FSR.frameTimeDelta", pDispatchDescription->frameTimeDelta);
    params->Set("FSR.transparencyAndComposition", pDispatchDescription->transparencyAndComposition.resource);
    params->Set("FSR.reactive", pDispatchDescription->reactive.resource);
    params->Set("FSR.viewSpaceToMetersFactor", pDispatchDescription->viewSpaceToMetersFactor);
    params->Set(NVSDK_NGX_Parameter_Sharpness, pDispatchDescription->sharpness);

    if (pDispatchDescription->color.resource != nullptr && pDispatchDescription->color.state > 0)
        Config::Instance()->ColorResourceBarrier.set_volatile_value(GetD3D12State(pDispatchDescription->color.state));

    if (pDispatchDescription->depth.resource != nullptr && pDispatchDescription->depth.state > 0)
        Config::Instance()->DepthResourceBarrier.set_volatile_value(GetD3D12State(pDispatchDescription->depth.state));

    if (pDispatchDescription->exposure.resource != nullptr && pDispatchDescription->exposure.state > 0)
        Config::Instance()->ExposureResourceBarrier.set_volatile_value(
            GetD3D12State(pDispatchDescription->exposure.state));

    if (pDispatchDescription->motionVectors.resource != nullptr && pDispatchDescription->motionVectors.state > 0)
        Config::Instance()->MVResourceBarrier.set_volatile_value(
            GetD3D12State(pDispatchDescription->motionVectors.state));

    if (pDispatchDescription->output.resource != nullptr && pDispatchDescription->output.state > 0)
        Config::Instance()->OutputResourceBarrier.set_volatile_value(GetD3D12State(pDispatchDescription->output.state));

    if (pDispatchDescription->reactive.resource != nullptr && pDispatchDescription->reactive.state > 0)
        Config::Instance()->MaskResourceBarrier.set_volatile_value(GetD3D12State(pDispatchDescription->reactive.state));

    LOG_DEBUG("handle: {:X}, internalResolution: {}x{}", handle->Id, pDispatchDescription->renderSize.width,
              pDispatchDescription->renderSize.height);

    State::Instance().setInputApiName = ApiUpscalerInput::FSR3_DX12;

    auto evalResult = NVSDK_NGX_D3D12_EvaluateFeature((ID3D12GraphicsCommandList*) pDispatchDescription->commandList,
                                                      handle, params, nullptr);

    if (evalResult == NVSDK_NGX_Result_Success)
        return Fsr3::FFX_OK;

    LOG_ERROR("evalResult: {:X}", (UINT) evalResult);
    return Fsr3::FFX_ERROR_BACKEND_API_ERROR;
}

static Fsr3::FfxErrorCode ffxFsr3ContextDestroy_Pattern_Dx12(Fsr3::FfxFsr3UpscalerContext* pContext)
{
    if (pContext == nullptr)
        return Fsr3::FFX_ERROR_BACKEND_API_ERROR;

    LOG_DEBUG("context: {:X}", (size_t) pContext);

    if (_contexts.contains(pContext))
        NVSDK_NGX_D3D12_ReleaseFeature(_contexts[pContext]);

    _contexts.erase(pContext);
    _nvParams.erase(pContext);
    _initParams.erase(pContext);

    auto cdResult = o_ffxFsr3UpscalerContextDestroy_Dx12(pContext);
    LOG_INFO("result: {:X}", (UINT) cdResult);

    return Fsr3::FFX_OK;
}

static float ffxFsr3GetUpscaleRatioFromQualityMode_Dx12(Fsr3::FfxFsr3UpscalerQualityMode qualityMode)
{
    auto ratio = GetQualityOverrideRatioFfx(qualityMode).value_or(qualityRatios[(UINT) qualityMode]);
    LOG_DEBUG("Quality mode: {}, Upscale ratio: {}", (UINT) qualityMode, ratio);
    return ratio;
}

static float ffxFsr3GetUpscaleRatioFromQualityMode_Pattern_Dx12(Fsr3::FfxFsr3UpscalerQualityMode qualityMode)
{
    auto ratio = GetQualityOverrideRatioFfx(qualityMode).value_or(qualityRatios[(UINT) qualityMode]);
    LOG_DEBUG("Quality mode: {}, Upscale ratio: {}", (UINT) qualityMode, ratio);
    return ratio;
}

static Fsr3::FfxErrorCode ffxFsr3GetRenderResolutionFromQualityMode_Dx12(uint32_t* pRenderWidth,
                                                                         uint32_t* pRenderHeight, uint32_t displayWidth,
                                                                         uint32_t displayHeight,
                                                                         Fsr3::FfxFsr3UpscalerQualityMode qualityMode)
{
    auto ratio = GetQualityOverrideRatioFfx(qualityMode).value_or(qualityRatios[(UINT) qualityMode]);

    if (pRenderWidth != nullptr)
        *pRenderWidth = (uint32_t) ((float) displayWidth / ratio);

    if (pRenderHeight != nullptr)
        *pRenderHeight = (uint32_t) ((float) displayHeight / ratio);

    if (pRenderWidth != nullptr && pRenderHeight != nullptr)
    {
        LOG_DEBUG("Quality mode: {}, Render resolution: {}x{}", (UINT) qualityMode, *pRenderWidth, *pRenderHeight);
        return Fsr3::FFX_OK;
    }

    LOG_WARN("Quality mode: {}, pOutRenderWidth or pOutRenderHeight is null!", (UINT) qualityMode);
    return Fsr3::FFX_ERROR_INVALID_ARGUMENT;
}

static Fsr3::FfxErrorCode hk_ffxFsr3GetInterfaceDX12(Fsr3::FfxInterface* backendInterface, Fsr3::FfxDevice device,
                                                     void* scratchBuffer, size_t scratchBufferSize,
                                                     uint32_t maxContexts)
{
    LOG_DEBUG("");
    _d3d12Device = (ID3D12Device*) device;
    auto result = o_ffxFSR3GetInterfaceDX12(backendInterface, device, scratchBuffer, scratchBufferSize, maxContexts);
    return result;
}

void HookFSR3ExeInputs()
{
    LOG_INFO("Trying to hook FSR3 methods");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_ffxFsr3UpscalerContextCreate_Dx12 == nullptr)
    {
        o_ffxFsr3UpscalerContextCreate_Dx12 = (PFN_ffxFsr3UpscalerContextCreate) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxFsr3UpscalerContextCreate");

        if (o_ffxFsr3UpscalerContextCreate_Dx12 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr3UpscalerContextCreate_Dx12, ffxFsr3ContextCreate_Dx12);

        LOG_DEBUG("ffxFsr3UpscalerContextCreate_Dx12: {:X}", (size_t) o_ffxFsr3UpscalerContextCreate_Dx12);
    }

    if (o_ffxFsr3UpscalerContextDispatch_Dx12 == nullptr)
    {
        o_ffxFsr3UpscalerContextDispatch_Dx12 = (PFN_ffxFsr3UpscalerContextDispatch) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxFsr3UpscalerContextDispatch");

        if (o_ffxFsr3UpscalerContextDispatch_Dx12 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr3UpscalerContextDispatch_Dx12, ffxFsr3ContextDispatch_Dx12);

        LOG_DEBUG("ffxFsr3UpscalerContextDispatch_Dx12: {:X}", (size_t) o_ffxFsr3UpscalerContextDispatch_Dx12);
    }

    if (o_ffxFsr3UpscalerContextDestroy_Dx12 == nullptr)
    {
        o_ffxFsr3UpscalerContextDestroy_Dx12 = (PFN_ffxFsr3UpscalerContextDestroy) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxFsr3UpscalerContextDestroy");

        if (o_ffxFsr3UpscalerContextDestroy_Dx12 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr3UpscalerContextDestroy_Dx12, ffxFsr3ContextDestroy_Dx12);

        LOG_DEBUG("ffxFsr3UpscalerContextDestroy_Dx12: {:X}", (size_t) o_ffxFsr3UpscalerContextDestroy_Dx12);
    }

    if (o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12 == nullptr)
    {
        o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12 =
            (PFN_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode) KernelBaseProxy::GetProcAddress_()(
                exeModule, "ffxFsr3UpscalerGetUpscaleRatioFromQualityMode");

        if (o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12,
                         ffxFsr3GetUpscaleRatioFromQualityMode_Dx12);

        LOG_DEBUG("ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12: {:X}",
                  (size_t) o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12);
    }

    if (o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12 == nullptr)
    {
        o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12 =
            (PFN_ffxFsr3UpscalerGetRenderResolutionFromQualityMode) KernelBaseProxy::GetProcAddress_()(
                exeModule, "ffxFsr3UpscalerGetRenderResolutionFromQualityMode");

        if (o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12,
                         ffxFsr3GetRenderResolutionFromQualityMode_Dx12);

        LOG_DEBUG("ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12: {:X}",
                  (size_t) o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12);
    }

    if (Config::Instance()->Fsr3Pattern.value_or_default())
    {
        // Create
        LOG_DEBUG("Checking createPattern");
        std::string_view createPattern(
            "48 ? ? ? ? 57 48 83 EC 20 48 8B DA 41 B8 ? ? ? ? 33 D2 48 8B F9 E8 ? ? ? ? 48 85 FF 74 ? 48 85 DB");
        o_ffxFsr3UpscalerContextCreate_Pattern_Dx12 =
            (PFN_ffxFsr3UpscalerContextCreate) scanner::GetAddress(exeModule, createPattern, 0);

        // RDR1 have duplicate methods and first found one is not used
        if (o_ffxFsr3UpscalerContextCreate_Pattern_Dx12 != nullptr &&
            State::Instance().gameQuirks & GameQuirk::SkipFsr3Method)
            o_ffxFsr3UpscalerContextCreate_Pattern_Dx12 = (PFN_ffxFsr3UpscalerContextCreate) scanner::GetAddress(
                exeModule, createPattern, 0, (size_t) o_ffxFsr3UpscalerContextCreate_Pattern_Dx12 + 2);

        if (o_ffxFsr3UpscalerContextCreate_Pattern_Dx12 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr3UpscalerContextCreate_Pattern_Dx12, ffxFsr3ContextCreate_Pattern_Dx12);

        // Destroy
        LOG_DEBUG("Checking destroyPattern");
        std::string_view destroyPattern(
            "40 ? ? ? ? 20 48 8B D9 48 85 C9 75 ? B8 ? ? ? ? 48 83 C4 20 5B C3 44 8B 81 ? ? ? ? 48 8D 91 ? ? ? ? 48 ? "
            "? ? ? 48 83 C1 18 48 ? ? ? ? 48 ? ? ? ? E8 ? ? ? ? 44 8B 83");

        // RDR1 have duplicate methods and first found one is not used
        if (State::Instance().gameQuirks & GameQuirk::SkipFsr3Method &&
            o_ffxFsr3UpscalerContextCreate_Pattern_Dx12 != nullptr)
            o_ffxFsr3UpscalerContextDestroy_Pattern_Dx12 = (PFN_ffxFsr3UpscalerContextDestroy) scanner::GetAddress(
                exeModule, destroyPattern, 0, (size_t) o_ffxFsr3UpscalerContextCreate_Pattern_Dx12);
        else
            o_ffxFsr3UpscalerContextDestroy_Pattern_Dx12 =
                (PFN_ffxFsr3UpscalerContextDestroy) scanner::GetAddress(exeModule, destroyPattern, 0);

        if (o_ffxFsr3UpscalerContextDestroy_Pattern_Dx12 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr3UpscalerContextDestroy_Pattern_Dx12, ffxFsr3ContextDestroy_Pattern_Dx12);

        LOG_DEBUG("ffxFsr3UpscalerContextDestroy_Pattern_Dx12: {:X}",
                  (size_t) o_ffxFsr3UpscalerContextDestroy_Pattern_Dx12);

        // Dispatch
        LOG_DEBUG("Checking dispatchPattern");
        std::string_view dispatchPattern("48 85 C9 74 36 48 85 D2 74 31 8B 41 04 39 82 ? ? ? ? 77 20 8B 41 08 39 82 ? "
                                         "? ? ? 77 15 48 83 B9 ? ? ? ? ? 75 06 B8 ? ? ? ? C3");

        // RDR1 have duplicate methods and first found one is not used
        if (State::Instance().gameQuirks & GameQuirk::SkipFsr3Method &&
            o_ffxFsr3UpscalerContextCreate_Pattern_Dx12 != nullptr)
            o_ffxFsr3UpscalerContextDispatch_Pattern_Dx12 = (PFN_ffxFsr3UpscalerContextDispatch) scanner::GetAddress(
                exeModule, dispatchPattern, 0, (size_t) o_ffxFsr3UpscalerContextCreate_Pattern_Dx12);
        else
            o_ffxFsr3UpscalerContextDispatch_Pattern_Dx12 =
                (PFN_ffxFsr3UpscalerContextDispatch) scanner::GetAddress(exeModule, dispatchPattern, 0);

        if (o_ffxFsr3UpscalerContextDispatch_Pattern_Dx12 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr3UpscalerContextDispatch_Pattern_Dx12, ffxFsr3ContextDispatch_Pattern_Dx12);

        LOG_DEBUG("ffxFsr3UpscalerContextDispatch_Pattern_Dx12: {:X}",
                  (size_t) o_ffxFsr3UpscalerContextDispatch_Pattern_Dx12);

        // Ratio from quality
        LOG_DEBUG("Checking dispatchPattern");
        std::string_view rfqPattern(
            "85 C9 74 3C 83 E9 01 74 2E 83 E9 01 74 20 83 E9 01 74 12 83 F9 01 74 04 0F 57 C0 C3");

        // RDR1 have duplicate methods and first found one is not used
        o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Pattern_Dx12 =
            (PFN_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode) scanner::GetAddress(exeModule, rfqPattern, 0);

        if (o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Pattern_Dx12 != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Pattern_Dx12,
                         ffxFsr3GetUpscaleRatioFromQualityMode_Pattern_Dx12);

        LOG_DEBUG("ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Pattern_Dx12: {:X}",
                  (size_t) o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Pattern_Dx12);
    }

    // if (o_ffxFSR3GetInterfaceDX12 == nullptr)
    //{
    //     o_ffxFSR3GetInterfaceDX12 = (PFN_ffxFSR3GetInterfaceDX12)DetourFindFunction(exeModule,
    //     "ffxGetInterfaceDX12");

    //    if (o_ffxFSR3GetInterfaceDX12 != nullptr)
    //        DetourAttach(&(PVOID&)o_ffxFSR3GetInterfaceDX12, hk_ffxFsr3GetInterfaceDX12);

    //    LOG_DEBUG("ffxGetInterfaceDX12: {:X}", (size_t)o_ffxFSR3GetInterfaceDX12);
    //}

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("DetourTransactionCommit failed: {:X}", detourResult);
        o_ffxFSR3GetInterfaceDX12 = nullptr;
        o_ffxFsr3UpscalerContextCreate_Dx12 = nullptr;
        o_ffxFsr3UpscalerContextDispatch_Dx12 = nullptr;
        o_ffxFsr3UpscalerContextDestroy_Dx12 = nullptr;
        o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12 = nullptr;
        o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12 = nullptr;

        o_ffxFsr3UpscalerContextCreate_Pattern_Dx12 = nullptr;
        o_ffxFsr3UpscalerContextDispatch_Pattern_Dx12 = nullptr;
        o_ffxFsr3UpscalerContextDestroy_Pattern_Dx12 = nullptr;
    }
    else
    {
        State::Instance().fsrHooks =
            o_ffxFsr3UpscalerContextCreate_Dx12 != nullptr || o_ffxFsr3UpscalerContextCreate_Pattern_Dx12 != nullptr;
    }
}

void HookFSR3Inputs(HMODULE module)
{
    LOG_INFO("Trying to hook FSR3 methods");

    if (module != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_ffxFSR3GetInterfaceDX12 == nullptr)
        {
            o_ffxFsr3UpscalerContextCreate_Dx12 = (PFN_ffxFsr3UpscalerContextCreate) KernelBaseProxy::GetProcAddress_()(
                module, "ffxFsr3UpscalerContextCreate");

            if (o_ffxFsr3UpscalerContextCreate_Dx12 != nullptr)
                DetourAttach(&(PVOID&) o_ffxFsr3UpscalerContextCreate_Dx12, ffxFsr3ContextCreate_Dx12);

            LOG_DEBUG("ffxFsr3UpscalerContextCreate_Dx12: {:X}", (size_t) o_ffxFsr3UpscalerContextCreate_Dx12);
        }

        if (o_ffxFsr3UpscalerContextDispatch_Dx12 == nullptr)
        {
            o_ffxFsr3UpscalerContextDispatch_Dx12 =
                (PFN_ffxFsr3UpscalerContextDispatch) KernelBaseProxy::GetProcAddress_()(
                    module, "ffxFsr3UpscalerContextDispatch");

            if (o_ffxFsr3UpscalerContextDispatch_Dx12 != nullptr)
                DetourAttach(&(PVOID&) o_ffxFsr3UpscalerContextDispatch_Dx12, ffxFsr3ContextDispatch_Dx12);

            LOG_DEBUG("ffxFsr3UpscalerContextDispatch_Dx12: {:X}", (size_t) o_ffxFsr3UpscalerContextDispatch_Dx12);
        }

        if (o_ffxFsr3UpscalerContextDestroy_Dx12 == nullptr)
        {
            o_ffxFsr3UpscalerContextDestroy_Dx12 =
                (PFN_ffxFsr3UpscalerContextDestroy) KernelBaseProxy::GetProcAddress_()(module,
                                                                                       "ffxFsr3UpscalerContextDestroy");

            if (o_ffxFsr3UpscalerContextDestroy_Dx12 != nullptr)
                DetourAttach(&(PVOID&) o_ffxFsr3UpscalerContextDestroy_Dx12, ffxFsr3ContextDestroy_Dx12);

            LOG_DEBUG("ffxFsr3UpscalerContextDestroy_Dx12: {:X}", (size_t) o_ffxFsr3UpscalerContextDestroy_Dx12);
        }

        if (o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12 == nullptr)
        {
            o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12 =
                (PFN_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode) KernelBaseProxy::GetProcAddress_()(
                    module, "ffxFsr3UpscalerGetUpscaleRatioFromQualityMode");

            if (o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12 != nullptr)
                DetourAttach(&(PVOID&) o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12,
                             ffxFsr3GetUpscaleRatioFromQualityMode_Dx12);

            LOG_DEBUG("ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12: {:X}",
                      (size_t) o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12);
        }

        if (o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12 == nullptr)
        {
            o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12 =
                (PFN_ffxFsr3UpscalerGetRenderResolutionFromQualityMode) KernelBaseProxy::GetProcAddress_()(
                    module, "ffxFsr3UpscalerGetRenderResolutionFromQualityMode");

            if (o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12 != nullptr)
                DetourAttach(&(PVOID&) o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12,
                             ffxFsr3GetRenderResolutionFromQualityMode_Dx12);

            LOG_DEBUG("ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12: {:X}",
                      (size_t) o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12);
        }

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("DetourTransactionCommit failed: {:X}", detourResult);
            o_ffxFSR3GetInterfaceDX12 = nullptr;
            o_ffxFsr3UpscalerContextCreate_Dx12 = nullptr;
            o_ffxFsr3UpscalerContextDispatch_Dx12 = nullptr;
            o_ffxFsr3UpscalerContextDestroy_Dx12 = nullptr;
            o_ffxFsr3UpscalerGetUpscaleRatioFromQualityMode_Dx12 = nullptr;
            o_ffxFsr3UpscalerGetRenderResolutionFromQualityMode_Dx12 = nullptr;
        }
        else
        {
            State::Instance().fsrHooks = o_ffxFsr3UpscalerContextCreate_Dx12 != nullptr;
        }
    }
}

void HookFSR3Dx12Inputs(HMODULE module)
{
    LOG_INFO("Trying to hook FSR3 methods");

    return;

    if (module != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_ffxFSR3GetInterfaceDX12 == nullptr)
        {
            o_ffxFSR3GetInterfaceDX12 =
                (PFN_ffxFSR3GetInterfaceDX12) KernelBaseProxy::GetProcAddress_()(module, "ffxGetInterfaceDX12");

            if (o_ffxFSR3GetInterfaceDX12 != nullptr)
                DetourAttach(&(PVOID&) o_ffxFSR3GetInterfaceDX12, hk_ffxFsr3GetInterfaceDX12);

            LOG_DEBUG("ffxGetInterfaceDX12: {:X}", (size_t) o_ffxFSR3GetInterfaceDX12);
        }

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("DetourTransactionCommit failed: {:X}", detourResult);
            o_ffxFSR3GetInterfaceDX12 = nullptr;
        }
    }
}
