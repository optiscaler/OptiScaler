#pragma once
#include "SysUtils.h"

class DxgiHooks
{
  private:
    static inline std::mutex hookMutex;

  public:
    static void Hook(bool force = false);

    // Hooks the factory methods with a temporary factory, can't be called from DllMain
    static void HookFactoryMethods();

    // Creates Luma's D3D12 device and loads Special K / ReShade after it, can't be called from DllMain
    static void LoadForLuma();

    // Defers executable FSR2 hooks until the game creates a DXGI factory, if factory hooks are available.
    static bool DeferFsr2ExeHooks(void (*hook)());

    // Loads waiting for the exe entry, also run on a factory creation in case the entry hook doesn't fire
    inline static void (*PendingLoads)() = nullptr;
};
