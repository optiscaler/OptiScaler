#pragma once
#include <NVNGX_Parameter.h>
#include <d3d12.h>
#include <vulkan/vulkan.h>
#include <OptiTypes.h>
#include <nvsdk_ngx_defs.h>
#include <State.h>

constexpr unsigned int NVNGX_PROVIDER_ID_OFFSET = 2000000;

class IFGNvngx
{
  public:
    virtual ~IFGNvngx() = default;

    virtual int getMaxFakeFramesCount(API api) = 0;
    virtual FGNvngxReplacement getType() = 0;
    virtual bool isDx12Available() = 0;
    virtual bool isVulkanAvailable() = 0;

    // D3D12
    virtual NVSDK_NGX_Result D3D12_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                        ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                        NVSDK_NGX_Version InSDKVersion) = 0;
    virtual NVSDK_NGX_Result D3D12_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                            ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                            const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo) = 0;
    virtual NVSDK_NGX_Result D3D12_Shutdown() = 0;
    virtual NVSDK_NGX_Result D3D12_Shutdown1(ID3D12Device* InDevice) = 0;
    virtual NVSDK_NGX_Result D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                        const NVSDK_NGX_Parameter* InParameters,
                                                        size_t* OutSizeInBytes) = 0;
    virtual NVSDK_NGX_Result D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                                 NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle) = 0;
    virtual NVSDK_NGX_Result D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle) = 0;
    virtual NVSDK_NGX_Result D3D12_GetFeatureRequirements(IDXGIAdapter* Adapter,
                                                          const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                          NVSDK_NGX_FeatureRequirement* OutSupported) = 0;
    virtual NVSDK_NGX_Result D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                   const NVSDK_NGX_Handle* InFeatureHandle,
                                                   NVSDK_NGX_Parameter* InParameters,
                                                   PFN_NVSDK_NGX_ProgressCallback InCallback) = 0;
    virtual NVSDK_NGX_Result D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters) = 0;

    // Vulkan
    virtual NVSDK_NGX_Result VULKAN_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                         VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                         PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                         const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                         NVSDK_NGX_Version InSDKVersion)
    {
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    };
    virtual NVSDK_NGX_Result VULKAN_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                             VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                             NVSDK_NGX_Version InSDKVersion,
                                             const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
    {
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    };
    virtual NVSDK_NGX_Result VULKAN_Init_Ext2(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                              VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                              PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                              NVSDK_NGX_Version InSDKVersion,
                                              const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
    {
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    };
    virtual NVSDK_NGX_Result VULKAN_Shutdown() { return NVSDK_NGX_Result_FAIL_FeatureNotSupported; };
    virtual NVSDK_NGX_Result VULKAN_Shutdown1(VkDevice InDevice) { return NVSDK_NGX_Result_FAIL_FeatureNotSupported; };
    virtual NVSDK_NGX_Result VULKAN_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                         const NVSDK_NGX_Parameter* InParameters,
                                                         size_t* OutSizeInBytes)
    {
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    };
    virtual NVSDK_NGX_Result VULKAN_CreateFeature(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Feature InFeatureID,
                                                  NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
    {
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    };
    virtual NVSDK_NGX_Result VULKAN_CreateFeature1(VkDevice InDevice, VkCommandBuffer InCmdList,
                                                   NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter* InParameters,
                                                   NVSDK_NGX_Handle** OutHandle)
    {
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    };
    virtual NVSDK_NGX_Result VULKAN_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
    {
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    };
    virtual NVSDK_NGX_Result VULKAN_GetFeatureRequirements(const VkInstance Instance,
                                                           const VkPhysicalDevice PhysicalDevice,
                                                           const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                           NVSDK_NGX_FeatureRequirement* OutSupported)
    {
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    };
    virtual NVSDK_NGX_Result VULKAN_EvaluateFeature(VkCommandBuffer InCmdList, const NVSDK_NGX_Handle* InFeatureHandle,
                                                    NVSDK_NGX_Parameter* InParameters,
                                                    PFN_NVSDK_NGX_ProgressCallback InCallback)
    {
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    };
    virtual NVSDK_NGX_Result VULKAN_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
    {
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    };
};
