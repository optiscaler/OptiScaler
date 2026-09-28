#pragma once
#include <vulkan/vulkan.h>
// Include ffx_api.h first. Optional context pNext extension, not an AMD type.
constexpr ffxStructType_t FSR4VK_CREATE_DESC_TYPE_COOPERATIVE_MATRIX = 0x46535234434D4154ull;
struct Fsr4VkCreateCooperativeMatrix {
    ffxCreateContextDescHeader header;
    PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR getProperties;
    // Host attests that its actual successful VkDeviceCreateInfo enabled
    // cooperativeMatrix, subgroupSizeControl, computeFullSubgroups,
    // vulkanMemoryModel, and the required device extensions.
    VkBool32 deviceFeaturesEnabled;
};
