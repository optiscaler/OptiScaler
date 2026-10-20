#include "pch.h"
#include "FSR4Upgrade.h"
#include "FSR4DriverProvider.h"

#include <proxies/FfxApi_Proxy.h>

HRESULT STDMETHODCALLTYPE AmdExtFfxApi::UpdateFfxApiProvider(void* pData, uint32_t dataSizeInBytes)
{
    auto callerModule = Util::GetCallerModule(_ReturnAddress());

    if (callerModule == exeModule)
    {
        LOG_WARN("Called from game module, returning E_NOINTERFACE");
        return E_NOINTERFACE;
    }

    auto effectType = FfxApiProxy::GetType(reinterpret_cast<ExternalProviderData*>(pData)->descType);

    auto effect = magic_enum::enum_name(effectType);
    if (effectType >= FFXStructType::Unknown)
        effect = "???";

    // Opti calls the driver upscaler itself and lists it next to the upscalers of its FFX modules
    if (effectType == FFXStructType::Upscaling && FfxApiProxy::IsUpscalerModule(callerModule))
    {
        LOG_DEBUG("Not giving the driver upscaler to Opti's FFX module");
        return E_NOINTERFACE;
    }

    auto driverModule = FSR4DriverProvider::Module();

    if (effectType == FFXStructType::Upscaling)
        FSR4DriverProvider::UpdateInt8Support(callerModule);

    if (driverModule == nullptr)
        return E_NOINTERFACE;

    if (o_UpdateFfxApiProvider == nullptr)
    {
        o_UpdateFfxApiProvider =
            (PFN_UpdateFfxApiProvider) KernelBaseProxy::GetProcAddress_()(driverModule, "UpdateFfxApiProvider");
        o_UpdateFfxApiProviderEx =
            (PFN_UpdateFfxApiProviderEx) KernelBaseProxy::GetProcAddress_()(driverModule, "UpdateFfxApiProviderEx");

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
