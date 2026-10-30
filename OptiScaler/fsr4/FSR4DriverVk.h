#pragma once

#include "FSR4Upgrade.h"

#include <include/ffx/FfxVkHeader.h>

// FSR 4 upscaler of the AMD Vulkan driver (amdvkffx64.dll). It's called directly, the FFX SDK only gets driver
// providers through the DX12 driver extension
class FSR4DriverVk
{
  public:
    // Loads the driver dll and gets its upscaler once, false when there is none
    static bool Init();

    static const FfxProviderFunctions& Functions();

    // The provider gets the same device info as the backend create desc
    static bool IsSupported(const VkDeviceContext& device);
};
