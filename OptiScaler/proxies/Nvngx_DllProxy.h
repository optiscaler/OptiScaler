#pragma once
#include <framegen/nvngx/IFGNvngx.h>
#include <proxies/Nvngx_Proxy.h>

class Nvngx_DllProxy : public IFGNvngx
{
  private:
    ID3D12Resource* depthCopy[2] {};

  protected:
    NgxFeatureModule ngx;

    // What PopulateParameters reports before init
    int reportedMaxFakeFrames = 0;
    feature_version antighostingVersion {};

    virtual void LoadLibraries() = 0;

    // Fills reportedMaxFakeFrames and antighostingVersion from PopulateParameters, D3D12's or else Vulkan's
    void QueryReportedParameters();

    // DLSSG.ShowDebug and DLSSG.DispatchFlags from the config
    static void SetDispatchFlags(NVSDK_NGX_Parameter* InParameters);

  public:
    Nvngx_DllProxy() = default;
    ~Nvngx_DllProxy()
    {
        if (ngx.dll)
            FreeLibrary(ngx.dll);

        SAFE_RELEASE(depthCopy[0]);
        SAFE_RELEASE(depthCopy[1]);
    };

    bool isDx12Available() override final { return ngx.D3D12_Init != nullptr; };
    bool isVulkanAvailable() override final { return ngx.VULKAN_Init != nullptr; };

    // A provider reporting the antighosting (GhostBuster) version is DLSS Enabler's frame generation or passes its
    // parameters through, so it takes DLSS Enabler's DLSSG.DispatchFlags
    feature_version extraVersion() override { return antighostingVersion; }
    bool supportsDispatchFlags() override { return antighostingVersion.major != 0 || antighostingVersion.minor != 0; }

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
