#pragma once

#include "Config.h"

enum class FSR4Source
{
    SDK,
    DriverDll,
};

// Overrides the FSR 4 preset by hooking the function that creates the network. It is found through RTTI and the code
// of the FSR 4 dispatch instead of byte patterns, so it works for every FFX SDK upscaler and driver dll with FSR 4.
// The model (FP8 or INT8) is picked through the support checks of the model providers
class FSR4ModelSelection
{
  public:
    // Safe to call more than once for the same module
    static void Hook(HMODULE module, FSR4Source source, API api);

    // A Dx12 module runs the INT8 model of Fsr4ForceEnableInt8
    static bool IsInt8FsrHookedDx12();

    // Model FFX picked last, nullptr when unknown
    static const char* ActiveModel();

    // INT8 models of the module FFX checked last, as Fsr4ForceEnableInt8 values
    static std::vector<uint32_t> SelectableModels();
    static const char* ModelName(uint32_t model);
};
