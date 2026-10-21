#pragma once
#include "SysUtils.h"

class VulkanHooks
{
    inline static std::atomic<VkSwapchainKHR> lowLatencySwapchain = VK_NULL_HANDLE;

  public:
    static PFN_vkCreateSemaphore o_vkCreateSemaphore;
    static PFN_vkAntiLagUpdateAMD o_vkAntiLagUpdateAMD;

    // The device's vkSignalSemaphore, or vkSignalSemaphoreKHR on devices with only VK_KHR_timeline_semaphore.
    // VK_ERROR_FEATURE_NOT_PRESENT when the device has neither.
    static VkResult SignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo* pSignalInfo);

    // The driver's VK_NV_low_latency2
    inline static PFN_vkSetLatencySleepModeNV o_vkSetLatencySleepModeNV = nullptr;
    inline static PFN_vkLatencySleepNV o_vkLatencySleepNV = nullptr;
    inline static PFN_vkSetLatencyMarkerNV o_vkSetLatencyMarkerNV = nullptr;
    inline static PFN_vkGetLatencyTimingsNV o_vkGetLatencyTimingsNV = nullptr;
    inline static PFN_vkQueueNotifyOutOfBandNV o_vkQueueNotifyOutOfBandNV = nullptr;

    // The swapchain with VK_NV_low_latency2's low latency mode, for the Reflex output
    static VkSwapchainKHR LowLatencySwapchain() { return lowLatencySwapchain; }
    static void SetLowLatencySwapchain(VkSwapchainKHR swapchain);
    static void ForgetLowLatencySwapchain(VkSwapchainKHR swapchain)
    {
        lowLatencySwapchain.compare_exchange_strong(swapchain, VK_NULL_HANDLE);
    }

    // Set while the FG output shows a surface the game created to its Streamline, whose vkCreateWin32SurfaceKHR gets
    // this one instead of a new one
    inline static thread_local VkSurfaceKHR existingSurface = VK_NULL_HANDLE;

    // The window a surface was created for
    static HWND SurfaceWindow(VkSurfaceKHR surface);

    // vkGetDeviceProcAddr without OptiScaler's hooks
    static PFN_vkVoidFunction GetDeviceProcAddr(VkDevice device, const char* pName);

    // vkGetInstanceProcAddr without OptiScaler's hooks
    static PFN_vkVoidFunction GetInstanceProcAddr(VkInstance instance, const char* pName);

    // The driver's swapchain function behind OptiScaler's hook, below hooks others put on the same function.
    // vkQueuePresentKHR's also times the presented frames for the menu. nullptr when it isn't hooked.
    static PFN_vkVoidFunction OriginalSwapchainFunction(const char* pName);

    static void Hook(HMODULE vulkan1);
    static void Unhook();
};
