#include "pch.h"
#include "reprojectionSwapchain.h"

#include <Util.h>
#include <Config.h>

#include <nvapi/fakenvapi.h>
#include <hooks/Reflex_Hooks.h>
#include <hooks/D3D12_Hooks.h>
#include <with_dx12/dx11_with_dx12_sync.h>

#include <menu/menu_overlay_dx.h>

#include <misc/FrameLimit.h>

#include <d3d11.h>
#include <d3d12.h>
#include <misc/IdentifyGpu.h>
#include <hooks/Xell_Hooks.h>

#include <magic_enum.hpp>

#ifdef LOW_LATENCY_INPUTS
#include <low_latency/input/input_antilag2.h>
#endif

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
static UINT64 _frameCounter = 0;
static double _lastFrameTime = 0;
static bool _dx11Device = false;
static bool _dx12Device = false;

const GUID IID_IUnwrappedDXGISwapChain = {
    0xe8a33b4a, 0x1405, 0x424c, { 0xae, 0x88, 0xd, 0x3e, 0x9d, 0x46, 0xc9, 0x14 }
};

static ID3D12Fence* resizeFence = nullptr;
static UINT64 resizeFenceValue = 0;
static HANDLE resizeFenceEvent = nullptr;

static void WaitForGPUIdle(IUnknown* object)
{
    if (State::Instance().currentD3D12Device == nullptr || object == nullptr)
        return;

    ID3D12CommandQueue* queue = nullptr;

    if (object->QueryInterface(IID_PPV_ARGS(&queue)) == S_OK)
    {
        LOG_DEBUG("Command queue obtained for GPU idle wait");
        queue->Release();
    }

    if (queue != nullptr && resizeFence != nullptr && resizeFenceEvent != nullptr)
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

        LOG_DEBUG("Waiting for GPU to finish before resizing buffers");

        resizeFenceValue++;
        queue->Signal(resizeFence, resizeFenceValue);

        if (resizeFence->GetCompletedValue() < resizeFenceValue)
        {
            resizeFence->SetEventOnCompletion(resizeFenceValue, resizeFenceEvent);
            // Max 5 sec
            auto waitResult = WaitForSingleObject(resizeFenceEvent, 5000);
            LOG_DEBUG("WaitForSingleObject result: {:X}", waitResult);
        }
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
                            const DXGI_PRESENT_PARAMETERS* pPresentParameters, IUnknown* pDevice, HWND hWnd, bool isUWP)
{
    HRESULT presentResult {};

    LOG_DEBUG("Calling original present");

    // swapchain present
    if (pPresentParameters == nullptr)
        presentResult = pSwapChain->Present(SyncInterval, Flags);
    else
        presentResult = ((IDXGISwapChain1*) pSwapChain)->Present1(SyncInterval, Flags, pPresentParameters);

    if (presentResult == S_OK)
    {
        LOG_DEBUG("Original present result: {:X}", (UINT) presentResult);
    }
    else
    {
        LOG_ERROR("Original present result: {:X}", (UINT) presentResult);

        if (presentResult == DXGI_ERROR_DEVICE_REMOVED && State::Instance().currentD3D12Device != nullptr)
            Util::GetDeviceRemovedReason(State::Instance().currentD3D12Device);
    }

    return presentResult;
}

ReprojectionDXGISwapChain::ReprojectionDXGISwapChain(IDXGISwapChain* real, IUnknown* pDevice, HWND hWnd, UINT flags,
                                                     bool isUWP)
    : _real(real), _device(pDevice), _handle(hWnd), _refcount(1), _uwp(isUWP)
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

    LOG_INFO("{} created, real: {:X}, refCount: {}", _id, (UINT64) real, refCount);
}

ReprojectionDXGISwapChain::~ReprojectionDXGISwapChain() {}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::QueryInterface(REFIID riid, void** ppvObject)
{
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
    else if (riid == __uuidof(ReprojectionDXGISwapChain))
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

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ReprojectionDXGISwapChain::AddRef()
{
    InterlockedIncrement(&_refcount);
    LOG_TRACE("Count: {}, caller: {}", _refcount, Util::WhoIsTheCaller(_ReturnAddress()));
    return _refcount;
}

ULONG STDMETHODCALLTYPE ReprojectionDXGISwapChain::Release()
{
    ULONG ret = InterlockedDecrement(&_refcount);

    LOG_TRACE("Count: {}, caller: {}", _refcount, Util::WhoIsTheCaller(_ReturnAddress()));

    if (ret == 0)
    {
        auto refCount = _real->Release();

        LOG_DEBUG("Real swapchain released, refCount: {}", refCount);

        delete this;
    }

    return ret;
}

//
HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData)
{
    return _real->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown)
{
    return _real->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData)
{
    return _real->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetParent(REFIID riid, void** ppParent)
{
    return _real->GetParent(riid, ppParent);
}

//
HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetDevice(REFIID riid, void** ppDevice)
{
    return _real->GetDevice(riid, ppDevice);
}

//
HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::Present(UINT SyncInterval, UINT Flags)
{
    if (_real == nullptr)
        return DXGI_ERROR_DEVICE_REMOVED;

    HRESULT result;

    if ((Flags & DXGI_PRESENT_TEST) == 0)
    {
        result = LocalPresent(_real, SyncInterval, Flags, nullptr, _device, _handle, _uwp);
    }
    else
    {
        result = _real->Present(SyncInterval, Flags);
    }

    return result;
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface)
{
    auto result = _real->GetBuffer(Buffer, riid, ppSurface);
    return result;
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget)
{
    HRESULT result = _real->SetFullscreenState(Fullscreen, pTarget);

    if (result != S_OK)
        LOG_ERROR("result: {:X}", (UINT) result);
    else
        LOG_DEBUG("result: {:X}", result);

    return result;
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget)
{
    return _real->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc)
{
    return _real->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height,
                                                                   DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    WaitForGPUIdle(_device);

    _lastFlags = SwapChainFlags;
    auto result = _real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);

    LOG_DEBUG("result: {0:X}", (UINT) result);

    return result;
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters)
{
    return _real->ResizeTarget(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetContainingOutput(IDXGIOutput** ppOutput)
{
    return _real->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats)
{
    return _real->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetLastPresentCount(UINT* pLastPresentCount)
{
    return _real->GetLastPresentCount(pLastPresentCount);
}

//
HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc)
{
    return _real1->GetDesc1(pDesc);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc)
{
    return _real1->GetFullscreenDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetHwnd(HWND* pHwnd) { return _real1->GetHwnd(pHwnd); }

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetCoreWindow(REFIID refiid, void** ppUnk)
{
    return _real1->GetCoreWindow(refiid, ppUnk);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::Present1(UINT SyncInterval, UINT Flags,
                                                              const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    if (_real1 == nullptr)
        return DXGI_ERROR_DEVICE_REMOVED;

    HRESULT result;

    if ((Flags & DXGI_PRESENT_TEST) == 0)
    {
        result = LocalPresent(_real1, SyncInterval, Flags, pPresentParameters, _device, _handle, _uwp);
    }
    else
    {
        result = _real1->Present1(SyncInterval, Flags, pPresentParameters);
    }

    return result;
}

BOOL STDMETHODCALLTYPE ReprojectionDXGISwapChain::IsTemporaryMonoSupported(void)
{
    return _real1->IsTemporaryMonoSupported();
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput)
{
    return _real1->GetRestrictToOutput(ppRestrictToOutput);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::SetBackgroundColor(const DXGI_RGBA* pColor)
{
    return _real1->SetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetBackgroundColor(DXGI_RGBA* pColor)
{
    return _real1->GetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::SetRotation(DXGI_MODE_ROTATION Rotation)
{
    return _real1->SetRotation(Rotation);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetRotation(DXGI_MODE_ROTATION* pRotation)
{
    return _real1->GetRotation(pRotation);
}

//
HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::SetSourceSize(UINT Width, UINT Height)
{
    return _real2->SetSourceSize(Width, Height);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetSourceSize(UINT* pWidth, UINT* pHeight)
{
    return _real2->GetSourceSize(pWidth, pHeight);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::SetMaximumFrameLatency(UINT MaxLatency)
{
    return _real2->SetMaximumFrameLatency(MaxLatency);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetMaximumFrameLatency(UINT* pMaxLatency)
{
    return _real2->GetMaximumFrameLatency(pMaxLatency);
}

HANDLE STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetFrameLatencyWaitableObject(void)
{
    return _real2->GetFrameLatencyWaitableObject();
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix)
{
    return _real2->SetMatrixTransform(pMatrix);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix)
{
    return _real2->GetMatrixTransform(pMatrix);
}

UINT STDMETHODCALLTYPE ReprojectionDXGISwapChain::GetCurrentBackBufferIndex(void)
{
    auto index = _real3->GetCurrentBackBufferIndex();
    // LOG_TRACE("index: {}", index);
    return index;
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                                            UINT* pColorSpaceSupport)
{
    return _real3->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    return _real3->SetColorSpace1(ColorSpace);
}

HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height,
                                                                    DXGI_FORMAT Format, UINT SwapChainFlags,
                                                                    const UINT* pCreationNodeMask,
                                                                    IUnknown* const* ppPresentQueue)
{
    WaitForGPUIdle(_device);

    _lastFlags = SwapChainFlags;
    auto result = _real3->ResizeBuffers(BufferCount, Width, Height, Format, SwapChainFlags);

    LOG_DEBUG("result: {0:X}", (UINT) result);

    return result;
}

//
HRESULT STDMETHODCALLTYPE ReprojectionDXGISwapChain::SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size,
                                                                    void* pMetaData)
{
    return _real4->SetHDRMetaData(Type, Size, pMetaData);
}
