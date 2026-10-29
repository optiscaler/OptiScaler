#pragma once

#include "Config.h"

typedef uint8_t(__fastcall* AmdInt8Check)(void* a1, void* device);

enum class FSR4Source
{
    SDK,
    DriverDll,
};

// Overrides the FSR 4 preset by hooking the function that creates the network. It is found through RTTI and the code
// of the FSR 4 dispatch instead of byte patterns, so it works for every FFX SDK upscaler and driver dll with FSR 4
class FSR4ModelSelection
{
    static uint8_t hkAmdInt8Check(void* a1, void* device);

    inline static AmdInt8Check o_amdInt8Check_Dx12 = nullptr;
    inline static AmdInt8Check o_amdInt8Check_Vk = nullptr;

  public:
    // Safe to call more than once for the same module
    static void Hook(HMODULE module, FSR4Source source, API api);
    static bool IsInt8FsrHookedDx12() { return o_amdInt8Check_Dx12; };
    static bool IsInt8FsrHookedVk() { return o_amdInt8Check_Vk; };
};
