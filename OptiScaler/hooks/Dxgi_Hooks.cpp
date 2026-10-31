#include "pch.h"
#include "Dxgi_Hooks.h"

#include "DxgiFactory_Hooks.h"

#include <proxies/Dxgi_Proxy.h>
#include <proxies/D3D12_Proxy.h>
#include <wrapped/wrapped_factory.h>

#include <DllNames.h>

#include "Hook_Utils.h"

static DxgiProxy::PFN_CreateDxgiFactory o_CreateDXGIFactory = nullptr;
static DxgiProxy::PFN_CreateDxgiFactory1 o_CreateDXGIFactory1 = nullptr;
static DxgiProxy::PFN_CreateDxgiFactory2 o_CreateDXGIFactory2 = nullptr;
static bool creatingD3D12DeviceForLuma = false;

#pragma intrinsic(_ReturnAddress)

static void GetHardwareAdapter(IDXGIFactory* InFactory, IDXGIAdapter** InAdapter, D3D_FEATURE_LEVEL InFeatureLevel,
                               bool InRequestHighPerformanceAdapter)
{
    LOG_FUNC();

    *InAdapter = nullptr;

    IDXGIAdapter1* adapter;
    IDXGIFactory1* factory1;
    IDXGIFactory6* factory6;

    if (SUCCEEDED(InFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        LOG_DEBUG("Using IDXGIFactory6 & EnumAdapterByGpuPreference");
        factory6->Release();

        for (UINT adapterIndex = 0;
             DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(adapterIndex,
                                                                          InRequestHighPerformanceAdapter == true
                                                                              ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                                                                              : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                                                                          IID_PPV_ARGS(&adapter));
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                adapter->Release();
                continue;
            }

            *InAdapter = adapter;
            break;
        }
    }
    else if (SUCCEEDED(InFactory->QueryInterface(IID_PPV_ARGS(&factory1))))
    {
        factory1->Release();

        LOG_DEBUG("Using InFactory & EnumAdapters1");
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory1->EnumAdapters1(adapterIndex, &adapter);
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                adapter->Release();
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            auto result = D3d12Proxy::D3D12CreateDevice_()(adapter, InFeatureLevel, _uuidof(ID3D12Device), nullptr);

            if (result == S_FALSE)
            {
                LOG_DEBUG("D3D12CreateDevice test result: {:X}", (UINT) result);
                *InAdapter = adapter;
                break;
            }

            adapter->Release();
        }
    }
}

static void InitD3D12DeviceForLuma(IDXGIFactory* factory)
{
    IDXGIAdapter* hardwareAdapter = nullptr;
    GetHardwareAdapter(factory, &hardwareAdapter, D3D_FEATURE_LEVEL_11_0, true);

    if (hardwareAdapter == nullptr)
        LOG_WARN("Can't get hardwareAdapter, will try nullptr!");

    auto result = D3d12Proxy::D3D12CreateDevice_()(hardwareAdapter, D3D_FEATURE_LEVEL_11_0,
                                                   IID_PPV_ARGS(&State::Instance().currentD3D12Device));

    if (hardwareAdapter != nullptr)
        hardwareAdapter->Release();

    LOG_DEBUG("D3D12CreateDevice result: {0:x}", result);
}

static void CheckLumaAndReShade(IDXGIFactory* factory)
{
    if (!Config::Instance()->LoadReShade.value_or_default() ||
        !Config::Instance()->CreateD3D12DeviceForLuma.value_or_default() ||
        State::Instance().currentD3D12Device != nullptr || creatingD3D12DeviceForLuma)
    {
        return;
    }

    auto rsFile = Util::ExePath().parent_path() / L"ReShade64.dll";
    if (reshadeModule == nullptr)
    {
        auto rsFileExist = std::filesystem::exists(rsFile);
        if (!rsFileExist)
        {
            Config::Instance()->LoadReShade.set_volatile_value(false);
            return;
        }
    }

    // For Luma mod + Agility update we are creating D3D12 device early to prevent issues with Luma
    if (Config::Instance()->CreateD3D12DeviceForLuma.value_or_default() &&
        State::Instance().currentD3D12Device == nullptr)
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

        ScopedSkipSpoofing skipSpoofing {};
        creatingD3D12DeviceForLuma = true;

        LOG_INFO("Applying Luma DX12 workaround - creating D3D12 device early");
        InitD3D12DeviceForLuma(factory);

        creatingD3D12DeviceForLuma = false;
    }

    // Loading Reshade after Luma's D3D12 device creation to prevent conflicts
    if (reshadeModule == nullptr && Config::Instance()->LoadReShade.value_or_default())
    {
        SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", L"1");

        if (skModule != nullptr)
            SetEnvironmentVariableW(L"RESHADE_DISABLE_GRAPHICS_HOOK", L"1");

        State::EnableServeOriginal(201);
        reshadeModule = NtdllProxy::LoadLibraryExW_Ldr(rsFile.c_str(), NULL, 0);
        State::DisableServeOriginal(201);

        LOG_INFO("Loading ReShade64.dll, result: {0:X}", (size_t) reshadeModule);
    }
}

VALIDATE_HOOK(hkCreateDXGIFactory, DxgiProxy::PFN_CreateDxgiFactory)
inline static HRESULT hkCreateDXGIFactory(REFIID riid, IDXGIFactory** ppFactory)
{
    auto caller = Util::WhoIsTheCaller(_ReturnAddress());
    LOG_DEBUG("Caller: {}", caller);

    if (creatingD3D12DeviceForLuma)
    {
        LOG_DEBUG("Bypassing hooking/wrapping during Luma D3D12 device creation");
        return o_CreateDXGIFactory(riid, ppFactory);
    }

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() && CheckDllName(&caller, &skipDxgiWrappingNames))
    {
        LOG_INFO("Skipping wrapping for: {}", caller);
        return o_CreateDXGIFactory(riid, ppFactory);
    }

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() &&
        Util::GetCallerModule(_ReturnAddress()) == slInterposerModule)
    {
        LOG_DEBUG("Delaying 100ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    State::DisableChecks(97, "dxgi");
#ifndef DXGI_DEBUG_ENABLED
    auto result = o_CreateDXGIFactory(riid, ppFactory);
#else
    auto result = o_CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, riid, (IDXGIFactory2**) ppFactory);
#endif
    State::EnableChecks(97);

    if (result != S_OK)
        return result;

    IDXGIFactory* real = nullptr;
    if (Util::CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        *ppFactory = real;

    real = (IDXGIFactory*) (*ppFactory);

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default())
        *ppFactory = (IDXGIFactory*) (new WrappedIDXGIFactory7(real));
    else
        DxgiFactoryHooks::HookToFactory(real);

    CheckLumaAndReShade(real);

    return result;
}

VALIDATE_HOOK(hkCreateDXGIFactory1, DxgiProxy::PFN_CreateDxgiFactory1)
inline static HRESULT hkCreateDXGIFactory1(REFIID riid, IDXGIFactory1** ppFactory)
{
    auto caller = Util::WhoIsTheCaller(_ReturnAddress());
    LOG_DEBUG("Caller: {}", caller);

    if (creatingD3D12DeviceForLuma)
    {
        LOG_DEBUG("Bypassing hooking/wrapping during Luma D3D12 device creation");
        return o_CreateDXGIFactory1(riid, ppFactory);
    }

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() && CheckDllName(&caller, &skipDxgiWrappingNames))
    {
        LOG_INFO("Skipping wrapping for: {}", caller);
        return o_CreateDXGIFactory1(riid, ppFactory);
    }

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() &&
        Util::GetCallerModule(_ReturnAddress()) == slInterposerModule)
    {
        LOG_DEBUG("Delaying 100ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    State::DisableChecks(98, "dxgi");
#ifndef DXGI_DEBUG_ENABLED
    auto result = o_CreateDXGIFactory1(riid, ppFactory);
#else
    auto result = o_CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, riid, (IDXGIFactory2**) ppFactory);
#endif
    State::EnableChecks(98);

    if (result != S_OK)
        return result;

    IDXGIFactory1* real = nullptr;
    if (Util::CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        *ppFactory = real;

    real = (IDXGIFactory1*) (*ppFactory);

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default())
        *ppFactory = (IDXGIFactory1*) (new WrappedIDXGIFactory7(real));
    else
        DxgiFactoryHooks::HookToFactory(real);

    CheckLumaAndReShade(real);

    return result;
}

VALIDATE_HOOK(hkCreateDXGIFactory2, DxgiProxy::PFN_CreateDxgiFactory2)
inline static HRESULT hkCreateDXGIFactory2(UINT Flags, REFIID riid, IDXGIFactory2** ppFactory)
{
    auto caller = Util::WhoIsTheCaller(_ReturnAddress());
    LOG_DEBUG("Caller: {}", caller);

    if (creatingD3D12DeviceForLuma)
    {
        LOG_DEBUG("Bypassing hooking/wrapping during Luma D3D12 device creation");
        return o_CreateDXGIFactory2(Flags, riid, ppFactory);
    }

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() && CheckDllName(&caller, &skipDxgiWrappingNames))
    {
        LOG_INFO("Skipping wrapping for: {}", caller);
        return o_CreateDXGIFactory2(Flags, riid, ppFactory);
    }

    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() &&
        Util::GetCallerModule(_ReturnAddress()) == slInterposerModule)
    {
        LOG_DEBUG("Delaying 100ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    State::DisableChecks(99, "dxgi");
#ifndef DXGI_DEBUG_ENABLED
    auto result = o_CreateDXGIFactory2(Flags, riid, ppFactory);
#else
    auto result = o_CreateDXGIFactory2(Flags | DXGI_CREATE_FACTORY_DEBUG, riid, ppFactory);
#endif
    State::EnableChecks(99);

    if (result != S_OK)
        return result;

    IDXGIFactory2* real = nullptr;
    if (Util::CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        *ppFactory = real;

    real = (IDXGIFactory2*) (*ppFactory);

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default())
        *ppFactory = (IDXGIFactory2*) (new WrappedIDXGIFactory7(real));
    else
        DxgiFactoryHooks::HookToFactory(real);

    CheckLumaAndReShade(real);

    return result;
}

void DxgiHooks::Hook()
{
    std::lock_guard<std::mutex> lock(hookMutex);

    // If not spoofing and
    // using no frame generation (or Nukem's) and
    // not using DXGI spoofing we don't need DXGI hooks
    // Probably I forgot something but we can add it later
    if (!Config::Instance()->OverlayMenu.value_or_default() &&
        (Config::Instance()->FGInput.value_or_default() == FGInput::NoFG ||
         Config::Instance()->FGInput.value_or_default() == FGInput::Nukems) &&
        !Config::Instance()->DxgiSpoofing.value_or_default())
    {
        return;
    }

    if (DxgiProxy::Module() == nullptr)
        return;

    LOG_DEBUG("");

    if (o_CreateDXGIFactory == nullptr)
        o_CreateDXGIFactory = DxgiProxy::Hook_CreateDxgiFactory(hkCreateDXGIFactory);

    if (o_CreateDXGIFactory1 == nullptr)
        o_CreateDXGIFactory1 = DxgiProxy::Hook_CreateDxgiFactory1(hkCreateDXGIFactory1);

    if (o_CreateDXGIFactory2 == nullptr)
        o_CreateDXGIFactory2 = DxgiProxy::Hook_CreateDxgiFactory2(hkCreateDXGIFactory2);
}
