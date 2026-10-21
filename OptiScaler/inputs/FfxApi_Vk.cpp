#include "pch.h"
#include "FfxApi_Vk.h"

#include <Util.h>
#include <Config.h>
#include <resource.h>
#include <NVNGX_Parameter.h>

#include <proxies/FfxApi_Proxy.h>

#include <ffx_upscale.h>
#include <vk/ffx_api_vk.h>

#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>

static std::unordered_map<ffxContext, ffxCreateContextDescUpscale> _initParams;
static std::unordered_map<ffxContext, NVSDK_NGX_Parameter*> _nvParams;
static std::unordered_map<ffxContext, NVSDK_NGX_Handle*> _contexts;
static VkDevice _vkDevice = nullptr;
static VkPhysicalDevice _vkPhysicalDevice = nullptr;
static PFN_vkGetDeviceProcAddr _vkDeviceProcAddress = nullptr;
static bool _nvnxgInited = false;
static float qualityRatios[] = { 1.0f, 1.5f, 1.7f, 2.0f, 3.0f };
static size_t _contextCounter = 0x00001ee7;

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

static VkFormat ffxApiGetVkFormat(uint32_t fmt)
{
    switch (fmt)
    {
    // 32-bit float formats
    case FFX_API_SURFACE_FORMAT_R32G32B32A32_FLOAT:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R32G32B32_FLOAT:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R32G32_FLOAT:
        return VK_FORMAT_R32G32_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R32_FLOAT:
        return VK_FORMAT_R32_SFLOAT;

    // 32-bit integer formats
    case FFX_API_SURFACE_FORMAT_R32G32B32A32_UINT:
        return VK_FORMAT_R32G32B32A32_UINT;
    case FFX_API_SURFACE_FORMAT_R32_UINT:
        return VK_FORMAT_R32_UINT;

    // 16-bit float formats
    case FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R16G16_FLOAT:
        return VK_FORMAT_R16G16_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R16_FLOAT:
        return VK_FORMAT_R16_SFLOAT;

    // 16-bit integer formats
    case FFX_API_SURFACE_FORMAT_R16G16_UINT:
        return VK_FORMAT_R16G16_UINT;
    case FFX_API_SURFACE_FORMAT_R16_UINT:
        return VK_FORMAT_R16_UINT;
    case FFX_API_SURFACE_FORMAT_R16G16_SINT:
        return VK_FORMAT_R16G16_SINT;

    // 16-bit normalized formats
    case FFX_API_SURFACE_FORMAT_R16_UNORM:
        return VK_FORMAT_R16_UNORM;
    case FFX_API_SURFACE_FORMAT_R16_SNORM:
        return VK_FORMAT_R16_SNORM;

    // 8-bit formats
    case FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case FFX_API_SURFACE_FORMAT_R8G8B8A8_SNORM:
        return VK_FORMAT_R8G8B8A8_SNORM;
    case FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case FFX_API_SURFACE_FORMAT_B8G8R8A8_SRGB:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case FFX_API_SURFACE_FORMAT_R8G8_UNORM:
        return VK_FORMAT_R8G8_UNORM;
    case FFX_API_SURFACE_FORMAT_R8G8_UINT:
        return VK_FORMAT_R8G8_UINT;
    case FFX_API_SURFACE_FORMAT_R8_UNORM:
        return VK_FORMAT_R8_UNORM;
    case FFX_API_SURFACE_FORMAT_R8_UINT:
        return VK_FORMAT_R8_UINT;

    // Packed formats
    case FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT:
        return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM:
        return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case FFX_API_SURFACE_FORMAT_R9G9B9E5_SHAREDEXP:
        return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;

    // Special/typeless formats - map to their typed equivalents
    case FFX_API_SURFACE_FORMAT_R32G32B32A32_TYPELESS:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R16G16B16A16_TYPELESS:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R32G32_TYPELESS:
        return VK_FORMAT_R32G32_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R10G10B10A2_TYPELESS:
        return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case FFX_API_SURFACE_FORMAT_R8G8B8A8_TYPELESS:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case FFX_API_SURFACE_FORMAT_B8G8R8A8_TYPELESS:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case FFX_API_SURFACE_FORMAT_R16G16_TYPELESS:
        return VK_FORMAT_R16G16_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R32_TYPELESS:
        return VK_FORMAT_R32_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R16_TYPELESS:
        return VK_FORMAT_R16_SFLOAT;
    case FFX_API_SURFACE_FORMAT_R8G8_TYPELESS:
        return VK_FORMAT_R8G8_UNORM;
    case FFX_API_SURFACE_FORMAT_R8_TYPELESS:
        return VK_FORMAT_R8_UNORM;

    default:
        LOG_WARN("Unknown FFX API surface format: {}", fmt);
        return VK_FORMAT_UNDEFINED;
    }
}

static bool CreateIVandNVRes(FfxApiResource resource, VkImageView* imageView, NVSDK_NGX_Resource_VK* nvResource)
{
    VkImageViewCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = (VkImage) resource.resource;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = ffxApiGetVkFormat(resource.description.format);
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_NONE;
    if (resource.description.usage & FFX_API_RESOURCE_USAGE_DEPTHTARGET)
        createInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    else if (resource.description.usage & FFX_API_RESOURCE_USAGE_STENCILTARGET)
        createInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    else
        createInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;

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
    (*nvResource).Resource.ImageViewInfo.Format = ffxApiGetVkFormat(resource.description.format);
    (*nvResource).ReadWrite = (resource.description.usage & FFX_API_RESOURCE_USAGE_UAV) > 0;

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
    auto commandList = (VkCommandBuffer) pExecParams->commandList;

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

static std::optional<float> GetQualityOverrideRatioFfx(const uint32_t input)
{
    std::optional<float> output;

    auto sliderLimit = Config::Instance()->ExtendedLimits.value_or(false) ? 0.1f : 1.0f;

    if (Config::Instance()->UpscaleRatioOverrideEnabled.value_or(false) &&
        Config::Instance()->UpscaleRatioOverrideValue.value_or(1.3f) >= sliderLimit)
    {
        output = Config::Instance()->UpscaleRatioOverrideValue.value_or(1.3f);

        return output;
    }

    if (!Config::Instance()->QualityRatioOverrideEnabled.value_or(false))
        return output; // override not enabled

    switch (input)
    {
    case FFX_UPSCALE_QUALITY_MODE_ULTRA_PERFORMANCE:
        if (Config::Instance()->QualityRatio_UltraPerformance.value_or(3.0f) >= sliderLimit)
            output = Config::Instance()->QualityRatio_UltraPerformance.value_or(3.0f);

        break;

    case FFX_UPSCALE_QUALITY_MODE_PERFORMANCE:
        if (Config::Instance()->QualityRatio_Performance.value_or(2.0f) >= sliderLimit)
            output = Config::Instance()->QualityRatio_Performance.value_or(2.0f);

        break;

    case FFX_UPSCALE_QUALITY_MODE_BALANCED:
        if (Config::Instance()->QualityRatio_Balanced.value_or(1.7f) >= sliderLimit)
            output = Config::Instance()->QualityRatio_Balanced.value_or(1.7f);

        break;

    case FFX_UPSCALE_QUALITY_MODE_QUALITY:
        if (Config::Instance()->QualityRatio_Quality.value_or(1.5f) >= sliderLimit)
            output = Config::Instance()->QualityRatio_Quality.value_or(1.5f);

        break;

    case FFX_UPSCALE_QUALITY_MODE_NATIVEAA:
        if (Config::Instance()->QualityRatio_DLAA.value_or(1.0f) >= sliderLimit)
            output = Config::Instance()->QualityRatio_DLAA.value_or(1.0f);

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

ffxReturnCode_t ffxCreateContext_Vk(ffxContext* context, ffxCreateContextDescHeader* desc,
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
        else if (header->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK)
        {
            auto backendDesc = (ffxCreateBackendVKDesc*) header;
            _vkDevice = backendDesc->vkDevice;
            _vkPhysicalDevice = backendDesc->vkPhysicalDevice;
            _vkDeviceProcAddress = backendDesc->vkDeviceProcAddr;
        }

        header = header->pNext;

    } while (header != nullptr);

    if (!upscaleContext || Config::Instance()->EnableHotSwapping.value_or_default())
    {
        auto ffxApiResult = FfxApiProxy::VULKAN_CreateContext()(context, desc, memCb);

        LOG_DEBUG("Vulkan_CreateContext result: {:X} ({}), context: {:X}", (UINT) ffxApiResult,
                  FfxApiProxy::ReturnCodeToString(ffxApiResult), (size_t) *context);

        if (!upscaleContext)
            return ffxApiResult;
    }

    if (!State::Instance().nvngxVkInited)
    {
        NVSDK_NGX_FeatureCommonInfo fcInfo {};
        auto exePath = Util::ExePath().remove_filename();

        auto nvResult = NVSDK_NGX_VULKAN_Init_ProjectID_Ext(
            OPTI_GUID, State::Instance().NVNGX_Engine, OPTI_VERSION, exePath.c_str(), State::Instance().VulkanInstance,
            _vkPhysicalDevice, _vkDevice, vkGetInstanceProcAddr, _vkDeviceProcAddress, State::Instance().NVNGX_Version,
            &fcInfo);

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

    if (NVSDK_NGX_VULKAN_GetCapabilityParameters(&params) != NVSDK_NGX_Result_Success)
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

ffxReturnCode_t ffxDestroyContext_Vk(ffxContext* context, const ffxAllocationCallbacks* memCb)
{
    LOG_DEBUG("");

    if (context == nullptr || *context == nullptr)
        return FFX_API_RETURN_OK;

    LOG_DEBUG("context: {:X}", (size_t) *context);

    bool upscalerContext =
        _contexts.contains(*context) || _initParams.contains(*context) || _nvParams.contains(*context);

    if (_contexts.contains(*context))
        NVSDK_NGX_VULKAN_ReleaseFeature(_contexts[*context]);

    _contexts.erase(*context);
    _nvParams.erase(*context);
    _initParams.erase(*context);

    if (upscalerContext && !Config::Instance()->EnableHotSwapping.value_or_default())
        return FFX_API_RETURN_OK;

    if (!State::Instance().isShuttingDown && upscalerContext &&
        Config::Instance()->EnableHotSwapping.value_or_default())
    {
        auto cdResult = FfxApiProxy::VULKAN_DestroyContext()(context, memCb);
        LOG_INFO("result: {:X}", (UINT) cdResult);
        return FFX_API_RETURN_OK;
    }

    auto cdResult = FfxApiProxy::VULKAN_DestroyContext()(context, memCb);
    LOG_INFO("result: {:X}", (UINT) cdResult);
    return cdResult;
}

ffxReturnCode_t ffxConfigure_Vk(ffxContext* context, const ffxConfigureDescHeader* desc)
{
    if (desc == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("type: {:X}", desc->type);

    if (desc->type == FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE)
    {
        auto kvDesc = (ffxConfigureDescUpscaleKeyValue*) desc;

        LOG_DEBUG("key: {}, value: {}, ptr: {:X}", magic_enum::enum_name((FfxApiConfigureUpscaleKey) kvDesc->key),
                  kvDesc->u64, (size_t) kvDesc->ptr);

        if (!Config::Instance()->EnableHotSwapping.value_or_default())
            return FFX_API_RETURN_OK;
    }

    if (desc->type > FFX_API_EFFECT_ID_FRAMEGENERATION && desc->type < 0x00050000u)
    {
        LOG_DEBUG("FG dispatch, redirecting to real FfxApi");
        return FfxApiProxy::VULKAN_Configure()(context, desc);
    }

    if (Config::Instance()->EnableHotSwapping.value_or_default())
        return FfxApiProxy::VULKAN_Configure()(context, desc);

    return FFX_API_RETURN_OK;
}

ffxReturnCode_t ffxQuery_Vk(ffxContext* context, ffxQueryDescHeader* desc)
{
    LOG_DEBUG("");

    if (desc == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("type: {:X}", desc->type);

    auto type = FfxApiProxy::GetIndirectType(desc);
    if (type == FFXStructType::SwapchainVulkan || type == FFXStructType::FG)
    {
        LOG_DEBUG("FG dispatch, redirecting to real FfxApi");
        return FfxApiProxy::VULKAN_Query()(context, desc);
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

        if (type == FFXStructType::SwapchainVulkan || type == FFXStructType::FG)
        {
            ver = FfxApiProxy::VersionVk();

            providerDesc->versionId =
                0xF600'0000ui64 << 32u << 32 | (((ver.major << 22) | (ver.minor << 12) | ver.patch) & 0xFFFFFFFF);
        }
        else if (type == FFXStructType::Upscaling)
        {
            ver = FfxApiProxy::VersionVk();

            providerDesc->versionId =
                0xF5A5'CA1Eui64 << 32 | (((ver.major << 22) | (ver.minor << 12) | ver.patch) & 0xFFFFFFFF);
        }
        else
        {
            ver = FfxApiProxy::VersionVk();

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
        return FfxApiProxy::VULKAN_Query()(context, desc);
    }

    return FFX_API_RETURN_OK;
}

ffxReturnCode_t ffxDispatch_Vk(ffxContext* context, ffxDispatchDescHeader* desc)
{
    if (desc == nullptr || context == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    LOG_DEBUG("context: {:X}, type: {:X}", (size_t) *context, desc->type);

    auto type = FfxApiProxy::GetType(desc->type);
    if (type == FFXStructType::SwapchainVulkan || type == FFXStructType::FG)
    {
        LOG_DEBUG("FG dispatch, redirecting to real FfxApi");
        return FfxApiProxy::VULKAN_Dispatch()(context, desc);
    }

    // Skip OptiScaler stuff
    if (Config::Instance()->EnableHotSwapping.value_or_default() &&
        !Config::Instance()->UseFfxInputs.value_or_default())
        return FfxApiProxy::VULKAN_Dispatch()(context, desc);

    if (context == nullptr || !_initParams.contains(*context))
    {
        LOG_INFO("Not in _contexts");
        return FfxApiProxy::VULKAN_Dispatch()(context, desc);
    }

    ffxApiHeader* header = desc;
    ffxDispatchDescUpscale* dispatchDesc = nullptr;
    bool rmDesc = false;

    // WORKAROUND: Doom TDA sends non-nullptr pNext
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
        return FfxApiProxy::VULKAN_Dispatch()(context, desc);
    }

    if (dispatchDesc->commandList == nullptr)
    {
        LOG_ERROR("dispatchDesc->commandList == nullptr !!!");
        return FfxApiProxy::VULKAN_Dispatch()(context, desc);
    }

    // If not in contexts list create and add context
    auto contextId = (size_t) *context;
    if (!_contexts.contains(*context) && _initParams.contains(*context) && !CreateDLSSContext(*context, dispatchDesc))
    {
        LOG_DEBUG("!_contexts.contains(*context) && _initParams.contains(*context) && !CreateDLSSContext(*context, "
                  "dispatchDesc)");
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }

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
    if (dispatchDesc->depth.resource == nullptr || !CreateIVandNVRes(dispatchDesc->depth, &depthImageView, &depthNVRes))
    {
        LOG_ERROR("Depth error!");
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }
    else
    {
        params->Set(NVSDK_NGX_Parameter_Depth, &depthNVRes);
    }

    if (dispatchDesc->exposure.resource != nullptr &&
        CreateIVandNVRes(dispatchDesc->exposure, &expImageView, &expNVRes))
    {
        params->Set(NVSDK_NGX_Parameter_ExposureTexture, &expNVRes);
    }

    if (dispatchDesc->reactive.resource != nullptr &&
        CreateIVandNVRes(dispatchDesc->reactive, &biasImageView, &biasNVRes))
    {
        params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &biasNVRes);
    }

    if (dispatchDesc->color.resource == nullptr || !CreateIVandNVRes(dispatchDesc->color, &colorImageView, &colorNVRes))
    {
        LOG_ERROR("Color error!");
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }
    else
    {
        params->Set(NVSDK_NGX_Parameter_Color, &colorNVRes);
    }

    if (dispatchDesc->motionVectors.resource == nullptr ||
        !CreateIVandNVRes(dispatchDesc->motionVectors, &mvImageView, &mvNVRes))
    {
        LOG_ERROR("Motion Vectors error!");
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }
    else
    {
        params->Set(NVSDK_NGX_Parameter_MotionVectors, &mvNVRes);
    }

    if (dispatchDesc->output.resource == nullptr ||
        !CreateIVandNVRes(dispatchDesc->output, &outputImageView, &outputNVRes))
    {
        LOG_ERROR("Output error!");
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }
    else
    {
        params->Set(NVSDK_NGX_Parameter_Output, &outputNVRes);
    }

    if (dispatchDesc->transparencyAndComposition.resource != nullptr &&
        CreateIVandNVRes(dispatchDesc->transparencyAndComposition, &fsrTransparencyView, &fsrTransparencyNVRes))
    {
        params->Set("FSR.transparencyAndComposition", &fsrTransparencyNVRes);
    }

    if (dispatchDesc->reactive.resource != nullptr &&
        CreateIVandNVRes(dispatchDesc->reactive, &fsrReactiveView, &fsrReactiveNVRes))
    {
        params->Set("FSR.reactive", &fsrReactiveNVRes);
    }

    params->Set("FSR.cameraNear", dispatchDesc->cameraNear);
    params->Set("FSR.cameraFar", dispatchDesc->cameraFar);
    params->Set("FSR.cameraFovAngleVertical", dispatchDesc->cameraFovAngleVertical);
    params->Set("FSR.frameTimeDelta", dispatchDesc->frameTimeDelta);
    params->Set("FSR.viewSpaceToMetersFactor", dispatchDesc->viewSpaceToMetersFactor);
    params->Set(NVSDK_NGX_Parameter_Sharpness, dispatchDesc->sharpness);

    LOG_DEBUG("handle: {:X}, internalResolution: {}x{}", handle->Id, dispatchDesc->renderSize.width,
              dispatchDesc->renderSize.height);

    State::Instance().setInputApiName = ApiUpscalerInput::FFX_VK;

    auto evalResult =
        NVSDK_NGX_VULKAN_EvaluateFeature((VkCommandBuffer) dispatchDesc->commandList, handle, params, nullptr);

    if (evalResult == NVSDK_NGX_Result_Success)
        return FFX_API_RETURN_OK;

    LOG_ERROR("evalResult: {:X}", (UINT) evalResult);
    return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
}
