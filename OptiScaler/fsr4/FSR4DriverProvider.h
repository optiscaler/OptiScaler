#pragma once

#include "SysUtils.h"

#include <ffx_api.h>

// Functions of an FFX provider that amdxcffx64 hands out through UpdateFfxApiProvider
struct ProviderInterface
{
    uint64_t versionId;
    const char* versionName;
    bool (*canProvide)(uint64_t descType);
    ffxReturnCode_t (*createContext)(ffxContext* context, ffxCreateContextDescHeader* desc,
                                     const ffxAllocationCallbacks* memCb);
    ffxReturnCode_t (*destroyContext)(ffxContext* context, const ffxAllocationCallbacks* memCb);
    ffxReturnCode_t (*configure)(ffxContext* context, const ffxConfigureDescHeader* desc);
    ffxReturnCode_t (*query)(ffxContext* context, ffxQueryDescHeader* desc);
    ffxReturnCode_t (*dispatch)(ffxContext* context, const ffxDispatchDescHeader* desc);
};

struct ExternalProviderData
{
    uint32_t structVersion;
    uint64_t descType;
    ProviderInterface provider;
};

// FSR 4 upscaler of the AMD driver (amdxcffx64.dll), called by Opti directly
//
// The FFX SDK can load it as an external provider through IAmdExtFfxApi, Opti keeps it away from its own
// FFX modules and lists it next to their upscalers instead, to decide itself when it's offered
class FSR4DriverProvider
{
  public:
    enum class Listing
    {
        Always,             // Opti's menu
        WhenNewestUpscaler, // Version queries of the game
    };

    // Loads amdxcffx64 on the first call, from the game folder and then from the driver store
    static HMODULE Module();

    // nullptr until Module() loads it
    static HMODULE LoadedModule();

    // Disables FSR 4 on INT8 GPUs when neither the FFX SDK nor the driver has the INT8 model
    static void UpdateInt8Support(HMODULE sdkModule);

    // Gets the upscaler of the driver on the first call, false when this GPU can't use it
    static bool Init();

    static uint64_t VersionId();

    // Adds the driver upscaler to a version list from the FFX SDK, keeping it sorted newest first
    static void AddToVersionList(std::vector<uint64_t>& ids, std::vector<const char*>& names, Listing listing);

    static bool OwnsContext(ffxContext* context);

    static ffxReturnCode_t CreateContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                         const ffxAllocationCallbacks* memCb);
    static ffxReturnCode_t DestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb);
    static ffxReturnCode_t Configure(ffxContext* context, const ffxConfigureDescHeader* desc);
    static ffxReturnCode_t Query(ffxContext* context, ffxQueryDescHeader* desc);
    static ffxReturnCode_t Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc);
};
