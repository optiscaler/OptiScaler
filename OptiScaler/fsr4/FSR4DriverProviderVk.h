#pragma once

#include "FSR4DriverProvider.h"

#include <proxies/FfxApi_Proxy.h>

// Vulkan FFX backend description type used by the driver side providers (SDK 2.x id)
// Layout is the same as ffxCreateBackendVKDesc
#define FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2 0x01000003u

// From SDK 2.x ffx_upscale.h, the driver complains without it and accepts versions 4.0.0 - 4.1.2
#define FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION_V2 0x0001000Bu
#define FFX_UPSCALER_VERSION_V2 ((4u << 22) | (1u << 12) | 2u)

struct ffxCreateContextDescUpscaleVersionV2
{
    ffxCreateContextDescHeader header;
    uint32_t version;
};

// From SDK 2.x ffx_framegeneration.h, the driver logs an error without it and accepts versions 4.0.0 - 4.0.2
#define FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION_V2 0x0002000Eu
#define FFX_FRAMEGENERATION_VERSION_V2 ((4u << 22) | 2u)

struct ffxCreateContextDescFrameGenerationVersionV2
{
    ffxCreateContextDescHeader header;
    uint32_t version;
};

// Optional for the driver's swapchain, has to be at least 3.1.5
#define FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VERSION_VK_V2 0x0004000Au
#define FFX_FGSWAPCHAIN_VERSION_VK_V2 (4u << 22)

struct ffxCreateContextDescFrameGenerationSwapChainVersionVKV2
{
    ffxCreateContextDescHeader header;
    uint32_t version;
};

// Uses the upscaler and frame generation providers of amdvkffx64.dll (AMD Vulkan driver) directly,
// without needing amd_fidelityfx_loader_vk.dll / amd_fidelityfx_upscaler_vk.dll / amd_fidelityfx_framegeneration_vk.dll
class FSR4DriverProviderVk
{
  public:
    static bool Init();
    static bool IsAvailable() { return _module.CreateContext != nullptr; }

    static FfxModule* Module() { return &_module; }
    static uint64_t VersionId() { return _upscaler.functions.versionId; }
    static const char* VersionName() { return _upscaler.versionName.c_str(); }

    static bool IsSupported(VkDevice device, VkPhysicalDevice physicalDevice, PFN_vkGetDeviceProcAddr gdpa);

    // ML frame generation (FP8 only) and the swapchain it needs, both have to come from the driver
    static bool InitFg();
    static bool IsFgAvailable()
    {
        return _fgModule.CreateContext != nullptr && _fgSwapchainModule.CreateContext != nullptr;
    }

    static FfxModule* FgModule() { return &_fgModule; }
    static FfxModule* FgSwapchainModule() { return &_fgSwapchainModule; }
    static uint64_t FgVersionId() { return _fg.functions.versionId; }
    static const char* FgVersionName() { return _fg.versionName.c_str(); }

    static bool IsFgSupported(VkDevice device, VkPhysicalDevice physicalDevice, PFN_vkGetDeviceProcAddr gdpa);

  private:
    inline static DriverProvider _upscaler {};
    inline static FfxModule _module {};

    inline static DriverProvider _fg {};
    inline static FfxModule _fgModule {};
    inline static FfxModule _fgSwapchainModule {};
};
