#pragma once
#include <cstdint>

// Optional snapshot of the *successful* VkDeviceCreateInfo. Physical support
// is not logical enablement. Include ffx_api.h first; no Vulkan ABI dependency.
constexpr ffxStructType_t FSR4VK_QUERY_DESC_TYPE_DEVICE_FEATURES = 0x4653523444455651ull;
constexpr ffxStructType_t FSR4VK_CREATE_DESC_TYPE_DEVICE_FEATURES = 0x4653523444455643ull;
constexpr std::uint32_t FSR4VK_DEVICE_ROBUST_BUFFER_ACCESS = 1u;
constexpr std::uint32_t FSR4VK_DEVICE_NATIVE_MIXED_DOT = 2u;
struct Fsr4VkQueryDeviceFeatures {
    ffxQueryDescHeader header;
    std::uint32_t version; // output: 1 supports the create descriptor below
};
struct Fsr4VkCreateDeviceFeatures {
    ffxCreateContextDescHeader header;
    std::uint32_t enabledFlags;
    std::uint32_t apiVersion; // effective instance API, or 0 if not tracked
    // Only supplied if the host knows the recording queue family. UINT32_MAX
    // means unknown; optional GPU profiling is then disabled, not guessed.
    std::uint32_t queueFamilyIndex;
};
