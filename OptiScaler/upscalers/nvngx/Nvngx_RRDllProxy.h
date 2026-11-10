#pragma once
#include <proxies/NVNGX_Proxy.h>

// nvngx_dlssd replacement from custom/<name>/dlssd, runs Opti's DLSSD feature
class Nvngx_RRDllProxy
{
  private:
    HMODULE _dll = nullptr;

    // Shutdown and PopulateParameters are only passed to a dll that was inited
    bool _dx12Inited = false;
    bool _vulkanInited = false;

  public:
    std::wstring filePath;

    PFN_D3D12_Init D3D12_Init = nullptr;
    PFN_D3D12_Init_Ext D3D12_Init_Ext = nullptr;
    PFN_D3D12_Shutdown D3D12_Shutdown = nullptr;
    PFN_D3D12_Shutdown1 D3D12_Shutdown1 = nullptr;
    PFN_D3D12_CreateFeature D3D12_CreateFeature = nullptr;
    PFN_D3D12_ReleaseFeature D3D12_ReleaseFeature = nullptr;
    PFN_D3D12_GetFeatureRequirements D3D12_GetFeatureRequirements = nullptr;
    PFN_D3D12_EvaluateFeature D3D12_EvaluateFeature = nullptr;
    PFN_D3D12_PopulateParameters_Impl D3D12_PopulateParameters_Impl = nullptr;

    PFN_VULKAN_Init VULKAN_Init = nullptr;
    PFN_VULKAN_Init_Ext VULKAN_Init_Ext = nullptr;
    PFN_VULKAN_Init_Ext2 VULKAN_Init_Ext2 = nullptr;
    PFN_VULKAN_Shutdown VULKAN_Shutdown = nullptr;
    PFN_VULKAN_Shutdown1 VULKAN_Shutdown1 = nullptr;
    PFN_VULKAN_CreateFeature VULKAN_CreateFeature = nullptr;
    PFN_VULKAN_CreateFeature1 VULKAN_CreateFeature1 = nullptr;
    PFN_VULKAN_ReleaseFeature VULKAN_ReleaseFeature = nullptr;
    PFN_VULKAN_GetFeatureRequirements VULKAN_GetFeatureRequirements = nullptr;
    PFN_VULKAN_EvaluateFeature VULKAN_EvaluateFeature = nullptr;
    PFN_VULKAN_PopulateParameters_Impl VULKAN_PopulateParameters_Impl = nullptr;

    explicit Nvngx_RRDllProxy(const std::string& name);

    bool IsDx12Available() const
    {
        return (D3D12_Init_Ext != nullptr || D3D12_Init != nullptr) && D3D12_CreateFeature != nullptr &&
               D3D12_EvaluateFeature != nullptr && D3D12_ReleaseFeature != nullptr;
    }

    bool IsVulkanAvailable() const
    {
        return (VULKAN_Init_Ext2 != nullptr || VULKAN_Init != nullptr || VULKAN_Init_Ext != nullptr) &&
               (VULKAN_CreateFeature1 != nullptr || VULKAN_CreateFeature != nullptr) &&
               VULKAN_EvaluateFeature != nullptr && VULKAN_ReleaseFeature != nullptr;
    }

    bool IsDx12Inited() const { return _dx12Inited; }
    bool IsVulkanInited() const { return _vulkanInited; }

    // Inited once until the shutdown, with the game's NGX app info
    bool InitDx12(ID3D12Device* InDevice);
    bool InitVulkan(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, PFN_vkGetInstanceProcAddr InGIPA,
                    PFN_vkGetDeviceProcAddr InGDPA);

    void ShutdownDx12(ID3D12Device* InDevice);
    void ShutdownVulkan(VkDevice InDevice);
};
