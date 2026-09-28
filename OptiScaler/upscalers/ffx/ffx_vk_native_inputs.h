#pragma once
#include <cstdint>

// Optional provider extension, not an AMD FFX descriptor. Query before attaching
// it to a dispatch so other providers continue receiving their original ABI.
// Include ffx_api.h first. Formats are numeric VkFormat values (no Vulkan header
// dependency); metadata applies to this dispatch only, never a cached VkImage.
constexpr ffxStructType_t FSR4VK_QUERY_DESC_TYPE_NATIVE_INPUTS = 0x465352344E494E51ull;
constexpr ffxStructType_t FSR4VK_DISPATCH_DESC_TYPE_NATIVE_INPUTS = 0x465352344E494E44ull;
struct Fsr4VkQueryNativeInputs
{
    ffxQueryDescHeader header;
    std::uint32_t version; // output: 1 supports the descriptor below
};
struct Fsr4VkDispatchNativeInputs
{
    ffxDispatchDescHeader header;
    std::uint32_t motionFormat; // exact original VkImage format, not an FFX alias
};
