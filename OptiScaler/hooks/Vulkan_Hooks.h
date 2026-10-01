#pragma once
#include "SysUtils.h"

class VulkanHooks
{
  public:
    static PFN_vkCreateSemaphore o_vkCreateSemaphore;
    static PFN_vkSignalSemaphore o_vkSignalSemaphore;
    static PFN_vkAntiLagUpdateAMD o_vkAntiLagUpdateAMD;

    static void Hook(HMODULE vulkan1);
    static void Unhook();
    static bool GetCreatedDeviceFeatures(VkDevice device, uint32_t& flags, uint32_t& apiVersion,
                                         uint32_t& queueFamilyIndex);
};
