#pragma once
#include "SysUtils.h"

class DxgiHooks
{
  private:
    static inline std::mutex hookMutex;

  public:
    static void Hook();

    // Hooks the factory methods with a temporary factory, can't be called from DllMain
    static void HookFactoryMethods();

    // Loads waiting for the exe entry, also run on a factory creation in case the entry hook doesn't fire
    inline static void (*PendingLoads)() = nullptr;
};
