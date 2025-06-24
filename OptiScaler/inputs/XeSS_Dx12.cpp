#include "XeSS_Dx12.h"

#include "NVNGX_Parameter.h"

#include <proxies/XeSS_Proxy.h>
#include "menu/menu_overlay_dx.h"

static UINT64 _handleCounter = 13370000;
static UINT64 _frameCounter = 0;
static xess_context_handle_t _currentContext = nullptr;
static ID3D12Device* _d3d12Device = nullptr;

static bool CreateDLSSContext(xess_context_handle_t handle, ID3D12GraphicsCommandList* commandList,
                              const xess_d3d12_execute_params_t* pExecParams)
{
    LOG_DEBUG("");

    if (!_nvParams.contains(handle))
        return false;

    NVSDK_NGX_Handle* nvHandle = nullptr;
    auto params = _nvParams[handle];
    auto initParams = &_d3d12InitParams[handle];

    UINT initFlags = 0;

    if ((initParams->initFlags & XESS_INIT_FLAG_LDR_INPUT_COLOR) == 0)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;

    if (initParams->initFlags & XESS_INIT_FLAG_INVERTED_DEPTH)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;

    if (initParams->initFlags & XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

    if (initParams->initFlags & XESS_INIT_FLAG_JITTERED_MV)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;

    if ((initParams->initFlags & XESS_INIT_FLAG_HIGH_RES_MV) == 0)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

    params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, initFlags);

    params->Set(NVSDK_NGX_Parameter_Width, pExecParams->inputWidth);
    params->Set(NVSDK_NGX_Parameter_Height, pExecParams->inputHeight);
    params->Set(NVSDK_NGX_Parameter_OutWidth, initParams->outputResolution.x);
    params->Set(NVSDK_NGX_Parameter_OutHeight, initParams->outputResolution.y);

    switch (initParams->qualitySetting)
    {
    case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_UltraPerformance);
        break;

    case XESS_QUALITY_SETTING_PERFORMANCE:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_MaxQuality);
        break;

    case XESS_QUALITY_SETTING_BALANCED:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_Balanced);
        break;

    case XESS_QUALITY_SETTING_QUALITY:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_MaxQuality);
        break;

    case XESS_QUALITY_SETTING_ULTRA_QUALITY:
    case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_UltraQuality);
        break;

    case XESS_QUALITY_SETTING_AA:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_DLAA);
        break;

    default:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_Balanced);
        break;
    }

    if (NVSDK_NGX_D3D12_CreateFeature(commandList, NVSDK_NGX_Feature_SuperSampling, params, &nvHandle) !=
        NVSDK_NGX_Result_Success)
        return false;

    _contexts[handle] = nvHandle;

    return true;
}

xess_result_t hk_xessD3D12CreateContext(ID3D12Device* pDevice, xess_context_handle_t* phContext)
{
    LOG_DEBUG("");

    if (pDevice == nullptr)
        return XESS_RESULT_ERROR_DEVICE;

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
            pathStorage.push_back(nvngxDlssPath.value().wstring());

        if (nvngxDlssDPath.has_value())
            pathStorage.push_back(nvngxDlssDPath.value().wstring());

        if (nvngxDlssGPath.has_value())
            pathStorage.push_back(nvngxDlssGPath.value().wstring());

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
            "OptiScaler", NVSDK_NGX_ENGINE_TYPE_CUSTOM, VER_PRODUCT_VERSION_STR, dllPath.c_str(), pDevice, &fcInfo,
            State::Instance().NVNGX_Version == 0 ? NVSDK_NGX_Version_API : State::Instance().NVNGX_Version);

        if (nvResult != NVSDK_NGX_Result_Success)
            return XESS_RESULT_ERROR_UNINITIALIZED;
    }

    _d3d12Device = pDevice;
    *phContext = (xess_context_handle_t) ++_handleCounter;

    NVSDK_NGX_Parameter* params = nullptr;

    if (NVSDK_NGX_D3D12_GetCapabilityParameters(&params) != NVSDK_NGX_Result_Success)
        return XESS_RESULT_ERROR_INVALID_ARGUMENT;

    _nvParams[*phContext] = params;
    _motionScales[*phContext] = { 1.0, 1.0 };

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessD3D12BuildPipelines(xess_context_handle_t hContext, ID3D12PipelineLibrary* pPipelineLibrary,
                                         bool blocking, uint32_t initFlags)
{
    LOG_WARN("");
    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessD3D12Init(xess_context_handle_t hContext, const xess_d3d12_init_params_t* pInitParams)
{
    LOG_DEBUG("");

    xess_d3d12_init_params_t ip {};
    ip.bufferHeapOffset = pInitParams->bufferHeapOffset;
    ip.creationNodeMask = pInitParams->creationNodeMask;
    ip.initFlags = pInitParams->initFlags;
    ip.outputResolution = pInitParams->outputResolution;
    ip.pPipelineLibrary = pInitParams->pPipelineLibrary;
    ip.pTempBufferHeap = pInitParams->pTempBufferHeap;
    ip.pTempTextureHeap = pInitParams->pTempTextureHeap;
    ip.qualitySetting = pInitParams->qualitySetting;
    ip.textureHeapOffset = pInitParams->textureHeapOffset;
    ip.visibleNodeMask = pInitParams->visibleNodeMask;

    _d3d12InitParams[hContext] = ip;

    if (!_contexts.contains(hContext))
        return XESS_RESULT_SUCCESS;

    NVSDK_NGX_D3D12_ReleaseFeature(_contexts[hContext]);
    _contexts.erase(hContext);

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessD3D12Execute(xess_context_handle_t hContext, ID3D12GraphicsCommandList* pCommandList,
                                  const xess_d3d12_execute_params_t* pExecParams)
{
    LOG_DEBUG("");

    if (pCommandList == nullptr)
        return XESS_RESULT_ERROR_INVALID_ARGUMENT;

    if (!_contexts.contains(hContext) && !CreateDLSSContext(hContext, pCommandList, pExecParams))
        return XESS_RESULT_ERROR_UNKNOWN;

    NVSDK_NGX_Parameter* params = _nvParams[hContext];
    NVSDK_NGX_Handle* handle = _contexts[hContext];
    xess_d3d12_init_params_t* initParams = &_d3d12InitParams[hContext];

    if (_motionScales.contains(hContext))
    {
        auto scales = &_motionScales[hContext];

        if ((initParams->initFlags & XESS_INIT_FLAG_USE_NDC_VELOCITY))
        {
            if (initParams->initFlags & XESS_INIT_FLAG_HIGH_RES_MV)
            {
                params->Set(NVSDK_NGX_Parameter_MV_Scale_X, initParams->outputResolution.x * 0.5 * scales->x);
                params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, initParams->outputResolution.y * -0.5 * scales->y);
            }
            else
            {
                params->Set(NVSDK_NGX_Parameter_MV_Scale_X, pExecParams->inputWidth * 0.5 * scales->x);
                params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, pExecParams->inputHeight * -0.5 * scales->y);
            }
        }
        else
        {
            params->Set(NVSDK_NGX_Parameter_MV_Scale_X, scales->x);
            params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, scales->y);
        }
    }

    float jitterScaleX = 1.0f;
    float jitterScaleY = 1.0f;

    if (_jitterScales.contains(hContext))
    {
        auto scales = &_jitterScales[hContext];
        jitterScaleX = scales->x;
        jitterScaleY = scales->y;
    }

    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, pExecParams->jitterOffsetX * jitterScaleX);
    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, pExecParams->jitterOffsetY * jitterScaleY);
    params->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, pExecParams->exposureScale);
    params->Set(NVSDK_NGX_Parameter_Reset, pExecParams->resetHistory);
    params->Set(NVSDK_NGX_Parameter_Width, pExecParams->inputWidth);
    params->Set(NVSDK_NGX_Parameter_Height, pExecParams->inputHeight);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, pExecParams->inputWidth);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, pExecParams->inputHeight);
    params->Set(NVSDK_NGX_Parameter_Depth, pExecParams->pDepthTexture);
    params->Set(NVSDK_NGX_Parameter_ExposureTexture, pExecParams->pExposureScaleTexture);

    if (feature_version { XeSSProxy::Version().major, XeSSProxy::Version().minor, XeSSProxy::Version().patch } <
        feature_version { 2, 0, 1 })
        params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, pExecParams->pResponsivePixelMaskTexture);
    else
        params->Set("FSR.reactive", pExecParams->pResponsivePixelMaskTexture);

    params->Set(NVSDK_NGX_Parameter_Color, pExecParams->pColorTexture);
    params->Set(NVSDK_NGX_Parameter_MotionVectors, pExecParams->pVelocityTexture);
    params->Set(NVSDK_NGX_Parameter_Output, pExecParams->pOutputTexture);

    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, pExecParams->inputColorBase.x);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, pExecParams->inputColorBase.y);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, pExecParams->inputDepthBase.x);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, pExecParams->inputDepthBase.y);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, pExecParams->inputMotionVectorBase.x);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, pExecParams->inputMotionVectorBase.y);
    params->Set(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, pExecParams->outputColorBase.x);
    params->Set(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y, pExecParams->outputColorBase.y);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_X,
                pExecParams->inputResponsiveMaskBase.x);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_Y,
                pExecParams->inputResponsiveMaskBase.y);

    State::Instance().setInputApiName = "XeSS";

    if (NVSDK_NGX_D3D12_EvaluateFeature(pCommandList, handle, params, nullptr) == NVSDK_NGX_Result_Success)
        return XESS_RESULT_SUCCESS;

    return XESS_RESULT_ERROR_UNKNOWN;
}

xess_result_t hk_xessD3D12GetInitParams(xess_context_handle_t hContext, xess_d3d12_init_params_t* pInitParams)
{
    LOG_DEBUG("");

    if (!_d3d12InitParams.contains(hContext))
        return XESS_RESULT_ERROR_INVALID_CONTEXT;

    auto ip = &_d3d12InitParams[hContext];

    pInitParams->bufferHeapOffset = ip->bufferHeapOffset;
    pInitParams->creationNodeMask = ip->creationNodeMask;
    pInitParams->initFlags = ip->initFlags;
    pInitParams->outputResolution = ip->outputResolution;
    pInitParams->pPipelineLibrary = ip->pPipelineLibrary;
    pInitParams->pTempBufferHeap = ip->pTempBufferHeap;
    pInitParams->pTempTextureHeap = ip->pTempTextureHeap;
    pInitParams->qualitySetting = ip->qualitySetting;
    pInitParams->textureHeapOffset = ip->textureHeapOffset;
    pInitParams->visibleNodeMask = ip->visibleNodeMask;

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessD3D12GetResourcesToDump(xess_context_handle_t hContext,
                                             xess_resources_to_dump_t** pResourcesToDump)
{
    LOG_DEBUG("");
    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessD3D12GetProfilingData(xess_context_handle_t hContext, xess_profiling_data_t** pProfilingData)
{
    LOG_DEBUG("");
    return XESS_RESULT_SUCCESS;
}
