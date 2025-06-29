#include "wrapped_swapchain.h"

#include <Util.h>
#include <Config.h>
#include "HooksDx.h"

#include <misc/FrameLimit.h>
#include <optional>

// Used RenderDoc's wrapped object as referance
// https://github.com/baldurk/renderdoc/blob/v1.x/renderdoc/driver/dxgi/dxgi_wrapped.cpp

static int scCount = 0;

WrappedIDXGISwapChain4::WrappedIDXGISwapChain4(IDXGISwapChain* real, IUnknown* pDevice, HWND hWnd,
                                               PFN_SC_Present renderTrig, PFN_SC_Clean clearTrig,
                                               PFN_SC_Release releaseTrig, bool isUWP)
    : m_pReal(real), Device(pDevice), Handle(hWnd), RenderTrig(renderTrig), ClearTrig(clearTrig),
      ReleaseTrig(releaseTrig), m_iRefcount(1), UWP(isUWP)
{
    id = ++scCount;
    LOG_INFO("{0} created, real: {1:X}", id, (UINT64) real);
    m_pReal->QueryInterface(IID_PPV_ARGS(&m_pReal1));
    m_pReal->QueryInterface(IID_PPV_ARGS(&m_pReal2));
    m_pReal->QueryInterface(IID_PPV_ARGS(&m_pReal3));
    m_pReal->QueryInterface(IID_PPV_ARGS(&m_pReal4));
    Device2 = Device;
}

WrappedIDXGISwapChain4::~WrappedIDXGISwapChain4() {}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::QueryInterface(REFIID riid, void** ppvObject)
{
    if (riid == __uuidof(IDXGISwapChain))
    {
        AddRef();
        *ppvObject = (IDXGISwapChain*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGISwapChain1))
    {
        if (m_pReal1)
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
        if (m_pReal2)
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
        if (m_pReal3)
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
        if (m_pReal4)
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
    else if (riid == __uuidof(this))
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

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedIDXGISwapChain4::AddRef()
{
    InterlockedIncrement(&m_iRefcount);
    LOG_DEBUG_ONLY("Count: {}", m_iRefcount);
    return m_iRefcount;
}

ULONG STDMETHODCALLTYPE WrappedIDXGISwapChain4::Release()
{
    auto ret = InterlockedDecrement(&m_iRefcount);
    LOG_DEBUG_ONLY("Count: {}", ret);

    ULONG relCount = 0;

    if (ret == 0)
    {
        if (ReleaseTrig != nullptr)
            ReleaseTrig(Handle);

        if (ClearTrig != nullptr)
            ClearTrig(true, Handle);

        if (m_pReal4 != nullptr)
            relCount = m_pReal4->Release();

        if (m_pReal3 != nullptr)
            relCount = m_pReal3->Release();

        if (m_pReal2 != nullptr)
            relCount = m_pReal2->Release();

        if (m_pReal1 != nullptr)
            relCount = m_pReal1->Release();

        if (m_pReal != nullptr)
            relCount = m_pReal->Release();

        LOG_INFO("{} released", id);

        delete this;
    }

    return ret;
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData)
{
    return m_pReal->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown)
{
    return m_pReal->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData)
{
    return m_pReal->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetParent(REFIID riid, void** ppParent)
{
    return m_pReal->GetParent(riid, ppParent);
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetDevice(REFIID riid, void** ppDevice)
{
    return m_pReal->GetDevice(riid, ppDevice);
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::Present(UINT SyncInterval, UINT Flags)
{
    if (m_pReal == nullptr)
        return DXGI_ERROR_DEVICE_REMOVED;

#ifdef USE_LOCAL_MUTEX
    OwnedLockGuard lock(_localMutex, 4);
#endif

    HRESULT result;

    if (!(Flags & DXGI_PRESENT_TEST || Flags & DXGI_PRESENT_RESTART) && RenderTrig != nullptr)
    {
        result = RenderTrig(m_pReal, SyncInterval, Flags, nullptr, Device, Handle, UWP);

        // When Reflex can't be used to limit, sleep in present
        if (!State::Instance().reflexLimitsFps)
            FrameLimit::sleep();
    }
    else
    {
        result = m_pReal->Present(SyncInterval, Flags);
    }

    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface)
{
    auto result = m_pReal->GetBuffer(Buffer, riid, ppSurface);
    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget)
{
    LOG_DEBUG("Fullscreen: {}, pTarget: {:X}", Fullscreen, (size_t) pTarget);
    HRESULT result = S_OK;

    std::optional<OwnedLockGuard> lock;
    {
#ifdef USE_LOCAL_MUTEX
        // dlssg calls this from present it seems
        // don't try to get a mutex when present owns it while dlssg mod is enabled
        if (!(_localMutex.getOwner() == 4 && Config::Instance()->FGType.value_or_default() == FGType::Nukems))
            OwnedLockGuard(_localMutex, 3);
#endif
        if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
        {

            if (State::Instance().currentFG != nullptr && State::Instance().currentFG->Mutex.getOwner() != 3)
            {
                LOG_TRACE("Waiting ffxMutex 3, current: {}", State::Instance().currentFG->Mutex.getOwner());
                lock.emplace(State::Instance().currentFG->Mutex, 3);
                LOG_TRACE("Accuired ffxMutex: {}", State::Instance().currentFG->Mutex.getOwner());
            }
            else
            {
                LOG_TRACE("Skipping ffxMutex, owner is already 3");
            }
        }

        result = m_pReal->SetFullscreenState(Fullscreen, pTarget);

        if (result != S_OK)
            LOG_ERROR("result: {:X}", (UINT) result);
        else
            LOG_DEBUG("result: {:X}", result);

        /*
        if (Config::Instance()->FGEnabled.value_or_default())
        {
            State::Instance().FGresetCapturedResources = true;
            State::Instance().FGonlyUseCapturedResources = false;

            if (State::Instance().currentFeature != nullptr)
                State::Instance().FGchanged = true;
        }

        /*
        if (ClearTrig != nullptr)
            ClearTrig(true, Handle);

        State::Instance().SCchanged = true;
        State::Instance().SCbuffers.clear();

        UINT bc = 0;
        if (m_pReal1 != nullptr)
        {
            DXGI_SWAP_CHAIN_DESC1 desc{};

            if (m_pReal1->GetDesc1(&desc) == S_OK)
                bc = desc.BufferCount;
        }

        for (size_t i = 0; i < bc; i++)
        {
            IUnknown* buffer;

            if (m_pReal->GetBuffer(i, IID_PPV_ARGS(&buffer)) == S_OK)
            {
                State::Instance().SCbuffers.push_back(buffer);
                buffer->Release();
            }
        }
        */
    }

    if (lock)
    {
        LOG_TRACE("Releasing ffxMutex: {}", State::Instance().currentFG->Mutex.getOwner());
    }

    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget)
{
    return m_pReal->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc)
{
    return m_pReal->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height,
                                                                DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    LOG_DEBUG("");

#ifdef USE_LOCAL_MUTEX
    // dlssg calls this from present it seems
    // don't try to get a mutex when present owns it while dlssg mod is enabled
    if (!(_localMutex.getOwner() == 4 && Config::Instance()->FGType.value_or_default() == FGType::Nukems))
        OwnedLockGuard(_localMutex, 1);
#endif

    std::optional<OwnedLockGuard> lock;
    if (State::Instance().currentFG != nullptr && Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Waiting ffxMutex 3, current: {}", State::Instance().currentFG->Mutex.getOwner());
        lock.emplace(State::Instance().currentFG->Mutex, 3);
        LOG_TRACE("Accuired ffxMutex: {}", State::Instance().currentFG->Mutex.getOwner());
    }

    HRESULT result;
    DXGI_SWAP_CHAIN_DESC desc {};
    m_pReal->GetDesc(&desc);

    if (Config::Instance()->FGEnabled.value_or_default())
    {
        State::Instance().FGresetCapturedResources = true;
        State::Instance().FGonlyUseCapturedResources = false;

        if (State::Instance().currentFeature != nullptr)
            State::Instance().FGchanged = true;
    }

    if (ClearTrig != nullptr)
        ClearTrig(true, Handle);

    State::Instance().SCchanged = true;

    LOG_DEBUG("BufferCount: {0}, Width: {1}, Height: {2}, NewFormat: {3}, SwapChainFlags: {4:X}", BufferCount, Width,
              Height, (UINT) NewFormat, SwapChainFlags);

    result = m_pReal->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
    if (result == S_OK && State::Instance().currentFeature == nullptr)
    {
        State::Instance().screenWidth = Width;
        State::Instance().screenHeight = Height;
        State::Instance().lastMipBias = 100.0f;
        State::Instance().lastMipBiasMax = -100.0f;
    }

    // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
    // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
    if (Config::Instance()->ForceHDR.value_or_default())
    {
        LOG_INFO("Force HDR on");

        do
        {
            if (m_pReal3 == nullptr)
                break;

            NewFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            DXGI_COLOR_SPACE_TYPE hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

            if (Config::Instance()->UseHDR10.value_or_default())
            {
                NewFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
                hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            }

            UINT css = 0;

            result = m_pReal3->CheckColorSpaceSupport(hdrCS, &css);

            if (result != S_OK)
            {
                LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
                break;
            }

            if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
            {
                result = m_pReal3->SetColorSpace1(hdrCS);

                if (result != S_OK)
                {
                    LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
                    break;
                }
            }

            LOG_INFO("HDR format and color space are set");

        } while (false);
    }

    State::Instance().SCbuffers.clear();
    UINT bc = BufferCount;
    if (bc == 0 && m_pReal1 != nullptr)
    {
        DXGI_SWAP_CHAIN_DESC1 desc {};

        if (m_pReal1->GetDesc1(&desc) == S_OK)
            bc = desc.BufferCount;
    }

    for (size_t i = 0; i < bc; i++)
    {
        IUnknown* buffer;

        if (m_pReal->GetBuffer(i, IID_PPV_ARGS(&buffer)) == S_OK)
        {
            State::Instance().SCbuffers.push_back(buffer);
            buffer->Release();
        }
    }

    LOG_DEBUG("result: {0:X}", (UINT) result);

    if (lock)
    {
        LOG_TRACE("Releasing ffxMutex: {}", State::Instance().currentFG->Mutex.getOwner());
    }

    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters)
{
    return m_pReal->ResizeTarget(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetContainingOutput(IDXGIOutput** ppOutput)
{
    return m_pReal->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats)
{
    return m_pReal->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetLastPresentCount(UINT* pLastPresentCount)
{
    return m_pReal->GetLastPresentCount(pLastPresentCount);
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc)
{
    return m_pReal1->GetDesc1(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc)
{
    return m_pReal1->GetFullscreenDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetHwnd(HWND* pHwnd) { return m_pReal1->GetHwnd(pHwnd); }

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetCoreWindow(REFIID refiid, void** ppUnk)
{
    return m_pReal1->GetCoreWindow(refiid, ppUnk);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::Present1(UINT SyncInterval, UINT Flags,
                                                           const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    if (m_pReal1 == nullptr)
        return DXGI_ERROR_DEVICE_REMOVED;

#ifdef USE_LOCAL_MUTEX
    OwnedLockGuard lock(_localMutex, 5);
#endif

    HRESULT result;

    if (!(Flags & DXGI_PRESENT_TEST || Flags & DXGI_PRESENT_RESTART) && RenderTrig != nullptr)
        result = RenderTrig(m_pReal1, SyncInterval, Flags, pPresentParameters, Device, Handle, UWP);
    else
        result = m_pReal1->Present1(SyncInterval, Flags, pPresentParameters);

    return result;
}

BOOL STDMETHODCALLTYPE WrappedIDXGISwapChain4::IsTemporaryMonoSupported(void)
{
    return m_pReal1->IsTemporaryMonoSupported();
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput)
{
    return m_pReal1->GetRestrictToOutput(ppRestrictToOutput);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetBackgroundColor(const DXGI_RGBA* pColor)
{
    return m_pReal1->SetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetBackgroundColor(DXGI_RGBA* pColor)
{
    return m_pReal1->GetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetRotation(DXGI_MODE_ROTATION Rotation)
{
    return m_pReal1->SetRotation(Rotation);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetRotation(DXGI_MODE_ROTATION* pRotation)
{
    return m_pReal1->GetRotation(pRotation);
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetSourceSize(UINT Width, UINT Height)
{
    return m_pReal2->SetSourceSize(Width, Height);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetSourceSize(UINT* pWidth, UINT* pHeight)
{
    return m_pReal2->GetSourceSize(pWidth, pHeight);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetMaximumFrameLatency(UINT MaxLatency)
{
    return m_pReal2->SetMaximumFrameLatency(MaxLatency);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetMaximumFrameLatency(UINT* pMaxLatency)
{
    return m_pReal2->GetMaximumFrameLatency(pMaxLatency);
}

HANDLE STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetFrameLatencyWaitableObject(void)
{
    return m_pReal2->GetFrameLatencyWaitableObject();
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix)
{
    return m_pReal2->SetMatrixTransform(pMatrix);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix)
{
    return m_pReal2->GetMatrixTransform(pMatrix);
}

UINT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetCurrentBackBufferIndex(void)
{
    return m_pReal3->GetCurrentBackBufferIndex();
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                                         UINT* pColorSpaceSupport)
{
    return m_pReal3->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    State::Instance().isHdrActive = ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                                    ColorSpace == DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020 ||
                                    ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020 ||
                                    ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

    return m_pReal3->SetColorSpace1(ColorSpace);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height,
                                                                 DXGI_FORMAT Format, UINT SwapChainFlags,
                                                                 const UINT* pCreationNodeMask,
                                                                 IUnknown* const* ppPresentQueue)
{
    LOG_DEBUG("");

#ifdef USE_LOCAL_MUTEX
    // dlssg calls this from present it seems
    // don't try to get a mutex when present owns it while dlssg mod is enabled
    if (!(_localMutex.getOwner() == 4 && Config::Instance()->FGType.value_or_default() == FGType::Nukems))
        OwnedLockGuard(_localMutex, 2);
#endif

    std::optional<OwnedLockGuard> lock;
    if (State::Instance().activeFgType == OptiFG && Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Waiting ffxMutex 3, current: {}", State::Instance().currentFG->Mutex.getOwner());
        lock.emplace(State::Instance().currentFG->Mutex, 3);
        LOG_TRACE("Accuired ffxMutex: {}", State::Instance().currentFG->Mutex.getOwner());
    }

    HRESULT result;
    DXGI_SWAP_CHAIN_DESC desc {};
    m_pReal->GetDesc(&desc);

    if (Config::Instance()->FGEnabled.value_or_default())
    {
        State::Instance().FGresetCapturedResources = true;
        State::Instance().FGonlyUseCapturedResources = false;

        if (State::Instance().currentFeature != nullptr)
            State::Instance().FGchanged = true;
    }

    if (ClearTrig != nullptr)
        ClearTrig(true, Handle);

    State::Instance().SCchanged = true;

    LOG_DEBUG(
        "BufferCount: {0}, Width: {1}, Height: {2}, NewFormat: {3}, SwapChainFlags: {4:X}, pCreationNodeMask: {5}",
        BufferCount, Width, Height, (UINT) Format, SwapChainFlags, *pCreationNodeMask);

    result =
        m_pReal3->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
    if (result == S_OK && State::Instance().currentFeature == nullptr)
    {
        State::Instance().screenWidth = Width;
        State::Instance().screenHeight = Height;
        State::Instance().lastMipBias = 100.0f;
        State::Instance().lastMipBiasMax = -100.0f;
    }

    // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
    // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
    if (Config::Instance()->ForceHDR.value_or_default())
    {
        LOG_INFO("Force HDR on");

        do
        {
            Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            DXGI_COLOR_SPACE_TYPE hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

            if (Config::Instance()->UseHDR10.value_or_default())
            {
                Format = DXGI_FORMAT_R10G10B10A2_UNORM;
                hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            }

            UINT css = 0;

            auto result = m_pReal3->CheckColorSpaceSupport(hdrCS, &css);

            if (result != S_OK)
            {
                LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
                break;
            }

            if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
            {
                result = m_pReal3->SetColorSpace1(hdrCS);

                if (result != S_OK)
                {
                    LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
                    break;
                }
            }

            LOG_INFO("HDR format and color space are set");

        } while (false);
    }

    State::Instance().SCbuffers.clear();
    UINT bc = BufferCount;
    if (bc == 0 && m_pReal1 != nullptr)
    {
        DXGI_SWAP_CHAIN_DESC1 desc {};

        if (m_pReal1->GetDesc1(&desc) == S_OK)
            bc = desc.BufferCount;
    }

    for (size_t i = 0; i < bc; i++)
    {
        IUnknown* buffer;

        if (m_pReal->GetBuffer(i, IID_PPV_ARGS(&buffer)) == S_OK)
        {
            State::Instance().SCbuffers.push_back(buffer);
            buffer->Release();
        }
    }

    LOG_DEBUG("result: {0:X}", (UINT) result);

    if (lock)
    {
        LOG_TRACE("Releasing ffxMutex: {}", State::Instance().currentFG->Mutex.getOwner());
    }

    return result;
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size,
                                                                 void* pMetaData)
{
    return m_pReal4->SetHDRMetaData(Type, Size, pMetaData);
}
