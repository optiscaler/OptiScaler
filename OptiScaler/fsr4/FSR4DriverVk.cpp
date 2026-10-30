#include "pch.h"
#include "FSR4DriverVk.h"

#include <Config.h>
#include <proxies/KernelBase_Proxy.h>

#include <ffx_upscale.h>

namespace
{
ExternalProviderData upscaler {};

bool QueryUpscaler()
{
    CheckForGPU();

    if (!Config::Instance()->Fsr4Update.value_or_default())
        return false;

    auto module = LoadFSR4DriverDll(API::Vulkan);
    if (module == nullptr)
        return false;

    auto updateFfxApiProviderEx = reinterpret_cast<PFN_UpdateFfxApiProviderEx>(
        KernelBaseProxy::GetProcAddress_()(module, "UpdateFfxApiProviderEx"));

    if (updateFfxApiProviderEx == nullptr)
    {
        LOG_WARN("amdvkffx64 has no UpdateFfxApiProviderEx");
        return false;
    }

    ExternalProviderData data {};
    data.structVersion = 3;
    data.descType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    magicData upscale = { { 0, 0, 1, 0 }, nullptr };

    HRESULT result;
    {
        ScopedSkipSpoofing skipSpoofing {};
        State::DisableChecks(1);
        result = updateFfxApiProviderEx(&data, sizeof(data), &upscale);
        State::EnableChecks(1);
    }

    // Drivers that don't know version 3 return S_OK without filling anything in
    if (FAILED(result) || data.provider.createContext == nullptr || data.isSupported == nullptr)
    {
        LOG_INFO("amdvkffx64 gives no upscaler, result: {:#X}", (UINT) result);
        return false;
    }

    // The driver dll stays loaded, its version name stays valid
    upscaler = data;

    if (upscaler.provider.versionName == nullptr)
        upscaler.provider.versionName = "";

    LOG_INFO("amdvkffx64 upscaler: {}, id: {:X}", upscaler.provider.versionName, upscaler.provider.versionId);
    return true;
}
} // namespace

bool FSR4DriverVk::Init()
{
    static std::once_flag initOnce;
    static bool available = false;

    std::call_once(initOnce, [] { available = QueryUpscaler(); });
    return available;
}

const FfxProviderFunctions& FSR4DriverVk::Functions() { return upscaler.provider; }

bool FSR4DriverVk::IsSupported(const VkDeviceContext& device)
{
    if (!Init())
        return false;

    ScopedSkipSpoofing skipSpoofing {};
    return upscaler.isSupported(&device);
}
