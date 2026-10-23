#pragma once

#include "SysUtils.h"
#include <framegen/IFGFeature_Vk.h>

#include <mfg_xefg_vk/mfg_xefg_vk.h>
#include "XeFG_DynamicMFG.h"

// XeFG on Vulkan through an XeFG-on-Vulkan engine (a loaded module exporting MfgXeFGVkGetApi), which presents
// XeFG's frames through its own swapchain. The game gets that swapchain.
class XeFG_Vk : public virtual IFGFeature_Vk
{
  private:
    const MfgXeFGVkApi* _api = nullptr;

    // Between PrepareDevice and OnDeviceCreated
    void* _deviceEdit = nullptr;
    bool _deviceHosted = false;

    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    uint32_t _initFlags = 0;

    // The inverted depth flag changed, the game recreates the swapchain on out of date
    bool _swapchainOutdated = false;

    std::string _name = "XeFG";

    // Dynamic MFG, shared with XeFG on D3D12
    XeFG_DynamicMFG _dynamic;
    HWND _hwnd = nullptr;

    // What the game waited in the engine last frame
    double _acquireMs = 0.0;
    double _presentMs = 0.0;

    // Interpolated frames the engine has, 0 = generation off (Dynamic MFG 1X), -1 = not set on this swapchain
    int _engineCount = -1;

    bool _debugView = false;

    MfgXeFGVkLatency _latency { sizeof(MfgXeFGVkLatency), UINT32_MAX };
    void UpdateLatency();

    // Why this output left the game's swapchain alone
    std::string _refusal;

    // idTech presents from another queue than the inputs are copied on, its present waits for _inputDone
    VkQueue _presentQueue = VK_NULL_HANDLE;
    VkSemaphore _inputDone[BUFFER_COUNT] {};
    VkSemaphore _pendingInputWait = VK_NULL_HANDLE;

    static PFN_vkVoidFunction VKAPI_PTR OriginalFunction(void* user, const char* name);
    uint32_t InitFlags();
    void ApplyFrameCount();
    bool Dispatch();

  public:
    // The engine from the loaded modules, nullptr if there is none
    static const MfgXeFGVkApi* EngineApi();

    // Why the engine isn't running XeFG on this game, empty when it is
    std::string EngineReason() const;

    // IFGFeature
    const char* Name() override final;
    feature_version Version() override final { return { 1, 0, 0 }; }

    bool Present() override final;
    void Activate() override final;
    void Deactivate() override final;
    void DestroyFGContext() override final;
    bool ReleaseSwapchain(HWND hwnd) override final;
    bool Shutdown() override final;
    bool SetInterpolatedFrameCount(UINT interpolatedFrameCount) override final { return true; }

    // IFGFeature_Vk
    void PrepareDevice(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo& createInfo) override final;
    void OnDeviceCreated(VkDevice device) override final;

    bool CreateSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                         const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) override final;
    bool DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain,
                          const VkAllocationCallbacks* pAllocator) override final;
    VkResult GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount,
                                VkImage* pSwapchainImages) override final;
    VkResult AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
                              VkFence fence, uint32_t* pImageIndex) override final;
    VkResult QueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) override final;
    void SetHdrMetadata(VkDevice device, VkSwapchainKHR swapchain, const VkHdrMetadataEXT* pMetadata) override final;

    void EvaluateState(FG_Constants& fgConstants) override final;

    // vkWaitForPresentKHR / vkWaitForPresent2KHR on the engine's swapchain
    VkResult WaitForPresent(VkDevice device, VkSwapchainKHR swapchain, uint64_t presentId, uint64_t timeout);

    // vkReleaseSwapchainImagesKHR / EXT (swapchain_maintenance1) on the engine's swapchain
    VkResult ReleaseSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t imageIndexCount,
                                    const uint32_t* pImageIndices);

    // vkGetRefreshCycleDurationGOOGLE on the engine's swapchain
    uint64_t RefreshDurationNs() const;

    XeFG_Vk();
    ~XeFG_Vk();
};
