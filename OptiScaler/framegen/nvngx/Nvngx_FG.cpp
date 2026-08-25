#include "pch.h"

#include <NVNGX_Parameter.h>
#include "Nvngx_FG.h"

#include "proxies/NVNGX_Proxy.h"
#include "proxies/Ntdll_Proxy.h"

#include "IFGNvngx.h"
#include "Nvngx_Nukems.h"
#include "Nvngx_Arturs.h"
#include "Nvngx_FFX.h"
#include "Nvngx_Combo.h"
#include <imgui/ImGuiNotify.hpp>

IFGNvngx* Nvngx_FG::getProvider()
{
    if (_provider)
        return _provider.get();

    const auto selectedProvider = State::Instance().activeFgNvngx;

    switch (selectedProvider)
    {
    case FGNvngxReplacement::FFX:
        _provider = std::make_unique<Nvngx_FFX>();
        break;

    case FGNvngxReplacement::Nukems:
        _provider = std::make_unique<Nvngx_Nukems>();
        break;

    case FGNvngxReplacement::Arturs:
        _provider = std::make_unique<Nvngx_Arturs>();
        break;

    case FGNvngxReplacement::Combo:
        _provider = std::make_unique<Nvngx_Combo>();
        break;

    case FGNvngxReplacement::None:
    default:
        return nullptr;
    }

    if (!_provider->isDx12Available() && !_provider->isVulkanAvailable())
    {
        // The selected provider cannot be used, try the remaining providers as fallback, try in order
        // FGNvngxReplacement::Combo doesn't make sense to try as it's Arturs + FFX
        const FGNvngxReplacement fallbacks[] = {
            FGNvngxReplacement::Arturs,
            FGNvngxReplacement::FFX,
            FGNvngxReplacement::Nukems,
        };

        auto formatProvider = [](FGNvngxReplacement provider)
        {
            switch (provider)
            {
            case FGNvngxReplacement::Arturs:
                return "Enabler";
            case FGNvngxReplacement::FFX:
                return "Nvngx FFX";
            case FGNvngxReplacement::Nukems:
                return "Nukems";
            case FGNvngxReplacement::Combo:
                return "Combo";
            case FGNvngxReplacement::None:
            default:
                return "???";
            }
        };

        for (const auto fallback : fallbacks)
        {
            if (fallback == selectedProvider)
                continue;

            std::unique_ptr<IFGNvngx> candidate;

            switch (fallback)
            {
            case FGNvngxReplacement::Arturs:
                candidate = std::make_unique<Nvngx_Arturs>();
                break;

            case FGNvngxReplacement::FFX:
                candidate = std::make_unique<Nvngx_FFX>();
                break;

            case FGNvngxReplacement::Nukems:
                candidate = std::make_unique<Nvngx_Nukems>();
                break;

            case FGNvngxReplacement::None:
            default:
                continue;
            }

            if (!candidate->isDx12Available() && !candidate->isVulkanAvailable())
                continue;

            _provider = std::move(candidate);

            Config::Instance()->FGNvngxReplacement.set_volatile_value(fallback);
            State::Instance().activeFgNvngx = fallback;

            LOG_WARN("Nvngx FG provider {} is not available, falling back to {}", formatProvider(selectedProvider),
                     formatProvider(fallback));

            ImGui::InsertNotification({ ImGuiToastType::Warning, 20000,
                                        std::format("{} is not available.\nFalling back to {}.",
                                                    formatProvider(selectedProvider), formatProvider(fallback))
                                            .c_str() });

            return _provider.get();
        }

        LOG_ERROR("Nvngx FG provider {} is not available and can't fallback", formatProvider(selectedProvider));
        ImGui::InsertNotification(
            { ImGuiToastType::Error, 20000,
              std::format("{} is not available and can't fallback", formatProvider(selectedProvider)).c_str() });

        Config::Instance()->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::None);
        State::Instance().activeFgNvngx = FGNvngxReplacement::None;

        _provider.reset();
        return nullptr;
    }

    return _provider.get();
}

int Nvngx_FG::getMaxFakeFramesCount()
{
    auto* provider = getProvider();

    if (!provider)
        return false;

    return provider->getMaxFakeFramesCount();
}

bool Nvngx_FG::isDx12Available()
{
    auto* provider = getProvider();

    if (!provider)
        return false;

    return provider->isDx12Available();
}

bool Nvngx_FG::isVulkanAvailable()
{
    auto* provider = getProvider();

    if (!provider)
        return false;

    return provider->isVulkanAvailable();
}

feature_version Nvngx_FG::version()
{
    auto* provider = getProvider();

    if (!provider)
        return {};

    return provider->version();
}

feature_version Nvngx_FG::extraVersion()
{
    auto* provider = getProvider();

    if (!provider)
        return {};

    return provider->extraVersion();
}

void Nvngx_FG::setDebugView(bool enabled)
{
    auto* provider = getProvider();

    if (!provider)
        return;

    if (provider->getType() == FGNvngxReplacement::Nukems)
    {
        auto* nukemsProvider = static_cast<Nvngx_Nukems*>(provider);
        nukemsProvider->setDebugView(enabled);
    }
}

void Nvngx_FG::setInterpolatedOnly(bool enabled)
{
    auto* provider = getProvider();

    if (!provider)
        return;

    if (provider->getType() == FGNvngxReplacement::Nukems)
    {
        auto* nukemsProvider = static_cast<Nvngx_Nukems*>(provider);
        nukemsProvider->setInterpolatedOnly(enabled);
    }
}

NVSDK_NGX_Result Nvngx_FG::D3D12_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                      ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                      NVSDK_NGX_Version InSDKVersion)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->D3D12_Init(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
}

NVSDK_NGX_Result Nvngx_FG::D3D12_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                          ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                          const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);
}

NVSDK_NGX_Result Nvngx_FG::D3D12_Shutdown()
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->D3D12_Shutdown();
}

NVSDK_NGX_Result Nvngx_FG::D3D12_Shutdown1(ID3D12Device* InDevice)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->D3D12_Shutdown1(InDevice);
}

NVSDK_NGX_Result Nvngx_FG::D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                      const NVSDK_NGX_Parameter* InParameters, size_t* OutSizeInBytes)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->D3D12_GetScratchBufferSize(InFeatureId, InParameters, OutSizeInBytes);
}

NVSDK_NGX_Result Nvngx_FG::D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                               NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    if (!OutHandle)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    auto proxyHandle = new Nvngx_FG_Handle(lastIdCreated++ + NVNGX_PROVIDER_ID_OFFSET);

    std::scoped_lock lock(proxyHandle->handleMutex);

    auto result = provider->D3D12_CreateFeature(InCmdList, InFeatureID, InParameters, &proxyHandle->nativeHandle);

    *OutHandle = (NVSDK_NGX_Handle*) proxyHandle;

    // LOG_TRACE("Handle given to the game: {:X}", (uint64_t) *OutHandle);

    return result;
}

NVSDK_NGX_Result Nvngx_FG::D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    if (!InHandle)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (InHandle->Id < NVNGX_PROVIDER_ID_OFFSET)
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;

    // LOG_TRACE("Handle received from the game: {:X}", (uint64_t) InHandle);

    std::scoped_lock lock(((Nvngx_FG_Handle*) InHandle)->handleMutex);

    auto result = provider->D3D12_ReleaseFeature(((Nvngx_FG_Handle*) InHandle)->nativeHandle);

    if (result == NVSDK_NGX_Result_Success)
        delete InHandle;

    return result;
}

NVSDK_NGX_Result Nvngx_FG::D3D12_GetFeatureRequirements(IDXGIAdapter* Adapter,
                                                        const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                        NVSDK_NGX_FeatureRequirement* OutSupported)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->D3D12_GetFeatureRequirements(Adapter, FeatureDiscoveryInfo, OutSupported);
}

NVSDK_NGX_Result Nvngx_FG::D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                 const NVSDK_NGX_Handle* InFeatureHandle,
                                                 NVSDK_NGX_Parameter* InParameters,
                                                 PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    if (!InFeatureHandle)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (InFeatureHandle->Id < NVNGX_PROVIDER_ID_OFFSET)
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;

    std::shared_lock lock(((Nvngx_FG_Handle*) InFeatureHandle)->handleMutex);

    bool applyHudCutoff = Config::Instance()->FGHudCutoff.value_or_default() > 0.0f ||
                          State::Instance().gameQuirks & GameQuirk::FSRFGHudlessMismatchFixup;

    uint32_t frameIndex = 1;
    InParameters->Get("DLSSG.MultiFrameIndex", &frameIndex);

    if (applyHudCutoff && frameIndex == 1)
    {
        ID3D12Resource* presentWithHud = nullptr;
        InParameters->Get("DLSSG.Backbuffer", &presentWithHud);
        auto presentWithHudState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        ID3D12Resource* hudlessResource = nullptr;
        InParameters->Get("DLSSG.HUDLess", &hudlessResource);
        auto hudlessState = D3D12_RESOURCE_STATE_COPY_DEST;

        auto device = State::Instance().currentD3D12Device;

        if (presentWithHud && hudlessResource && device)
        {
            if (_hudCopy.get() == nullptr)
                _hudCopy = std::make_unique<HudCopy_Dx12>("HudCopy", device);

            if (auto hudCopy = _hudCopy.get(); hudCopy && hudCopy->IsInit())
            {
                // In Cyberprank - DLSSG has noise issues, FSR FG has noise + vignetting
                // In Death Stranding 2 - DLSSG has wrong colormapping it seems, FSR FG is fine
                const bool isCyberpunk = State::Instance().gameQuirks[GameQuirk::CyberpunkHudlessState];
                float hudDetectionThreshold = 0.03f;

                if (isCyberpunk && State::Instance().activeFgInput != FGInput::FSRFG)
                    hudDetectionThreshold = 0.01f;

                if (Config::Instance()->FGHudCutoff.value_or_default() > 0.0f)
                    hudDetectionThreshold = Config::Instance()->FGHudCutoff.value_or_default() / 10.0f;

                hudCopy->Dispatch(InCmdList, hudlessResource, presentWithHud, hudlessState, presentWithHudState,
                                  hudDetectionThreshold);
            }
        }
        else
        {
            LOG_WARN("Couldn't run hudless fixup");
        }
    }

    if (Config::Instance()->NvngxFGDisableHudless.value_or_default())
        InParameters->Set("DLSSG.HUDLess", (void*) nullptr);

    // LOG_TRACE("Handle received from the game: {:X}", (uint64_t) InFeatureHandle);

    return provider->D3D12_EvaluateFeature(InCmdList, ((Nvngx_FG_Handle*) InFeatureHandle)->nativeHandle, InParameters,
                                           InCallback);
}

NVSDK_NGX_Result Nvngx_FG::D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->D3D12_PopulateParameters_Impl(InParameters);
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                       VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                       PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                       const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo, NVSDK_NGX_Version InSDKVersion)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->VULKAN_Init(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice, InGIPA, InGDPA,
                                 InFeatureInfo, InSDKVersion);
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                           VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                           NVSDK_NGX_Version InSDKVersion,
                                           const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->VULKAN_Init_Ext(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice, InSDKVersion,
                                     InFeatureInfo);
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_Init_Ext2(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                            VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                            PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                            NVSDK_NGX_Version InSDKVersion,
                                            const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->VULKAN_Init_Ext2(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice, InGIPA,
                                      InGDPA, InSDKVersion, InFeatureInfo);
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_Shutdown()
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->VULKAN_Shutdown();
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_Shutdown1(VkDevice InDevice)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->VULKAN_Shutdown1(InDevice);
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                       const NVSDK_NGX_Parameter* InParameters, size_t* OutSizeInBytes)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->VULKAN_GetScratchBufferSize(InFeatureId, InParameters, OutSizeInBytes);
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_CreateFeature(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Feature InFeatureID,
                                                NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    if (!OutHandle)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    auto proxyHandle = new Nvngx_FG_Handle(lastIdCreated++ + NVNGX_PROVIDER_ID_OFFSET);
    auto result = provider->VULKAN_CreateFeature(InCmdBuffer, InFeatureID, InParameters, &proxyHandle->nativeHandle);

    *OutHandle = (NVSDK_NGX_Handle*) proxyHandle;

    // LOG_TRACE("Handle given to the game: {:X}", (uint64_t) *OutHandle);

    return result;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_CreateFeature1(VkDevice InDevice, VkCommandBuffer InCmdList,
                                                 NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter* InParameters,
                                                 NVSDK_NGX_Handle** OutHandle)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    if (!OutHandle)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    auto proxyHandle = new Nvngx_FG_Handle(lastIdCreated++ + NVNGX_PROVIDER_ID_OFFSET);
    auto result =
        provider->VULKAN_CreateFeature1(InDevice, InCmdList, InFeatureID, InParameters, &proxyHandle->nativeHandle);

    *OutHandle = (NVSDK_NGX_Handle*) proxyHandle;

    // LOG_TRACE("Handle given to the game: {:X}", (uint64_t) *OutHandle);

    return result;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    if (!InHandle)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (InHandle->Id < NVNGX_PROVIDER_ID_OFFSET)
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;

    // LOG_TRACE("Handle received from the game: {:X}", (uint64_t) InHandle);

    auto result = provider->VULKAN_ReleaseFeature(((Nvngx_FG_Handle*) InHandle)->nativeHandle);

    if (result == NVSDK_NGX_Result_Success)
        delete InHandle;

    return result;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_GetFeatureRequirements(const VkInstance Instance,
                                                         const VkPhysicalDevice PhysicalDevice,
                                                         const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                         NVSDK_NGX_FeatureRequirement* OutSupported)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->VULKAN_GetFeatureRequirements(Instance, PhysicalDevice, FeatureDiscoveryInfo, OutSupported);
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_EvaluateFeature(VkCommandBuffer InCmdList, const NVSDK_NGX_Handle* InFeatureHandle,
                                                  NVSDK_NGX_Parameter* InParameters,
                                                  PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    if (InFeatureHandle->Id < NVNGX_PROVIDER_ID_OFFSET)
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;

    // LOG_TRACE("Handle received from the game: {:X}", (uint64_t) InFeatureHandle);

    return provider->VULKAN_EvaluateFeature(InCmdList, InFeatureHandle, InParameters, InCallback);
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    auto* provider = getProvider();

    if (!provider)
        return NVSDK_NGX_Result_Fail;

    return provider->VULKAN_PopulateParameters_Impl(InParameters);
}
