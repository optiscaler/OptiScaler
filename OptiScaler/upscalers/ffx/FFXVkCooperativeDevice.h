#pragma once
#include <vulkan/vulkan.h>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace FFXVkCooperativeDevice {
struct Entry { VkInstance instance; bool enabled; };
inline std::mutex mutex;
inline std::unordered_map<VkDevice, Entry> devices;
// Inspect only the actual create-info after successful vkCreateDevice. Never
// infer enabled logical-device bits from physical-device support.
inline void Record(VkInstance instance, VkDevice device, const VkDeviceCreateInfo& info) {
    bool matrix=false,size=false,full=false,memory=false,scope=false,extension=false;
    for(uint32_t i=0;i<info.enabledExtensionCount;++i)
        extension|=std::strcmp(info.ppEnabledExtensionNames[i],VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME)==0;
    for(auto* n=static_cast<const VkBaseInStructure*>(info.pNext);n;n=n->pNext) {
        switch(n->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR:
            matrix=reinterpret_cast<const VkPhysicalDeviceCooperativeMatrixFeaturesKHR*>(n)->cooperativeMatrix;break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES: {
            const auto& f=*reinterpret_cast<const VkPhysicalDeviceSubgroupSizeControlFeatures*>(n);
            size=f.subgroupSizeControl;full=f.computeFullSubgroups;break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: {
            const auto& f=*reinterpret_cast<const VkPhysicalDeviceVulkan13Features*>(n);
            size=f.subgroupSizeControl;full=f.computeFullSubgroups;break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES: {
            const auto& f=*reinterpret_cast<const VkPhysicalDeviceVulkanMemoryModelFeatures*>(n);
            memory=f.vulkanMemoryModel;scope=f.vulkanMemoryModelDeviceScope;break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
            const auto& f=*reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(n);
            memory=f.vulkanMemoryModel;scope=f.vulkanMemoryModelDeviceScope;break;
        }
        default:break;
        }
    }
    std::scoped_lock lock(mutex);
    // Also overwrite entries on handle reuse. Instance destruction clears the
    // remaining records; there is no need for another device-destruction hook.
    devices[device]={instance,matrix && size && full && memory && scope && extension};
}
inline bool Enabled(VkDevice device) {
    std::scoped_lock lock(mutex);auto it=devices.find(device);
    return it!=devices.end() && it->second.enabled;
}
inline void Forget(VkInstance instance) {
    std::scoped_lock lock(mutex);
    for(auto it=devices.begin();it!=devices.end();)
        if(it->second.instance==instance)it=devices.erase(it);else ++it;
}
}
