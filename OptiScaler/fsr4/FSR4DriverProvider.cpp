#include "pch.h"
#include "FSR4DriverProvider.h"

#include "FSR4Models.h"
#include "FSR4Presets.h"

#include <misc/IdentifyGpu.h>
#include <proxies/FfxApi_Proxy.h>
#include <proxies/KernelBase_Proxy.h>
#include <proxies/Ntdll_Proxy.h>

#include <ffx_upscale.h>

namespace
{
typedef HRESULT(STDMETHODCALLTYPE* PFN_UpdateFfxApiProvider)(void* pData, uint32_t dataSizeInBytes);

HMODULE driverModule = nullptr;
std::once_flag loadOnce;

ProviderInterface upscaler {};
bool upscalerAvailable = false;
std::once_flag initOnce;

std::unordered_set<ffxContext> contexts;
std::mutex contextsMutex;

HMODULE LoadDriverModule()
{
    HMODULE module = nullptr;
    HMODULE memModule = nullptr;
    auto optiPath = Config::Instance()->MainDllPath.value();
    Util::LoadProxyLibrary(L"amdxcffx64.dll", L"", optiPath, &memModule, &module);

    if (module == nullptr && memModule != nullptr)
        module = memModule;

    if (module != nullptr)
    {
        LOG_INFO("amdxcffx64 loaded from game folder");
        return module;
    }

    for (const auto& [luid, path] : Util::GetDriverStore())
    {
        auto dllPath = path / L"amdxcffx64.dll";
        LOG_DEBUG("Trying to load: {}", wstring_to_string(dllPath.c_str()));

        module = NtdllProxy::LoadLibraryExW_Ldr(dllPath.c_str(), NULL, 0);

        if (module != nullptr)
        {
            LOG_INFO(L"amdxcffx64 loaded from {}", dllPath.wstring());
            return module;
        }
    }

    LOG_WARN("Failed to load amdxcffx64.dll");
    return nullptr;
}

// FFX packs provider versions into the low bits of their ids, external providers also set bit 31
feature_version ProviderVersion(uint64_t versionId)
{
    auto packed = static_cast<uint32_t>(versionId) & 0x7FFFFFFF;
    return feature_version(packed >> 22, (packed >> 12) & 0x3FF, packed & 0xFFF);
}

bool QueryUpscaler(HMODULE module)
{
    auto updateFfxApiProvider =
        reinterpret_cast<PFN_UpdateFfxApiProvider>(KernelBaseProxy::GetProcAddress_()(module, "UpdateFfxApiProvider"));

    if (updateFfxApiProvider == nullptr)
    {
        LOG_ERROR("Failed to get UpdateFfxApiProvider");
        return false;
    }

    // FFX SDK 2.0+ sends version 2, the first drivers with FSR 4 only fill in version 1
    for (uint32_t structVersion : { 2u, 1u })
    {
        ExternalProviderData data {};
        data.structVersion = structVersion;
        data.descType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;

        auto owner = State::GetOwner();
        State::DisableChecks(owner);
        auto result = updateFfxApiProvider(&data, sizeof(data));
        State::EnableChecks(owner);

        if (SUCCEEDED(result) && data.provider.createContext != nullptr)
        {
            upscaler = data.provider;
            LOG_INFO("Driver upscaler: {}, struct version: {}", upscaler.versionName, structVersion);
            return true;
        }

        LOG_DEBUG("UpdateFfxApiProvider with struct version {}, result: 0x{:X}", structVersion, (UINT) result);
    }

    return false;
}

bool InitUpscaler()
{
    IdentifyGpu::updateD3d12Capabilities();

    if (IdentifyGpu::getPrimaryGpu().fsr4Support == FSR4Support::None)
    {
        LOG_INFO("GPU doesn't support FSR 4, skipping the driver upscaler");
        return false;
    }

    auto module = FSR4DriverProvider::Module();
    if (module == nullptr)
        return false;

    auto sdkModule = FfxApiProxy::Dx12Module_SR() ? FfxApiProxy::Dx12Module_SR() : FfxApiProxy::Dx12Module();
    FSR4DriverProvider::UpdateInt8Support(sdkModule);

    // The driver silently falls back to FSR 3 when the GPU can't run its FSR 4 models
    auto canRunAnyModel = [module]
    {
        for (size_t i = 0; i < (size_t) FSR4Model::Count; i++)
        {
            auto model = (FSR4Model) i;
            if (FSR4Models::ModuleHasModel(module, model) && FSR4Models::CanGpuRun(model))
                return true;
        }

        return false;
    };

    if (!canRunAnyModel())
    {
        LOG_INFO("The GPU can't run any FSR 4 model of the driver");
        return false;
    }

    return QueryUpscaler(module);
}

} // namespace

HMODULE FSR4DriverProvider::Module()
{
    std::call_once(loadOnce,
                   []
                   {
                       driverModule = LoadDriverModule();

                       FSR4Presets::Hook(driverModule);
                       FSR4Models::Hook(driverModule);
                   });

    return driverModule;
}

HMODULE FSR4DriverProvider::LoadedModule() { return driverModule; }

void FSR4DriverProvider::UpdateInt8Support(HMODULE sdkModule)
{
    auto sdkHasInt8 = FSR4Models::ModuleHasModel(sdkModule, FSR4Model::INT8);
    auto driverHasInt8 = FSR4Models::ModuleHasModel(LoadedModule(), FSR4Model::INT8);

    IdentifyGpu::updateInt8Support(sdkHasInt8, driverHasInt8);
}

bool FSR4DriverProvider::Init()
{
    std::call_once(initOnce, [] { upscalerAvailable = InitUpscaler(); });
    return upscalerAvailable;
}

uint64_t FSR4DriverProvider::VersionId() { return Init() ? upscaler.versionId : 0; }

void FSR4DriverProvider::AddToVersionList(std::vector<uint64_t>& ids, std::vector<const char*>& names, Listing listing)
{
    if (!Init())
        return;

    // FFX modules that Opti doesn't own still get the driver upscaler from IAmdExtFfxApi
    if (std::ranges::find(ids, upscaler.versionId) != ids.end())
        return;

    auto driverVersion = ProviderVersion(upscaler.versionId);
    auto isOlder = [driverVersion](uint64_t id) { return ProviderVersion(id) < driverVersion; };

    if (listing == Listing::WhenNewestUpscaler && !std::ranges::all_of(ids, isOlder))
    {
        LOG_DEBUG("Not listing driver upscaler {}, the FFX SDK has a newer or equal one", upscaler.versionName);
        return;
    }

    auto index = std::distance(ids.begin(), std::ranges::find_if(ids, isOlder));
    ids.insert(ids.begin() + index, upscaler.versionId);
    names.insert(names.begin() + index, upscaler.versionName);
}

bool FSR4DriverProvider::OwnsContext(ffxContext* context)
{
    if (context == nullptr || *context == nullptr)
        return false;

    std::scoped_lock lock(contextsMutex);
    return contexts.contains(*context);
}

ffxReturnCode_t FSR4DriverProvider::CreateContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                                  const ffxAllocationCallbacks* memCb)
{
    if (!Init())
        return FFX_API_RETURN_NO_PROVIDER;

    auto result = upscaler.createContext(context, desc, memCb);
    LOG_INFO("Created driver upscaler context, result: {}", FfxApiProxy::ReturnCodeToString(result));

    if (result == FFX_API_RETURN_OK)
    {
        std::scoped_lock lock(contextsMutex);
        contexts.insert(*context);
    }

    return result;
}

ffxReturnCode_t FSR4DriverProvider::DestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb)
{
    if (!OwnsContext(context))
        return FFX_API_RETURN_ERROR_PARAMETER;

    auto handle = *context;
    auto result = upscaler.destroyContext(context, memCb);

    if (result == FFX_API_RETURN_OK)
    {
        std::scoped_lock lock(contextsMutex);
        contexts.erase(handle);
    }

    return result;
}

ffxReturnCode_t FSR4DriverProvider::Configure(ffxContext* context, const ffxConfigureDescHeader* desc)
{
    return upscaler.configure(context, desc);
}

ffxReturnCode_t FSR4DriverProvider::Query(ffxContext* context, ffxQueryDescHeader* desc)
{
    return upscaler.query(context, desc);
}

ffxReturnCode_t FSR4DriverProvider::Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc)
{
    return upscaler.dispatch(context, desc);
}
