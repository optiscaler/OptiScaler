#include "pch.h"
#include "FSR2_Vk.h"

#include "Util.h"
#include "Config.h"
#include "resource.h"
#include "NVNGX_Parameter.h"

#include <proxies/KernelBase_Proxy.h>

#include "scanner/scanner.h"
#include "detours/detours.h"

#include "fsr2/ffx_fsr2.h"
#include "fsr2/vk/ffx_fsr2_vk.h"

#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>

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
typedef size_t (*PFN_ffxFsr2GetScratchMemorySizeVK)(VkPhysicalDevice physicalDevice);
typedef FfxDevice (*PFN_ffxGetDeviceVK)(VkDevice device);

// Extras
typedef int32_t (*PFN_ffxFsr2GetJitterPhaseCount)(int32_t renderWidth, int32_t displayWidth);
typedef FfxErrorCode (*PFN_ffxFsr2ContextGenerateReactiveMask)(FfxFsr2Context* context,
                                                               const FfxFsr2GenerateReactiveDescription* params);

static PFN_ffxFsr2ContextCreate o_ffxFsr2ContextCreate_Vk = nullptr;
static PFN_ffxFsr2ContextDispatch o_ffxFsr2ContextDispatch_Vk = nullptr;
static PFN_ffxFsr2ContextDestroy o_ffxFsr2ContextDestroy_Vk = nullptr;
static PFN_ffxFsr2GetUpscaleRatioFromQualityMode o_ffxFsr2GetUpscaleRatioFromQualityMode_Vk = nullptr;
static PFN_ffxFsr2GetRenderResolutionFromQualityMode o_ffxFsr2GetRenderResolutionFromQualityMode_Vk = nullptr;
static PFN_ffxFsr2GetJitterPhaseCount o_ffxFsr2GetJitterPhaseCount_Vk = nullptr;
static PFN_ffxFsr2GetScratchMemorySizeVK o_ffxFsr2GetScratchMemorySize_Vk = nullptr;
static PFN_ffxGetDeviceVK o_ffxGetDevice_Vk = nullptr;

static std::unordered_map<FfxFsr2Context*, FfxFsr2ContextDescription> _initParams;
static std::unordered_map<FfxFsr2Context*, NVSDK_NGX_Parameter*> _nvParams;
static std::unordered_map<FfxFsr2Context*, NVSDK_NGX_Handle*> _contexts;
static VkDevice _vkDevice = nullptr;
static VkPhysicalDevice _vkPhysicalDevice = nullptr;
static PFN_vkGetDeviceProcAddr _vkDeviceProcAddress = nullptr;
static bool _nvnxgInited = false;
static bool _skipCreate = false;
static bool _skipDispatch = false;
static bool _skipDestroy = false;
static float qualityRatios[] = { 1.0f, 1.5f, 1.7f, 2.0f, 3.0f };

static VkImageView depthImageView = nullptr;
static VkImageView expImageView = nullptr;
static VkImageView biasImageView = nullptr;
static VkImageView colorImageView = nullptr;
static VkImageView mvImageView = nullptr;
static VkImageView outputImageView = nullptr;
static VkImageView fsrReactiveView = nullptr;
static VkImageView fsrTransparencyView = nullptr;
static NVSDK_NGX_Resource_VK depthNVRes {};
static NVSDK_NGX_Resource_VK expNVRes {};
static NVSDK_NGX_Resource_VK biasNVRes {};
static NVSDK_NGX_Resource_VK colorNVRes {};
static NVSDK_NGX_Resource_VK mvNVRes {};
static NVSDK_NGX_Resource_VK outputNVRes {};
static NVSDK_NGX_Resource_VK fsrReactiveNVRes {};
static NVSDK_NGX_Resource_VK fsrTransparencyNVRes {};

static VkFormat ffxApiGetVkFormat(uint32_t fmt)
{

    switch (fmt)
    {
    // 32-bit float formats
    case FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case FFX_SURFACE_FORMAT_R32G32_FLOAT:
        return VK_FORMAT_R32G32_SFLOAT;
    case FFX_SURFACE_FORMAT_R32_FLOAT:
        return VK_FORMAT_R32_SFLOAT;

    // 32-bit integer formats
    case FFX_SURFACE_FORMAT_R32_UINT:
        return VK_FORMAT_R32_UINT;
    case FFX_SURFACE_FORMAT_R8G8B8A8_UNORM:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case FFX_SURFACE_FORMAT_R11G11B10_FLOAT:
        return VK_FORMAT_B10G11R11_UFLOAT_PACK32;

    // 16-bit float formats
    case FFX_SURFACE_FORMAT_R16G16_FLOAT:
        return VK_FORMAT_R16G16_SFLOAT;
    case FFX_SURFACE_FORMAT_R16G16_UINT:
        return VK_FORMAT_R16G16_UINT;
    case FFX_SURFACE_FORMAT_R16_FLOAT:
        return VK_FORMAT_R16_SFLOAT;

    // 16-bit integer formats
    case FFX_SURFACE_FORMAT_R16_UINT:
        return VK_FORMAT_R16_UINT;

    // 16-bit normalized formats
    case FFX_SURFACE_FORMAT_R16G16B16A16_UNORM:
        return VK_FORMAT_R16G16B16A16_UNORM;
    case FFX_SURFACE_FORMAT_R16_UNORM:
        return VK_FORMAT_R16_UNORM;
    case FFX_SURFACE_FORMAT_R16_SNORM:
        return VK_FORMAT_R16_SNORM;

    // 8-bit formats
    case FFX_SURFACE_FORMAT_R8G8_UNORM:
        return VK_FORMAT_R8G8_UNORM;
    case FFX_SURFACE_FORMAT_R8_UNORM:
        return VK_FORMAT_R8_UNORM;
    case FFX_SURFACE_FORMAT_R8_UINT:
        return VK_FORMAT_R8_UINT;

    default:
        LOG_WARN("Unknown FFX surface format: {}", fmt);
        return VK_FORMAT_UNDEFINED;
    }
}

static bool CreateIVandNVRes(FfxResource resource, VkImageView* imageView, NVSDK_NGX_Resource_VK* nvResource,
                             bool isUAV = false, bool isDepth = false)
{
    auto sf = ffxApiGetVkFormat(resource.description.format);

    // This is for WWZ depth issue
    if (sf == VK_FORMAT_UNDEFINED && State::Instance().gameQuirks & GameQuirk::ForceDepthD32S8)
        sf = VK_FORMAT_D32_SFLOAT;

    VkImageViewCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = (VkImage) resource.resource;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = sf;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (resource.isDepth || isDepth)
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    auto result = vkCreateImageView(_vkDevice, &createInfo, nullptr, imageView);

    if (result != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateImageView error!: {:X}", (int) result);
        return false;
    }

    (*nvResource).Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    (*nvResource).Resource.ImageViewInfo.ImageView = *imageView;
    (*nvResource).Resource.ImageViewInfo.Image = createInfo.image;
    (*nvResource).Resource.ImageViewInfo.SubresourceRange = createInfo.subresourceRange;
    (*nvResource).Resource.ImageViewInfo.Height = resource.description.height;
    (*nvResource).Resource.ImageViewInfo.Width = resource.description.width;
    (*nvResource).Resource.ImageViewInfo.Format = sf;
    (*nvResource).ReadWrite = isUAV;

    return true;
}

static bool CreateDLSSContext(FfxFsr2Context* handle, const FfxFsr2DispatchDescription* pExecParams)
{
    LOG_DEBUG("context: {:X}", (size_t) handle);

    if (!_nvParams.contains(handle))
        return false;

    NVSDK_NGX_Handle* nvHandle = nullptr;
    auto params = _nvParams[handle];
    auto initParams = &_initParams[handle];
    auto commandList = (VkCommandBuffer) pExecParams->commandList;

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

    auto width = pExecParams->output.description.width > 0 ? pExecParams->output.description.width
                                                           : initParams->displaySize.width;

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

    auto nvngxResult = NVSDK_NGX_VULKAN_CreateFeature(commandList, NVSDK_NGX_Feature_SuperSampling, params, &nvHandle);
    if (nvngxResult != NVSDK_NGX_Result_Success)
    {
        LOG_ERROR("NVSDK_NGX_VULKAN_CreateFeature error: {:X}", (UINT) nvngxResult);
        return false;
    }

    _contexts[handle] = nvHandle;
    LOG_INFO("context created: {:X}", (size_t) handle);

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
static FfxErrorCode ffxFsr2ContextCreate_Vk(FfxFsr2Context* context, FfxFsr2ContextDescription* contextDescription)
{
    LOG_DEBUG("");

    if (contextDescription == nullptr || contextDescription->device == nullptr)
        return FFX_ERROR_INVALID_ARGUMENT;

    auto& state = State::Instance();

    _skipCreate = true;

    FfxErrorCode ccResult = FFX_OK;
    {
        ScopedSkipHeapCapture skipHeapCapture {};

        ccResult = o_ffxFsr2ContextCreate_Vk(context, contextDescription);
        _skipCreate = false;

        if (ccResult != FFX_OK)
        {
            LOG_ERROR("ccResult: {:X}", (UINT) ccResult);
            return ccResult;
        }
    }

    if (contextDescription->device == VK_NULL_HANDLE)
        return ccResult;

    if (_vkDevice == VK_NULL_HANDLE)
    {
        _vkDevice = (VkDevice) contextDescription->device;
        LOG_DEBUG("VkDevice: {:X}", (size_t) _vkDevice);
    }

    if (!state.nvngxVkInited)
    {
        NVSDK_NGX_FeatureCommonInfo fcInfo {};
        auto exePath = Util::ExePath().remove_filename();

        auto nvResult = NVSDK_NGX_VULKAN_Init_ProjectID_Ext(
            OPTI_GUID, state.NVNGX_Engine, OPTI_VERSION, exePath.c_str(), State::Instance().VulkanInstance,
            _vkPhysicalDevice, _vkDevice, vkGetInstanceProcAddr, vkGetDeviceProcAddr,
            state.NVNGX_Version == 0 ? NVSDK_NGX_Version_API : state.NVNGX_Version, &fcInfo);

        if (nvResult != NVSDK_NGX_Result_Success)
            return FFX_ERROR_BACKEND_API_ERROR;

        _nvnxgInited = true;
    }

    NVSDK_NGX_Parameter* params = nullptr;

    if (NVSDK_NGX_VULKAN_GetCapabilityParameters(&params) != NVSDK_NGX_Result_Success)
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
static FfxErrorCode ffxFsr2ContextDispatch_Vk(FfxFsr2Context* context,
                                              const FfxFsr2DispatchDescription* dispatchDescription)
{
    LOG_DEBUG("");

    // Skip OptiScaler stuff
    if (!Config::Instance()->UseFsr2Inputs.value_or_default())
    {
        _skipDispatch = true;
        LOG_DEBUG("UseFsr2Inputs not enabled, skipping");
        auto result = o_ffxFsr2ContextDispatch_Vk(context, dispatchDescription);
        _skipDispatch = false;
        return result;
    }

    if (dispatchDescription == nullptr || context == nullptr || dispatchDescription->commandList == nullptr)
        return FFX_ERROR_INVALID_ARGUMENT;

    // If not in contexts list create and add context
    if (!_contexts.contains(context) && _initParams.contains(context) &&
        !CreateDLSSContext(context, dispatchDescription))
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

    // Clear last frames image views
    LOG_DEBUG("Clear last frames image views");
    if (depthImageView != nullptr)
        vkDestroyImageView(_vkDevice, depthImageView, nullptr);

    if (expImageView != nullptr)
        vkDestroyImageView(_vkDevice, expImageView, nullptr);

    if (biasImageView != nullptr)
        vkDestroyImageView(_vkDevice, biasImageView, nullptr);

    if (colorImageView != nullptr)
        vkDestroyImageView(_vkDevice, colorImageView, nullptr);

    if (mvImageView != nullptr)
        vkDestroyImageView(_vkDevice, mvImageView, nullptr);

    if (outputImageView != nullptr)
        vkDestroyImageView(_vkDevice, outputImageView, nullptr);

    if (fsrReactiveView != nullptr)
        vkDestroyImageView(_vkDevice, fsrReactiveView, nullptr);

    if (fsrTransparencyView != nullptr)
        vkDestroyImageView(_vkDevice, fsrTransparencyView, nullptr);

    // generate new ones with nvidia resource infos
    if (dispatchDescription->depth.resource == nullptr ||
        !CreateIVandNVRes(dispatchDescription->depth, &depthImageView, &depthNVRes, false, true))
    {
        LOG_ERROR("Depth error!");
        return FFX_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        params->Set(NVSDK_NGX_Parameter_Depth, &depthNVRes);
    }

    if (dispatchDescription->exposure.resource != nullptr &&
        CreateIVandNVRes(dispatchDescription->exposure, &expImageView, &expNVRes))
    {
        params->Set(NVSDK_NGX_Parameter_ExposureTexture, &expNVRes);
    }

    if (dispatchDescription->reactive.resource != nullptr &&
        CreateIVandNVRes(dispatchDescription->reactive, &biasImageView, &biasNVRes))
    {
        params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &biasNVRes);
    }

    if (dispatchDescription->color.resource == nullptr ||
        !CreateIVandNVRes(dispatchDescription->color, &colorImageView, &colorNVRes))
    {
        LOG_ERROR("Color error!");
        return FFX_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        params->Set(NVSDK_NGX_Parameter_Color, &colorNVRes);
    }

    if (dispatchDescription->motionVectors.resource == nullptr ||
        !CreateIVandNVRes(dispatchDescription->motionVectors, &mvImageView, &mvNVRes))
    {
        LOG_ERROR("Motion Vectors error!");
        return FFX_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        params->Set(NVSDK_NGX_Parameter_MotionVectors, &mvNVRes);
    }

    if (dispatchDescription->output.resource == nullptr ||
        !CreateIVandNVRes(dispatchDescription->output, &outputImageView, &outputNVRes, true))
    {
        LOG_ERROR("Output error!");
        return FFX_ERROR_INVALID_ARGUMENT;
    }
    else
    {
        params->Set(NVSDK_NGX_Parameter_Output, &outputNVRes);
    }

    if (dispatchDescription->transparencyAndComposition.resource != nullptr &&
        CreateIVandNVRes(dispatchDescription->transparencyAndComposition, &fsrTransparencyView, &fsrTransparencyNVRes))
    {
        params->Set("FSR.transparencyAndComposition", &fsrTransparencyNVRes);
    }

    if (dispatchDescription->reactive.resource != nullptr &&
        CreateIVandNVRes(dispatchDescription->reactive, &fsrReactiveView, &fsrReactiveNVRes))
    {
        params->Set("FSR.reactive", &fsrReactiveNVRes);
    }

    params->Set("FSR.cameraNear", dispatchDescription->cameraNear);
    params->Set("FSR.cameraFar", dispatchDescription->cameraFar);
    params->Set("FSR.cameraFovAngleVertical", dispatchDescription->cameraFovAngleVertical);
    params->Set("FSR.frameTimeDelta", dispatchDescription->frameTimeDelta);
    params->Set("FSR.transparencyAndComposition", dispatchDescription->transparencyAndComposition.resource);
    params->Set("FSR.reactive", dispatchDescription->reactive.resource);
    params->Set(NVSDK_NGX_Parameter_Sharpness, dispatchDescription->sharpness);

    LOG_DEBUG("handle: {:X}, internalResolution: {}x{}", handle->Id, dispatchDescription->renderSize.width,
              dispatchDescription->renderSize.height);

    State::Instance().setInputApiName = ApiUpscalerInput::FSR2X_VK;

    auto evalResult =
        NVSDK_NGX_VULKAN_EvaluateFeature((VkCommandBuffer) dispatchDescription->commandList, handle, params, nullptr);

    if (evalResult == NVSDK_NGX_Result_Success)
        return FFX_OK;

    LOG_ERROR("evalResult: {:X}", (UINT) evalResult);
    return FFX_ERROR_BACKEND_API_ERROR;
}

static FfxErrorCode ffxFsr2ContextDestroy_Vk(FfxFsr2Context* context)
{
    LOG_DEBUG("");

    if (context == nullptr)
        return FFX_ERROR_INVALID_ARGUMENT;

    if (_contexts.contains(context))
        NVSDK_NGX_VULKAN_ReleaseFeature(_contexts[context]);

    _contexts.erase(context);
    _nvParams.erase(context);
    _initParams.erase(context);

    _skipDestroy = true;
    auto cdResult = o_ffxFsr2ContextDestroy_Vk(context);
    _skipDestroy = false;

    LOG_INFO("result: {:X}", (UINT) cdResult);

    return FFX_OK;
}

static int32_t ffxFsr2GetJitterPhaseCount_Vk(int32_t renderWidth, int32_t displayWidth)
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

static float ffxFsr2GetUpscaleRatioFromQualityMode_Vk(FfxFsr2QualityMode qualityMode)
{
    LOG_DEBUG("");

    auto ratio = GetQualityOverrideRatioFfx(qualityMode).value_or(qualityRatios[(UINT) qualityMode]);
    LOG_DEBUG("Quality mode: {}, Upscale ratio: {}", (UINT) qualityMode, ratio);
    return ratio;
}

static FfxErrorCode ffxFsr2GetRenderResolutionFromQualityMode_Vk(uint32_t* renderWidth, uint32_t* renderHeight,
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

static size_t ffxFsr2GetScratchMemorySize_Vk(VkPhysicalDevice physicalDevice)
{
    _vkPhysicalDevice = physicalDevice;
    LOG_DEBUG("VkPhysicalDevice: {:X}", (size_t) physicalDevice);
    return o_ffxFsr2GetScratchMemorySize_Vk(physicalDevice);
}

static FfxDevice ffxGetDevice_Vk(VkDevice device)
{
    LOG_DEBUG("VkDevice: {:X}", (size_t) device);
    _vkDevice = device;
    return o_ffxGetDevice_Vk(device);
}

void HookFSR2VkExeInputs()
{
    LOG_INFO("Trying to hook FSR2 Vk methods");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // ffxFsr2ContextCreate
    if (o_ffxFsr2ContextCreate_Vk == nullptr)
    {
        o_ffxFsr2ContextCreate_Vk =
            (PFN_ffxFsr2ContextCreate) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxFsr2ContextCreate");

        if (o_ffxFsr2ContextCreate_Vk != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2ContextCreate_Vk, ffxFsr2ContextCreate_Vk);

        LOG_DEBUG("ffxFsr2ContextCreate_Vk: {:X}", (size_t) o_ffxFsr2ContextCreate_Vk);
    }

    // ffxFsr2ContextDispatch 2.X
    if (o_ffxFsr2ContextDispatch_Vk == nullptr)
    {
        o_ffxFsr2ContextDispatch_Vk =
            (PFN_ffxFsr2ContextDispatch) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxFsr2ContextDispatch");

        if (o_ffxFsr2ContextDispatch_Vk != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2ContextDispatch_Vk, ffxFsr2ContextDispatch_Vk);

        LOG_DEBUG("ffxFsr2ContextDispatch_Vk: {:X}", (size_t) o_ffxFsr2ContextDispatch_Vk);
    }

    // ffxFsr2ContextDestroy
    if (o_ffxFsr2ContextDestroy_Vk == nullptr)
    {
        o_ffxFsr2ContextDestroy_Vk =
            (PFN_ffxFsr2ContextDestroy) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxFsr2ContextDestroy");

        if (o_ffxFsr2ContextDestroy_Vk != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2ContextDestroy_Vk, ffxFsr2ContextDestroy_Vk);

        LOG_DEBUG("ffxFsr2ContextDestroy_Vk: {:X}", (size_t) o_ffxFsr2ContextDestroy_Vk);
    }

    // ffxFsr2GetUpscaleRatioFromQualityMode
    if (o_ffxFsr2GetUpscaleRatioFromQualityMode_Vk == nullptr)
    {
        o_ffxFsr2GetUpscaleRatioFromQualityMode_Vk =
            (PFN_ffxFsr2GetUpscaleRatioFromQualityMode) KernelBaseProxy::GetProcAddress_()(
                exeModule, "ffxFsr2GetUpscaleRatioFromQualityMode");

        if (o_ffxFsr2GetUpscaleRatioFromQualityMode_Vk != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2GetUpscaleRatioFromQualityMode_Vk,
                         ffxFsr2GetUpscaleRatioFromQualityMode_Vk);

        LOG_DEBUG("ffxFsr2GetUpscaleRatioFromQualityMode_Vk: {:X}",
                  (size_t) o_ffxFsr2GetUpscaleRatioFromQualityMode_Vk);
    }

    // ffxFsr2GetRenderResolutionFromQualityMode
    if (o_ffxFsr2GetRenderResolutionFromQualityMode_Vk == nullptr)
    {
        o_ffxFsr2GetRenderResolutionFromQualityMode_Vk =
            (PFN_ffxFsr2GetRenderResolutionFromQualityMode) KernelBaseProxy::GetProcAddress_()(
                exeModule, "ffxFsr2GetRenderResolutionFromQualityMode");

        if (o_ffxFsr2GetRenderResolutionFromQualityMode_Vk != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2GetRenderResolutionFromQualityMode_Vk,
                         ffxFsr2GetRenderResolutionFromQualityMode_Vk);

        LOG_DEBUG("ffxFsr2GetRenderResolutionFromQualityMode_Vk: {:X}",
                  (size_t) o_ffxFsr2GetRenderResolutionFromQualityMode_Vk);
    }

    // ffxFsr2GetJitterPhaseCount
    if (o_ffxFsr2GetJitterPhaseCount_Vk == nullptr)
    {
        o_ffxFsr2GetJitterPhaseCount_Vk = (PFN_ffxFsr2GetJitterPhaseCount) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxFsr2GetJitterPhaseCount");

        if (o_ffxFsr2GetJitterPhaseCount_Vk != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2GetJitterPhaseCount_Vk, ffxFsr2GetJitterPhaseCount_Vk);

        LOG_DEBUG("o_ffxFsr2GetJitterPhaseCount_Vk: {:X}", (size_t) o_ffxFsr2GetJitterPhaseCount_Vk);
    }

    // ffxFsr2GetScratchMemorySizeVK
    if (o_ffxFsr2GetScratchMemorySize_Vk == nullptr)
    {
        o_ffxFsr2GetScratchMemorySize_Vk = (PFN_ffxFsr2GetScratchMemorySizeVK) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxFsr2GetScratchMemorySizeVK");

        if (o_ffxFsr2GetScratchMemorySize_Vk != nullptr)
            DetourAttach(&(PVOID&) o_ffxFsr2GetScratchMemorySize_Vk, ffxFsr2GetScratchMemorySize_Vk);

        LOG_DEBUG("o_ffxFsr2GetScratchMemorySize_Vk: {:X}", (size_t) o_ffxFsr2GetScratchMemorySize_Vk);
    }

    // ffxGetDeviceVK
    if (o_ffxGetDevice_Vk == nullptr)
    {
        o_ffxGetDevice_Vk = (PFN_ffxGetDeviceVK) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxGetDeviceVK");

        if (o_ffxGetDevice_Vk != nullptr)
            DetourAttach(&(PVOID&) o_ffxGetDevice_Vk, ffxGetDevice_Vk);

        LOG_DEBUG("o_ffxGetDevice_Vk: {:X}", (size_t) o_ffxGetDevice_Vk);
    }

    State::Instance().fsrHooks = o_ffxFsr2ContextCreate_Vk != nullptr;

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to attach FSR2 Vk hooks: {:X}", detourResult);
        o_ffxFsr2ContextCreate_Vk = nullptr;
        o_ffxFsr2ContextDispatch_Vk = nullptr;
        o_ffxFsr2ContextDestroy_Vk = nullptr;
        o_ffxFsr2GetUpscaleRatioFromQualityMode_Vk = nullptr;
        o_ffxFsr2GetRenderResolutionFromQualityMode_Vk = nullptr;
        o_ffxFsr2GetJitterPhaseCount_Vk = nullptr;
        o_ffxFsr2GetScratchMemorySize_Vk = nullptr;
        o_ffxGetDevice_Vk = nullptr;
    }
}
