#include "pch.h"
#include "FSR4Upgrade.h"

#include <proxies/FfxApi_Proxy.h>
#include <misc/IdentifyGpu.h>

struct ffxProviderInterface
{
    uint64_t versionId;
    const char* versionName;
    PVOID canProvide;
    PVOID createContext;
    PVOID destroyContext;
    PVOID configure;
    PVOID query;
    PVOID dispatch;
};

struct ExternalProviderData
{
    uint32_t structVersion = 2;
    uint64_t descType;
    ffxProviderInterface provider;
};

HRESULT STDMETHODCALLTYPE AmdExtFfxApi::UpdateFfxApiProvider(void* pData, uint32_t dataSizeInBytes)
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

    static std::optional<bool> sdkSupportsInt8;
    static std::optional<bool> amdxcffx64SupportsInt8;

    if (effectType == FFXStructType::Upscaling && !sdkSupportsInt8.has_value())
    {
        wchar_t sdkDllPath[MAX_PATH] = { 0 };
        GetModuleFileNameW(callerModule, sdkDllPath, MAX_PATH);

        version_t sdkVersion;
        Util::GetFileVersion(sdkDllPath, &sdkVersion, nullptr);

        LOG_TRACE("sdkVersion: {}.{}.{}.{}", sdkVersion.major, sdkVersion.minor, sdkVersion.patch, sdkVersion.reserved);

        sdkSupportsInt8 = sdkVersion >= version_t(4, 1, 1, 0);
        IdentifyGpu::updateInt8Support(sdkSupportsInt8, amdxcffx64SupportsInt8);
    }

    if (o_UpdateFfxApiProvider == nullptr)
    {
        FSR4Upgrade::moduleAmdxcffx64 = nullptr;
        HMODULE memModule = nullptr;
        auto optiPath = Config::Instance()->MainDllPath.value();
        Util::LoadProxyLibrary(L"amdxcffx64.dll", L"", optiPath, &memModule, &FSR4Upgrade::moduleAmdxcffx64);

        if (FSR4Upgrade::moduleAmdxcffx64 == nullptr && memModule != nullptr)
            FSR4Upgrade::moduleAmdxcffx64 = memModule;

        if (FSR4Upgrade::moduleAmdxcffx64 == nullptr)
        {
            auto storePaths = Util::GetDriverStore();

            for (const auto& [luid, path] : storePaths)
            {
                if (FSR4Upgrade::moduleAmdxcffx64 == nullptr)
                {
                    auto dllPath = path / L"amdxcffx64.dll";
                    LOG_DEBUG("Trying to load: {}", wstring_to_string(dllPath.c_str()));
                    FSR4Upgrade::moduleAmdxcffx64 = NtdllProxy::LoadLibraryExW_Ldr(dllPath.c_str(), NULL, 0);

                    if (FSR4Upgrade::moduleAmdxcffx64 != nullptr)
                    {
                        LOG_INFO(L"amdxcffx64 loaded from {}", dllPath.wstring());
                        break;
                    }
                }
            }
        }
        else
        {
            LOG_INFO("amdxcffx64 loaded from game folder");
        }

        if (FSR4Upgrade::moduleAmdxcffx64)
        {
            bool wasInt8ModelSelectionHooked = FSR4ModelSelection::IsCreateModelDriver2Hooked();

            FSR4ModelSelection::Hook(FSR4Upgrade::moduleAmdxcffx64, FSR4Source::DriverDll);

            wchar_t driverDllPath[MAX_PATH] = { 0 };
            GetModuleFileNameW(FSR4Upgrade::moduleAmdxcffx64, driverDllPath, MAX_PATH);

            version_t amdxcffx64Version;
            Util::GetFileVersion(driverDllPath, &amdxcffx64Version);

            LOG_TRACE("amdxcffx64Version: {}.{}.{}.{}", amdxcffx64Version.major, amdxcffx64Version.minor,
                      amdxcffx64Version.patch, amdxcffx64Version.reserved);

            amdxcffx64SupportsInt8 = amdxcffx64Version >= version_t(2, 3, 0, 0);

            // Seems to happen with older Proton builds
            if (!amdxcffx64SupportsInt8.value() && !wasInt8ModelSelectionHooked &&
                FSR4ModelSelection::IsCreateModelDriver2Hooked())
            {
                LOG_TRACE("WAR: Override a likely wrong version read");
                amdxcffx64SupportsInt8 = true;
            }

            IdentifyGpu::updateInt8Support(sdkSupportsInt8, amdxcffx64SupportsInt8);
        }
        else
        {
            LOG_WARN("Failed to load amdxcffx64.dll");

            amdxcffx64SupportsInt8 = false;
            IdentifyGpu::updateInt8Support(sdkSupportsInt8, amdxcffx64SupportsInt8);

            return E_NOINTERFACE;
        }

        o_UpdateFfxApiProvider = (PFN_UpdateFfxApiProvider) KernelBaseProxy::GetProcAddress_()(
            FSR4Upgrade::moduleAmdxcffx64, "UpdateFfxApiProvider");
        o_UpdateFfxApiProviderEx = (PFN_UpdateFfxApiProviderEx) KernelBaseProxy::GetProcAddress_()(
            FSR4Upgrade::moduleAmdxcffx64, "UpdateFfxApiProviderEx");

        if (o_UpdateFfxApiProvider == nullptr)
        {
            LOG_ERROR("Failed to get UpdateFfxApiProvider");
            return E_NOINTERFACE;
        }
    }

    // Prevents the use of FP8 FG on unsupported cards
    if (effectType == FFXStructType::FG && realFsr4Support != FSR4Support::FP8)
        return E_NOINTERFACE;

    // Result 0x80004002 (E_NOINTERFACE) basically means that amdxcffx64 doesn't have a provider for that effect
    if ((effectType == FFXStructType::FG || effectType == FFXStructType::Upscaling ||
         effectType == FFXStructType::SwapchainDX12) &&
        o_UpdateFfxApiProviderEx != nullptr)
    {
        auto owner = State::GetOwner();
        State::DisableChecks(owner);

        magicData data = { { 0, 1, 1, 0 }, nullptr };
        auto result = o_UpdateFfxApiProviderEx(pData, dataSizeInBytes, &data);

        auto level = SUCCEEDED(result) ? spdlog::level::info : spdlog::level::err;
        spdlog::log(level, "UpdateFfxApiProviderEx for: {}, result: 0x{:X}", effect, (UINT) result);

        State::EnableChecks(owner);
        return result;
    }

    else if (o_UpdateFfxApiProvider != nullptr)
    {
        auto owner = State::GetOwner();
        State::DisableChecks(owner);

        auto result = o_UpdateFfxApiProvider(pData, dataSizeInBytes);

        auto level = SUCCEEDED(result) ? spdlog::level::info : spdlog::level::err;
        spdlog::log(level, "UpdateFfxApiProvider for: {}, result: 0x{:X}", effect, (UINT) result);

        State::EnableChecks(owner);
        return result;
    }

    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE AmdExtD3DShaderIntrinsics::GetInfo(void* ShaderIntrinsicsInfo)
{
    if (!State::Instance().isRunningOnLinux && Amdxc64Hooks::o_amdExtD3DShaderIntrinsics)
        return Amdxc64Hooks::o_amdExtD3DShaderIntrinsics->GetInfo(ShaderIntrinsicsInfo);

    LOG_FUNC();
    return S_OK;
}
HRESULT STDMETHODCALLTYPE AmdExtD3DShaderIntrinsics::CheckSupport(AmdExtD3DShaderIntrinsicsSupport intrinsic)
{
    if (!State::Instance().isRunningOnLinux && Amdxc64Hooks::o_amdExtD3DShaderIntrinsics)
        return Amdxc64Hooks::o_amdExtD3DShaderIntrinsics->CheckSupport(intrinsic);

    LOG_TRACE(": {}", magic_enum::enum_name(intrinsic));
    return S_OK;
}
HRESULT STDMETHODCALLTYPE AmdExtD3DShaderIntrinsics::Enable()
{
    if (!State::Instance().isRunningOnLinux && Amdxc64Hooks::o_amdExtD3DShaderIntrinsics)
        return Amdxc64Hooks::o_amdExtD3DShaderIntrinsics->Enable();

    LOG_FUNC();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AmdExtD3DDevice8::GetWaveMatrixProperties(uint64_t* count,
                                                                    AmdExtWaveMatrixProperties* waveMatrixProperties)
{
    LOG_TRACE(": {}", *count);

    waveMatrixProperties->mSize = 16;
    waveMatrixProperties->nSize = 16;
    waveMatrixProperties->kSize = 16;

    if (fsr4Support == FSR4Support::FP8 ||
        (realFsr4Support == FSR4Support::FP8 &&
         Util::WhoIsTheCaller(_ReturnAddress()).starts_with("amd_fidelityfx_framegeneration_dx12")))
    {
        waveMatrixProperties->aType = fp8;
    }
    else
    {
        waveMatrixProperties->aType = float16; // Just anything to fail the checks
    }

    waveMatrixProperties->bType = fp8;

    waveMatrixProperties->cType = float32;
    waveMatrixProperties->resultType = float32;

    waveMatrixProperties->saturatingAccumulation = false;

    // TODO: fill out the rest when improving AmdExtD3DDevice8 support
    *count = 1;

    return S_OK;
}
