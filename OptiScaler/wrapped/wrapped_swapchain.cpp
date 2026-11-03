#include "pch.h"
#include "wrapped_swapchain.h"

#include <Util.h>
#include <Config.h>

#include <nvapi/fakenvapi.h>
#include <hooks/Reflex_Hooks.h>
#include <hooks/D3D12_Hooks.h>
#include <hooks/FG_Hooks.h>
#include <with_dx12/dx11_with_dx12_sync.h>

#include <menu/menu_overlay_base.h>
#include <menu/menu_overlay_dx.h>
#include <wrapped/wrapped_factory.h>

#include <misc/FrameLimit.h>
#include <misc/DeferredRelease.h>

#include <d3d11.h>
#include <d3d12.h>
#include <misc/IdentifyGpu.h>
#include <low_latency/input/input_common.h>
#include <inputs/FG/XeFG_Inputs_Dx12.h>

#include <magic_enum.hpp>

#include <low_latency/input/input_antilag2.h>

#ifdef DXGI_DEBUG_ENABLED
#include <magic_enum.hpp>
#include <dxgidebug.h>

#pragma comment(lib, "dxguid.lib")

#ifdef ENABLE_DEBUG_LAYER_DX12
#include <d3d12sdklayers.h>
#endif
#endif

#pragma intrinsic(_ReturnAddress)

// Used RenderDoc's wrapped object as referance
// https://github.com/baldurk/renderdoc/blob/v1.x/renderdoc/driver/dxgi/dxgi_wrapped.cpp

static int scCount = 0;
static double _lastFrameTime = 0;
static bool _dx11Device = false;
static bool _dx12Device = false;

const GUID IID_IUnwrappedDXGISwapChain = {
    0xe8a33b4a, 0x1405, 0x424c, { 0xae, 0x88, 0xd, 0x3e, 0x9d, 0x46, 0xc9, 0x14 }
};

static ID3D12Fence* resizeFence = nullptr;
static ID3D12Device* resizeFenceDevice = nullptr;
static UINT64 resizeFenceValue = 0;
static HANDLE resizeFenceEvent = nullptr;

static void UpdateOutputColorSpace(DXGI_COLOR_SPACE_TYPE colorSpace)
{
    auto& state = State::Instance();

    OutputColorSpace info {};
    info.dxgiColorSpace = colorSpace;

    switch (colorSpace)
    {
        // ------------------------------------------------------------
        // SDR / Rec.709
        // ------------------------------------------------------------

    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
        info.transfer = ColorTransfer::SRGB;
        info.primaries = ColorPrimaries::Rec709;
        info.range = ColorRange::Full;
        info.model = ColorModel::RGB;
        info.valid = true;

        break;

    case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
        info.transfer = ColorTransfer::SRGB;
        info.primaries = ColorPrimaries::Rec709;
        info.range = ColorRange::Studio;
        info.model = ColorModel::RGB;
        info.valid = true;

        break;

        // ------------------------------------------------------------
        // scRGB / linear Rec.709
        // ------------------------------------------------------------

    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
        info.transfer = ColorTransfer::Linear;
        info.primaries = ColorPrimaries::Rec709;
        info.range = ColorRange::Full;
        info.model = ColorModel::RGB;
        info.valid = true;

        break;

        // ------------------------------------------------------------
        // HDR10 / PQ / Rec.2020
        // ------------------------------------------------------------

    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
        info.transfer = ColorTransfer::PQ;
        info.primaries = ColorPrimaries::Rec2020;
        info.range = ColorRange::Full;
        info.model = ColorModel::RGB;
        info.valid = true;

        break;

    case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
        info.transfer = ColorTransfer::PQ;
        info.primaries = ColorPrimaries::Rec2020;
        info.range = ColorRange::Studio;
        info.model = ColorModel::RGB;
        info.valid = true;

        break;

    case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:
    case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
        info.transfer = ColorTransfer::PQ;
        info.primaries = ColorPrimaries::Rec2020;
        info.range = ColorRange::Studio;
        info.model = ColorModel::YCbCr;
        info.valid = true;

        break;

        // ------------------------------------------------------------
        // HLG / Rec.2020
        // ------------------------------------------------------------

    case DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
        info.transfer = ColorTransfer::HLG;
        info.primaries = ColorPrimaries::Rec2020;
        info.range = ColorRange::Full;
        info.model = ColorModel::YCbCr;
        info.valid = true;

        break;

    case DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
        info.transfer = ColorTransfer::HLG;
        info.primaries = ColorPrimaries::Rec2020;
        info.range = ColorRange::Studio;
        info.model = ColorModel::YCbCr;
        info.valid = true;

        break;

        // ------------------------------------------------------------
        // Unknown / unsupported
        // ------------------------------------------------------------

    default:
        info.transfer = ColorTransfer::Unknown;
        info.primaries = ColorPrimaries::Unknown;
        info.range = ColorRange::Unknown;
        info.model = ColorModel::Unknown;
        info.valid = false;

        break;
    }

    state.outputColorSpace = info;

    LOG_INFO("Output color space updated: DXGI: {}, Transfer: {}, Primaries: {}, Range: {}, Model: {}, Valid: {}",
             magic_enum::enum_name(info.dxgiColorSpace), magic_enum::enum_name(info.transfer),
             magic_enum::enum_name(info.primaries), magic_enum::enum_name(info.range),
             magic_enum::enum_name(info.model), info.valid);
}

// FG and the swapchain state belong to the game's swapchain, not to other windows' swapchains
static bool IsCurrentSwapchain(IDXGISwapChain* swapchain)
{
    auto& state = State::Instance();
    return state.currentSwapchain == swapchain || state.currentWrappedSwapchain == swapchain;
}

// Game's output, the FG library's swapchain on the FG window included
static bool IsGameOutput(IDXGISwapChain* swapchain, HWND hwnd)
{
    return IsCurrentSwapchain(swapchain) ||
           (State::Instance().currentFGSwapchain != nullptr && !FGHooks::IsOtherWindowThanFG(hwnd));
}

// Menu draws on the current swapchain and the last presenting window
static bool HasMenu(IDXGISwapChain* swapchain, HWND hwnd)
{
    return IsCurrentSwapchain(swapchain) || MenuOverlayBase::Handle() == hwnd;
}

static void WaitForGPUIdle(IUnknown* object)
{
    if (object == nullptr)
        return;

    // Fence state is shared by every caller
    static std::mutex waitMutex;
    std::scoped_lock lock(waitMutex);

    ID3D12CommandQueue* queue = nullptr;

    if (object->QueryInterface(IID_PPV_ARGS(&queue)) != S_OK || queue == nullptr)
        return;

    ID3D12Device* device = nullptr;

    if (queue->GetDevice(IID_PPV_ARGS(&device)) != S_OK || device == nullptr)
    {
        LOG_ERROR("Can't get device of the command queue");
        queue->Release();
        return;
    }

    if (resizeFence == nullptr || resizeFenceDevice != device)
    {
        if (resizeFence != nullptr)
        {
            resizeFence->Release();
            resizeFence = nullptr;
        }

        resizeFenceValue = 0;
        resizeFenceDevice = nullptr;

        auto result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&resizeFence));

        if (result != S_OK)
        {
            LOG_ERROR("CreateFence error: {:X}", (UINT) result);
            resizeFence = nullptr;
        }
        else
        {
            resizeFenceDevice = device;
        }
    }

    device->Release();

    if (resizeFenceEvent == nullptr)
        resizeFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    if (resizeFence == nullptr || resizeFenceEvent == nullptr)
    {
        queue->Release();
        return;
    }

    LOG_DEBUG("Waiting for GPU to finish before resizing buffers");

    resizeFenceValue++;
    auto signalResult = queue->Signal(resizeFence, resizeFenceValue);
    queue->Release();

    if (signalResult != S_OK)
    {
        LOG_ERROR("Signal error: {:X}", (UINT) signalResult);
        return;
    }

    const auto deadline = GetTickCount64() + 5000; // Max 5 sec

    while (resizeFence->GetCompletedValue() < resizeFenceValue)
    {
        auto now = GetTickCount64();

        if (now >= deadline)
        {
            LOG_WARN("GPU idle wait timed out, fence value: {}, completed: {}", resizeFenceValue,
                     resizeFence->GetCompletedValue());
            break;
        }

        auto eventResult = resizeFence->SetEventOnCompletion(resizeFenceValue, resizeFenceEvent);

        if (eventResult != S_OK)
        {
            LOG_ERROR("SetEventOnCompletion error: {:X}", (UINT) eventResult);
            break;
        }

        auto waitResult = WaitForSingleObject(resizeFenceEvent, static_cast<DWORD>(deadline - now));
        LOG_DEBUG("WaitForSingleObject result: {:X}", waitResult);
    }
}

#ifdef DXGI_DEBUG_ENABLED
void ReportDXGILiveObjects()
{
    IDXGIDebug1* dxgiDebug = nullptr;

    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
    {
        dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
        dxgiDebug->Release();
    }
}

void ReadDxgiInfoQueue()
{
    IDXGIInfoQueue* dxgiInfoQueue = nullptr;
    if (DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue)) == S_OK)
    {
        UINT64 msgCount = dxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);
        for (UINT64 i = 0; i < msgCount; ++i)
        {
            SIZE_T msgLen = 0;
            dxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, nullptr, &msgLen);
            std::vector<char> buf(msgLen);
            auto* msg = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(buf.data());
            dxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, msg, &msgLen);

            auto description = std::string(msg->pDescription, msg->DescriptionByteLength);
            LOG_DEBUG("DXGI Debug Message: Category: {}, Severity: {}, ID: {}, Description: {}",
                      magic_enum::enum_name(msg->Category), magic_enum::enum_name(msg->Severity), msg->ID, description);
        }
    }
}

#ifdef ENABLE_DEBUG_LAYER_DX12
void ReportD3D12LiveObjects(ID3D12Device* device)
{
    ID3D12DebugDevice* debugDevice = nullptr;

    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&debugDevice))))
    {
        debugDevice->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
        debugDevice->Release();
    }
}
#endif
#endif

static HRESULT LocalPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags,
                            const DXGI_PRESENT_PARAMETERS* pPresentParameters, IUnknown* pDevice, HWND hWnd, bool isUWP,
                            bool isCurrent)
{
    if (State::Instance().isShuttingDown)
    {
        if (pPresentParameters == nullptr)
            return pSwapChain->Present(SyncInterval, Flags);
        else
            return ((IDXGISwapChain1*) pSwapChain)->Present1(SyncInterval, Flags, pPresentParameters);
    }

    // This lock will delay/prevent release of buffers while present is ongoing
    std::shared_lock<std::shared_mutex> dx11wDx12PresentLock(Dx11wDx12Sync::PresentResizeMutex(), std::defer_lock);
    if (State::Instance().swapchainInteropApi == SwapchainInteropApi::Dx11wDx12 &&
        State::Instance().activeFgOutput == FGOutput::XeFG)
    {
        dx11wDx12PresentLock.lock();
    }

    LOG_DEBUG("{}", State::Instance().frameCount);

    HRESULT presentResult;

    auto willPresent = (Flags & DXGI_PRESENT_TEST) == 0;

    if (willPresent)
    {
        double ftDelta = 0.0;

        // Other windows' presents would shorten the game's frame time
        if (isCurrent)
        {
            auto now = Util::MillisecondsNow();

            if (_lastFrameTime != 0)
                ftDelta = now - _lastFrameTime;

            _lastFrameTime = now;

            State::Instance().presentFrameTime = ftDelta;

            if (State::Instance().currentFG == nullptr)
                State::Instance().lastFGFrameTime = ftDelta;
        }

        LOG_DEBUG("SyncInterval: {}, Flags: {:X}, Frametime: {:0.3f} ms", SyncInterval, Flags, ftDelta);

        // Update swapchain info evey frame
        if (isCurrent && pSwapChain->GetDesc(&State::Instance().currentSwapchainDesc) != S_OK)
            LOG_WARN("Can't get swapchain desc!");
    }

    ID3D11Device* device = nullptr;
    ID3D12Device* device12 = nullptr;
    ID3D12CommandQueue* cq = nullptr;

    bool isD3D11 = false;

    // try to obtain directx objects and find the path
    if (pDevice->QueryInterface(IID_PPV_ARGS(&device)) == S_OK)
    {
        isD3D11 = true;
        device->Release();

        if (!_dx11Device)
            LOG_DEBUG("D3D11Device captured");

        _dx11Device = true;

        if (isCurrent)
        {
            State::Instance().swapchainApi = DX11;
            State::Instance().currentD3D11Device = device;
        }
    }
    else if (pDevice->QueryInterface(IID_PPV_ARGS(&cq)) == S_OK)
    {
        cq->Release();

        if (!_dx12Device)
            LOG_DEBUG("D3D12CommandQueue captured");

        ID3D12CommandQueue* realQueue = nullptr;
        if (Util::CheckForRealObject(__FUNCTION__, cq, (IUnknown**) &realQueue))
            cq = realQueue;

        if (isCurrent)
        {
            State::Instance().swapchainApi = DX12;

            if (State::Instance().currentCommandQueue == nullptr)
                State::Instance().currentCommandQueue = cq;
        }

        if (cq->GetDevice(IID_PPV_ARGS(&device12)) == S_OK)
        {
            device12->Release();

            if (!_dx12Device)
                LOG_DEBUG("D3D12Device captured");

            _dx12Device = true;

            if (isCurrent)
                State::Instance().currentD3D12Device = device12;

            D3D12Hooks::HookDevice(device12);
        }
    }

    auto fg = State::Instance().currentFG;
    if (willPresent && fg != nullptr)
        ReflexHooks::update(fg->IsActive(), false);
    else
        ReflexHooks::update(false, false);

    InputCommon::update();

    // Upscaler GPU time computation
    if (willPresent && (fg == nullptr || !fg->IsActive() || fg->IsPaused()))
    {
        if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)
        {
            std::optional<double> upscalerTimeOpt {};

            // If using DX11 with DX12 interop
            if (State::Instance().swapchainInteropApi == SwapchainInteropApi::Dx11wDx12)
            {
                if (State::Instance().currentD3D11Device != nullptr &&
                    (currentFeature->Api() == API::DX11 || currentFeature->IsWithDx12()))
                {
                    ID3D11DeviceContext* context = nullptr;
                    State::Instance().currentD3D11Device->GetImmediateContext(&context);

                    if (upscalerTimeOpt = currentFeature->ReadUpscalerTime(context); upscalerTimeOpt.has_value())
                        currentFeature->ReadDetailedGpuTimes(context, State::Instance().detailedGpuTimes);

                    context->Release();
                }
            }
            // If DX12
            else if (cq != nullptr && currentFeature->Api() == API::DX12 && !currentFeature->IsWithDx12())
            {
                if (upscalerTimeOpt = currentFeature->ReadUpscalerTime(cq); upscalerTimeOpt.has_value())
                    currentFeature->ReadDetailedGpuTimes(cq, State::Instance().detailedGpuTimes);
            }
            // If DX11
            else if (device != nullptr && (currentFeature->Api() == API::DX11 || currentFeature->IsWithDx12()))
            {
                ID3D11DeviceContext* context = nullptr;
                device->GetImmediateContext(&context);

                if (upscalerTimeOpt = currentFeature->ReadUpscalerTime(context); upscalerTimeOpt.has_value())
                    currentFeature->ReadDetailedGpuTimes(context, State::Instance().detailedGpuTimes);

                context->Release();
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

    // Fallback when FGPresent is not hooked for V-sync
    if (willPresent && Config::Instance()->ForceVsync.has_value())
    {
        LOG_DEBUG("ForceVsync: {}, VsyncInterval: {}, SCAllowTearing: {}, realExclusiveFullscreen: {}",
                  Config::Instance()->ForceVsync.value(), Config::Instance()->VsyncInterval.value_or_default(),
                  State::Instance().SCAllowTearing, State::Instance().realExclusiveFullscreen);

        if (!Config::Instance()->ForceVsync.value())
        {
            SyncInterval = 0;

            // This swapchain's flags, the global ones can belong to a released or another swapchain
            DXGI_SWAP_CHAIN_DESC scDesc {};
            bool allowTearing = pSwapChain->GetDesc(&scDesc) == S_OK &&
                                (scDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0 && scDesc.Windowed;

            if (allowTearing)
            {
                LOG_DEBUG("Adding DXGI_PRESENT_ALLOW_TEARING");
                Flags |= DXGI_PRESENT_ALLOW_TEARING;
            }
        }
        else
        {
            // Remove allow tearing
            SyncInterval = Config::Instance()->VsyncInterval.value_or_default();

            if (SyncInterval < 1)
                SyncInterval = 1;

            LOG_DEBUG("Removing DXGI_PRESENT_ALLOW_TEARING");
            Flags &= ~DXGI_PRESENT_ALLOW_TEARING;
        }

        LOG_DEBUG("Final SyncInterval: {}", SyncInterval);
    }

    ID3D12CommandQueue* fgPresentQueue = nullptr;
    uint32_t fgFrameMultiplier = 1;

    if (willPresent)
    {
        // Tick feature to let it know if it's frozen
        if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)
        {
            if (auto currentFg = State::Instance().currentFG; currentFg != nullptr)
                currentFeature->TickFrozenCheck(std::max(currentFg->GetInterpolatedFrameCount(), 1u));
            else
                currentFeature->TickFrozenCheck();
        }

        // Draw overlay
        MenuOverlayDx::Present(pSwapChain, SyncInterval, Flags, pPresentParameters, pDevice, hWnd, isUWP);

        if (State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgOutput == FGOutput::XeFG)
        {
            auto fgIsActive = fg != nullptr && fg->IsActive() && !fg->IsPaused();
            InputAntiLag2::injectAl2Context(pSwapChain, fgIsActive);

            // For the Reflex output (and AntiLag 2 with XeFG), the generated and real frames' presents as DLSSG
            // reports them
            if (fgIsActive && pDevice != nullptr && pDevice->QueryInterface(IID_PPV_ARGS(&fgPresentQueue)) == S_OK)
            {
                fgFrameMultiplier = fg->GetInterpolatedFrameCount() + 1;
                InputCommon::fg_output_present(fgPresentQueue, false, fgFrameMultiplier);
            }
        }
        else
        {
            // The game's DLSSG presents through here
            InputCommon::dlssg_present();
        }

        if (isCurrent)
            State::Instance().frameCount++;
    }

    LOG_DEBUG("Calling original present");

    // swapchain present
    if (pPresentParameters == nullptr)
        presentResult = pSwapChain->Present(SyncInterval, Flags);
    else
        presentResult = ((IDXGISwapChain1*) pSwapChain)->Present1(SyncInterval, Flags, pPresentParameters);

    DeferredRelease::Process();

    if (fgPresentQueue != nullptr)
    {
        InputCommon::fg_output_present(fgPresentQueue, true, fgFrameMultiplier);
        fgPresentQueue->Release();
    }

    // DXGI_STATUS_OCCLUDED etc. are not errors
    if (SUCCEEDED(presentResult))
    {
        LOG_DEBUG("Original present result: {:X}", (UINT) presentResult);
    }
    else
    {
        LOG_ERROR("Original present result: {:X}", (UINT) presentResult);

        if (presentResult == DXGI_ERROR_DEVICE_REMOVED)
        {
            if (isD3D11)
            {
                if (State::Instance().currentD3D11Device != nullptr)
                    Util::GetDeviceRemovedReason(State::Instance().currentD3D11Device);
            }
            else if (State::Instance().currentD3D12Device != nullptr)
            {
                Util::GetDeviceRemovedReason(State::Instance().currentD3D12Device);
            }
        }
    }

    return presentResult;
}

static void UpdateSCBuffers(IDXGISwapChain* real, IDXGISwapChain1* real1, UINT bufferCount)
{
    if (bufferCount == 0 && real1 != nullptr)
    {
        DXGI_SWAP_CHAIN_DESC1 desc {};

        if (real1->GetDesc1(&desc) == S_OK)
            bufferCount = desc.BufferCount;
    }

    std::vector<IUnknown*> buffers;

    for (UINT i = 0; i < bufferCount; i++)
    {
        IUnknown* buffer = nullptr;

        // Same pointer the game creates its views with
        if (real->GetBuffer(i, __uuidof(ID3D12Resource), (void**) &buffer) == S_OK)
        {
            buffers.push_back(buffer);
            buffer->Release();
        }
    }

    State::Instance().SetSCBuffers(std::move(buffers));
}

// OptiScaler FG's swapchain wraps this one, the game renders to FG's buffers which keep their format
static bool UseForceHDR()
{
    return Config::Instance()->ForceHDR.value_or_default() && State::Instance().currentFGSwapchain == nullptr;
}

// Only D3D12 views of the buffers are forced to the new format
static void ForceHDRFormat(IUnknown* device, DXGI_FORMAT& format)
{
    if (!UseForceHDR() || device == nullptr)
        return;

    ID3D12CommandQueue* cq = nullptr;
    if (device->QueryInterface(IID_PPV_ARGS(&cq)) != S_OK)
        return;

    cq->Release();
    format = Config::Instance()->UseHDR10.value_or_default() ? DXGI_FORMAT_R10G10B10A2_UNORM
                                                             : DXGI_FORMAT_R16G16B16A16_FLOAT;
}

static void SetForcedHDRColorSpace(IDXGISwapChain3* swapchain)
{
    LOG_INFO("Force HDR on");

    if (swapchain == nullptr || Config::Instance()->SkipColorSpace.value_or_default())
        return;

    DXGI_COLOR_SPACE_TYPE hdrCS = Config::Instance()->UseHDR10.value_or_default()
                                      ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                                      : DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

    UINT css = 0;
    auto result = swapchain->CheckColorSpaceSupport(hdrCS, &css);

    if (result != S_OK)
    {
        LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
        return;
    }

    if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
    {
        result = swapchain->SetColorSpace1(hdrCS);

        if (result != S_OK)
        {
            LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
            return;
        }
    }

    LOG_INFO("HDR color space is set");
}

WrappedIDXGISwapChain4::WrappedIDXGISwapChain4(IDXGISwapChain* real, IUnknown* pDevice, HWND hWnd, UINT flags,
                                               bool isUWP, IUnknown* owner)
    : _real(real), _owner(owner != nullptr ? owner : real), _device(pDevice), _handle(hWnd), _refcount(1), _uwp(isUWP)
{
    _id = ++scCount;
    _lastFlags = flags;

    _real->QueryInterface(IID_PPV_ARGS(&_real1));
    if (_real1 != nullptr)
        _real1->Release();

    _real->QueryInterface(IID_PPV_ARGS(&_real2));
    if (_real2 != nullptr)
        _real2->Release();

    _real->QueryInterface(IID_PPV_ARGS(&_real3));
    if (_real3 != nullptr)
        _real3->Release();

    _real->QueryInterface(IID_PPV_ARGS(&_real4));
    if (_real4 != nullptr)
        _real4->Release();

    _real->AddRef();
    auto refCount = _real->Release();

    CheckForHdrOutput();

    LOG_INFO("{} created, real: {:X}, refCount: {}", _id, (UINT64) real, refCount);
}

WrappedIDXGISwapChain4::~WrappedIDXGISwapChain4() {}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::QueryInterface(REFIID riid, void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    *ppvObject = nullptr;

    LOG_TRACE("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    if (riid == __uuidof(IDXGISwapChain))
    {
        AddRef();
        *ppvObject = (IDXGISwapChain*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGISwapChain1))
    {
        if (_real1)
        {
            AddRef();
            *ppvObject = (IDXGISwapChain1*) this;
            return S_OK;
        }
        else
        {
            return E_NOINTERFACE;
        }
    }
    else if (riid == __uuidof(IDXGISwapChain2))
    {
        if (_real2)
        {
            AddRef();
            *ppvObject = (IDXGISwapChain2*) this;
            return S_OK;
        }
        else
        {
            return E_NOINTERFACE;
        }
    }
    else if (riid == __uuidof(IDXGISwapChain3))
    {
        if (_real3)
        {
            AddRef();
            *ppvObject = (IDXGISwapChain3*) this;
            return S_OK;
        }
        else
        {
            return E_NOINTERFACE;
        }
    }
    else if (riid == __uuidof(IDXGISwapChain4))
    {
        if (_real4)
        {
            AddRef();
            *ppvObject = (IDXGISwapChain4*) this;
            return S_OK;
        }
        else
        {
            return E_NOINTERFACE;
        }
    }
    else if (riid == __uuidof(WrappedIDXGISwapChain4))
    {
        AddRef();
        *ppvObject = this;
        return S_OK;
    }
    else if (riid == __uuidof(IUnknown))
    {
        AddRef();
        *ppvObject = (IUnknown*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGIObject))
    {
        AddRef();
        *ppvObject = (IDXGIObject*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGIDeviceSubObject))
    {
        AddRef();
        *ppvObject = (IDXGIDeviceSubObject*) this;
        return S_OK;
    }
    else if (XeFGInputs::IsXeFGSwapChainIID(riid))
    {
        // Only checked for, never called
        AddRef();
        *ppvObject = this;
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedIDXGISwapChain4::AddRef()
{
    ULONG count = InterlockedIncrement(&_refcount);
    LOG_TRACE("Count: {}, caller: {}", count, Util::WhoIsTheCaller(_ReturnAddress()));
    return count;
}

ULONG STDMETHODCALLTYPE WrappedIDXGISwapChain4::Release()
{
    ULONG ret = InterlockedDecrement(&_refcount);

    LOG_TRACE("Count: {}, caller: {}", ret, Util::WhoIsTheCaller(_ReturnAddress()));

    if (ret == 0)
    {
        {
#ifdef USE_LOCAL_MUTEX
            OwnedLockGuard lock(_localMutex, 999);
#endif

            const bool isCurrent = IsCurrentSwapchain(this);

            // Menu's last command list can still be running
            if (HasMenu(this, _handle))
            {
                WaitForGPUIdle(_device);
                MenuOverlayDx::CleanupRenderTarget(true, _handle);
            }

            if (State::Instance().currentSwapchain == this)
                State::Instance().currentSwapchain = nullptr;

            if (State::Instance().currentWrappedSwapchain == this)
                State::Instance().currentWrappedSwapchain = nullptr;

            if (State::Instance().currentRealSwapchain == _real)
                State::Instance().currentRealSwapchain = nullptr;

            if (isCurrent && State::Instance().currentCommandQueue == _device)
                State::Instance().currentCommandQueue = nullptr;

            if (isCurrent)
                State::Instance().SetSCBuffers({});

            auto fg = State::Instance().currentFG;
            if (isCurrent && fg != nullptr && fg->Mutex.getOwner() != 1 && fg->SwapchainContext() != nullptr)
            {
                fg->Deactivate();
                fg->ReleaseSwapchain(_handle);

                if (State::Instance().currentFGSwapchain != nullptr)
                    State::Instance().currentFGSwapchain = nullptr;
            }

            auto refCount = _owner->Release();

            // Disabled for now, cause issues with some games
            /*
            IDXGISwapChain* skSC = nullptr;
            if (_real->QueryInterface(IID_IUnwrappedDXGISwapChain, (void**) &skSC) == S_OK && skSC != nullptr)
            {
                skSC->Release();
                LOG_DEBUG("Found SK swapchain, skip releasing of main swapchain");
            }
            else
            {
                // Release real swapchain, otherwise it can cause issues when re-creating swapchain with same handle
                while (refCount > 0)
                {
                    LOG_DEBUG("Waiting for real swapchain to be released, refCount: {}", refCount);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    refCount = _real->Release();
                }
            }
            */

            LOG_DEBUG("Owner released, refCount: {}", refCount);
        }

        delete this;
    }

    return ret;
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData)
{
    return _real->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown)
{
    return _real->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData)
{
    return _real->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetParent(REFIID riid, void** ppParent)
{
    auto result = _real->GetParent(riid, ppParent);

    if (result == S_OK && Config::Instance()->DxgiFactoryWrapping.value_or_default())
        WrappedIDXGIFactory7::WrapParent(riid, ppParent, _ReturnAddress());

    return result;
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetDevice(REFIID riid, void** ppDevice)
{
    return _real->GetDevice(riid, ppDevice);
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::Present(UINT SyncInterval, UINT Flags)
{
    if (_real == nullptr)
        return DXGI_ERROR_DEVICE_REMOVED;

#ifdef USE_LOCAL_MUTEX
    OwnedLockGuard lock(_localMutex, 4);
#endif

    HRESULT result;

    if ((Flags & DXGI_PRESENT_TEST) == 0)
    {
        auto isCurrent = IsCurrentSwapchain(this) || State::Instance().currentSwapchain == nullptr;
        result = LocalPresent(_real, SyncInterval, Flags, nullptr, _device, _handle, _uwp, isCurrent);

        // When Reflex can't be used to limit, sleep in present
        // DXVK's DXGI presents through Vulkan where the limiter already sleeps
        if (isCurrent && !State::Instance().reflexLimitsFps && State::Instance().activeFgOutput == FGOutput::NoFG &&
            !IdentifyGpu::getPrimaryGpu().dxgiIsDxvk && !InputCommon::can_limit_fps())
            FrameLimit::sleep(false);
    }
    else
    {
        result = _real->Present(SyncInterval, Flags);
    }

    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface)
{
    auto result = _real->GetBuffer(Buffer, riid, ppSurface);
    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget)
{
    LOG_DEBUG("Fullscreen: {}, pTarget: {:X}, Caller: {}", Fullscreen, (size_t) pTarget,
              Util::WhoIsTheCaller(_ReturnAddress()));

    HRESULT result = S_OK;

    // Unlock the locked FG, currentFG can change meanwhile
    auto lockedFG = State::Instance().currentFG;
    bool ffxLock = false;

    {
        if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
        {

            if (lockedFG != nullptr && lockedFG->IsActive() && !lockedFG->IsPaused() &&
                lockedFG->Mutex.getOwner() != 3 && !lockedFG->Mutex.isLockedByThisThread())
            {
                LOG_TRACE("Waiting ffxMutex 7, current: {}", lockedFG->Mutex.getOwner());
                lockedFG->Mutex.lock(7);
                ffxLock = true;
                LOG_TRACE("Accuired ffxMutex: {}", lockedFG->Mutex.getOwner());
            }
            else
            {
                LOG_TRACE("Skipping ffxMutex, owner is already 3");
            }
        }

        result = _real->SetFullscreenState(Fullscreen, pTarget);

        if (result == S_OK && IsCurrentSwapchain(this))
            State::Instance().realExclusiveFullscreen = Fullscreen;

        if (FAILED(result))
            LOG_ERROR("result: {:X}", (UINT) result);
        else
            LOG_DEBUG("result: {:X}", result);
    }

    if (ffxLock)
    {
        LOG_TRACE("Releasing ffxMutex: {}", lockedFG->Mutex.getOwner());
        lockedFG->Mutex.unlockThis(7);
    }

    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget)
{
    return _real->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) { return _real->GetDesc(pDesc); }

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height,
                                                                DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    LOG_DEBUG("");

    // Owner 7 is a mode switch waiting for the window to handle this resize
    // Unlock the locked FG, currentFG can change meanwhile
    auto lockedFG = State::Instance().currentFG;
    bool ffxLock = false;
    if (lockedFG != nullptr && Config::Instance()->FGUseMutexForSwapchain.value_or_default() &&
        !(lockedFG->Mutex.getOwner() == 7 && InSendMessage()) && !lockedFG->Mutex.isLockedByThisThread())
    {
        LOG_TRACE("Waiting ffxMutex 3, current: {}", lockedFG->Mutex.getOwner());
        lockedFG->Mutex.lock(3);
        ffxLock = true;
        LOG_TRACE("Accuired ffxMutex: {}", lockedFG->Mutex.getOwner());
    }

    HRESULT result;
    DXGI_SWAP_CHAIN_DESC desc {};
    _real->GetDesc(&desc);

    // Menu's last command list can still be running
    WaitForGPUIdle(_device);

    if (IsCurrentSwapchain(this))
    {
        if (Config::Instance()->IsFGEnabled())
        {
            State::Instance().fgResetCapturedResources = true;
            State::Instance().fgOnlyUseCapturedResources = false;
            State::Instance().fgChanged = true;
        }

        State::Instance().scChanged = true;
    }

    if (HasMenu(this, _handle))
        MenuOverlayDx::CleanupRenderTarget(true, _handle);

    // V-Sync override creates flip model with ALLOW_TEARING, game's resize params must follow it
    if (Config::Instance()->OverrideVsync.value_or_default())
    {
        DXGI_SWAP_CHAIN_DESC scDesc {};
        if (_real->GetDesc(&scDesc) == S_OK)
        {
            LOG_DEBUG("Overriding flags");

            // ALLOW_TEARING can't change on resize
            SwapChainFlags = (SwapChainFlags & ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) |
                             (scDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);

            // Flip model needs 2 buffers, 0 keeps the current count
            if (BufferCount == 1 && (scDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD ||
                                     scDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL))
            {
                BufferCount = 2;
            }
        }
    }

    if (IsCurrentSwapchain(this))
        State::Instance().SCAllowTearing = (SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;

    LOG_DEBUG("BufferCount: {0}, Width: {1}, Height: {2}, NewFormat: {3}, SwapChainFlags: {4:X}", BufferCount, Width,
              Height, (UINT) NewFormat, SwapChainFlags);

    // Release swapchain backbuffers to prevent errors when resizing
    /*

    const bool outputRequiresRelease =
        State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgOutput == FGOutput::XeFG;

    if (outputRequiresRelease && State::Instance().currentFG != nullptr)
    {
        IDXGISwapChain* skSC = nullptr;
        if (_real->QueryInterface(IID_IUnwrappedDXGISwapChain, (void**) &skSC) == S_OK && skSC != nullptr)
        {
            skSC->Release();
            LOG_DEBUG("Found SK swapchain, skip releasing backbuffers of main swapchain");
        }
        else
        {
            LOG_DEBUG("Releasing backbuffers, count: {}", desc.BufferCount);

            for (UINT i = 0; i < desc.BufferCount; i++)
            {
                ID3D12Resource* backBuffer = nullptr;
                auto bbResult = _real->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

                if (bbResult == S_OK)
                {
                    backBuffer->AddRef();

                    auto refCount = backBuffer->Release();
                    refCount = backBuffer->Release();
                    LOG_DEBUG("Current backbuffer {}, RefCount {}", i, refCount);

                    while (refCount > 1 && refCount < 4294967200ul)
                    {
                        refCount = backBuffer->Release();
                        LOG_DEBUG("Releasing backbuffer {}, RefCount {}", i, refCount);
                    }

                    LOG_DEBUG("Backbuffer {}, RefCount {}", i, refCount);
                }
                else
                {
                    LOG_DEBUG("GetBuffer failed for index {}: {:X}", i, (UINT) bbResult);
                    break;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    */

#ifdef DXGI_DEBUG_ENABLED
    ReportDXGILiveObjects();

#ifdef ENABLE_DEBUG_LAYER_DX12
    ReportD3D12LiveObjects(State::Instance().currentD3D12Device);
#endif
#endif

    if (IsCurrentSwapchain(this))
        ForceHDRFormat(_device, NewFormat);

    if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
    {
        ScopedSkipHeapCapture skipHeapCapture {};

        _lastFlags = SwapChainFlags;
        result = _real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }
    else
    {
        _lastFlags = SwapChainFlags;
        result = _real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    // Game recreates its views before the next present, resource tracking filters them by swapchain size
    if (SUCCEEDED(result) && IsCurrentSwapchain(this) &&
        _real->GetDesc(&State::Instance().currentSwapchainDesc) != S_OK)
        LOG_WARN("Can't get swapchain desc!");

    if (result == DXGI_ERROR_DEVICE_REMOVED && State::Instance().currentD3D12Device != nullptr)
        Util::GetDeviceRemovedReason(State::Instance().currentD3D12Device);

#ifdef DXGI_DEBUG_ENABLED
    if (result != S_OK)
        ReadDxgiInfoQueue();
#endif

    if (SUCCEEDED(result) && State::Instance().currentFeature == nullptr)
    {
        State::Instance().screenWidth = static_cast<float>(Width);
        State::Instance().screenHeight = static_cast<float>(Height);
        State::Instance().lastMipBias = 100.0f;
        State::Instance().lastMipBiasMax = -100.0f;
    }

    // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
    // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
    if (SUCCEEDED(result) && UseForceHDR() && IsGameOutput(this, _handle))
        SetForcedHDRColorSpace(_real3);

    if (SUCCEEDED(result) && UseForceHDR() && IsCurrentSwapchain(this))
        UpdateSCBuffers(_real, _real1, BufferCount);

    LOG_DEBUG("result: {0:X}", (UINT) result);

    if (ffxLock)
    {
        LOG_TRACE("Releasing ffxMutex: {}", lockedFG->Mutex.getOwner());
        lockedFG->Mutex.unlockThis(3);
    }

    CheckForHdrOutput();

    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters)
{
    return _real->ResizeTarget(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetContainingOutput(IDXGIOutput** ppOutput)
{
    return _real->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats)
{
    return _real->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetLastPresentCount(UINT* pLastPresentCount)
{
    return _real->GetLastPresentCount(pLastPresentCount);
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc)
{
    return _real1->GetDesc1(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc)
{
    return _real1->GetFullscreenDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetHwnd(HWND* pHwnd) { return _real1->GetHwnd(pHwnd); }

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetCoreWindow(REFIID refiid, void** ppUnk)
{
    return _real1->GetCoreWindow(refiid, ppUnk);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::Present1(UINT SyncInterval, UINT Flags,
                                                           const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    if (_real1 == nullptr)
        return DXGI_ERROR_DEVICE_REMOVED;

#ifdef USE_LOCAL_MUTEX
    OwnedLockGuard lock(_localMutex, 5);
#endif

    HRESULT result;

    if ((Flags & DXGI_PRESENT_TEST) == 0)
    {
        auto isCurrent = IsCurrentSwapchain(this) || State::Instance().currentSwapchain == nullptr;
        result = LocalPresent(_real1, SyncInterval, Flags, pPresentParameters, _device, _handle, _uwp, isCurrent);

        // When Reflex can't be used to limit, sleep in present
        // DXVK's DXGI presents through Vulkan where the limiter already sleeps
        if (isCurrent && !State::Instance().reflexLimitsFps && State::Instance().activeFgOutput == FGOutput::NoFG &&
            !IdentifyGpu::getPrimaryGpu().dxgiIsDxvk && !InputCommon::can_limit_fps())
            FrameLimit::sleep(false);
    }
    else
    {
        result = _real1->Present1(SyncInterval, Flags, pPresentParameters);
    }

    return result;
}

BOOL STDMETHODCALLTYPE WrappedIDXGISwapChain4::IsTemporaryMonoSupported(void)
{
    return _real1->IsTemporaryMonoSupported();
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput)
{
    return _real1->GetRestrictToOutput(ppRestrictToOutput);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetBackgroundColor(const DXGI_RGBA* pColor)
{
    return _real1->SetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetBackgroundColor(DXGI_RGBA* pColor)
{
    return _real1->GetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetRotation(DXGI_MODE_ROTATION Rotation)
{
    return _real1->SetRotation(Rotation);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetRotation(DXGI_MODE_ROTATION* pRotation)
{
    return _real1->GetRotation(pRotation);
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetSourceSize(UINT Width, UINT Height)
{
    return _real2->SetSourceSize(Width, Height);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetSourceSize(UINT* pWidth, UINT* pHeight)
{
    return _real2->GetSourceSize(pWidth, pHeight);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetMaximumFrameLatency(UINT MaxLatency)
{
    return _real2->SetMaximumFrameLatency(MaxLatency);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetMaximumFrameLatency(UINT* pMaxLatency)
{
    return _real2->GetMaximumFrameLatency(pMaxLatency);
}

HANDLE STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetFrameLatencyWaitableObject(void)
{
    return _real2->GetFrameLatencyWaitableObject();
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix)
{
    return _real2->SetMatrixTransform(pMatrix);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix)
{
    return _real2->GetMatrixTransform(pMatrix);
}

UINT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetCurrentBackBufferIndex(void)
{
    auto index = _real3->GetCurrentBackBufferIndex();
    // LOG_TRACE("index: {}", index);
    return index;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                                         UINT* pColorSpaceSupport)
{
    return _real3->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    auto result = _real3->SetColorSpace1(ColorSpace);

    if (SUCCEEDED(result) && IsGameOutput(this, _handle))
    {
        UpdateOutputColorSpace(ColorSpace);

        CheckForHdrOutput();

        LOG_INFO("Output HDR Active: {}", State::Instance().hdrOutputActive);

        MenuOverlayDx::ApplyThemeStyle();
    }

    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height,
                                                                 DXGI_FORMAT Format, UINT SwapChainFlags,
                                                                 const UINT* pCreationNodeMask,
                                                                 IUnknown* const* ppPresentQueue)
{
    LOG_DEBUG("");

    // Owner 7 is a mode switch waiting for the window to handle this resize
    // Unlock the locked FG, currentFG can change meanwhile
    auto lockedFG = State::Instance().currentFG;
    bool ffxLock = false;
    if (lockedFG != nullptr && Config::Instance()->FGUseMutexForSwapchain.value_or_default() &&
        !(lockedFG->Mutex.getOwner() == 7 && InSendMessage()) && !lockedFG->Mutex.isLockedByThisThread())
    {
        LOG_TRACE("Waiting ffxMutex 3, current: {}", lockedFG->Mutex.getOwner());
        lockedFG->Mutex.lock(3);
        ffxLock = true;
        LOG_TRACE("Accuired ffxMutex: {}", lockedFG->Mutex.getOwner());
    }

    // Menu's last command list can still be running
    WaitForGPUIdle(_device);

    HRESULT result = E_FAIL;
    DXGI_SWAP_CHAIN_DESC desc {};
    _real->GetDesc(&desc);

    if (IsCurrentSwapchain(this))
    {
        if (Config::Instance()->IsFGEnabled())
        {
            State::Instance().fgResetCapturedResources = true;
            State::Instance().fgOnlyUseCapturedResources = false;
            State::Instance().fgChanged = true;
        }

        State::Instance().scChanged = true;
    }

    if (HasMenu(this, _handle))
        MenuOverlayDx::CleanupRenderTarget(true, _handle);

    // V-Sync override creates flip model with ALLOW_TEARING, game's resize params must follow it
    if (Config::Instance()->OverrideVsync.value_or_default())
    {
        DXGI_SWAP_CHAIN_DESC scDesc {};
        if (_real->GetDesc(&scDesc) == S_OK)
        {
            LOG_DEBUG("Overriding flags");

            // ALLOW_TEARING can't change on resize
            SwapChainFlags = (SwapChainFlags & ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) |
                             (scDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);

            // Flip model needs 2 buffers, 0 keeps the current count
            if (BufferCount == 1 && (scDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD ||
                                     scDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL))
            {
                BufferCount = 2;
            }
        }
    }

    if (IsCurrentSwapchain(this))
        State::Instance().SCAllowTearing = (SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;

    LOG_DEBUG("BufferCount: {}, Width: {}, Height: {}, NewFormat: {}, SwapChainFlags: {:X}", BufferCount, Width, Height,
              (UINT) Format, SwapChainFlags);

    if (IsCurrentSwapchain(this))
        ForceHDRFormat(_device, Format);

    bool skResized = false;

    // Release swapchain backbuffers to prevent errors when resizing
    const bool isUsingOptiFgFeature =
        State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgOutput == FGOutput::XeFG;

    if (isUsingOptiFgFeature && State::Instance().currentFG != nullptr)
    {
        IDXGISwapChain* skSC = nullptr;
        if (_real->QueryInterface(IID_IUnwrappedDXGISwapChain, (void**) &skSC) == S_OK && skSC != nullptr)
        {
            skSC->Release();
            skResized = true;
            LOG_DEBUG(
                "Found SK swapchain, skip releasing backbuffersand using ResizeBuffers instead of ResizeBuffers1");

#ifdef DXGI_DEBUG_ENABLED
            ReportDXGILiveObjects();

#ifdef ENABLE_DEBUG_LAYER_DX12
            ReportD3D12LiveObjects(State::Instance().currentD3D12Device);
#endif
#endif

            if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            {
                ScopedSkipHeapCapture skipHeapCapture {};

                _lastFlags = SwapChainFlags;
                result = _real3->ResizeBuffers(BufferCount, Width, Height, Format, SwapChainFlags);
            }
            else
            {
                _lastFlags = SwapChainFlags;
                result = _real3->ResizeBuffers(BufferCount, Width, Height, Format, SwapChainFlags);
            }
        }
        /*
        else
        {
            LOG_DEBUG("Releasing backbuffers, count: {}", desc.BufferCount);

            for (UINT i = 0; i < desc.BufferCount; i++)
            {
                ID3D12Resource* backBuffer = nullptr;
                auto bbResult = _real->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

                if (bbResult == S_OK)
                {
                    backBuffer->AddRef();

                    auto refCount = backBuffer->Release();
                    refCount = backBuffer->Release();
                    LOG_DEBUG("Current backbuffer {}, RefCount {}", i, refCount);

                    while (refCount > 1 && refCount < 4294967200ul)
                    {
                        refCount = backBuffer->Release();
                        LOG_DEBUG("Releasing backbuffer {}, RefCount {}", i, refCount);
                    }

                    LOG_DEBUG("Backbuffer {}, RefCount {}", i, refCount);
                }
                else
                {
                    LOG_DEBUG("GetBuffer failed for index {}: {:X}", i, (UINT) bbResult);
                    break;
                }
            }
        }
        */

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

#ifdef DXGI_DEBUG_ENABLED
    ReportDXGILiveObjects();

#ifdef ENABLE_DEBUG_LAYER_DX12
    ReportD3D12LiveObjects(State::Instance().currentD3D12Device);
#endif
#endif

    if (skResized)
    {
        LOG_DEBUG("Resized with ResizeBuffers, result: {:X}", (UINT) result);
    }
    else if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
    {
        ScopedSkipHeapCapture skipHeapCapture {};

        _lastFlags = SwapChainFlags;
        result = _real3->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask,
                                        ppPresentQueue);
    }
    else
    {
        _lastFlags = SwapChainFlags;
        result = _real3->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask,
                                        ppPresentQueue);
    }

    // Present queue only changes with a successful ResizeBuffers1, SK path resizes without it
    if (SUCCEEDED(result) && !skResized && ppPresentQueue != nullptr && *ppPresentQueue != nullptr)
    {
        // Unwrapped like on creation, Release matches currentCommandQueue with it
        IUnknown* realQueue = nullptr;
        if (!Util::CheckForRealObject(__FUNCTION__, *ppPresentQueue, &realQueue))
            realQueue = *ppPresentQueue;

        _device = realQueue;

        if (IsCurrentSwapchain(this))
            State::Instance().currentCommandQueue = (ID3D12CommandQueue*) realQueue;
    }

    // Game recreates its views before the next present, resource tracking filters them by swapchain size
    if (SUCCEEDED(result) && IsCurrentSwapchain(this) &&
        _real->GetDesc(&State::Instance().currentSwapchainDesc) != S_OK)
        LOG_WARN("Can't get swapchain desc!");

    if (result == DXGI_ERROR_DEVICE_REMOVED && State::Instance().currentD3D12Device != nullptr)
        Util::GetDeviceRemovedReason(State::Instance().currentD3D12Device);

#ifdef DXGI_DEBUG_ENABLED
    if (result != S_OK)
        ReadDxgiInfoQueue();
#endif

    if (SUCCEEDED(result) && State::Instance().currentFeature == nullptr)
    {
        State::Instance().screenWidth = static_cast<float>(Width);
        State::Instance().screenHeight = static_cast<float>(Height);
        State::Instance().lastMipBias = 100.0f;
        State::Instance().lastMipBiasMax = -100.0f;
    }

    // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
    // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
    if (SUCCEEDED(result) && UseForceHDR() && IsGameOutput(this, _handle))
        SetForcedHDRColorSpace(_real3);

    if (SUCCEEDED(result) && UseForceHDR() && IsCurrentSwapchain(this))
        UpdateSCBuffers(_real, _real1, BufferCount);

    LOG_DEBUG("result: {0:X}", (UINT) result);

    if (ffxLock)
    {
        LOG_TRACE("Releasing ffxMutex: {}", lockedFG->Mutex.getOwner());
        lockedFG->Mutex.unlockThis(3);
    }

    CheckForHdrOutput();

    return result;
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size,
                                                                 void* pMetaData)
{
    return _real4->SetHDRMetaData(Type, Size, pMetaData);
}
