#include "XeSS_Vulkan.h"

#include "NVNGX_Parameter.h"

#include <proxies/XeSS_Proxy.h>
#include "menu/menu_overlay_vk.h"

#include <nvsdk_ngx_vk.h>

static UINT64 _handleCounter = 23370000;
static UINT64 _frameCounter = 0;
static xess_context_handle_t _currentContext = nullptr;
static VkDevice _device = nullptr;
static VkInstance _instance = nullptr;
static VkPhysicalDevice _physicalDevice = nullptr;
static NVSDK_NGX_Resource_VK expNVRes[3] {};
static NVSDK_NGX_Resource_VK depthNVRes[3] {};
static NVSDK_NGX_Resource_VK biasNVRes[3] {};
static NVSDK_NGX_Resource_VK colorNVRes[3] {};
static NVSDK_NGX_Resource_VK outputNVRes[3] {};
static NVSDK_NGX_Resource_VK mvNVRes[3] {};

static bool CreateDLSSContext(xess_context_handle_t handle, VkCommandBuffer commandList,
                              const xess_vk_execute_params_t* pExecParams)
{
    LOG_DEBUG("");

    if (!_nvParams.contains(handle))
        return false;

    NVSDK_NGX_Handle* nvHandle = nullptr;
    auto params = _nvParams[handle];
    auto initParams = &_vkInitParams[handle];

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

    if (NVSDK_NGX_VULKAN_CreateFeature(commandList, NVSDK_NGX_Feature_SuperSampling, params, &nvHandle) !=
        NVSDK_NGX_Result_Success)
        return false;

    _contexts[handle] = nvHandle;

    return true;
}

static void CreateNVRes(const xess_vk_image_view_info* resource, NVSDK_NGX_Resource_VK* nvResource,
                        bool readWrite = false)
{
    (*nvResource).Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    (*nvResource).Resource.ImageViewInfo.ImageView = resource->imageView;
    (*nvResource).Resource.ImageViewInfo.Image = resource->image;
    (*nvResource).Resource.ImageViewInfo.SubresourceRange = resource->subresourceRange;
    (*nvResource).Resource.ImageViewInfo.Height = resource->height;
    (*nvResource).Resource.ImageViewInfo.Width = resource->width;
    (*nvResource).Resource.ImageViewInfo.Format = resource->format;
    (*nvResource).ReadWrite = readWrite;
}

xess_result_t hk_xessVKCreateContext(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                                     xess_context_handle_t* phContext)
{
    LOG_DEBUG("");

    if (instance == nullptr || physicalDevice == nullptr || device == nullptr)
        return XESS_RESULT_ERROR_DEVICE;

    _instance = instance;
    _physicalDevice = physicalDevice;
    _device = device;

    if (!State::Instance().NvngxVkInited)
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

        auto nvResult = NVSDK_NGX_VULKAN_Init_ProjectID_Ext(
            "OptiScaler", NVSDK_NGX_ENGINE_TYPE_CUSTOM, VER_PRODUCT_VERSION_STR, dllPath.c_str(), _instance,
            _physicalDevice, _device, vkGetInstanceProcAddr, vkGetDeviceProcAddr, State::Instance().NVNGX_Version,
            &fcInfo);

        if (nvResult != NVSDK_NGX_Result_Success)
            return XESS_RESULT_ERROR_UNINITIALIZED;
    }

    *phContext = (xess_context_handle_t) ++_handleCounter;

    NVSDK_NGX_Parameter* params = nullptr;

    if (NVSDK_NGX_VULKAN_GetCapabilityParameters(&params) != NVSDK_NGX_Result_Success)
        return XESS_RESULT_ERROR_INVALID_ARGUMENT;

    _nvParams[*phContext] = params;
    _motionScales[*phContext] = { 1.0f, 1.0f };

    LOG_DEBUG("Created context: {}", (size_t) *phContext);

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessVKBuildPipelines(xess_context_handle_t hContext, VkPipelineCache pipelineCache, bool blocking,
                                      uint32_t initFlags)
{
    LOG_DEBUG("");
    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessVKInit(xess_context_handle_t hContext, const xess_vk_init_params_t* pInitParams)
{
    LOG_DEBUG("initFlags: {:X}", pInitParams->initFlags);

    xess_vk_init_params_t ip {};
    ip.bufferHeapOffset = pInitParams->bufferHeapOffset;
    ip.creationNodeMask = pInitParams->creationNodeMask;
    ip.initFlags = pInitParams->initFlags;
    ip.outputResolution = pInitParams->outputResolution;
    ip.pipelineCache = pInitParams->pipelineCache;
    ip.qualitySetting = pInitParams->qualitySetting;
    ip.tempBufferHeap = pInitParams->tempBufferHeap;
    ip.tempTextureHeap = pInitParams->tempTextureHeap;
    ip.textureHeapOffset = pInitParams->textureHeapOffset;
    ip.visibleNodeMask = pInitParams->visibleNodeMask;

    _vkInitParams[hContext] = ip;

    if (!_contexts.contains(hContext))
        return XESS_RESULT_SUCCESS;

    NVSDK_NGX_VULKAN_ReleaseFeature(_contexts[hContext]);
    _contexts.erase(hContext);

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessVKExecute(xess_context_handle_t hContext, VkCommandBuffer commandBuffer,
                               const xess_vk_execute_params_t* pExecParams)
{
    LOG_DEBUG("");

    if (commandBuffer == nullptr)
        return XESS_RESULT_ERROR_INVALID_ARGUMENT;

    if (!_contexts.contains(hContext) && !CreateDLSSContext(hContext, commandBuffer, pExecParams))
        return XESS_RESULT_ERROR_UNKNOWN;

    NVSDK_NGX_Parameter* params = _nvParams[hContext];
    NVSDK_NGX_Handle* handle = _contexts[hContext];
    xess_vk_init_params_t* initParams = &_vkInitParams[hContext];

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

    _frameCounter++;
    auto index = _frameCounter % 3;

    // generate new ones with nvidia resource infos
    if (pExecParams->depthTexture.image == nullptr)
    {
        LOG_ERROR("Depth error!");
        return XESS_RESULT_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        CreateNVRes(&pExecParams->depthTexture, &depthNVRes[index]);
        params->Set(NVSDK_NGX_Parameter_Depth, &depthNVRes[index]);
    }

    if (pExecParams->exposureScaleTexture.image != nullptr)
    {
        CreateNVRes(&pExecParams->exposureScaleTexture, &expNVRes[index]);
        params->Set(NVSDK_NGX_Parameter_ExposureTexture, &expNVRes[index]);
    }

    if (pExecParams->responsivePixelMaskTexture.image != nullptr)
    {
        CreateNVRes(&pExecParams->responsivePixelMaskTexture, &biasNVRes[index]);

        if (feature_version { XeSSProxy::Version().major, XeSSProxy::Version().minor, XeSSProxy::Version().patch } <
            feature_version { 2, 0, 1 })
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &biasNVRes[index]);
        else
            params->Set("FSR.reactive", &biasNVRes[index]);
    }

    if (pExecParams->colorTexture.image == nullptr)
    {
        LOG_ERROR("Color error!");
        return XESS_RESULT_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        CreateNVRes(&pExecParams->colorTexture, &colorNVRes[index]);
        params->Set(NVSDK_NGX_Parameter_Color, &colorNVRes[index]);
    }

    if (pExecParams->velocityTexture.image == nullptr)
    {
        LOG_ERROR("Motion Vectors error!");
        return XESS_RESULT_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        CreateNVRes(&pExecParams->velocityTexture, &mvNVRes[index]);
        params->Set(NVSDK_NGX_Parameter_MotionVectors, &mvNVRes[index]);
    }

    if (pExecParams->outputTexture.image == nullptr)
    {
        LOG_ERROR("Output error!");
        return XESS_RESULT_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        CreateNVRes(&pExecParams->outputTexture, &outputNVRes[index], true);
        params->Set(NVSDK_NGX_Parameter_Output, &outputNVRes[index]);
    }

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

    if (NVSDK_NGX_VULKAN_EvaluateFeature(commandBuffer, handle, params, nullptr) == NVSDK_NGX_Result_Success)
        return XESS_RESULT_SUCCESS;

    return XESS_RESULT_ERROR_UNKNOWN;
}

xess_result_t hk_xessVKGetInitParams(xess_context_handle_t hContext, xess_vk_init_params_t* pInitParams)
{
    LOG_DEBUG("");

    if (!_vkInitParams.contains(hContext))
        return XESS_RESULT_ERROR_INVALID_CONTEXT;

    auto ip = &_vkInitParams[hContext];

    pInitParams->bufferHeapOffset = ip->bufferHeapOffset;
    pInitParams->creationNodeMask = ip->creationNodeMask;
    pInitParams->initFlags = ip->initFlags;
    pInitParams->outputResolution = ip->outputResolution;
    pInitParams->tempBufferHeap = ip->tempBufferHeap;
    pInitParams->tempTextureHeap = ip->tempTextureHeap;
    pInitParams->pipelineCache = ip->pipelineCache;
    pInitParams->qualitySetting = ip->qualitySetting;
    pInitParams->textureHeapOffset = ip->textureHeapOffset;
    pInitParams->visibleNodeMask = ip->visibleNodeMask;

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessVKGetResourcesToDump(xess_context_handle_t hContext,
                                          xess_vk_resources_to_dump_t** pResourcesToDump)
{
    LOG_DEBUG("");
    return XESS_RESULT_SUCCESS;
}
