#pragma once
#include "SysUtils.h"
#include <framegen/IFGFeature_Vk.h>
#include <proxies/FfxApi_Proxy.h>
#include <shaders/hud_copy/HudCopy_Vk.h>
#include <shaders/hudless_compare_compute/HCC_Vk.h>

// FSR 3.1 FG from the FFX SDK 1.1.4, the last one with Vulkan support
class FSRFG_Vk : public virtual IFGFeature_Vk
{
  private:
    enum ExtraQueueIndex
    {
        PresentQueue,
        ImageAcquireQueue,
        AsyncComputeQueue,
    };

    ffxContext _swapChainContext = nullptr;
    ffxContext _fgContext = nullptr;
    ffxQueryDescSwapchainReplacementFunctionsVK _functions {};
    feature_version _version { 0, 0, 0 };

    // Game destroys its old swapchain after creating the new one, the context took it with it already
    VkSwapchainKHR _retiredSwapchain = VK_NULL_HANDLE;

    // FFX takes these at context creation
    flag_set<FG_Flags> _contextFlags;

    // FFX wants hudless in the swapchain's format, the HUD shaders need it in our copy too
    ImageCopy _hudlessCopies[BUFFER_COUNT] {};
    bool _hudlessInCopy[BUFFER_COUNT] {};

    // Not owned, the copy the HUD shaders work on: _hudlessCopies or the one SetResource made
    ImageCopy _hudlessTarget[BUFFER_COUNT] {};

    std::unique_ptr<HudCopy_Vk> _hudCopy;
    std::unique_ptr<HCC_Vk> _hudlessCompare;

    // Views of FFX's replacement images the game renders to
    std::unordered_map<VkImage, VkImageView> _presentViews;

    uint64_t _lastDispatchedFrameId = 0;

    void CreateContext();
    void ReleaseSwapchainContext();
    ffxReturnCode_t ConfigureEnabled(bool enabled);

    bool Dispatch();
    ffxReturnCode_t DispatchCallback(ffxDispatchDescFrameGeneration* params);
    void DispatchHudShaders(ffxDispatchDescFrameGeneration* params);
    VkImageView PresentView(VkImage image);

  public:
    // IFGFeature
    const char* Name() override final { return "FSR-FG"; }
    feature_version Version() override final { return _version; }

    bool Present() override final;
    void Activate() override final;
    void Deactivate() override final;
    void DestroyFGContext() override final;
    bool ReleaseSwapchain(HWND hwnd) override final;
    bool Shutdown() override final;
    bool SetInterpolatedFrameCount(UINT interpolatedFrameCount) override final { return true; }

    // IFGFeature_Vk
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

    FSRFG_Vk();
    ~FSRFG_Vk();
};
