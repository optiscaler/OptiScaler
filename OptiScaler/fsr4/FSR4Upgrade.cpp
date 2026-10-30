#include "pch.h"
#include "FSR4Upgrade.h"

#include <proxies/Dxgi_Proxy.h>
#include <detours/detours.h>
#include <scanner/scanner.h>
#include <ffx_framegeneration.h>
#include <ffx_upscale.h>
#include "FSR4ModelSelection.h"
#include "proxies/FfxApi_Proxy.h"
#include <magic_enum.hpp>

#include <mutex>
#include <optional>

#pragma intrinsic(_ReturnAddress)

static HMODULE moduleAmdxc64 = nullptr;
static HMODULE moduleAmdxcffx64 = nullptr;
static HMODULE moduleAmdvkffx64 = nullptr;

static AmdExtD3DDevice8* amdExtD3DDevice8 = nullptr;
static AmdExtD3DShaderIntrinsics* amdExtD3DShaderIntrinsics = nullptr;
static AmdExtD3DFactory* amdExtD3DFactory = nullptr;
static AmdExtD3DFactory* o_amdExtD3DFactory = nullptr;
static AmdExtFfxApi* amdExtFfxApi = nullptr;

static PFN_AmdExtD3DCreateInterface o_AmdExtD3DCreateInterface = nullptr;

typedef decltype(&D3DKMTQueryAdapterInfo) PFN_D3DKMTQueryAdapterInfo;
typedef decltype(&D3DKMTEnumAdapters) PFN_D3DKMTEnumAdapters;
typedef decltype(&D3DKMTCloseAdapter) PFN_D3DKMTCloseAdapter;

// Adapter and driver folder of each adapter
static std::vector<std::pair<LUID, std::filesystem::path>> EnumDriverStores()
{
    std::vector<std::pair<LUID, std::filesystem::path>> result;

    // Load D3DKMT functions dynamically
    bool libraryLoaded = false;
    HMODULE hGdi32 = KernelBaseProxy::GetModuleHandleW_()(L"Gdi32.dll");

    if (hGdi32 == nullptr)
    {
        hGdi32 = NtdllProxy::LoadLibraryExW_Ldr(L"Gdi32.dll", NULL, 0);
        libraryLoaded = hGdi32 != nullptr;
    }

    if (hGdi32 == nullptr)
    {
        LOG_ERROR("Failed to load Gdi32.dll");
        return result;
    }

    do
    {
        auto o_D3DKMTEnumAdapters =
            (PFN_D3DKMTEnumAdapters) KernelBaseProxy::GetProcAddress_()(hGdi32, "D3DKMTEnumAdapters");
        auto o_D3DKMTQueryAdapterInfo =
            (PFN_D3DKMTQueryAdapterInfo) KernelBaseProxy::GetProcAddress_()(hGdi32, "D3DKMTQueryAdapterInfo");
        auto o_D3DKMTCloseAdapter =
            (PFN_D3DKMTCloseAdapter) KernelBaseProxy::GetProcAddress_()(hGdi32, "D3DKMTCloseAdapter");

        if (o_D3DKMTEnumAdapters == nullptr || o_D3DKMTQueryAdapterInfo == nullptr || o_D3DKMTCloseAdapter == nullptr)
        {
            LOG_ERROR("Failed to resolve D3DKMT functions");
            break;
        }

        D3DKMT_UMDFILENAMEINFO umdFileInfo = {};
        D3DKMT_QUERYADAPTERINFO queryAdapterInfo = {};

        queryAdapterInfo.Type = KMTQAITYPE_UMDRIVERNAME;
        queryAdapterInfo.pPrivateDriverData = &umdFileInfo;
        queryAdapterInfo.PrivateDriverDataSize = sizeof(umdFileInfo);

        D3DKMT_ENUMADAPTERS enumAdapters = {};

        // Query the number of adapters first
        if (o_D3DKMTEnumAdapters(&enumAdapters) != 0)
        {
            LOG_ERROR("Failed to enumerate adapters.");
            break;
        }

        // If there are any adapters, the first one should be in the list
        if (enumAdapters.NumAdapters > 0)
        {
            for (size_t i = 0; i < enumAdapters.NumAdapters; i++)
            {
                D3DKMT_ADAPTERINFO adapter = enumAdapters.Adapters[i];
                queryAdapterInfo.hAdapter = adapter.hAdapter;

                auto hr = o_D3DKMTQueryAdapterInfo(&queryAdapterInfo);

                if (hr != 0)
                    LOG_WARN("Failed to query adapter info {:X}", hr);
                else
                    result.emplace_back(adapter.AdapterLuid,
                                        std::filesystem::path(umdFileInfo.UmdFileName).parent_path());

                D3DKMT_CLOSEADAPTER closeAdapter = {};
                closeAdapter.hAdapter = adapter.hAdapter;
                auto closeResult = o_D3DKMTCloseAdapter(&closeAdapter);
                if (closeResult != 0)
                    LOG_ERROR("D3DKMTCloseAdapter error: {:X}", closeResult);
            }
        }
        else
        {
            LOG_ERROR("No adapters found.");
            break;
        }

    } while (false);

    if (libraryLoaded)
        NtdllProxy::FreeLibrary_Ldr(hGdi32);

    return result;
}

static bool SameLuid(const LUID& a, const LUID& b) { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }

std::vector<std::filesystem::path> GetDriverStore(std::optional<LUID> preferred)
{
    std::vector<std::filesystem::path> result;

    // Systems with two GPUs of a vendor have a driver package for each, the preferred one goes first
    for (const auto& [luid, path] : EnumDriverStores())
    {
        if (preferred.has_value() && SameLuid(luid, *preferred))
            result.insert(result.begin(), path);
        else
            result.push_back(path);
    }

    return result;
}

static std::optional<std::filesystem::path> GetDriverStoreOf(const LUID& adapter)
{
    for (const auto& [luid, path] : EnumDriverStores())
    {
        if (SameLuid(luid, adapter))
            return path;
    }

    return std::nullopt;
}

#pragma endregion

void CheckForGPU()
{
    // CheckForGPU already ran before, no need to run it again
    if (State::Instance().isRunningOnRDNA4.has_value() || State::Instance().isRunningOnRDNA3.has_value())
        return;

    // Call init for any case
    DxgiProxy::Init();

    IDXGIFactory* factory = nullptr;
    HRESULT result = DxgiProxy::CreateDxgiFactory_()(__uuidof(factory), &factory);

    if (result != S_OK || factory == nullptr)
        return;

    UINT adapterIndex = 0;
    DXGI_ADAPTER_DESC adapterDesc {};
    IDXGIAdapter* adapter;

    while (factory->EnumAdapters(adapterIndex, &adapter) == S_OK)
    {
        if (adapter == nullptr)
        {
            adapterIndex++;
            continue;
        }

        {
            ScopedSkipSpoofing skipSpoofing {};
            result = adapter->GetDesc(&adapterDesc);
        }

        if (result == S_OK && adapterDesc.VendorId != VendorId::Microsoft)
        {
            if (!State::Instance().isRunningOnRDNA4.has_value() || !State::Instance().isRunningOnRDNA4.value())
                State::Instance().isRunningOnRDNA4 = false;

            if (!State::Instance().isRunningOnRDNA3.has_value() || !State::Instance().isRunningOnRDNA3.value())
                State::Instance().isRunningOnRDNA3 = false;

            std::wstring szName(adapterDesc.Description);
            std::string descStr = std::format("Adapter: {}, VRAM: {} MB", wstring_to_string(szName),
                                              adapterDesc.DedicatedVideoMemory / (1024.0 * 1024.0));
            LOG_INFO("{}", descStr);

            // If GPU is AMD
            if (adapterDesc.VendorId == VendorId::AMD)
            {
                // If GPU Name contains 90XX or GFX12 (Linux) always set it to true
                if (szName.find(L" 90") != std::wstring::npos || szName.find(L" GFX12") != std::wstring::npos)
                {
                    LOG_DEBUG("RDNA4 GPU detected");
                    State::Instance().isRunningOnRDNA4 = true;

                    if (!Config::Instance()->Dx12Upscaler.has_value())
                    {
                        if (State::Instance().WindowsVer == WindowsVersion::Windows11)
                        {
                            LOG_INFO("Setting Dx12Upscaler to fsr31 because RDNA4 GPU is detected");
                            Config::Instance()->Dx12Upscaler.set_volatile_value("fsr31");
                        }
                        else
                        {
                            if (KernelBaseProxy::GetModuleHandleW_()(L"D3D12Core.dll") != nullptr)
                            {
                                LOG_INFO("Setting Dx12Upscaler to fsr31 because RDNA4 GPU is detected");
                                Config::Instance()->Dx12Upscaler.set_volatile_value("fsr31");
                            }
                            else
                            {
                                LOG_WARN("RDNA4 GPU is detected but Agility SDK is not detected!");
                            }
                        }
                    }
                }

                // Don't look, checks for RDNA 3
                if (szName.contains(L"7900") || szName.contains(L"7800") || szName.contains(L"7700") ||
                    szName.contains(L"7600") || szName.find(L" GFX110") != std::wstring::npos)
                {
                    LOG_DEBUG("RDNA3 GPU detected");
                    State::Instance().isRunningOnRDNA3 = true;

                    if (!Config::Instance()->Dx12Upscaler.has_value())
                    {
                        if (State::Instance().WindowsVer == WindowsVersion::Windows11)
                        {
                            LOG_INFO("Setting Dx12Upscaler to fsr31 because RDNA3 GPU is detected");
                            Config::Instance()->Dx12Upscaler.set_volatile_value("fsr31");
                        }
                        else
                        {
                            if (KernelBaseProxy::GetModuleHandleW_()(L"D3D12Core.dll") != nullptr)
                            {
                                LOG_INFO("Setting Dx12Upscaler to fsr31 because RDNA3 GPU is detected");
                                Config::Instance()->Dx12Upscaler.set_volatile_value("fsr31");
                            }
                            else
                            {
                                LOG_WARN("RDNA3 GPU is detected but Agility SDK is not detected!");
                            }
                        }
                    }
                }
            }
        }
        else
        {
            LOG_DEBUG("Can't get description of adapter: {}", adapterIndex);
        }

        adapter->Release();
        adapter = nullptr;
        adapterIndex++;
    }

    factory->Release();
    factory = nullptr;

    // If not set at Config, enable/disable Fsr4Update according to GPU detection
    if (!Config::Instance()->Fsr4Update.has_value())
        Config::Instance()->Fsr4Update.set_volatile_value(State::Instance().isRunningOnRDNA4.value() ||
                                                          State::Instance().isRunningOnRDNA3.value());

    LOG_INFO("RDNA4: {}, RDNA3: {}, Fsr4Update: {}", State::Instance().isRunningOnRDNA4.value(),
             State::Instance().isRunningOnRDNA3.value(), Config::Instance()->Fsr4Update.value_or_default());
}

// Adapter of the device that asked for IAmdExtFfxApi, the driver dll is loaded from its driver package
static std::optional<LUID> ffxApiAdapter;

static std::optional<LUID> AdapterOf(IUnknown* object)
{
    ID3D12Device* device = nullptr;
    if (object == nullptr || FAILED(object->QueryInterface(IID_PPV_ARGS(&device))))
        return std::nullopt;

    auto luid = device->GetAdapterLuid();
    device->Release();
    return luid;
}

// Driver dll that gets itself for every FFX SDK dll it looks up while it sets its SDK version
static std::atomic<HMODULE> primingDll = nullptr;
static decltype(&GetModuleHandleW) o_driverGetModuleHandleW = nullptr;

// Only the driver dlls call it, through their patched imports
static HMODULE WINAPI hkDriverGetModuleHandleW(LPCWSTR lpModuleName)
{
    auto caller = Util::GetCallerModule(_ReturnAddress());

    if (caller != nullptr && caller == primingDll && lpModuleName != nullptr &&
        _wcsnicmp(lpModuleName, L"amd_fidelityfx_", 15) == 0)
    {
        return caller;
    }

    auto module = o_driverGetModuleHandleW(lpModuleName);

    // amdvkffx64 only gives out providers when it sees a signed FFX SDK dll, the driver dll stands in for missing ones
    if (module == nullptr && caller != nullptr && caller == moduleAmdvkffx64 && lpModuleName != nullptr &&
        (_wcsicmp(lpModuleName, L"amd_fidelityfx_loader_vk.dll") == 0 ||
         _wcsicmp(lpModuleName, L"amd_fidelityfx_upscaler_vk.dll") == 0 ||
         _wcsicmp(lpModuleName, L"amd_fidelityfx_framegeneration_vk.dll") == 0))
    {
        return caller;
    }

    return module;
}

static BOOL CALLBACK PatchGetModuleHandleW(PVOID pContext, DWORD nOrdinal, LPCSTR pszFunc, PVOID* ppvFunc)
{
    if (pszFunc == nullptr || ppvFunc == nullptr || strcmp(pszFunc, "GetModuleHandleW") != 0)
        return TRUE;

    DWORD oldProtect = 0;
    if (!VirtualProtect(ppvFunc, sizeof(PVOID), PAGE_READWRITE, &oldProtect))
        return FALSE;

    // Every driver dll imports the same kernel32 export
    if (o_driverGetModuleHandleW == nullptr)
        o_driverGetModuleHandleW = reinterpret_cast<decltype(&GetModuleHandleW)>(*ppvFunc);

    *ppvFunc = reinterpret_cast<PVOID>(&hkDriverGetModuleHandleW);
    VirtualProtect(ppvFunc, sizeof(PVOID), oldProtect, &oldProtect);

    *static_cast<bool*>(pContext) = true;
    return FALSE;
}

// The driver keeps the SDK version of the first FFX dll it checks for the rest of the process. An old one, like a
// legacy amd_fidelityfx_dx12.dll in the game folder, leaves the driver upscaler and frame generation out. This
// request runs before any other and has the driver dll check itself, its file version counts as the newest SDK
static void PrimeSdkVersion(HMODULE driverDll, PFN_UpdateFfxApiProvider updateFfxApiProvider)
{
    bool patched = false;
    DetourEnumerateImportsEx(driverDll, &patched, nullptr, PatchGetModuleHandleW);

    if (!patched)
    {
        LOG_WARN("Couldn't patch GetModuleHandleW of {:X}", (size_t) driverDll);
        return;
    }

    ExternalProviderData data {};
    data.structVersion = 2;
    data.descType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;

    State::DisableChecks(1);
    primingDll = driverDll;
    auto result = updateFfxApiProvider(&data, offsetof(ExternalProviderData, isSupported));
    primingDll = nullptr;
    State::EnableChecks(1);

    LOG_DEBUG("Set the SDK version of {:X}, result: {:#X}", (size_t) driverDll, (UINT) result);
}

// A copy next to Opti overrides the one of the driver
static HMODULE LoadDriverDll(const wchar_t* name, std::optional<LUID> adapter)
{
    HMODULE module = nullptr;
    HMODULE memModule = nullptr;
    auto optiPath = Config::Instance()->MainDllPath.value();
    Util::LoadProxyLibrary(name, L"", optiPath, &memModule, &module);

    if (module == nullptr && memModule != nullptr)
        module = memModule;

    if (module != nullptr)
    {
        LOG_INFO(L"{} loaded from game folder", name);
        return module;
    }

    for (const auto& storePath : GetDriverStore(adapter))
    {
        auto dllPath = storePath / name;
        LOG_DEBUG("Trying to load: {}", wstring_to_string(dllPath.c_str()));
        module = NtdllProxy::LoadLibraryExW_Ldr(dllPath.c_str(), NULL, 0);

        if (module != nullptr)
        {
            LOG_INFO(L"{} loaded from {}", name, dllPath.wstring());
            return module;
        }
    }

    LOG_WARN(L"Failed to load {}", name);
    return nullptr;
}

HMODULE LoadFSR4DriverDll(API api)
{
    static std::once_flag dx12Once;
    static std::once_flag vkOnce;

    const bool isDx12 = api == API::DX12;
    auto& module = isDx12 ? moduleAmdxcffx64 : moduleAmdvkffx64;

    std::call_once(isDx12 ? dx12Once : vkOnce,
                   [&]
                   {
                       // The DX12 one has to come from the driver of the device that asked for IAmdExtFfxApi
                       auto loaded = LoadDriverDll(isDx12 ? L"amdxcffx64.dll" : L"amdvkffx64.dll",
                                                   isDx12 ? ffxApiAdapter : std::nullopt);
                       if (loaded == nullptr)
                           return;

                       FSR4ModelSelection::Hook(loaded, FSR4Source::DriverDll, api);

                       // Set before priming, the Vulkan stand-in checks for it
                       module = loaded;

                       auto update = reinterpret_cast<PFN_UpdateFfxApiProvider>(
                           KernelBaseProxy::GetProcAddress_()(loaded, "UpdateFfxApiProvider"));

                       if (update != nullptr)
                           PrimeSdkVersion(loaded, update);
                   });

    return module;
}

struct AmdExtFfxApi : public IAmdExtFfxApi
{
    PFN_UpdateFfxApiProvider o_UpdateFfxApiProviderDx12 = nullptr;
    PFN_UpdateFfxApiProviderEx o_UpdateFfxApiProviderExDx12 = nullptr;

    HRESULT STDMETHODCALLTYPE UpdateFfxApiProvider(void* pData, uint32_t dataSizeInBytes) override
    {
        auto returnAddress = _ReturnAddress();
        auto callerModule = Util::GetCallerModule(returnAddress);

        if (callerModule == exeModule)
        {
            LOG_WARN("Called from game module, returning E_NOINTERFACE");
            return E_NOINTERFACE;
        }

        auto effectType = FfxApiProxy::GetType(reinterpret_cast<ExternalProviderData*>(pData)->descType);

        auto effect = magic_enum::enum_name(effectType);
        if (effectType >= FFXStructType::Unknown)
            effect = "???";

        // Dx12
        if (o_UpdateFfxApiProviderDx12 == nullptr)
        {
            if (LoadFSR4DriverDll(API::DX12) == nullptr)
                return E_NOINTERFACE;

            o_UpdateFfxApiProviderDx12 =
                (PFN_UpdateFfxApiProvider) KernelBaseProxy::GetProcAddress_()(moduleAmdxcffx64, "UpdateFfxApiProvider");
            o_UpdateFfxApiProviderExDx12 = (PFN_UpdateFfxApiProviderEx) KernelBaseProxy::GetProcAddress_()(
                moduleAmdxcffx64, "UpdateFfxApiProviderEx");

            if (o_UpdateFfxApiProviderDx12 == nullptr)
            {
                LOG_ERROR("Failed to get UpdateFfxApiProviderDx12");
                return E_NOINTERFACE;
            }
        }

        // Prevent RDNA3 from running MLFG when not on Linux
        if (effectType == FFXStructType::FG && State::Instance().isRunningOnRDNA3 &&
            State::Instance().isRunningOnRDNA3.value())
        {
            const char* envvar = getenv("DXIL_SPIRV_CONFIG");
            bool rdna3fp8 = envvar && strstr(envvar, "wmma_rdna3_workaround");

            if (!rdna3fp8)
                return E_NOINTERFACE;
        }

        // Dx12
        // Result 0x80004002 (E_NOINTERFACE) basically means that amdxcffx64 doesn't have a provider for that effect
        if ((effectType == FFXStructType::FG || effectType == FFXStructType::Upscaling ||
             effectType == FFXStructType::SwapchainDX12) &&
            o_UpdateFfxApiProviderExDx12 != nullptr)
        {
            State::DisableChecks(1);

            // Options { type, effect, enable, pad }: frame generation (1) and its swapchain (5) are only
            // given out when enabled, the driver keeps the enabled state for later calls
            magicData swapchain = { { 0, 5, 1, 0 }, nullptr };
            magicData data = { { 0, 1, 1, 0 }, &swapchain };
            auto result = o_UpdateFfxApiProviderExDx12(pData, dataSizeInBytes, &data);

            auto level = SUCCEEDED(result) ? spdlog::level::info : spdlog::level::err;
            spdlog::log(level, "UpdateFfxApiProviderExDx12 for: {}, result: {:#X}", effect, (UINT) result);

            State::EnableChecks(1);
            return result;
        }

        else if (o_UpdateFfxApiProviderDx12 != nullptr)
        {
            State::DisableChecks(1);

            auto result = o_UpdateFfxApiProviderDx12(pData, dataSizeInBytes);

            auto level = SUCCEEDED(result) ? spdlog::level::info : spdlog::level::err;
            spdlog::log(level, "UpdateFfxApiProviderDx12 for: {}, result: {:#X}", effect, (UINT) result);

            State::EnableChecks(1);
            return result;
        }

        return E_NOINTERFACE;
    }

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }

    ULONG __stdcall AddRef(void) override { return 0; }

    ULONG __stdcall Release(void) override { return 0; }
};

#define STUB(number)                                                                                                   \
    HRESULT STDMETHODCALLTYPE unknown##number()                                                                        \
    {                                                                                                                  \
        LOG_FUNC();                                                                                                    \
        return S_OK;                                                                                                   \
    }

struct AmdExtD3DShaderIntrinsics : public IAmdExtD3DShaderIntrinsics
{
    HRESULT STDMETHODCALLTYPE GetInfo(void* ShaderIntrinsicsInfo)
    {
        LOG_FUNC();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckSupport(AmdExtD3DShaderIntrinsicsSupport intrinsic)
    {
        LOG_TRACE(": {}", magic_enum::enum_name(intrinsic));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Enable()
    {
        LOG_FUNC();
        return S_OK;
    }

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }
    ULONG __stdcall AddRef(void) override { return 0; }
    ULONG __stdcall Release(void) override { return 0; }
};

struct AmdExtD3DDevice8 : public IAmdExtD3DDevice8
{
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
    HRESULT STDMETHODCALLTYPE GetWaveMatrixProperties(uint64_t* count, AmdExtWaveMatrixProperties* waveMatrixProperties)
    {
        LOG_TRACE(": {}", *count);

        waveMatrixProperties->mSize = 16;
        waveMatrixProperties->nSize = 16;
        waveMatrixProperties->kSize = 16;

        waveMatrixProperties->aType = fp8;
        waveMatrixProperties->bType = fp8;

        waveMatrixProperties->cType = float32;
        waveMatrixProperties->resultType = float32;

        waveMatrixProperties->saturatingAccumulation = false;

        if (State::Instance().isRunningOnRDNA3 && State::Instance().isRunningOnRDNA3.value())
        {
            const char* envvar = getenv("DXIL_SPIRV_CONFIG");
            bool rdna3fp8 = envvar && strstr(envvar, "wmma_rdna3_workaround");

            // Try to allow for MLFG on Rdna3 on Linux while forcing int8 for the upscaler
            if (!Util::WhoIsTheCaller(_ReturnAddress()).starts_with("amd_fidelityfx_framegeneration_dx12") || !rdna3fp8)
            {
                if (!rdna3fp8 || FSR4ModelSelection::IsInt8FsrHookedDx12())
                    waveMatrixProperties->aType = float16; // Anything to fail the check
            }
        }

        return S_OK;
    }

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }
    ULONG __stdcall AddRef(void) override { return 0; }
    ULONG __stdcall Release(void) override { return 0; }
};

struct AmdExtD3DFactory : public IAmdExtD3DFactory
{
    HRESULT STDMETHODCALLTYPE CreateInterface(IUnknown* pOuter, REFIID riid, void** ppvObject) override
    {
        if (riid == __uuidof(IAmdExtD3DShaderIntrinsics))
        {
            if (amdExtD3DShaderIntrinsics == nullptr)
                amdExtD3DShaderIntrinsics = new AmdExtD3DShaderIntrinsics();

            *ppvObject = amdExtD3DShaderIntrinsics;

            LOG_INFO("Custom IAmdExtD3DShaderIntrinsics queried, returning custom AmdExtD3DShaderIntrinsics");

            return S_OK;
        }
        else if (riid == __uuidof(IAmdExtD3DDevice8))
        {
            if (amdExtD3DDevice8 == nullptr)
                amdExtD3DDevice8 = new AmdExtD3DDevice8();

            *ppvObject = amdExtD3DDevice8;

            LOG_INFO("Custom IAmdExtD3DDevice8 queried, returning custom AmdExtD3DDevice8");

            return S_OK;
        }
        else if (o_amdExtD3DFactory)
        {
            return o_amdExtD3DFactory->CreateInterface(pOuter, riid, ppvObject);
        }

        return E_NOINTERFACE;
    }

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (o_amdExtD3DFactory)
            return o_amdExtD3DFactory->QueryInterface(riid, ppvObject);

        return E_NOTIMPL;
    }
    ULONG __stdcall AddRef(void) override
    {
        if (o_amdExtD3DFactory)
            return o_amdExtD3DFactory->AddRef();

        return 0;
    }
    ULONG __stdcall Release(void) override
    {
        if (o_amdExtD3DFactory)
        {
            auto result = o_amdExtD3DFactory->Release();

            if (result == 0)
                o_amdExtD3DFactory = nullptr;

            return result;
        }

        return 0;
    }
};

// AMD flip meter, the FSR FG swapchain 4.0 creates it through IAmdExtD3DFactory to let the driver pace presents

typedef HRESULT(STDMETHODCALLTYPE* PFN_AmdExtD3DFactory_CreateInterface)(IAmdExtD3DFactory* This, IUnknown* pOuter,
                                                                         REFIID riid, void** ppvObject);
typedef HRESULT(STDMETHODCALLTYPE* PFN_AmdExtFlipMeter_Config)(IAmdExtFlipMeter* This, AmdExtFlipMeterConfig* config);
typedef ULONG(STDMETHODCALLTYPE* PFN_AmdExtFlipMeter_Release)(IAmdExtFlipMeter* This);

static PFN_AmdExtD3DFactory_CreateInterface o_FactoryCreateInterface = nullptr;
static PFN_AmdExtFlipMeter_Config o_FlipMeterSetConfig = nullptr;
static PFN_AmdExtFlipMeter_Config o_FlipMeterGetConfig = nullptr;
static PFN_AmdExtFlipMeter_Release o_FlipMeterRelease = nullptr;

// Recursive in case the driver releases the interface from within its own calls
static std::recursive_mutex flipMeterMutex;
// Not ref counted to not keep the swapchain alive, cleared on its final Release
static IAmdExtFlipMeter* flipMeter = nullptr;
static std::optional<AmdExtFlipMeterConfig> flipMeterRequested {};

static HRESULT SetFlipMeterConfig(IAmdExtFlipMeter* meter, const AmdExtFlipMeterConfig& requested)
{
    // Modify a copy so FSR keeps its requested state and doesn't try to re-apply it
    auto config = requested;
    if (Config::Instance()->FSRFGDisableFlipMetering.value_or_default())
        config.mode = 0;

    auto result = o_FlipMeterSetConfig(meter, &config);

    LOG_INFO("Flip meter requested mode: {}, applied mode: {}, flags: {:X}, result: {:X}", requested.mode, config.mode,
             config.flags, (UINT) result);

    if (SUCCEEDED(result))
        State::Instance().amdFlipMeterMode = config.mode;

    return result;
}

void ApplyFlipMeterOverride()
{
    std::scoped_lock lock(flipMeterMutex);

    if (flipMeter != nullptr && flipMeterRequested.has_value() && o_FlipMeterSetConfig != nullptr)
        SetFlipMeterConfig(flipMeter, flipMeterRequested.value());
}

static ULONG STDMETHODCALLTYPE hkFlipMeterRelease(IAmdExtFlipMeter* This)
{
    std::scoped_lock lock(flipMeterMutex);

    auto refCount = o_FlipMeterRelease(This);

    if (refCount == 0 && This == flipMeter)
    {
        LOG_DEBUG("IAmdExtFlipMeter released");
        flipMeter = nullptr;
        flipMeterRequested.reset();
        State::Instance().amdFlipMeterCreated = false;
        State::Instance().amdFlipMeterMode = 0;
    }

    return refCount;
}

static HRESULT STDMETHODCALLTYPE hkFlipMeterSetConfig(IAmdExtFlipMeter* This, AmdExtFlipMeterConfig* config)
{
    std::scoped_lock lock(flipMeterMutex);

    if (This == flipMeter)
        flipMeterRequested = *config;

    return SetFlipMeterConfig(This, *config);
}

// FSR reads the config back when SetConfig fails
static HRESULT STDMETHODCALLTYPE hkFlipMeterGetConfig(IAmdExtFlipMeter* This, AmdExtFlipMeterConfig* config)
{
    std::scoped_lock lock(flipMeterMutex);

    auto result = o_FlipMeterGetConfig(This, config);

    LOG_DEBUG("Flip meter mode: {}, result: {:X}", config->mode, (UINT) result);

    if (SUCCEEDED(result) && This == flipMeter)
        State::Instance().amdFlipMeterMode = config->mode;

    return result;
}

static void HookFlipMeter(IAmdExtFlipMeter* meter)
{
    if (o_FlipMeterSetConfig != nullptr)
        return;

    // IUnknown: 0 QueryInterface, 1 AddRef, 2 Release, then 3 GetCaps, 4 SetConfig, 5 GetConfig
    auto vtable = *(void***) meter;
    o_FlipMeterRelease = (PFN_AmdExtFlipMeter_Release) vtable[2];
    o_FlipMeterSetConfig = (PFN_AmdExtFlipMeter_Config) vtable[4];
    o_FlipMeterGetConfig = (PFN_AmdExtFlipMeter_Config) vtable[5];

    LOG_DEBUG("Hooking IAmdExtFlipMeter");
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&) o_FlipMeterRelease, hkFlipMeterRelease);
    DetourAttach(&(PVOID&) o_FlipMeterSetConfig, hkFlipMeterSetConfig);
    DetourAttach(&(PVOID&) o_FlipMeterGetConfig, hkFlipMeterGetConfig);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to attach detour: {:X}", detourResult);
        o_FlipMeterRelease = nullptr;
        o_FlipMeterSetConfig = nullptr;
        o_FlipMeterGetConfig = nullptr;
    }
}

static HRESULT STDMETHODCALLTYPE hkFactoryCreateInterface(IAmdExtD3DFactory* This, IUnknown* pOuter, REFIID riid,
                                                          void** ppvObject)
{
    auto result = o_FactoryCreateInterface(This, pOuter, riid, ppvObject);

    if (riid == __uuidof(IAmdExtFlipMeter))
    {
        LOG_INFO("IAmdExtFlipMeter created for swapchain: {:X}, result: {:X}", (size_t) pOuter, (UINT) result);

        if (SUCCEEDED(result) && *ppvObject != nullptr)
        {
            HookFlipMeter((IAmdExtFlipMeter*) *ppvObject);

            std::scoped_lock lock(flipMeterMutex);
            flipMeter = (IAmdExtFlipMeter*) *ppvObject;
            flipMeterRequested.reset();
            State::Instance().amdFlipMeterCreated = true;
        }
    }

    return result;
}

static void HookFactory(IAmdExtD3DFactory* factory)
{
    if (o_FactoryCreateInterface != nullptr)
        return;

    // IUnknown: 0 QueryInterface, 1 AddRef, 2 Release, then 3 CreateInterface
    auto vtable = *(void***) factory;
    o_FactoryCreateInterface = (PFN_AmdExtD3DFactory_CreateInterface) vtable[3];

    LOG_DEBUG("Hooking IAmdExtD3DFactory::CreateInterface");
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&) o_FactoryCreateInterface, hkFactoryCreateInterface);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to attach detour: {:X}", detourResult);
        o_FactoryCreateInterface = nullptr;
    }
}

// The hooked amdxc64 is the first one loaded, with two GPUs of a vendor it can belong to the other driver package.
// A device only works with the extension of its own driver, which is loaded by d3d12 from its driver package
static PFN_AmdExtD3DCreateInterface CreateInterfaceOf(IUnknown* pOuter)
{
    auto adapter = AdapterOf(pOuter);
    if (!adapter.has_value())
        return o_AmdExtD3DCreateInterface;

    auto store = GetDriverStoreOf(*adapter);
    if (!store.has_value())
        return o_AmdExtD3DCreateInterface;

    auto module = KernelBaseProxy::GetModuleHandleW_()((*store / L"amdxc64.dll").c_str());

    // The export of the hooked module would call the hook again
    if (module == nullptr || module == moduleAmdxc64)
        return o_AmdExtD3DCreateInterface;

    auto createInterface = reinterpret_cast<PFN_AmdExtD3DCreateInterface>(
        KernelBaseProxy::GetProcAddress_()(module, "AmdExtD3DCreateInterface"));

    if (createInterface == nullptr)
        return o_AmdExtD3DCreateInterface;

    static std::once_flag logOnce;
    std::call_once(logOnce, [&] { LOG_INFO(L"Using amdxc64 of the device's driver: {}", store->wstring()); });

    return createInterface;
}

// Original call, hooking the real factory on the way so the flip meter is seen
static HRESULT CreateInterfaceOriginal(IUnknown* pOuter, REFIID riid, void** ppvObject)
{
    auto result = CreateInterfaceOf(pOuter)(pOuter, riid, ppvObject);

    // The FSR 4 support checks of the FFX SDK and the driver rely on these
    if (FAILED(result))
    {
        wchar_t iid[40] {};
        StringFromGUID2(riid, iid, 40);
        LOG_WARN(L"AmdExtD3DCreateInterface {} failed: {:X}", iid, (UINT) result);
    }

    if (SUCCEEDED(result) && riid == __uuidof(IAmdExtD3DFactory) && *ppvObject != nullptr)
        HookFactory((IAmdExtD3DFactory*) *ppvObject);

    return result;
}

void InitFSR4Update()
{
    if (Config::Instance()->Fsr4Update.has_value() && !Config::Instance()->Fsr4Update.value())
        return;

    if (o_AmdExtD3DCreateInterface != nullptr)
        return;

    LOG_DEBUG("");

    // For FSR4 Upgrade
    moduleAmdxc64 = KernelBaseProxy::GetModuleHandleW_()(L"amdxc64.dll");

    if (moduleAmdxc64 == nullptr && !Config::Instance()->Fsr4DoNotLoadAmdxc64.value_or_default())
        moduleAmdxc64 = NtdllProxy::LoadLibraryExW_Ldr(L"amdxc64.dll", NULL, 0);

    if (moduleAmdxc64 != nullptr)
    {
        wchar_t amdxc64Path[MAX_PATH] {};
        GetModuleFileNameW(moduleAmdxc64, amdxc64Path, MAX_PATH);
        LOG_INFO(L"amdxc64.dll loaded from {}", amdxc64Path);
        o_AmdExtD3DCreateInterface = (PFN_AmdExtD3DCreateInterface) KernelBaseProxy::GetProcAddress_()(
            moduleAmdxc64, "AmdExtD3DCreateInterface");

        if (o_AmdExtD3DCreateInterface != nullptr)
        {
            LOG_DEBUG("Hooking AmdExtD3DCreateInterface");
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&) o_AmdExtD3DCreateInterface, hkAmdExtD3DCreateInterface);
            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to attach detour: {:X}", detourResult);
                o_AmdExtD3DCreateInterface = nullptr;
            }
        }
    }
    else
    {
        LOG_INFO("Failed to load amdxc64.dll");
    }
}

HMODULE GetFSR4Module() { return moduleAmdxcffx64; }

HRESULT STDMETHODCALLTYPE hkAmdExtD3DCreateInterface(IUnknown* pOuter, REFIID riid, void** ppvObject)
{
    CheckForGPU();

    if (!Config::Instance()->Fsr4Update.value_or_default() && o_AmdExtD3DCreateInterface != nullptr)
        return CreateInterfaceOriginal(pOuter, riid, ppvObject);

    // Proton bleeding edge ships amdxc64 that is missing some required functions
    else if (riid == __uuidof(IAmdExtD3DFactory) && State::Instance().isRunningOnLinux)
    {
        // Required for the custom AmdExtFfxApi, lack of it triggers visual glitches
        if (amdExtD3DFactory == nullptr)
            amdExtD3DFactory = new AmdExtD3DFactory();

        *ppvObject = amdExtD3DFactory;

        LOG_INFO("IAmdExtD3DFactory queried, returning custom AmdExtD3DFactory");

        if (o_AmdExtD3DCreateInterface != nullptr && o_amdExtD3DFactory == nullptr)
            o_AmdExtD3DCreateInterface(pOuter, riid, (void**) &o_amdExtD3DFactory);

        // The custom factory forwards to the real one
        if (o_amdExtD3DFactory != nullptr)
            HookFactory(o_amdExtD3DFactory);

        return S_OK;
    }

    else if (riid == __uuidof(IAmdExtFfxApi))
    {
        if (!ffxApiAdapter.has_value())
            ffxApiAdapter = AdapterOf(pOuter);

        if (amdExtFfxApi == nullptr)
            amdExtFfxApi = new AmdExtFfxApi();

        // Return custom one
        *ppvObject = amdExtFfxApi;

        LOG_INFO("IAmdExtFfxApi queried, returning custom AmdExtFfxApi");

        return S_OK;
    }

    else if (o_AmdExtD3DCreateInterface != nullptr)
        return CreateInterfaceOriginal(pOuter, riid, ppvObject);

    return E_NOINTERFACE;
}
