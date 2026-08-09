#include "pch.h"
#include "FfxApi_Dx12.h"

#include "Util.h"
#include "Config.h"

#include "resource.h"
#include "NVNGX_Parameter.h"
#include "proxies/FfxApi_Proxy.h"

#include "FG/FfxApi_Dx12_FG.h"

#include "ffx_upscale.h"
#include "dx12/ffx_api_dx12.h"

#include <magic_enum.hpp>

static std::unordered_map<ffxContext, ffxCreateContextDescUpscale> _initParams;
static std::unordered_map<ffxContext, NVSDK_NGX_Parameter*> _nvParams;
static std::unordered_map<ffxContext, NVSDK_NGX_Handle*> _contexts;
static ID3D12Device* _d3d12Device = nullptr;
static bool _nvnxgInited = false;
static float qualityRatios[] = { 1.0f, 1.5f, 1.7f, 2.0f, 3.0f };
static size_t _contextCounter = 0x00001ee7;

static float halton(int32_t index, int32_t base)
{
    float f = 1.0f, result = 0.0f;

    for (int32_t currentIndex = index; currentIndex > 0;)
    {

        f /= (float) base;
        result = result + f * (float) (currentIndex % base);
        currentIndex = (uint32_t) (floorf((float) (currentIndex) / (float) (base)));
    }

    return result;
}

static bool ffxGetJitterOffsetLocal(float* outX, float* outY, int32_t index, int32_t phaseCount)
{
    if (outX == nullptr)
        return false;

    if (outY == nullptr)
        return false;

    if (phaseCount <= 0)
        return false;

    const float x = halton((index % phaseCount) + 1, 2) - 0.5f;
    const float y = halton((index % phaseCount) + 1, 3) - 0.5f;

    *outX = x;
    *outY = y;

    return true;
}

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

ffxReturnCode_t ffxCreateContext_Dx12(ffxContext* context, ffxCreateContextDescHeader* desc,
                                      const ffxAllocationCallbacks* memCb)
{
    LOG_DEBUG("");

    if (desc == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("type: {}", FfxApiProxy::GetTypeName(desc->type));

    auto& state = State::Instance();
    auto type = FfxApiProxy::GetType(desc->type);

    // Extra checks added for Silent Hill f
    // Game is creating FSR-FG swapchain and calling present twice per frame
    // So when using OptiFG I am hijacking FSR-FG swapchain
    // It would crash the games which uses swapchain for FG
    const bool isFgType = type == FFXStructType::SwapchainDX12 || type == FFXStructType::FG;
    const bool swapchainCreationType =
        desc->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12 ||
        desc->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
    const bool unaffectedOutput = state.activeFgOutput == FGOutput::NoFG || state.activeFgInput == FGInput::NvngxFG;
    const bool shouldCaptureGamesSwapchain = Config::Instance()->FGAlwaysCaptureFSRFGSwapchain.value_or_default() &&
                                             !unaffectedOutput && swapchainCreationType;
    if (isFgType && (state.activeFgInput == FGInput::FSRFG || shouldCaptureGamesSwapchain))
    {
        auto result = ffxCreateContext_Dx12FG(context, desc, memCb);

        if (result == PASSTHRU_RETURN_CODE)
            return FfxApiProxy::D3D12_CreateContext(context, desc, memCb);

        return result;
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

    if (!upscaleContext || Config::Instance()->EnableHotSwapping.value_or_default())
    {
        ScopedSkipHeapCapture skipHeapCapture {};
        auto ffxApiResult = FfxApiProxy::D3D12_CreateContext(context, desc, memCb);

        LOG_DEBUG("D3D12_CreateContext result: {:X} ({}), context: {:X}", (UINT) ffxApiResult,
                  FfxApiProxy::ReturnCodeToString(ffxApiResult), (size_t) *context);

        if (!upscaleContext)
            return ffxApiResult;
    }

    if (!state.nvngxDx12Inited)
    {
        NVSDK_NGX_FeatureCommonInfo fcInfo {};
        auto exePath = Util::ExePath().remove_filename();

        auto nvResult = NVSDK_NGX_D3D12_Init_with_ProjectID(
            OPTI_GUID, state.NVNGX_Engine, OPTI_VERSION, exePath.c_str(), _d3d12Device, &fcInfo,
            state.NVNGX_Version == 0 ? NVSDK_NGX_Version_API : state.NVNGX_Version);

        if (nvResult != NVSDK_NGX_Result_Success)
            return FFX_API_RETURN_ERROR_RUNTIME_ERROR;

        _nvnxgInited = true;
    }

    if (!Config::Instance()->EnableHotSwapping.value_or_default())
    {
        *context = (ffxContext) ++_contextCounter;
        LOG_INFO("Custom context index:{}", _contextCounter);
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

ffxReturnCode_t ffxDestroyContext_Dx12(ffxContext* context, const ffxAllocationCallbacks* memCb)
{
    LOG_DEBUG("");

    if (context == nullptr || *context == nullptr)
        return FFX_API_RETURN_OK;

    LOG_DEBUG("context: {:X}", (size_t) *context);

    if (*context == (void*) scContext || *context == (void*) fgContext)
    {
        auto result = ffxDestroyContext_Dx12FG(context, memCb);

        if (result == PASSTHRU_RETURN_CODE)
            return FfxApiProxy::D3D12_DestroyContext(context, memCb);

        return result;
    }

    bool upscalerContext =
        _contexts.contains(*context) || _initParams.contains(*context) || _nvParams.contains(*context);

    if (_contexts.contains(*context))
        NVSDK_NGX_D3D12_ReleaseFeature(_contexts[*context]);

    _contexts.erase(*context);
    _nvParams.erase(*context);
    _initParams.erase(*context);

    if (upscalerContext && !Config::Instance()->EnableHotSwapping.value_or_default())
        return FFX_API_RETURN_OK;

    if (State::Instance().currentFG != nullptr)
        LOG_DEBUG("context: {:X}, SwapchainContext: {:X}, FGContext: {:X}", (size_t) *context,
                  (size_t) State::Instance().currentFG->SwapchainContext(),
                  (size_t) State::Instance().currentFG->FrameGenerationContext());

    // Destroy real upscaler context
    if (!State::Instance().isShuttingDown && upscalerContext &&
        Config::Instance()->EnableHotSwapping.value_or_default())
    {
        auto cdResult = FfxApiProxy::D3D12_DestroyContext(context, memCb);
        LOG_INFO("result: {:X}", (UINT) cdResult);
        return FFX_API_RETURN_OK;
    }

    if (State::Instance().activeFgInput == FGInput::FSRFG && !upscalerContext)
    {
        auto result = ffxDestroyContext_Dx12FG(context, memCb);

        if (result != rcContinue)
            return result;
    }

    auto cdResult = FfxApiProxy::D3D12_DestroyContext(context, memCb);
    LOG_INFO("result: {:X}", (UINT) cdResult);
    return cdResult;
}

ffxReturnCode_t ffxConfigure_Dx12(ffxContext* context, ffxConfigureDescHeader* desc)
{
    if (desc == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("type: {}", FfxApiProxy::GetTypeName(desc->type));

    auto type = FfxApiProxy::GetType(desc->type);
    if (type == FFXStructType::SwapchainDX12 || type == FFXStructType::FG)
    {
        ffxReturnCode_t result = PASSTHRU_RETURN_CODE;

        if (State::Instance().activeFgInput == FGInput::FSRFG)
            result = ffxConfigure_Dx12FG(context, desc);

        if (result == PASSTHRU_RETURN_CODE)
            return FfxApiProxy::D3D12_Configure(context, desc);

        return result;
    }

    if (desc->type == FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE)
    {

        auto kvDesc = (ffxConfigureDescUpscaleKeyValue*) desc;

        LOG_DEBUG("key: {}, value: {}, ptr: {:X}", magic_enum::enum_name((FfxApiConfigureUpscaleKey) kvDesc->key),
                  kvDesc->u64, (size_t) kvDesc->ptr);

        if (!Config::Instance()->EnableHotSwapping.value_or_default())
            return FFX_API_RETURN_OK;
    }

    if (Config::Instance()->EnableHotSwapping.value_or_default())
        return FfxApiProxy::D3D12_Configure(context, desc);

    return FFX_API_RETURN_OK;
}

ffxReturnCode_t ffxQuery_Dx12(ffxContext* context, ffxQueryDescHeader* desc)
{
    LOG_DEBUG("");

    if (desc == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    auto type = FfxApiProxy::GetIndirectType(desc);
    LOG_DEBUG("Header type: {}, Indirect type: {}", FfxApiProxy::GetTypeName(desc->type), magic_enum::enum_name(type));

    if (type == FFXStructType::SwapchainDX12 || type == FFXStructType::FG)
    {
        ffxReturnCode_t result = PASSTHRU_RETURN_CODE;

        if (State::Instance().activeFgInput == FGInput::FSRFG)
            result = ffxQuery_Dx12FG(context, desc);

        if (result == PASSTHRU_RETURN_CODE)
            return FfxApiProxy::D3D12_Query(context, desc);

        return result;
    }

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
    else if (desc->type == FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT)
    {
        // Take output scaling into account
        auto jitterPhaseDesc = (ffxQueryDescUpscaleGetJitterPhaseCount*) desc;

        if (jitterPhaseDesc && State::Instance().currentFeature)
        {
            jitterPhaseDesc->displayWidth = State::Instance().currentFeature->TargetWidth();
            jitterPhaseDesc->renderWidth = State::Instance().currentFeature->RenderWidth();
        }

        if (!Config::Instance()->EnableHotSwapping.value_or_default())
        {
            float ratio = (float) jitterPhaseDesc->displayWidth / (float) jitterPhaseDesc->renderWidth;
            *jitterPhaseDesc->pOutPhaseCount = static_cast<int32_t>(ceil(ratio * ratio * 8.0f)); // ceil(8*n^2)
            LOG_DEBUG("Render resolution: {}, Display resolution: {}, Ratio: {}, Jitter phase count: {}",
                      jitterPhaseDesc->renderWidth, jitterPhaseDesc->displayWidth, ratio,
                      *jitterPhaseDesc->pOutPhaseCount);

            return FFX_API_RETURN_OK;
        }
    }
    else if (desc->type == FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION)
    {
        auto providerDesc = (ffxQueryGetProviderVersion*) desc;
        feature_version ver = {};

        if (type == FFXStructType::SwapchainDX12 || type == FFXStructType::FG)
        {
            ver = FfxApiProxy::VersionDx12_FG();

            providerDesc->versionId =
                0xF600'0000ui64 << 32u << 32 | (((ver.major << 22) | (ver.minor << 12) | ver.patch) & 0xFFFFFFFF);
        }
        else if (type == FFXStructType::Upscaling)
        {
            ver = FfxApiProxy::VersionDx12_SR();

            providerDesc->versionId =
                0xF5A5'CA1Eui64 << 32 | (((ver.major << 22) | (ver.minor << 12) | ver.patch) & 0xFFFFFFFF);
        }
        else if (type == FFXStructType::Denoiser)
        {
            ver = FfxApiProxy::VersionDx12_RR();

            providerDesc->versionId =
                0xF5A5'CA1Eui64 << 32 | (((ver.major << 22) | (ver.minor << 12) | ver.patch) & 0xFFFFFFFF);
        }
        else if (type == FFXStructType::RadianceCache)
        {
            ver = FfxApiProxy::VersionDx12_RC();

            providerDesc->versionId =
                0xF5A5'CA1Eui64 << 32 | (((ver.major << 22) | (ver.minor << 12) | ver.patch) & 0xFFFFFFFF);
        }
        else
        {
            ver = FfxApiProxy::VersionDx12();

            providerDesc->versionId = (((ver.major << 22) | (ver.minor << 12) | ver.patch) & 0xFFFFFFFF);
        }

        auto verName = std::format("{}.{}.{}", ver.major, ver.minor, ver.patch);
        providerDesc->versionName = _strdup(verName.c_str());

        return FFX_API_RETURN_OK;
    }
    else if (desc->type == FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET)
    {
        auto joDesc = (ffxQueryDescUpscaleGetJitterOffset*) desc;

        if (ffxGetJitterOffsetLocal(joDesc->pOutX, joDesc->pOutY, joDesc->index, joDesc->phaseCount))
        {
            LOG_DEBUG("Jitter offset: ({}, {})", *joDesc->pOutX, *joDesc->pOutY);
            return FFX_API_RETURN_OK;
        }

        if (joDesc->pOutX != nullptr)
            *joDesc->pOutX = 0.0f;

        if (joDesc->pOutY != nullptr)
            *joDesc->pOutY = 0.0f;

        LOG_DEBUG("Jitter offset: (0.0, 0.0)");

        return FFX_API_RETURN_OK;
    }

    if (context != nullptr && _contexts.contains(*context) && !Config::Instance()->EnableHotSwapping.value_or_default())
    {
        LOG_INFO("Hot swapping disabled, ignoring upscaler query");
        return FFX_API_RETURN_OK;
    }

    // Need to redirect base queries to real FfxApi
    if (Config::Instance()->EnableHotSwapping.value_or_default() ||
        FfxApiProxy::GetType(desc->type) == FFXStructType::General)
    {
        return FfxApiProxy::D3D12_Query(context, desc);
    }

    return FFX_API_RETURN_OK;
}

ffxReturnCode_t ffxDispatch_Dx12(ffxContext* context, ffxDispatchDescHeader* desc)
{
    if (desc == nullptr || context == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("context: {:X}, type: {}", (size_t) *context, FfxApiProxy::GetTypeName(desc->type));

    auto type = FfxApiProxy::GetType(desc->type);
    if (type == FFXStructType::SwapchainDX12 || type == FFXStructType::FG)
    {
        ffxReturnCode_t result = PASSTHRU_RETURN_CODE;

        if (State::Instance().activeFgInput == FGInput::FSRFG)
            result = ffxDispatch_Dx12FG(context, desc);

        if (result == PASSTHRU_RETURN_CODE)
            return FfxApiProxy::D3D12_Dispatch(context, desc);

        return result;
    }

    // Skip OptiScaler stuff
    if (Config::Instance()->EnableHotSwapping.value_or_default() &&
        !Config::Instance()->UseFfxInputs.value_or_default())
        return FfxApiProxy::D3D12_Dispatch(context, desc);

    if (context == nullptr || !_initParams.contains(*context))
    {
        LOG_INFO("Not in _contexts");
        return FfxApiProxy::D3D12_Dispatch(context, desc);
    }

    ffxApiHeader* header = desc;
    ffxDispatchDescUpscale* dispatchDesc = nullptr;
    bool rmDesc = false;

    // For now we are assuming dispatch is called with only one desc
    if (header->type == FFX_API_DISPATCH_DESC_TYPE_UPSCALE)
    {
        dispatchDesc = (ffxDispatchDescUpscale*) header;
    }
    else if (!Config::Instance()->EnableHotSwapping.value_or_default() &&
             header->type == FFX_API_DISPATCH_DESC_TYPE_UPSCALE_GENERATEREACTIVEMASK)
    {
        return FFX_API_RETURN_OK;
    }

    if (dispatchDesc == nullptr)
    {
        LOG_INFO("dispatchDesc == nullptr, desc type: {:X}", desc->type);
        return FfxApiProxy::D3D12_Dispatch(context, desc);
    }

    if (dispatchDesc->commandList == nullptr)
        return FfxApiProxy::D3D12_Dispatch(context, desc);

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

    if (dispatchDesc->reactive.description.width >= dispatchDesc->renderSize.width &&
        dispatchDesc->reactive.description.height >= dispatchDesc->renderSize.height)
        params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, dispatchDesc->reactive.resource);

    params->Set(NVSDK_NGX_Parameter_Color, dispatchDesc->color.resource);
    params->Set(NVSDK_NGX_Parameter_MotionVectors, dispatchDesc->motionVectors.resource);
    params->Set(NVSDK_NGX_Parameter_Output, dispatchDesc->output.resource);
    params->Set("FSR.cameraNear", dispatchDesc->cameraNear);
    params->Set("FSR.cameraFar", dispatchDesc->cameraFar);
    params->Set("FSR.cameraFovAngleVertical", dispatchDesc->cameraFovAngleVertical);
    params->Set("FSR.frameTimeDelta", dispatchDesc->frameTimeDelta);
    params->Set("FSR.viewSpaceToMetersFactor", dispatchDesc->viewSpaceToMetersFactor);

    if (dispatchDesc->transparencyAndComposition.description.width >= dispatchDesc->renderSize.width &&
        dispatchDesc->transparencyAndComposition.description.height >= dispatchDesc->renderSize.height)
        params->Set("FSR.transparencyAndComposition", dispatchDesc->transparencyAndComposition.resource);

    if (dispatchDesc->reactive.description.width >= dispatchDesc->renderSize.width &&
        dispatchDesc->reactive.description.height >= dispatchDesc->renderSize.height)
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
