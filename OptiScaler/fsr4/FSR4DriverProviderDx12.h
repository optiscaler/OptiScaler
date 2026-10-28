#pragma once

#include "FSR4DriverProvider.h"

#include "hooks/Amdxc64_Hooks.h"

// FSR 4 upscaler of the AMD DX12 driver (amdxcffx64.dll), called by Opti directly
//
// The FFX SDK can load it as an external provider through IAmdExtFfxApi, Opti keeps it away from its own
// FFX modules and lists it next to their upscalers instead, to decide itself when it's offered
class FSR4DriverProviderDx12
{
  public:
    using Listing = DriverListing;

    static FSR4DriverDll& Dll();

    // Loads amdxcffx64 on the first call, from the game folder and then from the driver store
    static HMODULE Module() { return Dll().Load(); }

    // nullptr until Module() loads it
    static HMODULE LoadedModule() { return Dll().Module(); }

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

// AMD driver extension interfaces Opti gives to the game and the FFX SDK

// The driver objects aren't reference counted, none of them gets released
template <typename Interface> struct AmdExtInterface : public Interface
{
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }
};

struct AmdExtFfxApi : public AmdExtInterface<IAmdExtFfxApi>
{
    FSR4Support realFsr4Support {};

    HRESULT STDMETHODCALLTYPE UpdateFfxApiProvider(void* pData, uint32_t dataSizeInBytes) override;
};

struct AmdExtD3DShaderIntrinsics : public AmdExtInterface<IAmdExtD3DShaderIntrinsics>
{
    HRESULT STDMETHODCALLTYPE GetInfo(void* ShaderIntrinsicsInfo) override;
    HRESULT STDMETHODCALLTYPE CheckSupport(AmdExtD3DShaderIntrinsicsSupport intrinsic) override;
    HRESULT STDMETHODCALLTYPE Enable() override;
};

#define STUB(number)                                                                                                   \
    HRESULT STDMETHODCALLTYPE unknown##number()                                                                        \
    {                                                                                                                  \
        LOG_FUNC();                                                                                                    \
        return S_OK;                                                                                                   \
    }

struct AmdExtD3DDevice8 : public AmdExtInterface<IAmdExtD3DDevice8>
{
    FSR4Support realFsr4Support {};
    FSR4Support fsr4Support {};

    STUB(1)
    STUB(2)
    STUB(3)
    STUB(4)
    STUB(5)
    STUB(6)
    STUB(7)
    STUB(8)
    STUB(9)
    STUB(10)
    STUB(11)
    STUB(12)
    STUB(13)
    HRESULT STDMETHODCALLTYPE GetWaveMatrixProperties(uint64_t* count,
                                                      AmdExtWaveMatrixProperties* waveMatrixProperties) override;
};
