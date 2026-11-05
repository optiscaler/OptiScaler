#pragma once

#include "SysUtils.h"
#include <vulkan/vulkan.h>

namespace MenuOverlayVk
{
void CreateSwapchain(VkDevice device, VkPhysicalDevice pd, VkInstance instance, HWND hwnd,
                     const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                     VkSwapchainKHR* pSwapchain);
void DeviceCreated(VkDevice device, const VkDeviceCreateInfo* pCreateInfo);
bool QueuePresent(VkQueue queue, VkPresentInfoKHR* pPresentInfo);
// With a swapchain, only when the menu is still on it
void DestroyVulkanObjects(bool shutdown, VkDevice device = VK_NULL_HANDLE, VkSwapchainKHR swapchain = VK_NULL_HANDLE);

// Overlay objects of a device that is being destroyed
void ReleaseDevice(VkDevice device);

// The next swapchain gets the menu, also on another window
void DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain);

// The menu stays on its window while that exists, games can have more than one
bool OnOtherWindow(HWND hwnd);
} // namespace MenuOverlayVk
