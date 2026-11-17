#include <pch.h>
#include <Config.h>
#include <Logger.h>

#include "DLSSFeature_Vk.h"
#include <imgui/ImGuiNotify.hpp>

PFN_VULKAN_CreateFeature DLSSFeatureVk::CreateFeature() const
{
    if (_custom != nullptr)
        return _custom->vulkanInited ? _custom->module.VULKAN_CreateFeature : nullptr;

    return NVNGXProxy::VULKAN_CreateFeature();
}

PFN_VULKAN_EvaluateFeature DLSSFeatureVk::EvaluateFeature() const
{
    if (_custom != nullptr)
        return _custom->vulkanInited ? _custom->module.VULKAN_EvaluateFeature : nullptr;

    return NVNGXProxy::VULKAN_EvaluateFeature();
}

PFN_VULKAN_ReleaseFeature DLSSFeatureVk::ReleaseFeature() const
{
    if (_custom != nullptr)
        return _custom->vulkanInited ? _custom->module.VULKAN_ReleaseFeature : nullptr;

    return NVNGXProxy::VULKAN_ReleaseFeature();
}

bool DLSSFeatureVk::InitInternal(VkCommandBuffer InCmdList, NVSDK_NGX_Parameter* InParameters)
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
        if (!_dlssInited)
        {

            _dlssInited = _custom != nullptr
                              ? NVNGXProxy::InitCustomVulkan(*_custom, Instance, PhysicalDevice, Device, GIPA, GDPA)
                              : NVNGXProxy::InitVulkan(Instance, PhysicalDevice, Device, GIPA, GDPA);

            if (!_dlssInited)
                return false;

            // LoadCustom already checked the functions of a custom runtime
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
            }

            // delay between init and create feature
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        LOG_INFO("Creating DLSS feature");

        if (CreateFeature() != nullptr)
        {
            ProcessInitParams(InParameters);

            _p_dlssHandle = &_dlssHandle;
            nvResult = CreateFeature()(InCmdList, NVSDK_NGX_Feature_SuperSampling, InParameters, &_p_dlssHandle);

            if (nvResult != NVSDK_NGX_Result_Success)
            {
                LOG_ERROR("_CreateFeature result: {0:X}", (unsigned int) nvResult);
                _p_dlssHandle = nullptr; // NGX never issued it
                break;
            }
        }
        else
        {
            LOG_ERROR("_CreateFeature is nullptr");
            break;
        }

        ReadVersion(_custom != nullptr ? _custom->module.filePath : L"");

        initResult = true;

    } while (false);

    SetInit(initResult);

    return initResult;
}

bool DLSSFeatureVk::EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
{
    if (!_moduleLoaded)
    {
        LOG_ERROR("nvngx.dll or _nvngx.dll is not loaded!");
        return false;
    }

    if (EvaluateFeature() != nullptr)
    {
        ProcessEvaluateParams(InParameters);

        NVSDK_NGX_Result nvResult = EvaluateFeature()(InCmdBuffer, _p_dlssHandle, InParameters, NULL);

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

DLSSFeatureVk::DLSSFeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters,
                             const std::string& customUpscaler)
    : IFeature(InHandleId, InParameters), IFeature_Vk(InHandleId, InParameters), DLSSFeature(InHandleId, InParameters)
{
    if (!customUpscaler.empty())
    {
        _custom = NVNGXProxy::LoadCustom(customUpscaler, API::Vulkan);
        _moduleLoaded = _custom != nullptr;

        if (!_moduleLoaded)
            ImGui::InsertNotification(
                { ImGuiToastType::Error, 10000, "Can't load custom upscaler %s", customUpscaler.c_str() });

        return;
    }

    if (NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_INFO("nvngx.dll not loaded, now loading");
        NVNGXProxy::InitNVNGX();
    }

    LOG_INFO("binding complete!");
}

DLSSFeatureVk::~DLSSFeatureVk()
{
    if (State::Instance().isShuttingDown)
        return;

    if (ReleaseFeature() != nullptr && _p_dlssHandle != nullptr)
        ReleaseFeature()(_p_dlssHandle);
}
