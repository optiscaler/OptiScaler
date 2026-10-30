#include "pch.h"
#include "FG_Hooks.h"
#include <Config.h>

#include <framegen/ffx/FSRFG_Dx12.h>
#include <framegen/xefg/XeFG_Dx12.h>

#include <inputs/FG/FSR3_Dx12_FG.h>
#include <inputs/FG/FfxApi_Dx12_FG.h>

#include <hudfix/Hudfix_Dx12.h>
#include <resource_tracking/ResTrack_Dx12.h>

#include <misc/FrameLimit.h>
#include <menu/menu_overlay_dx.h>
#include <upscaler_time/UpscalerTime_Dx12.h>

#include <detours/detours.h>

#include <d3d12.h>

#include <optional>

#define XEFG_RESOURCE_REF_LIMIT 1

static ID3D12Fence* resizeFence = nullptr;
static UINT64 resizeFenceValue = 0;
static HANDLE resizeFenceEvent = nullptr;
static IUnknown* oldSwapChain = nullptr;
static ID3D12CommandQueue* currentCommandQueue = nullptr;
static bool _forcedHdrForXeFG = false;
static HANDLE _semaphore = nullptr;

#if (XEFG_RESOURCE_REF_LIMIT == 0)
inline static std::vector<void*> oldBackBuffers;
#endif

static void SetCurrentCommandQueue(ID3D12CommandQueue* cq)
{
    if (currentCommandQueue != nullptr)
        currentCommandQueue->Release();

    currentCommandQueue = cq;
}

// For FG swapchain  wrap another FG swapchain resize deadlock issue
static thread_local int resizeDepth = 0;

struct ResizeScope
{
    std::optional<OwnedLockGuard> lock;

    ResizeScope(OwnedMutex& mutex, uint32_t owner)
    {
        if (resizeDepth++ == 0)
            lock.emplace(mutex, owner);
    }

    ~ResizeScope()
    {
        lock.reset();
        resizeDepth--;
    }
};

static void PauseFG(IFGFeature_Dx12* fg)
{
    if (fg != nullptr && fg->IsActive())
    {
        State::Instance().FGchanged = true;
        fg->UpdateTarget();
        fg->Deactivate();
    }
}

static void WaitForGPUIdle()
{
    if (currentCommandQueue != nullptr && resizeFence != nullptr && resizeFenceEvent != nullptr)
    {
        LOG_DEBUG("Waiting for GPU to finish before resizing buffers");

        resizeFenceValue++;
        currentCommandQueue->Signal(resizeFence, resizeFenceValue);

        if (resizeFence->GetCompletedValue() < resizeFenceValue)
        {
            resizeFence->SetEventOnCompletion(resizeFenceValue, resizeFenceEvent);
            // Max 5 sec
            auto waitResult = WaitForSingleObject(resizeFenceEvent, 5000);
            LOG_DEBUG("WaitForSingleObject result: {:X}", waitResult);
        }
    }
}

static bool CheckForFGStatus()
{
    if (State::Instance().activeFgInput == FGInput::NoFG || State::Instance().activeFgInput == FGInput::Nukems)
        return false;

    // Disable FG if amd dll is not found
    if (State::Instance().activeFgOutput == FGOutput::FSRFG)
    {
        FfxApiProxy::InitFfxDx12();
        if (!FfxApiProxy::IsFGReady())
        {
            LOG_DEBUG("Can't init FfxApiProxy, disabling FGOutput");
            Config::Instance()->FGOutput.set_volatile_value(FGOutput::NoFG);
            State::Instance().activeFgOutput = Config::Instance()->FGOutput.value_or_default();
        }
    }
    else if (State::Instance().activeFgOutput == FGOutput::XeFG && !XeFGProxy::InitXeFG())
    {
        LOG_DEBUG("Can't init XeFGProxy, disabling FGOutput");
        Config::Instance()->FGOutput.set_volatile_value(FGOutput::NoFG);
        State::Instance().activeFgOutput = Config::Instance()->FGOutput.value_or_default();
    }

    if (State::Instance().activeFgOutput != FGOutput::FSRFG && State::Instance().activeFgOutput != FGOutput::XeFG)
    {
        LOG_WARN("FGOutput is not set to FSR-FG or XeFG");
        return false;
    }

    return true;
}

bool FGHooks::IsOtherWindowThanFG(HWND hwnd)
{
    return State::Instance().currentFGSwapchain != nullptr && _hwnd != nullptr && hwnd != _hwnd;
}

static bool IsOtherWindowWhileFGSwapchainExists(HWND fgHwnd, HWND hwnd)
{
    if (!FGHooks::IsOtherWindowThanFG(hwnd))
        return false;

    LOG_WARN("FG swapchain already exists for window {:X}, not using FG for window {:X}", (size_t) fgHwnd,
             (size_t) hwnd);

    return true;
}

HRESULT FGHooks::CreateSwapChain(IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                 IDXGISwapChain** ppSwapChain)
{
    if (!CheckForFGStatus())
    {
        LOG_WARN("Can't init FG Feature or invalid FGOutput setting!");
        return E_NOINTERFACE;
    }

    if (IsOtherWindowWhileFGSwapchainExists(_hwnd, pDesc->OutputWindow))
        return E_NOINTERFACE;

    // Check if it's Dx12
    ID3D12CommandQueue* cq = nullptr;
    if (pDevice->QueryInterface(IID_PPV_ARGS(&cq)) != S_OK)
    {
        LOG_ERROR("FG Feature requires D3D12 Command Queue!");
        return E_INVALIDARG;
    }

    SetCurrentCommandQueue(cq);

    if (State::Instance().currentFG == nullptr)
    {
        // FG Init
        if (State::Instance().activeFgOutput == FGOutput::FSRFG)
        {
            State::Instance().currentFG = new FSRFG_Dx12();
        }
        else if (State::Instance().activeFgOutput == FGOutput::XeFG)
        {
            State::Instance().currentFG = new XeFG_Dx12();
        }
    }

    // Create FG swapchain
    auto fg = State::Instance().currentFG;
    bool scResult = false;

    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = true;

        if (State::Instance().activeFgOutput == FGOutput::XeFG && !pDesc->Windowed)
            LOG_WARN("Using exclusive fullscreen with XeFG!!!");

        // These effects are not supported in DX12
        if (pDesc->SwapEffect == DXGI_SWAP_EFFECT_SEQUENTIAL)
        {
            LOG_WARN("DXGI_SWAP_EFFECT_SEQUENTIAL is not supported in DX12, changing to FLIP_SEQUENTIAL");
            pDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        }
        else if (pDesc->SwapEffect == DXGI_SWAP_EFFECT_DISCARD)
        {
            LOG_WARN("DXGI_SWAP_EFFECT_DISCARD is not supported in DX12, changing to FLIP_DISCARD");
            pDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        }

        scResult = fg->CreateSwapchain(pFactory, cq, pDesc, ppSwapChain, true);

        // Looks like game is creating new swapchain,
        // without releasing old one, be sure gpu is in idle state
        if (!Config::Instance()->FGPreserveSwapChain.value_or_default() &&
            State::Instance().currentFGSwapchain != nullptr)
        {
            LOG_WARN("Looks like game is creating new swapchain, without releasing old one!");

            WaitForGPUIdle();
            oldSwapChain = State::Instance().currentFGSwapchain;
        }

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = false;
    }

    if (scResult)
    {
        if (State::Instance().currentD3D12Device != nullptr)
        {
            if (resizeFence != nullptr)
            {
                resizeFence->Release();
                resizeFence = nullptr;
            }

            if (resizeFenceEvent != nullptr)
            {
                CloseHandle(resizeFenceEvent);
                resizeFenceEvent = nullptr;
            }

            State::Instance().currentD3D12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&resizeFence));
            resizeFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        }

        _hwnd = pDesc->OutputWindow;
        State::Instance().currentFGSwapchain = *ppSwapChain;

        HookFGSwapchain(*ppSwapChain);

        State::Instance().currentSwapchain = *ppSwapChain;

        return S_OK;
    }

    return E_INVALIDARG;
}

HRESULT FGHooks::CreateSwapChainForHwnd(IDXGIFactory* pFactory, IUnknown* pDevice, HWND hWnd,
                                        DXGI_SWAP_CHAIN_DESC1* pDesc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                        IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)
{
    if (!CheckForFGStatus())
    {
        LOG_WARN("Can't init FG Feature or invalid FGOutput setting!");
        return E_NOINTERFACE;
    }

    if (IsOtherWindowWhileFGSwapchainExists(_hwnd, hWnd))
        return E_NOINTERFACE;

    // Check if it's Dx12
    ID3D12CommandQueue* cq = nullptr;
    if (pDevice->QueryInterface(IID_PPV_ARGS(&cq)) != S_OK)
    {
        LOG_ERROR("FG Feature requires D3D12 Command Queue!");
        return E_INVALIDARG;
    }

    SetCurrentCommandQueue(cq);

    if (State::Instance().currentFG == nullptr)
    {
        // FG Init
        if (State::Instance().activeFgOutput == FGOutput::FSRFG)
        {
            State::Instance().currentFG = new FSRFG_Dx12();
        }
        else if (State::Instance().activeFgOutput == FGOutput::XeFG)
        {
            State::Instance().currentFG = new XeFG_Dx12();
        }
    }

    // Create FG swapchain
    auto fg = State::Instance().currentFG;
    bool scResult = false;
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = true;

        if (State::Instance().activeFgOutput == FGOutput::XeFG && pFullscreenDesc != nullptr &&
            !pFullscreenDesc->Windowed)
            LOG_WARN("Using exclusive fullscreen with XeFG!!!");

        // These effects are not supported in DX12
        if (pDesc->SwapEffect == DXGI_SWAP_EFFECT_SEQUENTIAL)
        {
            LOG_WARN("DXGI_SWAP_EFFECT_SEQUENTIAL is not supported in DX12, changing to FLIP_SEQUENTIAL");
            pDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        }
        else if (pDesc->SwapEffect == DXGI_SWAP_EFFECT_DISCARD)
        {
            LOG_WARN("DXGI_SWAP_EFFECT_DISCARD is not supported in DX12, changing to FLIP_DISCARD");
            pDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        }

        scResult = fg->CreateSwapchain1(pFactory, cq, hWnd, pDesc, pFullscreenDesc, ppSwapChain, true);

        // Looks like game is creating new swapchain,
        // without releasing old one, be sure gpu is in idle state
        if (!Config::Instance()->FGPreserveSwapChain.value_or_default() &&
            State::Instance().currentFGSwapchain != nullptr)
        {
            LOG_WARN("Looks like game is creating new swapchain, without releasing old one!");

            WaitForGPUIdle();
            oldSwapChain = State::Instance().currentFGSwapchain;
        }

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = false;
    }

    if (scResult)
    {
        if (State::Instance().currentD3D12Device != nullptr)
        {
            if (resizeFence != nullptr)
            {
                resizeFence->Release();
                resizeFence = nullptr;
            }

            if (resizeFenceEvent != nullptr)
            {
                CloseHandle(resizeFenceEvent);
                resizeFenceEvent = nullptr;
            }

            State::Instance().currentD3D12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&resizeFence));
            resizeFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        }

        _hwnd = hWnd;
        State::Instance().currentFGSwapchain = *ppSwapChain;

        HookFGSwapchain(*ppSwapChain);
        State::Instance().currentSwapchain = *ppSwapChain;

        return S_OK;
    }

    return E_INVALIDARG;
}

void FGHooks::HookFGSwapchain(IDXGISwapChain* pSwapChain)
{
    if (o_FGSCPresent != nullptr || pSwapChain == nullptr)
        return;

    void** pFactoryVTable = *reinterpret_cast<void***>(pSwapChain);

    o_FGRelease = (PFN_Release) pFactoryVTable[2];
    o_FGSCPresent = (PFN_Present) pFactoryVTable[8];
    o_FGSCSetFullscreenState = (PFN_SetFullscreenState) pFactoryVTable[10];
    o_FGSCGetFullscreenState = (PFN_GetFullscreenState) pFactoryVTable[11];
    o_FGSCResizeBuffers = (PFN_ResizeBuffers) pFactoryVTable[13];
    o_FGSCResizeTarget = (PFN_ResizeTarget) pFactoryVTable[14];
    o_FGSCGetFullscreenDesc = (PFN_GetFullscreenDesc) pFactoryVTable[19];
    o_FGSCPresent1 = (PFN_Present1) pFactoryVTable[22];
    o_FGSCGetFrameLatencyWaitableObject = (PFN_GetFrameLatencyWaitableObject) pFactoryVTable[33];
    o_FGSCResizeBuffers1 = (PFN_ResizeBuffers1) pFactoryVTable[39];

    if (o_FGSCPresent != nullptr)
    {
        LOG_INFO("Hooking FG SwapChain present");
        LOG_TRACE("FGRelease: {:X}", (size_t) o_FGRelease);
        LOG_TRACE("FGSCPresent: {:X}", (size_t) o_FGSCPresent);
        LOG_TRACE("FGSCSetFullscreenState: {:X}", (size_t) o_FGSCSetFullscreenState);
        LOG_TRACE("FGSCGetFullscreenState: {:X}", (size_t) o_FGSCGetFullscreenState);
        LOG_TRACE("FGSCResizeBuffers: {:X}", (size_t) o_FGSCResizeBuffers);
        LOG_TRACE("FGSCResizeTarget: {:X}", (size_t) o_FGSCResizeTarget);
        LOG_TRACE("FGSCGetFullscreenDesc: {:X}", (size_t) o_FGSCGetFullscreenDesc);
        LOG_TRACE("FGSCPresent1: {:X}", (size_t) o_FGSCPresent1);
        LOG_TRACE("FGSCResizeBuffers1: {:X}", (size_t) o_FGSCResizeBuffers1);
        LOG_TRACE("FGSCGetFrameLatencyWaitableObject: {:X}", (size_t) o_FGSCGetFrameLatencyWaitableObject);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_FGRelease, hkFGRelease);
        DetourAttach(&(PVOID&) o_FGSCPresent, hkFGPresent);
        DetourAttach(&(PVOID&) o_FGSCResizeTarget, hkResizeTarget);
        DetourAttach(&(PVOID&) o_FGSCResizeBuffers, hkResizeBuffers);
        DetourAttach(&(PVOID&) o_FGSCSetFullscreenState, hkSetFullscreenState);

        if (o_FGSCPresent1 != nullptr)
            DetourAttach(&(PVOID&) o_FGSCPresent1, hkFGPresent1);

        if (o_FGSCResizeBuffers1 != nullptr)
            DetourAttach(&(PVOID&) o_FGSCResizeBuffers1, hkResizeBuffers1);

        if (State::Instance().activeFgOutput == FGOutput::XeFG)
        {
            if (o_FGSCGetFullscreenState != nullptr)
                DetourAttach(&(PVOID&) o_FGSCGetFullscreenState, hkGetFullscreenState);

            if (o_FGSCGetFullscreenDesc != nullptr)
                DetourAttach(&(PVOID&) o_FGSCGetFullscreenDesc, hkGetFullscreenDesc);

            if ((Config::Instance()->SimulateWaitableObject.value_or_default() ||
                 (State::Instance().GameEngine == GameEngineType::Unity &&
                  State::Instance().activeFgOutput == FGOutput::XeFG)) &&
                o_FGSCGetFrameLatencyWaitableObject != nullptr)
            {
                DetourAttach(&(PVOID&) o_FGSCGetFrameLatencyWaitableObject, hkGetFrameLatencyWaitableObject);
            }
        }

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to attach detour: {:X}", detourResult);
            o_FGRelease = nullptr;
            o_FGSCPresent = nullptr;
            o_FGSCSetFullscreenState = nullptr;
            o_FGSCGetFullscreenState = nullptr;
            o_FGSCResizeBuffers = nullptr;
            o_FGSCResizeTarget = nullptr;
            o_FGSCGetFullscreenDesc = nullptr;
            o_FGSCPresent1 = nullptr;
            o_FGSCResizeBuffers1 = nullptr;
            o_FGSCGetFrameLatencyWaitableObject = nullptr;
        }
    }
}

HRESULT FGHooks::hkSetFullscreenState(IDXGISwapChain* This, BOOL Fullscreen, IDXGIOutput* pTarget)
{
    auto fg = State::Instance().currentFG;
    PauseFG(fg);

    bool modeChanged = false;
    bool orgFS = Fullscreen;
    if (Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        if (Fullscreen)
        {
            Fullscreen = false;

            if (!State::Instance().SCExclusiveFullscreen)
            {
                State::Instance().SCExclusiveFullscreen = true;
                modeChanged = true;
            }

            LOG_DEBUG("Prevented exclusive fullscreen");
        }
        else
        {
            if (State::Instance().SCExclusiveFullscreen)
            {
                modeChanged = true;
                State::Instance().SCExclusiveFullscreen = false;
            }
        }
    }

    State::Instance().realExclusiveFullscreen = Fullscreen;

    if (State::Instance().activeFgOutput == FGOutput::XeFG && Fullscreen)
        LOG_WARN("Using exclusive fullscreen with XeFG!!!");

    auto result = S_OK;

    if (!Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        result = o_FGSCSetFullscreenState(This, Fullscreen, pTarget);
        LOG_DEBUG("Fullscreen: {}, pTarget: {:X}, Result: {:X}", Fullscreen, (size_t) pTarget, (UINT) result);
    }

    if (result == S_OK && modeChanged)
    {
        LOG_DEBUG("Mode changed");

        DXGI_SWAP_CHAIN_DESC scDesc {};
        This->GetDesc(&scDesc);

        if (State::Instance().SCExclusiveFullscreen)
        {
            SetWindowLongPtr(_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowLongPtr(_hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);

            Util::MonitorInfo info;

            if (pTarget != nullptr)
                info = Util::GetMonitorInfoForOutput(pTarget);
            else
                info = Util::GetMonitorInfoForWindow(_hwnd);

            LOG_DEBUG("Overriding window size: {}x{}, and pos: {}x{} at monitor: {}", info.width, info.height, info.x,
                      info.y, wstring_to_string(info.name));

            SetWindowPos(_hwnd, HWND_TOP, info.x, info.y, info.width, info.height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        else
        {
            SetWindowLongPtr(_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            SetWindowLongPtr(_hwnd, GWL_EXSTYLE, WS_EX_OVERLAPPEDWINDOW);
            SetWindowPos(_hwnd, nullptr, 0, 0, scDesc.BufferDesc.Width, scDesc.BufferDesc.Height,
                         SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    return result;
}

HANDLE FGHooks::hkGetFrameLatencyWaitableObject(IDXGISwapChain2* This)
{
    if (State::Instance().activeFgOutput != FGOutput::XeFG)
        return o_FGSCGetFrameLatencyWaitableObject(This);

    if (_semaphore == nullptr)
    {
        _semaphore = CreateSemaphore(nullptr, 16, 16, nullptr);

        if (_semaphore == nullptr)
            return nullptr;
    }

    HANDLE duplicatedHandle = nullptr;

    if (!DuplicateHandle(GetCurrentProcess(), _semaphore, GetCurrentProcess(), &duplicatedHandle, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
    {
        return nullptr;
    }

    return duplicatedHandle;
}

HRESULT FGHooks::hkGetFullscreenDesc(IDXGISwapChain1* This, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc)
{
    auto result = o_FGSCGetFullscreenDesc(This, pDesc);

    if (result == S_OK && State::Instance().SCExclusiveFullscreen &&
        Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        pDesc->Windowed = false;
    }

    return result;
}

HRESULT FGHooks::hkGetFullscreenState(IDXGISwapChain* This, BOOL* pFullscreen, IDXGIOutput** ppTarget)
{
    auto result = o_FGSCGetFullscreenState(This, pFullscreen, ppTarget);

    if (result == S_OK && State::Instance().SCExclusiveFullscreen &&
        Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        *pFullscreen = true;
    }

    return result;
}

HRESULT FGHooks::hkResizeBuffers(IDXGISwapChain* This, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat,
                                 UINT SwapChainFlags)
{
    // Skip XeFG's internal call
    if (_skipResize)
    {
        LOG_DEBUG("XeFG call skipping");
        _skipResize = false;

        IDXGISwapChain* sc = nullptr;

        if (sc != nullptr)
        {
            auto result = sc->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
            LOG_DEBUG("XeFG internal ResizeBuffers result: {:X}", (UINT) result);
            return result;
        }

        return o_FGSCResizeBuffers(This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    auto fg = State::Instance().currentFG;

    ResizeScope lg(fg->Mutex, 6677);

    // Disable frame generation when resizing swapchain
    PauseFG(fg);

    // Wait for GPU to finish rendering before resizing buffers to prevent issues with unreleased backbuffers
    WaitForGPUIdle();

    // Prevent mode switch when using borderless workaround for XeFG
    if (State::Instance().activeFgOutput == FGOutput::XeFG)
    {
        if (Config::Instance()->FGXeFGForceBorderless.value_or_default())
        {
            SwapChainFlags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        }

        if (State::Instance().SCLastFlags != SwapChainFlags)
        {
            LOG_WARN("SwapChainFlags changed from {:X} to {:X}", State::Instance().SCLastFlags, SwapChainFlags);

            if (State::Instance().activeFgOutput == FGOutput::XeFG)
            {
                LOG_WARN("Preventing flag change for XeFG!");
                SwapChainFlags = State::Instance().SCLastFlags;
            }
        }
    }

    LOG_DEBUG("BufferCount: {}, Width: {}, Height: {}, NewFormat:{}, SwapChainFlags: {:X}", BufferCount, Width, Height,
              (UINT) NewFormat, SwapChainFlags);

    // Skip resize checks for XeFG
    if (State::Instance().activeFgOutput == FGOutput::XeFG && !State::Instance().SCExclusiveFullscreen &&
        Config::Instance()->FGSkipResizeBuffers.value_or_default())
    {
        DXGI_SWAP_CHAIN_DESC desc {};
        if (This->GetDesc(&desc) == S_OK)
        {
            LOG_DEBUG("SC BufferCount: {}, Width: {}, Height: {}, NewFormat:{}, SwapChainFlags: {:X}", desc.BufferCount,
                      desc.BufferDesc.Width, desc.BufferDesc.Height, (UINT) desc.BufferDesc.Format,
                      State::Instance().SCLastFlags);

            if (BufferCount == 0)
                BufferCount = desc.BufferCount;

            if ((desc.BufferDesc.Width == Width || Width == 0) && (desc.BufferDesc.Height == Height || Height == 0) &&
                (NewFormat == desc.BufferDesc.Format || NewFormat == 0) &&
                State::Instance().SCLastFlags == SwapChainFlags &&
                (BufferCount == desc.BufferCount || BufferCount == 0))
            {
                LOG_DEBUG("Skipping resize");

                if (Config::Instance()->FGModifyBufferState.value_or_default() ||
                    Config::Instance()->FGModifySCIndex.value_or_default())
                {
                    auto swapchain = ((IDXGISwapChain3*) This);
                    auto swapchainIndex = swapchain->GetCurrentBackBufferIndex();

                    if (fg != nullptr && Config::Instance()->FGModifyBufferState.value_or_default())
                    {
                        LOG_INFO("Trying to change backbuffer state to COMMON");

                        auto cmdList = fg->GetUICommandList();

                        if (cmdList != nullptr)
                        {
                            for (size_t i = 0; i < desc.BufferCount; i++)
                            {
                                ID3D12Resource* backBuffer = nullptr;
                                if (swapchain->GetBuffer(swapchainIndex, IID_PPV_ARGS(&backBuffer)) == S_OK)
                                {
                                    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                                        backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);

                                    cmdList->ResourceBarrier(1, &barrier);

                                    backBuffer->Release();
                                }
                            }
                        }
                    }

                    if (swapchainIndex != 0 && Config ::Instance()->FGModifySCIndex.value_or_default())
                    {
                        auto presents = desc.BufferCount - swapchainIndex;

                        LOG_DEBUG("Trying to reset backbuffer index: {} with {} present calls", swapchainIndex,
                                  presents);

                        for (size_t i = 0; i < presents; i++)
                        {
                            swapchain->Present(0, 0);
                        }
                    }
                }

                return S_OK;
            }
        }

        SwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    _skipResize1 = true;
    State::Instance().FGResizing = true;

    State::Instance().SCAllowTearing = (SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;
    State::Instance().SCLastFlags = SwapChainFlags;

    HRESULT result = E_FAIL;

    // Release menu render targets
    if (Config::Instance()->OverlayMenu.value_or_default())
        MenuOverlayDx::CleanupRenderTarget(false, NULL);

    // Release swapchain backbuffers to prevent errors when resizing
    if (State::Instance().activeFgOutput == FGOutput::XeFG)
    {
        for (UINT i = 0; i < 8; i++)
        {
            ID3D12Resource* backBuffer = nullptr;
            auto bbResult = This->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

            if (bbResult == S_OK)
            {
                LOG_DEBUG("Backbuffer {}: {:X}", i, (size_t) backBuffer);
                auto refCount = backBuffer->Release();
                while (refCount > XEFG_RESOURCE_REF_LIMIT)
                {
                    LOG_DEBUG("Releasing backbuffer {}: RefCount {}", i, refCount);
                    refCount = backBuffer->Release();
                }

#if (XEFG_RESOURCE_REF_LIMIT == 0)
                oldBackBuffers.push_back(backBuffer);
#endif
            }
            else
            {
                LOG_DEBUG("GetBuffer failed for index {}: {:X}", i, (UINT) bbResult);
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Force HDR10 for XeFG if HDR16 is used
    if (State::Instance().activeFgOutput == FGOutput::XeFG && NewFormat >= DXGI_FORMAT_R16G16B16A16_TYPELESS &&
        NewFormat <= DXGI_FORMAT_R16G16B16A16_SINT && !Config::Instance()->ForceHDR.has_value())
    {
        if (!Config::Instance()->ForceHDR.has_value())
        {
            LOG_INFO("XeFG is active, forcing HDR10");
            Config::Instance()->ForceHDR.set_volatile_value(true);
            Config::Instance()->UseHDR10.set_volatile_value(true);
            Config::Instance()->SkipColorSpace.set_volatile_value(true);
            _forcedHdrForXeFG = true;
        }
    }
    else if (_forcedHdrForXeFG)
    {
        LOG_INFO("Disabling forced HDR10");
        Config::Instance()->ForceHDR.reset();
        Config::Instance()->UseHDR10.reset();
        Config::Instance()->SkipColorSpace.reset();
        _forcedHdrForXeFG = false;
    }

    // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
    // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
    if (Config::Instance()->ForceHDR.value_or_default())
    {
        LOG_INFO("Force HDR on");

        IDXGISwapChain3* _real3 = nullptr;
        if (This->QueryInterface(IID_PPV_ARGS(&_real3)) == S_OK)
        {
            do
            {
                NewFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
                DXGI_COLOR_SPACE_TYPE hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

                if (Config::Instance()->UseHDR10.value_or_default())
                {
                    NewFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
                    hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                }

                if (!Config::Instance()->SkipColorSpace.value_or_default())
                {
                    UINT css = 0;

                    auto result = _real3->CheckColorSpaceSupport(hdrCS, &css);

                    if (result != S_OK)
                    {
                        LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
                        break;
                    }

                    if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
                    {
                        result = _real3->SetColorSpace1(hdrCS);

                        if (result != S_OK)
                        {
                            LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
                            break;
                        }
                    }

                    LOG_INFO("HDR format and color space are set");
                }

            } while (false);

            _real3->Release();
        }
    }

    {
        ScopedSkipSpoofing skipSpoofing {};
        result = o_FGSCResizeBuffers(This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    LOG_DEBUG("Result: {:X}, Caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));

    // Resize window to cover the screen
    if (result == S_OK && Config::Instance()->FGXeFGForceBorderless.value_or_default() &&
        State::Instance().SCExclusiveFullscreen)
    {
        SetWindowLongPtr(_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongPtr(_hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);

        Util::MonitorInfo info;
        info = Util::GetMonitorInfoForWindow(_hwnd);

        LOG_DEBUG("Overriding window size: {}x{}, and pos: {}x{} at monitor: {}", info.width, info.height, info.x,
                  info.y, wstring_to_string(info.name));

        SetWindowPos(_hwnd, HWND_TOP, info.x, info.y, info.width, info.height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }

    State::Instance().FGResizing = false;
    _skipResize1 = false;

    return result;
}

HRESULT FGHooks::hkResizeTarget(IDXGISwapChain* This, const DXGI_MODE_DESC* pNewTargetParameters)
{
    if (Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        LOG_DEBUG("Skipping resize target.");
        return S_OK;
    }

    return o_FGSCResizeTarget(This, pNewTargetParameters);
}

HRESULT FGHooks::hkResizeBuffers1(IDXGISwapChain3* This, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format,
                                  UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue)
{
    // Skip XeFG's internal call
    if (_skipResize1)
    {
        LOG_DEBUG("XeFG call skipping");
        _skipResize1 = false;

        IDXGISwapChain3* sc = nullptr;

        if (sc != nullptr)
        {
            auto result = sc->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask,
                                             ppPresentQueue);

            LOG_DEBUG("XeFG internal ResizeBuffers1 result: {:X}", (UINT) result);
            return result;
        }

        return o_FGSCResizeBuffers1(This, BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask,
                                    ppPresentQueue);
    }

    auto fg = State::Instance().currentFG;

    ResizeScope lg(fg->Mutex, 6678);

    // Disable frame generation when resizing swapchain
    PauseFG(fg);

    // Wait for GPU to finish rendering before resizing buffers to prevent issues with unreleased backbuffers
    WaitForGPUIdle();

    // Prevent mode switch when using borderless workaround for XeFG
    if (State::Instance().activeFgOutput == FGOutput::XeFG)
    {
        if (Config::Instance()->FGXeFGForceBorderless.value_or_default())
        {
            SwapChainFlags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        }

        if (State::Instance().SCLastFlags != SwapChainFlags)
        {
            LOG_WARN("SwapChainFlags changed from {:X} to {:X}", State::Instance().SCLastFlags, SwapChainFlags);

            if (State::Instance().activeFgOutput == FGOutput::XeFG)
            {
                LOG_WARN("Preventing flag change for XeFG!");
                SwapChainFlags = State::Instance().SCLastFlags;
            }
        }
    }

    LOG_DEBUG("BufferCount: {}, Width: {}, Height: {}, NewFormat:{}, SwapChainFlags: {:X}, Caller: {}", BufferCount,
              Width, Height, (UINT) Format, SwapChainFlags, Util::WhoIsTheCaller(_ReturnAddress()));

    // Skip resize checks for XeFG
    if (State::Instance().activeFgOutput == FGOutput::XeFG && !State::Instance().SCExclusiveFullscreen &&
        Config::Instance()->FGSkipResizeBuffers.value_or_default())
    {
        DXGI_SWAP_CHAIN_DESC desc {};
        if (This->GetDesc(&desc) == S_OK)
        {
            LOG_DEBUG("SC BufferCount: {}, Width: {}, Height: {}, NewFormat:{}, SwapChainFlags: {:X}", desc.BufferCount,
                      desc.BufferDesc.Width, desc.BufferDesc.Height, (UINT) desc.BufferDesc.Format,
                      State::Instance().SCLastFlags);

            if (BufferCount == 0)
                BufferCount = desc.BufferCount;

            if ((desc.BufferDesc.Width == Width || Width == 0) && (desc.BufferDesc.Height == Height || Height == 0) &&
                (Format == desc.BufferDesc.Format || Format == 0) && State::Instance().SCLastFlags == SwapChainFlags &&
                (BufferCount == desc.BufferCount || BufferCount == 0))
            {
                LOG_DEBUG("Skipping resize");

                if (Config::Instance()->FGModifyBufferState.value_or_default() ||
                    Config::Instance()->FGModifySCIndex.value_or_default())
                {
                    auto swapchain = ((IDXGISwapChain3*) This);
                    auto swapchainIndex = swapchain->GetCurrentBackBufferIndex();

                    if (fg != nullptr && Config::Instance()->FGModifyBufferState.value_or_default())
                    {
                        LOG_INFO("Trying to change backbuffer state to COMMON");

                        auto cmdList = fg->GetUICommandList();

                        if (cmdList != nullptr)
                        {
                            for (size_t i = 0; i < desc.BufferCount; i++)
                            {
                                ID3D12Resource* backBuffer = nullptr;
                                if (swapchain->GetBuffer(swapchainIndex, IID_PPV_ARGS(&backBuffer)) == S_OK)
                                {
                                    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                                        backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);

                                    cmdList->ResourceBarrier(1, &barrier);

                                    backBuffer->Release();
                                }
                            }
                        }
                    }

                    if (swapchainIndex != 0 && Config ::Instance()->FGModifySCIndex.value_or_default())
                    {
                        auto presents = desc.BufferCount - swapchainIndex;

                        LOG_DEBUG("Trying to reset backbuffer index: {} with {} present calls", swapchainIndex,
                                  presents);

                        for (size_t i = 0; i < presents; i++)
                        {
                            swapchain->Present(0, 0);
                        }
                    }
                }

                return S_OK;
            }
        }

        SwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    _skipResize = true;
    State::Instance().FGResizing = true;

    State::Instance().SCAllowTearing = (SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;
    State::Instance().SCLastFlags = SwapChainFlags;

    HRESULT result;

    // Release menu render targets
    if (Config::Instance()->OverlayMenu.value_or_default())
        MenuOverlayDx::CleanupRenderTarget(false, NULL);

    // Release swapchain backbuffers to prevent errors when resizing
    if (State::Instance().activeFgOutput == FGOutput::XeFG)
    {
        for (UINT i = 0; i < 8; i++)
        {
            ID3D12Resource* backBuffer = nullptr;
            auto bbResult = This->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

            if (bbResult == S_OK)
            {
                LOG_DEBUG("Backbuffer {}: {:X}", i, (size_t) backBuffer);
                auto refCount = backBuffer->Release();
                while (refCount > XEFG_RESOURCE_REF_LIMIT)
                {
                    LOG_DEBUG("Releasing backbuffer {}: RefCount {}", i, refCount);
                    refCount = backBuffer->Release();
                }

#if (XEFG_RESOURCE_REF_LIMIT == 0)
                oldBackBuffers.push_back(backBuffer);
#endif
            }
            else
            {
                LOG_DEBUG("GetBuffer failed for index {}: {:X}", i, (UINT) bbResult);
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Force HDR10 for XeFG if HDR16 is used
    if (State::Instance().activeFgOutput == FGOutput::XeFG && Format >= DXGI_FORMAT_R16G16B16A16_TYPELESS &&
        Format <= DXGI_FORMAT_R16G16B16A16_SINT && !Config::Instance()->ForceHDR.has_value())
    {
        if (!Config::Instance()->ForceHDR.has_value())
        {
            LOG_INFO("XeFG is active, forcing HDR10");
            Config::Instance()->ForceHDR.set_volatile_value(true);
            Config::Instance()->UseHDR10.set_volatile_value(true);
            Config::Instance()->SkipColorSpace.set_volatile_value(true);
            _forcedHdrForXeFG = true;
        }
    }
    else if (_forcedHdrForXeFG)
    {
        LOG_INFO("Disabling forced HDR10");
        Config::Instance()->ForceHDR.reset();
        Config::Instance()->UseHDR10.reset();
        Config::Instance()->SkipColorSpace.reset();
        _forcedHdrForXeFG = false;
    }

    // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
    // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
    if (Config::Instance()->ForceHDR.value_or_default())
    {
        LOG_INFO("Force HDR on");

        IDXGISwapChain3* _real3 = nullptr;
        if (This->QueryInterface(IID_PPV_ARGS(&_real3)) == S_OK)
        {
            do
            {
                Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                DXGI_COLOR_SPACE_TYPE hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

                if (Config::Instance()->UseHDR10.value_or_default())
                {
                    Format = DXGI_FORMAT_R10G10B10A2_UNORM;
                    hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                }

                if (!Config::Instance()->SkipColorSpace.value_or_default())
                {
                    UINT css = 0;

                    auto result = _real3->CheckColorSpaceSupport(hdrCS, &css);

                    if (result != S_OK)
                    {
                        LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
                        break;
                    }

                    if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
                    {
                        result = _real3->SetColorSpace1(hdrCS);

                        if (result != S_OK)
                        {
                            LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
                            break;
                        }
                    }

                    LOG_INFO("HDR format and color space are set");
                }

            } while (false);

            _real3->Release();
        }
    }

    {
        ScopedSkipSpoofing skipSpoofing {};
        result = o_FGSCResizeBuffers1(This, BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask,
                                      ppPresentQueue);
    }

    LOG_DEBUG("Result: {:X}, Caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));

    // Resize window to cover the screen
    if (result == S_OK && Config::Instance()->FGXeFGForceBorderless.value_or_default() &&
        State::Instance().SCExclusiveFullscreen)
    {
        SetWindowLongPtr(_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongPtr(_hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);

        Util::MonitorInfo info;
        info = Util::GetMonitorInfoForWindow(_hwnd);

        LOG_DEBUG("Overriding window size: {}x{}, and pos: {}x{} at monitor: {}", info.width, info.height, info.x,
                  info.y, wstring_to_string(info.name));

        SetWindowPos(_hwnd, HWND_TOP, info.x, info.y, info.width, info.height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }

    State::Instance().FGResizing = false;
    _skipResize = false;

    return result;
}

HRESULT FGHooks::hkFGPresent(IDXGISwapChain* This, UINT SyncInterval, UINT Flags)
{
    // Skip XeFG's internal call
    if (_skipPresent)
    {
        LOG_DEBUG("XeFG call skipping");

        IDXGISwapChain* sc = nullptr;

        HRESULT result;

        if (sc != nullptr)
        {
            result = sc->Present(SyncInterval, Flags);
            LOG_DEBUG("sc->Present result: {:X}", (UINT) result);
        }
        else
        {
            result = o_FGSCPresent(This, SyncInterval, Flags);
            LOG_DEBUG("o_FGSCPresent result: {:X}", (UINT) result);
        }

        return result;
    }

    LOG_DEBUG("SyncInterval: {}, Flags: {:X}", SyncInterval, Flags);

    _skipPresent1 = true;
    auto result = FGPresent(This, SyncInterval, Flags, nullptr);
    _skipPresent1 = false;

    return result;
}

HRESULT FGHooks::hkFGPresent1(IDXGISwapChain1* This, UINT SyncInterval, UINT Flags,
                              const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    // Skip XeFG's internal call
    if (_skipPresent1)
    {
        LOG_DEBUG("XeFG call skipping");

        IDXGISwapChain3* sc = nullptr;

        HRESULT result;

        if (sc != nullptr)
        {
            result = sc->Present1(SyncInterval, Flags, pPresentParameters);
            LOG_DEBUG("sc->Present result: {:X}", (UINT) result);
        }
        else
        {
            result = o_FGSCPresent1(This, SyncInterval, Flags, pPresentParameters);
            LOG_DEBUG("o_FGSCPresent result: {:X}", (UINT) result);
        }

        return result;
    }

    LOG_DEBUG("SyncInterval: {}, Flags: {:X}", SyncInterval, Flags);
    _skipPresent = true;
    auto result = FGPresent(This, SyncInterval, Flags, pPresentParameters);
    _skipPresent = false;

    return result;
}

HRESULT FGHooks::FGPresent(IDXGISwapChain* This, UINT SyncInterval, UINT Flags,
                           const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    _lastPresentFlags = Flags;

    if (State::Instance().isShuttingDown)
    {
        if (pPresentParameters == nullptr)
            return o_FGSCPresent(This, SyncInterval, Flags);
        else
            return o_FGSCPresent1((IDXGISwapChain1*) This, SyncInterval, Flags, pPresentParameters);
    }

    auto willPresent = (Flags & DXGI_PRESENT_TEST) == 0;

    if (willPresent)
    {
        State::Instance().FGLastFrame++;

        double ftDelta = 0.0f;
        auto now = Util::MillisecondsNow();

        if (_lastFGFrameTime > 0.0)
            ftDelta = now - _lastFGFrameTime;

        _lastFGFrameTime = now;
        State::Instance().lastFGFrameTime = ftDelta;

        LOG_DEBUG("flags: {:X}, Frametime: {}", Flags, ftDelta);
    }

    // The referenced queue of the FG swapchain
    if (willPresent && currentCommandQueue != nullptr)
    {
        UpscalerTimeDx12::ReadUpscalingTime(currentCommandQueue);
    }

    auto fg = State::Instance().currentFG;
    bool mutexUsed = false;
    if (willPresent && fg != nullptr && fg->IsActive() && !fg->IsPaused() &&
        Config::Instance()->FGUseMutexForSwapchain.value_or_default() && fg->Mutex.getOwner() != 2)
    {
        LOG_TRACE("Waiting FG->Mutex 2, current: {}", fg->Mutex.getOwner());
        fg->Mutex.lock(2);
        mutexUsed = true;
        LOG_TRACE("Accuired FG->Mutex: {}", fg->Mutex.getOwner());
    }

    if (willPresent && fg != nullptr)
    {
        // Some games use this callback to render UI even when
        // FG is disabled. So call it when there is FGFeature
        if (State::Instance().activeFgInput == FGInput::FSRFG)
            ffxPresentCallback();
        else if (State::Instance().activeFgInput == FGInput::FSRFG30)
            FSR3FG::ffxPresentCallback();

        // And if Optiscalers FG is active call
        // FG Features present
        fg->Present();
    }

    if (willPresent)
    {
        ResTrack_Dx12::ClearPossibleHudless();
        Hudfix_Dx12::PresentStart();
    }

    if (willPresent && Config::Instance()->ForceVsync.has_value())
    {
        LOG_DEBUG("ForceVsync: {}, VsyncInterval: {}, SCAllowTearing: {}, realExclusiveFullscreen: {}",
                  Config::Instance()->ForceVsync.value(), Config::Instance()->VsyncInterval.value_or_default(),
                  State::Instance().SCAllowTearing, State::Instance().realExclusiveFullscreen);

        if (!Config::Instance()->ForceVsync.value())
        {
            SyncInterval = 0;

            if (State::Instance().SCAllowTearing && !State::Instance().realExclusiveFullscreen)
            {
                LOG_DEBUG("Adding DXGI_PRESENT_ALLOW_TEARING");
                Flags |= DXGI_PRESENT_ALLOW_TEARING;
            }
        }
        else
        {
            SyncInterval = Config::Instance()->VsyncInterval.value_or_default();

            if (SyncInterval < 1)
                SyncInterval = 1;

            LOG_DEBUG("Removing DXGI_PRESENT_ALLOW_TEARING");
            Flags &= ~DXGI_PRESENT_ALLOW_TEARING;
        }

        LOG_DEBUG("Final SyncInterval: {}", SyncInterval);
    }

    // Used at wrapped_swapchain LocalPresent to determine is frame is interpolated or not
    if (willPresent)
        State::Instance().FGPresentIsCalled = true;

    HRESULT result;
    if (pPresentParameters == nullptr)
        result = o_FGSCPresent(This, SyncInterval, Flags);
    else
        result = o_FGSCPresent1((IDXGISwapChain1*) This, SyncInterval, Flags, pPresentParameters);

    if (result == S_OK)
    {
        LOG_DEBUG("Result: {:X}", result);
    }
    else
    {
        if (result == DXGI_ERROR_DEVICE_REMOVED && State::Instance().currentD3D12Device != nullptr)
            Util::GetDeviceRemovedReason(State::Instance().currentD3D12Device);
    }

    Hudfix_Dx12::PresentEnd();

    if (willPresent && !State::Instance().reflexLimitsFps && State::Instance().activeFgOutput != FGOutput::NoFG &&
        !State::Instance().isRunningOnDXVK)
    {
        FrameLimit::sleep(fg != nullptr ? fg->IsActive() && !fg->IsPaused() : false);
    }

    if ((Config::Instance()->SimulateWaitableObject.value_or_default() ||
         (State::Instance().GameEngine == GameEngineType::Unity &&
          State::Instance().activeFgOutput == FGOutput::XeFG)) &&
        _semaphore != nullptr)
    {
        ReleaseSemaphore(_semaphore, 1, nullptr);
    }

    if (mutexUsed && fg != nullptr)
    {
        LOG_TRACE("Releasing FG->Mutex: {}", fg->Mutex.getOwner());
        fg->Mutex.unlockThis(2);
    }

    LOG_DEBUG("Present finished");

    return result;
}

ULONG FGHooks::hkFGRelease(IUnknown* This)
{
    // We already released this one, prevent crashes
    if (This == oldSwapChain)
    {
        LOG_DEBUG("Release called on old swapchain, skipping release and returning 0");
        return 0;
    }

#if (XEFG_RESOURCE_REF_LIMIT == 0)
    // find if this resource in oldBackBuffers, if it is, skip release and return 0
    if (oldBackBuffers.size() > 0)
    {
        for (auto it = oldBackBuffers.begin(); it != oldBackBuffers.end(); ++it)
        {
            if (*it == This)
            {
                LOG_DEBUG("Release called on old backbuffer, skipping release and returning 0");
                return 0;
            }
        }
    }
#endif // (XEFG_RESOURCE_REF_LIMIT == 0)

    static bool skipReleaseChecks = false;

    if (skipReleaseChecks || State::Instance().currentFGSwapchain != This || State::Instance().isShuttingDown)
        return o_FGRelease(This);

    This->AddRef();

    if (!Config::Instance()->FGPreserveSwapChain.value_or_default())
    {
        if (o_FGRelease(This) == 1)
        {
            LOG_DEBUG("");

            WaitForGPUIdle();

            DXGI_SWAP_CHAIN_DESC scDesc {};
            ((IDXGISwapChain*) This)->GetDesc(&scDesc);

            // Release swapchain backbuffers to prevent errors when releasing FG swapchain
            {
                for (UINT i = 0; i < scDesc.BufferCount; i++)
                {
                    ID3D12Resource* backBuffer = nullptr;
                    auto bbResult = ((IDXGISwapChain*) This)->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

                    if (bbResult == S_OK)
                    {
                        LOG_DEBUG("Backbuffer {}: {:X}", i, (size_t) backBuffer);
                        auto refCount = backBuffer->Release();
                        while (refCount > XEFG_RESOURCE_REF_LIMIT)
                        {
                            LOG_DEBUG("Releasing backbuffer {}: RefCount {}", i, refCount);
                            refCount = backBuffer->Release();
                        }

#if (XEFG_RESOURCE_REF_LIMIT == 0)
                        oldBackBuffers.push_back(backBuffer);
#endif
                    }
                    else
                    {
                        LOG_DEBUG("GetBuffer failed for index {}: {:X}", i, (UINT) bbResult);
                        break;
                    }
                }
            }

            // To prevent deadlock when FG release the swapchain
            skipReleaseChecks = true;

            if (State::Instance().currentFG != nullptr)
            {
                LOG_DEBUG("FG Swapchain released, release FG & swapchain context");
                State::Instance().currentFG->ReleaseSwapchain(_hwnd);
            }

            LOG_DEBUG("FG Swapchain released, clearing currentFGSwapchain");
            State::Instance().currentFGSwapchain = nullptr;

            if (State::Instance().currentWrappedSwapchain != nullptr &&
                State::Instance().currentSwapchainDesc.OutputWindow == _hwnd)
            {
                auto refCount = State::Instance().currentWrappedSwapchain->Release();

                while (refCount > 0 && refCount < 0xffffff00)
                {
                    refCount = State::Instance().currentWrappedSwapchain->Release();
                }

                State::Instance().currentWrappedSwapchain = nullptr;
            }

            skipReleaseChecks = false;

            return 0;
        }
    }
    else
    {
        if (o_FGRelease(This) == 1)
        {
            LOG_INFO("Preserving FG Swapchain from release");
            return 0;
        }
    }

    return o_FGRelease(This);
}
