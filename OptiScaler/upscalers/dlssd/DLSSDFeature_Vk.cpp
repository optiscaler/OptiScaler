#include <pch.h>
#include <Config.h>
#include <Logger.h>

#include "DLSSDFeature_Vk.h"

bool DLSSDFeatureVk::Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
                          PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                          NVSDK_NGX_Parameter* InParameters)
{
    if (NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_ERROR("nvngx.dll not loaded!");

        SetInit(false);
        return false;
    }

    NVSDK_NGX_Result nvResult;
    bool initResult = false;

    Instance = InInstance;
    PhysicalDevice = InPD;
    Device = InDevice;

    do
    {
        if (!_dlssdInited)
        {
            _dlssdInited = NVNGXProxy::InitVulkan(InInstance, InPD, InDevice, InGIPA, InGDPA);

            if (!_dlssdInited)
                return false;

            _moduleLoaded =
                (NVNGXProxy::VULKAN_Init_ProjectID() != nullptr || NVNGXProxy::VULKAN_Init_Ext() != nullptr) &&
                (NVNGXProxy::VULKAN_Shutdown() != nullptr || NVNGXProxy::VULKAN_Shutdown1() != nullptr) &&
                (NVNGXProxy::VULKAN_GetParameters() != nullptr || NVNGXProxy::VULKAN_AllocateParameters() != nullptr) &&
                NVNGXProxy::VULKAN_DestroyParameters() != nullptr && NVNGXProxy::VULKAN_CreateFeature() != nullptr &&
                NVNGXProxy::VULKAN_ReleaseFeature() != nullptr && NVNGXProxy::VULKAN_EvaluateFeature() != nullptr;

            // delay between init and create feature
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        LOG_INFO("Creating DLSSD feature");

        if (NVNGXProxy::VULKAN_CreateFeature() != nullptr)
        {
            ProcessInitParams(InParameters);

            _p_dlssdHandle = &_dlssdHandle;
            nvResult = NVNGXProxy::VULKAN_CreateFeature()(InCmdList, NVSDK_NGX_Feature_RayReconstruction, InParameters,
                                                          &_p_dlssdHandle);

            if (nvResult != NVSDK_NGX_Result_Success)
            {
                LOG_ERROR("_CreateFeature result: {0:X}", (unsigned int) nvResult);
                break;
            }
            else
            {
                LOG_INFO("_CreateFeature result: NVSDK_NGX_Result_Success, HandleId: {0}", _p_dlssdHandle->Id);
            }
        }
        else
        {
            LOG_ERROR("_CreateFeature is nullptr");
            break;
        }

        ReadVersion();

        initResult = true;

    } while (false);

    SetInit(initResult);

    if (initResult)
    {
        if (RCAS == nullptr)
            RCAS = std::make_unique<RCAS_Vk>("RCAS", InDevice, InPD);

        if (OS == nullptr)
            OS = std::make_unique<OS_Vk>("OS", InDevice, InPD, (TargetWidth() < DisplayWidth()));
    }

    return initResult;
}

bool DLSSDFeatureVk::Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
{
    if (!_moduleLoaded)
    {
        LOG_ERROR("nvngx.dll or _nvngx.dll is not loaded!");
        return false;
    }

    if (!RCAS->IsInit())
        Config::Instance()->RcasEnabled.set_volatile_value(false);

    if (!OS->IsInit())
        Config::Instance()->OutputScalingEnabled.set_volatile_value(false);

    NVSDK_NGX_Result nvResult;

    if (NVNGXProxy::VULKAN_EvaluateFeature() != nullptr)
    {
        ProcessEvaluateParams(InParameters);

        NVSDK_NGX_Resource_VK* paramOutput = nullptr;

        VkImageView finalOutputView = VK_NULL_HANDLE;
        VkImage finalOutputImage = VK_NULL_HANDLE;

        _sharpness = GetSharpness(InParameters);
        float ssMulti = Config::Instance()->OutputScalingMultiplier.value_or(1.5f);
        bool useSS = Config::Instance()->OutputScalingEnabled.value_or_default() &&
                     (LowResMV() || RenderWidth() == DisplayWidth());

        bool rcasEnabled = Config::Instance()->RcasEnabled.value_or(true) &&
                           (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or(false) &&
                                                  Config::Instance()->MotionSharpness.value_or(0.4) > 0.0f)) &&
                           RCAS->CanRender();

        if (rcasEnabled && InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput) &&
            paramOutput == nullptr)
        {
            rcasEnabled = false;
            useSS = false;
        }

        if (rcasEnabled)
        {
            finalOutputView = paramOutput->Resource.ImageViewInfo.ImageView;
            finalOutputImage = paramOutput->Resource.ImageViewInfo.Image;

            InParameters->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
            VkImage oldImage = RCAS->GetImage();

            if (RCAS->CreateImageResource(
                    Device, PhysicalDevice, paramOutput->Resource.ImageViewInfo.Width,
                    paramOutput->Resource.ImageViewInfo.Height, paramOutput->Resource.ImageViewInfo.Format,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            {
                VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                if (oldImage != VK_NULL_HANDLE && oldImage == paramOutput->Resource.ImageViewInfo.Image)
                    oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                paramOutput->Resource.ImageViewInfo.Image = RCAS->GetImage();
                paramOutput->Resource.ImageViewInfo.ImageView = RCAS->GetImageView();

                VkImageSubresourceRange range {};
                range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                range.baseMipLevel = 0;
                range.levelCount = 1;
                range.baseArrayLayer = 0;
                range.layerCount = 1;

                RCAS->SetImageLayout(InCmdBuffer, paramOutput->Resource.ImageViewInfo.Image, oldLayout,
                                     VK_IMAGE_LAYOUT_GENERAL, range);
            }
            else
            {
                rcasEnabled = false;
            }
        }

        if (useSS)
        {
            if (finalOutputImage != RCAS->GetImage())
            {
                finalOutputView = paramOutput->Resource.ImageViewInfo.ImageView;
                finalOutputImage = paramOutput->Resource.ImageViewInfo.Image;
            }

            VkImage oldImage = OS->GetImage();

            if (OS->CreateImageResource(
                    Device, PhysicalDevice, TargetWidth(), TargetHeight(), paramOutput->Resource.ImageViewInfo.Format,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            {
                VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                if (oldImage != VK_NULL_HANDLE && oldImage == paramOutput->Resource.ImageViewInfo.Image)
                    oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                paramOutput->Resource.ImageViewInfo.Image = OS->GetImage();
                paramOutput->Resource.ImageViewInfo.ImageView = OS->GetImageView();

                VkImageSubresourceRange range {};
                range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                range.baseMipLevel = 0;
                range.levelCount = 1;
                range.baseArrayLayer = 0;
                range.layerCount = 1;

                OS->SetImageLayout(InCmdBuffer, paramOutput->Resource.ImageViewInfo.Image, oldLayout,
                                   VK_IMAGE_LAYOUT_GENERAL, range);
            }
            else
            {
                useSS = false;
            }
        }

        nvResult = NVNGXProxy::VULKAN_EvaluateFeature()(InCmdBuffer, _p_dlssdHandle, InParameters, NULL);

        if (nvResult != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("_EvaluateFeature result: {0:X}", (unsigned int) nvResult);
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
            NVSDK_NGX_Resource_VK* paramVelocity = nullptr;

            if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity) &&
                paramVelocity != nullptr)
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

                NVSDK_NGX_Resource_VK* paramDepth = nullptr;
                InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth);

                VkExtent2D outExtent = { DisplayWidth(), DisplayHeight() };

                RCAS->Dispatch(Device, InCmdBuffer, rcasConstants, RCAS->GetImageView(),
                               paramVelocity->Resource.ImageViewInfo.ImageView,
                               paramDepth != nullptr ? paramDepth->Resource.ImageViewInfo.ImageView : VK_NULL_HANDLE,
                               finalOutputView, outExtent);

                paramOutput->Resource.ImageViewInfo.Image = finalOutputImage;
                paramOutput->Resource.ImageViewInfo.ImageView = finalOutputView;
            }
        }
    }
    else
    {
        LOG_ERROR("_EvaluateFeature is nullptr");
        return false;
    }

    _frameCount++;

    return true;
}

DLSSDFeatureVk::DLSSDFeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters), DLSSDFeature(InHandleId, InParameters)
{
    if (NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_INFO("nvngx.dll not loaded, now loading");
        NVNGXProxy::InitNVNGX();
    }

    LOG_INFO("binding complete!");
}

DLSSDFeatureVk::~DLSSDFeatureVk()
{
    if (State::Instance().isShuttingDown)
        return;

    if (NVNGXProxy::VULKAN_ReleaseFeature() != nullptr && _p_dlssdHandle != nullptr)
        NVNGXProxy::VULKAN_ReleaseFeature()(_p_dlssdHandle);
}
