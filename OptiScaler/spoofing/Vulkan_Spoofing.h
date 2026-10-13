#pragma once

#include "SysUtils.h"

#include <vulkan/vulkan.h>

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_win32.h>
#endif

class VulkanSpoofing
{
  private:
  public:
    inline static VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};

    static VkResult hkvkCreateDevice(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo* pCreateInfo,
                                     const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
    static VkResult hkvkCreateInstance(VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                       VkInstance* pInstance);

    static PFN_vkVoidFunction hkvkGetDeviceProcAddr(const PFN_vkVoidFunction orgFunc, const char* pName);
    static PFN_vkVoidFunction hkvkGetInstanceProcAddr(const PFN_vkVoidFunction orgFunc, const char* pName);

    static void HookForVulkanSpoofing(HMODULE vulkanModule);
    static void HookForVulkanExtensionSpoofing(HMODULE vulkanModule);
    static void HookForVulkanVRAMSpoofing(HMODULE vulkanModule);
};
