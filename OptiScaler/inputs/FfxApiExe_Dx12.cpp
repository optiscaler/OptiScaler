#include "pch.h"
#include "FfxApi_Dx12.h"
#include "Config.h"
#include "Util.h"

#include "resource.h"
#include "NVNGX_Parameter.h"

#include <proxies/KernelBase_Proxy.h>

#include "detours/detours.h"

#include "ffx_api.h"
#include "ffx_upscale.h"
#include "dx12/ffx_api_dx12.h"
#include "FfxApiExe_Dx12.h"

inline static PfnFfxCreateContext _D3D12_CreateContext = nullptr;
inline static PfnFfxDestroyContext _D3D12_DestroyContext = nullptr;
inline static PfnFfxConfigure _D3D12_Configure = nullptr;
inline static PfnFfxQuery _D3D12_Query = nullptr;
inline static PfnFfxDispatch _D3D12_Dispatch = nullptr;

static std::unordered_map<ffxContext, ffxCreateContextDescUpscale> _initParams;
static std::unordered_map<ffxContext, NVSDK_NGX_Parameter*> _nvParams;
static std::unordered_map<ffxContext, NVSDK_NGX_Handle*> _contexts;
static ID3D12Device* _d3d12Device = nullptr;
static bool _nvnxgInited = false;
static float qualityRatios[] = { 1.0f, 1.5f, 1.7f, 2.0f, 3.0f };

static bool CreateDLSSContext(ffxContext handle, const ffxDispatchDescUpscale* pExecParams)
{
    LOG_DEBUG("context: {:X}", (size_t) handle);

    if (!_nvParams.contains(handle))
        return false;

    NVSDK_NGX_Handle* nvHandle = nullptr;
    auto params = _nvParams[handle];
    auto initParams = &_initParams[handle];
    auto commandList = (ID3D12GraphicsCommandList*) pExecParams->commandList;

    UINT initFlags = 0;

    if (initParams->flags & FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;

    if (initParams->flags & FFX_UPSCALE_ENABLE_DEPTH_INVERTED)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;

    if (initParams->flags & FFX_UPSCALE_ENABLE_AUTO_EXPOSURE)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

    if (initParams->flags & FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;

    if ((initParams->flags & FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) == 0)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

    State::Instance().dynamicResolution = (initParams->flags & FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION) != 0;

    params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, initFlags);

    params->Set(NVSDK_NGX_Parameter_Width, pExecParams->renderSize.width);
    params->Set(NVSDK_NGX_Parameter_Height, pExecParams->renderSize.height);
    params->Set(NVSDK_NGX_Parameter_OutWidth, initParams->maxUpscaleSize.width);
    params->Set(NVSDK_NGX_Parameter_OutHeight, initParams->maxUpscaleSize.height);
    params->Set("FSR.upscaleSize.width", pExecParams->upscaleSize.width);
    params->Set("FSR.upscaleSize.height", pExecParams->upscaleSize.height);

    auto width = pExecParams->upscaleSize.width > 0 ? pExecParams->upscaleSize.width : initParams->maxUpscaleSize.width;

    auto ratio = (float) width / (float) pExecParams->renderSize.width;

    LOG_INFO("renderWidth: {}, maxWidth: {}, ratio: {}", pExecParams->renderSize.width, width, ratio);

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

    auto nvngxResult = NVSDK_NGX_D3D12_CreateFeature(commandList, NVSDK_NGX_Feature_SuperSampling, params, &nvHandle);
    if (nvngxResult != NVSDK_NGX_Result_Success)
    {
        LOG_ERROR("NVSDK_NGX_D3D12_CreateFeature error: {:X}", (UINT) nvngxResult);
        return false;
    }

    _contexts[handle] = nvHandle;
    LOG_INFO("context created: {:X}", (size_t) handle);

    return true;
}

static std::optional<float> GetQualityOverrideRatioFfx(const uint32_t input)
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
    case FFX_UPSCALE_QUALITY_MODE_ULTRA_PERFORMANCE:
        if (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_UltraPerformance.value_or_default();

        break;

    case FFX_UPSCALE_QUALITY_MODE_PERFORMANCE:
        if (Config::Instance()->QualityRatio_Performance.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Performance.value_or_default();

        break;

    case FFX_UPSCALE_QUALITY_MODE_BALANCED:
        if (Config::Instance()->QualityRatio_Balanced.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Balanced.value_or_default();

        break;

    case FFX_UPSCALE_QUALITY_MODE_QUALITY:
        if (Config::Instance()->QualityRatio_Quality.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_Quality.value_or_default();

        break;

    case FFX_UPSCALE_QUALITY_MODE_NATIVEAA:
        if (Config::Instance()->QualityRatio_DLAA.value_or_default() >= sliderLimit)
            output = Config::Instance()->QualityRatio_DLAA.value_or_default();

        break;

    default:
        LOG_WARN("Unknown quality: {0}", (int) input);
        break;
    }

    if (output.has_value())
        LOG_DEBUG("ratio: {}", output.value());
    else
        LOG_DEBUG("ratio: no value");

    return output;
}

static ffxReturnCode_t ffxCreateContext_Dx12(ffxContext* context, ffxCreateContextDescHeader* desc,
                                             const ffxAllocationCallbacks* memCb)
{
    if (desc == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("type: {:X}", desc->type);

    auto ffxApiResult = _D3D12_CreateContext(context, desc, memCb);

    if (ffxApiResult != FFX_API_RETURN_OK)
    {
        LOG_ERROR("D3D12_CreateContext error: {:X}", (UINT) ffxApiResult);
        return ffxApiResult;
    }

    bool upscaleContext = false;
    ffxApiHeader* header = desc;
    ffxCreateContextDescUpscale* createDesc = nullptr;

    do
    {
        if (header->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE)
        {
            upscaleContext = true;
            createDesc = (ffxCreateContextDescUpscale*) header;
        }
        else if (header->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12)
        {
            auto backendDesc = (ffxCreateBackendDX12Desc*) header;
            _d3d12Device = backendDesc->device;
        }

        header = header->pNext;

    } while (header != nullptr);

    if (!upscaleContext)
        return ffxApiResult;

    if (!State::Instance().nvngxDx12Inited)
    {
        NVSDK_NGX_FeatureCommonInfo fcInfo {};
        auto exePath = Util::ExePath().remove_filename();

        auto nvResult = NVSDK_NGX_D3D12_Init_with_ProjectID(
            OPTI_GUID, State::Instance().NVNGX_Engine, OPTI_VERSION, exePath.c_str(), _d3d12Device, &fcInfo,
            State::Instance().NVNGX_Version == 0 ? NVSDK_NGX_Version_API : State::Instance().NVNGX_Version);

        if (nvResult != NVSDK_NGX_Result_Success)
            return FFX_API_RETURN_ERROR_RUNTIME_ERROR;

        _nvnxgInited = true;
    }

    NVSDK_NGX_Parameter* params = nullptr;

    if (NVSDK_NGX_D3D12_GetCapabilityParameters(&params) != NVSDK_NGX_Result_Success)
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;

    _nvParams[*context] = params;

    ffxCreateContextDescUpscale ccd {};
    ccd.flags = createDesc->flags;
    ccd.maxRenderSize = createDesc->maxRenderSize;
    ccd.maxUpscaleSize = createDesc->maxUpscaleSize;
    _initParams[*context] = ccd;

    LOG_INFO("context created: {:X}", (size_t) *context);

    return FFX_API_RETURN_OK;
}

static ffxReturnCode_t ffxDestroyContext_Dx12(ffxContext* context, const ffxAllocationCallbacks* memCb)
{
    if (context == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("context: {:X}", (size_t) *context);

    auto cdResult = _D3D12_DestroyContext(context, memCb);
    LOG_INFO("result: {:X}", (UINT) cdResult);

    if (_contexts.contains(*context))
        NVSDK_NGX_D3D12_ReleaseFeature(_contexts[*context]);

    _contexts.erase(*context);
    _nvParams.erase(*context);
    _initParams.erase(*context);

    return FFX_API_RETURN_OK;
}

static ffxReturnCode_t ffxConfigure_Dx12(ffxContext* context, ffxConfigureDescHeader* desc)
{
    if (desc == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("type: {:X}", desc->type);

    if (desc->type == FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE)
        return FFX_API_RETURN_OK;

    return _D3D12_Configure(context, desc);
}

static ffxReturnCode_t ffxQuery_Dx12(ffxContext* context, ffxQueryDescHeader* desc)
{
    if (desc == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("type: {:X}", desc->type);

    if (desc->type == FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE)
    {
        auto ratioDesc = (ffxQueryDescUpscaleGetRenderResolutionFromQualityMode*) desc;
        auto ratio = GetQualityOverrideRatioFfx(ratioDesc->qualityMode).value_or(qualityRatios[ratioDesc->qualityMode]);

        if (ratioDesc->pOutRenderHeight != nullptr)
            *ratioDesc->pOutRenderHeight = (uint32_t) (ratioDesc->displayHeight / ratio);

        if (ratioDesc->pOutRenderWidth != nullptr)
            *ratioDesc->pOutRenderWidth = (uint32_t) (ratioDesc->displayWidth / ratio);

        if (ratioDesc->pOutRenderWidth != nullptr && ratioDesc->pOutRenderHeight != nullptr)
            LOG_DEBUG("Quality mode: {}, Render resolution: {}x{}", ratioDesc->qualityMode, *ratioDesc->pOutRenderWidth,
                      *ratioDesc->pOutRenderHeight);
        else
            LOG_WARN("Quality mode: {}, pOutRenderWidth or pOutRenderHeight is null!", ratioDesc->qualityMode);

        return FFX_API_RETURN_OK;
    }
    else if (desc->type == FFX_API_QUERY_DESC_TYPE_UPSCALE_GETUPSCALERATIOFROMQUALITYMODE)
    {
        auto scaleDesc = (ffxQueryDescUpscaleGetUpscaleRatioFromQualityMode*) desc;
        *scaleDesc->pOutUpscaleRatio = GetQualityOverrideRatioFfx((FfxApiUpscaleQualityMode) scaleDesc->qualityMode)
                                           .value_or(qualityRatios[scaleDesc->qualityMode]);

        LOG_DEBUG("Quality mode: {}, Upscale ratio: {}", scaleDesc->qualityMode, *scaleDesc->pOutUpscaleRatio);

        return FFX_API_RETURN_OK;
    }

    return _D3D12_Query(context, desc);
}

static ffxReturnCode_t ffxDispatch_Dx12(ffxContext* context, ffxDispatchDescHeader* desc)
{
    // Skip OptiScaler stuff
    if (!Config::Instance()->UseFfxInputs.value_or_default())
        return _D3D12_Dispatch(context, desc);

    if (desc == nullptr || context == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("context: {:X}, type: {:X}", (size_t) *context, desc->type);

    if (context == nullptr || !_initParams.contains(*context))
    {
        LOG_INFO("Not in _contexts, desc type: {:X}", desc->type);
        return _D3D12_Dispatch(context, desc);
    }

    ffxApiHeader* header = desc;
    ffxDispatchDescUpscale* dispatchDesc = nullptr;
    bool rmDesc = false;

    do
    {
        if (header->type == FFX_API_DISPATCH_DESC_TYPE_UPSCALE)
            dispatchDesc = (ffxDispatchDescUpscale*) header;

        header = header->pNext;

    } while (header != nullptr);

    if (dispatchDesc == nullptr)
    {
        LOG_INFO("dispatchDesc == nullptr, desc type: {:X}", desc->type);
        return _D3D12_Dispatch(context, desc);
    }

    if (dispatchDesc->commandList == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    // If not in contexts list create and add context
    auto contextId = (size_t) *context;
    if (!_contexts.contains(*context) && _initParams.contains(*context) && !CreateDLSSContext(*context, dispatchDesc))
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;

    NVSDK_NGX_Parameter* params = _nvParams[*context];
    NVSDK_NGX_Handle* handle = _contexts[*context];

    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, dispatchDesc->jitterOffset.x);
    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, dispatchDesc->jitterOffset.y);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_X, dispatchDesc->motionVectorScale.x);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, dispatchDesc->motionVectorScale.y);
    params->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0);
    params->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, dispatchDesc->preExposure);
    params->Set(NVSDK_NGX_Parameter_Reset, dispatchDesc->reset ? 1 : 0);
    params->Set(NVSDK_NGX_Parameter_Width, dispatchDesc->renderSize.width);
    params->Set(NVSDK_NGX_Parameter_Height, dispatchDesc->renderSize.height);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, dispatchDesc->renderSize.width);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, dispatchDesc->renderSize.height);
    params->Set(NVSDK_NGX_Parameter_Depth, dispatchDesc->depth.resource);
    params->Set(NVSDK_NGX_Parameter_ExposureTexture, dispatchDesc->exposure.resource);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, dispatchDesc->reactive.resource);
    params->Set(NVSDK_NGX_Parameter_Color, dispatchDesc->color.resource);
    params->Set(NVSDK_NGX_Parameter_MotionVectors, dispatchDesc->motionVectors.resource);
    params->Set(NVSDK_NGX_Parameter_Output, dispatchDesc->output.resource);
    params->Set("FSR.cameraNear", dispatchDesc->cameraNear);
    params->Set("FSR.cameraFar", dispatchDesc->cameraFar);
    params->Set("FSR.cameraFovAngleVertical", dispatchDesc->cameraFovAngleVertical);
    params->Set("FSR.frameTimeDelta", dispatchDesc->frameTimeDelta);
    params->Set("FSR.viewSpaceToMetersFactor", dispatchDesc->viewSpaceToMetersFactor);
    params->Set("FSR.transparencyAndComposition", dispatchDesc->transparencyAndComposition.resource);
    params->Set("FSR.reactive", dispatchDesc->reactive.resource);
    params->Set(NVSDK_NGX_Parameter_Sharpness, dispatchDesc->sharpness);
    params->Set("FSR.upscaleSize.width", dispatchDesc->upscaleSize.width);
    params->Set("FSR.upscaleSize.height", dispatchDesc->upscaleSize.height);

    LOG_DEBUG("handle: {:X}, internalResolution: {}x{}", handle->Id, dispatchDesc->renderSize.width,
              dispatchDesc->renderSize.height);

    State::Instance().setInputApiName = ApiUpscalerInput::FFX_DX12;

    auto evalResult = NVSDK_NGX_D3D12_EvaluateFeature((ID3D12GraphicsCommandList*) dispatchDesc->commandList, handle,
                                                      params, nullptr);

    if (evalResult == NVSDK_NGX_Result_Success)
        return FFX_API_RETURN_OK;

    LOG_ERROR("evalResult: {:X}", (UINT) evalResult);
    return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
}

void HookFfxExeInputs()
{
    LOG_INFO("Trying to hook Ffx Exe methods");

    HMODULE exeModule = GetModuleHandle(Util::ExePath().filename().c_str());

    if (exeModule != nullptr)
    {
        _D3D12_Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxConfigure");
        _D3D12_CreateContext = (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxCreateContext");
        _D3D12_DestroyContext =
            (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxDestroyContext");
        _D3D12_Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxDispatch");
        _D3D12_Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxQuery");
    }

    if (_D3D12_CreateContext != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (_D3D12_Configure != nullptr)
        {
            LOG_DEBUG("ffxConfigure_Dx12: {:X}", (size_t) _D3D12_Configure);
            DetourAttach(&(PVOID&) _D3D12_Configure, ffxConfigure_Dx12);
        }

        if (_D3D12_CreateContext != nullptr)
        {
            LOG_DEBUG("ffxCreateContext_Dx12: {:X}", (size_t) _D3D12_CreateContext);
            DetourAttach(&(PVOID&) _D3D12_CreateContext, ffxCreateContext_Dx12);
        }

        if (_D3D12_DestroyContext != nullptr)
        {
            LOG_DEBUG("ffxDestroyContext_Dx12: {:X}", (size_t) _D3D12_DestroyContext);
            DetourAttach(&(PVOID&) _D3D12_DestroyContext, ffxDestroyContext_Dx12);
        }

        if (_D3D12_Dispatch != nullptr)
        {
            LOG_DEBUG("ffxDispatch_Dx12: {:X}", (size_t) _D3D12_Dispatch);
            DetourAttach(&(PVOID&) _D3D12_Dispatch, ffxDispatch_Dx12);
        }

        if (_D3D12_Query != nullptr)
        {
            LOG_DEBUG("ffxQuery_Dx12: {:X}", (size_t) _D3D12_Query);
            DetourAttach(&(PVOID&) _D3D12_Query, ffxQuery_Dx12);
        }

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook Ffx Exe methods: {:X}", (UINT) detourResult);
            _D3D12_Configure = nullptr;
            _D3D12_CreateContext = nullptr;
            _D3D12_DestroyContext = nullptr;
            _D3D12_Dispatch = nullptr;
            _D3D12_Query = nullptr;
        }
        else
        {
            State::Instance().fsrHooks = true;
        }
    }
}
