#pragma once

#include "SysUtils.h"

#include <ffx_api.h>

// Functions of an FFX provider that the driver dlls hand out through UpdateFfxApiProvider
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

// Data passed to UpdateFfxApiProvider(Ex), the driver fills in the provider for descType
struct ExternalProviderData
{
    uint32_t structVersion;
    uint64_t descType;
    ProviderInterface provider;

    // Version 3, whether the provider can run on a device. Takes what the driver gets from the backend create desc:
    // the ID3D12Device* on DX12, a pointer to { VkDevice, VkPhysicalDevice, GDPA } on Vulkan
    bool (*isSupported)(const void* backend);
    void* reserved;

    // The driver dlls require all 0x60 bytes for version 3 and 0x50 for older versions,
    // versions newer than the driver knows return S_OK without filling anything in
    static constexpr uint32_t SizeFor(uint32_t structVersion)
    {
        return structVersion >= 3 ? sizeof(ExternalProviderData) : offsetof(ExternalProviderData, isSupported);
    }
};

static_assert(offsetof(ExternalProviderData, isSupported) == 0x50);
static_assert(sizeof(ExternalProviderData) == 0x60);

// Effects that UpdateFfxApiProviderEx gives out, upscaling is enabled by default, the others need an option.
// The driver keeps the enabled state for all later calls, plain UpdateFfxApiProvider only enables upscaling
enum class ProviderEffect : uint32_t
{
    Upscale = 0,
    FrameGeneration = 1,
    FrameGenerationSwapchain = 5,
};

// Options of UpdateFfxApiProviderEx, chained through next
struct ProviderOption
{
    uint32_t type = 0; // Seemingly unused
    ProviderEffect effect = ProviderEffect::Upscale;
    bool enable = true;
    ProviderOption* next = nullptr;
};

static_assert(sizeof(ProviderOption) == 0x18);

typedef HRESULT(STDMETHODCALLTYPE* PFN_UpdateFfxApiProvider)(void* pData, uint32_t dataSizeInBytes);
typedef HRESULT(STDMETHODCALLTYPE* PFN_UpdateFfxApiProviderEx)(void* pData, uint32_t dataSizeInBytes,
                                                               ProviderOption* options);

// When the driver upscaler gets added to the upscaler versions of the FFX SDK
enum class DriverListing
{
    Always,             // Opti's menu
    WhenNewestUpscaler, // Version queries of the game
};

// FFX packs provider versions into the low bits of their ids, external providers also set bit 31
inline feature_version ProviderVersion(uint64_t versionId)
{
    auto packed = static_cast<uint32_t>(versionId) & 0x7FFFFFFF;
    return feature_version(packed >> 22, (packed >> 12) & 0x3FF, packed & 0xFFF);
}
// Adds a driver provider to a version list from the FFX SDK, keeping it sorted newest first
void AddDriverToVersionList(std::vector<uint64_t>& ids, std::vector<const char*>& names, uint64_t driverId,
                            const char* driverName, DriverListing listing);

// A provider of a driver dll, with its own copy of the name as the driver formats names into buffers that later calls
// overwrite
struct DriverProvider
{
    ProviderInterface functions {}; // versionName is left empty, the name is in versionName below
    std::string versionName;
    bool (*isSupported)(const void* backend) = nullptr;
};

// An AMD driver dll with FSR 4 (amdxcffx64 for DX12, amdvkffx64 for Vulkan) whose FFX providers Opti uses
class FSR4DriverDll
{
  public:
    // sdkModules are the FFX SDK dlls the driver needs to see loaded and signed before it hands out providers. When the
    // game hasn't loaded them, the driver dll stands in for them, it's signed with the same certificate. The driver
    // also derives the SDK version from the file version of the first one it checks, so Load() has it check itself
    // first, before any FFX dll of the game
    FSR4DriverDll(const wchar_t* name, std::vector<std::wstring> sdkModules);

    FSR4DriverDll(const FSR4DriverDll&) = delete;
    FSR4DriverDll& operator=(const FSR4DriverDll&) = delete;

    // Loads the dll on the first call, from the game folder and then from the driver store
    HMODULE Load();

    // nullptr until Load() succeeds
    HMODULE Module() const { return _module; }

    // Gets the provider for descType, the struct versions are tried in order
    std::optional<DriverProvider> QueryProvider(uint64_t descType, std::initializer_list<uint32_t> structVersions,
                                                ProviderOption* options = nullptr) const;

    // Passes a request of an FFX SDK dll to the driver, options need UpdateFfxApiProviderEx
    HRESULT UpdateFfxApiProvider(void* data, uint32_t dataSizeInBytes, ProviderOption* options = nullptr) const;

    // Driver dll to return for a GetModuleHandle call of the caller, nullptr when it's not a stand-in request
    static HMODULE SdkModuleStandIn(const wchar_t* moduleName, HMODULE caller);

  private:
    std::wstring _name;
    std::vector<std::wstring> _sdkModules;

    HMODULE _module = nullptr;
    PFN_UpdateFfxApiProvider _updateFfxApiProvider = nullptr;
    PFN_UpdateFfxApiProviderEx _updateFfxApiProviderEx = nullptr;
    std::once_flag _loadOnce;
};
