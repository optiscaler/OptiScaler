#include <pch.h>
#include <Config.h>
#include "FSR2Feature_Vk_212.h"
#include "nvsdk_ngx_vk.h"
#include "MathUtils.h"

using namespace OptiMath;

bool FSR2FeatureVk212::InitFSR2(const NVSDK_NGX_Parameter* InParameters)
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
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

        auto scratchBufferSize = Fsr212::ffxFsr2GetScratchMemorySizeVK212(PhysicalDevice);
        void* scratchBuffer = calloc(scratchBufferSize, 1);

        auto errorCode = Fsr212::ffxFsr2GetInterfaceVK212(&_contextDesc.callbacks, scratchBuffer, scratchBufferSize,
                                                          PhysicalDevice, vkGetDeviceProcAddr);

        if (errorCode != Fsr212::FFX_OK)
        {
            LOG_ERROR("ffxGetInterfaceVK error: {0}", ResultToString212(errorCode));
            free(scratchBuffer);
            return false;
        }

        _contextDesc.device = Fsr212::ffxGetDeviceVK212(Device);

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
            _contextDesc.flags |= Fsr212::FFX_FSR2_ENABLE_DEPTH_INVERTED;

        if (AutoExposure())
            _contextDesc.flags |= Fsr212::FFX_FSR2_ENABLE_AUTO_EXPOSURE;

        if (IsHdr())
            _contextDesc.flags |= Fsr212::FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;

        if (JitteredMV())
            _contextDesc.flags |= Fsr212::FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

        if (!LowResMV())
            _contextDesc.flags |= Fsr212::FFX_FSR2_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;

        LOG_DEBUG("ffxFsr2ContextCreate!");

        auto ret = Fsr212::ffxFsr2ContextCreate212(&_context, &_contextDesc);

        if (ret != Fsr212::FFX_OK)
        {
            LOG_ERROR("ffxFsr2ContextCreate error: {0}", ResultToString212(ret));
            return false;
        }
    }

    SetInit(true);

    return true;
}

bool FSR2FeatureVk212::InitInternal(VkCommandBuffer InCmdList, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    return InitFSR2(InParameters);
}

void transitionImageToShaderReadOnly(VkCommandBuffer commandBuffer, VkImage image, VkFormat format,
                                     VkAccessFlagBits flag = VK_ACCESS_SHADER_READ_BIT)
{
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Previous layout is unknown or irrelevant
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    barrier.srcAccessMask = 0; // No previous accesses need to be waited on
    barrier.dstAccessMask = flag;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,     // Earliest possible stage
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // Transition to fragment shader stage
                         0,                                     // No special flags
                         0, nullptr,                            // No memory barriers
                         0, nullptr,                            // No buffer barriers
                         1, &barrier                            // One image barrier
    );
}

bool FSR2FeatureVk212::EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!IsInited())
        return false;

    auto& cfg = *Config::Instance();
    const auto& ngxParams = *InParameters;

    Fsr212::FfxFsr2DispatchDescription params {};

    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &params.jitterOffset.x);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &params.jitterOffset.y);

    unsigned int reset;
    InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset);
    params.reset = (reset == 1);

    GetRenderResolution(InParameters, &params.renderSize.width, &params.renderSize.height);

    LOG_DEBUG("Input Resolution: {0}x{1}", params.renderSize.width, params.renderSize.height);

    params.commandList = Fsr212::ffxGetCommandListVK212(InCmdBuffer);

    NVSDK_NGX_Resource_VK* paramColor;
    InParameters->Get(NVSDK_NGX_Parameter_Color, (void**) &paramColor);

    if (paramColor)
    {
        LOG_DEBUG("Color exist..");

        params.color = Fsr212::ffxGetTextureResourceVK212(
            &_context, paramColor->Resource.ImageViewInfo.Image, paramColor->Resource.ImageViewInfo.ImageView,
            paramColor->Resource.ImageViewInfo.Width, paramColor->Resource.ImageViewInfo.Height,
            paramColor->Resource.ImageViewInfo.Format, (wchar_t*) L"FSR2_Color",
            Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
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

        params.motionVectors = Fsr212::ffxGetTextureResourceVK212(
            &_context, paramVelocity->Resource.ImageViewInfo.Image, paramVelocity->Resource.ImageViewInfo.ImageView,
            paramVelocity->Resource.ImageViewInfo.Width, paramVelocity->Resource.ImageViewInfo.Height,
            paramVelocity->Resource.ImageViewInfo.Format, (wchar_t*) L"FSR2_MotionVectors",
            Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
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

        params.output = Fsr212::ffxGetTextureResourceVK212(
            &_context, paramOutput->Resource.ImageViewInfo.Image, paramOutput->Resource.ImageViewInfo.ImageView,
            paramOutput->Resource.ImageViewInfo.Width, paramOutput->Resource.ImageViewInfo.Height,
            paramOutput->Resource.ImageViewInfo.Format, (wchar_t*) L"FSR2_Output",
            Fsr212::FFX_RESOURCE_STATE_UNORDERED_ACCESS);
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

        params.depth = Fsr212::ffxGetTextureResourceVK212(
            &_context, paramDepth->Resource.ImageViewInfo.Image, paramDepth->Resource.ImageViewInfo.ImageView,
            paramDepth->Resource.ImageViewInfo.Width, paramDepth->Resource.ImageViewInfo.Height,
            paramDepth->Resource.ImageViewInfo.Format, (wchar_t*) L"FSR2_Depth",
            Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
    }
    else
    {
        LOG_ERROR("Depth not exist!!");
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

            params.exposure = Fsr212::ffxGetTextureResourceVK212(
                &_context, paramExp->Resource.ImageViewInfo.Image, paramExp->Resource.ImageViewInfo.ImageView,
                paramExp->Resource.ImageViewInfo.Width, paramExp->Resource.ImageViewInfo.Height,
                paramExp->Resource.ImageViewInfo.Format, (wchar_t*) L"FSR2_Exposure",
                Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
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
            params.transparencyAndComposition = Fsr212::ffxGetTextureResourceVK212(
                &_context, paramTransparency->Resource.ImageViewInfo.Image,
                paramTransparency->Resource.ImageViewInfo.ImageView, paramTransparency->Resource.ImageViewInfo.Width,
                paramTransparency->Resource.ImageViewInfo.Height, paramTransparency->Resource.ImageViewInfo.Format,
                (wchar_t*) L"FSR2_Reactive", Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
        }

        if (paramReactiveMask != nullptr)
        {
            LOG_DEBUG("Using FSR reactive mask..");
            params.reactive = Fsr212::ffxGetTextureResourceVK212(
                &_context, paramReactiveMask->Resource.ImageViewInfo.Image,
                paramReactiveMask->Resource.ImageViewInfo.ImageView, paramReactiveMask->Resource.ImageViewInfo.Width,
                paramReactiveMask->Resource.ImageViewInfo.Height, paramReactiveMask->Resource.ImageViewInfo.Format,
                (wchar_t*) L"FSR2_Reactive", Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
        }
        else
        {
            if (paramReactiveMask2 != nullptr)
            {
                LOG_DEBUG("Bias mask exist..");
                if (Config::Instance()->FsrUseMaskForTransparency.value_or_default())
                {
                    params.transparencyAndComposition = Fsr212::ffxGetTextureResourceVK212(
                        &_context, paramReactiveMask2->Resource.ImageViewInfo.Image,
                        paramReactiveMask2->Resource.ImageViewInfo.ImageView,
                        paramReactiveMask2->Resource.ImageViewInfo.Width,
                        paramReactiveMask2->Resource.ImageViewInfo.Height,
                        paramReactiveMask2->Resource.ImageViewInfo.Format, (wchar_t*) L"FSR2_Transparency",
                        Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
                }

                if (Config::Instance()->DlssReactiveMaskBias.value_or_default() > 0.0f)
                {
                    params.reactive = Fsr212::ffxGetTextureResourceVK212(
                        &_context, paramReactiveMask2->Resource.ImageViewInfo.Image,
                        paramReactiveMask2->Resource.ImageViewInfo.ImageView,
                        paramReactiveMask2->Resource.ImageViewInfo.Width,
                        paramReactiveMask2->Resource.ImageViewInfo.Height,
                        paramReactiveMask2->Resource.ImageViewInfo.Format, (wchar_t*) L"FSR2_Reactive",
                        Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
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

    LOG_DEBUG("Dispatch!!");
    auto result = Fsr212::ffxFsr2ContextDispatch212(&_context, &params);

    if (result != Fsr212::FFX_OK)
    {
        LOG_ERROR("ffxFsr2ContextDispatch error: {0}", ResultToString212(result));
        return false;
    }

    return true;
}
