#include "FfxApi_Dx12.h"
#include "Config.h"
#include "Util.h"

#include "resource.h"
#include "proxies/FfxApi_Proxy.h"
#include "NVNGX_Parameter.h"

#include "ffx_upscale.h"
#include "dx12/ffx_api_dx12.h"

static std::map<ffxContext, ffxCreateContextDescUpscale> _initParams;
static std::map<ffxContext, NVSDK_NGX_Parameter*> _nvParams;
static std::map<ffxContext, NVSDK_NGX_Handle*> _contexts;
static ID3D12Device* _d3d12Device = nullptr;
static bool _nvnxgInited = false;
static float qualityRatios[] = { 1.0, 1.5, 1.7, 2.0, 3.0 };
static size_t _contextCounter = 0;

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

    if (ratio <= 3.0)
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_UltraPerformance);
    else if (ratio <= 2.0)
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_MaxPerf);
    else if (ratio <= 1.7)
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_Balanced);
    else if (ratio <= 1.5)
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_MaxQuality);
    else if (ratio <= 1.3)
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
    if (desc == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("type: {:X}", desc->type);

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
        else if (State::Instance().activeFgType == OptiFG)
        {
            if (header->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12)
            {
                LOG_INFO("Using already wrapped swapchain");
                return FFX_API_RETURN_OK;
            }
            else if (header->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12)
            {
                LOG_INFO("Creating OptiFG swapchain, new swapchain");
                auto fgDesc = (ffxCreateContextDescFrameGenerationSwapChainNewDX12*) header;
                auto scResult = fgDesc->dxgiFactory->CreateSwapChain(fgDesc->gameQueue, fgDesc->desc,
                                                                     (IDXGISwapChain**) fgDesc->swapchain);

                if (scResult == S_OK)
                {
                    if (State::Instance().currentFG == nullptr)
                    {
                        LOG_ERROR("State::Instance().currentFG is nullptr");
                        return FFX_API_RETURN_ERROR_PARAMETER;
                    }

                    void* scContext = State::Instance().currentFG->SwapchainContext();
                    *context = scContext;
                    return FFX_API_RETURN_OK;
                }
                else
                {
                    LOG_ERROR("CreateSwapChain error: {:X}", (UINT) scResult);
                    return FFX_API_RETURN_ERROR_PARAMETER;
                }
            }
            else if (header->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12)
            {
                LOG_INFO("Creating OptiFG swapchain, new swapchain for hwnd");
                auto fgDesc = (ffxCreateContextDescFrameGenerationSwapChainForHwndDX12*) header;

                IDXGIFactory2* factory = nullptr;
                auto scResult = fgDesc->dxgiFactory->QueryInterface(IID_PPV_ARGS(&factory));
                if (scResult != S_OK)
                {
                    LOG_ERROR("CreateSwapChain error: {:X}", (UINT) scResult);
                    return FFX_API_RETURN_ERROR_PARAMETER;
                }

                factory->Release();

                scResult = factory->CreateSwapChainForHwnd(fgDesc->gameQueue, fgDesc->hwnd, fgDesc->desc,
                                                           fgDesc->fullscreenDesc, nullptr,
                                                           (IDXGISwapChain1**) fgDesc->swapchain);
                if (scResult == S_OK)
                {
                    auto fg = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);
                    auto scContext = fg->SwapchainContext();
                    *context = scContext;
                    return FFX_API_RETURN_OK;
                }
                else
                {
                    LOG_ERROR("CreateSwapChainForHwnd error: {:X}", (UINT) scResult);
                    return FFX_API_RETURN_ERROR_PARAMETER;
                }
            }
        }

        header = header->pNext;

    } while (header != nullptr);

    if (!upscaleContext || Config::Instance()->EnableHotSwapping.value_or_default())
    {
        auto ffxApiResult = FfxApiProxy::D3D12_CreateContext()(context, desc, memCb);

        if (ffxApiResult != FFX_API_RETURN_OK)
            LOG_ERROR("D3D12_CreateContext error: {:X} ({})", (UINT) ffxApiResult,
                      FfxApiProxy::ReturnCodeToString(ffxApiResult));

        if (!upscaleContext)
            return ffxApiResult;
    }

    if (!State::Instance().NvngxDx12Inited)
    {
        NVSDK_NGX_FeatureCommonInfo fcInfo {};

        auto dllPath = Util::DllPath().remove_filename();
        auto nvngxDlssPath = Util::FindFilePath(dllPath, "nvngx_dlss.dll");
        auto nvngxDlssDPath = Util::FindFilePath(dllPath, "nvngx_dlssd.dll");
        auto nvngxDlssGPath = Util::FindFilePath(dllPath, "nvngx_dlssg.dll");

        std::vector<std::wstring> pathStorage;

        pathStorage.push_back(dllPath.wstring());

        if (nvngxDlssPath.has_value())
            pathStorage.push_back(nvngxDlssPath.value().parent_path().wstring());

        if (nvngxDlssDPath.has_value())
            pathStorage.push_back(nvngxDlssDPath.value().parent_path().wstring());

        if (nvngxDlssGPath.has_value())
            pathStorage.push_back(nvngxDlssGPath.value().parent_path().wstring());

        if (Config::Instance()->DLSSFeaturePath.has_value())
            pathStorage.push_back(Config::Instance()->DLSSFeaturePath.value());

        // Build pointer array
        wchar_t const** paths = new const wchar_t*[pathStorage.size()];
        for (size_t i = 0; i < pathStorage.size(); ++i)
        {
            paths[i] = pathStorage[i].c_str();
        }

        fcInfo.PathListInfo.Path = paths;
        fcInfo.PathListInfo.Length = (int) pathStorage.size();

        auto nvResult = NVSDK_NGX_D3D12_Init_with_ProjectID(
            "OptiScaler", State::Instance().NVNGX_Engine, VER_PRODUCT_VERSION_STR, dllPath.c_str(), _d3d12Device,
            &fcInfo, State::Instance().NVNGX_Version == 0 ? NVSDK_NGX_Version_API : State::Instance().NVNGX_Version);

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
    if (context == nullptr || *context == nullptr)
        return FFX_API_RETURN_OK;

    LOG_DEBUG("context: {:X}", (size_t) *context);

    bool upscalerContext = false;
    if (_contexts.contains(*context))
    {
        NVSDK_NGX_D3D12_ReleaseFeature(_contexts[*context]);
        upscalerContext = true;
    }

    _contexts.erase(*context);
    _nvParams.erase(*context);
    _initParams.erase(*context);

    if (State::Instance().currentFG != nullptr)
        LOG_DEBUG("context: {:X}, SwapchainContext: {:X}, FGContext: {:X}", (size_t) (*context),
                  (size_t) State::Instance().currentFG->SwapchainContext(),
                  (size_t) State::Instance().currentFG->FrameGenerationContext());

    if (!State::Instance().isShuttingDown &&
        (!upscalerContext || Config::Instance()->EnableHotSwapping.value_or_default()) &&
        (State::Instance().activeFgType != OptiFG || State::Instance().currentFG == nullptr ||
         State::Instance().currentFG->SwapchainContext() != (*context)))
    {
        auto cdResult = FfxApiProxy::D3D12_DestroyContext()(context, memCb);
        LOG_INFO("result: {:X}", (UINT) cdResult);
    }

    return FFX_API_RETURN_OK;
}

ffxReturnCode_t ffxConfigure_Dx12(ffxContext* context, const ffxConfigureDescHeader* desc)
{
    if (desc == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("type: {:X} (UPSCALE_KEYVALUE: 0x00010007)", desc->type);

    if (desc->type == FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE &&
        !Config::Instance()->EnableHotSwapping.value_or_default())
        return FFX_API_RETURN_OK;

    return FfxApiProxy::D3D12_Configure()(context, desc);
}

ffxReturnCode_t ffxQuery_Dx12(ffxContext* context, ffxQueryDescHeader* desc)
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

    if (context != nullptr && _contexts.contains(*context) && !Config::Instance()->EnableHotSwapping.value_or_default())
    {
        LOG_INFO("Hot swapping disabled, ignoring upscaler query");
        return FFX_API_RETURN_OK;
    }

    return FfxApiProxy::D3D12_Query()(context, desc);
}

ffxReturnCode_t ffxDispatch_Dx12(ffxContext* context, ffxDispatchDescHeader* desc)
{
    // Skip OptiScaler stuff
    if (Config::Instance()->EnableHotSwapping.value_or_default() &&
        !Config::Instance()->UseFfxInputs.value_or_default())
        return FfxApiProxy::D3D12_Dispatch()(context, desc);

    if (desc == nullptr || context == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("context: {:X}, type: {:X}", (size_t) *context, desc->type);

    if (context == nullptr || !_initParams.contains(*context))
    {
        LOG_INFO("Not in _contexts, desc type: {:X}", desc->type);
        return FfxApiProxy::D3D12_Dispatch()(context, desc);
    }

    ffxApiHeader* header = desc;
    ffxDispatchDescUpscale* dispatchDesc = nullptr;
    bool rmDesc = false;

    do
    {
        if (header->type == FFX_API_DISPATCH_DESC_TYPE_UPSCALE)
            dispatchDesc = (ffxDispatchDescUpscale*) header;
        else if (!Config::Instance()->EnableHotSwapping.value_or_default() &&
                 header->type == FFX_API_DISPATCH_DESC_TYPE_UPSCALE_GENERATEREACTIVEMASK)
            return FFX_API_RETURN_OK;

        header = header->pNext;

    } while (header != nullptr);

    if (dispatchDesc == nullptr)
    {
        LOG_INFO("dispatchDesc == nullptr, desc type: {:X}", desc->type);
        return FfxApiProxy::D3D12_Dispatch()(context, desc);
    }

    if (dispatchDesc->commandList == nullptr)
        return FfxApiProxy::D3D12_Dispatch()(context, desc);

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

    State::Instance().setInputApiName = "FFX-DX12";

    auto evalResult = NVSDK_NGX_D3D12_EvaluateFeature((ID3D12GraphicsCommandList*) dispatchDesc->commandList, handle,
                                                      params, nullptr);

    if (evalResult == NVSDK_NGX_Result_Success)
        return FFX_API_RETURN_OK;

    LOG_ERROR("evalResult: {:X}", (UINT) evalResult);
    return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
}
