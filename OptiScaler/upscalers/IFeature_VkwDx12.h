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

#define VKDX12_BUFFER_COUNT 2

class IFeature_VkwDx12 : public virtual IFeature_Vk
{
  protected:
    // Vulkan with D3D12 interop structures
    using QUERY_INDEX_BUFFERS = struct QUERY_INDEX_BUFFERS
    {
        VkCommandBuffer VulkanCopyCommandBuffer[VKDX12_BUFFER_COUNT] {};
        VkCommandPool VulkanCopyCommandPool[VKDX12_BUFFER_COUNT] {};
        VkCommandBuffer VulkanBarrierCommandBuffer[VKDX12_BUFFER_COUNT] {};
        VkCommandPool VulkanBarrierCommandPool[VKDX12_BUFFER_COUNT] {};
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
    uint32_t ActiveQueueFamilyIndex = 9999;

    PFN_vkGetInstanceProcAddr VulkanGIPA = nullptr;
    PFN_vkGetDeviceProcAddr VulkanGDPA = nullptr;

    // D3D12 context
    ID3D12CommandQueue* Dx12CommandQueue = nullptr;
    ID3D12CommandAllocator* Dx12CommandAllocator[VKDX12_BUFFER_COUNT] {};
    ID3D12GraphicsCommandList* Dx12CommandList[VKDX12_BUFFER_COUNT] {};
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
    VkSemaphore vkSemaphoreTextureCopy[VKDX12_BUFFER_COUNT] {};
    VkSemaphore vkSemaphoreCopyBack[VKDX12_BUFFER_COUNT] {};
    ID3D12Fence* dx12FenceTextureCopy[VKDX12_BUFFER_COUNT] {};
    HANDLE vkSHForTextureCopy[VKDX12_BUFFER_COUNT] {};
    ULONG _fenceValue = 0;

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

    bool CreateSharedTexture(const VkImageCreateInfo& ImageInfo, VkImage& VulkanResource, VkDeviceMemory& VulkanMemory,
                             ID3D12Resource*& D3D12Resource, bool InOutput);
    bool CopyTextureFromVkToDx12(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Resource_VK* InParam,
                                 VK_TEXTURE2D_RESOURCE_C* OutResource, ResourceCopy_Vk* InCopyShader, bool InCopy,
                                 bool InDepth);
    bool ProcessVulkanTextures(VkCommandBuffer InCmdList, const NVSDK_NGX_Parameter* InParameters);
    bool CopyBackOutput();

    void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                         D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);
    void SetVkObjectName(VkDevice device, VkObjectType objectType, uint64_t objectHandle, const char* name);
    uint32_t FindVulkanMemoryTypeIndex(uint32_t MemoryTypeBits, VkMemoryPropertyFlags PropertyFlags);

    bool LoadVulkanExternalMemoryFunctions();
    bool CreateVulkanCommandBuffers(uint32_t queueFamilyIndex);
    bool CreateSharedFenceSemaphore();

    void ReleaseSharedResources();
    void ReleaseSyncResources();

    // vk w/dx12 can't be wrapped by the IFeature_Vk, needs its own Init and Evaluate
    bool InitInternal(VkCommandBuffer InCmdList, NVSDK_NGX_Parameter* InParameters) final { return false; };
    bool EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) final { return false; };

  public:
    bool BaseInit(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
                  PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA, NVSDK_NGX_Parameter* InParameters);

    API Api() const override { return API::DX12; }
    bool IsWithDx12() override { return true; }

    IFeature_VkwDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
    ~IFeature_VkwDx12();

    virtual bool Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
                      PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                      NVSDK_NGX_Parameter* InParameters) = 0;

    virtual bool Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) = 0;
};
