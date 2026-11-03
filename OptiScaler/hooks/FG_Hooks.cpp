#include "pch.h"
#include "FG_Hooks.h"
#include <Config.h>

#include <framegen/ffx/FSRFG_Dx12.h>
#include <framegen/xefg/XeFG_Dx12.h>
#include <framegen/dlssg/DLSSG_Dx12.h>
#include <framegen/reprojection/Reprojection_Dx12.h>

#include <inputs/FG/FSR3_Dx12_FG.h>
#include <inputs/FG/FfxApi_Dx12_FG.h>

#include <hudfix/Hudfix_Dx12.h>
#include <resource_tracking/ResTrack_Dx12.h>

#include <misc/FrameLimit.h>

#include <misc/IdentifyGpu.h>
#include <hooks/Reflex_Hooks.h>
#include <low_latency/input/input_common.h>
#include <menu/menu_overlay_dx.h>
#include <imgui/ImGuiNotify.hpp>

#include <d3d12.h>
#include <detours/detours.h>

#define XEFG_RESOURCE_REF_LIMIT 1

static ID3D12Fence* resizeFence = nullptr;
static UINT64 resizeFenceValue = 0;
static HANDLE resizeFenceEvent = nullptr;

// Fence state is shared by every caller
static std::mutex resizeFenceMutex;
static IUnknown* oldSwapChain = nullptr;
static ID3D12CommandQueue* currentCommandQueue = nullptr;
static bool _forcedHdrForXeFG = false;
static HANDLE _semaphore = nullptr;

#if (XEFG_RESOURCE_REF_LIMIT == 0)
inline static std::vector<void*> oldBackBuffers;
#endif

// Keeps a reference to the FG swapchain's queue
static void SetCurrentCommandQueue(ID3D12CommandQueue* cq)
{
    if (currentCommandQueue != nullptr)
        currentCommandQueue->Release();

    currentCommandQueue = cq;
}

static bool WaitForQueueIdle(ID3D12CommandQueue* queue)
{
    std::scoped_lock lock(resizeFenceMutex);

    auto fence = resizeFence;
    auto fenceEvent = resizeFenceEvent;
    auto& fenceValue = resizeFenceValue;

    if (queue == nullptr || fence == nullptr || fenceEvent == nullptr)
        return true;

    const UINT64 waitValue = ++fenceValue;
    auto result = queue->Signal(fence, waitValue);
    if (FAILED(result))
    {
        LOG_ERROR("FG/present queue idle Signal failed: {:X}", (UINT) result);
        return false;
    }

    if (fence->GetCompletedValue() >= waitValue)
        return true;

    result = fence->SetEventOnCompletion(waitValue, fenceEvent);
    if (FAILED(result))
    {
        LOG_ERROR("FG/present queue idle SetEventOnCompletion failed. fence {}, completed {}, result {:X}", waitValue,
                  fence->GetCompletedValue(), (UINT) result);
        return false;
    }

    const auto waitResult = WaitForSingleObject(fenceEvent, 5000);
    if (waitResult != WAIT_OBJECT_0)
    {
        LOG_ERROR("FG/present queue idle wait failed. fence {}, completed {}, waitResult {:X}", waitValue,
                  fence->GetCompletedValue(), waitResult);
        return false;
    }

    return true;
}

// Nested FG swapchain resizes would deadlock on the FG mutex
static thread_local int resizeDepth = 0;

struct ResizeScope
{
    std::optional<OwnedLockGuard> lock;

    ResizeScope(IFGFeature_Dx12* fg, uint32_t owner)
    {
        // Owner 7 is a mode switch waiting for the window to handle this resize
        if (resizeDepth++ == 0 && fg != nullptr && Config::Instance()->FGUseMutexForSwapchain.value_or_default() &&
            !fg->Mutex.isLockedByThisThread() && !(fg->Mutex.getOwner() == 7 && InSendMessage()))
        {
            lock.emplace(fg->Mutex, owner);
        }
    }

    ~ResizeScope()
    {
        lock.reset();
        resizeDepth--;
    }
};

static bool CheckForFGStatus()
{
    // Need to check overlay menu parameter, goes to places it shouldn't go
    // if (!Config::Instance()->OverlayMenu.value_or_default())
    //    return false;

    if (State::Instance().activeFgInput == FGInput::NoFG || State::Instance().activeFgInput == FGInput::NvngxFG)
        return false;

    // Disable FG if amd dll is not found
    if (State::Instance().activeFgOutput == FGOutput::FSRFG)
    {
        FfxApiProxy::InitFfxDx12();
        if (!FfxApiProxy::IsFGReady())
        {
            ImGui::InsertNotification(
                { ImGuiToastType::Error, 20000, "Can't init FSR FG\nAre you missing the required DLLs?" });

            LOG_DEBUG("Can't init FfxApiProxy, disabling FGOutput");
            Config::Instance()->FGOutput.set_volatile_value(FGOutput::NoFG);
            State::Instance().activeFgOutput = Config::Instance()->FGOutput.value_or_default();
        }
    }
    else if (State::Instance().activeFgOutput == FGOutput::XeFG && !XeFGProxy::InitXeFG())
    {
        ImGui::InsertNotification(
            { ImGuiToastType::Error, 20000, "Can't init XeFG\nAre you missing the required DLLs?" });

        LOG_DEBUG("Can't init XeFGProxy, disabling FGOutput");
        Config::Instance()->FGOutput.set_volatile_value(FGOutput::NoFG);
        State::Instance().activeFgOutput = Config::Instance()->FGOutput.value_or_default();
    }
    else if (State::Instance().activeFgOutput == FGOutput::DLSSG && !StreamlineProxy::LoadStreamline())
    {
        ImGui::InsertNotification(
            { ImGuiToastType::Error, 20000, "Can't init DLSSG Output\nAre you missing the streamline folder?" });

        LOG_DEBUG("Can't init StreamlineProxy, disabling FGOutput");
        Config::Instance()->FGOutput.set_volatile_value(FGOutput::NoFG);
        State::Instance().activeFgOutput = Config::Instance()->FGOutput.value_or_default();

        Config::Instance()->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::None);
        State::Instance().activeFgNvngx = Config::Instance()->FGNvngxReplacement.value_or_default();
    }

    if (State::Instance().activeFgOutput == FGOutput::NoFG)
    {
        LOG_WARN("FGOutput doesn't have any FG active");
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

void FGHooks::ReleaseUnusableFGSwapchain(HWND hwnd, IUnknown* pDevice)
{
    auto swapchain = State::Instance().currentFGSwapchain;
    if (swapchain == nullptr || _releasedFGSwapchain != swapchain || pDevice == nullptr)
        return;

    ID3D12CommandQueue* cq = nullptr;
    if (pDevice->QueryInterface(IID_PPV_ARGS(&cq)) != S_OK)
        return;

    cq->Release();

    IUnknown* queue = nullptr;
    if (!Util::CheckForRealObject(__FUNCTION__, cq, &queue))
        queue = cq;

    if (hwnd == _hwnd && queue == currentCommandQueue)
        return;

    LOG_INFO("Releasing kept FG swapchain, window: {:X} -> {:X}, queue: {:X} -> {:X}", (size_t) _hwnd, (size_t) hwnd,
             (size_t) currentCommandQueue, (size_t) queue);

    // Other hooks can still hold its references
    ReleaseFGSwapchain(swapchain, false);
}

// XeFG doesn't support exclusive fullscreen, it's emulated with a borderless window
bool FGHooks::UseBorderless()
{
    return State::Instance().activeFgOutput == FGOutput::XeFG &&
           Config::Instance()->FGXeFGForceBorderless.value_or_default();
}

// Sends window messages, call it without holding the FG mutex
void FGHooks::ApplyBorderless(IDXGIOutput* target)
{
    if (_hwnd == nullptr)
        return;

    // Minimized window has no usable rect, done again on a later present
    if (IsIconic(_hwnd))
    {
        _borderlessPending = true;
        return;
    }

    Util::MonitorInfo info;

    if (target != nullptr)
        info = Util::GetMonitorInfoForOutput(target);
    else
        info = Util::GetMonitorInfoForWindow(_hwnd);

    const LONG_PTR style = WS_POPUP | WS_VISIBLE;
    const LONG_PTR exStyle = WS_EX_APPWINDOW;

    RECT rect {};
    GetWindowRect(_hwnd, &rect);

    // Changing it again sends WM_SIZE and games resize again
    if (GetWindowLongPtr(_hwnd, GWL_STYLE) == style && GetWindowLongPtr(_hwnd, GWL_EXSTYLE) == exStyle &&
        rect.left == info.x && rect.top == info.y && rect.right - rect.left == info.width &&
        rect.bottom - rect.top == info.height)
    {
        return;
    }

    if (!_windowSaved)
    {
        _savedStyle = GetWindowLongPtr(_hwnd, GWL_STYLE);
        _savedExStyle = GetWindowLongPtr(_hwnd, GWL_EXSTYLE);
        _savedRect = rect;
        _windowSaved = true;
    }

    LOG_DEBUG("Overriding window size: {}x{}, and pos: {}x{} at monitor: {}", info.width, info.height, info.x, info.y,
              wstring_to_string(info.name));

    SetWindowLongPtr(_hwnd, GWL_STYLE, style);
    SetWindowLongPtr(_hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(_hwnd, HWND_TOP, info.x, info.y, info.width, info.height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void FGHooks::RestoreWindow(UINT width, UINT height)
{
    if (_hwnd == nullptr)
        return;

    if (IsIconic(_hwnd))
    {
        _borderlessPending = true;
        return;
    }

    if (_windowSaved)
    {
        SetWindowLongPtr(_hwnd, GWL_STYLE, _savedStyle);
        SetWindowLongPtr(_hwnd, GWL_EXSTYLE, _savedExStyle);
        SetWindowPos(_hwnd, nullptr, _savedRect.left, _savedRect.top, _savedRect.right - _savedRect.left,
                     _savedRect.bottom - _savedRect.top, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        _windowSaved = false;
        return;
    }

    // Client area has the swapchain's size
    const DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    RECT rect { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    AdjustWindowRectEx(&rect, style, FALSE, WS_EX_OVERLAPPEDWINDOW);

    SetWindowLongPtr(_hwnd, GWL_STYLE, style);
    SetWindowLongPtr(_hwnd, GWL_EXSTYLE, WS_EX_OVERLAPPEDWINDOW);
    SetWindowPos(_hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
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
        else if (State::Instance().activeFgOutput == FGOutput::DLSSG)
        {
            State::Instance().currentFG = new DLSSG_Dx12();
        }
        else if (State::Instance().activeFgOutput == FGOutput::Reprojection)
        {
            State::Instance().currentFG = new Reprojection_Dx12();
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

        // Reusing failed, the kept swapchain still owns the window
        if (!scResult && _releasedFGSwapchain != nullptr &&
            _releasedFGSwapchain == State::Instance().currentFGSwapchain)
        {
            ReleaseFGSwapchain(State::Instance().currentFGSwapchain, false);

            if (State::Instance().currentFGSwapchain == nullptr)
                scResult = fg->CreateSwapchain(pFactory, cq, pDesc, ppSwapChain, true);
        }

        // Looks like game is creating new swapchain,
        // without releasing old one, be sure gpu is in idle state
        if (scResult && !Config::Instance()->FGPreserveSwapChain.value_or_default() &&
            State::Instance().currentFGSwapchain != nullptr)
        {
            LOG_WARN("Looks like game is creating new swapchain, without releasing old one!");

            if (State::Instance().currentCommandQueue != nullptr && resizeFence != nullptr &&
                resizeFenceEvent != nullptr)
            {
                LOG_DEBUG("Waiting for GPU to finish");

                const auto waitResult = WaitForQueueIdle(State::Instance().currentCommandQueue);

                LOG_DEBUG("WaitForSingleObject result: {}", waitResult);
            }

            oldSwapChain = State::Instance().currentFGSwapchain;
        }

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = false;
    }

    if (scResult)
    {
        if (State::Instance().currentD3D12Device != nullptr)
        {
            std::scoped_lock lock(resizeFenceMutex);

            SAFE_RELEASE(resizeFence);
            SAFE_CLOSE_HANDLE(resizeFenceEvent);

            State::Instance().currentD3D12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&resizeFence));
            resizeFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        }

        SetCurrentCommandQueue(cq);
        SetFGSwapchain(*ppSwapChain, pDesc->OutputWindow);

        return S_OK;
    }

    cq->Release();
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
        else if (State::Instance().activeFgOutput == FGOutput::DLSSG)
        {
            State::Instance().currentFG = new DLSSG_Dx12();
        }
        else if (State::Instance().activeFgOutput == FGOutput::Reprojection)
        {
            State::Instance().currentFG = new Reprojection_Dx12();
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

        // Reusing failed, the kept swapchain still owns the window
        if (!scResult && _releasedFGSwapchain != nullptr &&
            _releasedFGSwapchain == State::Instance().currentFGSwapchain)
        {
            ReleaseFGSwapchain(State::Instance().currentFGSwapchain, false);

            if (State::Instance().currentFGSwapchain == nullptr)
                scResult = fg->CreateSwapchain1(pFactory, cq, hWnd, pDesc, pFullscreenDesc, ppSwapChain, true);
        }

        // Looks like game is creating new swapchain,
        // without releasing old one, be sure gpu is in idle state
        if (scResult && !Config::Instance()->FGPreserveSwapChain.value_or_default() &&
            State::Instance().currentFGSwapchain != nullptr)
        {
            LOG_WARN("Looks like game is creating new swapchain, without releasing old one!");

            if (State::Instance().currentCommandQueue != nullptr && resizeFence != nullptr &&
                resizeFenceEvent != nullptr)
            {
                LOG_DEBUG("Waiting for GPU to finish");

                const auto waitResult = WaitForQueueIdle(State::Instance().currentCommandQueue);

                LOG_DEBUG("WaitForSingleObject result: {}", waitResult);
            }

            oldSwapChain = State::Instance().currentFGSwapchain;
        }

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = false;
    }

    if (scResult)
    {
        if (State::Instance().currentD3D12Device != nullptr)
        {
            std::scoped_lock lock(resizeFenceMutex);

            SAFE_RELEASE(resizeFence);
            SAFE_CLOSE_HANDLE(resizeFenceEvent);

            State::Instance().currentD3D12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&resizeFence));
            resizeFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        }

        SetCurrentCommandQueue(cq);
        SetFGSwapchain((IDXGISwapChain*) *ppSwapChain, hWnd);

        return S_OK;
    }

    cq->Release();
    return E_INVALIDARG;
}

void FGHooks::SetFGSwapchain(IDXGISwapChain* pSwapChain, HWND hWnd)
{
    if (pSwapChain == nullptr)
        return;

    // References the FG library keeps, the game's last release leaves only these
    if (pSwapChain != State::Instance().currentFGSwapchain)
    {
        pSwapChain->AddRef();
        _fgSwapchainBaseRefs = pSwapChain->Release() - 1;
    }

    if (_hwnd != hWnd)
        _windowSaved = false;

    _hwnd = hWnd;
    _releasedFGSwapchain = nullptr;
    State::Instance().SCExclusiveFullscreen = UseBorderless() && State::Instance().SCExclusiveFullscreenRequested;
    _borderlessPending = UseBorderless();

    if (_dx12InteropPresentSC == pSwapChain)
    {
        _dx12InteropPresentSC = nullptr;
        _dx12InteropPresentHwnd = nullptr;
    }

    State::Instance().currentFGSwapchain = pSwapChain;
    State::Instance().currentSwapchain = pSwapChain;

    if (oldSwapChain == pSwapChain)
        oldSwapChain = nullptr;

    HookFGSwapchain(pSwapChain);
}

void FGHooks::SetDx12InteropPresentSC(IDXGISwapChain* pSwapChain, HWND hWnd)
{
    if (pSwapChain == nullptr)
        return;

    _dx12InteropPresentSC = pSwapChain;
    _dx12InteropPresentHwnd = hWnd;

    LOG_INFO("Set DX12 presenting swapchain: {:X}, hWnd: {:X}", (size_t) pSwapChain, (size_t) hWnd);
}

void FGHooks::ClearDx12InteropPresentSC(IUnknown* pSwapChain)
{
    if (pSwapChain == nullptr || pSwapChain != _dx12InteropPresentSC)
        return;

    LOG_INFO("Clearing DX12 presenting swapchain: {:X}", (size_t) pSwapChain);

    _dx12InteropPresentSC = nullptr;
    _dx12InteropPresentHwnd = nullptr;
}

bool FGHooks::IsDx12InteropPresentSC(IUnknown* pSwapChain)
{
    return pSwapChain != nullptr && pSwapChain == _dx12InteropPresentSC;
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
                 (State::Instance().gameEngine == GameEngineType::Unity &&
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
    IFGFeature* fg = State::Instance().currentFG;

    if (fg != nullptr && fg->IsActive())
    {
        State::Instance().fgChanged = true;
        fg->UpdateTarget();
        fg->Deactivate();
    }

    const bool borderless = UseBorderless();
    bool modeChanged = false;
    if (borderless)
    {
        if (Fullscreen)
        {
            Fullscreen = false;

            // Also when created in emulated fullscreen, the window may not cover the screen yet
            State::Instance().SCExclusiveFullscreen = true;
            modeChanged = true;

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

    if (State::Instance().activeFgOutput == FGOutput::XeFG && Fullscreen)
        LOG_WARN("Using exclusive fullscreen with XeFG!!!");

    auto result = S_OK;

    if (!borderless)
    {
        result = o_FGSCSetFullscreenState(This, Fullscreen, pTarget);
        LOG_DEBUG("Fullscreen: {}, pTarget: {:X}, Result: {:X}", Fullscreen, (size_t) pTarget, (UINT) result);
    }

    if (result == S_OK)
        State::Instance().realExclusiveFullscreen = Fullscreen;

    if (result == S_OK && modeChanged)
    {
        LOG_DEBUG("Mode changed");

        _borderlessPending = false;

        if (State::Instance().SCExclusiveFullscreen)
        {
            ApplyBorderless(pTarget);
        }
        else
        {
            DXGI_SWAP_CHAIN_DESC scDesc {};
            This->GetDesc(&scDesc);
            RestoreWindow(scDesc.BufferDesc.Width, scDesc.BufferDesc.Height);
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

    if (result == S_OK && State::Instance().SCExclusiveFullscreen && UseBorderless())
        pDesc->Windowed = false;

    return result;
}

HRESULT FGHooks::hkGetFullscreenState(IDXGISwapChain* This, BOOL* pFullscreen, IDXGIOutput** ppTarget)
{
    auto result = o_FGSCGetFullscreenState(This, pFullscreen, ppTarget);

    if (result == S_OK && State::Instance().SCExclusiveFullscreen && UseBorderless())
    {
        if (pFullscreen != nullptr)
            *pFullscreen = true;

        // Exclusive fullscreen reports its output, games use it
        if (ppTarget != nullptr && *ppTarget == nullptr)
            This->GetContainingOutput(ppTarget);
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

    if (State::Instance().currentCommandQueue != nullptr && resizeFence != nullptr && resizeFenceEvent != nullptr)
    {
        LOG_DEBUG("Waiting for GPU to finish before resizing buffers");

        const auto waitResult = WaitForQueueIdle(State::Instance().currentCommandQueue);

        LOG_DEBUG("WaitForSingleObject result: {}", waitResult);
    }

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

    // DLSSG creates its swapchain with ALLOW_TEARING and DXGI rejects resizes that add or remove it
    if (State::Instance().activeFgOutput == FGOutput::DLSSG && !(SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING))
    {
        LOG_DEBUG("Keeping DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING for DLSSG");
        SwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    LOG_DEBUG("BufferCount: {}, Width: {}, Height: {}, NewFormat:{}, SwapChainFlags: {:X}", BufferCount, Width, Height,
              (UINT) NewFormat, SwapChainFlags);

    auto fgDx12 = State::Instance().currentFG;
    IFGFeature* fg = fgDx12;

    if (!State::Instance().SCExclusiveFullscreen && Config::Instance()->FGSkipResizeBuffers.value_or_default())
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

                    if (fgDx12 != nullptr && Config::Instance()->FGModifyBufferState.value_or_default())
                    {
                        LOG_INFO("Trying to change backbuffer state to COMMON");

                        auto cmdList = fgDx12->GetUICommandList();

                        ID3D12Resource* backBuffer = nullptr;
                        if (cmdList != nullptr &&
                            swapchain->GetBuffer(swapchainIndex, IID_PPV_ARGS(&backBuffer)) == S_OK)
                        {
                            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                                backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);

                            cmdList->ResourceBarrier(1, &barrier);

                            backBuffer->Release();
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

        // Docs say you cannot do that
        // SwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    State::Instance().SCAllowTearing = (SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;

    State::Instance().SCLastFlags = SwapChainFlags;

    ResizeScope lg(fgDx12, 6677);

    if (fg != nullptr && fg->IsActive())
    {
        State::Instance().fgChanged = true;
        fg->UpdateTarget();
        fg->Deactivate();
    }

    _skipResize1 = true;

    // Release swapchain backbuffers to prevent errors when resizing
    //    if (State::Instance().activeFgOutput == FGOutput::XeFG)
    //    {
    //        for (UINT i = 0; i < 8; i++)
    //        {
    //            ID3D12Resource* backBuffer = nullptr;
    //            auto bbResult = This->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
    //
    //            if (bbResult == S_OK)
    //            {
    //                LOG_DEBUG("Backbuffer {}: {:X}", i, (size_t) backBuffer);
    //                auto refCount = backBuffer->Release();
    //                while (refCount > XEFG_RESOURCE_REF_LIMIT)
    //                {
    //                    LOG_DEBUG("Releasing backbuffer {}: RefCount {}", i, refCount);
    //                    refCount = backBuffer->Release();
    //                }
    //
    // #if (XEFG_RESOURCE_REF_LIMIT == 0)
    //                oldBackBuffers.push_back(backBuffer);
    // #endif
    //            }
    //            else
    //            {
    //                LOG_DEBUG("GetBuffer failed for index {}: {:X}", i, (UINT) bbResult);
    //                break;
    //            }
    //        }
    //    }

    HRESULT result;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        result = o_FGSCResizeBuffers(This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    _skipResize1 = false;

    LOG_DEBUG("Result: {:X}, Caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));

    if (result == S_OK)
    {
        if (fg != nullptr)
        {
            State::Instance().fgChanged = true;
            fg->Deactivate();
            fg->UpdateTarget();
        }
    }

    // Window is fixed on next present, it sends window messages and would resize again from here
    if (SUCCEEDED(result) && UseBorderless() && State::Instance().SCExclusiveFullscreen)
        _borderlessPending = true;

    return result;
}

HRESULT FGHooks::hkResizeTarget(IDXGISwapChain* This, const DXGI_MODE_DESC* pNewTargetParameters)
{
    // Windowed games resize their window with it
    if (UseBorderless() && State::Instance().SCExclusiveFullscreen)
    {
        LOG_DEBUG("Skipping resize target.");
        return S_OK;
    }

    IFGFeature* fg = State::Instance().currentFG;

    if (fg != nullptr && fg->IsActive())
    {
        State::Instance().fgChanged = true;
        fg->UpdateTarget();
        fg->Deactivate();
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

    if (State::Instance().currentCommandQueue != nullptr && resizeFence != nullptr && resizeFenceEvent != nullptr)
    {
        LOG_DEBUG("Waiting for GPU to finish before resizing buffers");

        const auto waitResult = WaitForQueueIdle(State::Instance().currentCommandQueue);

        LOG_DEBUG("WaitForSingleObject result: {}", waitResult);
    }

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

    // DLSSG creates its swapchain with ALLOW_TEARING and DXGI rejects resizes that add or remove it
    if (State::Instance().activeFgOutput == FGOutput::DLSSG && !(SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING))
    {
        LOG_DEBUG("Keeping DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING for DLSSG");
        SwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    LOG_DEBUG("BufferCount: {}, Width: {}, Height: {}, NewFormat:{}, SwapChainFlags: {:X}, Caller: {}", BufferCount,
              Width, Height, (UINT) Format, SwapChainFlags, Util::WhoIsTheCaller(_ReturnAddress()));

    auto fgDx12 = State::Instance().currentFG;
    IFGFeature* fg = fgDx12;

    if (!State::Instance().SCExclusiveFullscreen && Config::Instance()->FGSkipResizeBuffers.value_or_default())
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

                    if (fgDx12 != nullptr && Config::Instance()->FGModifyBufferState.value_or_default())
                    {
                        LOG_INFO("Trying to change backbuffer state to COMMON");

                        auto cmdList = fgDx12->GetUICommandList();
                        ID3D12Resource* backBuffer = nullptr;
                        if (cmdList != nullptr &&
                            swapchain->GetBuffer(swapchainIndex, IID_PPV_ARGS(&backBuffer)) == S_OK)
                        {
                            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                                backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);

                            cmdList->ResourceBarrier(1, &barrier);

                            backBuffer->Release();
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

        // SwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    State::Instance().SCAllowTearing = (SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;

    State::Instance().SCLastFlags = SwapChainFlags;

    ResizeScope lg(fgDx12, 6678);

    if (fg != nullptr && fg->IsActive())
    {
        State::Instance().fgChanged = true;
        fg->UpdateTarget();
        fg->Deactivate();
    }

    // Release menu render targets
    if (Config::Instance()->OverlayMenu.value_or_default())
        MenuOverlayDx::CleanupRenderTarget(false, NULL);

    // Release swapchain backbuffers to prevent errors when resizing
    //    if (State::Instance().activeFgOutput == FGOutput::XeFG)
    //    {
    //        for (UINT i = 0; i < 8; i++)
    //        {
    //            ID3D12Resource* backBuffer = nullptr;
    //            auto bbResult = This->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
    //
    //            if (bbResult == S_OK)
    //            {
    //                LOG_DEBUG("Backbuffer {}: {:X}", i, (size_t) backBuffer);
    //                auto refCount = backBuffer->Release();
    //                while (refCount > XEFG_RESOURCE_REF_LIMIT)
    //                {
    //                    LOG_DEBUG("Releasing backbuffer {}: RefCount {}", i, refCount);
    //                    refCount = backBuffer->Release();
    //                }
    //
    // #if (XEFG_RESOURCE_REF_LIMIT == 0)
    //                oldBackBuffers.push_back(backBuffer);
    // #endif
    //            }
    //            else
    //            {
    //                LOG_DEBUG("GetBuffer failed for index {}: {:X}", i, (UINT) bbResult);
    //                break;
    //            }
    //        }
    //    }

    HRESULT result;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        _skipResize = true;

        // FSR FG proxy doesn't implement ResizeBuffers1, it returns S_OK without resizing.
        // Called through the vtable, _skipResize passes the hooked class to its original
        if (State::Instance().activeFgOutput == FGOutput::FSRFG)
        {
            result = This->ResizeBuffers(BufferCount, Width, Height, Format, SwapChainFlags);
        }
        else
        {
            result = o_FGSCResizeBuffers1(This, BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask,
                                          ppPresentQueue);
        }

        _skipResize = false;
    }

    LOG_DEBUG("Result: {:X}, Caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));

    if (result == S_OK)
    {
        if (fg != nullptr)
        {
            State::Instance().fgChanged = true;
            fg->Deactivate();
            fg->UpdateTarget();
        }
    }

    // Window is fixed on next present, it sends window messages and would resize again from here
    if (SUCCEEDED(result) && UseBorderless() && State::Instance().SCExclusiveFullscreen)
        _borderlessPending = true;

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

    if (_borderlessPending)
    {
        _borderlessPending = false;

        if (State::Instance().SCExclusiveFullscreen)
            ApplyBorderless(nullptr);
        else if (_windowSaved)
            RestoreWindow(0, 0);
    }

    auto& state = State::Instance();
    auto config = Config::Instance();

    if (state.isShuttingDown)
    {
        if (pPresentParameters == nullptr)
            return o_FGSCPresent(This, SyncInterval, Flags);
        else
            return o_FGSCPresent1((IDXGISwapChain1*) This, SyncInterval, Flags, pPresentParameters);
    }

    auto willPresent = (Flags & DXGI_PRESENT_TEST) == 0;

    if (willPresent)
    {
        state.fgLastFrame++;

        double ftDelta = 0.0f;
        auto now = Util::MillisecondsNow();

        if (_lastFGFrameTime > 0.0)
            ftDelta = now - _lastFGFrameTime;

        _lastFGFrameTime = now;
        state.lastFGFrameTime = ftDelta;

        LOG_DEBUG("flags: {:X}, Frametime: {}", Flags, ftDelta);

        IUnknown* device = state.currentD3D12Device ? state.currentD3D12Device : (IUnknown*) state.currentD3D11Device;
        InputCommon::mark_present_start(device);
    }

    IFGFeature* fg = state.currentFG;

    if (fg != nullptr && willPresent && fg->IsActive() && !fg->IsPaused())
    {
        if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)
        {
            std::optional<double> upscalerTimeOpt {};

            if (state.swapchainInteropApi == SwapchainInteropApi::Dx11wDx12 && state.currentD3D11Device != nullptr)
            {
                ID3D11DeviceContext* context = nullptr;
                state.currentD3D11Device->GetImmediateContext(&context);

                if (upscalerTimeOpt = currentFeature->ReadUpscalerTime(context); upscalerTimeOpt.has_value())
                    currentFeature->ReadDetailedGpuTimes(context, state.detailedGpuTimes);

                context->Release();
            }
            else if (state.swapchainInteropApi == SwapchainInteropApi::None && currentCommandQueue != nullptr)
            {
                if (upscalerTimeOpt = currentFeature->ReadUpscalerTime(currentCommandQueue);
                    upscalerTimeOpt.has_value())
                {
                    currentFeature->ReadDetailedGpuTimes(currentCommandQueue, state.detailedGpuTimes);
                }

                if (auto fgGpuTime = fg->ReadGpuTime(currentCommandQueue))
                {
                    state.detailedGpuTimes.emplace_back(DetailedGpuTime { fg->Name(), fgGpuTime.value(), false });
                }
            }

            if (upscalerTimeOpt.has_value())
            {
                auto upscalerTime = upscalerTimeOpt.value();
                // filter out possibly wrong measured high values
                if (upscalerTime < 100.0)
                {
                    State::Instance().frameTimeMutex.lock();
                    State::Instance().upscaleTimes.push_back(upscalerTime);
                    State::Instance().upscaleTimes.pop_front();
                    State::Instance().frameTimeMutex.unlock();
                }
            }
        }
    }

    const bool useMutex = config->FGUseMutexForSwapchain.value_or_default();
    bool mutexUsed = false;
    if (willPresent && fg != nullptr && fg->IsActive() && !fg->IsPaused() && useMutex && fg->Mutex.getOwner() != 2)
    {
        LOG_TRACE("Waiting FG->Mutex 2, current: {}", fg->Mutex.getOwner());
        fg->Mutex.lock(2);
        mutexUsed = true;
        LOG_TRACE("Accuired FG->Mutex: {}", fg->Mutex.getOwner());
    }

    // FG can become active after the check above, its Present then would race with EvaluateState
    const bool fgFeatureActive =
        fg != nullptr && fg->IsActive() && !fg->IsPaused() && (!useMutex || mutexUsed || fg->Mutex.getOwner() == 2);

    sl::FrameToken* localToken = nullptr;
    sl::Result tokenResult = sl::Result::eErrorReflexAPI;
    if (willPresent && fg != nullptr && !fgFeatureActive)
        state.dlssgDetectedInterpolationCount = 0;

    if (willPresent && fgFeatureActive && state.activeFgOutput == FGOutput::DLSSG)
    {
        if ((!ReflexHooks::gameIsSendingMarkers() || !config->FGDLSSGUseGamesReflexMarkers.value_or_default()))
        {
            if (StreamlineProxy::PCLSetMarker() != nullptr)
            {
                ((IDXGISwapChain4*) This)->GetCurrentBackBufferIndex();
                const uint32_t frameId = (uint32_t) fg->FrameCount();
                tokenResult = StreamlineProxy::GetNewFrameToken()(localToken, &frameId);

                if (tokenResult == sl::Result::eOk)
                {
                    ScopedOptiScalerReflex optiScalerCall {};
                    StreamlineProxy::PCLSetMarker()(sl::PCLMarker::ePresentStart, *localToken);
                }
            }
        }
    }

    if (willPresent && fgFeatureActive)
    {
        if (state.activeFgInput == FGInput::FSRFG)
            ffxPresentCallback();
        else if (state.activeFgInput == FGInput::FSRFG30)
            FSR3FG::ffxPresentCallback();

        fg->Present();
    }
    else if (willPresent && fg != nullptr)
    {
        LOG_TRACE("FGHooks::FGPresent: FG feature exists but is inactive/paused; pass-through present only");
    }

    if (willPresent && state.swapchainInteropApi == SwapchainInteropApi::None)
    {
        ResTrack_Dx12::ClearPossibleHudless();
        Hudfix_Dx12::PresentStart();
    }

    if (willPresent && config->ForceVsync.has_value())
    {
        LOG_DEBUG("ForceVsync: {}, VsyncInterval: {}, SCAllowTearing: {}, realExclusiveFullscreen: {}",
                  config->ForceVsync.value(), config->VsyncInterval.value_or_default(), state.SCAllowTearing,
                  state.realExclusiveFullscreen);

        if (!config->ForceVsync.value())
        {
            SyncInterval = 0;

            // This swapchain's flags and live windowed state, the global ones can be stale or the game's
            DXGI_SWAP_CHAIN_DESC scDesc {};
            const bool allowTearing = This->GetDesc(&scDesc) == S_OK &&
                                      (scDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0 && scDesc.Windowed;

            if (allowTearing)
            {
                LOG_DEBUG("Adding DXGI_PRESENT_ALLOW_TEARING");
                Flags |= DXGI_PRESENT_ALLOW_TEARING;
            }
        }
        else
        {
            SyncInterval = config->VsyncInterval.value_or_default();

            if (SyncInterval < 1)
                SyncInterval = 1;

            LOG_DEBUG("Removing DXGI_PRESENT_ALLOW_TEARING");
            Flags &= ~DXGI_PRESENT_ALLOW_TEARING;
        }

        LOG_DEBUG("Final SyncInterval: {}", SyncInterval);
    }

    // FSR-FG and XeFG with the Reflex output: the game's present as DLSSG reports it
    ID3D12CommandQueue* gameQueue = nullptr;
    uint32_t frameMultiplier = 1;
    void* xefgContext = nullptr;
    uint64_t xefgFrameId = 0;

    if (auto fg = state.currentFG; willPresent && fg != nullptr &&
                                   (state.activeFgOutput == FGOutput::FSRFG || state.activeFgOutput == FGOutput::XeFG))
    {
        gameQueue = fg->GetCommandQueue();
        frameMultiplier = fg->IsActive() && !fg->IsPaused() ? fg->GetInterpolatedFrameCount() + 1 : 1;
        InputCommon::fg_game_present(gameQueue, false, frameMultiplier);

        if (state.activeFgOutput == FGOutput::XeFG)
        {
            xefgContext = fg->SwapchainContext();
            xefgFrameId = InputCommon::xefg_marked_frame_id();
        }
    }

    HRESULT result;
    const auto presentStart = Util::MillisecondsNow();
    if (pPresentParameters == nullptr)
        result = o_FGSCPresent(This, SyncInterval, Flags);
    else
        result = o_FGSCPresent1((IDXGISwapChain1*) This, SyncInterval, Flags, pPresentParameters);

    if (gameQueue != nullptr)
        InputCommon::fg_game_present(gameQueue, true, frameMultiplier);

    // How many frames XeFG sends to its presenter for the batch it marked in this present
    if (xefgContext != nullptr && XeFGProxy::GetLastPresentStatus() != nullptr)
    {
        xefg_swapchain_present_status_t status {};

        if (XeFGProxy::GetLastPresentStatus()((xefg_swapchain_handle_t) xefgContext, &status) ==
            XEFG_SWAPCHAIN_RESULT_SUCCESS)
        {
            InputCommon::xefg_present_status(xefgFrameId, status.framesPresented);
        }
    }

    if (result == S_OK)
    {
        LOG_DEBUG("Result: {:X}", result);
    }
    else
    {
        if (result == DXGI_ERROR_DEVICE_REMOVED && state.currentD3D12Device != nullptr)
            Util::GetDeviceRemovedReason(state.currentD3D12Device);
    }

    if (tokenResult == sl::Result::eOk && localToken != nullptr && fgFeatureActive &&
        (!ReflexHooks::gameIsSendingMarkers() || !config->FGDLSSGUseGamesReflexMarkers.value_or_default()) &&
        willPresent && state.activeFgOutput == FGOutput::DLSSG)
    {
        if (StreamlineProxy::PCLSetMarker() != nullptr)
        {
            ScopedOptiScalerReflex optiScalerCall {};
            StreamlineProxy::PCLSetMarker()(sl::PCLMarker::ePresentEnd, *localToken);
        }

        LOG_DEBUG("Calling ReflexSleep");
        ScopedOptiScalerReflex optiScalerCall {};
        StreamlineProxy::ReflexSleep()(*localToken);
    }

    if (state.swapchainInteropApi == SwapchainInteropApi::None)
        Hudfix_Dx12::PresentEnd();

    // DXVK's DXGI presents through Vulkan where the limiter already sleeps
    if (willPresent && !state.reflexLimitsFps && state.activeFgOutput != FGOutput::NoFG &&
        !IdentifyGpu::getPrimaryGpu().dxgiIsDxvk && !InputCommon::can_limit_fps())
    {
        FrameLimit::sleep(fg != nullptr ? fg->IsActive() && !fg->IsPaused() : false);
    }

    if (willPresent)
        state.lastFGPresentMs = Util::MillisecondsNow() - presentStart;

    if ((config->SimulateWaitableObject.value_or_default() ||
         (state.gameEngine == GameEngineType::Unity && state.activeFgOutput == FGOutput::XeFG)) &&
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

    if (_skipReleaseChecks || State::Instance().currentFGSwapchain != This || State::Instance().isShuttingDown)
        return o_FGRelease(This);

    This->AddRef();

    if (!Config::Instance()->FGPreserveSwapChain.value_or_default())
    {
        if (o_FGRelease(This) == 1)
        {
            ReleaseFGSwapchain(This, true);
            return 0;
        }
    }
    else
    {
        auto refCount = o_FGRelease(This);

        // Only FSR FG and XeFG release the kept swapchain with their swapchain context
        const bool canRelease =
            State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgOutput == FGOutput::XeFG;

        if (refCount == 1)
        {
            LOG_INFO("Preserving FG Swapchain from release");
            _releasedFGSwapchain = canRelease ? This : nullptr;
            _fgSwapchainBaseRefs = 1;
            return 0;
        }

        // Only the FG library's own references are left
        if (canRelease && refCount - 1 == _fgSwapchainBaseRefs)
            _releasedFGSwapchain = This;
    }

    return o_FGRelease(This);
}

void FGHooks::ReleaseFGSwapchain(IUnknown* swapchain, bool releaseReferences)
{
    LOG_DEBUG("");

    if (State::Instance().currentCommandQueue != nullptr && resizeFence != nullptr && resizeFenceEvent != nullptr)
    {
        LOG_DEBUG("Waiting for GPU to finish");

        const auto waitResult = WaitForQueueIdle(State::Instance().currentCommandQueue);

        LOG_DEBUG("WaitForSingleObject result: {}", waitResult);
    }

    // Release swapchain backbuffers to prevent errors when releasing FG swapchain
    if (releaseReferences)
    {
        DXGI_SWAP_CHAIN_DESC scDesc {};
        ((IDXGISwapChain*) swapchain)->GetDesc(&scDesc);

        for (UINT i = 0; i < scDesc.BufferCount; i++)
        {
            ID3D12Resource* backBuffer = nullptr;
            auto bbResult = ((IDXGISwapChain*) swapchain)->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

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
    _skipReleaseChecks = true;

    if (State::Instance().currentFG != nullptr)
    {
        LOG_DEBUG("FG Swapchain released, release FG & swapchain context");
        State::Instance().currentFG->ReleaseSwapchain(_hwnd);
    }

    // A kept swapchain stays tracked when the FG library couldn't release it
    if (!releaseReferences && State::Instance().currentFG != nullptr &&
        State::Instance().currentFG->SwapchainContext() != nullptr)
    {
        LOG_WARN("Kept FG swapchain couldn't be released");
        _skipReleaseChecks = false;
        return;
    }

    LOG_DEBUG("FG Swapchain released, clearing currentFGSwapchain");
    State::Instance().currentFGSwapchain = nullptr;
    _releasedFGSwapchain = nullptr;
    _borderlessPending = false;
    State::Instance().SCExclusiveFullscreen = false;

    if (State::Instance().currentSwapchain == swapchain)
        State::Instance().currentSwapchain = nullptr;

    if (releaseReferences && State::Instance().currentWrappedSwapchain != nullptr &&
        State::Instance().currentSwapchainDesc.OutputWindow == _hwnd)
    {
        auto refCount = State::Instance().currentWrappedSwapchain->Release();

        while (refCount > 0 && refCount < 0xffffff00)
        {
            refCount = State::Instance().currentWrappedSwapchain->Release();
        }

        State::Instance().currentWrappedSwapchain = nullptr;
    }

    _skipReleaseChecks = false;
}
