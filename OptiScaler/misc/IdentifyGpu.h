#pragma once
#include <nvapi.h>
#include <proxies/D3D12_Proxy.h>
#include <device_info/device_info.hpp>

// vkd3d-proton
MIDL_INTERFACE("39da4e09-bd1c-4198-9fae-86bbe3be41fd")
ID3D12DXVKInteropDevice : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDXGIAdapter(REFIID iid, void** ppvObject) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetInstanceExtensions(UINT * pExtensionCount, const char** ppExtensions) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetDeviceExtensions(UINT * pExtensionCount, const char** ppExtensions) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetDeviceFeatures(const VkPhysicalDeviceFeatures2** ppFeatures) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetVulkanHandles(VkInstance * pVkInstance, VkPhysicalDevice * pVkPhysicalDevice,
                                                       VkDevice * pVkDevice) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetVulkanQueueInfo(ID3D12CommandQueue * pCommandQueue, VkQueue * pVkQueue,
                                                         UINT32 * pVkQueueFamily) = 0;

    virtual void STDMETHODCALLTYPE GetVulkanImageLayout(ID3D12Resource * pResource, D3D12_RESOURCE_STATES State,
                                                        VkImageLayout * pVkLayout) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetVulkanResourceInfo(ID3D12Resource * pResource, UINT64 * pVkHandle,
                                                            UINT64 * pBufferOffset) = 0;

    virtual HRESULT STDMETHODCALLTYPE LockCommandQueue(ID3D12CommandQueue * pCommandQueue) = 0;

    virtual HRESULT STDMETHODCALLTYPE UnlockCommandQueue(ID3D12CommandQueue * pCommandQueue) = 0;
};

// dxvk
MIDL_INTERFACE("3a6d8f2c-b0e8-4ab4-b4dc-4fd24891bfa5")
IDXGIVkInteropAdapter : public IUnknown
{
    virtual void STDMETHODCALLTYPE GetVulkanHandles(VkInstance * pInstance, VkPhysicalDevice * pPhysDev) = 0;
};

MIDL_INTERFACE("4c5e1b0d-b0c8-4131-bfd8-9b2476f7f408")
IDXGIVkInteropFactory : public IUnknown
{
    virtual void STDMETHODCALLTYPE GetVulkanInstance(VkInstance * pInstance,
                                                     PFN_vkGetInstanceProcAddr * ppfnVkGetInstanceProcAddr) = 0;
};

struct GpuInformation
{
    LUID luid {}; // Unique id to be able to reference the exact GPU
    std::string name {};
    VendorId::Value vendorId = VendorId::Invalid;
    uint32_t deviceId = 0x0;
    uint32_t subsystemId = 0x0;
    uint32_t revisionId = 0x0;
    size_t dedicatedVramInBytes = 0;
    // Only says which implementation backs the API, not that the game renders through it
    // Use IdentifyGpu::gameUsesDxvk/gameUsesVkd3dProton for that
    bool dxgiIsDxvk = false;         // Adapter was enumerated through DXVK's DXGI
    bool d3d12IsVkd3dProton = false; // D3D12 device creation goes through vkd3d-proton
    bool softwareAdapter = false;
    std::filesystem::path driverStore {};

    // AMD
    bool fsr4ForcedSupport = false;
    FSR4Support fsr4Support {};
    FSR4Support realFsr4Support {};
    device_info::HwGeneration amdHwGeneration = device_info::HwGeneration::kUndefinedGeneration;

    // Nvidia
    bool dlssCapable = false;
    NV_GPU_ARCH_INFO nvidiaArchInfo {};
    bool noDisplayConnected = false;
};

inline constexpr bool IsEqualLUID(LUID luid1, LUID luid2)
{
    return luid1.HighPart == luid2.HighPart && luid1.LowPart == luid2.LowPart;
}

class IdentifyGpu
{
    inline static bool hasD3d12Capabilities = false;
    inline static std::mutex mutex {};
    inline static std::vector<GpuInformation> cache {};

    static std::vector<GpuInformation> checkGpuInfo();
    static void queryNvapi(GpuInformation& gpuInfo);

  public:
    static void getHardwareAdapter(IDXGIFactory* InFactory, IDXGIAdapter** InAdapter,
                                   D3D_FEATURE_LEVEL requiredFeatureLevel);

    // Sorted by priority, the first one should be treated as the primary one
    static std::vector<GpuInformation> getAllGpus();
    static GpuInformation getPrimaryGpu();

    // Game is rendering through the translation layer, based on the API it's currently using
    static bool gameUsesDxvk();
    static bool gameUsesVkd3dProton();

    static void updateD3d12Capabilities(D3d12Proxy::PFN_D3D12CreateDevice o_D3D12CreateDevice = nullptr);
    static void updateInt8Support(std::optional<bool>& sdkSupportsInt8, std::optional<bool>& amdxcffx64SupportsInt8);
};
