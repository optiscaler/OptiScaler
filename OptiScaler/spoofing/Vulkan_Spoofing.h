#pragma once

#include "SysUtils.h"

#include <vulkan/vulkan.hpp>

#include <memory>

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_win32.h>
#endif

class VulkanDeviceFeatureState
{
  public:
    VulkanDeviceFeatureState(VkDeviceCreateInfo* createInfo,
                             PFN_vkGetPhysicalDeviceFeatures2 getPhysicalDeviceFeatures2);
    ~VulkanDeviceFeatureState();

    VulkanDeviceFeatureState(const VulkanDeviceFeatureState&) = delete;
    VulkanDeviceFeatureState& operator=(const VulkanDeviceFeatureState&) = delete;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    friend class VulkanSpoofing;
};

class VulkanSpoofing
{
  private:
  public:
    inline static VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};

    static VkResult hkvkCreateDevice(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo* pCreateInfo,
                                     const VkAllocationCallbacks* pAllocator, VkDevice* pDevice,
                                     VulkanDeviceFeatureState* featureState = nullptr);
    static VkResult hkvkCreateInstance(VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                       VkInstance* pInstance);

    static PFN_vkVoidFunction hkvkGetDeviceProcAddr(const PFN_vkVoidFunction orgFunc, const char* pName);
    static PFN_vkVoidFunction hkvkGetInstanceProcAddr(const PFN_vkVoidFunction orgFunc, const char* pName);

    static void HookForVulkanSpoofing(HMODULE vulkanModule);
    static void HookForVulkanExtensionSpoofing(HMODULE vulkanModule);
    static void HookForVulkanVRAMSpoofing(HMODULE vulkanModule);
};
