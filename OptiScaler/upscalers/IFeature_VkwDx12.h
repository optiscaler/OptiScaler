#pragma once
#include "IFeature_Vk.h"

#include <menu/menu_overlay_vk.h>

#include <shaders/rcas/RCAS_Dx12.h>
#include <shaders/bias/Bias_Dx12.h>
#include <shaders/output_scaling/OS_Dx12.h>
#include <shaders/depth_transfer/DT_Vk.h>
#include <shaders/resource_copy/RC_Vk.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <vulkan/vulkan.hpp>

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_win32.h>
#endif

#include <nvsdk_ngx_vk.h>

class IFeature_VkwDx12 : public virtual IFeature_Vk
{
  protected:
    // Vulkan with D3D12 interop structures
    using QUERY_INDEX_BUFFERS = struct QUERY_INDEX_BUFFERS
    {
        VkCommandBuffer VulkanCopyCommandBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkCommandPool VulkanCopyCommandPool[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkCommandBuffer VulkanBarrierCommandBuffer[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkCommandPool VulkanBarrierCommandPool[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    };

    using VK_TEXTURE2D_RESOURCE_C = struct VK_TEXTURE2D_RESOURCE_C
    {
        VkFormat Format = VK_FORMAT_UNDEFINED;
        uint32_t Width = 0;
        uint32_t Height = 0;
        VkImage VkSourceImage = VK_NULL_HANDLE;
        VkImageView VkSourceImageView = VK_NULL_HANDLE;
        VkImageLayout VkSourceImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkAccessFlags VkSourceImageAccess = VK_ACCESS_NONE;
        VkImage VkSharedImage = VK_NULL_HANDLE;
        VkImageView VkSharedImageView = VK_NULL_HANDLE;
        VkImageLayout VkSharedImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkAccessFlags VkSharedImageAccess = VK_ACCESS_NONE;
        VkDeviceMemory VkSharedMemory = VK_NULL_HANDLE;
        ID3D12Resource* Dx12Resource = nullptr;
        HANDLE SharedHandle = NULL;
    };

    // Vulkan context - renamed to avoid conflicts
    VkDevice VulkanDevice = VK_NULL_HANDLE;
    VkPhysicalDevice VulkanPhysicalDevice = VK_NULL_HANDLE;
    VkInstance VulkanInstance = VK_NULL_HANDLE;
    VkQueue VulkanGraphicsQueue = VK_NULL_HANDLE;
    std::map<uint32_t, QUERY_INDEX_BUFFERS> VulkanQueueCommandBuffers;
    uint32_t ActiveQueueFamilyIndex = UINT32_MAX;

    PFN_vkGetInstanceProcAddr VulkanGIPA = nullptr;
    PFN_vkGetDeviceProcAddr VulkanGDPA = nullptr;

    // D3D12 context
    ID3D12CommandQueue* Dx12CommandQueue = nullptr;
    ID3D12CommandAllocator* Dx12CommandAllocator[2] = { nullptr, nullptr };
    ID3D12GraphicsCommandList* Dx12CommandList[2] = { nullptr, nullptr };
    ID3D12Fence* Dx12Fence = nullptr;
    HANDLE Dx12FenceEvent = nullptr;
    D3D12_COMMAND_LIST_TYPE Dx12CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;

    // Shared resources
    VK_TEXTURE2D_RESOURCE_C vkColor = {};
    VK_TEXTURE2D_RESOURCE_C vkMv = {};
    VK_TEXTURE2D_RESOURCE_C vkDepth = {};
    VK_TEXTURE2D_RESOURCE_C vkReactive = {};
    VK_TEXTURE2D_RESOURCE_C vkExp = {};
    VK_TEXTURE2D_RESOURCE_C vkOut = {};

    // Vulkan synchronization for texture copies - using shared fence pattern like Dx11wDx12
    VkSemaphore vkSemaphoreTextureCopy[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkSemaphore vkSemaphoreCopyBack[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    ID3D12Fence* dx12FenceTextureCopy[2] = { nullptr, nullptr };
    HANDLE vkSHForTextureCopy[2] = { nullptr, nullptr };
    uint64_t _fenceValue = 0;
    uint64_t pendingResourceCopyValue = 0;
    VkCommandBuffer registeredVirtualCommandBuffer = VK_NULL_HANDLE;
    bool commandListOpen = false;
    uint32_t _abortedWaits = 0;
    std::mutex EvaluateMutex;

    // D3D12 processing shaders
    std::unique_ptr<OS_Dx12> OutputScaler = nullptr;
    std::unique_ptr<RCAS_Dx12> RCAS = nullptr;
    std::unique_ptr<Bias_Dx12> Bias = nullptr;

    // Copy shaders
    std::unique_ptr<ResourceCopy_Vk> ColorCopy = nullptr;
    std::unique_ptr<ResourceCopy_Vk> VelocityCopy = nullptr;
    std::unique_ptr<DepthTransfer_Vk> DT = nullptr;
    std::unique_ptr<ResourceCopy_Vk> DepthCopy = nullptr;
    std::unique_ptr<ResourceCopy_Vk> ReactiveCopy = nullptr;
    std::unique_ptr<ResourceCopy_Vk> ExpCopy = nullptr;
    std::unique_ptr<ResourceCopy_Vk> OutCopy = nullptr;
    std::unique_ptr<ResourceCopy_Vk> OutCopy2 = nullptr;

    // Vulkan function pointers for external memory
    PFN_vkGetMemoryWin32HandlePropertiesKHR vkGetMemoryWin32HandlePropertiesKHR = nullptr;
    PFN_vkImportSemaphoreWin32HandleKHR vkImportSemaphoreWin32HandleKHR = nullptr;

    // Helper methods
    HRESULT CreateDx12Device();
    void GetHardwareAdapter(IDXGIFactory1* InFactory, IDXGIAdapter** InAdapter, D3D_FEATURE_LEVEL InFeatureLevel,
                            bool InRequestHighPerformanceAdapter);

    bool CreateSharedTexture(const VkImageCreateInfo& ImageInfo, VkImage& VulkanResource, VkDeviceMemory& VulkanMemory,
                             ID3D12Resource*& D3D12Resource, bool InOutput);
    bool CopyTextureFromVkToDx12(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Resource_VK* InParam,
                                 VK_TEXTURE2D_RESOURCE_C* OutResource, ResourceCopy_Vk* InCopyShader, bool InCopy,
                                 bool InDepth);
    bool ProcessVulkanTextures(VkCommandBuffer InCmdList, const NVSDK_NGX_Parameter* InParameters);
    bool CopyBackOutput(VkCommandBuffer InCmdBuffer);
    void AbortPendingInterop(VkCommandBuffer InCmdBuffer, uint32_t InFrame);

    void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                         D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);
    void SetVkObjectName(VkDevice device, VkObjectType objectType, uint64_t objectHandle, const char* name);
    uint32_t FindVulkanMemoryTypeIndex(uint32_t MemoryTypeBits, VkMemoryPropertyFlags PropertyFlags);

    bool LoadVulkanExternalMemoryFunctions();
    bool CreateVulkanCommandBuffers(uint32_t queueFamilyIndex);
    bool CreateSharedFenceSemaphore();

    void ReleaseSharedResources();
    // Lets the last upscale finish on D3D12, call before destroying the upscaler context
    void FinishPendingWork();
    void ReleaseSyncResources();
    void ReleaseSharedResource(VK_TEXTURE2D_RESOURCE_C& resource);

  public:
    bool BaseInit(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
                  PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA, NVSDK_NGX_Parameter* InParameters);

    IFeature_VkwDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
    ~IFeature_VkwDx12();

    virtual bool Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
                      PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                      NVSDK_NGX_Parameter* InParameters) = 0;

    virtual bool Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) = 0;
};
