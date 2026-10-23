#include "pch.h"
#include "Amdxc64_Hooks.h"

#include <fsr4/FSR4DriverProviderDx12.h>
#include <ffx_antilag2_dx12.h>
#include <proxies/KernelBase_Proxy.h>
#include <proxies/Ntdll_Proxy.h>
#include <low_latency/input/input_antilag2.h>
#include <misc/IdentifyGpu.h>

#pragma endregion

struct AmdExtD3DFactory : public IAmdExtD3DFactory
{
    IAmdExtD3DFactory* real = nullptr;
    std::atomic<ULONG> refCount = 1;

    bool linux = false;
    FSR4Support fsr4Support {};
    FSR4Support realFsr4Support {};
    bool fsr4ForcedSupport = false;

    HRESULT STDMETHODCALLTYPE CreateInterface(IUnknown* pOuter, REFIID riid, void** ppvObject) override
    {
        if (riid == __uuidof(IAmdExtD3DShaderIntrinsics))
        {
            if (Amdxc64Hooks::amdExtD3DShaderIntrinsics == nullptr)
                Amdxc64Hooks::amdExtD3DShaderIntrinsics = new AmdExtD3DShaderIntrinsics();

            *ppvObject = Amdxc64Hooks::amdExtD3DShaderIntrinsics;

            if (real != nullptr)
                real->CreateInterface(pOuter, riid, (void**) &Amdxc64Hooks::o_amdExtD3DShaderIntrinsics);

            LOG_INFO("Custom IAmdExtD3DShaderIntrinsics queried, returning custom AmdExtD3DShaderIntrinsics");

            return S_OK;
        }
        // TODO: Implementation too incomplete to always enable it
        else if (riid == __uuidof(IAmdExtD3DDevice8) && (linux || fsr4ForcedSupport))
        {
            if (Amdxc64Hooks::amdExtD3DDevice8 == nullptr)
            {
                Amdxc64Hooks::amdExtD3DDevice8 = new AmdExtD3DDevice8();
                Amdxc64Hooks::amdExtD3DDevice8->fsr4Support = fsr4Support;
                Amdxc64Hooks::amdExtD3DDevice8->realFsr4Support = realFsr4Support;
            }

            *ppvObject = Amdxc64Hooks::amdExtD3DDevice8;

            LOG_INFO("Custom IAmdExtD3DDevice8 queried, returning custom AmdExtD3DDevice8");

            return S_OK;
        }
        else if (real != nullptr)
        {
            return real->CreateInterface(pOuter, riid, ppvObject);
        }

        return E_NOINTERFACE;
    }

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (ppvObject == nullptr)
            return E_POINTER;

        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAmdExtD3DFactory))
        {
            AddRef();
            *ppvObject = this;
            return S_OK;
        }

        if (real != nullptr)
            return real->QueryInterface(riid, ppvObject);

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef(void) override { return ++refCount; }
    ULONG __stdcall Release(void) override
    {
        auto result = --refCount;

        if (result == 0)
        {
            if (real != nullptr)
                real->Release();

            delete this;
        }

        return result;
    }
};

void Amdxc64Hooks::Init()
{
    if (o_AmdExtD3DCreateInterface != nullptr)
        return;

    LOG_DEBUG("");

    moduleAmdxc64 = KernelBaseProxy::GetModuleHandleW_()(L"amdxc64.dll");

    // When LoadCustomAmdxc64OnRdna2 is set, don't blindly load any amdxc64.dll
    // Wait for it to be loaded by d3d12 and at that point we know what GPU is RDNA 2
    if (moduleAmdxc64 == nullptr && !Config::Instance()->Fsr4DoNotLoadAmdxc64.value_or_default() &&
        !Config::Instance()->LoadCustomAmdxc64OnRdna2.value_or_default())
    {
        moduleAmdxc64 = NtdllProxy::LoadLibraryExW_Ldr(L"amdxc64.dll", NULL, 0);
    }

    if (moduleAmdxc64 != nullptr)
    {
        // Pin the dll so that our hooks stay valid, mainly for Linux
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"amdxc64.dll", &moduleAmdxc64);

        LOG_INFO("amdxc64.dll loaded");

        static std::mutex initMutex;
        std::scoped_lock lock(initMutex);

        if (o_AmdExtD3DCreateInterface != nullptr)
            return;

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

HRESULT STDMETHODCALLTYPE Amdxc64Hooks::hkAmdExtD3DCreateInterface(IUnknown* pOuter, REFIID riid, void** ppvObject)
{
    // We need to know D3D12 capabilities by now, one of them being FSR 4
    IdentifyGpu::updateD3d12Capabilities();

    const auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    const bool runFsr4Upgrade = primaryGpu.fsr4Support != FSR4Support::None;

    // Proton bleeding edge ships amdxc64 that is missing some required functions
    if (runFsr4Upgrade && riid == __uuidof(IAmdExtD3DFactory))
    {
        // Required for the custom AmdExtFfxApi, lack of it triggers visual glitches
        auto factory = new AmdExtD3DFactory();
        factory->linux = State::Instance().isRunningOnLinux;
        factory->fsr4Support = primaryGpu.fsr4Support;
        factory->realFsr4Support = primaryGpu.realFsr4Support;
        factory->fsr4ForcedSupport = primaryGpu.fsr4ForcedSupport;

        if (o_AmdExtD3DCreateInterface != nullptr &&
            FAILED(o_AmdExtD3DCreateInterface(pOuter, riid, (void**) &factory->real)))
        {
            factory->real = nullptr;
        }

        if (factory->real != nullptr)
            HookFactory(factory->real);

        LOG_INFO("IAmdExtD3DFactory queried, returning custom AmdExtD3DFactory: {:X}, real: {:X}", (size_t) factory,
                 (size_t) factory->real);

        *ppvObject = factory;

        return S_OK;
    }

    else if (runFsr4Upgrade && riid == __uuidof(IAmdExtFfxApi))
    {
        if (amdExtFfxApi == nullptr)
        {
            amdExtFfxApi = new AmdExtFfxApi();
            amdExtFfxApi->realFsr4Support = primaryGpu.realFsr4Support;
        }

        // Return custom one
        *ppvObject = amdExtFfxApi;

        LOG_INFO("IAmdExtFfxApi queried, returning custom AmdExtFfxApi");

        return S_OK;
    }

    else if (riid == IID_IAmdExtAntiLagApi && giveGameAl2Proxy)
    {
        if (amdExtAntiLagApi == nullptr)
            amdExtAntiLagApi = new AmdExtAntiLagApi();

        // Return custom one
        *ppvObject = amdExtAntiLagApi;

        LOG_INFO("Providing the game with AL2 proxy, AL2 inputs should be avaliable");

        return S_OK;
    }

    else if (o_AmdExtD3DCreateInterface != nullptr)
    {
        auto result = o_AmdExtD3DCreateInterface(pOuter, riid, ppvObject);

        if (SUCCEEDED(result) && riid == __uuidof(IAmdExtD3DFactory) && *ppvObject != nullptr)
            HookFactory((IAmdExtD3DFactory*) *ppvObject);

        return result;
    }

    return E_NOINTERFACE;
}

void Amdxc64Hooks::HookFactory(IAmdExtD3DFactory* factory)
{
    if (o_FactoryCreateInterface != nullptr)
        return;

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

void Amdxc64Hooks::HookFlipMeter(IAmdExtFlipMeter* flipMeter)
{
    if (o_FlipMeterSetConfig != nullptr)
        return;

    auto vtable = *(void***) flipMeter;
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

HRESULT Amdxc64Hooks::SetFlipMeterConfig(IAmdExtFlipMeter* flipMeter, const AmdExtFlipMeterConfig& requested)
{
    // Modify a copy so FSR keeps its requested state and doesn't try to re-apply it
    auto config = requested;
    if (Config::Instance()->FSRFGDisableFlipMetering.value_or_default())
        config.mode = 0;

    auto result = o_FlipMeterSetConfig(flipMeter, &config);

    LOG_INFO("Flip meter requested mode: {}, applied mode: {}, flags: {:X}, result: {:X}", requested.mode, config.mode,
             config.flags, (UINT) result);

    if (SUCCEEDED(result))
        State::Instance().amdFlipMeterMode = config.mode;

    return result;
}

void Amdxc64Hooks::ApplyFlipMeterOverride()
{
    std::scoped_lock lock(flipMeterMutex);

    if (flipMeter != nullptr && flipMeterRequested.has_value() && o_FlipMeterSetConfig != nullptr)
        SetFlipMeterConfig(flipMeter, flipMeterRequested.value());
}

ULONG STDMETHODCALLTYPE Amdxc64Hooks::hkFlipMeterRelease(IAmdExtFlipMeter* This)
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

HRESULT STDMETHODCALLTYPE Amdxc64Hooks::hkFactoryCreateInterface(IAmdExtD3DFactory* This, IUnknown* pOuter, REFIID riid,
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

HRESULT STDMETHODCALLTYPE Amdxc64Hooks::hkFlipMeterSetConfig(IAmdExtFlipMeter* This, AmdExtFlipMeterConfig* config)
{
    std::scoped_lock lock(flipMeterMutex);

    if (This == flipMeter)
        flipMeterRequested = *config;

    return SetFlipMeterConfig(This, *config);
}

// FSR reads the config back when SetConfig fails
HRESULT STDMETHODCALLTYPE Amdxc64Hooks::hkFlipMeterGetConfig(IAmdExtFlipMeter* This, AmdExtFlipMeterConfig* config)
{
    std::scoped_lock lock(flipMeterMutex);

    auto result = o_FlipMeterGetConfig(This, config);

    LOG_DEBUG("Flip meter mode: {}, result: {:X}", config->mode, (UINT) result);

    if (SUCCEEDED(result) && This == flipMeter)
        State::Instance().amdFlipMeterMode = config->mode;

    return result;
}
