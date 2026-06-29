#pragma once

#include "Config.h"

typedef uint64_t (*PFN_getModelBlob)(uint32_t preset, uint64_t unknown, uint64_t* source, uint64_t* size);
typedef uint64_t (*PFN_createModel)(void* context, uint32_t preset);
typedef uint64_t (*PFN_createModel2)(void* context, uint32_t preset, void** model);

enum class FSR4Source
{
    SDK,
    DriverDll,
};

class FSR4ModelSelection
{
    static uint64_t hkgetModelBlobSDK(uint32_t preset, uint64_t unknown, uint64_t* source, uint64_t* size);
    static uint64_t hkgetModelBlobDriver(uint32_t preset, uint64_t unknown, uint64_t* source, uint64_t* size);
    static PFN_getModelBlob o_getModelBlobSDK;
    static PFN_getModelBlob o_getModelBlobDriver;
    static uint64_t hkcreateModelSDK(void* context, uint32_t preset);
    static uint64_t hkcreateModelDriver(void* context, uint32_t preset);
    static PFN_createModel o_createModelSDK;
    static PFN_createModel o_createModelDriver;
    static uint64_t hkcreateModelDriver2(void* context, uint32_t preset, void** model);
    static PFN_createModel2 o_createModelDriver2;
    static uint64_t hkcreateModelSDK2(void* context, uint32_t preset, void** model);
    static PFN_createModel2 o_createModelSDK2;

  public:
    static void Hook(HMODULE module, FSR4Source source);
    static bool IsInt8FsrHooked() { return o_createModelDriver2 || o_createModelSDK2; };
};
