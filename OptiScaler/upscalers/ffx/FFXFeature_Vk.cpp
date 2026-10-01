#include <pch.h>
#include <Config.h>
#include <Util.h>
#include <proxies/FfxApi_Proxy.h>
#include "FFXFeature_Vk.h"
#include "FFXVkPresetReporting.h"
#include "FFXVkInputReads.h"
#include "ffx_vk_native_inputs.h"
#include "ffx_vk_create_device_features.h"
#include <hooks/Vulkan_Hooks.h>
#include "nvsdk_ngx_vk.h"
#include "MathUtils.h"

using namespace OptiMath;

static inline uint32_t ffxApiGetSurfaceFormatVKLocal(VkFormat fmt)
{
    switch (fmt)
    {
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return FFX_API_SURFACE_FORMAT_R32G32B32A32_FLOAT;
    case VK_FORMAT_R32G32B32_SFLOAT:
        return FFX_API_SURFACE_FORMAT_R32G32B32_FLOAT;
    case VK_FORMAT_R32G32B32A32_UINT:
        return FFX_API_SURFACE_FORMAT_R32G32B32A32_UINT;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
    case VK_FORMAT_R32G32_SFLOAT:
        return FFX_API_SURFACE_FORMAT_R32G32_FLOAT;
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        return FFX_API_SURFACE_FORMAT_R32_UINT;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_SNORM:
        return FFX_API_SURFACE_FORMAT_R8G8B8A8_SNORM;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB;
    case VK_FORMAT_B8G8R8A8_UNORM:
        return FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return FFX_API_SURFACE_FORMAT_B8G8R8A8_SRGB;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        return FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        return FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM;
    case VK_FORMAT_R16G16_UNORM:
        return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_R16G16_SNORM:
        return FFX_API_SURFACE_FORMAT_R8G8B8A8_SNORM;
    case VK_FORMAT_R16G16_USCALED:
    case VK_FORMAT_R16G16_SSCALED:
    case VK_FORMAT_R16G16_SFLOAT:
        return FFX_API_SURFACE_FORMAT_R16G16_FLOAT;
    case VK_FORMAT_R16G16_UINT:
        return FFX_API_SURFACE_FORMAT_R16G16_UINT;
    case VK_FORMAT_R16G16_SINT:
        return FFX_API_SURFACE_FORMAT_R16G16_SINT;
    case VK_FORMAT_R16_SFLOAT:
        return FFX_API_SURFACE_FORMAT_R16_FLOAT;
    case VK_FORMAT_R16_UINT:
        return FFX_API_SURFACE_FORMAT_R16_UINT;
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D16_UNORM_S8_UINT:
        return FFX_API_SURFACE_FORMAT_R16_UNORM;
    case VK_FORMAT_R16_SNORM:
        return FFX_API_SURFACE_FORMAT_R16_SNORM;
    case VK_FORMAT_R8_UNORM:
        return FFX_API_SURFACE_FORMAT_R8_UNORM;
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_S8_UINT:
        return FFX_API_SURFACE_FORMAT_R8_UINT;
    case VK_FORMAT_R8G8_UNORM:
        return FFX_API_SURFACE_FORMAT_R8G8_UNORM;
    case VK_FORMAT_R8G8_UINT:
        return FFX_API_SURFACE_FORMAT_R8G8_UINT;
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return FFX_API_SURFACE_FORMAT_R32_FLOAT;
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        return FFX_API_SURFACE_FORMAT_R9G9B9E5_SHAREDEXP;
    case VK_FORMAT_UNDEFINED:
        return FFX_API_SURFACE_FORMAT_UNKNOWN;

    default:
        // NOTE: we do not support typeless formats here
        // FFX_ASSERT_MESSAGE(false, "Format not yet supported");
        return FFX_API_SURFACE_FORMAT_UNKNOWN;
    }
}

static inline FfxApiResourceDescription ffxApiGetImageResourceDescriptionVKLocal(NVSDK_NGX_Resource_VK* vkResource)
{
    FfxApiResourceDescription resourceDescription = {};

    // This is valid
    if (vkResource->Resource.ImageViewInfo.Image == VK_NULL_HANDLE)
        return resourceDescription;

    // Set flags properly for resource registration
    resourceDescription.usage = FFX_API_RESOURCE_USAGE_READ_ONLY;

    // Unordered access use
    if (vkResource->ReadWrite)
        resourceDescription.usage |= FFX_API_RESOURCE_USAGE_UAV;

    // depth use
    if ((vkResource->Resource.ImageViewInfo.SubresourceRange.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) > 0)
        resourceDescription.usage |= FFX_API_RESOURCE_USAGE_DEPTHTARGET;

    if ((vkResource->Resource.ImageViewInfo.SubresourceRange.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) > 0)
        resourceDescription.usage |= FFX_API_RESOURCE_USAGE_STENCILTARGET;

    // Some NGX inputs report COLOR aspect even for a depth/stencil VkFormat.
    // Preserve the actual format class before the FFX enum collapses it to R32.
    switch (vkResource->Resource.ImageViewInfo.Format)
    {
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        resourceDescription.usage |= FFX_API_RESOURCE_USAGE_STENCILTARGET;
        [[fallthrough]];
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
        resourceDescription.usage |= FFX_API_RESOURCE_USAGE_DEPTHTARGET;
        break;
    default:
        break;
    }

    resourceDescription.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    resourceDescription.width = vkResource->Resource.ImageViewInfo.Width;
    resourceDescription.height = vkResource->Resource.ImageViewInfo.Height;
    resourceDescription.mipCount = 1;
    resourceDescription.depth = 1;
    resourceDescription.flags = FFX_API_RESOURCE_FLAGS_NONE;
    resourceDescription.format = ffxApiGetSurfaceFormatVKLocal(vkResource->Resource.ImageViewInfo.Format);

    return resourceDescription;
}

FFXFeatureVk::FFXFeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : FFXFeature(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters), IFeature(InHandleId, InParameters)
{
    _moduleLoaded = FfxApiProxy::InitFfxVk();

    if (_moduleLoaded)
        LOG_INFO("Vulkan FFX methods loaded!");
    else
        LOG_ERROR("Can't load Vulkan FFX methods!");
}

bool FFXFeatureVk::InitFFX(const NVSDK_NGX_Parameter* InParameters)
{
    LOG_DEBUG("FFXFeatureVk::InitFFX");

    if (!ModuleLoaded())
        return false;

    if (IsInited())
        return true;

    State::Instance().currentFsr4Preset.reset();
    _presetQuerySupported = true;
    _nativeInputsSupported = false;

    if (PhysicalDevice == nullptr)
    {
        LOG_ERROR("PhysicalDevice is null!");
        return false;
    }

    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

        QueryVersionsVulkan();

        InitFlags();

        ffxCreateBackendVKDesc backendDesc = { 0 };
        backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backendDesc.vkDevice = Device;
        backendDesc.vkPhysicalDevice = PhysicalDevice;

        if (GDPA == nullptr)
            backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;
        else
            backendDesc.vkDeviceProcAddr = GDPA;

        _contextDesc.header.pNext = &backendDesc.header;

        if (Config::Instance()->FfxUpscalerIndex.value_or_default() < 0 ||
            Config::Instance()->FfxUpscalerIndex.value_or_default() >= State::Instance().ffxUpscalerVersionIds.size())
            Config::Instance()->FfxUpscalerIndex.set_volatile_value(0);

        ffxOverrideVersion ov = { 0 };
        ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
        ov.versionId = State::Instance().ffxUpscalerVersionIds[Config::Instance()->FfxUpscalerIndex.value_or_default()];
        backendDesc.header.pNext = &ov.header;

        // Optional provider capability: old/other FFX providers keep their
        // unchanged create/dispatch chains. No name/version-string guessing.
        Fsr4VkQueryNativeInputs nativeQuery {};
        nativeQuery.header.type = FSR4VK_QUERY_DESC_TYPE_NATIVE_INPUTS;
        _nativeInputsSupported =
            FfxApiProxy::VULKAN_Query()(nullptr, &nativeQuery.header) == FFX_API_RETURN_OK && nativeQuery.version == 1;

        Fsr4VkQueryDeviceFeatures deviceQuery {};
        deviceQuery.header.type = FSR4VK_QUERY_DESC_TYPE_DEVICE_FEATURES;
        Fsr4VkCreateDeviceFeatures enabledDevice {};
        if (FfxApiProxy::VULKAN_Query()(nullptr, &deviceQuery.header) == FFX_API_RETURN_OK &&
            deviceQuery.version == 1 &&
            VulkanHooks::GetCreatedDeviceFeatures(Device, enabledDevice.enabledFlags, enabledDevice.apiVersion,
                                                  enabledDevice.queueFamilyIndex))
        {
            enabledDevice.header = { FSR4VK_CREATE_DESC_TYPE_DEVICE_FEATURES, backendDesc.header.pNext };
            backendDesc.header.pNext = &enabledDevice.header;
        }

        LOG_DEBUG("_createContext!");
        auto ret = FfxApiProxy::VULKAN_CreateContext()(&_context, &_contextDesc.header, NULL);

        if (ret != FFX_API_RETURN_OK)
        {
            LOG_ERROR("_createContext error: {0}", FfxApiProxy::ReturnCodeToString(ret));
            return false;
        }
    }

    const auto supportedPresets = FFXVkPresetReporting::SupportedPresets(FfxApiProxy::VULKAN_Query(), &_context);
    const auto requestedPreset =
        Config::Instance()->Fsr4Preset.has_value() ? Config::Instance()->Fsr4Preset.value() : FSR4VK_PRESET_AUTO;
    if (supportedPresets.has_value())
    {
        const auto result =
            FFXVkPresetReporting::Apply(FfxApiProxy::VULKAN_Configure(), &_context, *supportedPresets, requestedPreset);
        if (result != FFX_API_RETURN_OK)
            LOG_WARN("Vulkan FSR4 preset {} rejected; new context remains Auto ({})", requestedPreset,
                     FfxApiProxy::ReturnCodeToString(result));
    }
    else if (requestedPreset != FSR4VK_PRESET_AUTO)
        LOG_WARN("Vulkan FSR4 provider does not advertise preset forcing; using provider default");

    auto version = State::Instance().ffxUpscalerVersionNames[Config::Instance()->FfxUpscalerIndex.value_or_default()];
    _name = "FSR";
    parse_version(version);

    if (Version().major >= 4)
        State::Instance().currentFsr4Preset.reset();

    SetInit(true);

    return true;
}

bool FFXFeatureVk::InitInternal(VkCommandBuffer InCmdList, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    return InitFFX(InParameters);
}

bool FFXFeatureVk::EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    FFXVkPresetReporting::DispatchReport presetReport { State::Instance().currentFsr4Preset };

    if (!IsInited())
        return false;

    auto& state = State::Instance();
    auto& cfg = *Config::Instance();
    const auto& ngxParams = *InParameters;

    struct ffxDispatchDescUpscale params = { 0 };
    Fsr4VkDispatchNativeInputs nativeInputs {};
    params.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;

    if (Config::Instance()->FsrDebugView.value_or_default())
        params.flags = FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW;

    if (Config::Instance()->FsrNonLinearPQ.value_or_default())
        params.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ;
    else if (Config::Instance()->FsrNonLinearSRGB.value_or_default())
        params.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;

    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &params.jitterOffset.x);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &params.jitterOffset.y);

    const auto reset = FFXVkInputReads::Optional(InParameters, NVSDK_NGX_Parameter_Reset, NVSDK_NGX_Result_Success, 0u);
    params.reset = (reset == 1);

    GetRenderResolution(InParameters, &params.renderSize.width, &params.renderSize.height);

    LOG_DEBUG("Input Resolution: {0}x{1}", params.renderSize.width, params.renderSize.height);

    params.commandList = InCmdBuffer;

    NVSDK_NGX_Resource_VK* paramColor = nullptr;
    if (FFXVkInputReads::Required(InParameters, NVSDK_NGX_Parameter_Color, NVSDK_NGX_Result_Success, paramColor))
    {
        LOG_DEBUG("Color exist..");

        params.color = ffxApiGetResourceVK(paramColor->Resource.ImageViewInfo.Image,
                                           ffxApiGetImageResourceDescriptionVKLocal(paramColor),
                                           FFX_API_RESOURCE_STATE_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("Color not exist!!");
        return false;
    }

    NVSDK_NGX_Resource_VK* paramVelocity = nullptr;
    if (FFXVkInputReads::Required(InParameters, NVSDK_NGX_Parameter_MotionVectors, NVSDK_NGX_Result_Success,
                                  paramVelocity))
    {
        LOG_DEBUG("MotionVectors exist..");

        params.motionVectors = ffxApiGetResourceVK(paramVelocity->Resource.ImageViewInfo.Image,
                                                   ffxApiGetImageResourceDescriptionVKLocal(paramVelocity),
                                                   FFX_API_RESOURCE_STATE_COMPUTE_READ);
        if (_nativeInputsSupported)
        {
            nativeInputs.header = { FSR4VK_DISPATCH_DESC_TYPE_NATIVE_INPUTS, params.header.pNext };
            nativeInputs.motionFormat = paramVelocity->Resource.ImageViewInfo.Format;
            params.header.pNext = &nativeInputs.header;
        }
    }
    else
    {
        LOG_ERROR("MotionVectors not exist!!");
        return false;
    }

    NVSDK_NGX_Resource_VK* paramOutput = nullptr;
    if (FFXVkInputReads::Required(InParameters, NVSDK_NGX_Parameter_Output, NVSDK_NGX_Result_Success, paramOutput))
    {
        LOG_DEBUG("Output exist..");

        params.output = ffxApiGetResourceVK(paramOutput->Resource.ImageViewInfo.Image,
                                            ffxApiGetImageResourceDescriptionVKLocal(paramOutput),
                                            FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    else
    {
        LOG_ERROR("Output not exist!!");
        return false;
    }

    NVSDK_NGX_Resource_VK* paramDepth = nullptr;
    InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth);

    if (paramDepth)
    {
        LOG_DEBUG("Depth exist..");

        params.depth = ffxApiGetResourceVK(paramDepth->Resource.ImageViewInfo.Image,
                                           ffxApiGetImageResourceDescriptionVKLocal(paramDepth),
                                           FFX_API_RESOURCE_STATE_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("Depth not exist!!");

        if (LowResMV())
            return false;
    }

    if (!_resourceContractLogged)
    {
        const auto logResource = [](const char* name, const NVSDK_NGX_Resource_VK* resource)
        {
            if (!resource)
                return;
            const auto& image = resource->Resource.ImageViewInfo;
            LOG_INFO("Vulkan resource contract {}: {}x{} format={} aspect={} baseMip={} mipCount={} "
                     "baseLayer={} layerCount={} readWrite={}",
                     name, image.Width, image.Height, static_cast<int>(image.Format), image.SubresourceRange.aspectMask,
                     image.SubresourceRange.baseMipLevel, image.SubresourceRange.levelCount,
                     image.SubresourceRange.baseArrayLayer, image.SubresourceRange.layerCount, resource->ReadWrite);
        };
        logResource("color", paramColor);
        logResource("depth", paramDepth);
        logResource("motion", paramVelocity);
        logResource("output", paramOutput);
        _resourceContractLogged = true;
    }

    NVSDK_NGX_Resource_VK* paramExp = nullptr;
    if (AutoExposure())
    {
        LOG_DEBUG("AutoExposure enabled!");
    }
    else
    {
        InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, (void**) &paramExp);

        if (paramExp)
        {
            LOG_DEBUG("ExposureTexture exist..");

            params.exposure = ffxApiGetResourceVK(paramExp->Resource.ImageViewInfo.Image,
                                                  ffxApiGetImageResourceDescriptionVKLocal(paramExp),
                                                  FFX_API_RESOURCE_STATE_COMPUTE_READ);
        }
        else
        {
            LOG_DEBUG("AutoExposure disabled but ExposureTexture is not exist, it may cause problems!!");
            State::Instance().autoExposure = true;
            State::Instance().changeBackend[Handle()->Id] = true;
            return true;
        }
    }

    NVSDK_NGX_Resource_VK* paramTransparency = nullptr;
    InParameters->Get("FSR.transparencyAndComposition", (void**) &paramTransparency);

    NVSDK_NGX_Resource_VK* paramReactiveMask = nullptr;
    InParameters->Get("FSR.reactive", (void**) &paramReactiveMask);

    NVSDK_NGX_Resource_VK* paramReactiveMask2 = nullptr;
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactiveMask2);

    if (!Config::Instance()->DisableReactiveMask.value_or(paramReactiveMask == nullptr &&
                                                          paramReactiveMask2 == nullptr))
    {
        if (paramTransparency != nullptr)
        {
            LOG_DEBUG("Using FSR transparency mask..");
            params.transparencyAndComposition = ffxApiGetResourceVK(
                paramTransparency->Resource.ImageViewInfo.Image,
                ffxApiGetImageResourceDescriptionVKLocal(paramTransparency), FFX_API_RESOURCE_STATE_COMPUTE_READ);
        }

        if (paramReactiveMask != nullptr)
        {
            LOG_DEBUG("Using FSR reactive mask..");
            params.reactive = ffxApiGetResourceVK(paramReactiveMask->Resource.ImageViewInfo.Image,
                                                  ffxApiGetImageResourceDescriptionVKLocal(paramReactiveMask),
                                                  FFX_API_RESOURCE_STATE_COMPUTE_READ);
        }
        else
        {
            if (paramReactiveMask2 != nullptr)
            {
                if (Config::Instance()->FsrUseMaskForTransparency.value_or_default())
                {
                    params.transparencyAndComposition =
                        ffxApiGetResourceVK(paramReactiveMask2->Resource.ImageViewInfo.Image,
                                            ffxApiGetImageResourceDescriptionVKLocal(paramReactiveMask2),
                                            FFX_API_RESOURCE_STATE_COMPUTE_READ);
                }

                LOG_DEBUG("Bias mask exist..");

                if (Config::Instance()->DlssReactiveMaskBias.value_or_default() > 0.0f)
                {
                    params.reactive = ffxApiGetResourceVK(paramReactiveMask2->Resource.ImageViewInfo.Image,
                                                          ffxApiGetImageResourceDescriptionVKLocal(paramReactiveMask2),
                                                          FFX_API_RESOURCE_STATE_COMPUTE_READ);
                }
            }
            else
            {
                LOG_DEBUG("Bias mask not exist and its enabled in config, it may cause problems!!");
                Config::Instance()->DisableReactiveMask.set_volatile_value(true);
                return true;
            }
        }
    }

    _hasColor = params.color.resource != nullptr;
    _hasDepth = params.depth.resource != nullptr;
    _hasMV = params.motionVectors.resource != nullptr;
    _hasExposure = params.exposure.resource != nullptr;
    _hasTM = params.transparencyAndComposition.resource != nullptr;
    _accessToReactiveMask = paramReactiveMask != nullptr || paramReactiveMask2 != nullptr;
    _hasOutput = params.output.resource != nullptr;

    params.motionVectorScale.x = 1.0f;
    params.motionVectorScale.y = 1.0f;

    if (InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &params.motionVectorScale.x) != NVSDK_NGX_Result_Success ||
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &params.motionVectorScale.y) != NVSDK_NGX_Result_Success)
    {
        LOG_WARN("Can't get motion vector scales!");
    }

    params.enableSharpening = _sharpness > 0.0f;
    params.sharpness = _sharpness;

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

    const auto preExposureResult = InParameters->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &params.preExposure);
    if (preExposureResult != NVSDK_NGX_Result_Success)
        params.preExposure = 1.0f;
    else if (!std::isfinite(params.preExposure) || params.preExposure <= 0.0f)
    {
        LOG_WARN("Invalid pre-exposure {} received from game; using 1.0", params.preExposure);
        params.preExposure = 1.0f;
    }

    if (Version() >= feature_version { 3, 1, 1 } && _velocity != Config::Instance()->FsrVelocity.value_or_default())
    {
        _velocity = Config::Instance()->FsrVelocity.value_or_default();
        ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
        m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
        m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FVELOCITYFACTOR;
        m_upscalerKeyValueConfig.ptr = &_velocity;
        auto result = FfxApiProxy::VULKAN_Configure()(&_context, &m_upscalerKeyValueConfig.header);

        if (result != FFX_API_RETURN_OK)
            LOG_WARN("Velocity configure result: {}", (UINT) result);
    }

    if (Version() >= feature_version { 3, 1, 4 })
    {
        if (_reactiveScale != Config::Instance()->FsrReactiveScale.value_or_default())
        {
            _reactiveScale = Config::Instance()->FsrReactiveScale.value_or_default();
            ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
            m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
            m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FREACTIVENESSSCALE;
            m_upscalerKeyValueConfig.ptr = &_reactiveScale;
            auto result = FfxApiProxy::VULKAN_Configure()(&_context, &m_upscalerKeyValueConfig.header);

            if (result != FFX_API_RETURN_OK)
                LOG_WARN("Reactive Scale configure result: {}", (UINT) result);
        }

        if (_shadingScale != Config::Instance()->FsrShadingScale.value_or_default())
        {
            _shadingScale = Config::Instance()->FsrShadingScale.value_or_default();
            ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
            m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
            m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FSHADINGCHANGESCALE;
            m_upscalerKeyValueConfig.ptr = &_shadingScale;
            auto result = FfxApiProxy::VULKAN_Configure()(&_context, &m_upscalerKeyValueConfig.header);

            if (result != FFX_API_RETURN_OK)
                LOG_WARN("Shading Scale configure result: {}", (UINT) result);
        }

        if (_accAddPerFrame != Config::Instance()->FsrAccAddPerFrame.value_or_default())
        {
            _accAddPerFrame = Config::Instance()->FsrAccAddPerFrame.value_or_default();
            ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
            m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
            m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FACCUMULATIONADDEDPERFRAME;
            m_upscalerKeyValueConfig.ptr = &_accAddPerFrame;
            auto result = FfxApiProxy::VULKAN_Configure()(&_context, &m_upscalerKeyValueConfig.header);

            if (result != FFX_API_RETURN_OK)
                LOG_WARN("Acc. Add Per Frame configure result: {}", (UINT) result);
        }

        if (_minDisOccAcc != Config::Instance()->FsrMinDisOccAcc.value_or_default())
        {
            _minDisOccAcc = Config::Instance()->FsrMinDisOccAcc.value_or_default();
            ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
            m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
            m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FMINDISOCCLUSIONACCUMULATION;
            m_upscalerKeyValueConfig.ptr = &_minDisOccAcc;
            auto result = FfxApiProxy::VULKAN_Configure()(&_context, &m_upscalerKeyValueConfig.header);

            if (result != FFX_API_RETURN_OK)
                LOG_WARN("Minimum Disocclusion Acc. configure result: {}", (UINT) result);
        }
    }

    if (InParameters->Get("FSR.upscaleSize.width", &params.upscaleSize.width) == NVSDK_NGX_Result_Success &&
        Config::Instance()->OutputScalingEnabled.value_or_default())
    {
        auto originalWidth = static_cast<float>(params.upscaleSize.width);
        params.upscaleSize.width =
            static_cast<uint32_t>(originalWidth * Config::Instance()->OutputScalingMultiplier.value_or_default());
    }
    else if (params.upscaleSize.width == 0)
    {
        params.upscaleSize.width = TargetWidth();
    }

    if (InParameters->Get("FSR.upscaleSize.height", &params.upscaleSize.height) == NVSDK_NGX_Result_Success &&
        Config::Instance()->OutputScalingEnabled.value_or_default())
    {
        auto originalHeight = static_cast<float>(params.upscaleSize.height);
        params.upscaleSize.height =
            static_cast<uint32_t>(originalHeight * Config::Instance()->OutputScalingMultiplier.value_or_default());
    }
    else if (params.upscaleSize.height == 0)
    {
        params.upscaleSize.height = TargetHeight();
    }

    LOG_DEBUG("Dispatch!!");
    auto result = FfxApiProxy::VULKAN_Dispatch()(&_context, &params.header);

    if (result != FFX_API_RETURN_OK)
    {
        LOG_ERROR("ffxFsr2ContextDispatch error: {0}", FfxApiProxy::ReturnCodeToString(result));
        return false;
    }

    presetReport.activePreset =
        FFXVkPresetReporting::Read(FfxApiProxy::VULKAN_Query(), &_context, _presetQuerySupported);

    return true;
}
