#pragma once
#include "IFGNvngx.h"
#include <proxies/NVNGX_Proxy.h>

class Nvngx_DllProxy : public IFGNvngx
{
  private:
    ID3D12Resource* depthCopy[2];

  protected:
    HMODULE dll = nullptr;

    PFN_D3D12_Init _DLSSG_D3D12_Init = nullptr;
    PFN_D3D12_Init_Ext _DLSSG_D3D12_Init_Ext = nullptr;
    PFN_D3D12_Shutdown _DLSSG_D3D12_Shutdown = nullptr;
    PFN_D3D12_Shutdown1 _DLSSG_D3D12_Shutdown1 = nullptr;
    PFN_D3D12_GetScratchBufferSize _DLSSG_D3D12_GetScratchBufferSize = nullptr;
    PFN_D3D12_CreateFeature _DLSSG_D3D12_CreateFeature = nullptr;
    PFN_D3D12_ReleaseFeature _DLSSG_D3D12_ReleaseFeature = nullptr;
    PFN_D3D12_GetFeatureRequirements _DLSSG_D3D12_GetFeatureRequirements = nullptr; // unused
    PFN_D3D12_EvaluateFeature _DLSSG_D3D12_EvaluateFeature = nullptr;
    PFN_D3D12_PopulateParameters_Impl _DLSSG_D3D12_PopulateParameters_Impl = nullptr;

    PFN_VULKAN_Init _DLSSG_VULKAN_Init = nullptr;
    PFN_VULKAN_Init_Ext _DLSSG_VULKAN_Init_Ext = nullptr;
    PFN_VULKAN_Init_Ext2 _DLSSG_VULKAN_Init_Ext2 = nullptr;
    PFN_VULKAN_Shutdown _DLSSG_VULKAN_Shutdown = nullptr;
    PFN_VULKAN_Shutdown1 _DLSSG_VULKAN_Shutdown1 = nullptr;
    PFN_VULKAN_GetScratchBufferSize _DLSSG_VULKAN_GetScratchBufferSize = nullptr;
    PFN_VULKAN_CreateFeature _DLSSG_VULKAN_CreateFeature = nullptr;
    PFN_VULKAN_CreateFeature1 _DLSSG_VULKAN_CreateFeature1 = nullptr;
    PFN_VULKAN_ReleaseFeature _DLSSG_VULKAN_ReleaseFeature = nullptr;
    PFN_VULKAN_GetFeatureRequirements _DLSSG_VULKAN_GetFeatureRequirements = nullptr; // unused
    PFN_VULKAN_EvaluateFeature _DLSSG_VULKAN_EvaluateFeature = nullptr;
    PFN_VULKAN_PopulateParameters_Impl _DLSSG_VULKAN_PopulateParameters_Impl = nullptr;

    virtual void LoadLibraries() = 0;

  public:
    Nvngx_DllProxy() = default;
    ~Nvngx_DllProxy()
    {
        if (dll)
            FreeLibrary(dll);

        SAFE_RELEASE(depthCopy[0]);
        SAFE_RELEASE(depthCopy[1]);
    };

    bool isDx12Available() override final { return _DLSSG_D3D12_Init != nullptr; };
    bool isVulkanAvailable() override final { return _DLSSG_VULKAN_Init != nullptr; };

    // D3D12
    NVSDK_NGX_Result D3D12_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                NVSDK_NGX_Version InSDKVersion) override;

    NVSDK_NGX_Result D3D12_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                    ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                    const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo) override;

    NVSDK_NGX_Result D3D12_Shutdown() override;

    NVSDK_NGX_Result D3D12_Shutdown1(ID3D12Device* InDevice) override;

    NVSDK_NGX_Result D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId, const NVSDK_NGX_Parameter* InParameters,
                                                size_t* OutSizeInBytes) override;

    NVSDK_NGX_Result D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                         NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle) override;

    NVSDK_NGX_Result D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle) override;

    NVSDK_NGX_Result D3D12_GetFeatureRequirements(IDXGIAdapter* Adapter,
                                                  const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                  NVSDK_NGX_FeatureRequirement* OutSupported) override;

    NVSDK_NGX_Result D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                           const NVSDK_NGX_Handle* InFeatureHandle, NVSDK_NGX_Parameter* InParameters,
                                           PFN_NVSDK_NGX_ProgressCallback InCallback) override;

    NVSDK_NGX_Result D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters) override;

    // Vulkan
    NVSDK_NGX_Result VULKAN_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                 VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                 PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                 const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                 NVSDK_NGX_Version InSDKVersion) override;

    NVSDK_NGX_Result VULKAN_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                     VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                     NVSDK_NGX_Version InSDKVersion,
                                     const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo) override;

    NVSDK_NGX_Result VULKAN_Init_Ext2(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                      VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                      PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                      NVSDK_NGX_Version InSDKVersion,
                                      const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo) override;

    NVSDK_NGX_Result VULKAN_Shutdown() override;

    NVSDK_NGX_Result VULKAN_Shutdown1(VkDevice InDevice) override;

    NVSDK_NGX_Result VULKAN_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId, const NVSDK_NGX_Parameter* InParameters,
                                                 size_t* OutSizeInBytes) override;

    NVSDK_NGX_Result VULKAN_CreateFeature(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Feature InFeatureID,
                                          NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle) override;

    NVSDK_NGX_Result VULKAN_CreateFeature1(VkDevice InDevice, VkCommandBuffer InCmdList, NVSDK_NGX_Feature InFeatureID,
                                           NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle) override;

    NVSDK_NGX_Result VULKAN_ReleaseFeature(NVSDK_NGX_Handle* InHandle) override;

    NVSDK_NGX_Result VULKAN_GetFeatureRequirements(const VkInstance Instance, const VkPhysicalDevice PhysicalDevice,
                                                   const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                   NVSDK_NGX_FeatureRequirement* OutSupported) override;

    NVSDK_NGX_Result VULKAN_EvaluateFeature(VkCommandBuffer InCmdList, const NVSDK_NGX_Handle* InFeatureHandle,
                                            NVSDK_NGX_Parameter* InParameters,
                                            PFN_NVSDK_NGX_ProgressCallback InCallback) override;

    NVSDK_NGX_Result VULKAN_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters) override;
};
