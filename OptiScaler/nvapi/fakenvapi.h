#pragma once

#include <dxgi.h>
#include <d3d12.h>
#include <unordered_map>
#include "low_latency/ll_util.h"

class fakenvapi
{
    inline static bool _usingFakenvapiAsMainNvapi = false;
    inline static bool _usingOnNvidia = false;
    inline static void* _lowLatencyTechContext = nullptr;
    inline static LowLatencyMode _lowLatencyMode = LowLatencyMode::LatencyFlex;
    inline static HMODULE _dllForNvidia = nullptr;

    static std::unordered_map<NvU32, void*> idToFuncMapping;

    static bool updateModeAndContext();

    static NvAPI_Status __cdecl placeholder()
    {
        // return OK();
        // return ERROR(NVAPI_NO_IMPLEMENTATION);
        return NVAPI_NO_IMPLEMENTATION; // no logging
    }

  public:
    static void init(bool onlyContext);
    static void deinit();
    static void* queryInterface(NvU32 id);
    static bool isLowLatencyActive();

    static LowLatencyMode getCurrentMode();

    static bool isUsingAsMainNvapi();
    static void setUsingAsMainNvapi(bool usingAsMain);
};
