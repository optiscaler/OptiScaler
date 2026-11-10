#include <pch.h>
#include <Config.h>
#include <Logger.h>

#include "DLSSDFeature_Vk.h"
#include <upscalers/nvngx/Nvngx_RR.h>
#include <imgui/ImGuiNotify.hpp>

bool DLSSDFeatureVk::InitInternal(VkCommandBuffer InCmdList, NVSDK_NGX_Parameter* InParameters)
{
    if (_custom == nullptr && NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_ERROR("nvngx.dll not loaded!");

        SetInit(false);
        return false;
    }

    NVSDK_NGX_Result nvResult;
    bool initResult = false;

    do
    {
        if (!_dlssdInited)
        {
            _dlssdInited = _custom != nullptr ? _custom->InitVulkan(Instance, PhysicalDevice, Device, GIPA, GDPA)
                                              : NVNGXProxy::InitVulkan(Instance, PhysicalDevice, Device, GIPA, GDPA);

            if (!_dlssdInited)
                return false;

            // Find already checked the functions of a custom DLSSD
            if (_custom == nullptr)
            {
                _moduleLoaded =
                    (NVNGXProxy::VULKAN_Init_ProjectID() != nullptr || NVNGXProxy::VULKAN_Init_Ext() != nullptr) &&
                    (NVNGXProxy::VULKAN_Shutdown() != nullptr || NVNGXProxy::VULKAN_Shutdown1() != nullptr) &&
                    (NVNGXProxy::VULKAN_GetParameters() != nullptr ||
                     NVNGXProxy::VULKAN_AllocateParameters() != nullptr) &&
                    NVNGXProxy::VULKAN_DestroyParameters() != nullptr &&
                    NVNGXProxy::VULKAN_CreateFeature() != nullptr && NVNGXProxy::VULKAN_ReleaseFeature() != nullptr &&
                    NVNGXProxy::VULKAN_EvaluateFeature() != nullptr;

                // delay between init and create feature
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }

        LOG_INFO("Creating DLSSD feature");

        ProcessInitParams(InParameters);

        _p_dlssdHandle = &_dlssdHandle;

        if (_custom != nullptr && _custom->ngx.VULKAN_CreateFeature1 != nullptr)
            nvResult = _custom->ngx.VULKAN_CreateFeature1(Device, InCmdList, NVSDK_NGX_Feature_RayReconstruction,
                                                          InParameters, &_p_dlssdHandle);
        else if (_custom != nullptr)
            nvResult = _custom->ngx.VULKAN_CreateFeature(InCmdList, NVSDK_NGX_Feature_RayReconstruction, InParameters,
                                                         &_p_dlssdHandle);
        else if (NVNGXProxy::VULKAN_CreateFeature() != nullptr)
            nvResult = NVNGXProxy::VULKAN_CreateFeature()(InCmdList, NVSDK_NGX_Feature_RayReconstruction, InParameters,
                                                          &_p_dlssdHandle);
        else
        {
            LOG_ERROR("_CreateFeature is nullptr");
            _p_dlssdHandle = nullptr;
            break;
        }

        if (nvResult != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("_CreateFeature result: {0:X}", (unsigned int) nvResult);
            _p_dlssdHandle = nullptr; // NGX never issued it
            break;
        }

        LOG_INFO("_CreateFeature result: NVSDK_NGX_Result_Success, HandleId: {0}", _p_dlssdHandle->Id);

        ReadVersion();

        initResult = true;

    } while (false);

    SetInit(initResult);

    return initResult;
}

bool DLSSDFeatureVk::EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
{
    if (!_moduleLoaded)
    {
        LOG_ERROR("nvngx.dll or _nvngx.dll is not loaded!");
        return false;
    }

    auto evaluateFeature = _custom != nullptr
                               ? (_custom->IsVulkanInited() ? _custom->ngx.VULKAN_EvaluateFeature : nullptr)
                               : NVNGXProxy::VULKAN_EvaluateFeature();

    if (evaluateFeature != nullptr)
    {
        ProcessEvaluateParams(InParameters);

        NVSDK_NGX_Result nvResult = evaluateFeature(InCmdBuffer, _p_dlssdHandle, InParameters, NULL);

        if (nvResult != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("_EvaluateFeature result: {0:X}", (unsigned int) nvResult);
            return false;
        }
    }
    else
    {
        LOG_ERROR("_EvaluateFeature is nullptr");
        return false;
    }

    return true;
}

DLSSDFeatureVk::DLSSDFeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters, const std::string& customRR)
    : IFeature(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters), DLSSDFeature(InHandleId, InParameters)
{
    if (!customRR.empty())
    {
        _customName = customRR;
        _custom = Nvngx_RR::Find(customRR, API::Vulkan);
        _moduleLoaded = _custom != nullptr;

        if (!_moduleLoaded)
            ImGui::InsertNotification({ ImGuiToastType::Error, 10000, "Can't load custom DLSSD %s", customRR.c_str() });

        return;
    }

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

    auto releaseFeature = _custom != nullptr
                              ? (_custom->IsVulkanInited() ? _custom->ngx.VULKAN_ReleaseFeature : nullptr)
                              : NVNGXProxy::VULKAN_ReleaseFeature();

    if (releaseFeature != nullptr && _p_dlssdHandle != nullptr)
        releaseFeature(_p_dlssdHandle);
}
