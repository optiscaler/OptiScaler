#include <pch.h>
#include "FeatureProvider_Vk.h"

#include "Util.h"
#include "Config.h"

#include "NVNGX_Parameter.h"

#include "upscalers/fsr2/FSR2Feature_Vk.h"
#include "upscalers/dlss/DLSSFeature_Vk.h"
#include "upscalers/dlss/DLSSFeature_VkOnDx12.h"
#include "upscalers/dlssd/DLSSDFeature_Vk.h"
#include "upscalers/fsr2_212/FSR2Feature_Vk_212.h"
#include "upscalers/fsr2_212/FSR2Feature_VkOnDx12_212.h"
#include "upscalers/ffx/FFXFeature_Vk.h"
#include "upscalers/xess/XeSSFeature_Vk.h"
#include "upscalers/ffx/FFXFeature_VkOn12.h"
#include <misc/IdentifyGpu.h>
#include <misc/CustomProviders.h>
#include <upscalers/nvngx/Nvngx_RR.h>

bool FeatureProvider_Vk::GetFeature(Upscaler upscaler, UINT handleId, NVSDK_NGX_Parameter* parameters,
                                    std::unique_ptr<IFeature_Vk>* feature, bool rayReconstruction)
{
    State& state = State::Instance();
    Config& cfg = *Config::Instance();
    auto primaryGpu = IdentifyGpu::getPrimaryGpu();

    const auto custom = CustomProviders::Selected(API::Vulkan, upscaler);

    switch (upscaler)
    {
    case Upscaler::XeSS:
        *feature = std::make_unique<XeSSFeature_Vk>(handleId, parameters);
        break;

    case Upscaler::FSR21:
        *feature = std::make_unique<FSR2FeatureVk212>(handleId, parameters);
        break;

    case Upscaler::FSR21_on12:
        *feature = std::make_unique<FSR2FeatureVkOnDx12_212>(handleId, parameters);
        break;

    case Upscaler::FSR22:
        *feature = std::make_unique<FSR2FeatureVk>(handleId, parameters);
        break;

    case Upscaler::FFX:
        *feature = std::make_unique<FFXFeatureVk>(handleId, parameters, custom);
        break;

    case Upscaler::FFX_on12:
        *feature = std::make_unique<FFXFeatureVkOn12>(handleId, parameters, custom);
        break;

    case Upscaler::DLSS:
        // A custom DLSS doesn't need a DLSS capable GPU
        if (!custom.empty() || (primaryGpu.dlssCapable && state.NVNGX_DLSS_Path.has_value()))
        {
            *feature = std::make_unique<DLSSFeatureVk>(handleId, parameters, custom);
            break;
        }
        else
        {
            *feature = std::make_unique<FSR2FeatureVk>(handleId, parameters);
            upscaler = Upscaler::FSR22;
            break;
        }

    case Upscaler::DLSS_on12:
        if (primaryGpu.dlssCapable && state.NVNGX_DLSS_Path.has_value())
        {
            *feature = std::make_unique<DLSSFeatureVkOnDx12>(handleId, parameters);
            break;
        }
        else
        {
            *feature = std::make_unique<FSR2FeatureVk>(handleId, parameters);
            upscaler = Upscaler::FSR22;
            break;
        }

    case Upscaler::DLSSD:
        if (auto customRR = rayReconstruction ? Nvngx_RR::SelectedName(API::Vulkan) : std::string(); !customRR.empty())
        {
            *feature = std::make_unique<DLSSDFeatureVk>(handleId, parameters, customRR);
            break;
        }
        else if (primaryGpu.dlssCapable && state.NVNGX_DLSSD_Path.has_value())
        {
            *feature = std::make_unique<DLSSDFeatureVk>(handleId, parameters);
            break;
        }
        else
        {
            *feature = std::make_unique<FSR2FeatureVk>(handleId, parameters);
            upscaler = Upscaler::FSR22;
            break;
        }

    default:
        *feature = std::make_unique<FSR2FeatureVk>(handleId, parameters);
        upscaler = Upscaler::FSR22;
        break;
    }

    bool loaded = (*feature)->ModuleLoaded();

    if (!loaded)
    {
        // Fail after the constructor
        ImGui::InsertNotification({ ImGuiToastType::Warning, 10000, "Falling back to FSR 2.2" });
        *feature = std::make_unique<FSR2FeatureVk>(handleId, parameters);
        upscaler = Upscaler::FSR22;
        loaded = true; // Assuming the fallback always loads successfully
    }

    // Ray reconstruction's DLSSD isn't one of the upscalers, nor is a regular one picked for it
    if (rayReconstruction)
    {
        if (upscaler != Upscaler::DLSSD && !Nvngx_RR::Regular().has_value())
            Nvngx_RR::Fallback();

        return loaded;
    }

    cfg.VulkanUpscaler = upscaler;

    return loaded;
}

bool FeatureProvider_Vk::ChangeFeature(Upscaler upscaler, VkInstance instance, VkPhysicalDevice pd, VkDevice device,
                                       VkCommandBuffer cmdBuffer, PFN_vkGetInstanceProcAddr gipa,
                                       PFN_vkGetDeviceProcAddr gdpa, UINT handleId, NVSDK_NGX_Parameter* parameters,
                                       ContextData<IFeature_Vk>* contextData)
{
    State& state = State::Instance();
    Config& cfg = *Config::Instance();

    // Ray reconstruction runs on DLSSD, newBackend is left to the upscalers
    const bool rayReconstruction = state.rrHandles.contains(handleId);
    Upscaler rrBackend = Nvngx_RR::Backend(API::Vulkan);
    Upscaler& backend = rayReconstruction ? rrBackend : state.newBackend;

    const bool dlssOnNonCapable = !IdentifyGpu::getPrimaryGpu().dlssCapable &&
                                  (backend == Upscaler::DLSS || backend == Upscaler::DLSS_on12) &&
                                  CustomProviders::Selected(API::Vulkan, backend).empty();
    if (backend == Upscaler::Reset || dlssOnNonCapable)
        backend = cfg.VulkanUpscaler.value_or_default();

    contextData->changeBackendCounter++;

    LOG_INFO("changeBackend is true, counter: {0}", contextData->changeBackendCounter);

    // first release everything
    if (contextData->changeBackendCounter == 1)
    {
        if (contextData->feature != nullptr)
        {
            LOG_INFO("changing backend to {0}", UpscalerDisplayName(backend));

            auto* dc = contextData->feature.get();
            // Use given params if using DLSS passthrough
            const bool isPassthrough =
                backend == Upscaler::DLSSD || backend == Upscaler::DLSS || backend == Upscaler::DLSS_on12;

            // A retry after a failed init still has the previous ones
            if (!contextData->createParamsFromGame)
                TryDestroyNGXParameters(contextData->createParams, NVNGXProxy::VULKAN_DestroyParameters());

            contextData->createParams = isPassthrough ? parameters : GetNGXParameters(API::Vulkan, false);
            contextData->createParamsFromGame = isPassthrough;
            contextData->createParams->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, dc->GetFeatureFlags());
            contextData->createParams->Set(NVSDK_NGX_Parameter_Width, dc->RenderWidth());
            contextData->createParams->Set(NVSDK_NGX_Parameter_Height, dc->RenderHeight());
            contextData->createParams->Set(NVSDK_NGX_Parameter_OutWidth, dc->DisplayWidth());
            contextData->createParams->Set(NVSDK_NGX_Parameter_OutHeight, dc->DisplayHeight());
            contextData->createParams->Set(NVSDK_NGX_Parameter_PerfQualityValue, dc->PerfQualityValue());

            dc = nullptr;

            vkDeviceWaitIdle(device);

            State::Instance().currentFeature = nullptr;

            Util::DelayedDestroy(std::move(contextData->feature));

            // LOG_DEBUG("sleeping before reset of current feature for 1000ms");
            // std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            // contextData->feature.reset();
            // contextData->feature = nullptr;
        }
        else
        {
            LOG_ERROR("can't find handle {0} in VkContexts!", handleId);

            backend = Upscaler::Reset;
            state.changeBackend[handleId] = false;

            if (!contextData->createParamsFromGame)
                TryDestroyNGXParameters(contextData->createParams, NVNGXProxy::VULKAN_DestroyParameters());

            contextData->createParams = nullptr;

            contextData->changeBackendCounter = 0;

            return false;
        }

        return true;
    }

    if (contextData->changeBackendCounter == 2)
    {
        LOG_INFO("Creating new {} upscaler", UpscalerDisplayName(backend));

        contextData->feature.reset();

        if (!GetFeature(backend, handleId, contextData->createParams, &contextData->feature, rayReconstruction))
        {
            LOG_ERROR("Upscaler can't created");

            // End the change, phase 3 would Init a null feature
            contextData->feature.reset();
            backend = Upscaler::Reset;
            state.changeBackend[handleId] = false;

            if (!contextData->createParamsFromGame)
                TryDestroyNGXParameters(contextData->createParams, NVNGXProxy::VULKAN_DestroyParameters());

            contextData->createParams = nullptr;
            contextData->changeBackendCounter = 0;

            return false;
        }

        return true;
    }

    if (contextData->changeBackendCounter == 3)
    {
        // next frame create context
        auto initResult = false;
        {
            ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
            initResult =
                contextData->feature->Init(instance, pd, device, cmdBuffer, gipa, gdpa, contextData->createParams);
        }

        contextData->changeBackendCounter = 0;

        if (!initResult || !contextData->feature->ModuleLoaded())
        {
            LOG_ERROR("init failed with {0} feature", UpscalerDisplayName(backend));

            if (rayReconstruction)
            {
                // The FSR fallback isn't retried
                if (!Nvngx_RR::InitFailed(contextData->feature.get()))
                {
                    state.changeBackend[handleId] = false;
                    return false;
                }
            }
            else if (cfg.VulkanUpscaler == Upscaler::DLSS || cfg.VulkanUpscaler == Upscaler::DLSS_on12)
            {
                backend = Upscaler::XeSS;
                ImGui::InsertNotification({ ImGuiToastType::Warning, 10000, "Falling back to XeSS" });
            }
            else
            {
                backend = Upscaler::FSR21;
                ImGui::InsertNotification({ ImGuiToastType::Warning, 10000, "Falling back to FSR 2.1.2" });
            }

            state.changeBackend[handleId] = true;

            return false;
        }
        else
        {
            LOG_INFO("init successful for {0}, upscaler changed", UpscalerDisplayName(backend));

            backend = Upscaler::Reset;
            state.changeBackend[handleId] = false;
        }

        // If this is an OptiScaler fake NVNGX param table, delete it
        int optiParam = 0;

        if (!contextData->createParamsFromGame &&
            contextData->createParams->Get("OptiScaler", &optiParam) == NVSDK_NGX_Result_Success && optiParam == 1)
        {
            TryDestroyNGXParameters(contextData->createParams, NVNGXProxy::VULKAN_DestroyParameters());
            contextData->createParams = nullptr;
        }
    }

    // if initial feature can't be inited
    state.currentFeature = contextData->feature.get();

    return true;
}
