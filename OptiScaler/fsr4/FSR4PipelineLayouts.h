#pragma once

#include "SysUtils.h"

// Gives the pipelines of the FFX Vulkan backend in amdvkffx64 pipeline layouts that get reused after destruction
//
// The backend gives every new pipeline the next slot of a fixed pipeline layout array and never takes it back.
// FSR 4 recreates its network whenever the upscale ratio moves to another preset, so after a few recreations
// the slots of the effect context run out and new layouts get written over the resource array placed right after
// them, which crashes the next dispatch. The hooks hand the backend layouts of their own instead, so there is
// no limit. The functions are found through byte patterns that contain every offset the hooks rely on,
// a dll with a different backend layout is left alone
class FSR4PipelineLayouts
{
  public:
    // Safe to call more than once for the same module
    static void Hook(HMODULE module);

    static bool IsHooked();
};
