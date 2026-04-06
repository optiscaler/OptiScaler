#include <pch.h>
#include <Config.h>
#include "FSR2Feature_Vk.h"
#include "nvsdk_ngx_vk.h"
#include "MathUtils.h"

using namespace OptiMath;

bool FSR2FeatureVk::InitFSR2(const NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    if (PhysicalDevice == nullptr)
    {
        LOG_ERROR("PhysicalDevice is null!");
        return false;
    }

    {
        ScopedSkipSpoofing skipSpoofing {};

        auto scratchBufferSize = ffxFsr2GetScratchMemorySizeVK(PhysicalDevice);
        void* scratchBuffer = calloc(scratchBufferSize, 1);

        auto errorCode = ffxFsr2GetInterfaceVK(&_contextDesc.callbacks, scratchBuffer, scratchBufferSize,
                                               PhysicalDevice, vkGetDeviceProcAddr);

        if (errorCode != FFX_OK)
        {
            LOG_ERROR("ffxGetInterfaceVK error: {0}", ResultToString(errorCode));
            free(scratchBuffer);
            return false;
        }

        _contextDesc.device = ffxGetDeviceVK(Device);

        if (Config::Instance()->OutputScalingEnabled.value_or_default() &&
            (LowResMV() || RenderWidth() == DisplayWidth()))
        {
            float ssMulti = Config::Instance()->OutputScalingMultiplier.value_or_default();

            if (ssMulti < 0.5f)
            {
                ssMulti = 0.5f;
                Config::Instance()->OutputScalingMultiplier.set_volatile_value(ssMulti);
            }
            else if (ssMulti > 3.0f)
            {
                ssMulti = 3.0f;
                Config::Instance()->OutputScalingMultiplier.set_volatile_value(ssMulti);
            }

            _targetWidth = static_cast<unsigned int>(DisplayWidth() * ssMulti);
            _targetHeight = static_cast<unsigned int>(DisplayHeight() * ssMulti);
        }
        else
        {
            _targetWidth = DisplayWidth();
            _targetHeight = DisplayHeight();
        }

        // extended limits changes how resolution
        if (Config::Instance()->ExtendedLimits.value_or_default() && RenderWidth() > DisplayWidth())
        {
            _contextDesc.maxRenderSize.width = RenderWidth();
            _contextDesc.maxRenderSize.height = RenderHeight();

            Config::Instance()->OutputScalingMultiplier.set_volatile_value(1.0f);

            // if output scaling active let it to handle downsampling
            if (Config::Instance()->OutputScalingEnabled.value_or_default() &&
                (LowResMV() || RenderWidth() == DisplayWidth()))
            {

                _contextDesc.displaySize.width = _contextDesc.maxRenderSize.width;
                _contextDesc.displaySize.height = _contextDesc.maxRenderSize.height;

                // update target res
                _targetWidth = _contextDesc.maxRenderSize.width;
                _targetHeight = _contextDesc.maxRenderSize.height;
            }
            else
            {
                _contextDesc.displaySize.width = DisplayWidth();
                _contextDesc.displaySize.height = DisplayHeight();
            }
        }
        else
        {
            _contextDesc.maxRenderSize.width = TargetWidth() > DisplayWidth() ? TargetWidth() : DisplayWidth();
            _contextDesc.maxRenderSize.height = TargetHeight() > DisplayHeight() ? TargetHeight() : DisplayHeight();
            _contextDesc.displaySize.width = TargetWidth();
            _contextDesc.displaySize.height = TargetHeight();
        }

        _contextDesc.flags = 0;

        if (DepthInverted())
            _contextDesc.flags |= FFX_FSR2_ENABLE_DEPTH_INVERTED;

        if (AutoExposure())
            _contextDesc.flags |= FFX_FSR2_ENABLE_AUTO_EXPOSURE;

        if (IsHdr())
            _contextDesc.flags |= FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;

        if (JitteredMV())
            _contextDesc.flags |= FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

        if (!LowResMV())
            _contextDesc.flags |= FFX_FSR2_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;

#if _DEBUG
        _contextDesc.flags |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
        _contextDesc.fpMessage = FfxLogCallback;
#endif

        LOG_DEBUG("ffxFsr2ContextCreate!");

        auto ret = ffxFsr2ContextCreate(&_context, &_contextDesc);

        if (ret != FFX_OK)
        {
            LOG_ERROR("ffxFsr2ContextCreate error: {0}", ResultToString(ret));
            return false;
        }
    }

    SetInit(true);

    return true;
}

bool FSR2FeatureVk::Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
                         PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                         NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    Instance = InInstance;
    PhysicalDevice = InPD;
    Device = InDevice;
    GIPA = InGIPA;
    GDPA = InGDPA;

    if (RCAS == nullptr)
        RCAS = std::make_unique<RCAS_Vk>("RCAS", InDevice, InPD);

    if (OS == nullptr)
        OS = std::make_unique<OS_Vk>("OS", InDevice, InPD, (TargetWidth() < DisplayWidth()));

    return InitFSR2(InParameters);
}

bool FSR2FeatureVk::Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!IsInited())
        return false;

    auto& cfg = *Config::Instance();
    const auto& ngxParams = *InParameters;

    if (!RCAS->IsInit())
        Config::Instance()->RcasEnabled.set_volatile_value(false);

    if (!OS->IsInit())
        Config::Instance()->OutputScalingEnabled.set_volatile_value(false);

    FfxFsr2DispatchDescription params {};

    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &params.jitterOffset.x);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &params.jitterOffset.y);

    unsigned int reset;
    InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset);
    params.reset = (reset == 1);

    GetRenderResolution(InParameters, &params.renderSize.width, &params.renderSize.height);

    LOG_DEBUG("Input Resolution: {0}x{1}", params.renderSize.width, params.renderSize.height);

    params.commandList = ffxGetCommandListVK(InCmdBuffer);

    void* paramColor;
    InParameters->Get(NVSDK_NGX_Parameter_Color, &paramColor);

    if (paramColor)
    {
        LOG_DEBUG("Color exist..");

        params.color =
            ffxGetTextureResourceVK(&_context, ((NVSDK_NGX_Resource_VK*) paramColor)->Resource.ImageViewInfo.Image,
                                    ((NVSDK_NGX_Resource_VK*) paramColor)->Resource.ImageViewInfo.ImageView,
                                    ((NVSDK_NGX_Resource_VK*) paramColor)->Resource.ImageViewInfo.Width,
                                    ((NVSDK_NGX_Resource_VK*) paramColor)->Resource.ImageViewInfo.Height,
                                    ((NVSDK_NGX_Resource_VK*) paramColor)->Resource.ImageViewInfo.Format,
                                    (wchar_t*) L"FSR2_Color", FFX_RESOURCE_STATE_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("Color not exist!!");
        return false;
    }

    void* paramVelocity;
    InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramVelocity);

    if (paramVelocity)
    {
        LOG_DEBUG("MotionVectors exist..");

        params.motionVectors =
            ffxGetTextureResourceVK(&_context, ((NVSDK_NGX_Resource_VK*) paramVelocity)->Resource.ImageViewInfo.Image,
                                    ((NVSDK_NGX_Resource_VK*) paramVelocity)->Resource.ImageViewInfo.ImageView,
                                    ((NVSDK_NGX_Resource_VK*) paramVelocity)->Resource.ImageViewInfo.Width,
                                    ((NVSDK_NGX_Resource_VK*) paramVelocity)->Resource.ImageViewInfo.Height,
                                    ((NVSDK_NGX_Resource_VK*) paramVelocity)->Resource.ImageViewInfo.Format,
                                    (wchar_t*) L"FSR2_MotionVectors", FFX_RESOURCE_STATE_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("MotionVectors not exist!!");
        return false;
    }

    void* paramOutput;
    InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput);

    if (paramOutput)
    {
        LOG_DEBUG("Output exist..");

        params.output =
            ffxGetTextureResourceVK(&_context, ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Image,
                                    ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.ImageView,
                                    ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Width,
                                    ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Height,
                                    ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Format,
                                    (wchar_t*) L"FSR2_Output", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    else
    {
        LOG_ERROR("Output not exist!!");
        return false;
    }

    void* paramDepth;
    InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth);

    if (paramDepth)
    {
        LOG_DEBUG("Depth exist..");

        params.depth =
            ffxGetTextureResourceVK(&_context, ((NVSDK_NGX_Resource_VK*) paramDepth)->Resource.ImageViewInfo.Image,
                                    ((NVSDK_NGX_Resource_VK*) paramDepth)->Resource.ImageViewInfo.ImageView,
                                    ((NVSDK_NGX_Resource_VK*) paramDepth)->Resource.ImageViewInfo.Width,
                                    ((NVSDK_NGX_Resource_VK*) paramDepth)->Resource.ImageViewInfo.Height,
                                    ((NVSDK_NGX_Resource_VK*) paramDepth)->Resource.ImageViewInfo.Format,
                                    (wchar_t*) L"FSR2_Depth", FFX_RESOURCE_STATE_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("Depth not exist!!");
        return false;
    }

    void* paramExp = nullptr;
    if (AutoExposure())
    {
        LOG_DEBUG("AutoExposure enabled!");
    }
    else
    {
        InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, &paramExp);

        if (paramExp)
        {
            LOG_DEBUG("ExposureTexture exist..");

            params.exposure =
                ffxGetTextureResourceVK(&_context, ((NVSDK_NGX_Resource_VK*) paramExp)->Resource.ImageViewInfo.Image,
                                        ((NVSDK_NGX_Resource_VK*) paramExp)->Resource.ImageViewInfo.ImageView,
                                        ((NVSDK_NGX_Resource_VK*) paramExp)->Resource.ImageViewInfo.Width,
                                        ((NVSDK_NGX_Resource_VK*) paramExp)->Resource.ImageViewInfo.Height,
                                        ((NVSDK_NGX_Resource_VK*) paramExp)->Resource.ImageViewInfo.Format,
                                        (wchar_t*) L"FSR2_Exposure", FFX_RESOURCE_STATE_COMPUTE_READ);
        }
        else
        {
            LOG_DEBUG("AutoExposure disabled but ExposureTexture is not exist, it may cause problems!!");
            State::Instance().AutoExposure = true;
            State::Instance().changeBackend[Handle()->Id] = true;
            return true;
        }
    }

    void* paramTransparency = nullptr;
    InParameters->Get("FSR.transparencyAndComposition", &paramTransparency);

    void* paramReactiveMask = nullptr;
    InParameters->Get("FSR.reactive", &paramReactiveMask);

    void* paramReactiveMask2 = nullptr;
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &paramReactiveMask2);

    if (!Config::Instance()->DisableReactiveMask.value_or(paramReactiveMask == nullptr &&
                                                          paramReactiveMask2 == nullptr))
    {
        if (paramTransparency != nullptr)
        {
            LOG_DEBUG("Using FSR transparency mask..");
            params.transparencyAndComposition = ffxGetTextureResourceVK(
                &_context, ((NVSDK_NGX_Resource_VK*) paramTransparency)->Resource.ImageViewInfo.Image,
                ((NVSDK_NGX_Resource_VK*) paramTransparency)->Resource.ImageViewInfo.ImageView,
                ((NVSDK_NGX_Resource_VK*) paramTransparency)->Resource.ImageViewInfo.Width,
                ((NVSDK_NGX_Resource_VK*) paramTransparency)->Resource.ImageViewInfo.Height,
                ((NVSDK_NGX_Resource_VK*) paramTransparency)->Resource.ImageViewInfo.Format,
                (wchar_t*) L"FSR2_Reactive", FFX_RESOURCE_STATE_COMPUTE_READ);
        }

        if (paramReactiveMask != nullptr)
        {
            LOG_DEBUG("Using FSR reactive mask..");
            params.reactive = ffxGetTextureResourceVK(
                &_context, ((NVSDK_NGX_Resource_VK*) paramReactiveMask)->Resource.ImageViewInfo.Image,
                ((NVSDK_NGX_Resource_VK*) paramReactiveMask)->Resource.ImageViewInfo.ImageView,
                ((NVSDK_NGX_Resource_VK*) paramReactiveMask)->Resource.ImageViewInfo.Width,
                ((NVSDK_NGX_Resource_VK*) paramReactiveMask)->Resource.ImageViewInfo.Height,
                ((NVSDK_NGX_Resource_VK*) paramReactiveMask)->Resource.ImageViewInfo.Format,
                (wchar_t*) L"FSR2_Reactive", FFX_RESOURCE_STATE_COMPUTE_READ);
        }
        else
        {
            if (paramReactiveMask2 != nullptr)
            {
                LOG_DEBUG("Bias mask exist..");
                if (Config::Instance()->FsrUseMaskForTransparency.value_or_default())
                {
                    params.transparencyAndComposition = ffxGetTextureResourceVK(
                        &_context, ((NVSDK_NGX_Resource_VK*) paramReactiveMask2)->Resource.ImageViewInfo.Image,
                        ((NVSDK_NGX_Resource_VK*) paramReactiveMask2)->Resource.ImageViewInfo.ImageView,
                        ((NVSDK_NGX_Resource_VK*) paramReactiveMask2)->Resource.ImageViewInfo.Width,
                        ((NVSDK_NGX_Resource_VK*) paramReactiveMask2)->Resource.ImageViewInfo.Height,
                        ((NVSDK_NGX_Resource_VK*) paramReactiveMask2)->Resource.ImageViewInfo.Format,
                        (wchar_t*) L"FSR2_Transparency", FFX_RESOURCE_STATE_COMPUTE_READ);
                }

                if (Config::Instance()->DlssReactiveMaskBias.value_or_default() > 0.0f)
                {
                    params.reactive = ffxGetTextureResourceVK(
                        &_context, ((NVSDK_NGX_Resource_VK*) paramReactiveMask2)->Resource.ImageViewInfo.Image,
                        ((NVSDK_NGX_Resource_VK*) paramReactiveMask2)->Resource.ImageViewInfo.ImageView,
                        ((NVSDK_NGX_Resource_VK*) paramReactiveMask2)->Resource.ImageViewInfo.Width,
                        ((NVSDK_NGX_Resource_VK*) paramReactiveMask2)->Resource.ImageViewInfo.Height,
                        ((NVSDK_NGX_Resource_VK*) paramReactiveMask2)->Resource.ImageViewInfo.Format,
                        (wchar_t*) L"FSR2_Reactive", FFX_RESOURCE_STATE_COMPUTE_READ);
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

    VkImageView finalOutputView = ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.ImageView;
    VkImage finalOutputImage = ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Image;

    _sharpness = GetSharpness(InParameters);
    float ssMulti = Config::Instance()->OutputScalingMultiplier.value_or(1.5f);
    bool useSS =
        Config::Instance()->OutputScalingEnabled.value_or_default() && (LowResMV() || RenderWidth() == DisplayWidth());

    bool rcasEnabled = Config::Instance()->RcasEnabled.value_or(true) &&
                       (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or(false) &&
                                              Config::Instance()->MotionSharpness.value_or(0.4) > 0.0f)) &&
                       RCAS->CanRender();

    if (rcasEnabled)
    {
        VkImage oldImage = RCAS->GetImage();

        if (RCAS->CreateImageResource(
                Device, PhysicalDevice, ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Width,
                ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Height,
                ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Format,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        {
            VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (oldImage != VK_NULL_HANDLE && oldImage == params.output.resource)
                oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            params.output =
                ffxGetTextureResourceVK(&_context, RCAS->GetImage(), RCAS->GetImageView(),
                                        ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Width,
                                        ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Height,
                                        ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Format,
                                        (wchar_t*) L"FSR2_Output", FFX_RESOURCE_STATE_UNORDERED_ACCESS);

            VkImageSubresourceRange range {};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            RCAS->SetImageLayout(InCmdBuffer, RCAS->GetImage(), oldLayout, VK_IMAGE_LAYOUT_GENERAL, range);
        }
        else
        {
            rcasEnabled = false;
        }
    }

    if (useSS)
    {
        VkImage oldImage = OS->GetImage();

        if (OS->CreateImageResource(Device, PhysicalDevice, TargetWidth(), TargetHeight(),
                                    ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Format,
                                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        {
            VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (oldImage != VK_NULL_HANDLE && oldImage == params.output.resource)
                oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            params.output =
                ffxGetTextureResourceVK(&_context, OS->GetImage(), OS->GetImageView(),
                                        ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Width,
                                        ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Height,
                                        ((NVSDK_NGX_Resource_VK*) paramOutput)->Resource.ImageViewInfo.Format,
                                        (wchar_t*) L"FSR2_Output", FFX_RESOURCE_STATE_UNORDERED_ACCESS);

            VkImageSubresourceRange range {};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            OS->SetImageLayout(InCmdBuffer, OS->GetImage(), oldLayout, VK_IMAGE_LAYOUT_GENERAL, range);
        }
        else
        {
            useSS = false;
        }
    }

    _hasColor = params.color.resource != nullptr;
    _hasDepth = params.depth.resource != nullptr;
    _hasMV = params.motionVectors.resource != nullptr;
    _hasExposure = params.exposure.resource != nullptr;
    _hasTM = params.transparencyAndComposition.resource != nullptr;
    _accessToReactiveMask = paramReactiveMask != nullptr || paramReactiveMask2 != nullptr;
    _hasOutput = params.output.resource != nullptr;

    float MVScaleX = 1.0f;
    float MVScaleY = 1.0f;

    if (InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &MVScaleX) == NVSDK_NGX_Result_Success &&
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &MVScaleY) == NVSDK_NGX_Result_Success)
    {
        params.motionVectorScale.x = MVScaleX;
        params.motionVectorScale.y = MVScaleY;
    }
    else
    {
        LOG_WARN("Can't get motion vector scales!");

        params.motionVectorScale.x = MVScaleX;
        params.motionVectorScale.y = MVScaleY;
    }

    if (rcasEnabled)
    {
        params.enableSharpening = false;
        params.sharpness = 0.0f;
    }
    else
    {
        if (Config::Instance()->OverrideSharpness.value_or_default())
        {
            params.enableSharpening = Config::Instance()->Sharpness.value_or_default() > 0.0f;
            params.sharpness = Config::Instance()->Sharpness.value_or_default();
        }
        else
        {
            float shapness = 0.0f;
            if (InParameters->Get(NVSDK_NGX_Parameter_Sharpness, &shapness) == NVSDK_NGX_Result_Success)
            {
                _sharpness = shapness;

                params.enableSharpening = shapness > 0.0f;

                if (params.enableSharpening)
                {
                    if (shapness > 1.0f)
                        params.sharpness = 1.0f;
                    else
                        params.sharpness = shapness;
                }
            }
        }
    }

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

    LOG_DEBUG("Dispatch!!");
    auto result = ffxFsr2ContextDispatch(&_context, &params);

    if (result != FFX_OK)
    {
        LOG_ERROR("ffxFsr2ContextDispatch error: {0}", ResultToString(result));
        return false;
    }

    if (useSS)
    {
        VkImageSubresourceRange range {};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        OS->SetImageLayout(InCmdBuffer, OS->GetImage(), VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

        VkExtent2D outExtent = { DisplayWidth(), DisplayHeight() };

        if (!rcasEnabled)
            OS->Dispatch(Device, InCmdBuffer, OS->GetImageView(), finalOutputView, outExtent);
        else
            OS->Dispatch(Device, InCmdBuffer, OS->GetImageView(), RCAS->GetImageView(), outExtent);
    }

    if (rcasEnabled)
    {
        VkImageSubresourceRange range {};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        RCAS->SetImageLayout(InCmdBuffer, RCAS->GetImage(), VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

        RcasConstants rcasConstants {};
        rcasConstants.Sharpness = _sharpness;
        rcasConstants.DisplayWidth = TargetWidth();
        rcasConstants.DisplayHeight = TargetHeight();
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &rcasConstants.MvScaleX);
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &rcasConstants.MvScaleY);
        rcasConstants.DisplaySizeMV = !(GetFeatureFlags() & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes);
        rcasConstants.RenderHeight = RenderHeight();
        rcasConstants.RenderWidth = RenderWidth();
        rcasConstants.DepthInverted = DepthInverted();

        VkExtent2D outExtent = { DisplayWidth(), DisplayHeight() };

        RCAS->Dispatch(Device, InCmdBuffer, rcasConstants, RCAS->GetImageView(),
                       ((NVSDK_NGX_Resource_VK*) paramVelocity)->Resource.ImageViewInfo.ImageView,
                       paramDepth != nullptr ? ((NVSDK_NGX_Resource_VK*) paramDepth)->Resource.ImageViewInfo.ImageView
                                             : VK_NULL_HANDLE,
                       finalOutputView, outExtent);
    }

    _frameCount++;

    return true;
}
