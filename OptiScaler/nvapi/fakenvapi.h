#pragma once

#include <dxgi.h>
#include <d3d12.h>
#include <unordered_map>
#include "low_latency/ll_util.h"

class fakenvapi
{
    inline static bool _usingFakenvapiAsMainNvapi = false;

    static std::unordered_map<NvU32, void*> idToFuncMapping;

    static NvAPI_Status __cdecl placeholder()
    {
        // return OK();
        // return ERROR(NVAPI_NO_IMPLEMENTATION);
        return NVAPI_NO_IMPLEMENTATION; // no logging
    }

  public:
    static void* queryInterface(NvU32 id);
    static bool isUsingAsMainNvapi();
    static void setUsingAsMainNvapi(bool usingAsMain);
};
