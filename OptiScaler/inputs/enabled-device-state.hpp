#pragma once
#include <vulkan/vulkan.h>
#include <cstring>
#include <stdexcept>

namespace fsr4vk {
struct EnabledDeviceState {
    bool robust_buffer_access=false;
    bool native_mixed_dot=false;
};
// Read the final create-info while owned pNext storage is still alive. This is
// enablement evidence, unlike vkGetPhysicalDeviceFeatures2 support queries.
inline EnabledDeviceState read_enabled_device_state(const VkDeviceCreateInfo& info,uint32_t api) {
    EnabledDeviceState result;
    result.robust_buffer_access=info.pEnabledFeatures && info.pEnabledFeatures->robustBufferAccess;
    bool mixed=false,controls=false;
    unsigned count=0;
    for(auto* node=static_cast<const VkBaseInStructure*>(info.pNext);node;node=node->pNext) {
        if(++count>64) throw std::invalid_argument("enabled device chain cyclic or too long");
        if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
            result.robust_buffer_access=reinterpret_cast<const VkPhysicalDeviceFeatures2*>(node)->features.robustBufferAccess;
        // Only the first boolean is needed. Copy its Khronos ABI position so
        // older Vulkan headers need not declare the full new VALVE structure.
        if(static_cast<int>(node->sType)==1000673000) {
            VkBool32 value{};
            std::memcpy(&value,reinterpret_cast<const char*>(node)+sizeof(VkBaseInStructure),sizeof(value));
            mixed=value==VK_TRUE;
        }
        if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES_KHR)
            controls=reinterpret_cast<const VkPhysicalDeviceShaderFloatControls2FeaturesKHR*>(node)->shaderFloatControls2;
#if defined(VK_VERSION_1_4)
        if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES)
            controls=reinterpret_cast<const VkPhysicalDeviceVulkan14Features*>(node)->shaderFloatControls2;
#endif
    }
    const auto extension=[&](const char* name) {
        for(uint32_t i=0;i<info.enabledExtensionCount;++i)
            if(std::strcmp(info.ppEnabledExtensionNames[i],name)==0) return true;
        return false;
    };
    result.native_mixed_dot=mixed && controls && extension("VK_VALVE_shader_mixed_float_dot_product") &&
        (api>=VK_API_VERSION_1_4 || extension("VK_KHR_shader_float_controls2"));
    return result;
}
}
