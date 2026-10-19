#pragma once

#include "SysUtils.h"

// Overrides the FSR 4 preset, the network weights tuned for a specific upscale ratio
//
// FSR 4 picks the preset in its dispatch from the upscale ratio and passes it to the function that creates the
// network. That function is found through RTTI and the code around its call instead of byte patterns,
// so it works for every amdxcffx64 and FFX SDK upscaler dll that contains FSR 4
class FSR4Presets
{
  public:
    // Safe to call more than once for the same module
    static void Hook(HMODULE module);

    static bool IsAnyModuleHooked();
};
