#include "pch.h"
#include "DxgiFactory_WrappedCalls.h"

#include "FG_Hooks.h"
#include "D3D11_Hooks.h"
#include "D3D12_Hooks.h"

#include <Config.h>
#include <spoofing/Dxgi_Spoofing.h>

#include <misc/HiddenWindow.h>
#include <with_dx12/with_dx12.h>
#include <wrapped/wrapped_swapchain.h>
#include <with_dx12/dx11_with_dx12_sc.h>

#include <d3d11.h>
#include <magic_enum.hpp>

// #define DETAILED_SC_LOGS

#ifdef DETAILED_SC_LOGS
#include <magic_enum.hpp>
#endif
#include <misc/IdentifyGpu.h>

static bool ShouldCreateDx11wDx12Swapchain()
{
    return State::Instance().activeFgInput == FGInput::Upscaler && State::Instance().activeFgOutput != FGOutput::NoFG &&
           State::Instance().activeFgInput != FGInput::NvngxFG;
}

static bool PrepareDx12InteropDesc(DXGI_SWAP_CHAIN_DESC& desc)
{
    if (desc.SampleDesc.Count > 1)
    {
        LOG_WARN("Dx11wDx12 interop does not support MSAA swapchains!");
        return false;
    }

    if (desc.BufferCount < 2)
        desc.BufferCount = 2;

    if (desc.SwapEffect == DXGI_SWAP_EFFECT_DISCARD)
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    else if (desc.SwapEffect == DXGI_SWAP_EFFECT_SEQUENTIAL)
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Windowed = TRUE;
    return true;
}

static bool PrepareDx12InteropDesc1(DXGI_SWAP_CHAIN_DESC1& desc)
{
    if (desc.SampleDesc.Count > 1)
    {
        LOG_ERROR("Dx11wDx12 interop does not support MSAA swapchains!");
        return false;
    }

    if (desc.BufferCount < 2)
        desc.BufferCount = 2;

    if (desc.SwapEffect == DXGI_SWAP_EFFECT_DISCARD)
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    else if (desc.SwapEffect == DXGI_SWAP_EFFECT_SEQUENTIAL)
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    return true;
}

HRESULT DxgiFactoryWrappedCalls::CreateSwapChain(IDXGIFactory* realFactory, WrappedIDXGIFactory7* wrappedFactory,
                                                 IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC* pDesc,
                                                 IDXGISwapChain** ppSwapChain)
{
    *ppSwapChain = nullptr;

    DXGI_SWAP_CHAIN_DESC localDesc = {};

    if (pDesc != nullptr)
        memcpy(&localDesc, pDesc, sizeof(DXGI_SWAP_CHAIN_DESC));

    if (State::Instance().vulkanCreatingSC)
    {
        LOG_WARN("Vulkan is creating swapchain!");

        if (pDesc != nullptr)
            LOG_DEBUG("Width: {}, Height: {}, Format: {}, Count: {}, Hwnd: {:X}, Windowed: {}, SkipWrapping: {}",
                      pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, (UINT) pDesc->BufferDesc.Format,
                      pDesc->BufferCount, (SIZE_T) pDesc->OutputWindow, pDesc->Windowed, _skipFGSwapChainCreation);

        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        ScopedSkipParentWrapping skipParentWrapping {};

        auto res = realFactory->CreateSwapChain(pDevice, pDesc == nullptr ? nullptr : &localDesc, ppSwapChain);
        return res;
    }

    if (pDevice == nullptr || pDesc == nullptr)
    {
        LOG_WARN("pDevice or pDesc is nullptr!");

        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        ScopedSkipParentWrapping skipParentWrapping {};

        auto res = realFactory->CreateSwapChain(pDevice, pDesc == nullptr ? nullptr : &localDesc, ppSwapChain);
        return res;
    }

    if (pDesc->BufferDesc.Height < 100 || pDesc->BufferDesc.Width < 100)
    {
        LOG_WARN("Overlay call!");

        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        ScopedSkipParentWrapping skipParentWrapping {};

        auto res = realFactory->CreateSwapChain(pDevice, pDesc == nullptr ? nullptr : &localDesc, ppSwapChain);
        return res;
    }

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
        if (State::Instance().currentD3D12Device == nullptr)
        {
            ID3D12Device* device = nullptr;
            if (cq->GetDevice(IID_PPV_ARGS(&device)) == S_OK)
            {
                if (device != nullptr)
                {
                    // Update current D3D12 device
                    if (State::Instance().currentD3D12Device != device)
                    {
                        State::Instance().currentD3D12Device = device;
                    }

                    LOG_INFO("Captured D3D12 device from command queue: {:X}", (UINT64) device);
                    D3D12Hooks::HookDevice(State::Instance().currentD3D12Device);
                }
            }
        }

        if (!Util::CheckForRealObject(__FUNCTION__, cq, &real))
            real = cq;

        State::Instance().currentCommandQueue = (ID3D12CommandQueue*) real;

        if (State::Instance().currentD3D12Device != nullptr)
        {
            WithDx12::SetD3D12Objects(State::Instance().currentD3D12Device, State::Instance().currentCommandQueue,
                                      D3D12_COMMAND_LIST_TYPE_DIRECT);
        }

        // Create FG SwapChain
        if (!_skipFGSwapChainCreation)
        {
            ScopedSkipFGSCCreation skipFGSCCreation {};
            FGSCResult = FGHooks::CreateSwapChain(wrappedFactory, real, &localDesc, ppSwapChain);

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
            State::Instance().currentD3D11Device = device;

            if (!_skipFGSwapChainCreation && ShouldCreateDx11wDx12Swapchain())
            {
                auto hiddenHwnd = CreateHiddenSwapchainWindow();

                ID3D12Device* dx12Device = nullptr;
                ID3D12CommandQueue* dx12Queue = nullptr;

                if (WithDx12::PrepareD3D12ForD3D11(device, D3D_FEATURE_LEVEL_11_0))
                {
                    dx12Device = WithDx12::GetD3D12Device();
                    dx12Queue = WithDx12::GetD3D12CommandQueue();
                }

                if (hiddenHwnd != nullptr && dx12Device != nullptr && dx12Queue != nullptr)
                {
                    DXGI_SWAP_CHAIN_DESC realDesc = localDesc;
                    realDesc.OutputWindow = hiddenHwnd;
                    realDesc.Windowed = TRUE;

                    IDXGISwapChain* realDx11SwapChain = nullptr;
                    HRESULT realScResult = E_FAIL;
                    {
                        ScopedSkipParentWrapping skipParentWrapping {};
                        realScResult = realFactory->CreateSwapChain(pDevice, &realDesc, &realDx11SwapChain);
                    }

                    DXGI_SWAP_CHAIN_DESC fgDesc = localDesc;
                    HRESULT fgScResult = E_FAIL;
                    IDXGISwapChain* fgSwapChain = nullptr;
                    IDXGISwapChain4* fgSwapChain4 = nullptr;
                    bool fgSwapChainIsRealFG = false;

                    if (SUCCEEDED(realScResult) && PrepareDx12InteropDesc(fgDesc))
                    {
                        {
                            ScopedSkipFGSCCreation skipFGSCCreation {};
                            fgScResult = FGHooks::CreateSwapChain(wrappedFactory, dx12Queue, &fgDesc, &fgSwapChain);
                            fgSwapChainIsRealFG = SUCCEEDED(fgScResult) && fgSwapChain != nullptr;
                        }

                        if (FAILED(fgScResult) || fgSwapChain == nullptr)
                        {
                            fgSwapChainIsRealFG = false;

                            LOG_WARN("Dx11wDx12 FG swapchain creation failed: {:X}; creating plain DX12 swapchain",
                                     (UINT) fgScResult);

                            ScopedSkipParentWrapping skipParentWrapping {};
                            fgScResult = realFactory->CreateSwapChain(dx12Queue, &fgDesc, &fgSwapChain);
                        }

                        if (SUCCEEDED(fgScResult) && fgSwapChain != nullptr)
                            fgSwapChain->QueryInterface(IID_PPV_ARGS(&fgSwapChain4));
                    }

                    if (SUCCEEDED(realScResult) && realDx11SwapChain != nullptr && fgSwapChain4 != nullptr)
                    {
                        State::Instance().currentSwapchainDesc = localDesc;
                        State::Instance().currentRealSwapchain = realDx11SwapChain;
                        State::Instance().currentD3D11Device = device;
                        State::Instance().currentD3D12Device = WithDx12::GetD3D12Device();
                        State::Instance().currentCommandQueue = WithDx12::GetD3D12CommandQueue();
                        State::Instance().swapchainInteropApi = SwapchainInteropApi::Dx11wDx12;

                        if (!fgSwapChainIsRealFG)
                            FGHooks::SetDx12InteropPresentSC(fgSwapChain4, localDesc.OutputWindow);

                        *ppSwapChain = new Dx11wDx12SC(realDx11SwapChain, fgSwapChain4, device, localDesc.OutputWindow,
                                                       localDesc.Flags);
                        State::Instance().currentSwapchain = *ppSwapChain;
                        State::Instance().currentWrappedSwapchain = *ppSwapChain;

                        LOG_INFO("Created Dx11wDx12SC: wrapper {:X}, real11 {:X}, fg12 {:X}", (size_t) *ppSwapChain,
                                 (size_t) realDx11SwapChain, (size_t) fgSwapChain4);

                        realDx11SwapChain->Release();
                        fgSwapChain4->Release();
                        if (fgSwapChain != nullptr)
                            fgSwapChain->Release();
                        device->Release();
                        return S_OK;
                    }

                    LOG_WARN("Dx11wDx12 swapchain creation failed: real {:X}, fg {:X}", (UINT) realScResult,
                             (UINT) fgScResult);

                    if (realDx11SwapChain != nullptr)
                        realDx11SwapChain->Release();
                    if (fgSwapChain4 != nullptr)
                        fgSwapChain4->Release();
                    if (fgSwapChain != nullptr)
                        fgSwapChain->Release();
                }
            }

            device->Release();
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
            result = realFactory->CreateSwapChain(pDevice, &localDesc, ppSwapChain);
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
            State::Instance().swapchainInteropApi = SwapchainInteropApi::None;

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

            WrappedIDXGISwapChain4* wrapped;
            if ((*ppSwapChain)->QueryInterface(IID_PPV_ARGS(&wrapped)) != S_OK)
            {
                *ppSwapChain =
                    new WrappedIDXGISwapChain4(realSC, realDevice, localDesc.OutputWindow, localDesc.Flags, false);

                // Set as currentSwapchain is FG is disabled
                if (!_skipFGSwapChainCreation)
                    State::Instance().currentSwapchain = *ppSwapChain;

                State::Instance().currentWrappedSwapchain = *ppSwapChain;

                LOG_DEBUG("Created new WrappedIDXGISwapChain4: {:X}, pDevice: {:X}", (size_t) *ppSwapChain,
                          (size_t) pDevice);
            }
            else
            {
                wrapped->Release();
            }
        }
    }
    else
    {
        LOG_ERROR("CreateSwapChain failed: {:X}", (UINT) result);
    }

    return result;
}

HRESULT DxgiFactoryWrappedCalls::CreateSwapChainForHwnd(IDXGIFactory2* realFactory,
                                                        WrappedIDXGIFactory7* wrappedFactory, IUnknown* pDevice,
                                                        HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                                        IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)
{
    *ppSwapChain = nullptr;

    DXGI_SWAP_CHAIN_DESC1 localDesc = {};

    if (pDesc != nullptr)
        memcpy(&localDesc, pDesc, sizeof(DXGI_SWAP_CHAIN_DESC1));

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

            result = realFactory->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput,
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
            result = realFactory->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput,
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
            result = realFactory->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput,
                                                         ppSwapChain);
        }

        if (firstCall)
            _skipFGSwapChainCreation = false;

        return result;
    }

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
        if (State::Instance().currentD3D12Device == nullptr)
        {
            ID3D12Device* device = nullptr;
            if (cq->GetDevice(IID_PPV_ARGS(&device)) == S_OK)
            {
                if (device != nullptr)
                {
                    // Update current D3D12 device
                    if (State::Instance().currentD3D12Device != device)
                    {
                        State::Instance().currentD3D12Device = device;
                    }

                    LOG_INFO("Captured D3D12 device from command queue: {:X}", (UINT64) device);
                    D3D12Hooks::HookDevice(State::Instance().currentD3D12Device);
                }
            }
        }

        if (!Util::CheckForRealObject(__FUNCTION__, cq, &real))
            real = cq;

        State::Instance().currentCommandQueue = (ID3D12CommandQueue*) real;

        if (State::Instance().currentD3D12Device != nullptr)
        {
            WithDx12::SetD3D12Objects(State::Instance().currentD3D12Device, State::Instance().currentCommandQueue,
                                      D3D12_COMMAND_LIST_TYPE_DIRECT);
        }

        // Create FG SwapChain
        if (!_skipFGSwapChainCreation)
        {
            ScopedSkipFGSCCreation skipFGSCCreation {};
            FGSCResult = FGHooks::CreateSwapChainForHwnd(wrappedFactory, real, hWnd, &localDesc,
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
            State::Instance().currentD3D11Device = device;

            if (!_skipFGSwapChainCreation && ShouldCreateDx11wDx12Swapchain())
            {
                auto hiddenHwnd = CreateHiddenSwapchainWindow();

                ID3D12Device* dx12Device = nullptr;
                ID3D12CommandQueue* dx12Queue = nullptr;

                if (WithDx12::PrepareD3D12ForD3D11(device, D3D_FEATURE_LEVEL_11_0))
                {
                    dx12Device = WithDx12::GetD3D12Device();
                    dx12Queue = WithDx12::GetD3D12CommandQueue();
                }

                if (hiddenHwnd != nullptr && dx12Device != nullptr && dx12Queue != nullptr)
                {
                    DXGI_SWAP_CHAIN_DESC1 realDesc = localDesc;
                    IDXGISwapChain1* realDx11SwapChain1 = nullptr;
                    HRESULT realScResult = E_FAIL;
                    {
                        ScopedSkipParentWrapping skipParentWrapping {};
                        realScResult = realFactory->CreateSwapChainForHwnd(pDevice, hiddenHwnd, &realDesc, nullptr,
                                                                           pRestrictToOutput, &realDx11SwapChain1);
                    }

                    DXGI_SWAP_CHAIN_DESC1 fgDesc = localDesc;
                    HRESULT fgScResult = E_FAIL;
                    IDXGISwapChain1* fgSwapChain1 = nullptr;
                    IDXGISwapChain4* fgSwapChain4 = nullptr;
                    bool fgSwapChainIsRealFG = false;

                    if (SUCCEEDED(realScResult) && PrepareDx12InteropDesc1(fgDesc))
                    {
                        {
                            ScopedSkipFGSCCreation skipFGSCCreation {};
                            fgScResult = FGHooks::CreateSwapChainForHwnd(
                                wrappedFactory, dx12Queue, hWnd, &fgDesc,
                                pFullscreenDesc != nullptr ? &localFullscreenDesc : nullptr, pRestrictToOutput,
                                &fgSwapChain1);
                            fgSwapChainIsRealFG = SUCCEEDED(fgScResult) && fgSwapChain1 != nullptr;
                        }

                        if (FAILED(fgScResult) || fgSwapChain1 == nullptr)
                        {
                            fgSwapChainIsRealFG = false;

                            LOG_WARN("Dx11wDx12 FG swapchain creation failed: {:X}; creating plain DX12 swapchain",
                                     (UINT) fgScResult);

                            ScopedSkipParentWrapping skipParentWrapping {};
                            fgScResult = realFactory->CreateSwapChainForHwnd(
                                dx12Queue, hWnd, &fgDesc, pFullscreenDesc != nullptr ? &localFullscreenDesc : nullptr,
                                pRestrictToOutput, &fgSwapChain1);
                        }

                        if (SUCCEEDED(fgScResult) && fgSwapChain1 != nullptr)
                            fgSwapChain1->QueryInterface(IID_PPV_ARGS(&fgSwapChain4));
                    }

                    if (SUCCEEDED(realScResult) && realDx11SwapChain1 != nullptr && fgSwapChain4 != nullptr)
                    {
                        ((IDXGISwapChain*) realDx11SwapChain1)->GetDesc(&State::Instance().currentSwapchainDesc);
                        State::Instance().currentSwapchainDesc.OutputWindow = hWnd;
                        State::Instance().currentRealSwapchain = realDx11SwapChain1;
                        State::Instance().currentD3D11Device = device;
                        State::Instance().currentD3D12Device = WithDx12::GetD3D12Device();
                        State::Instance().currentCommandQueue = WithDx12::GetD3D12CommandQueue();
                        State::Instance().swapchainInteropApi = SwapchainInteropApi::Dx11wDx12;

                        if (!fgSwapChainIsRealFG)
                            FGHooks::SetDx12InteropPresentSC((IDXGISwapChain*) fgSwapChain4, hWnd);

                        *ppSwapChain = (IDXGISwapChain1*) new Dx11wDx12SC(realDx11SwapChain1, fgSwapChain4, device,
                                                                          hWnd, localDesc.Flags);
                        State::Instance().currentSwapchain = *ppSwapChain;
                        State::Instance().currentWrappedSwapchain = *ppSwapChain;

                        LOG_INFO("Created Dx11wDx12SC HWND: wrapper {:X}, real11 {:X}, fg12 {:X}",
                                 (size_t) *ppSwapChain, (size_t) realDx11SwapChain1, (size_t) fgSwapChain4);

                        realDx11SwapChain1->Release();
                        fgSwapChain4->Release();
                        if (fgSwapChain1 != nullptr)
                            fgSwapChain1->Release();
                        device->Release();
                        return S_OK;
                    }

                    LOG_WARN("Dx11wDx12 HWND swapchain creation failed: real {:X}, fg {:X}", (UINT) realScResult,
                             (UINT) fgScResult);

                    if (realDx11SwapChain1 != nullptr)
                        realDx11SwapChain1->Release();
                    if (fgSwapChain4 != nullptr)
                        fgSwapChain4->Release();
                    if (fgSwapChain1 != nullptr)
                        fgSwapChain1->Release();
                }
            }

            // Legacy DX11 FG path intentionally removed.
            // DX11 FG must now go through Dx11wDx12SC; if interop creation failed, fall back to the normal wrapper
            // path below.

            device->Release();
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
            result = realFactory->CreateSwapChainForHwnd(pDevice, hWnd, &localDesc,
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
            State::Instance().swapchainInteropApi = SwapchainInteropApi::None;

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

            WrappedIDXGISwapChain4* wrapped;
            if ((*ppSwapChain)->QueryInterface(IID_PPV_ARGS(&wrapped)) != S_OK)
            {
                *ppSwapChain = new WrappedIDXGISwapChain4(realSC, readDevice, hWnd, localDesc.Flags, false);

                LOG_DEBUG("Created new WrappedIDXGISwapChain4: {0:X}, pDevice: {1:X}", (uintptr_t) *ppSwapChain,
                          (uintptr_t) pDevice);

                if (!_skipFGSwapChainCreation)
                    State::Instance().currentSwapchain = *ppSwapChain;

                State::Instance().currentWrappedSwapchain = *ppSwapChain;
            }
            else
            {
                wrapped->Release();
            }
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

HRESULT DxgiFactoryWrappedCalls::CreateSwapChainForCoreWindow(IDXGIFactory2* realFactory, IUnknown* pDevice,
                                                              IUnknown* pWindow, const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                              IDXGIOutput* pRestrictToOutput,
                                                              IDXGISwapChain1** ppSwapChain)
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

    LOG_DEBUG("Width: {}, Height: {}, Format: {}, Count: {}, SkipWrapping: {}", pDesc->Width, pDesc->Height,
              (UINT) pDesc->Format, pDesc->BufferCount, _skipFGSwapChainCreation);

    DXGI_SWAP_CHAIN_DESC1 localDesc {};
    memcpy(&localDesc, pDesc, sizeof(DXGI_SWAP_CHAIN_DESC1));

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

    ID3D12CommandQueue* realQ = nullptr;
    if (!Util::CheckForRealObject(__FUNCTION__, pDevice, (IUnknown**) &realQ))
        realQ = (ID3D12CommandQueue*) pDevice;

    State::Instance().currentCommandQueue = realQ;

    State::Instance().skipDxgiLoadChecks = true;
    auto result =
        realFactory->CreateSwapChainForCoreWindow(pDevice, pWindow, &localDesc, pRestrictToOutput, ppSwapChain);
    State::Instance().skipDxgiLoadChecks = false;

    if (result == S_OK)
    {
        // check for SL proxy
        IDXGISwapChain* realSC = nullptr;
        if (!Util::CheckForRealObject(__FUNCTION__, *ppSwapChain, (IUnknown**) &realSC))
            realSC = *ppSwapChain;

        State::Instance().currentRealSwapchain = realSC;
        realSC->GetDesc(&State::Instance().currentSwapchainDesc);

        IUnknown* readDevice = nullptr;
        if (!Util::CheckForRealObject(__FUNCTION__, pDevice, (IUnknown**) &readDevice))
            readDevice = pDevice;

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

HRESULT DxgiFactoryWrappedCalls::EnumAdapters(IDXGIFactory* realFactory, UINT Adapter, IDXGIAdapter** ppAdapter)
{
    HRESULT result = S_FALSE;

    if (Config::Instance()->PreferFirstDedicatedGpu.value_or_default() && Adapter > 0)
    {
        LOG_DEBUG("{}, returning not found", Adapter);
        return DXGI_ERROR_NOT_FOUND;
    }

    IDXGIFactory6* factory6 = nullptr;
    if (realFactory->QueryInterface(IID_PPV_ARGS(&factory6)) == S_OK && factory6 != nullptr)
    {
        auto allGpus = IdentifyGpu::getAllGpus();
        if (Adapter < allGpus.size())
        {
            LOG_DEBUG("Trying to select: {}", allGpus[Adapter].name);

            auto gpuLuid = allGpus[Adapter].luid;

            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
            result = factory6->EnumAdapterByLuid(gpuLuid, IID_PPV_ARGS(ppAdapter));
        }
        else
            result = DXGI_ERROR_NOT_FOUND;

        factory6->Release();
    }

    if (result != S_OK && result != DXGI_ERROR_NOT_FOUND)
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        result = realFactory->EnumAdapters(Adapter, ppAdapter);
    }

    if (result == S_OK)
        DxgiSpoofing::AttachToAdapter(*ppAdapter);

#if _DEBUG
    LOG_TRACE("result: {:X}, Adapter: {}, pAdapter: {:X}", (UINT) result, Adapter, (uintptr_t) *ppAdapter);
#endif

    return result;
}

HRESULT DxgiFactoryWrappedCalls::EnumAdapters1(IDXGIFactory1* realFactory, UINT Adapter, IDXGIAdapter1** ppAdapter)
{
    HRESULT result = S_FALSE;

    if (Config::Instance()->PreferFirstDedicatedGpu.value_or_default() && Adapter > 0)
    {
        LOG_DEBUG("{}, returning not found", Adapter);
        return DXGI_ERROR_NOT_FOUND;
    }

    IDXGIFactory6* factory6 = nullptr;
    if (realFactory->QueryInterface(IID_PPV_ARGS(&factory6)) == S_OK && factory6 != nullptr)
    {
        auto allGpus = IdentifyGpu::getAllGpus();
        if (Adapter < allGpus.size())
        {
            LOG_DEBUG("Trying to select: {}", allGpus[Adapter].name);

            auto gpuLuid = allGpus[Adapter].luid;

            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
            result = factory6->EnumAdapterByLuid(gpuLuid, IID_PPV_ARGS(ppAdapter));
        }
        else
            result = DXGI_ERROR_NOT_FOUND;

        factory6->Release();
    }

    if (result != S_OK && result != DXGI_ERROR_NOT_FOUND)
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        result = realFactory->EnumAdapters1(Adapter, ppAdapter);
    }

    if (result == S_OK)
        DxgiSpoofing::AttachToAdapter(*ppAdapter);

#if _DEBUG
    LOG_TRACE("result: {:X}, Adapter: {}, pAdapter: {:X}", (UINT) result, Adapter, (uintptr_t) *ppAdapter);
#endif

    return result;
}

HRESULT DxgiFactoryWrappedCalls::EnumAdapterByLuid(IDXGIFactory4* realFactory, LUID AdapterLuid, REFIID riid,
                                                   void** ppvAdapter)
{
    HRESULT result;

    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        result = realFactory->EnumAdapterByLuid(AdapterLuid, riid, ppvAdapter);
    }

    if (result == S_OK)
        DxgiSpoofing::AttachToAdapter((IUnknown*) *ppvAdapter);

#if _DEBUG
    LOG_TRACE("result: {:X}, pAdapter: {:X}", (UINT) result, (uintptr_t) *ppvAdapter);
#endif

    return result;
}

HRESULT DxgiFactoryWrappedCalls::EnumAdapterByGpuPreference(IDXGIFactory6* realFactory, UINT Adapter,
                                                            DXGI_GPU_PREFERENCE GpuPreference, REFIID riid,
                                                            void** ppvAdapter)
{
    HRESULT result;

    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
        result = realFactory->EnumAdapterByGpuPreference(Adapter, GpuPreference, riid, ppvAdapter);
    }

    if (result == S_OK)
        DxgiSpoofing::AttachToAdapter((IUnknown*) *ppvAdapter);

#if _DEBUG
    LOG_TRACE("result: {:X}, Adapter: {}, pAdapter: {:X}", (UINT) result, Adapter, (uintptr_t) *ppvAdapter);
#endif

    return result;
}
