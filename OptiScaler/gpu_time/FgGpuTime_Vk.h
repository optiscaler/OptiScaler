#pragma once

#include "FgTiming.h"
#include <vulkan/vulkan.h>

// Restricted to primary, one-time command buffers allocated after Hook(). A timing is published only
// after the observed submission completes on a registered queue, through its fence or a tracked,
// GPU-only timeline semaphore. Missing lifecycle/submission evidence leaves the channel unavailable.
class FgGpuTime_Vk
{
  public:
    static bool Hook(VkDevice device, VkPhysicalDevice physicalDevice);
    static void RegisterQueue(VkDevice device, VkQueue queue, uint32_t family);
    static void Poll();

    // The owner must finish its existing SDK/swapchain teardown while the VkDevice is still alive.
    // This function does not add a wait; it releases query pools after that teardown has completed.
    static void ForgetDevice(VkDevice device);
};

class ScopedFgGpuTime_Vk
{
    VkDevice _device = VK_NULL_HANDLE;
    VkCommandBuffer _commandBuffer = VK_NULL_HANDLE;
    uint64_t _serial = 0;

  public:
    ScopedFgGpuTime_Vk(VkDevice device, VkCommandBuffer commandBuffer, std::shared_ptr<FgTiming::Channel> channel,
                       uint64_t frameId, uint32_t generatedFrames = 0);
    ~ScopedFgGpuTime_Vk();
    void End(bool success = true);

    ScopedFgGpuTime_Vk(const ScopedFgGpuTime_Vk&) = delete;
    ScopedFgGpuTime_Vk& operator=(const ScopedFgGpuTime_Vk&) = delete;
};
