#pragma once

#include <NVNGX_Parameter.h>

#include "proxies/NVNGX_Proxy.h"
#include "proxies/Ntdll_Proxy.h"
#include <shaders/hud_copy/HudCopy_Dx12.h>

#define DLSSG_MOD_ID_OFFSET 2000000

typedef void (*PFN_RefreshGlobalConfiguration)();
typedef void (*PFN_EnableDebugView)(bool enable);

class Nvngx_FG
{
  private:
    inline static HMODULE _dllDx12 = nullptr;
    inline static HMODULE _dllVulkan = nullptr;

    inline static PFN_RefreshGlobalConfiguration _refreshGlobalConfiguration = nullptr;
    inline static PFN_EnableDebugView _fsrDebugView = nullptr; // for now keep compatibility with the patched 0.110

    inline static PFN_D3D12_Init _DLSSG_D3D12_Init = nullptr;
    inline static PFN_D3D12_Init_Ext _DLSSG_D3D12_Init_Ext = nullptr;
    inline static PFN_D3D12_Shutdown _DLSSG_D3D12_Shutdown = nullptr;
    inline static PFN_D3D12_Shutdown1 _DLSSG_D3D12_Shutdown1 = nullptr;
    inline static PFN_D3D12_GetScratchBufferSize _DLSSG_D3D12_GetScratchBufferSize = nullptr;
    inline static PFN_D3D12_CreateFeature _DLSSG_D3D12_CreateFeature = nullptr;
    inline static PFN_D3D12_ReleaseFeature _DLSSG_D3D12_ReleaseFeature = nullptr;
    inline static PFN_D3D12_GetFeatureRequirements _DLSSG_D3D12_GetFeatureRequirements = nullptr; // unused
    inline static PFN_D3D12_EvaluateFeature _DLSSG_D3D12_EvaluateFeature = nullptr;
    inline static PFN_D3D12_PopulateParameters_Impl _DLSSG_D3D12_PopulateParameters_Impl = nullptr;

    inline static PFN_VULKAN_Init _DLSSG_VULKAN_Init = nullptr;
    inline static PFN_VULKAN_Init_Ext _DLSSG_VULKAN_Init_Ext = nullptr;
    inline static PFN_VULKAN_Init_Ext2 _DLSSG_VULKAN_Init_Ext2 = nullptr;
    inline static PFN_VULKAN_Shutdown _DLSSG_VULKAN_Shutdown = nullptr;
    inline static PFN_VULKAN_Shutdown1 _DLSSG_VULKAN_Shutdown1 = nullptr;
    inline static PFN_VULKAN_GetScratchBufferSize _DLSSG_VULKAN_GetScratchBufferSize = nullptr;
    inline static PFN_VULKAN_CreateFeature _DLSSG_VULKAN_CreateFeature = nullptr;
    inline static PFN_VULKAN_CreateFeature1 _DLSSG_VULKAN_CreateFeature1 = nullptr;
    inline static PFN_VULKAN_ReleaseFeature _DLSSG_VULKAN_ReleaseFeature = nullptr;
    inline static PFN_VULKAN_GetFeatureRequirements _DLSSG_VULKAN_GetFeatureRequirements = nullptr; // unused
    inline static PFN_VULKAN_EvaluateFeature _DLSSG_VULKAN_EvaluateFeature = nullptr;
    inline static PFN_VULKAN_PopulateParameters_Impl _DLSSG_VULKAN_PopulateParameters_Impl = nullptr;

    inline static bool _dx12_inited = false;
    inline static bool _vulkan_inited = false;

    inline static bool _mfgDx12 = false;

    inline static std::unique_ptr<HudCopy_Dx12> _hudCopy;

    // Envvars that can be set:
    // DLSSGTOFSR3_EnableDebugOverlay
    // DLSSGTOFSR3_EnableDebugTearLines
    // DLSSGTOFSR3_EnableInterpolatedFramesOnly
    static void setSetting(const wchar_t* setting, const wchar_t* value);
    static HMODULE TryInitMFG();

  public:
    static void InitDLSSGMod_Dx12();

    static void InitDLSSGMod_Vulkan();

    static inline bool isLoaded(API api)
    {
        if (api == API::DX12)
            return _dllDx12 != nullptr;
        else if (api == API::Vulkan)
            return _dllVulkan != nullptr;

        return false;
    }

    // Essentially a check to see if we are using Artur's mod for nvngx fg
    static inline int getMaxFakeFramesCount(API api)
    {
        if (api == API::Vulkan || api == API::DX12 && !_mfgDx12)
            return 1;
        else if (api == API::DX12 && _mfgDx12)
            return 5;

        return 0;
    }

    static void setDebugView(bool enabled);

    static void setInterpolatedOnly(bool enabled);

    static inline bool is120orNewer() { return _refreshGlobalConfiguration != nullptr; }

    static inline PFN_EnableDebugView FSRDebugView() { return _fsrDebugView; }

    static inline bool isDx12Available() { return isLoaded(API::DX12) && _dx12_inited; }

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
    static inline bool isVulkanAvailable() { return isLoaded(API::Vulkan) && _vulkan_inited; }

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
