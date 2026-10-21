#include "pch.h"
#include "FSR4DriverProviderDx12.h"

#include "FSR4Models.h"

#include <misc/IdentifyGpu.h>
#include <proxies/FfxApi_Proxy.h>

#include <ffx_upscale.h>

namespace
{
// Struct version 1 requests need a signed amd_fidelityfx_dx12.dll, newer ones the upscaler or frame generation dll of
// the effect and a signed or missing loader. Every driver with FSR 4 answers version 1, so it's the only one that gets
// a stand-in
FSR4DriverDll amdxcffx64 { L"amdxcffx64.dll", { L"amd_fidelityfx_dx12.dll" } };

DriverProvider upscaler {};
bool upscalerAvailable = false;
std::once_flag initOnce;

std::unordered_set<ffxContext> contexts;
std::mutex contextsMutex;

// FFX packs provider versions into the low bits of their ids, external providers also set bit 31
feature_version ProviderVersion(uint64_t versionId)
{
    auto packed = static_cast<uint32_t>(versionId) & 0x7FFFFFFF;
    return feature_version(packed >> 22, (packed >> 12) & 0x3FF, packed & 0xFFF);
}

bool QueryUpscaler()
{
    // FFX SDK 2.0+ sends version 2, the first drivers with FSR 4 only fill in version 1.
    // Version 2 also fails when Opti's FFX dlls aren't loaded, version 1 then goes through the stand-in
    auto provider = amdxcffx64.QueryProvider(FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE, { 2u, 1u });

    if (!provider.has_value())
        return false;

    upscaler = std::move(*provider);
    return true;
}

bool InitUpscaler()
{
    IdentifyGpu::updateD3d12Capabilities();

    if (IdentifyGpu::getPrimaryGpu().fsr4Support == FSR4Support::None)
    {
        LOG_INFO("GPU doesn't support FSR 4, skipping the driver upscaler");
        return false;
    }

    auto module = FSR4DriverProviderDx12::Module();
    if (module == nullptr)
        return false;

    auto sdkModule = FfxApiProxy::Dx12Module_SR() ? FfxApiProxy::Dx12Module_SR() : FfxApiProxy::Dx12Module();
    FSR4DriverProviderDx12::UpdateInt8Support(sdkModule);

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

    return QueryUpscaler();
}

} // namespace

FSR4DriverDll& FSR4DriverProviderDx12::Dll() { return amdxcffx64; }

void FSR4DriverProviderDx12::UpdateInt8Support(HMODULE sdkModule)
{
    auto sdkHasInt8 = FSR4Models::ModuleHasModel(sdkModule, FSR4Model::INT8);
    auto driverHasInt8 = FSR4Models::ModuleHasModel(LoadedModule(), FSR4Model::INT8);

    IdentifyGpu::updateInt8Support(sdkHasInt8, driverHasInt8);
}

bool FSR4DriverProviderDx12::Init()
{
    std::call_once(initOnce, [] { upscalerAvailable = InitUpscaler(); });
    return upscalerAvailable;
}

uint64_t FSR4DriverProviderDx12::VersionId() { return Init() ? upscaler.functions.versionId : 0; }

void FSR4DriverProviderDx12::AddToVersionList(std::vector<uint64_t>& ids, std::vector<const char*>& names,
                                              Listing listing)
{
    if (!Init())
        return;

    // FFX modules that Opti doesn't own still get the driver upscaler from IAmdExtFfxApi
    if (std::ranges::find(ids, upscaler.functions.versionId) != ids.end())
        return;

    auto driverVersion = ProviderVersion(upscaler.functions.versionId);
    auto isOlder = [driverVersion](uint64_t id) { return ProviderVersion(id) < driverVersion; };

    if (listing == Listing::WhenNewestUpscaler && !std::ranges::all_of(ids, isOlder))
    {
        LOG_DEBUG("Not listing driver upscaler {}, the FFX SDK has a newer or equal one", upscaler.versionName);
        return;
    }

    auto index = std::distance(ids.begin(), std::ranges::find_if(ids, isOlder));
    ids.insert(ids.begin() + index, upscaler.functions.versionId);
    names.insert(names.begin() + index, upscaler.versionName.c_str());
}

bool FSR4DriverProviderDx12::OwnsContext(ffxContext* context)
{
    if (context == nullptr || *context == nullptr)
        return false;

    std::scoped_lock lock(contextsMutex);
    return contexts.contains(*context);
}

ffxReturnCode_t FSR4DriverProviderDx12::CreateContext(ffxContext* context, ffxCreateContextDescHeader* desc,
                                                      const ffxAllocationCallbacks* memCb)
{
    if (!Init())
        return FFX_API_RETURN_NO_PROVIDER;

    auto result = upscaler.functions.createContext(context, desc, memCb);
    LOG_INFO("Created driver upscaler context, result: {}", FfxApiProxy::ReturnCodeToString(result));

    if (result == FFX_API_RETURN_OK)
    {
        std::scoped_lock lock(contextsMutex);
        contexts.insert(*context);
    }

    return result;
}

ffxReturnCode_t FSR4DriverProviderDx12::DestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb)
{
    if (!OwnsContext(context))
        return FFX_API_RETURN_ERROR_PARAMETER;

    auto handle = *context;
    auto result = upscaler.functions.destroyContext(context, memCb);

    if (result == FFX_API_RETURN_OK)
    {
        std::scoped_lock lock(contextsMutex);
        contexts.erase(handle);
    }

    return result;
}

ffxReturnCode_t FSR4DriverProviderDx12::Configure(ffxContext* context, const ffxConfigureDescHeader* desc)
{
    return upscaler.functions.configure(context, desc);
}

ffxReturnCode_t FSR4DriverProviderDx12::Query(ffxContext* context, ffxQueryDescHeader* desc)
{
    return upscaler.functions.query(context, desc);
}

ffxReturnCode_t FSR4DriverProviderDx12::Dispatch(ffxContext* context, const ffxDispatchDescHeader* desc)
{
    return upscaler.functions.dispatch(context, desc);
}

HRESULT STDMETHODCALLTYPE AmdExtFfxApi::UpdateFfxApiProvider(void* pData, uint32_t dataSizeInBytes)
{
    auto callerModule = Util::GetCallerModule(_ReturnAddress());

    if (callerModule == exeModule)
    {
        LOG_WARN("Called from game module, returning E_NOINTERFACE");
        return E_NOINTERFACE;
    }

    auto effectType = FfxApiProxy::GetType(static_cast<ExternalProviderData*>(pData)->descType);

    auto effect = magic_enum::enum_name(effectType);
    if (effectType >= FFXStructType::Unknown)
        effect = "???";

    // Opti calls the driver upscaler itself and lists it next to the upscalers of its FFX modules
    if (effectType == FFXStructType::Upscaling && FfxApiProxy::IsUpscalerModule(callerModule))
    {
        LOG_DEBUG("Not giving the driver upscaler to Opti's FFX module");
        return E_NOINTERFACE;
    }

    auto driverModule = FSR4DriverProviderDx12::Module();

    if (effectType == FFXStructType::Upscaling)
        FSR4DriverProviderDx12::UpdateInt8Support(callerModule);

    if (driverModule == nullptr)
        return E_NOINTERFACE;

    // Prevents the use of FP8 FG on unsupported cards
    if (effectType == FFXStructType::FG && realFsr4Support != FSR4Support::FP8)
        return E_NOINTERFACE;

    // Frame generation and its swapchain are only given out when enabled through the options
    ProviderOption swapchain { .effect = ProviderEffect::FrameGenerationSwapchain };
    ProviderOption frameGeneration { .effect = ProviderEffect::FrameGeneration, .next = &swapchain };
    auto useOptions = effectType == FFXStructType::FG || effectType == FFXStructType::Upscaling ||
                      effectType == FFXStructType::SwapchainDX12;

    auto result = amdxcffx64.UpdateFfxApiProvider(pData, dataSizeInBytes, useOptions ? &frameGeneration : nullptr);

    // Result 0x80004002 (E_NOINTERFACE) basically means that amdxcffx64 doesn't have a provider for that effect
    auto level = SUCCEEDED(result) ? spdlog::level::info : spdlog::level::err;
    spdlog::log(level, "UpdateFfxApiProvider for: {}, result: 0x{:X}", effect, (UINT) result);

    return result;
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

    // Emulated FP8 is for FG and an explicitly selected FP8 upscaler, otherwise the detected INT8 model is used
    if (fsr4Support == FSR4Support::FP8 ||
        (realFsr4Support == FSR4Support::FP8 &&
         (Config::Instance()->Fsr4Model.value_or(FSR4Model::Count) == FSR4Model::FP8 ||
          Util::WhoIsTheCaller(_ReturnAddress()).starts_with("amd_fidelityfx_framegeneration_dx12"))))
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
