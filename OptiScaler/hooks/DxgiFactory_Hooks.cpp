#include "pch.h"
#include "DxgiFactory_Hooks.h"

#include "D3D11_Hooks.h"
#include "D3D12_Hooks.h"

#include <Config.h>

#include <spoofing/Dxgi_Spoofing.h>
#include <wrapped/wrapped_swapchain.h>

#include <magic_enum.hpp>
#include <detours/detours.h>

#include <d3d11.h>

// #define DETAILED_SC_LOGS

#ifdef DETAILED_SC_LOGS
#include <magic_enum.hpp>
#endif

void DxgiFactoryHooks::CheckAdapter(IUnknown* unkAdapter)
{
    if (State::Instance().isRunningOnDXVK)
        return;

    // DXVK VkInterface GUID
    const GUID guid = { 0x907bf281, 0xea3c, 0x43b4, { 0xa8, 0xe4, 0x9f, 0x23, 0x11, 0x07, 0xb4, 0xff } };

    IDXGIAdapter* adapter = nullptr;
    bool adapterOk = unkAdapter->QueryInterface(IID_PPV_ARGS(&adapter)) == S_OK;

    void* dxvkAdapter = nullptr;
    if (adapterOk && adapter->QueryInterface(guid, &dxvkAdapter) == S_OK)
    {
        State::Instance().isRunningOnDXVK = dxvkAdapter != nullptr;
        ((IDXGIAdapter*) dxvkAdapter)->Release();
    }

    if (adapterOk)
        adapter->Release();
}

void DxgiFactoryHooks::HookToFactory(IDXGIFactory* pFactory)
{
    if (pFactory == nullptr)
        return;

    LOG_FUNC();

    IDXGIFactory* real = nullptr;
    if (!Util::CheckForRealObject(__FUNCTION__, pFactory, (IUnknown**) &real))
        real = pFactory;

    void** pFactoryVTable = *reinterpret_cast<void***>(real);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_EnumAdapters == nullptr)
    {
        o_EnumAdapters = (PFN_EnumAdapters) pFactoryVTable[7];

        if (o_EnumAdapters != nullptr)
            DetourAttach(&(PVOID&) o_EnumAdapters, DxgiFactoryHooks::EnumAdapters);
    }

    if (o_CreateSwapChain == nullptr)
    {
        o_CreateSwapChain = (PFN_CreateSwapChain) pFactoryVTable[10];

        if (o_CreateSwapChain != nullptr)
            DetourAttach(&(PVOID&) o_CreateSwapChain, DxgiFactoryHooks::CreateSwapChain);
    }

    IDXGIFactory1* factory1 = nullptr;
    if (pFactory->QueryInterface(IID_PPV_ARGS(&factory1)) == S_OK)
    {
        factory1->Release();

        if (o_EnumAdapters1 == nullptr)
        {
            o_EnumAdapters1 = (PFN_EnumAdapters1) pFactoryVTable[12];

            if (o_EnumAdapters1 != nullptr)
                DetourAttach(&(PVOID&) o_EnumAdapters1, DxgiFactoryHooks::EnumAdapters1);
        }
    }

    IDXGIFactory2* factory2 = nullptr;
    if (pFactory->QueryInterface(IID_PPV_ARGS(&factory2)) == S_OK)
    {
        factory2->Release();

        if (o_CreateSwapChainForHwnd == nullptr)
        {
            o_CreateSwapChainForHwnd = (PFN_CreateSwapChainForHwnd) pFactoryVTable[15];

            if (o_CreateSwapChainForHwnd != nullptr)
                DetourAttach(&(PVOID&) o_CreateSwapChainForHwnd, DxgiFactoryHooks::CreateSwapChainForHwnd);
        }

        if (o_CreateSwapChainForCoreWindow == nullptr)
        {
            o_CreateSwapChainForCoreWindow = (PFN_CreateSwapChainForCoreWindow) pFactoryVTable[16];

            if (o_CreateSwapChainForCoreWindow != nullptr)
                DetourAttach(&(PVOID&) o_CreateSwapChainForCoreWindow, DxgiFactoryHooks::CreateSwapChainForCoreWindow);
        }
    }

    IDXGIFactory4* factory4 = nullptr;
    if (pFactory->QueryInterface(IID_PPV_ARGS(&factory4)) == S_OK)
    {
        factory4->Release();

        if (o_EnumAdapterByLuid == nullptr)
        {
            o_EnumAdapterByLuid = (PFN_EnumAdapterByLuid) pFactoryVTable[26];

            if (o_EnumAdapterByLuid != nullptr)
                DetourAttach(&(PVOID&) o_EnumAdapterByLuid, DxgiFactoryHooks::EnumAdapterByLuid);
        }
    }

    IDXGIFactory6* factory6 = nullptr;
    if (pFactory->QueryInterface(IID_PPV_ARGS(&factory6)) == S_OK)
    {
        factory6->Release();

        if (o_EnumAdapterByGpuPreference == nullptr)
        {
            o_EnumAdapterByGpuPreference = (PFN_EnumAdapterByGpuPreference) pFactoryVTable[29];

            if (o_EnumAdapterByGpuPreference != nullptr)
                DetourAttach(&(PVOID&) o_EnumAdapterByGpuPreference, DxgiFactoryHooks::EnumAdapterByGpuPreference);
        }
    }

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to hook IDXGIFactory: {:X}", detourResult);
        o_EnumAdapters = nullptr;
        o_CreateSwapChain = nullptr;
        o_EnumAdapters1 = nullptr;
        o_CreateSwapChainForHwnd = nullptr;
        o_CreateSwapChainForCoreWindow = nullptr;
        o_EnumAdapterByLuid = nullptr;
        o_EnumAdapterByGpuPreference = nullptr;
    }
}

HRESULT DxgiFactoryHooks::CreateSwapChain(IDXGIFactory* realFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                          IDXGISwapChain** ppSwapChain)
{
    *ppSwapChain = nullptr;

    if (State::Instance().vulkanCreatingSC)
    {
        LOG_WARN("Vulkan is creating swapchain!");

        if (pDesc != nullptr)
            LOG_DEBUG("Width: {}, Height: {}, Format: {}, Count: {}, Hwnd: {:X}, Windowed: {}, SkipWrapping: {}",
                      pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, (UINT) pDesc->BufferDesc.Format,
                      pDesc->BufferCount, (SIZE_T) pDesc->OutputWindow, pDesc->Windowed, _skipFGSwapChainCreation);

        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        ScopedSkipParentWrapping skipParentWrapping {};

        auto res = o_CreateSwapChain(realFactory, pDevice, pDesc, ppSwapChain);
        return res;
    }

    if (pDevice == nullptr || pDesc == nullptr)
    {
        LOG_WARN("pDevice or pDesc is nullptr!");

        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        ScopedSkipParentWrapping skipParentWrapping {};

        auto res = o_CreateSwapChain(realFactory, pDevice, pDesc, ppSwapChain);
        return res;
    }

    if (pDesc->BufferDesc.Height < 100 || pDesc->BufferDesc.Width < 100)
    {
        LOG_WARN("Overlay call!");

        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        ScopedSkipParentWrapping skipParentWrapping {};

        auto res = o_CreateSwapChain(realFactory, pDevice, pDesc, ppSwapChain);
        return res;
    }

    DXGI_SWAP_CHAIN_DESC localDesc {};
    memcpy(&localDesc, pDesc, sizeof(DXGI_SWAP_CHAIN_DESC));

    LOG_DEBUG("Width: {}, Height: {}, Format: {}, Count: {}, Flags: {:X}, Hwnd: {:X}, Windowed: {}, SkipWrapping: {}",
              localDesc.BufferDesc.Width, localDesc.BufferDesc.Height, (UINT) localDesc.BufferDesc.Format,
              localDesc.BufferCount, localDesc.Flags, (SIZE_T) localDesc.OutputWindow, localDesc.Windowed,
              _skipFGSwapChainCreation);

    if (State::Instance().activeFgOutput == FGOutput::XeFG &&
        Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        if (!localDesc.Windowed)
        {
            State::Instance().SCExclusiveFullscreen = true;
            localDesc.Windowed = true;
        }

        localDesc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        localDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_STRETCHED;
    }

    // For vsync override
    if (!localDesc.Windowed)
    {
        LOG_INFO("Game is creating fullscreen swapchain, disabled V-Sync overrides");
        Config::Instance()->OverrideVsync.set_volatile_value(false);
    }

    if (Config::Instance()->OverrideVsync.value_or_default())
    {
        localDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        localDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        if (localDesc.BufferCount < 2)
            localDesc.BufferCount = 2;
    }

    State::Instance().SCAllowTearing = (localDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;
    State::Instance().SCLastFlags = localDesc.Flags;
    State::Instance().realExclusiveFullscreen = !localDesc.Windowed;

#ifdef DETAILED_SC_LOGS
    LOG_TRACE("localDesc.BufferCount: {}", localDesc.BufferCount);
    LOG_TRACE("localDesc.BufferDesc.Format: {}", magic_enum::enum_name(localDesc.BufferDesc.Format));
    LOG_TRACE("localDesc.BufferDesc.Height: {}", localDesc.BufferDesc.Height);
    LOG_TRACE("localDesc.BufferDesc.RefreshRate.Denominator: {}", localDesc.BufferDesc.RefreshRate.Denominator);
    LOG_TRACE("localDesc.BufferDesc.RefreshRate.Numerator: {}", localDesc.BufferDesc.RefreshRate.Numerator);
    LOG_TRACE("localDesc.BufferDesc.Scaling: {}", magic_enum::enum_name(localDesc.BufferDesc.Scaling));
    LOG_TRACE("localDesc.BufferDesc.ScanlineOrdering: {}",
              magic_enum::enum_name(localDesc.BufferDesc.ScanlineOrdering));
    LOG_TRACE("localDesc.BufferDesc.Width: {}", localDesc.BufferDesc.Width);
    LOG_TRACE("localDesc.BufferUsage: {}", localDesc.BufferUsage);
    LOG_TRACE("localDesc.Flags: {}", localDesc.Flags);
    LOG_TRACE("localDesc.OutputWindow: {}", (UINT64) localDesc.OutputWindow);
    LOG_TRACE("localDesc.SampleDesc.Count: {}", localDesc.SampleDesc.Count);
    LOG_TRACE("localDesc.SampleDesc.Quality: {}", localDesc.SampleDesc.Quality);
    LOG_TRACE("localDesc.SwapEffect: {}", magic_enum::enum_name(localDesc.SwapEffect));
    LOG_TRACE("localDesc.Windowed: {}", localDesc.Windowed);
#endif //

    // Check for SL proxy, get real queue
    ID3D12CommandQueue* cq = nullptr;
    IUnknown* real = nullptr;
    HRESULT FGSCResult = E_NOTIMPL;

    if (pDevice->QueryInterface(IID_PPV_ARGS(&cq)) == S_OK)
    {
        cq->Release();

        if (State::Instance().currentD3D12Device == nullptr)
        {
            ID3D12Device* device = nullptr;
            if (cq->GetDevice(IID_PPV_ARGS(&device)) == S_OK)
            {
                if (device != nullptr)
                {
                    // Update current D3D12 device and adapter
                    if (State::Instance().currentD3D12Device != device)
                    {
                        State::Instance().currentD3D12Device = device;

                        IDXGIDevice* dxgiDevice = nullptr;
                        if (device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)) == S_OK)
                        {
                            IDXGIAdapter* adapter = nullptr;
                            if (dxgiDevice->GetAdapter(&adapter) == S_OK)
                            {
                                adapter->GetDesc(&State::Instance().currentD3D12AdepterDesc);
                                adapter->Release();
                            }
                            else
                            {
                                State::Instance().currentD3D12AdepterDesc = {};
                            }

                            dxgiDevice->Release();
                        }
                    }
                    else
                    {
                        State::Instance().currentD3D12AdepterDesc = {};
                    }

                    LOG_INFO("Captured D3D12 device from command queue: {:X}", (UINT64) device);
                    D3D12Hooks::HookDevice(State::Instance().currentD3D12Device);
                    device->Release();
                }
            }
        }

        if (!Util::CheckForRealObject(__FUNCTION__, cq, &real))
            real = cq;

        State::Instance().currentCommandQueue = (ID3D12CommandQueue*) real;

        // Create FG SwapChain
        if (!_skipFGSwapChainCreation)
        {
            ScopedSkipFGSCCreation skipFGSCCreation {};
            FGSCResult = FGHooks::CreateSwapChain(realFactory, real, &localDesc, ppSwapChain);

            if (FGSCResult == S_OK)
            {
                State::Instance().currentSwapchainDesc = localDesc;
                return FGSCResult;
            }
        }
    }
    else
    {
        LOG_INFO("Failed to get ID3D12CommandQueue from pDevice, creating Dx11 swapchain!");

        ID3D11Device* device = nullptr;

        if (pDevice->QueryInterface(IID_PPV_ARGS(&device)) == S_OK)
        {
            D3D11Hooks::HookToDevice(device);
            device->Release();

            // Update current D3D11 device and adapter
            if (State::Instance().currentD3D11Device != device)
            {
                State::Instance().currentD3D11Device = device;

                IDXGIDevice* dxgiDevice = nullptr;
                if (device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)) == S_OK)
                {
                    IDXGIAdapter* adapter = nullptr;
                    if (dxgiDevice->GetAdapter(&adapter) == S_OK)
                    {
                        adapter->GetDesc(&State::Instance().currentD3D11AdepterDesc);
                        adapter->Release();
                    }
                    else
                    {
                        State::Instance().currentD3D11AdepterDesc = {};
                    }

                    dxgiDevice->Release();
                }
            }
            else
            {
                State::Instance().currentD3D11AdepterDesc = {};
            }
        }
    }

    HRESULT result = E_FAIL;

    // If FG is disabled or call is coming from FG library
    // Create the DXGI SwapChain and wrap it
    if (_skipFGSwapChainCreation || FGSCResult != S_OK)
    {
        // !_skipFGSwapChainCreation for preventing early enablement flags
        if (!_skipFGSwapChainCreation)
        {
            State::Instance().skipDxgiLoadChecks = true;

            if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
                State::Instance().skipHeapCapture = true;
        }

        {
            ScopedSkipParentWrapping skipParentWrapping {};
            result = o_CreateSwapChain(realFactory, pDevice, &localDesc, ppSwapChain);
        }

        if (!_skipFGSwapChainCreation)
        {
            if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
                State::Instance().skipHeapCapture = false;

            State::Instance().skipDxgiLoadChecks = false;
        }

        if (result == S_OK)
        {
            State::Instance().currentSwapchainDesc = localDesc;

            // Check for SL proxy
            IDXGISwapChain* realSC = nullptr;
            if (!Util::CheckForRealObject(__FUNCTION__, *ppSwapChain, (IUnknown**) &realSC))
                realSC = *ppSwapChain;

            State::Instance().currentRealSwapchain = realSC;

            IUnknown* realDevice = nullptr;
            if (!Util::CheckForRealObject(__FUNCTION__, pDevice, (IUnknown**) &realDevice))
                realDevice = pDevice;

            if (Util::GetProcessWindow() == localDesc.OutputWindow)
            {
                State::Instance().screenWidth = static_cast<float>(localDesc.BufferDesc.Width);
                State::Instance().screenHeight = static_cast<float>(localDesc.BufferDesc.Height);
            }

            LOG_DEBUG("Created new swapchain: {0:X}, hWnd: {1:X}", (UINT64) *ppSwapChain,
                      (UINT64) localDesc.OutputWindow);
            *ppSwapChain =
                new WrappedIDXGISwapChain4(realSC, realDevice, localDesc.OutputWindow, localDesc.Flags, false);

            // Set as currentSwapchain is FG is disabled
            if (!_skipFGSwapChainCreation)
                State::Instance().currentSwapchain = *ppSwapChain;

            State::Instance().currentWrappedSwapchain = *ppSwapChain;

            LOG_DEBUG("Created new WrappedIDXGISwapChain4: {:X}, pDevice: {:X}", (size_t) *ppSwapChain,
                      (size_t) pDevice);
        }
    }
    else
    {
        LOG_ERROR("CreateSwapChain failed: {:X}", (UINT) result);
    }

    return result;
}

HRESULT DxgiFactoryHooks::CreateSwapChainForHwnd(IDXGIFactory2* realFactory, IUnknown* pDevice, HWND hWnd,
                                                 const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                 const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                                 IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)
{
    *ppSwapChain = nullptr;

    static bool firstCall = static_cast<bool>(State::Instance().gameQuirks & GameQuirk::NoFSRFGFirstSwapchain);
    if (firstCall)
    {
        LOG_DEBUG("Skipping FG swapchain creation");
        _skipFGSwapChainCreation = true;
    }

    if (State::Instance().vulkanCreatingSC)
    {
        LOG_WARN("Vulkan is creating swapchain!");
        HRESULT result;

        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
            ScopedSkipParentWrapping skipParentWrapping {};

            result = o_CreateSwapChainForHwnd(realFactory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput,
                                              ppSwapChain);
        }

        if (firstCall)
            _skipFGSwapChainCreation = false;

        return result;
    }

    if (pDevice == nullptr || pDesc == nullptr)
    {
        LOG_WARN("pDevice or pDesc is nullptr!");
        HRESULT result;

        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
            ScopedSkipParentWrapping skipParentWrapping {};
            result = o_CreateSwapChainForHwnd(realFactory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput,
                                              ppSwapChain);
        }

        if (firstCall)
            _skipFGSwapChainCreation = false;

        return result;
    }

    if (pDesc->Height < 100 || pDesc->Width < 100)
    {
        LOG_WARN("Overlay call!");
        HRESULT result;

        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
            ScopedSkipParentWrapping skipParentWrapping {};
            result = o_CreateSwapChainForHwnd(realFactory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput,
                                              ppSwapChain);
        }

        if (firstCall)
            _skipFGSwapChainCreation = false;

        return result;
    }

    DXGI_SWAP_CHAIN_DESC1 localDesc {};
    memcpy(&localDesc, pDesc, sizeof(DXGI_SWAP_CHAIN_DESC1));

    LOG_DEBUG("Width: {}, Height: {}, Format: {}, Count: {}, Flags: {:X}, Hwnd: {:X}, SkipWrapping: {}",
              localDesc.Width, localDesc.Height, (UINT) localDesc.Format, localDesc.BufferCount, localDesc.Flags,
              (size_t) hWnd, _skipFGSwapChainCreation);

    if (pFullscreenDesc != nullptr)
        State::Instance().realExclusiveFullscreen = !pFullscreenDesc->Windowed;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC localFullscreenDesc {};

    if (pFullscreenDesc != nullptr)
        memcpy(&localFullscreenDesc, pFullscreenDesc, sizeof(DXGI_SWAP_CHAIN_FULLSCREEN_DESC));

    if (State::Instance().activeFgOutput == FGOutput::XeFG &&
        Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        if (pFullscreenDesc != nullptr && !localFullscreenDesc.Windowed)
        {

            State::Instance().SCExclusiveFullscreen = true;
            localFullscreenDesc.Windowed = true;
        }

        localDesc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        localDesc.Scaling = DXGI_SCALING_STRETCH;
    }

    // For vsync override
    if (pFullscreenDesc != nullptr && !localFullscreenDesc.Windowed)
    {
        LOG_INFO("Game is creating fullscreen swapchain, disabled V-Sync overrides");
        Config::Instance()->OverrideVsync.set_volatile_value(false);
    }

    if (Config::Instance()->OverrideVsync.value_or_default())
    {
        localDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        localDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        if (localDesc.BufferCount < 2)
            localDesc.BufferCount = 2;
    }

    State::Instance().SCAllowTearing = (localDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;
    State::Instance().SCLastFlags = localDesc.Flags;
    State::Instance().realExclusiveFullscreen = pFullscreenDesc != nullptr && !localFullscreenDesc.Windowed;

#ifdef VER_PRE_RELEASE
    LOG_TRACE("localDesc.AlphaMode : {}", magic_enum::enum_name(localDesc.AlphaMode));
    LOG_TRACE("localDesc.BufferCount : {}", localDesc.BufferCount);
    LOG_TRACE("localDesc.BufferUsage : {}", localDesc.BufferUsage);
    LOG_TRACE("localDesc.Flags : {}", localDesc.Flags);
    LOG_TRACE("localDesc.Format : {}", magic_enum::enum_name(localDesc.Format));
    LOG_TRACE("localDesc.Height : {}", localDesc.Height);
    LOG_TRACE("localDesc.SampleDesc.Count : {}", localDesc.SampleDesc.Count);
    LOG_TRACE("localDesc.SampleDesc.Quality : {}", localDesc.SampleDesc.Quality);
    LOG_TRACE("localDesc.Scaling : {}", magic_enum::enum_name(localDesc.Scaling));
    LOG_TRACE("localDesc.Stereo : {}", localDesc.Stereo);

    if (pFullscreenDesc != nullptr)
    {
        LOG_TRACE("localFullscreenDesc.RefreshRate.Denominator : {}", localFullscreenDesc.RefreshRate.Denominator);
        LOG_TRACE("localFullscreenDesc.RefreshRate.Numerator : {}", localFullscreenDesc.RefreshRate.Numerator);
        LOG_TRACE("localFullscreenDesc.Scaling : {}", magic_enum::enum_name(localFullscreenDesc.Scaling));
        LOG_TRACE("localFullscreenDesc.ScanlineOrdering : {}",
                  magic_enum::enum_name(localFullscreenDesc.ScanlineOrdering));
        LOG_TRACE("localFullscreenDesc.Windowed : {}", localFullscreenDesc.Windowed);
    }
#endif

    // Check for SL proxy, get real queue
    ID3D12CommandQueue* cq = nullptr;
    IUnknown* real = nullptr;
    HRESULT FGSCResult = E_NOTIMPL;

    if (pDevice->QueryInterface(IID_PPV_ARGS(&cq)) == S_OK)
    {
        cq->Release();

        if (State::Instance().currentD3D12Device == nullptr)
        {
            ID3D12Device* device = nullptr;
            if (cq->GetDevice(IID_PPV_ARGS(&device)) == S_OK)
            {
                if (device != nullptr)
                {
                    // Update current D3D12 device and adapter
                    if (State::Instance().currentD3D12Device != device)
                    {
                        State::Instance().currentD3D12Device = device;

                        IDXGIDevice* dxgiDevice = nullptr;
                        if (device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)) == S_OK)
                        {
                            IDXGIAdapter* adapter = nullptr;
                            if (dxgiDevice->GetAdapter(&adapter) == S_OK)
                            {
                                adapter->GetDesc(&State::Instance().currentD3D12AdepterDesc);
                                adapter->Release();
                            }
                            else
                            {
                                State::Instance().currentD3D12AdepterDesc = {};
                            }

                            dxgiDevice->Release();
                        }
                    }
                    else
                    {
                        State::Instance().currentD3D12AdepterDesc = {};
                    }

                    LOG_INFO("Captured D3D12 device from command queue: {:X}", (UINT64) device);
                    D3D12Hooks::HookDevice(State::Instance().currentD3D12Device);
                    device->Release();
                }
            }
        }

        if (!Util::CheckForRealObject(__FUNCTION__, cq, &real))
            real = cq;

        State::Instance().currentCommandQueue = (ID3D12CommandQueue*) real;

        // Create FG SwapChain
        if (!_skipFGSwapChainCreation)
        {
            ScopedSkipFGSCCreation skipFGSCCreation {};
            FGSCResult = FGHooks::CreateSwapChainForHwnd(realFactory, real, hWnd, &localDesc,
                                                         pFullscreenDesc != nullptr ? &localFullscreenDesc : nullptr,
                                                         pRestrictToOutput, ppSwapChain);

            if (FGSCResult == S_OK)
            {
                ((IDXGISwapChain*) *ppSwapChain)->GetDesc(&State::Instance().currentSwapchainDesc);
                return FGSCResult;
            }
        }
    }
    else
    {
        LOG_INFO("Failed to get ID3D12CommandQueue from pDevice, creating Dx11 swapchain!");

        ID3D11Device* device = nullptr;

        if (pDevice->QueryInterface(IID_PPV_ARGS(&device)) == S_OK)
        {
            D3D11Hooks::HookToDevice(device);

            device->Release();

            // Update current D3D11 device and adapter
            if (State::Instance().currentD3D11Device != device)
            {
                State::Instance().currentD3D11Device = device;

                IDXGIDevice* dxgiDevice = nullptr;
                if (device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)) == S_OK)
                {
                    IDXGIAdapter* adapter = nullptr;
                    if (dxgiDevice->GetAdapter(&adapter) == S_OK)
                    {
                        adapter->GetDesc(&State::Instance().currentD3D11AdepterDesc);
                        adapter->Release();
                    }
                    else
                    {
                        State::Instance().currentD3D11AdepterDesc = {};
                    }

                    dxgiDevice->Release();
                }
            }
            else
            {
                State::Instance().currentD3D11AdepterDesc = {};
            }
        }
    }

    HRESULT result = E_FAIL;

    // If FG is disabled or call is coming from FG library
    // Create the DXGI SwapChain and wrap it
    if (_skipFGSwapChainCreation || FGSCResult != S_OK)
    {

        // !_skipFGSwapChainCreation for preventing early enablement flags
        if (!_skipFGSwapChainCreation)
        {
            State::Instance().skipDxgiLoadChecks = true;

            if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
                State::Instance().skipHeapCapture = true;
        }

        {
            ScopedSkipParentWrapping skipParentWrapping {};
            result = o_CreateSwapChainForHwnd(realFactory, pDevice, hWnd, &localDesc,
                                              pFullscreenDesc != nullptr ? &localFullscreenDesc : nullptr,
                                              pRestrictToOutput, ppSwapChain);
        }

        if (!_skipFGSwapChainCreation)
        {
            if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
                State::Instance().skipHeapCapture = false;

            State::Instance().skipDxgiLoadChecks = false;
        }

        if (result == S_OK)
        {
            // check for SL proxy
            IDXGISwapChain1* realSC = nullptr;
            if (!Util::CheckForRealObject(__FUNCTION__, *ppSwapChain, (IUnknown**) &realSC))
                realSC = *ppSwapChain;

            State::Instance().currentRealSwapchain = realSC;

            IUnknown* readDevice = nullptr;
            if (!Util::CheckForRealObject(__FUNCTION__, pDevice, (IUnknown**) &readDevice))
                readDevice = pDevice;

            if (Util::GetProcessWindow() == hWnd)
            {
                State::Instance().screenWidth = static_cast<float>(localDesc.Width);
                State::Instance().screenHeight = static_cast<float>(localDesc.Height);
            }

            realSC->GetDesc(&State::Instance().currentSwapchainDesc);

            LOG_DEBUG("Created new swapchain: {0:X}, hWnd: {1:X}", (uintptr_t) *ppSwapChain, (uintptr_t) hWnd);
            *ppSwapChain = new WrappedIDXGISwapChain4(realSC, readDevice, hWnd, localDesc.Flags, false);

            LOG_DEBUG("Created new WrappedIDXGISwapChain4: {0:X}, pDevice: {1:X}", (uintptr_t) *ppSwapChain,
                      (uintptr_t) pDevice);

            if (!_skipFGSwapChainCreation)
                State::Instance().currentSwapchain = *ppSwapChain;

            State::Instance().currentWrappedSwapchain = *ppSwapChain;
        }
        else
        {
            LOG_ERROR("CreateSwapChainForHwnd failed: {:X}", (UINT) result);
        }
    }

    if (firstCall)
    {
        LOG_DEBUG("Unsetting skip FG swapchain creation");
        _skipFGSwapChainCreation = false;
        firstCall = false;
    }

    return result;
}

HRESULT DxgiFactoryHooks::CreateSwapChainForCoreWindow(IDXGIFactory2* realFactory, IUnknown* pDevice, IUnknown* pWindow,
                                                       const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                       IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)
{
    if (State::Instance().vulkanCreatingSC)
    {
        LOG_WARN("Vulkan is creating swapchain!");

        if (pDesc != nullptr)
            LOG_DEBUG("Width: {}, Height: {}, Format: {}, Flags: {:X}, Count: {}, SkipWrapping: {}", pDesc->Width,
                      pDesc->Height, (UINT) pDesc->Format, pDesc->Flags, pDesc->BufferCount, _skipFGSwapChainCreation);

        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        return realFactory->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
    }

    if (pDevice == nullptr || pDesc == nullptr)
    {
        LOG_WARN("pDevice or pDesc is nullptr!");
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        return realFactory->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
    }

    if (pDesc->Height < 100 || pDesc->Width < 100)
    {
        LOG_WARN("Overlay call!");

        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        return realFactory->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
    }

    DXGI_SWAP_CHAIN_DESC1 localDesc {};
    memcpy(&localDesc, pDesc, sizeof(DXGI_SWAP_CHAIN_DESC1));

    LOG_DEBUG("Width: {}, Height: {}, Format: {}, Count: {}, SkipWrapping: {}", localDesc.Width, localDesc.Height,
              (UINT) localDesc.Format, localDesc.BufferCount, _skipFGSwapChainCreation);

    // For vsync override
    if (Config::Instance()->OverrideVsync.value_or_default())
    {
        localDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        localDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        if (localDesc.BufferCount < 2)
            localDesc.BufferCount = 2;
    }

    State::Instance().SCAllowTearing = (localDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;
    State::Instance().SCLastFlags = localDesc.Flags;
    State::Instance().realExclusiveFullscreen = false;

    ID3D12CommandQueue* cq = nullptr;
    IUnknown* real = nullptr;
    if (pDevice->QueryInterface(IID_PPV_ARGS(&cq)) == S_OK)
    {
        cq->Release();

        if (!Util::CheckForRealObject(__FUNCTION__, cq, &real))
            real = cq;

        State::Instance().currentCommandQueue = (ID3D12CommandQueue*) real;
    }

    HRESULT result = E_FAIL;
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        result =
            o_CreateSwapChainForCoreWindow(realFactory, pDevice, pWindow, &localDesc, pRestrictToOutput, ppSwapChain);
    }

    if (result == S_OK)
    {
        // check for SL proxy
        IDXGISwapChain* realSC = nullptr;
        if (!Util::CheckForRealObject(__FUNCTION__, *ppSwapChain, (IUnknown**) &realSC))
            realSC = *ppSwapChain;

        State::Instance().currentRealSwapchain = realSC;

        IUnknown* readDevice = nullptr;
        if (!Util::CheckForRealObject(__FUNCTION__, pDevice, (IUnknown**) &readDevice))
            readDevice = pDevice;

        realSC->GetDesc(&State::Instance().currentSwapchainDesc);

        State::Instance().screenWidth = static_cast<float>(localDesc.Width);
        State::Instance().screenHeight = static_cast<float>(localDesc.Height);

        LOG_DEBUG("Created new swapchain: {0:X}, hWnd: {1:X}", (UINT64) *ppSwapChain, (UINT64) pWindow);
        *ppSwapChain = new WrappedIDXGISwapChain4(realSC, readDevice, (HWND) pWindow, localDesc.Flags, true);

        if (!_skipFGSwapChainCreation)
            State::Instance().currentSwapchain = *ppSwapChain;

        State::Instance().currentWrappedSwapchain = *ppSwapChain;

        LOG_DEBUG("Created new WrappedIDXGISwapChain4: {0:X}, pDevice: {1:X}", (UINT64) *ppSwapChain, (UINT64) pDevice);
    }

    return result;
}

HRESULT DxgiFactoryHooks::EnumAdapters(IDXGIFactory* realFactory, UINT Adapter, IDXGIAdapter** ppAdapter)
{
    HRESULT result = S_OK;

    if (!_skipHighPerfCheck && Config::Instance()->PreferDedicatedGpu.value_or_default())
    {
        if (Config::Instance()->PreferFirstDedicatedGpu.value_or_default() && Adapter > 0)
        {
            LOG_DEBUG("{}, returning not found", Adapter);
            return DXGI_ERROR_NOT_FOUND;
        }

        IDXGIFactory6* factory6 = nullptr;
        if (realFactory->QueryInterface(IID_PPV_ARGS(&factory6)) == S_OK && factory6 != nullptr)
        {
            LOG_DEBUG("Trying to select high performance adapter ({})", Adapter);

            {
                ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
                ScopedSkipHighPerfCheck skipHighPerfCheck {};

                result = o_EnumAdapterByGpuPreference(factory6, Adapter, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                      __uuidof(IDXGIAdapter1), (void**) ppAdapter);
            }

            if (result != S_OK)
            {
                LOG_ERROR("Can't get high performance adapter: {:X}, fallback to standard method", Adapter);
                ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
                result = o_EnumAdapters(realFactory, Adapter, ppAdapter);
            }

            if (result == S_OK)
            {
                DXGI_ADAPTER_DESC desc;
                ScopedSkipSpoofing skipSpoofing {};

                if ((*ppAdapter)->GetDesc(&desc) == S_OK)
                {
                    std::wstring name(desc.Description);
                    LOG_DEBUG("Adapter ({}) will be used", wstring_to_string(name));
                }
                else
                {
                    LOG_ERROR("Can't get adapter description!");
                }
            }

            factory6->Release();
        }
        else
        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
            result = o_EnumAdapters(realFactory, Adapter, ppAdapter);
        }
    }
    else
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        result = o_EnumAdapters(realFactory, Adapter, ppAdapter);
    }

    if (result == S_OK)
    {
        CheckAdapter(*ppAdapter);
        DxgiSpoofing::AttachToAdapter(*ppAdapter);
    }

#if _DEBUG
    LOG_TRACE("result: {:X}, Adapter: {}, pAdapter: {:X}", (UINT) result, Adapter, (uintptr_t) *ppAdapter);
#endif

    return result;
}

HRESULT DxgiFactoryHooks::EnumAdapters1(IDXGIFactory1* realFactory, UINT Adapter, IDXGIAdapter1** ppAdapter)
{
    HRESULT result = S_OK;

    if (!_skipHighPerfCheck && Config::Instance()->PreferDedicatedGpu.value_or_default())
    {
        LOG_WARN("High perf GPU selection");

        if (Config::Instance()->PreferFirstDedicatedGpu.value_or_default() && Adapter > 0)
        {
            LOG_DEBUG("{}, returning not found", Adapter);
            return DXGI_ERROR_NOT_FOUND;
        }

        IDXGIFactory6* factory6 = nullptr;
        if (realFactory->QueryInterface(IID_PPV_ARGS(&factory6)) == S_OK && factory6 != nullptr)
        {
            LOG_DEBUG("Trying to select high performance adapter ({})", Adapter);

            {
                ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
                ScopedSkipHighPerfCheck skipHighPerfCheck {};

                result = o_EnumAdapterByGpuPreference(factory6, Adapter, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                      __uuidof(IDXGIAdapter1), (void**) ppAdapter);
            }

            if (result != S_OK)
            {
                LOG_ERROR("Can't get high performance adapter: {:X}, fallback to standard method", Adapter);
                ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
                result = o_EnumAdapters1(realFactory, Adapter, ppAdapter);
            }

            if (result == S_OK)
            {
                DXGI_ADAPTER_DESC desc;
                ScopedSkipSpoofing skipSpoofing {};

                if ((*ppAdapter)->GetDesc(&desc) == S_OK)
                {
                    std::wstring name(desc.Description);
                    LOG_DEBUG("High performance adapter ({}) will be used", wstring_to_string(name));
                }
                else
                {
                    LOG_DEBUG("High performance adapter (Can't get description!) will be used");
                }
            }

            factory6->Release();
        }
        else
        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
            result = o_EnumAdapters1(realFactory, Adapter, ppAdapter);
        }
    }
    else
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        result = o_EnumAdapters1(realFactory, Adapter, ppAdapter);
    }

    if (result == S_OK)
    {
        CheckAdapter(*ppAdapter);
        DxgiSpoofing::AttachToAdapter(*ppAdapter);
    }

#if _DEBUG
    LOG_TRACE("result: {:X}, Adapter: {}, pAdapter: {:X}", (UINT) result, Adapter, (uintptr_t) *ppAdapter);
#endif

    return result;
}

HRESULT DxgiFactoryHooks::EnumAdapterByLuid(IDXGIFactory4* realFactory, LUID AdapterLuid, REFIID riid,
                                            void** ppvAdapter)
{
    HRESULT result;
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        result = o_EnumAdapterByLuid(realFactory, AdapterLuid, riid, ppvAdapter);
    }

    if (result == S_OK)
    {
        CheckAdapter((IUnknown*) *ppvAdapter);
        DxgiSpoofing::AttachToAdapter((IUnknown*) *ppvAdapter);
    }

#if _DEBUG
    LOG_TRACE("result: {:X}, pAdapter: {:X}", (UINT) result, (uintptr_t) *ppvAdapter);
#endif

    return result;
}

HRESULT DxgiFactoryHooks::EnumAdapterByGpuPreference(IDXGIFactory6* realFactory, UINT Adapter,
                                                     DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void** ppvAdapter)
{
    HRESULT result;

    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        result = o_EnumAdapterByGpuPreference(realFactory, Adapter, GpuPreference, riid, ppvAdapter);
    }

    if (result == S_OK)
    {
        CheckAdapter((IUnknown*) *ppvAdapter);
        DxgiSpoofing::AttachToAdapter((IUnknown*) *ppvAdapter);
    }

#if _DEBUG
    LOG_TRACE("result: {:X}, Adapter: {}, pAdapter: {:X}", (UINT) result, Adapter, (uintptr_t) *ppvAdapter);
#endif

    return result;
}
