#include <pch.h>
#include <Config.h>
#include <Util.h>
#include <proxies/FfxApi_Proxy.h>
#include "FFXFeature_Vk.h"
#include "nvsdk_ngx_vk.h"
#include "MathUtils.h"
#include <fsr4/FSR4DriverProviderVk.h>

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

    // Some games pass depth with a color aspect, FFX picks the view format from these flags
    const auto format = vkResource->Resource.ImageViewInfo.Format;
    const auto aspectMask = vkResource->Resource.ImageViewInfo.SubresourceRange.aspectMask;
    const bool stencilFormat = format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT ||
                               format == VK_FORMAT_D32_SFLOAT_S8_UINT;

    if ((aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) > 0 || ffxApiIsDepthFormat(format))
        resourceDescription.usage |= FFX_API_RESOURCE_USAGE_DEPTHTARGET;

    if ((aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) > 0 || stencilFormat)
        resourceDescription.usage |= FFX_API_RESOURCE_USAGE_STENCILTARGET;

    resourceDescription.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    resourceDescription.width = vkResource->Resource.ImageViewInfo.Width;
    resourceDescription.height = vkResource->Resource.ImageViewInfo.Height;
    resourceDescription.mipCount = 1;
    resourceDescription.depth = 1;
    resourceDescription.flags = FFX_API_RESOURCE_FLAGS_NONE;
    resourceDescription.format = ffxApiGetSurfaceFormatVKLocal(vkResource->Resource.ImageViewInfo.Format);

    return resourceDescription;
}

FFXFeatureVk::FFXFeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters,
                           const std::string& customUpscaler)
    : FFXFeature(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters), IFeature(InHandleId, InParameters)
{
    // Regular dlls go first, so their name lookups don't find the custom dll
    FfxApiProxy::InitFfxVk();

    if (!customUpscaler.empty())
    {
        _customUpscaler = FfxApiProxy::LoadCustomUpscalerVk(customUpscaler);
        _moduleLoaded = _customUpscaler != nullptr;

        if (_moduleLoaded)
            LOG_INFO("Custom FFX Vulkan upscaler {} methods loaded!", customUpscaler);
        else
            ImGui::InsertNotification(
                { ImGuiToastType::Error, 10000, "Can't load custom upscaler %s", customUpscaler.c_str() });

        return;
    }

    _moduleLoaded = FfxApiProxy::IsSRReadyVk();

    if (_moduleLoaded)
        LOG_INFO("FFX Vulkan upscaler methods loaded!");
    else
        LOG_ERROR("Can't load FFX Vulkan upscaler methods!");

    // Driver provided FSR 4 works without the FFX SDK
    if (FSR4DriverProviderVk::Init())
        _moduleLoaded = true;
}

bool FFXFeatureVk::InitFFX(const NVSDK_NGX_Parameter* InParameters)
{
    LOG_DEBUG("FFXFeatureVk::InitFFX");

    if (!ModuleLoaded())
        return false;

    if (IsInited())
        return true;

    if (PhysicalDevice == nullptr)
    {
        LOG_ERROR("PhysicalDevice is null!");
        return false;
    }

    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

        ffxCreateBackendVKDesc backendDesc = { 0 };
        backendDesc.vkDevice = Device;
        backendDesc.vkPhysicalDevice = PhysicalDevice;

        if (GDPA == nullptr)
            backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;
        else
            backendDesc.vkDeviceProcAddr = GDPA;

        // The device is used for filtering out unsupported upscalers (FSR4), the fields after the header are
        // VkDeviceContext of FFX SDK 2.x
        auto& versions =
            _customUpscaler != nullptr ? _customUpscaler->versions : State::Instance().ffxUpscalerVersionsVk;

        if (!QueryVersionsVulkan(&backendDesc.vkDevice, _customUpscaler) || versions.ids.empty())
        {
            LOG_ERROR("No FFX upscalers reported!");
            return false;
        }

        InitFlags();

        _contextDesc.header.pNext = &backendDesc.header;

        auto& upscalerIndex =
            Config::Instance()->FfxUpscalerIndexFor(_customUpscaler != nullptr ? _customUpscaler->name : "");

        if (upscalerIndex.value_or_default() < 0 || upscalerIndex.value_or_default() >= versions.ids.size())
            upscalerIndex.set_volatile_value(0);

        ffxOverrideVersion ov = { 0 };
        ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
        ov.versionId = versions.ids[upscalerIndex.value_or_default()];

        ffxCreateContextDescUpscaleVersionV2 upscaleVersion {};
        upscaleVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION_V2;
        upscaleVersion.version = FFX_UPSCALER_VERSION_V2;

        if (_customUpscaler != nullptr)
        {
            backendDesc.header.pNext = &ov.header;
            _ffx = &_customUpscaler->module;
        }
        else if (FSR4DriverProviderVk::IsAvailable() && ov.versionId == FSR4DriverProviderVk::VersionId())
        {
            if (!FSR4DriverProviderVk::IsSupported(Device, PhysicalDevice, backendDesc.vkDeviceProcAddr))
            {
                LOG_ERROR("Driver provided FSR is not supported on this device!");
                return false;
            }

            LOG_INFO("Using driver provided FSR {}", FSR4DriverProviderVk::VersionName());

            // Version override is not needed
            backendDesc.header.pNext = &upscaleVersion.header;
            _ffx = FSR4DriverProviderVk::Module();
        }
        else
        {
            backendDesc.header.pNext = &ov.header;
            _ffx = FfxApiProxy::VkFfxModule_SR();
        }

        backendDesc.header.type = FfxApiProxy::VkBackendType(_ffx);

        if (_ffx->CreateContext == nullptr)
        {
            LOG_ERROR("FFX Vulkan upscaler is not loaded!");
            return false;
        }

        LOG_DEBUG("_createContext!");
        auto ret = _ffx->CreateContext(&_context, &_contextDesc.header, NULL);

        if (ret != FFX_API_RETURN_OK)
        {
            LOG_ERROR("_createContext error: {0}", FfxApiProxy::ReturnCodeToString(ret));
            return false;
        }
    }

    auto& versions = _customUpscaler != nullptr ? _customUpscaler->versions : State::Instance().ffxUpscalerVersionsVk;
    auto& upscalerIndex =
        Config::Instance()->FfxUpscalerIndexFor(_customUpscaler != nullptr ? _customUpscaler->name : "");
    _name = "FSR";
    parse_version(versions.names[upscalerIndex.value_or_default()]);

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

    if (!IsInited())
        return false;

    auto& state = State::Instance();
    auto& cfg = *Config::Instance();
    const auto& ngxParams = *InParameters;

    struct ffxDispatchDescUpscale params = { 0 };
    params.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;

    if (Config::Instance()->FsrDebugView.value_or_default())
        params.flags = FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW;

    if (Config::Instance()->FsrNonLinearPQ.value_or_default())
        params.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ;
    else if (Config::Instance()->FsrNonLinearSRGB.value_or_default())
        params.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;

    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &params.jitterOffset.x);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &params.jitterOffset.y);

    unsigned int reset;
    InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset);
    params.reset = (reset == 1);

    GetRenderResolution(InParameters, &params.renderSize.width, &params.renderSize.height);

    LOG_DEBUG("Input Resolution: {0}x{1}", params.renderSize.width, params.renderSize.height);

    params.commandList = InCmdBuffer;

    NVSDK_NGX_Resource_VK* paramColor;
    InParameters->Get(NVSDK_NGX_Parameter_Color, (void**) &paramColor);

    if (paramColor)
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

    NVSDK_NGX_Resource_VK* paramVelocity;
    InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity);

    if (paramVelocity)
    {
        LOG_DEBUG("MotionVectors exist..");

        auto& mvInfo = paramVelocity->Resource.ImageViewInfo;
        auto mvDesc = ffxApiGetImageResourceDescriptionVKLocal(paramVelocity);
        VkImage mvImage = mvInfo.Image;

        // Normalized 16-bit MV formats get copied into an R16G16_SFLOAT image
        if (mvInfo.Format == VK_FORMAT_R16G16_SNORM || mvInfo.Format == VK_FORMAT_R16G16_UNORM)
        {
            if (!_velocityConvert)
                _velocityConvert = std::make_unique<VelocityConvert_Vk>("VelocityConvert", Device, PhysicalDevice);

            if (_velocityConvert->CanRender() &&
                _velocityConvert->Dispatch(InCmdBuffer, mvInfo.ImageView, { mvInfo.Width, mvInfo.Height }))
            {
                mvImage = _velocityConvert->GetImage();
                mvDesc.format = FFX_API_SURFACE_FORMAT_R16G16_FLOAT;
            }
        }

        params.motionVectors = ffxApiGetResourceVK(mvImage, mvDesc, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("MotionVectors not exist!!");
        return false;
    }

    NVSDK_NGX_Resource_VK* paramOutput;
    InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput);

    if (paramOutput)
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

    NVSDK_NGX_Resource_VK* paramDepth;
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

    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &params.preExposure) != NVSDK_NGX_Result_Success)
        params.preExposure = 1.0f;

    if (Version() >= feature_version { 3, 1, 1 } && _velocity != Config::Instance()->FsrVelocity.value_or_default())
    {
        _velocity = Config::Instance()->FsrVelocity.value_or_default();
        ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
        m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
        m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FVELOCITYFACTOR;
        m_upscalerKeyValueConfig.ptr = &_velocity;
        auto result = _ffx->Configure(&_context, &m_upscalerKeyValueConfig.header);

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
            auto result = _ffx->Configure(&_context, &m_upscalerKeyValueConfig.header);

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
            auto result = _ffx->Configure(&_context, &m_upscalerKeyValueConfig.header);

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
            auto result = _ffx->Configure(&_context, &m_upscalerKeyValueConfig.header);

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
            auto result = _ffx->Configure(&_context, &m_upscalerKeyValueConfig.header);

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

    // Log what the driver provided FSR 4 gets, only when it changes
    if (_ffx == FSR4DriverProviderVk::Module())
    {
        auto describe = [](const char* name, NVSDK_NGX_Resource_VK* vkRes, const FfxApiResource& res)
        {
            if (res.resource == nullptr || vkRes == nullptr)
                return std::format("{}: none", name);

            return std::format("{}: {}x{} vk: {} ffx: {} usage: {:X}", name, res.description.width,
                               res.description.height, (int) vkRes->Resource.ImageViewInfo.Format,
                               res.description.format, res.description.usage);
        };

        auto activeModel = FSR4Models::ActiveModel(FSR4DriverProviderVk::Module()->dll);

        // preExposure is left out of the comparison as it can change every frame
        auto inputs = std::format(
            "model: {}, flags: {:X}, dispatch flags: {:X}\n  {}\n  {}\n  {}\n  {}\n  {}",
            activeModel ? FSR4Models::Name(*activeModel) : "None", _contextDesc.flags, params.flags,
            describe("color", paramColor, params.color), describe("depth", paramDepth, params.depth),
            describe("mv", paramVelocity, params.motionVectors), describe("exposure", paramExp, params.exposure),
            describe("output", paramOutput, params.output));

        if (inputs != _lastFsr4Inputs)
        {
            _lastFsr4Inputs = inputs;
            LOG_INFO("FSR 4 inputs, preExposure: {}, {}", params.preExposure, inputs);
        }
    }

    LOG_DEBUG("Dispatch!!");
    auto result = _ffx->Dispatch(&_context, &params.header);

    if (result != FFX_API_RETURN_OK)
    {
        LOG_ERROR("ffxFsr2ContextDispatch error: {0}", FfxApiProxy::ReturnCodeToString(result));
        return false;
    }

    return true;
}
