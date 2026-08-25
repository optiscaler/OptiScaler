#pragma once

#include <NVNGX_Parameter.h>

#include "proxies/NVNGX_Proxy.h"
#include "proxies/Ntdll_Proxy.h"
#include <shaders/hud_copy/HudCopy_Dx12.h>
#include "IFGNvngx.h"

class Nvngx_FG
{
  private:
    // TODO: store an list of all the handles and lookup,
    // in case the game gives us a new handle each time and only id is correct
    struct Nvngx_FG_Handle
    {
        unsigned int id;
        NVSDK_NGX_Handle* nativeHandle = nullptr;
        std::shared_mutex handleMutex {}; // FFX is not mutex'ed and replies on this
    };

    static inline std::atomic_uint32_t lastIdCreated = 0;
    static inline std::unique_ptr<IFGNvngx> _provider;
    static inline std::unique_ptr<HudCopy_Dx12> _hudCopy;

    static IFGNvngx* getProvider();

  public:
    static int getMaxFakeFramesCount();
    static bool isDx12Available();
    static bool isVulkanAvailable();
    static feature_version version();
    static feature_version extraVersion();

    // TODO: nukem-specific, unify
    static void setDebugView(bool enabled);
    static void setInterpolatedOnly(bool enabled);

    static NVSDK_NGX_Result D3D12_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                       ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                       NVSDK_NGX_Version InSDKVersion);

    static NVSDK_NGX_Result D3D12_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                           ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                           const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);

    static NVSDK_NGX_Result D3D12_Shutdown();

    static NVSDK_NGX_Result D3D12_Shutdown1(ID3D12Device* InDevice);

    static NVSDK_NGX_Result D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                       const NVSDK_NGX_Parameter* InParameters, size_t* OutSizeInBytes);

    static NVSDK_NGX_Result D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                                NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle);

    static NVSDK_NGX_Result D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle);

    static NVSDK_NGX_Result D3D12_GetFeatureRequirements(IDXGIAdapter* Adapter,
                                                         const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                         NVSDK_NGX_FeatureRequirement* OutSupported);

    static NVSDK_NGX_Result D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                  const NVSDK_NGX_Handle* InFeatureHandle,
                                                  NVSDK_NGX_Parameter* InParameters,
                                                  PFN_NVSDK_NGX_ProgressCallback InCallback);

    static NVSDK_NGX_Result D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters);

    // Vulkan
    static NVSDK_NGX_Result VULKAN_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                        VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                        PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                        const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                        NVSDK_NGX_Version InSDKVersion);

    static NVSDK_NGX_Result VULKAN_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                            VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                            NVSDK_NGX_Version InSDKVersion,
                                            const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);

    static NVSDK_NGX_Result VULKAN_Init_Ext2(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                             VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                             PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                             NVSDK_NGX_Version InSDKVersion,
                                             const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);

    static NVSDK_NGX_Result VULKAN_Shutdown();

    static NVSDK_NGX_Result VULKAN_Shutdown1(VkDevice InDevice);

    static NVSDK_NGX_Result VULKAN_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                        const NVSDK_NGX_Parameter* InParameters,
                                                        size_t* OutSizeInBytes);

    static NVSDK_NGX_Result VULKAN_CreateFeature(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Feature InFeatureID,
                                                 NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle);

    static NVSDK_NGX_Result VULKAN_CreateFeature1(VkDevice InDevice, VkCommandBuffer InCmdList,
                                                  NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter* InParameters,
                                                  NVSDK_NGX_Handle** OutHandle);

    static NVSDK_NGX_Result VULKAN_ReleaseFeature(NVSDK_NGX_Handle* InHandle);

    static NVSDK_NGX_Result VULKAN_GetFeatureRequirements(const VkInstance Instance,
                                                          const VkPhysicalDevice PhysicalDevice,
                                                          const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                          NVSDK_NGX_FeatureRequirement* OutSupported);

    static NVSDK_NGX_Result VULKAN_EvaluateFeature(VkCommandBuffer InCmdList, const NVSDK_NGX_Handle* InFeatureHandle,
                                                   NVSDK_NGX_Parameter* InParameters,
                                                   PFN_NVSDK_NGX_ProgressCallback InCallback);

    static NVSDK_NGX_Result VULKAN_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters);
};
