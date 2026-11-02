#include "pch.h"
#include "D3D11_Hooks.h"

#include <Util.h>
#include <Config.h>

#include <proxies/KernelBase_Proxy.h>

#include <wrapped/wrapped_swapchain.h>

#include <resource_tracking/ResTrack_dx11.h>

#include <inputs/FSR2_Dx11.h>
#include <inputs/XeSS_Dx11.h>
#include <inputs/NVNGX_DLSS.h>

#include <detours/detours.h>

#include <d3d11_4.h>
#include <d3d11on12.h>
#include <dxgi1_6.h>

#include "FG_Hooks.h"
#include "Hook_Utils.h"
#include "Reflex_Hooks.h"
#include "Sampler_Overrides.h"

#pragma intrinsic(_ReturnAddress)

bool _skipDx11Create = false;

// DirectX
using PFN_CreateSamplerState = rewrite_signature<decltype(&ID3D11Device::CreateSamplerState)>::type;

using PFN_Release = rewrite_signature<decltype(&IUnknown::Release)>::type;

static PFN_D3D11_CREATE_DEVICE o_D3D11CreateDevice = nullptr;
static PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN o_D3D11CreateDeviceAndSwapChain = nullptr;
static PFN_CreateSamplerState o_CreateSamplerState = nullptr;
static PFN_D3D11ON12_CREATE_DEVICE o_D3D11On12CreateDevice = nullptr;

// One original per hooked Release, devices of other classes (debug layer, wrappers) have their own
static PVOID _d3d11DeviceReleaseTargets[4] {};
static PFN_Release o_D3D11DeviceRelease[4] {};
static std::mutex _d3d11DeviceReleaseMutex;

static HRESULT hkCreateSamplerState(ID3D11Device* This, const D3D11_SAMPLER_DESC* pSamplerDesc,
                                    ID3D11SamplerState** ppSamplerState);

static ULONG D3D11DeviceRelease(IUnknown* device, PFN_Release original)
{
    if (State::Instance().currentD3D11Device == device)
    {
        device->AddRef();
        auto refCount = original(device);

        if (refCount == 1)
        {
            LOG_DEBUG("Set State::Instance().currentD3D11Device = nullptr, was: {:X}", (size_t) device);
            State::Instance().currentD3D11Device = nullptr;
        }
    }

    auto result = original(device);

    if (result == 0)
    {
        State::Instance().RemoveD3D11Device(device);
        ReflexHooks::deviceDestroyed(device);
        FSR2Dx11DeviceDestroyed(device);
        XeSSDx11DeviceDestroyed(device);
        NVNGXDx11DeviceDestroyed(device);
    }

    return result;
}

template <size_t I> static ULONG hkD3D11DeviceRelease(IUnknown* device)
{
    return D3D11DeviceRelease(device, o_D3D11DeviceRelease[I]);
}
VALIDATE_MEMBER_HOOK(hkD3D11DeviceRelease<0>, PFN_Release)

static constexpr PFN_Release hkD3D11DeviceReleases[] = { hkD3D11DeviceRelease<0>, hkD3D11DeviceRelease<1>,
                                                         hkD3D11DeviceRelease<2>, hkD3D11DeviceRelease<3> };

// Hooks the Release of the device's class once
static void HookD3D11DeviceRelease(ID3D11Device* device)
{
    auto target = (*(PVOID**) device)[2];

    std::scoped_lock lock(_d3d11DeviceReleaseMutex);

    for (size_t i = 0; i < std::size(_d3d11DeviceReleaseTargets); i++)
    {
        if (_d3d11DeviceReleaseTargets[i] == target)
            return;

        if (_d3d11DeviceReleaseTargets[i] != nullptr)
            continue;

        _d3d11DeviceReleaseTargets[i] = target;
        o_D3D11DeviceRelease[i] = (PFN_Release) target;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) o_D3D11DeviceRelease[i], (PVOID) hkD3D11DeviceReleases[i]);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook ID3D11Device::Release: {:X}", detourResult);
            o_D3D11DeviceRelease[i] = nullptr;
            _d3d11DeviceReleaseTargets[i] = nullptr;
        }

        return;
    }

    LOG_WARN("No free slot to hook ID3D11Device::Release of {:X}", (size_t) device);
}

static void HookToDeviceLocal(ID3D11Device* InDevice)
{
    if (InDevice == nullptr)
        return;

    HookD3D11DeviceRelease(InDevice);

    if (o_CreateSamplerState != nullptr)
        return;

    LOG_DEBUG("Dx11");

    // Get the vtable pointer
    PVOID* pVTable = *(PVOID**) InDevice;

    o_CreateSamplerState = (PFN_CreateSamplerState) pVTable[23];

    // Apply the detour
    if (o_CreateSamplerState != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_CreateSamplerState, hkCreateSamplerState);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook ID3D11Device: {:X}", detourResult);
            o_CreateSamplerState = nullptr;
        }
    }
}

VALIDATE_HOOK(hkD3D11On12CreateDevice, PFN_D3D11ON12_CREATE_DEVICE)
static HRESULT hkD3D11On12CreateDevice(IUnknown* pDevice, UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels,
                                       UINT FeatureLevels, IUnknown* const* ppCommandQueues, UINT NumQueues,
                                       UINT NodeMask, ID3D11Device** ppDevice, ID3D11DeviceContext** ppImmediateContext,
                                       D3D_FEATURE_LEVEL* pChosenFeatureLevel)
{
    LOG_DEBUG("Caller: {}, Device: {:X}", Util::WhoIsTheCaller(_ReturnAddress()), (UINT64) pDevice);

#ifdef ENABLE_DEBUG_LAYER_DX11
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    bool rtss = false;

    std::vector<IUnknown*> copyCommandQueues;

    if (ppCommandQueues && NumQueues > 0)
        copyCommandQueues.assign(ppCommandQueues, ppCommandQueues + NumQueues);

    // Assuming RTSS is creating a D3D11on12 device, not sure why but sometimes RTSS tries to create
    // it's D3D11on12 device with old CommandQueue which results crash
    // I am changing it's CommandQueue with current swapchain's command queue
    if (State::Instance().currentCommandQueue != nullptr && !copyCommandQueues.empty() &&
        copyCommandQueues[0] != State::Instance().currentCommandQueue &&
        GetModuleHandle(L"RTSSHooks64.dll") != nullptr && pDevice == State::Instance().currentD3D12Device)
    {
        LOG_INFO("Replaced RTSS CommandQueue with correct one {0:X} -> {1:X}", (UINT64) *ppCommandQueues,
                 (UINT64) State::Instance().currentCommandQueue);

        copyCommandQueues[0] = State::Instance().currentCommandQueue;

        rtss = true;
    }

    HRESULT result = E_FAIL;
    {
        ScopedCreatingD3DDevice skipCreatingD3DDevice {};
        result = o_D3D11On12CreateDevice(pDevice, Flags, pFeatureLevels, FeatureLevels, copyCommandQueues.data(),
                                         static_cast<UINT>(copyCommandQueues.size()), NodeMask, ppDevice,
                                         ppImmediateContext, pChosenFeatureLevel);
    }

    if (result == S_OK && ppDevice != nullptr && *ppDevice != nullptr && !rtss &&
        !State::Instance().D3D12DeviceCreated())
    {
        LOG_INFO("Device captured, D3D11Device: {0:X}", (UINT64) *ppDevice);
        HookToDeviceLocal(*ppDevice);
    }

    if (result == S_OK && ppDevice != nullptr && *ppDevice != nullptr)
    {
        State::Instance().AddD3D11Device(*ppDevice);
        HookD3D11DeviceRelease(*ppDevice);
    }

    LOG_FUNC_RESULT(result);

    return result;
}

VALIDATE_HOOK(hkD3D11CreateDevice, PFN_D3D11_CREATE_DEVICE)
static HRESULT hkD3D11CreateDevice(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
                                   const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
                                   ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel,
                                   ID3D11DeviceContext** ppImmediateContext)
{
    if (_skipDx11Create)
    {
        LOG_DEBUG("Skip");
        return o_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion,
                                   ppDevice, pFeatureLevel, ppImmediateContext);
    }

    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

#ifdef ENABLE_DEBUG_LAYER_DX11
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    DXGI_ADAPTER_DESC desc {};
    std::wstring szName;
    if (pAdapter != nullptr)
    {
        {
            ScopedSkipSpoofingThread skipSpoofingThread {};

            if (pAdapter->GetDesc(&desc) == S_OK)
            {
                szName = desc.Description;
                LOG_INFO("Adapter Desc: {}", wstring_to_string(szName));

                if (desc.VendorId == VendorId::Microsoft)
                {
                    _skipDx11Create = true;

                    HRESULT result;
                    {
                        ScopedSkipParentWrapping skipParentWrapping {};
                        ScopedCreatingD3DDevice skipCreatingD3DDevice {};
                        result =
                            o_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
                                                SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
                    }

                    _skipDx11Create = false;

                    return result;
                }
            }
        }
    }

    auto gameFeatureLevels = pFeatureLevels;
    auto gameFeatureLevelCount = FeatureLevels;

    if (!(State::Instance().gameQuirks & GameQuirk::SkipD3D11FeatureLevelElevation))
    {
        static const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
        };

        D3D_FEATURE_LEVEL maxLevel = D3D_FEATURE_LEVEL_1_0_CORE;

        for (UINT i = 0; i < FeatureLevels; ++i)
        {
            maxLevel = std::max(maxLevel, pFeatureLevels[i]);
        }

        if (maxLevel == D3D_FEATURE_LEVEL_11_0)
        {
            LOG_INFO("Overriding D3D_FEATURE_LEVEL, "
                     "Game requested D3D_FEATURE_LEVEL_11_0, "
                     "we need D3D_FEATURE_LEVEL_11_1!");

            pFeatureLevels = levels;
            FeatureLevels = ARRAYSIZE(levels);
        }
    }

    _skipDx11Create = true;

    HRESULT result;
    {
        ScopedSkipParentWrapping skipParentWrapping {};
        ScopedCreatingD3DDevice creatingD3DDevice {};
        result = o_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion,
                                     ppDevice, pFeatureLevel, ppImmediateContext);

        if ((result == DXGI_ERROR_UNSUPPORTED || result == E_INVALIDARG) && pFeatureLevels != gameFeatureLevels)
        {
            LOG_WARN("D3D_FEATURE_LEVEL_11_1 failed, using game's feature levels");
            result =
                o_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, gameFeatureLevels, gameFeatureLevelCount,
                                    SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
        }
    }

    _skipDx11Create = false;

    if (result == S_OK && ppDevice != nullptr && *ppDevice != nullptr && !State::Instance().D3D12DeviceCreated())
    {
        LOG_INFO("Device captured");
        HookToDeviceLocal(*ppDevice);
    }

    LOG_FUNC_RESULT(result);

    if (result == S_OK && ppDevice != nullptr && *ppDevice != nullptr)
    {
        State::Instance().AddD3D11Device(*ppDevice);
        HookD3D11DeviceRelease(*ppDevice);
    }

    return result;
}

VALIDATE_HOOK(hkD3D11CreateDeviceAndSwapChain, PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)
static HRESULT hkD3D11CreateDeviceAndSwapChain(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
                                               UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
                                               UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
                                               IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
                                               D3D_FEATURE_LEVEL* pFeatureLevel,
                                               ID3D11DeviceContext** ppImmediateContext)
{
    if (_skipDx11Create)
    {

        LOG_DEBUG("Skip");
        return o_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
                                               SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel,
                                               ppImmediateContext);
    }

    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

#ifdef ENABLE_DEBUG_LAYER_DX11
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    DXGI_ADAPTER_DESC desc {};
    std::wstring szName;
    if (pAdapter != nullptr)
    {
        {
            ScopedSkipSpoofingThread skipSpoofingThread {};

            if (pAdapter->GetDesc(&desc) == S_OK)
            {
                szName = desc.Description;
                LOG_INFO("Adapter Desc: {}", wstring_to_string(szName));

                if (desc.VendorId == VendorId::Microsoft)
                {
                    _skipDx11Create = true;

                    HRESULT result;
                    {
                        ScopedSkipParentWrapping skipParentWrapping {};
                        ScopedCreatingD3DDevice skipCreatingD3DDevice {};
                        result = o_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels,
                                                                 FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain,
                                                                 ppDevice, pFeatureLevel, ppImmediateContext);
                    }

                    _skipDx11Create = false;

                    return result;
                }
            }
        }
    }

    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1 };
    auto gameFeatureLevels = pFeatureLevels;
    auto gameFeatureLevelCount = FeatureLevels;

    if (!(State::Instance().gameQuirks & GameQuirk::SkipD3D11FeatureLevelElevation))
    {
        D3D_FEATURE_LEVEL maxLevel = D3D_FEATURE_LEVEL_1_0_CORE;

        for (UINT i = 0; i < FeatureLevels; ++i)
        {
            maxLevel = std::max(maxLevel, pFeatureLevels[i]);
        }

        if (maxLevel == D3D_FEATURE_LEVEL_11_0)
        {
            LOG_INFO("Overriding D3D_FEATURE_LEVEL, "
                     "Game requested D3D_FEATURE_LEVEL_11_0, "
                     "we need D3D_FEATURE_LEVEL_11_1!");

            pFeatureLevels = levels;
            FeatureLevels = ARRAYSIZE(levels);
        }
    }

    if (pSwapChainDesc != nullptr &&
        Util::IsOverlaySwapchain(pSwapChainDesc->OutputWindow, pSwapChainDesc->BufferDesc.Width,
                                 pSwapChainDesc->BufferDesc.Height))
    {
        LOG_WARN("Overlay call!");

        _skipDx11Create = true;
        State::Instance().skipParentWrapping = true;

        auto result = o_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels,
                                                      FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice,
                                                      pFeatureLevel, ppImmediateContext);

        State::Instance().skipParentWrapping = false;
        _skipDx11Create = false;

        return result;
    }

    DXGI_SWAP_CHAIN_DESC localDesc {};

    // For vsync override
    if (pSwapChainDesc != nullptr)
    {
        memcpy(&localDesc, pSwapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC));

        if (!pSwapChainDesc->Windowed)
        {
            LOG_INFO("Game is creating fullscreen swapchain, disabled V-Sync overrides");
            Config::Instance()->OverrideVsync.set_volatile_value(false);
        }

        // Flip model doesn't support MSAA or sRGB buffers
        if (Config::Instance()->OverrideVsync.value_or_default() && localDesc.SampleDesc.Count <= 1 &&
            localDesc.BufferDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
            localDesc.BufferDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        {
            localDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            localDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

            if (localDesc.BufferCount < 2)
                localDesc.BufferCount = 2;
        }
    }

    _skipDx11Create = true;

    auto result = o_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
                                                  SDKVersion, pSwapChainDesc != nullptr ? &localDesc : nullptr,
                                                  ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);

    if ((result == DXGI_ERROR_UNSUPPORTED || result == E_INVALIDARG) && pFeatureLevels != gameFeatureLevels)
    {
        LOG_WARN("D3D_FEATURE_LEVEL_11_1 failed, using game's feature levels");
        result = o_D3D11CreateDeviceAndSwapChain(
            pAdapter, DriverType, Software, Flags, gameFeatureLevels, gameFeatureLevelCount, SDKVersion,
            pSwapChainDesc != nullptr ? &localDesc : nullptr, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
    }
    _skipDx11Create = false;

    // Success codes like DXGI_STATUS_OCCLUDED still return a valid device and swapchain
    if (SUCCEEDED(result) && ppDevice != nullptr && *ppDevice != nullptr && !State::Instance().D3D12DeviceCreated())
    {
        LOG_INFO("Device captured");
        HookToDeviceLocal(*ppDevice);
    }

    // Factory hooks wrap it already, they don't make other windows' swapchains current
    WrappedIDXGISwapChain4* wrapped = nullptr;
    if (SUCCEEDED(result) && ppSwapChain != nullptr && *ppSwapChain != nullptr &&
        (*ppSwapChain)->QueryInterface(IID_PPV_ARGS(&wrapped)) == S_OK)
    {
        LOG_DEBUG("Swapchain is wrapped by the factory hooks");
        wrapped->Release();
    }
    else if (SUCCEEDED(result) && pSwapChainDesc != nullptr && ppSwapChain != nullptr && *ppSwapChain != nullptr &&
             ppDevice != nullptr && *ppDevice != nullptr)
    {
        const bool otherWindow = FGHooks::IsOtherWindowThanFG(pSwapChainDesc->OutputWindow);

        // check for SL proxy
        IDXGISwapChain* realSC = nullptr;
        if (!Util::CheckForRealObject(__FUNCTION__, *ppSwapChain, (IUnknown**) &realSC))
            realSC = *ppSwapChain;

        IUnknown* realDevice = nullptr;
        if (!Util::CheckForRealObject(__FUNCTION__, *ppDevice, (IUnknown**) &realDevice))
            realDevice = *ppDevice;

        if (!otherWindow)
        {
            State::Instance().currentRealSwapchain = realSC;
            State::Instance().screenWidth = static_cast<float>(pSwapChainDesc->BufferDesc.Width);
            State::Instance().screenHeight = static_cast<float>(pSwapChainDesc->BufferDesc.Height);
        }

        LOG_DEBUG("Created new swapchain: {0:X}, hWnd: {1:X}", (UINT64) *ppSwapChain,
                  (UINT64) pSwapChainDesc->OutputWindow);

        *ppSwapChain = new WrappedIDXGISwapChain4(realSC, realDevice, pSwapChainDesc->OutputWindow,
                                                  pSwapChainDesc->Flags, false, *ppSwapChain);

        if (!otherWindow)
        {
            State::Instance().currentSwapchain = *ppSwapChain;
            State::Instance().currentWrappedSwapchain = *ppSwapChain;
            State::Instance().SCAllowTearing = (localDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;
            State::Instance().SCLastFlags = localDesc.Flags;
            State::Instance().realExclusiveFullscreen = !localDesc.Windowed;
        }

        LOG_DEBUG("Created new WrappedIDXGISwapChain4: {0:X}, pDevice: {1:X}", (UINT64) *ppSwapChain,
                  (UINT64) *ppDevice);
    }

    if (SUCCEEDED(result) && ppDevice != nullptr && *ppDevice != nullptr)
    {
        State::Instance().AddD3D11Device(*ppDevice);
        HookD3D11DeviceRelease(*ppDevice);
    }

    LOG_FUNC_RESULT(result);
    return result;
}

VALIDATE_HOOK(hkCreateSamplerState, PFN_CreateSamplerState)
static HRESULT hkCreateSamplerState(ID3D11Device* This, const D3D11_SAMPLER_DESC* pSamplerDesc,
                                    ID3D11SamplerState** ppSamplerState)
{
    if (pSamplerDesc == nullptr || This == nullptr)
        return E_INVALIDARG;

    LOG_FUNC();

    D3D11_SAMPLER_DESC newDesc = *pSamplerDesc;
    SamplerOverrides::Apply(newDesc);

    return o_CreateSamplerState(This, &newDesc, ppSamplerState);
}

void D3D11Hooks::HookToDevice(ID3D11Device* InDevice) { HookToDeviceLocal(InDevice); }

void D3D11Hooks::Hook(HMODULE dx11Module)
{
    if (o_D3D11CreateDevice != nullptr)
        return;

    LOG_DEBUG("");

    o_D3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE) KernelBaseProxy::GetProcAddress_()(dx11Module, "D3D11CreateDevice");
    o_D3D11CreateDeviceAndSwapChain = (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN) KernelBaseProxy::GetProcAddress_()(
        dx11Module, "D3D11CreateDeviceAndSwapChain");
    o_D3D11On12CreateDevice =
        (PFN_D3D11ON12_CREATE_DEVICE) KernelBaseProxy::GetProcAddress_()(dx11Module, "D3D11On12CreateDevice");

    if (o_D3D11CreateDevice != nullptr || o_D3D11On12CreateDevice != nullptr ||
        o_D3D11CreateDeviceAndSwapChain != nullptr)
    {
        LOG_DEBUG("Hooking D3D11CreateDevice methods");

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_D3D11CreateDevice != nullptr)
            DetourAttach(&(PVOID&) o_D3D11CreateDevice, hkD3D11CreateDevice);

        if (o_D3D11On12CreateDevice != nullptr)
            DetourAttach(&(PVOID&) o_D3D11On12CreateDevice, hkD3D11On12CreateDevice);

        if (o_D3D11CreateDeviceAndSwapChain != nullptr)
            DetourAttach(&(PVOID&) o_D3D11CreateDeviceAndSwapChain, hkD3D11CreateDeviceAndSwapChain);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook ID3D11Device: {:X}", detourResult);
            o_D3D11CreateDevice = nullptr;
            o_D3D11On12CreateDevice = nullptr;
            o_D3D11CreateDeviceAndSwapChain = nullptr;
        }
    }
}

void D3D11Hooks::Unhook()
{
    std::scoped_lock lock(_d3d11DeviceReleaseMutex);

    ResTrack_Dx11::ReleaseHooks();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_D3D11CreateDevice != nullptr)
        DetourDetach(&(PVOID&) o_D3D11CreateDevice, hkD3D11CreateDevice);

    for (size_t i = 0; i < std::size(o_D3D11DeviceRelease); i++)
    {
        if (o_D3D11DeviceRelease[i] != nullptr)
            DetourDetach(&(PVOID&) o_D3D11DeviceRelease[i], (PVOID) hkD3D11DeviceReleases[i]);
    }

    if (o_D3D11On12CreateDevice != nullptr)
        DetourDetach(&(PVOID&) o_D3D11On12CreateDevice, hkD3D11On12CreateDevice);

    if (o_D3D11CreateDeviceAndSwapChain != nullptr)
        DetourDetach(&(PVOID&) o_D3D11CreateDeviceAndSwapChain, hkD3D11CreateDeviceAndSwapChain);

    if (o_CreateSamplerState != nullptr)
        DetourDetach(&(PVOID&) o_CreateSamplerState, hkCreateSamplerState);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook ID3D11Device: {:X}", detourResult);
    }
    else
    {
        o_D3D11CreateDevice = nullptr;
        o_CreateSamplerState = nullptr;
        o_D3D11On12CreateDevice = nullptr;
        o_D3D11CreateDeviceAndSwapChain = nullptr;

        for (size_t i = 0; i < std::size(o_D3D11DeviceRelease); i++)
        {
            o_D3D11DeviceRelease[i] = nullptr;
            _d3d11DeviceReleaseTargets[i] = nullptr;
        }
    }
}

#pragma endregion
