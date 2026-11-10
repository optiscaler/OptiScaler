#pragma once
#include <proxies/Nvngx_Proxy.h>

// nvngx_dlssd replacement from custom/<name>/dlssd, runs Opti's DLSSD feature
class Nvngx_RRDllProxy
{
  private:
    // Shutdown and PopulateParameters are only passed to a dll that was inited
    bool _dx12Inited = false;
    bool _vulkanInited = false;

  public:
    NgxFeatureModule ngx;

    explicit Nvngx_RRDllProxy(const std::string& name);

    bool IsDx12Available() const
    {
        return (ngx.D3D12_Init_Ext != nullptr || ngx.D3D12_Init != nullptr) && ngx.D3D12_CreateFeature != nullptr &&
               ngx.D3D12_EvaluateFeature != nullptr && ngx.D3D12_ReleaseFeature != nullptr;
    }

    bool IsVulkanAvailable() const
    {
        return (ngx.VULKAN_Init_Ext2 != nullptr || ngx.VULKAN_Init != nullptr || ngx.VULKAN_Init_Ext != nullptr) &&
               (ngx.VULKAN_CreateFeature1 != nullptr || ngx.VULKAN_CreateFeature != nullptr) &&
               ngx.VULKAN_EvaluateFeature != nullptr && ngx.VULKAN_ReleaseFeature != nullptr;
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
