#include <pch.h>
#include "FeatureProvider_Vk.h"

#include "Util.h"
#include "Config.h"

#include "NVNGX_Parameter.h"

#include "upscalers/fsr2/FSR2Feature_Vk.h"
#include "upscalers/dlss/DLSSFeature_Vk.h"
#include "upscalers/dlssd/DLSSDFeature_Vk.h"
#include "upscalers/fsr2_212/FSR2Feature_Vk_212.h"
#include "upscalers/fsr2_212/FSR2Feature_VkOnDx12_212.h"
#include "upscalers/fsr31/FSR31Feature_Vk.h"
#include "upscalers/xess/XeSSFeature_Vk.h"
#include "upscalers/fsr31/FSR31Feature_VkOn12.h"

bool FeatureProvider_Vk::GetFeature(std::string upscalerName, UINT handleId, NVSDK_NGX_Parameter* parameters,
                                    std::unique_ptr<IFeature_Vk>* feature)
{
    do
    {
        if (upscalerName == "xess")
        {
            *feature = std::make_unique<XeSSFeature_Vk>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr21")
        {
            *feature = std::make_unique<FSR2FeatureVk212>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr21_12")
        {
            *feature = std::make_unique<FSR2FeatureVkOnDx12_212>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr22")
        {
            *feature = std::make_unique<FSR2FeatureVk>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr31")
        {
            *feature = std::make_unique<FSR31FeatureVk>(handleId, parameters);
            break;
        }
        else if (upscalerName == "fsr31_12")
        {
            *feature = std::make_unique<FSR31FeatureVkOn12>(handleId, parameters);
            break;
        }

        if (Config::Instance()->DLSSEnabled.value_or_default())
        {
            if (upscalerName == "dlss" && State::Instance().NVNGX_DLSS_Path.has_value())
            {
                *feature = std::make_unique<DLSSFeatureVk>(handleId, parameters);
                break;
            }
            else if (upscalerName == "dlssd" && State::Instance().NVNGX_DLSSD_Path.has_value())
            {
                *feature = std::make_unique<DLSSDFeatureVk>(handleId, parameters);
                break;
            }
            else
            {
                *feature = std::make_unique<FSR2FeatureVk>(handleId, parameters);
            }
        }
        else
        {
            *feature = std::make_unique<FSR2FeatureVk>(handleId, parameters);
        }

    } while (false);

    if (!(*feature)->ModuleLoaded())
    {
        (*feature).reset();
        *feature = std::make_unique<FSR2FeatureVk>(handleId, parameters);
        upscalerName = "fsr22";
    }
    else
    {
        Config::Instance()->VulkanUpscaler = upscalerName;
    }

    auto result = (*feature)->ModuleLoaded();

    if (result)
    {
        if (upscalerName == "dlssd")
            upscalerName = "dlss";

        Config::Instance()->VulkanUpscaler = upscalerName;
    }

    return result;
}

bool FeatureProvider_Vk::ChangeFeature(std::string upscalerName, VkInstance instance, VkPhysicalDevice pd,
                                       VkDevice device, VkCommandBuffer cmdBuffer, PFN_vkGetInstanceProcAddr gipa,
                                       PFN_vkGetDeviceProcAddr gdpa, UINT handleId, NVSDK_NGX_Parameter* parameters,
                                       ContextData<IFeature_Vk>* contextData)
{
    if (State::Instance().newBackend == "" ||
        (!Config::Instance()->DLSSEnabled.value_or_default() && State::Instance().newBackend == "dlss"))
        State::Instance().newBackend = Config::Instance()->VulkanUpscaler.value_or_default();

    contextData->changeBackendCounter++;

    LOG_INFO("changeBackend is true, counter: {0}", contextData->changeBackendCounter);

    // first release everything
    if (contextData->changeBackendCounter == 1)
    {
        if (contextData->feature != nullptr)
        {
            LOG_INFO("changing backend to {0}", State::Instance().newBackend);

            auto dc = contextData->feature.get();

            if (State::Instance().newBackend != "dlssd" && State::Instance().newBackend != "dlss")
                contextData->createParams = GetNGXParameters("OptiVk");
            else
                contextData->createParams = parameters;

            contextData->ownsCreateParams = contextData->createParams != parameters;

            contextData->createParams->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, dc->GetFeatureFlags());
            contextData->createParams->Set(NVSDK_NGX_Parameter_Width, dc->RenderWidth());
            contextData->createParams->Set(NVSDK_NGX_Parameter_Height, dc->RenderHeight());
            contextData->createParams->Set(NVSDK_NGX_Parameter_OutWidth, dc->DisplayWidth());
            contextData->createParams->Set(NVSDK_NGX_Parameter_OutHeight, dc->DisplayHeight());
            contextData->createParams->Set(NVSDK_NGX_Parameter_PerfQualityValue, dc->PerfQualityValue());

            dc = nullptr;

            vkDeviceWaitIdle(device);

            State::Instance().currentFeature = nullptr;

            LOG_DEBUG("sleeping before reset of current feature for 1000ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            contextData->feature.reset();
            contextData->feature = nullptr;
        }
        else
        {
            LOG_ERROR("can't find handle {0} in VkContexts!", handleId);

            State::Instance().newBackend = "";
            State::Instance().changeBackend[handleId] = false;

            if (contextData->ownsCreateParams)
                delete static_cast<NVNGX_Parameters*>(contextData->createParams);

            contextData->createParams = nullptr;
            contextData->ownsCreateParams = false;

            contextData->changeBackendCounter = 0;
        }

        return NVSDK_NGX_Result_Success;
    }

    if (contextData->changeBackendCounter == 2)
    {
        LOG_INFO("Creating new {} upscaler", State::Instance().newBackend);

        contextData->feature.reset();

        if (!GetFeature(State::Instance().newBackend, handleId, contextData->createParams, &contextData->feature))
        {
            LOG_ERROR("Upscaler can't created");
            return false;
        }

        return true;
    }

    if (contextData->changeBackendCounter == 3)
    {
        // next frame create context
        auto initResult = false;
        {
            ScopedSkipSpoofing skipSpoofing;
            initResult =
                contextData->feature->Init(instance, pd, device, cmdBuffer, gipa, gdpa, contextData->createParams);
        }

        contextData->changeBackendCounter = 0;

        if (!initResult || !contextData->feature->ModuleLoaded())
        {
            LOG_ERROR("init failed with {0} feature", State::Instance().newBackend);

            if (State::Instance().newBackend != "dlssd")
            {
                if (Config::Instance()->VulkanUpscaler == "dlss")
                {
                    State::Instance().newBackend = "xess";
                }
                else
                {
                    State::Instance().newBackend = "fsr21";
                }
            }
            else
            {
                // Retry DLSSD
                State::Instance().newBackend = "dlssd";
            }

            State::Instance().changeBackend[handleId] = true;
            return NVSDK_NGX_Result_Success;
        }
        else
        {
            LOG_INFO("init successful for {0}, upscaler changed", State::Instance().newBackend);

            State::Instance().newBackend = "";
            State::Instance().changeBackend[handleId] = false;
        }

        // Only params created for the change, dlss and dlssd use the caller's
        if (contextData->ownsCreateParams)
        {
            delete static_cast<NVNGX_Parameters*>(contextData->createParams);
            contextData->createParams = nullptr;
            contextData->ownsCreateParams = false;
        }
    }

    // if initial feature can't be inited
    State::Instance().currentFeature = contextData->feature.get();

    return true;
}
