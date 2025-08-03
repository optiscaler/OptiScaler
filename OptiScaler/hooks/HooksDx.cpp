#include "HooksDx.h"

#include <Util.h>
#include <Config.h>

#include "wrapped_swapchain.h"

#include <hudfix/Hudfix_Dx12.h>
#include <menu/menu_overlay_dx.h>
#include <framegen/ffx/FSRFG_Dx12.h>
#include <resource_tracking/ResTrack_Dx12.h>

#include <proxies/Dxgi_Proxy.h>
#include <proxies/D3D12_Proxy.h>
#include <proxies/KernelBase_Proxy.h>
#include <proxies/IGDExt_Proxy.h>

#include <nvapi/fakenvapi.h>
#include <nvapi/ReflexHooks.h>

#include <detours/detours.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_framegeneration.h>

#include <d3d11on12.h>

#pragma intrinsic(_ReturnAddress)

#pragma region FG definitions

// Is FG mutex accuired for Half/Full sync?
static bool _lockAccuiredForHalfOrFull = false;

// Target frame/present could for releasing the FG mutex
static UINT64 _releaseMutexTargetFrame = 0;

// To prevent recursive FG swapchain creation
static bool _skipFGSwapChainCreation = false;

// Swapchain frame counter
static UINT64 _frameCounter = 0;
static double _lastFrameTime = 0.0;
static bool _fgPresentCalled = false;

#pragma endregion

// dxgi stuff
typedef HRESULT (*PFN_EnumAdapterByGpuPreference2)(IDXGIFactory6* This, UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference,
                                                   REFIID riid, IUnknown** ppvAdapter);
typedef HRESULT (*PFN_EnumAdapterByLuid2)(IDXGIFactory4* This, LUID AdapterLuid, REFIID riid, IUnknown** ppvAdapter);
typedef HRESULT (*PFN_EnumAdapters12)(IDXGIFactory1* This, UINT Adapter, IUnknown** ppAdapter);
typedef HRESULT (*PFN_EnumAdapters2)(IDXGIFactory* This, UINT Adapter, IUnknown** ppAdapter);

typedef HRESULT (*PFN_CreateSwapChain)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
typedef HRESULT (*PFN_CreateSwapChainForHwnd)(IDXGIFactory*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                              const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
typedef HRESULT (*PFN_CreateSwapChainForCoreWindow)(IDXGIFactory2*, IUnknown* pDevice, IUnknown* pWindow,
                                                    DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput,
                                                    IDXGISwapChain1** ppSwapChain);

typedef HRESULT (*PFN_Present)(void* This, UINT SyncInterval, UINT Flags);

static DxgiProxy::PFN_CreateDxgiFactory o_CreateDXGIFactory = nullptr;
static DxgiProxy::PFN_CreateDxgiFactory1 o_CreateDXGIFactory1 = nullptr;
static DxgiProxy::PFN_CreateDxgiFactory2 o_CreateDXGIFactory2 = nullptr;

static PFN_EnumAdapters2 o_EnumAdapters = nullptr;
static PFN_EnumAdapters12 o_EnumAdapters1 = nullptr;
static PFN_EnumAdapterByLuid2 o_EnumAdapterByLuid = nullptr;
static PFN_EnumAdapterByGpuPreference2 o_EnumAdapterByGpuPreference = nullptr;

static PFN_CreateSwapChain oCreateSwapChain = nullptr;
static PFN_CreateSwapChainForHwnd oCreateSwapChainForHwnd = nullptr;
static PFN_CreateSwapChainForCoreWindow oCreateSwapChainForCoreWindow = nullptr;

static PFN_Present o_FGSCPresent = nullptr;

static bool skipHighPerfCheck = false;

// DirectX
typedef void (*PFN_CreateSampler)(ID3D12Device* device, const D3D12_SAMPLER_DESC* pDesc,
                                  D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

typedef HRESULT (*PFN_CheckFeatureSupport)(ID3D12Device* device, D3D12_FEATURE Feature, void* pFeatureSupportData,
                                           UINT FeatureSupportDataSize);

typedef HRESULT (*PFN_CreateCommittedResource)(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* pHeapProperties,
                                               D3D12_HEAP_FLAGS HeapFlags, D3D12_RESOURCE_DESC* pDesc,
                                               D3D12_RESOURCE_STATES InitialResourceState,
                                               const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource,
                                               void** ppvResource);

typedef HRESULT (*PFN_CreatePlacedResource)(ID3D12Device* device, ID3D12Heap* pHeap, UINT64 HeapOffset,
                                            D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState,
                                            const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid,
                                            void** ppvResource);

typedef D3D12_RESOURCE_ALLOCATION_INFO (*PFN_GetResourceAllocationInfo)(ID3D12Device* device, UINT visibleMask,
                                                                        UINT numResourceDescs,
                                                                        D3D12_RESOURCE_DESC* pResourceDescs);

typedef HRESULT (*PFN_CreateSamplerState)(ID3D11Device* This, const D3D11_SAMPLER_DESC* pSamplerDesc,
                                          ID3D11SamplerState** ppSamplerState);

typedef ULONG (*PFN_Release)(IUnknown* This);

static PFN_CreateSampler o_CreateSampler = nullptr;
static PFN_CheckFeatureSupport o_CheckFeatureSupport = nullptr;
static PFN_CreateCommittedResource o_CreateCommittedResource = nullptr;
static PFN_CreatePlacedResource o_CreatePlacedResource = nullptr;
static PFN_GetResourceAllocationInfo o_GetResourceAllocationInfo = nullptr;

static D3d12Proxy::PFN_D3D12CreateDevice o_D3D12CreateDevice = nullptr;
static D3d12Proxy::PFN_D3D12SerializeRootSignature o_D3D12SerializeRootSignature = nullptr;
static D3d12Proxy::PFN_D3D12SerializeVersionedRootSignature o_D3D12SerializeVersionedRootSignature = nullptr;
static PFN_Release o_D3D12DeviceRelease = nullptr;

static PFN_D3D11_CREATE_DEVICE o_D3D11CreateDevice = nullptr;
static PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN o_D3D11CreateDeviceAndSwapChain = nullptr;
static PFN_CreateSamplerState o_CreateSamplerState = nullptr;
static PFN_D3D11ON12_CREATE_DEVICE o_D3D11On12CreateDevice = nullptr;

static ID3D12Device* _intelD3D12Device = nullptr;
static ULONG _intelD3D12DeviceRefTarget = 0;

// menu
static bool _dx11Device = false;
static bool _dx12Device = false;

// for dx11
static ID3D11DeviceContext* _d3d11DeviceContext = nullptr;
static ID3D11Device* d3d11Device = nullptr;
static ID3D11Device* d3d11on12Device = nullptr;

// status
static bool _isInited = false;
static bool _d3d12Captured = false;

static void hkCreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* pDesc,
                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

static HRESULT hkCheckFeatureSupport(ID3D12Device* device, D3D12_FEATURE Feature, void* pFeatureSupportData,
                                     UINT FeatureSupportDataSize);

static ULONG hkD3D12DeviceRelease(IUnknown* device);

static HRESULT hkCreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* pHeapProperties,
                                         D3D12_HEAP_FLAGS HeapFlags, D3D12_RESOURCE_DESC* pDesc,
                                         D3D12_RESOURCE_STATES InitialResourceState,
                                         const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource,
                                         void** ppvResource);

static HRESULT hkCreatePlacedResource(ID3D12Device* device, ID3D12Heap* pHeap, UINT64 HeapOffset,
                                      D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState,
                                      const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource);

static D3D12_RESOURCE_ALLOCATION_INFO hkGetResourceAllocationInfo(ID3D12Device* device, UINT visibleMask,
                                                                  UINT numResourceDescs,
                                                                  D3D12_RESOURCE_DESC* pResourceDescs);

static HRESULT hkCreateSamplerState(ID3D11Device* This, const D3D11_SAMPLER_DESC* pSamplerDesc,
                                    ID3D11SamplerState** ppSamplerState);
static HRESULT hkEnumAdapters(IDXGIFactory* This, UINT Adapter, IUnknown** ppAdapter);
static HRESULT hkEnumAdapters1(IDXGIFactory1* This, UINT Adapter, IUnknown** ppAdapter);
static HRESULT hkEnumAdapterByLuid(IDXGIFactory4* This, LUID AdapterLuid, REFIID riid, IUnknown** ppvAdapter);
static HRESULT hkEnumAdapterByGpuPreference(IDXGIFactory6* This, UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference,
                                            REFIID riid, IUnknown** ppvAdapter);
static HRESULT hkCreateDXGIFactory(REFIID riid, IDXGIFactory** ppFactory);
static HRESULT hkCreateDXGIFactory1(REFIID riid, IDXGIFactory1** ppFactory);
static HRESULT hkCreateDXGIFactory2(UINT Flags, REFIID riid, IDXGIFactory2** ppFactory);

static IID streamlineRiid {};
static bool CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject)
{
#ifdef CHECK_FOR_SL_PROXY_OBJECTS
    if (streamlineRiid.Data1 == 0)
    {
        auto iidResult = IIDFromString(L"{ADEC44E2-61F0-45C3-AD9F-1B37379284FF}", &streamlineRiid);

        if (iidResult != S_OK)
            return false;
    }

    auto qResult = pObject->QueryInterface(streamlineRiid, (void**) ppRealObject);

    if (qResult == S_OK && *ppRealObject != nullptr)
    {
        LOG_INFO("{} Streamline proxy found!", functionName);
        (*ppRealObject)->Release();
        return true;
    }
#endif

    return false;
}

#pragma region Callbacks for wrapped swapchain

static HRESULT hkFGPresent(void* This, UINT SyncInterval, UINT Flags)
{
    if (State::Instance().isShuttingDown)
        return o_FGSCPresent(This, SyncInterval, Flags);

    auto willPresent = !(Flags & DXGI_PRESENT_TEST || Flags & DXGI_PRESENT_RESTART);

    if (willPresent)
    {
        _fgPresentCalled = true;

        double ftDelta = 0.0f;

        auto now = Util::MillisecondsNow();

        if (_lastFrameTime != 0)
            ftDelta = now - _lastFrameTime;

        _lastFrameTime = now;
        State::Instance().lastFrameTime = ftDelta;

        LOG_DEBUG("_frameCounter: {}, flags: {:X}, Frametime: {}", _frameCounter, Flags, ftDelta);
    }

    IFGFeature_Dx12* fg = State::Instance().currentFG;

    if (willPresent)
    {
        if (State::Instance().activeFgInput == FGInput::Upscaler && HooksDx::dx12UpscaleTrig &&
            HooksDx::readbackBuffer != nullptr && HooksDx::queryHeap != nullptr &&
            State::Instance().currentCommandQueue != nullptr)
        {
            UINT64* timestampData;
            HooksDx::readbackBuffer->Map(0, nullptr, reinterpret_cast<void**>(&timestampData));

            if (timestampData != nullptr)
            {
                // Get the GPU timestamp frequency (ticks per second)
                UINT64 gpuFrequency;
                State::Instance().currentCommandQueue->GetTimestampFrequency(&gpuFrequency);

                // Calculate elapsed time in milliseconds
                UINT64 startTime = timestampData[0];
                UINT64 endTime = timestampData[1];
                double elapsedTimeMs = (endTime - startTime) / static_cast<double>(gpuFrequency) * 1000.0;

                // filter out posibly wrong measured high values
                if (elapsedTimeMs < 100.0)
                {
                    State::Instance().frameTimeMutex.lock();
                    State::Instance().upscaleTimes.push_back(elapsedTimeMs);
                    State::Instance().upscaleTimes.pop_front();
                    State::Instance().frameTimeMutex.unlock();
                }
            }
            else
            {
                LOG_WARN("timestampData is null!");
            }

            // Unmap the buffer
            HooksDx::readbackBuffer->Unmap(0, nullptr);
            HooksDx::dx12UpscaleTrig = false;
        }
    }

    auto lockAccuired = false;
    if (willPresent && fg != nullptr && fg->IsActive() &&
        Config::Instance()->FGUseMutexForSwapchain.value_or_default() && fg->Mutex.getOwner() != 2)
    {
        LOG_TRACE("Waiting FG->Mutex 2, current: {}", fg->Mutex.getOwner());
        fg->Mutex.lock(2);

        // If half or full sync is active, we need to release the mutex after 1 or 2 frames at Present
        lockAccuired = !Config::Instance()->FGHudfixHalfSync.value_or_default() &&
                       !Config::Instance()->FGHudfixFullSync.value_or_default();
        _lockAccuiredForHalfOrFull = !lockAccuired;

        if (Config::Instance()->FGDebugView.value_or_default() ||
            Config::Instance()->FGHudfixHalfSync.value_or_default())
            _releaseMutexTargetFrame = _frameCounter + 1; // For debug 1 frame
        else
            _releaseMutexTargetFrame = _frameCounter + 2; // For FG 2 frames

        LOG_TRACE("Accuired FG->Mutex: {}, fgMutexReleaseFrame: {}", fg->Mutex.getOwner(), _releaseMutexTargetFrame);
    }

    if (willPresent && State::Instance().currentCommandQueue != nullptr &&
        State::Instance().activeFgInput == FGInput::Upscaler && fg != nullptr && fg->IsActive() &&
        fg->TargetFrame() < fg->FrameCount() && fg->LastDispatchedFrame() != fg->FrameCount() &&
        fg->UpscalerInputsReady())
    {
        State::Instance().fgTrigSource = "Present";
        fg->Present();

        LOG_DEBUG("Dispatch hudless fg");

        if (fg->Dispatch(nullptr, false, State::Instance().lastFrameTime))
        {
            auto cmdList = fg->GetCommandList();
            State::Instance().currentCommandQueue->ExecuteCommandLists(1, &cmdList);
        }
    }

    // if (willPresent && State::Instance().currentCommandQueue != nullptr &&
    //     State::Instance().activeFgInput == FGInput::DLSSG && fg != nullptr)
    //{
    //     auto cmdList = fg->GetCommandList();

    //    if (cmdList)
    //    {
    //        LOG_DEBUG("Executing cmdlist for Streamline inputs: {:X}", (uint64_t) cmdList);
    //        State::Instance().currentCommandQueue->ExecuteCommandLists(1, &cmdList);
    //    }
    //}

    if (willPresent && State::Instance().activeFgInput == FGInput::Upscaler)
    {
        ResTrack_Dx12::ClearPossibleHudless();
        Hudfix_Dx12::PresentStart();
    }

    HRESULT result;
    result = o_FGSCPresent(This, SyncInterval, Flags);
    LOG_DEBUG("Result: {:X}", result);

    Hudfix_Dx12::PresentEnd();

    if (lockAccuired && Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Releasing FG->Mutex: {}", fg->Mutex.getOwner());
        fg->Mutex.unlockThis(2);
    }

    return result;
}

static HRESULT hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags,
                         const DXGI_PRESENT_PARAMETERS* pPresentParameters, IUnknown* pDevice, HWND hWnd, bool isUWP)
{
    if (State::Instance().isShuttingDown)
    {
        if (pPresentParameters == nullptr)
            return pSwapChain->Present(SyncInterval, Flags);
        else
            return ((IDXGISwapChain1*) pSwapChain)->Present1(SyncInterval, Flags, pPresentParameters);
    }

    LOG_DEBUG("{}", _frameCounter);

    HRESULT presentResult;

    auto willPresent = !(Flags & DXGI_PRESENT_TEST || Flags & DXGI_PRESENT_RESTART);

    if (State::Instance().activeFgInput != FGInput::Upscaler && willPresent)
    {
        double ftDelta = 0.0f;

        auto now = Util::MillisecondsNow();

        if (_lastFrameTime != 0)
            ftDelta = now - _lastFrameTime;

        _lastFrameTime = now;
        State::Instance().lastFrameTime = ftDelta;
        LOG_DEBUG("Frametime: {:0.3f} ms", ftDelta);
    }

    ID3D11Device* device = nullptr;
    ID3D12Device* device12 = nullptr;
    ID3D12CommandQueue* cq = nullptr;

    // try to obtain directx objects and find the path
    if (pDevice->QueryInterface(IID_PPV_ARGS(&device)) == S_OK)
    {
        if (!_dx11Device)
            LOG_DEBUG("D3D11Device captured");

        _dx11Device = true;
        State::Instance().swapchainApi = DX11;
        State::Instance().currentD3D11Device == device;

        if (!State::Instance().DeviceAdapterNames.contains(device))
        {
            IDXGIDevice* dxgiDevice = nullptr;
            auto qResult = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));

            if (qResult == S_OK)
            {
                IDXGIAdapter* dxgiAdapter = nullptr;
                qResult = dxgiDevice->GetAdapter(&dxgiAdapter);

                if (qResult == S_OK)
                {
                    State::Instance().skipSpoofing = true;

                    std::wstring szName;
                    DXGI_ADAPTER_DESC desc {};

                    if (dxgiAdapter->GetDesc(&desc) == S_OK)
                    {
                        szName = desc.Description;
                        auto adapterDesc = wstring_to_string(szName);
                        LOG_INFO("Adapter Desc: {}", adapterDesc);
                        State::Instance().DeviceAdapterNames[device] = adapterDesc;
                    }
                    else
                    {
                        LOG_ERROR("GetDesc: {:X}", (UINT) qResult);
                    }

                    State::Instance().skipSpoofing = false;
                }
                else
                {
                    LOG_ERROR("GetAdapter: {:X}", (UINT) qResult);
                }

                if (dxgiAdapter != nullptr)
                    dxgiAdapter->Release();
            }
            else
            {
                LOG_ERROR("QueryInterface: {:X}", (UINT) qResult);
            }

            if (dxgiDevice != nullptr)
                dxgiDevice->Release();
        }
    }
    else if (pDevice->QueryInterface(IID_PPV_ARGS(&cq)) == S_OK)
    {
        if (!_dx12Device)
            LOG_DEBUG("D3D12CommandQueue captured");

        State::Instance().swapchainApi = DX12;
        State::Instance().currentCommandQueue = cq;

        if (cq->GetDevice(IID_PPV_ARGS(&device12)) == S_OK)
        {
            if (!_dx12Device)
                LOG_DEBUG("D3D12Device captured");

            _dx12Device = true;
            State::Instance().currentD3D12Device = device12;
        }
    }

    if (_fgPresentCalled)
        _fgPresentCalled = false;

    IFGFeature_Dx12* fg = State::Instance().currentFG;

    if (fg != nullptr)
        ReflexHooks::update(fg->IsActive(), false);
    else
        ReflexHooks::update(false, false);

    // Upscaler GPU time computation
    if (State::Instance().activeFgInput != FGInput::Upscaler && HooksDx::dx12UpscaleTrig &&
        HooksDx::readbackBuffer != nullptr && HooksDx::queryHeap != nullptr && cq != nullptr)
    {
        UINT64* timestampData;
        HooksDx::readbackBuffer->Map(0, nullptr, reinterpret_cast<void**>(&timestampData));

        if (timestampData != nullptr)
        {
            // Get the GPU timestamp frequency (ticks per second)
            UINT64 gpuFrequency;
            cq->GetTimestampFrequency(&gpuFrequency);

            // Calculate elapsed time in milliseconds
            UINT64 startTime = timestampData[0];
            UINT64 endTime = timestampData[1];
            double elapsedTimeMs = (endTime - startTime) / static_cast<double>(gpuFrequency) * 1000.0;

            // filter out posibly wrong measured high values
            if (elapsedTimeMs < 100.0)
            {
                State::Instance().frameTimeMutex.lock();
                State::Instance().upscaleTimes.push_back(elapsedTimeMs);
                State::Instance().upscaleTimes.pop_front();
                State::Instance().frameTimeMutex.unlock();
            }
        }
        else
        {
            LOG_WARN("timestampData is null!");
        }

        // Unmap the buffer
        HooksDx::readbackBuffer->Unmap(0, nullptr);

        HooksDx::dx12UpscaleTrig = false;
    }
    else if (HooksDx::dx11UpscaleTrig[HooksDx::currentFrameIndex] && device != nullptr &&
             HooksDx::disjointQueries[0] != nullptr && HooksDx::startQueries[0] != nullptr &&
             HooksDx::endQueries[0] != nullptr)
    {
        if (_d3d11DeviceContext == nullptr)
            device->GetImmediateContext(&_d3d11DeviceContext);

        // Retrieve the results from the previous frame
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
        if (_d3d11DeviceContext->GetData(HooksDx::disjointQueries[HooksDx::previousFrameIndex], &disjointData,
                                         sizeof(disjointData), 0) == S_OK)
        {
            if (!disjointData.Disjoint && disjointData.Frequency > 0)
            {
                UINT64 startTime = 0, endTime = 0;
                if (_d3d11DeviceContext->GetData(HooksDx::startQueries[HooksDx::previousFrameIndex], &startTime,
                                                 sizeof(UINT64), 0) == S_OK &&
                    _d3d11DeviceContext->GetData(HooksDx::endQueries[HooksDx::previousFrameIndex], &endTime,
                                                 sizeof(UINT64), 0) == S_OK)
                {
                    double elapsedTimeMs = (endTime - startTime) / static_cast<double>(disjointData.Frequency) * 1000.0;

                    // filter out posibly wrong measured high values
                    if (elapsedTimeMs < 100.0)
                    {
                        State::Instance().frameTimeMutex.lock();
                        State::Instance().upscaleTimes.push_back(elapsedTimeMs);
                        State::Instance().upscaleTimes.pop_front();
                        State::Instance().frameTimeMutex.unlock();
                    }
                }
            }
        }

        HooksDx::dx11UpscaleTrig[HooksDx::currentFrameIndex] = false;
        HooksDx::currentFrameIndex = (HooksDx::currentFrameIndex + 1) % HooksDx::QUERY_BUFFER_COUNT;
    }

    // DXVK check, it's here because of upscaler time calculations
    if (State::Instance().isRunningOnDXVK)
    {
        if (cq != nullptr)
            cq->Release();

        if (device != nullptr)
            device->Release();

        if (device12 != nullptr)
            device12->Release();

        if (pPresentParameters == nullptr)
            presentResult = pSwapChain->Present(SyncInterval, Flags);
        else
            presentResult = ((IDXGISwapChain1*) pSwapChain)->Present1(SyncInterval, Flags, pPresentParameters);

        if (presentResult == S_OK)
            LOG_TRACE("3 {}", (UINT) presentResult);
        else
            LOG_ERROR("3 {:X}", (UINT) presentResult);

        return presentResult;
    }

    // Tick feature to let it know if it's frozen
    if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)
        currentFeature->TickFrozenCheck();

    // Draw overlay
    MenuOverlayDx::Present(pSwapChain, SyncInterval, Flags, pPresentParameters, pDevice, hWnd, isUWP);

    if (State::Instance().activeFgOutput == FGOutput::FSRFG)
    {
        fakenvapi::reportFGPresent(pSwapChain, fg != nullptr && fg->IsActive(), _frameCounter % 2);
    }

    _frameCounter++;

    // swapchain present
    if (pPresentParameters == nullptr)
        presentResult = pSwapChain->Present(SyncInterval, Flags);
    else
        presentResult = ((IDXGISwapChain1*) pSwapChain)->Present1(SyncInterval, Flags, pPresentParameters);

    // release used objects
    if (cq != nullptr)
        cq->Release();

    if (device != nullptr)
        device->Release();

    if (device12 != nullptr)
        device12->Release();

    if (presentResult == S_OK)
        LOG_TRACE("4 {}, Present result: {:X}", _frameCounter, (UINT) presentResult);
    else
        LOG_ERROR("4 {:X}", (UINT) presentResult);

    // If Half of Full sync is active or was active (_releaseMutexTargetFrame != 0)
    if (_releaseMutexTargetFrame != 0 && Config::Instance()->FGUseMutexForSwapchain.value_or_default() &&
        _frameCounter >= _releaseMutexTargetFrame && fg != nullptr)
    {
        if (_lockAccuiredForHalfOrFull)
        {
            LOG_TRACE("Releasing FG->Mutex: {}", fg->Mutex.getOwner());
            fg->Mutex.unlockThis(2);
            _lockAccuiredForHalfOrFull = false;
        }

        _releaseMutexTargetFrame = 0;
    }

    LOG_DEBUG("Done");

    return presentResult;
}

#pragma endregion

#pragma region DXGI hooks

static void CheckAdapter(IUnknown* unkAdapter)
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

        // Temporary fix for Linux & DXVK
        if (State::Instance().isRunningOnDXVK || State::Instance().isRunningOnLinux)
            Config::Instance()->UseHQFont.set_volatile_value(false);
    }

    if (adapterOk)
        adapter->Release();
}

static HRESULT hkCreateSwapChainForCoreWindow(IDXGIFactory2* pFactory, IUnknown* pDevice, IUnknown* pWindow,
                                              DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput,
                                              IDXGISwapChain1** ppSwapChain)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    if (State::Instance().vulkanCreatingSC)
    {
        LOG_WARN("Vulkan is creating swapchain!");

        if (pDesc != nullptr)
            LOG_DEBUG("Width: {}, Height: {}, Format: {:X}, Count: {}, SkipWrapping: {}", pDesc->Width, pDesc->Height,
                      (UINT) pDesc->Format, pDesc->BufferCount, _skipFGSwapChainCreation);

        State::Instance().skipDxgiLoadChecks = true;
        auto res = oCreateSwapChainForCoreWindow(pFactory, pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
        State::Instance().skipDxgiLoadChecks = false;
        return res;
    }

    if (pDevice == nullptr || pDesc == nullptr)
    {
        LOG_WARN("pDevice or pDesc is nullptr!");
        State::Instance().skipDxgiLoadChecks = true;
        auto res = oCreateSwapChainForCoreWindow(pFactory, pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
        State::Instance().skipDxgiLoadChecks = false;
        return res;
    }

    if (pDesc->Height < 100 || pDesc->Width < 100)
    {
        LOG_WARN("Overlay call!");
        State::Instance().skipDxgiLoadChecks = true;
        auto res = oCreateSwapChainForCoreWindow(pFactory, pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
        State::Instance().skipDxgiLoadChecks = false;
        return res;
    }

    LOG_DEBUG("Width: {}, Height: {}, Format: {:X}, Count: {}, SkipWrapping: {}", pDesc->Width, pDesc->Height,
              (UINT) pDesc->Format, pDesc->BufferCount, _skipFGSwapChainCreation);

    // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
    // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
    if (Config::Instance()->ForceHDR.value_or_default() && !_skipFGSwapChainCreation)
    {
        LOG_INFO("Force HDR on");

        if (Config::Instance()->UseHDR10.value_or_default())
        {
            LOG_INFO("Using HDR10");
            pDesc->Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        }
        else
        {
            LOG_INFO("Not using HDR10");
            pDesc->Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        }

        if (pDesc->BufferCount < 2)
            pDesc->BufferCount = 2;

        pDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        pDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    State::Instance().skipDxgiLoadChecks = true;
    auto result = oCreateSwapChainForCoreWindow(pFactory, pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
    State::Instance().skipDxgiLoadChecks = false;

    if (result == S_OK)
    {
        // check for SL proxy
        IDXGISwapChain* realSC = nullptr;
        if (!CheckForRealObject(__FUNCTION__, *ppSwapChain, (IUnknown**) &realSC))
            realSC = *ppSwapChain;

        IUnknown* readDevice = nullptr;
        if (!CheckForRealObject(__FUNCTION__, pDevice, (IUnknown**) &readDevice))
            readDevice = pDevice;

        State::Instance().screenWidth = pDesc->Width;
        State::Instance().screenHeight = pDesc->Height;

        LOG_DEBUG("Created new swapchain: {0:X}, hWnd: {1:X}", (UINT64) *ppSwapChain, (UINT64) pWindow);
        *ppSwapChain =
            new WrappedIDXGISwapChain4(realSC, readDevice, (HWND) pWindow, hkPresent,
                                       MenuOverlayDx::CleanupRenderTarget, HooksDx::ReleaseDx12SwapChain, true);

        if (!_skipFGSwapChainCreation)
            State::Instance().currentSwapchain = *ppSwapChain;

        LOG_DEBUG("Created new WrappedIDXGISwapChain4: {0:X}, pDevice: {1:X}", (UINT64) *ppSwapChain, (UINT64) pDevice);

        // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
        // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
        if (Config::Instance()->ForceHDR.value_or_default())
        {
            if (!_skipFGSwapChainCreation)
            {
                State::Instance().SCbuffers.clear();
                for (size_t i = 0; i < pDesc->BufferCount; i++)
                {
                    IUnknown* buffer;
                    if ((*ppSwapChain)->GetBuffer(i, IID_PPV_ARGS(&buffer)) == S_OK)
                    {
                        State::Instance().SCbuffers.push_back(buffer);
                        buffer->Release();
                    }
                }
            }

            IDXGISwapChain3* sc3 = nullptr;
            do
            {
                if ((*ppSwapChain)->QueryInterface(IID_PPV_ARGS(&sc3)) == S_OK)
                {
                    DXGI_COLOR_SPACE_TYPE hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

                    if (Config::Instance()->UseHDR10.value_or_default())
                        hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

                    UINT css = 0;

                    auto result = sc3->CheckColorSpaceSupport(hdrCS, &css);

                    if (result != S_OK)
                    {
                        LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
                        break;
                    }

                    if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
                    {
                        result = sc3->SetColorSpace1(hdrCS);

                        if (result != S_OK)
                        {
                            LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
                            break;
                        }
                    }

                    LOG_INFO("HDR format and color space are set");
                }

            } while (false);

            if (sc3 != nullptr)
                sc3->Release();
        }
    }

    return result;
}

static HRESULT hkCreateSwapChain(IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                 IDXGISwapChain** ppSwapChain)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    *ppSwapChain = nullptr;

    if (State::Instance().vulkanCreatingSC)
    {
        LOG_WARN("Vulkan is creating swapchain!");

        if (pDesc != nullptr)
            LOG_DEBUG("Width: {}, Height: {}, Format: {:X}, Count: {}, Hwnd: {:X}, Windowed: {}, SkipWrapping: {}",
                      pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, (UINT) pDesc->BufferDesc.Format,
                      pDesc->BufferCount, (UINT) pDesc->OutputWindow, pDesc->Windowed, _skipFGSwapChainCreation);

        State::Instance().skipDxgiLoadChecks = true;
        auto res = oCreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
        State::Instance().skipDxgiLoadChecks = false;
        return res;
    }

    if (pDevice == nullptr || pDesc == nullptr)
    {
        LOG_WARN("pDevice or pDesc is nullptr!");
        State::Instance().skipDxgiLoadChecks = true;
        auto res = oCreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
        State::Instance().skipDxgiLoadChecks = false;
        return res;
    }

    if (pDesc->BufferDesc.Height < 100 || pDesc->BufferDesc.Width < 100)
    {
        LOG_WARN("Overlay call!");
        State::Instance().skipDxgiLoadChecks = true;
        auto res = oCreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
        State::Instance().skipDxgiLoadChecks = false;
        return res;
    }

    LOG_DEBUG("Width: {}, Height: {}, Format: {:X}, Count: {}, Hwnd: {:X}, Windowed: {}, SkipWrapping: {}",
              pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, (UINT) pDesc->BufferDesc.Format, pDesc->BufferCount,
              (UINT) pDesc->OutputWindow, pDesc->Windowed, _skipFGSwapChainCreation);

    // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
    // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
    if (Config::Instance()->ForceHDR.value_or_default() && !_skipFGSwapChainCreation)
    {
        LOG_INFO("Force HDR on");

        if (Config::Instance()->UseHDR10.value_or_default())
        {
            LOG_INFO("Using HDR10");
            pDesc->BufferDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        }
        else
        {
            LOG_INFO("Not using HDR10");
            pDesc->BufferDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        }

        if (pDesc->BufferCount < 2)
            pDesc->BufferCount = 2;

        pDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        pDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    ID3D12CommandQueue* cq = nullptr;
    if (Config::Instance()->OverlayMenu.value_or_default() && State::Instance().activeFgOutput == FGOutput::FSRFG &&
        !_skipFGSwapChainCreation && FfxApiProxy::InitFfxDx12() && pDevice->QueryInterface(IID_PPV_ARGS(&cq)) == S_OK)
    {
        cq->SetName(L"GameQueue");
        cq->Release();

        // FG Init
        if (State::Instance().currentFG == nullptr)
            State::Instance().currentFG = new FSRFG_Dx12();

        HooksDx::ReleaseDx12SwapChain(pDesc->OutputWindow);

        auto fg = State::Instance().currentFG;

        ID3D12CommandQueue* real = nullptr;
        if (!CheckForRealObject(__FUNCTION__, pDevice, (IUnknown**) &real))
            real = (ID3D12CommandQueue*) pDevice;

        _skipFGSwapChainCreation = true;
        State::Instance().skipHeapCapture = true;
        State::Instance().skipDxgiLoadChecks = true;

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = true;

        auto scResult = fg->CreateSwapchain(pFactory, real, pDesc, ppSwapChain);

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = false;

        State::Instance().skipDxgiLoadChecks = false;
        State::Instance().skipHeapCapture = false;
        _skipFGSwapChainCreation = false;

        if (scResult)
        {
            if (o_FGSCPresent == nullptr && *ppSwapChain != nullptr)
            {
                void** pFactoryVTable = *reinterpret_cast<void***>(*ppSwapChain);

                o_FGSCPresent = (PFN_Present) pFactoryVTable[8];

                if (o_FGSCPresent != nullptr)
                {
                    LOG_INFO("Hooking FSR FG SwapChain present");

                    DetourTransactionBegin();
                    DetourUpdateThread(GetCurrentThread());

                    DetourAttach(&(PVOID&) o_FGSCPresent, hkFGPresent);

                    DetourTransactionCommit();
                }
            }

            State::Instance().SCbuffers.clear();
            for (size_t i = 0; i < 3; i++)
            {
                IUnknown* buffer;
                if ((*ppSwapChain)->GetBuffer(i, IID_PPV_ARGS(&buffer)) == S_OK)
                {
                    State::Instance().SCbuffers.push_back(buffer);
                    buffer->Release();
                }
            }

            if (Config::Instance()->ForceHDR.value_or_default())
            {
                IDXGISwapChain3* sc3 = nullptr;

                do
                {
                    if ((*ppSwapChain)->QueryInterface(IID_PPV_ARGS(&sc3)) == S_OK)
                    {
                        DXGI_COLOR_SPACE_TYPE hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

                        if (Config::Instance()->UseHDR10.value_or_default())
                            hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

                        UINT css = 0;

                        auto result = sc3->CheckColorSpaceSupport(hdrCS, &css);

                        if (result != S_OK)
                        {
                            LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
                            break;
                        }

                        if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
                        {
                            result = sc3->SetColorSpace1(hdrCS);

                            if (result != S_OK)
                            {
                                LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
                                break;
                            }
                        }

                        LOG_INFO("HDR format and color space are set");
                    }

                } while (false);

                if (sc3 != nullptr)
                    sc3->Release();
            }

            State::Instance().currentSwapchain = (*ppSwapChain);

            LOG_DEBUG("Created FSR-FG swapchain");
            return S_OK;
        }

        LOG_ERROR("D3D12_CreateContext error: {}", scResult);

        return E_INVALIDARG;
    }

    // Disable FSR FG if amd dll is not found
    if (State::Instance().activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::InitFfxDx12())
    {
        Config::Instance()->FGOutput.set_volatile_value(FGOutput::NoFG);
        State::Instance().activeFgOutput = Config::Instance()->FGOutput.value_or_default();
    }

    if (!_skipFGSwapChainCreation && Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
        State::Instance().skipHeapCapture = true;

    State::Instance().skipDxgiLoadChecks = true;
    auto result = oCreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
    State::Instance().skipDxgiLoadChecks = false;

    if (!_skipFGSwapChainCreation && Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
        State::Instance().skipHeapCapture = false;

    if (result == S_OK)
    {
        // check for SL proxy
        IDXGISwapChain* realSC = nullptr;
        if (!CheckForRealObject(__FUNCTION__, *ppSwapChain, (IUnknown**) &realSC))
            realSC = *ppSwapChain;

        IUnknown* readDevice = nullptr;
        if (!CheckForRealObject(__FUNCTION__, pDevice, (IUnknown**) &readDevice))
            readDevice = pDevice;

        if (Util::GetProcessWindow() == pDesc->OutputWindow)
        {
            State::Instance().screenWidth = pDesc->BufferDesc.Width;
            State::Instance().screenHeight = pDesc->BufferDesc.Height;
        }

        LOG_DEBUG("Created new swapchain: {0:X}, hWnd: {1:X}", (UINT64) *ppSwapChain, (UINT64) pDesc->OutputWindow);
        *ppSwapChain =
            new WrappedIDXGISwapChain4(realSC, readDevice, pDesc->OutputWindow, hkPresent,
                                       MenuOverlayDx::CleanupRenderTarget, HooksDx::ReleaseDx12SwapChain, false);

        if (!_skipFGSwapChainCreation)
            State::Instance().currentSwapchain = *ppSwapChain;

        LOG_DEBUG("Created new WrappedIDXGISwapChain4: {0:X}, pDevice: {1:X}", (UINT64) *ppSwapChain, (UINT64) pDevice);

        // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
        // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
        if (Config::Instance()->ForceHDR.value_or_default())
        {
            if (!_skipFGSwapChainCreation)
            {
                State::Instance().SCbuffers.clear();
                for (size_t i = 0; i < pDesc->BufferCount; i++)
                {
                    IUnknown* buffer;
                    if ((*ppSwapChain)->GetBuffer(i, IID_PPV_ARGS(&buffer)) == S_OK)
                    {
                        State::Instance().SCbuffers.push_back(buffer);
                        buffer->Release();
                    }
                }
            }

            IDXGISwapChain3* sc3 = nullptr;
            do
            {
                if ((*ppSwapChain)->QueryInterface(IID_PPV_ARGS(&sc3)) == S_OK)
                {
                    DXGI_COLOR_SPACE_TYPE hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

                    if (Config::Instance()->UseHDR10.value_or_default())
                        hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

                    UINT css = 0;

                    auto result = sc3->CheckColorSpaceSupport(hdrCS, &css);

                    if (result != S_OK)
                    {
                        LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
                        break;
                    }

                    if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
                    {
                        result = sc3->SetColorSpace1(hdrCS);

                        if (result != S_OK)
                        {
                            LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
                            break;
                        }
                    }

                    LOG_INFO("HDR format and color space are set");
                }

            } while (false);

            if (sc3 != nullptr)
                sc3->Release();
        }
    }

    return result;
}

static HRESULT hkCreateSwapChainForHwnd(IDXGIFactory* This, IUnknown* pDevice, HWND hWnd, DXGI_SWAP_CHAIN_DESC1* pDesc,
                                        DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                        IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    static bool firstCall = static_cast<bool>(State::Instance().gameQuirks & GameQuirk::NoFSRFGFirstSwapchain);
    if (firstCall)
    {
        LOG_DEBUG("Skipping FG swapchain creation");
        _skipFGSwapChainCreation = true;
    }

    *ppSwapChain = nullptr;

    if (State::Instance().vulkanCreatingSC)
    {
        LOG_WARN("Vulkan is creating swapchain!");
        State::Instance().skipDxgiLoadChecks = true;
        return oCreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
        State::Instance().skipDxgiLoadChecks = false;
    }

    if (pDevice == nullptr || pDesc == nullptr)
    {
        LOG_WARN("pDevice or pDesc is nullptr!");
        State::Instance().skipDxgiLoadChecks = true;
        return oCreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
        State::Instance().skipDxgiLoadChecks = false;
    }

    if (pDesc->Height < 100 || pDesc->Width < 100)
    {
        LOG_WARN("Overlay call!");
        State::Instance().skipDxgiLoadChecks = true;
        return oCreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
        State::Instance().skipDxgiLoadChecks = false;
    }

    LOG_DEBUG("Width: {}, Height: {}, Format: {:X}, Count: {}, Hwnd: {:X}, SkipWrapping: {}", pDesc->Width,
              pDesc->Height, (UINT) pDesc->Format, pDesc->BufferCount, (UINT) hWnd, _skipFGSwapChainCreation);

    if (Config::Instance()->ForceHDR.value_or_default() && !_skipFGSwapChainCreation)
    {
        LOG_INFO("Force HDR on");

        if (Config::Instance()->UseHDR10.value_or_default())
        {
            LOG_INFO("Using HDR10");
            pDesc->Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        }
        else
        {
            LOG_INFO("Not using HDR10");
            pDesc->Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        }

        if (pDesc->BufferCount < 2)
            pDesc->BufferCount = 2;

        pDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        pDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    ID3D12CommandQueue* cq = nullptr;
    if (State::Instance().activeFgOutput == FGOutput::FSRFG && !_skipFGSwapChainCreation &&
        FfxApiProxy::InitFfxDx12() && pDevice->QueryInterface(IID_PPV_ARGS(&cq)) == S_OK)
    {
        cq->SetName(L"GameQueueHwnd");
        cq->Release();

        // FG Init
        if (State::Instance().currentFG == nullptr)
            State::Instance().currentFG = new FSRFG_Dx12();

        HooksDx::ReleaseDx12SwapChain(hWnd);

        auto fg = State::Instance().currentFG;

        ID3D12CommandQueue* real = nullptr;
        if (!CheckForRealObject(__FUNCTION__, pDevice, (IUnknown**) &real))
            real = (ID3D12CommandQueue*) pDevice;

        _skipFGSwapChainCreation = true;
        State::Instance().skipHeapCapture = true;
        State::Instance().skipDxgiLoadChecks = true;

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = true;

        auto scResult = fg->CreateSwapchain1(This, real, hWnd, pDesc, pFullscreenDesc, ppSwapChain);

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = false;

        State::Instance().skipDxgiLoadChecks = false;
        State::Instance().skipHeapCapture = false;
        _skipFGSwapChainCreation = false;

        if (scResult)
        {
            if (o_FGSCPresent == nullptr && *ppSwapChain != nullptr)
            {
                void** pFactoryVTable = *reinterpret_cast<void***>(*ppSwapChain);

                o_FGSCPresent = (PFN_Present) pFactoryVTable[8];

                if (o_FGSCPresent != nullptr)
                {
                    LOG_INFO("Hooking FSR FG SwapChain present");

                    DetourTransactionBegin();
                    DetourUpdateThread(GetCurrentThread());

                    DetourAttach(&(PVOID&) o_FGSCPresent, hkFGPresent);

                    DetourTransactionCommit();
                }
            }

            State::Instance().SCbuffers.clear();
            for (size_t i = 0; i < 3; i++)
            {
                IUnknown* buffer;
                if ((*ppSwapChain)->GetBuffer(i, IID_PPV_ARGS(&buffer)) == S_OK)
                {
                    State::Instance().SCbuffers.push_back(buffer);
                    buffer->Release();
                }
            }

            if (Config::Instance()->ForceHDR.value_or_default())
            {
                IDXGISwapChain3* sc3 = nullptr;

                do
                {
                    if ((*ppSwapChain)->QueryInterface(IID_PPV_ARGS(&sc3)) == S_OK)
                    {
                        DXGI_COLOR_SPACE_TYPE hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

                        if (Config::Instance()->UseHDR10.value_or_default())
                            hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

                        UINT css = 0;

                        auto result = sc3->CheckColorSpaceSupport(hdrCS, &css);

                        if (result != S_OK)
                        {
                            LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
                            break;
                        }

                        if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
                        {
                            result = sc3->SetColorSpace1(hdrCS);

                            if (result != S_OK)
                            {
                                LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
                                break;
                            }
                        }

                        LOG_INFO("HDR format and color space are set");
                    }

                } while (false);

                if (sc3 != nullptr)
                    sc3->Release();
            }

            State::Instance().currentSwapchain = (*ppSwapChain);

            LOG_DEBUG("Created FSR-FG swapchain");
            return S_OK;
        }

        LOG_ERROR("D3D12_CreateContext error: {}", scResult);
        return E_INVALIDARG;
    }

    // Disable FSR FG if amd dll is not found
    if (State::Instance().activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::InitFfxDx12())
    {
        Config::Instance()->FGOutput.set_volatile_value(FGOutput::NoFG);
        State::Instance().activeFgOutput = Config::Instance()->FGOutput.value_or_default();
    }

    if (!_skipFGSwapChainCreation && Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
        State::Instance().skipHeapCapture = true;

    State::Instance().skipDxgiLoadChecks = true;
    auto result = oCreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
    State::Instance().skipDxgiLoadChecks = false;

    if (!_skipFGSwapChainCreation && Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
        State::Instance().skipHeapCapture = false;

    if (result == S_OK)
    {
        // check for SL proxy
        IDXGISwapChain1* realSC = nullptr;
        if (!CheckForRealObject(__FUNCTION__, *ppSwapChain, (IUnknown**) &realSC))
            realSC = *ppSwapChain;

        IUnknown* readDevice = nullptr;
        if (!CheckForRealObject(__FUNCTION__, pDevice, (IUnknown**) &readDevice))
            readDevice = pDevice;

        if (Util::GetProcessWindow() == hWnd)
        {
            State::Instance().screenWidth = pDesc->Width;
            State::Instance().screenHeight = pDesc->Height;
        }

        LOG_DEBUG("Created new swapchain: {0:X}, hWnd: {1:X}", (UINT64) *ppSwapChain, (UINT64) hWnd);
        *ppSwapChain =
            new WrappedIDXGISwapChain4(realSC, readDevice, hWnd, hkPresent, MenuOverlayDx::CleanupRenderTarget,
                                       HooksDx::ReleaseDx12SwapChain, false);
        LOG_DEBUG("Created new WrappedIDXGISwapChain4: {0:X}, pDevice: {1:X}", (UINT64) *ppSwapChain, (UINT64) pDevice);

        if (!_skipFGSwapChainCreation)
            State::Instance().currentSwapchain = *ppSwapChain;

        if (Config::Instance()->ForceHDR.value_or_default())
        {
            if (!_skipFGSwapChainCreation)
            {
                State::Instance().SCbuffers.clear();
                for (size_t i = 0; i < pDesc->BufferCount; i++)
                {
                    IUnknown* buffer;
                    if ((*ppSwapChain)->GetBuffer(i, IID_PPV_ARGS(&buffer)) == S_OK)
                    {
                        State::Instance().SCbuffers.push_back(buffer);
                        buffer->Release();
                    }
                }
            }

            IDXGISwapChain3* sc3 = nullptr;
            do
            {
                if ((*ppSwapChain)->QueryInterface(IID_PPV_ARGS(&sc3)) == S_OK)
                {
                    DXGI_COLOR_SPACE_TYPE hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

                    if (Config::Instance()->UseHDR10.value_or_default())
                        hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

                    UINT css = 0;

                    auto result = sc3->CheckColorSpaceSupport(hdrCS, &css);

                    if (result != S_OK)
                    {
                        LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
                        break;
                    }

                    if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
                    {
                        result = sc3->SetColorSpace1(hdrCS);

                        if (result != S_OK)
                        {
                            LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
                            break;
                        }
                    }

                    LOG_INFO("HDR format and color space are set");
                }

            } while (false);

            if (sc3 != nullptr)
                sc3->Release();
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

static HRESULT hkCreateDXGIFactory(REFIID riid, IDXGIFactory** ppFactory)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    State::DisableChecks(97, "dxgi");
#ifndef ENABLE_DEBUG_LAYER_DX12
    auto result = o_CreateDXGIFactory(riid, ppFactory);
#else
    auto result = o_CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, riid, (IDXGIFactory2**) ppFactory);
#endif
    State::EnableChecks(97);

    if (result != S_OK)
        return result;

    IDXGIFactory* real = nullptr;
    if (!CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        real = *ppFactory;

    if (oCreateSwapChain == nullptr)
    {
        void** pFactoryVTable = *reinterpret_cast<void***>(real);

        oCreateSwapChain = (PFN_CreateSwapChain) pFactoryVTable[10];

        IDXGIFactory2* factory2 = nullptr;
        if (real->QueryInterface(IID_PPV_ARGS(&factory2)) == S_OK && factory2 != nullptr)
        {
            pFactoryVTable = *reinterpret_cast<void***>(factory2);
            factory2->Release();

            oCreateSwapChainForHwnd = (PFN_CreateSwapChainForHwnd) pFactoryVTable[15];
            oCreateSwapChainForCoreWindow = (PFN_CreateSwapChainForCoreWindow) pFactoryVTable[16];
        }

        if (oCreateSwapChain != nullptr)
        {
            LOG_INFO("Hooking native DXGIFactory");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            DetourAttach(&(PVOID&) oCreateSwapChain, hkCreateSwapChain);

            if (oCreateSwapChainForHwnd != nullptr)
                DetourAttach(&(PVOID&) oCreateSwapChainForHwnd, hkCreateSwapChainForHwnd);

            if (oCreateSwapChainForCoreWindow != nullptr)
                DetourAttach(&(PVOID&) oCreateSwapChainForCoreWindow, hkCreateSwapChainForCoreWindow);

            DetourTransactionCommit();
        }
    }

    return result;
}

static HRESULT hkCreateDXGIFactory1(REFIID riid, IDXGIFactory1** ppFactory)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    State::DisableChecks(98, "dxgi");
#ifndef ENABLE_DEBUG_LAYER_DX12
    auto result = o_CreateDXGIFactory1(riid, ppFactory);
#else
    auto result = o_CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, riid, (IDXGIFactory2**) ppFactory);
#endif
    State::EnableChecks(98);

    if (result != S_OK)
        return result;

    IDXGIFactory1* real = nullptr;
    if (!CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        real = *ppFactory;

    if (oCreateSwapChain == nullptr)
    {
        void** pFactoryVTable = *reinterpret_cast<void***>(real);

        oCreateSwapChain = (PFN_CreateSwapChain) pFactoryVTable[10];

        IDXGIFactory2* factory2 = nullptr;
        if (real->QueryInterface(IID_PPV_ARGS(&factory2)) == S_OK && factory2 != nullptr)
        {
            pFactoryVTable = *reinterpret_cast<void***>(factory2);
            factory2->Release();

            oCreateSwapChainForHwnd = (PFN_CreateSwapChainForHwnd) pFactoryVTable[15];
            oCreateSwapChainForCoreWindow = (PFN_CreateSwapChainForCoreWindow) pFactoryVTable[16];
        }

        if (oCreateSwapChain != nullptr)
        {
            LOG_INFO("Hooking native DXGIFactory1");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            DetourAttach(&(PVOID&) oCreateSwapChain, hkCreateSwapChain);

            if (oCreateSwapChainForHwnd != nullptr)
                DetourAttach(&(PVOID&) oCreateSwapChainForHwnd, hkCreateSwapChainForHwnd);

            if (oCreateSwapChainForCoreWindow != nullptr)
                DetourAttach(&(PVOID&) oCreateSwapChainForCoreWindow, hkCreateSwapChainForCoreWindow);

            DetourTransactionCommit();
        }
    }

    return result;
}

static HRESULT hkCreateDXGIFactory2(UINT Flags, REFIID riid, IDXGIFactory2** ppFactory)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    State::DisableChecks(99, "dxgi");
#ifndef ENABLE_DEBUG_LAYER_DX12
    auto result = o_CreateDXGIFactory2(Flags, riid, ppFactory);
#else
    auto result = o_CreateDXGIFactory2(Flags | DXGI_CREATE_FACTORY_DEBUG, riid, ppFactory);
#endif
    State::EnableChecks(99);

    if (result != S_OK)
        return result;

    IDXGIFactory2* real = nullptr;
    if (!CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        real = *ppFactory;

    if (oCreateSwapChainForHwnd == nullptr)
    {
        void** pFactoryVTable = *reinterpret_cast<void***>(real);

        bool skip = false;

        if (oCreateSwapChain == nullptr)
            oCreateSwapChain = (PFN_CreateSwapChain) pFactoryVTable[10];
        else
            skip = true;

        oCreateSwapChainForHwnd = (PFN_CreateSwapChainForHwnd) pFactoryVTable[15];
        oCreateSwapChainForCoreWindow = (PFN_CreateSwapChainForCoreWindow) pFactoryVTable[16];

        if (oCreateSwapChainForHwnd != nullptr)
        {
            LOG_INFO("Hooking native DXGIFactory2");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (!skip)
                DetourAttach(&(PVOID&) oCreateSwapChain, hkCreateSwapChain);

            if (oCreateSwapChainForHwnd != nullptr)
                DetourAttach(&(PVOID&) oCreateSwapChainForHwnd, hkCreateSwapChainForHwnd);

            if (oCreateSwapChainForCoreWindow != nullptr)
                DetourAttach(&(PVOID&) oCreateSwapChainForCoreWindow, hkCreateSwapChainForCoreWindow);

            DetourTransactionCommit();
        }
    }

    return result;
}

static HRESULT hkEnumAdapterByGpuPreference(IDXGIFactory6* This, UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference,
                                            REFIID riid, IUnknown** ppvAdapter)
{
    LOG_TRACE("HooksDx Adapter: {}, GpuPreference: {}", Adapter, (UINT) GpuPreference);

    State::DisableChecks(2, "dxgi");
    auto result = o_EnumAdapterByGpuPreference(This, Adapter, GpuPreference, riid, ppvAdapter);
    State::EnableChecks(2);

    if (result == S_OK)
        CheckAdapter(*ppvAdapter);

    LOG_TRACE("HooksDx result: {:X}, Adapter: {:X}", (UINT) result, (size_t) *ppvAdapter);
    return result;
}

static HRESULT hkEnumAdapterByLuid(IDXGIFactory4* This, LUID AdapterLuid, REFIID riid, IUnknown** ppvAdapter)
{
    LOG_FUNC();

    State::DisableChecks(3, "dxgi");
    auto result = o_EnumAdapterByLuid(This, AdapterLuid, riid, ppvAdapter);
    State::EnableChecks(3);

    if (result == S_OK)
        CheckAdapter(*ppvAdapter);

    return result;
}

static HRESULT hkEnumAdapters1(IDXGIFactory1* This, UINT Adapter, IUnknown** ppAdapter)
{
    LOG_TRACE("HooksDx");

    State::DisableChecks(4, "dxgi");
    HRESULT result = o_EnumAdapters1(This, Adapter, ppAdapter);
    State::EnableChecks(4);

    if (result == S_OK)
        CheckAdapter(*ppAdapter);

    return result;
}

static HRESULT hkEnumAdapters(IDXGIFactory* This, UINT Adapter, IUnknown** ppAdapter)
{
    LOG_FUNC();

    State::DisableChecks(5, "dxgi");
    HRESULT result = o_EnumAdapters(This, Adapter, ppAdapter);
    State::EnableChecks(5);

    if (result == S_OK)
        CheckAdapter(*ppAdapter);

    return result;
}

#pragma endregion

#pragma region DirectX hooks

static void HookToDevice(ID3D12Device* InDevice)
{
    if (o_CreateSampler != nullptr || InDevice == nullptr)
        return;

    LOG_DEBUG("Dx12");

    // Get the vtable pointer
    PVOID* pVTable = *(PVOID**) InDevice;

    ID3D12Device* realDevice = nullptr;
    if (CheckForRealObject(__FUNCTION__, InDevice, (IUnknown**) &realDevice))
        pVTable = *(PVOID**) realDevice;

    // hudless
    o_D3D12DeviceRelease = (PFN_Release) pVTable[2];
    o_CreateSampler = (PFN_CreateSampler) pVTable[22];
    o_CheckFeatureSupport = (PFN_CheckFeatureSupport) pVTable[13];
    o_GetResourceAllocationInfo = (PFN_GetResourceAllocationInfo) pVTable[25];
    o_CreateCommittedResource = (PFN_CreateCommittedResource) pVTable[27];
    o_CreatePlacedResource = (PFN_CreatePlacedResource) pVTable[29];

    // Apply the detour
    if (o_CreateSampler != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_CreateSampler != nullptr)
            DetourAttach(&(PVOID&) o_CreateSampler, hkCreateSampler);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default())
        {
            if (o_CheckFeatureSupport != nullptr)
                DetourAttach(&(PVOID&) o_CheckFeatureSupport, hkCheckFeatureSupport);

            if (o_CreateCommittedResource != nullptr)
                DetourAttach(&(PVOID&) o_CreateCommittedResource, hkCreateCommittedResource);

            if (o_CreatePlacedResource != nullptr)
                DetourAttach(&(PVOID&) o_CreatePlacedResource, hkCreatePlacedResource);

            if (o_D3D12DeviceRelease != nullptr)
                DetourAttach(&(PVOID&) o_D3D12DeviceRelease, hkD3D12DeviceRelease);

            // This does not work but luckily
            // UE works without Intel Extension for it
            // if (o_GetResourceAllocationInfo != nullptr)
            //    DetourAttach(&(PVOID&) o_GetResourceAllocationInfo, hkGetResourceAllocationInfo);
        }

        DetourTransactionCommit();
    }

    if ((State::Instance().activeFgInput == FGInput::Upscaler || State::Instance().activeFgInput == FGInput::DLSSG) &&
        Config::Instance()->OverlayMenu.value_or_default())
        ResTrack_Dx12::HookDevice(InDevice);
}

static void HookToDevice(ID3D11Device* InDevice)
{
    if (o_CreateSamplerState != nullptr || InDevice == nullptr)
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

        DetourTransactionCommit();
    }
}

static HRESULT hkD3D11On12CreateDevice(IUnknown* pDevice, UINT Flags, D3D_FEATURE_LEVEL* pFeatureLevels,
                                       UINT FeatureLevels, IUnknown** ppCommandQueues, UINT NumQueues, UINT NodeMask,
                                       ID3D11Device** ppDevice, ID3D11DeviceContext** ppImmediateContext,
                                       D3D_FEATURE_LEVEL* pChosenFeatureLevel)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

#ifdef ENABLE_DEBUG_LAYER_DX11
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    bool rtss = false;

    // Assuming RTSS is creating a D3D11on12 device, not sure why but sometimes RTSS tries to create
    // it's D3D11on12 device with old CommandQueue which results crash
    // I am changing it's CommandQueue with current swapchain's command queue
    if (State::Instance().currentCommandQueue != nullptr && *ppCommandQueues != State::Instance().currentCommandQueue &&
        GetModuleHandle(L"RTSSHooks64.dll") != nullptr && pDevice == State::Instance().currentD3D12Device)
    {
        LOG_INFO("Replaced RTSS CommandQueue with correct one {0:X} -> {1:X}", (UINT64) *ppCommandQueues,
                 (UINT64) State::Instance().currentCommandQueue);
        *ppCommandQueues = State::Instance().currentCommandQueue;
        rtss = true;
    }

    auto result = o_D3D11On12CreateDevice(pDevice, Flags, pFeatureLevels, FeatureLevels, ppCommandQueues, NumQueues,
                                          NodeMask, ppDevice, ppImmediateContext, pChosenFeatureLevel);

    if (result == S_OK && *ppDevice != nullptr && !rtss && !_d3d12Captured)
    {
        LOG_INFO("Device captured, D3D11Device: {0:X}", (UINT64) *ppDevice);
        d3d11on12Device = *ppDevice;
        HookToDevice(d3d11on12Device);
    }

    if (result == S_OK && *ppDevice != nullptr)
        State::Instance().d3d11Devices.push_back(*ppDevice);

    LOG_FUNC_RESULT(result);

    return result;
}

static HRESULT hkD3D11CreateDevice(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
                                   CONST D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
                                   ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel,
                                   ID3D11DeviceContext** ppImmediateContext)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

#ifdef ENABLE_DEBUG_LAYER_DX11
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
    };

    D3D_FEATURE_LEVEL maxLevel = D3D_FEATURE_LEVEL_1_0_CORE;

    for (UINT i = 0; i < FeatureLevels; ++i)
    {
        maxLevel = std::max(maxLevel, pFeatureLevels[i]);
    }

    DXGI_ADAPTER_DESC desc {};
    std::wstring szName;
    if (pAdapter != nullptr)
    {
        State::Instance().skipSpoofing = true;
        if (pAdapter->GetDesc(&desc) == S_OK)
        {
            szName = desc.Description;
            LOG_INFO("Adapter Desc: {}", wstring_to_string(szName));
        }
        State::Instance().skipSpoofing = false;
    }

    if (maxLevel == D3D_FEATURE_LEVEL_11_0)
    {
        LOG_INFO(
            "Overriding D3D_FEATURE_LEVEL, Game requested D3D_FEATURE_LEVEL_11_0, we need D3D_FEATURE_LEVEL_11_1!");
        pFeatureLevels = levels;
        FeatureLevels = ARRAYSIZE(levels);
    }

    auto result = o_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion,
                                      ppDevice, pFeatureLevel, ppImmediateContext);

    if (result == S_OK && *ppDevice != nullptr && !_d3d12Captured)
    {
        LOG_INFO("Device captured");
        d3d11Device = *ppDevice;

        if (szName.size() > 0)
            State::Instance().DeviceAdapterNames[*ppDevice] = wstring_to_string(szName);

        HookToDevice(d3d11Device);
    }

    LOG_FUNC_RESULT(result);

    if (result == S_OK && ppDevice != nullptr && *ppDevice != nullptr)
        State::Instance().d3d11Devices.push_back(*ppDevice);

    return result;
}

static HRESULT hkD3D11CreateDeviceAndSwapChain(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
                                               UINT Flags, CONST D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
                                               UINT SDKVersion, DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
                                               IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
                                               D3D_FEATURE_LEVEL* pFeatureLevel,
                                               ID3D11DeviceContext** ppImmediateContext)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

#ifdef ENABLE_DEBUG_LAYER_DX11
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

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
        LOG_INFO(
            "Overriding D3D_FEATURE_LEVEL, Game requested D3D_FEATURE_LEVEL_11_0, we need D3D_FEATURE_LEVEL_11_1!");
        pFeatureLevels = levels;
        FeatureLevels = ARRAYSIZE(levels);
    }

    if (pSwapChainDesc != nullptr && pSwapChainDesc->BufferDesc.Height == 2 && pSwapChainDesc->BufferDesc.Width == 2)
    {
        LOG_WARN("Overlay call!");
        auto result = o_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels,
                                                      FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice,
                                                      pFeatureLevel, ppImmediateContext);
        return result;
    }

    // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
    // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
    if (pSwapChainDesc != nullptr && Config::Instance()->ForceHDR.value_or_default())
    {
        LOG_INFO("Force HDR on");

        if (Config::Instance()->UseHDR10.value_or_default())
        {
            LOG_INFO("Using HDR10");
            pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
        }
        else
        {
            LOG_INFO("Not using HDR10");
            pSwapChainDesc->BufferDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        }

        if (pSwapChainDesc->BufferCount < 2)
            pSwapChainDesc->BufferCount = 2;

        pSwapChainDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        pSwapChainDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    auto result = o_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
                                                  SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel,
                                                  ppImmediateContext);

    if (result == S_OK && *ppDevice != nullptr && !_d3d12Captured)
    {
        LOG_INFO("Device captured");
        d3d11Device = *ppDevice;
        HookToDevice(d3d11Device);
    }

    if (result == S_OK && pSwapChainDesc != nullptr && ppSwapChain != nullptr && *ppSwapChain != nullptr &&
        ppDevice != nullptr && *ppDevice != nullptr)
    {
        // check for SL proxy
        IDXGISwapChain* realSC = nullptr;
        if (!CheckForRealObject(__FUNCTION__, *ppSwapChain, (IUnknown**) &realSC))
            realSC = *ppSwapChain;

        IUnknown* readDevice = nullptr;
        if (!CheckForRealObject(__FUNCTION__, *ppDevice, (IUnknown**) &readDevice))
            readDevice = *ppDevice;

        if (Util::GetProcessWindow() == pSwapChainDesc->OutputWindow)
        {
            State::Instance().screenWidth = pSwapChainDesc->BufferDesc.Width;
            State::Instance().screenHeight = pSwapChainDesc->BufferDesc.Height;
        }

        LOG_DEBUG("Created new swapchain: {0:X}, hWnd: {1:X}", (UINT64) *ppSwapChain,
                  (UINT64) pSwapChainDesc->OutputWindow);
        *ppSwapChain =
            new WrappedIDXGISwapChain4(realSC, readDevice, pSwapChainDesc->OutputWindow, hkPresent,
                                       MenuOverlayDx::CleanupRenderTarget, HooksDx::ReleaseDx12SwapChain, false);

        if (!_skipFGSwapChainCreation)
            State::Instance().currentSwapchain = *ppSwapChain;

        LOG_DEBUG("Created new WrappedIDXGISwapChain4: {0:X}, pDevice: {1:X}", (UINT64) *ppSwapChain,
                  (UINT64) *ppDevice);

        // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
        // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
        if (Config::Instance()->ForceHDR.value_or_default())
        {
            if (!_skipFGSwapChainCreation)
            {
                State::Instance().SCbuffers.clear();
                for (size_t i = 0; i < pSwapChainDesc->BufferCount; i++)
                {
                    IUnknown* buffer;
                    if ((*ppSwapChain)->GetBuffer(i, IID_PPV_ARGS(&buffer)) == S_OK)
                    {
                        State::Instance().SCbuffers.push_back(buffer);
                        buffer->Release();
                    }
                }
            }

            IDXGISwapChain3* sc3 = nullptr;
            do
            {
                if ((*ppSwapChain)->QueryInterface(IID_PPV_ARGS(&sc3)) == S_OK)
                {
                    DXGI_COLOR_SPACE_TYPE hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

                    if (Config::Instance()->UseHDR10.value_or_default())
                        hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

                    UINT css = 0;

                    auto result = sc3->CheckColorSpaceSupport(hdrCS, &css);

                    if (result != S_OK)
                    {
                        LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
                        break;
                    }

                    if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
                    {
                        result = sc3->SetColorSpace1(hdrCS);

                        if (result != S_OK)
                        {
                            LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
                            break;
                        }
                    }

                    LOG_INFO("HDR format and color space are set");
                }

            } while (false);

            if (sc3 != nullptr)
                sc3->Release();
        }
    }

    if (result == S_OK && ppDevice != nullptr && *ppDevice != nullptr)
        State::Instance().d3d11Devices.push_back(*ppDevice);

    LOG_FUNC_RESULT(result);
    return result;
}

#ifdef ENABLE_DEBUG_LAYER_DX12
static ID3D12Debug3* debugController = nullptr;
static ID3D12InfoQueue* infoQueue = nullptr;
static ID3D12InfoQueue1* infoQueue1 = nullptr;

static void CALLBACK D3D12DebugCallback(D3D12_MESSAGE_CATEGORY Category, D3D12_MESSAGE_SEVERITY Severity,
                                        D3D12_MESSAGE_ID ID, LPCSTR pDescription, void* pContext)
{
    LOG_DEBUG("Category: {}, Severity: {}, ID: {}, Message: {}", (UINT) Category, (UINT) Severity, (UINT) ID,
              pDescription);
}
#endif

static HRESULT hkD3D12CreateDevice(IDXGIAdapter* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid,
                                   void** ppDevice)
{
    LOG_DEBUG("Adapter: {:X}, Level: {:X}, Caller: {}", (size_t) pAdapter, (UINT) MinimumFeatureLevel,
              Util::WhoIsTheCaller(_ReturnAddress()));

#ifdef ENABLE_DEBUG_LAYER_DX12
    LOG_WARN("Debug layers active!");
    if (debugController == nullptr && D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)) == S_OK)
    {
        debugController->EnableDebugLayer();

#ifdef ENABLE_GPU_VALIDATION
        LOG_WARN("GPU Based Validation active!");
        debugController->SetEnableGPUBasedValidation(TRUE);
#endif
    }
#endif

    DXGI_ADAPTER_DESC desc {};
    std::wstring szName;
    if (pAdapter != nullptr && MinimumFeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE)
    {
        State::Instance().skipSpoofing = true;
        if (pAdapter->GetDesc(&desc) == S_OK)
        {
            szName = desc.Description;
            LOG_INFO("Adapter Desc: {}", wstring_to_string(szName));
        }
        State::Instance().skipSpoofing = false;
    }

    auto minLevel = MinimumFeatureLevel;
    if (Config::Instance()->SpoofFeatureLevel.value_or_default() && MinimumFeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE)
    {
        LOG_INFO("Forcing feature level 0xb000 for new device");
        minLevel = D3D_FEATURE_LEVEL_11_0;
    }

    if (desc.VendorId == VendorId::Intel)
        State::Instance().skipSpoofing = true;

    auto result = o_D3D12CreateDevice(pAdapter, minLevel, riid, ppDevice);

    if (desc.VendorId == VendorId::Intel)
        State::Instance().skipSpoofing = false;

    LOG_DEBUG("o_D3D12CreateDevice result: {:X}", (UINT) result);

    if (result == S_OK && ppDevice != nullptr && MinimumFeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE)
    {
        LOG_DEBUG("Device captured: {0:X}", (size_t) *ppDevice);
        State::Instance().currentD3D12Device = (ID3D12Device*) *ppDevice;

        if (szName.size() > 0)
            State::Instance().DeviceAdapterNames[*ppDevice] = wstring_to_string(szName);

        if (desc.VendorId == VendorId::Intel && Config::Instance()->UESpoofIntelAtomics64.value_or_default())
        {
            IGDExtProxy::EnableAtomicSupport(State::Instance().currentD3D12Device);
            _intelD3D12Device = State::Instance().currentD3D12Device;
            _intelD3D12DeviceRefTarget = _intelD3D12Device->AddRef();

            if (o_D3D12DeviceRelease == nullptr)
                _intelD3D12Device->Release();
            else
                o_D3D12DeviceRelease(_intelD3D12Device);
        }

        HookToDevice(State::Instance().currentD3D12Device);
        _d3d12Captured = true;

        State::Instance().d3d12Devices.push_back((ID3D12Device*) *ppDevice);

#ifdef ENABLE_DEBUG_LAYER_DX12
        if (infoQueue != nullptr)
            infoQueue->Release();

        if (infoQueue1 != nullptr)
            infoQueue1->Release();

        if (State::Instance().currentD3D12Device->QueryInterface(IID_PPV_ARGS(&infoQueue)) == S_OK)
        {
            LOG_DEBUG("infoQueue accuired");

            infoQueue->ClearRetrievalFilter();
            infoQueue->SetMuteDebugOutput(false);

            HRESULT res;
            res = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            // res = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            // res = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

            if (infoQueue->QueryInterface(IID_PPV_ARGS(&infoQueue1)) == S_OK && infoQueue1 != nullptr)
            {
                LOG_DEBUG("infoQueue1 accuired, registering MessageCallback");
                res = infoQueue1->RegisterMessageCallback(D3D12DebugCallback, D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,
                                                          NULL, NULL);
            }
        }
#endif
    }

    LOG_DEBUG("final result: {:X}", (UINT) result);
    return result;
}

static HRESULT hkD3D12SerializeRootSignature(D3d12Proxy::D3D12_ROOT_SIGNATURE_DESC_L* pRootSignature,
                                             D3D_ROOT_SIGNATURE_VERSION Version, ID3DBlob** ppBlob,
                                             ID3DBlob** ppErrorBlob)
{
    if (pRootSignature != nullptr)
    {
        for (size_t i = 0; i < pRootSignature->NumStaticSamplers; i++)
        {
            auto samplerDesc = &pRootSignature->pStaticSamplers[i];

            if (Config::Instance()->MipmapBiasOverride.has_value())
            {
                if ((samplerDesc->MipLODBias < 0.0f && samplerDesc->MinLOD != samplerDesc->MaxLOD) ||
                    Config::Instance()->MipmapBiasOverrideAll.value_or_default())
                {
                    if (Config::Instance()->MipmapBiasOverride.has_value())
                    {
                        LOG_DEBUG("Overriding mipmap bias {0} -> {1}", samplerDesc->MipLODBias,
                                  Config::Instance()->MipmapBiasOverride.value());

                        if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                            samplerDesc->MipLODBias = Config::Instance()->MipmapBiasOverride.value();
                        else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                            samplerDesc->MipLODBias =
                                samplerDesc->MipLODBias * Config::Instance()->MipmapBiasOverride.value();
                        else
                            samplerDesc->MipLODBias =
                                samplerDesc->MipLODBias + Config::Instance()->MipmapBiasOverride.value();
                    }

                    if (State::Instance().lastMipBiasMax < samplerDesc->MipLODBias)
                        State::Instance().lastMipBiasMax = samplerDesc->MipLODBias;

                    if (State::Instance().lastMipBias > samplerDesc->MipLODBias)
                        State::Instance().lastMipBias = samplerDesc->MipLODBias;
                }
            }

            if (Config::Instance()->AnisotropyOverride.has_value())
            {
                bool force = false;

                switch (samplerDesc->Filter)
                {
                case D3D12_FILTER_MIN_MAG_MIP_LINEAR:
                case D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT:
                    samplerDesc->Filter = D3D12_FILTER_ANISOTROPIC;
                    force = true;
                    break;

                case D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR:
                case D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT:
                    samplerDesc->Filter = D3D12_FILTER_COMPARISON_ANISOTROPIC;
                    force = true;
                    break;

                case D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR:
                case D3D12_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT:
                    samplerDesc->Filter = D3D12_FILTER_MINIMUM_ANISOTROPIC;
                    force = true;
                    break;

                case D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR:
                case D3D12_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT:
                    samplerDesc->Filter = D3D12_FILTER_MAXIMUM_ANISOTROPIC;
                    force = true;
                    break;
                }

                if (force)
                {
                    LOG_DEBUG("Overriding {2:X} to anisotropic filtering {0} -> {1}", samplerDesc->MaxAnisotropy,
                              Config::Instance()->AnisotropyOverride.value(), (UINT) samplerDesc->Filter);

                    samplerDesc->MaxAnisotropy = Config::Instance()->AnisotropyOverride.value();
                }
            }
        }
    }

    return o_D3D12SerializeRootSignature(pRootSignature, Version, ppBlob, ppErrorBlob);
}

static HRESULT hkD3D12SerializeVersionedRootSignature(D3d12Proxy::D3D12_VERSIONED_ROOT_SIGNATURE_DESC_L* pRootSignature,
                                                      ID3DBlob** ppBlob, ID3DBlob** ppErrorBlob)
{
    if (pRootSignature != nullptr)
    {
        for (size_t i = 0; i < pRootSignature->Desc_1_0.NumStaticSamplers; i++)
        {
            auto samplerDesc = &pRootSignature->Desc_1_0.pStaticSamplers[i];

            if (Config::Instance()->MipmapBiasOverride.has_value())
            {
                if ((samplerDesc->MipLODBias < 0.0f && samplerDesc->MinLOD != samplerDesc->MaxLOD) ||
                    Config::Instance()->MipmapBiasOverrideAll.value_or_default())
                {
                    if (Config::Instance()->MipmapBiasOverride.has_value())
                    {
                        LOG_DEBUG("Overriding mipmap bias {0} -> {1}", samplerDesc->MipLODBias,
                                  Config::Instance()->MipmapBiasOverride.value());

                        if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                            samplerDesc->MipLODBias = Config::Instance()->MipmapBiasOverride.value();
                        else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                            samplerDesc->MipLODBias =
                                samplerDesc->MipLODBias * Config::Instance()->MipmapBiasOverride.value();
                        else
                            samplerDesc->MipLODBias =
                                samplerDesc->MipLODBias + Config::Instance()->MipmapBiasOverride.value();
                    }

                    if (State::Instance().lastMipBiasMax < samplerDesc->MipLODBias)
                        State::Instance().lastMipBiasMax = samplerDesc->MipLODBias;

                    if (State::Instance().lastMipBias > samplerDesc->MipLODBias)
                        State::Instance().lastMipBias = samplerDesc->MipLODBias;
                }
            }

            if (Config::Instance()->AnisotropyOverride.has_value())
            {
                bool force = false;

                switch (samplerDesc->Filter)
                {
                case D3D12_FILTER_MIN_MAG_MIP_LINEAR:
                case D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT:
                    samplerDesc->Filter = D3D12_FILTER_ANISOTROPIC;
                    force = true;
                    break;

                case D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR:
                case D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT:
                    samplerDesc->Filter = D3D12_FILTER_COMPARISON_ANISOTROPIC;
                    force = true;
                    break;

                case D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR:
                case D3D12_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT:
                    samplerDesc->Filter = D3D12_FILTER_MINIMUM_ANISOTROPIC;
                    force = true;
                    break;

                case D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR:
                case D3D12_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT:
                    samplerDesc->Filter = D3D12_FILTER_MAXIMUM_ANISOTROPIC;
                    force = true;
                    break;
                }

                if (force)
                {
                    LOG_DEBUG("Overriding {2:X} to anisotropic filtering {0} -> {1}", samplerDesc->MaxAnisotropy,
                              Config::Instance()->AnisotropyOverride.value(), (UINT) samplerDesc->Filter);

                    samplerDesc->MaxAnisotropy = Config::Instance()->AnisotropyOverride.value();
                }
            }
        }
    }

    return o_D3D12SerializeVersionedRootSignature(pRootSignature, ppBlob, ppErrorBlob);
}

static ULONG hkD3D12DeviceRelease(IUnknown* device)
{
    if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && device == _intelD3D12Device)
    {
        auto refCount = device->AddRef();

        if (refCount == _intelD3D12DeviceRefTarget)
        {
            LOG_INFO("Destroying IGDExt context!");
            _intelD3D12Device = nullptr;
            IGDExtProxy::DestroyContext();
        }

        o_D3D12DeviceRelease(device);
    }

    auto result = o_D3D12DeviceRelease(device);

    return result;
}

static HRESULT hkCheckFeatureSupport(ID3D12Device* device, D3D12_FEATURE Feature, void* pFeatureSupportData,
                                     UINT FeatureSupportDataSize)
{
    auto result = o_CheckFeatureSupport(device, Feature, pFeatureSupportData, FeatureSupportDataSize);

    if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && Feature == D3D12_FEATURE_D3D12_OPTIONS9)
    {
        auto featureSupport = (D3D12_FEATURE_DATA_D3D12_OPTIONS9*) pFeatureSupportData;
        LOG_INFO("Spoofing AtomicInt64OnTypedResourceSupported");
        featureSupport->AtomicInt64OnTypedResourceSupported = true;
    }

    return result;
}

struct UE_D3D12_RESOURCE_DESC
{
    D3D12_RESOURCE_DIMENSION Dimension;
    UINT64 Alignment;
    UINT64 Width;
    UINT Height;
    UINT16 DepthOrArraySize;
    UINT16 MipLevels;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D12_TEXTURE_LAYOUT Layout;
    D3D12_RESOURCE_FLAGS Flags;

    // UE Part
    uint8_t PixelFormat { 0 };
    uint8_t UAVPixelFormat { 0 };
    bool bRequires64BitAtomicSupport : 1 = false;
    bool bReservedResource : 1 = false;
    bool bBackBuffer : 1 = false;
    bool bExternal : 1 = false;
};

static bool skipCommitedResource = false;

static HRESULT hkCreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* pHeapProperties,
                                         D3D12_HEAP_FLAGS HeapFlags, D3D12_RESOURCE_DESC* pDesc,
                                         D3D12_RESOURCE_STATES InitialResourceState,
                                         const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource,
                                         void** ppvResource)
{
    if (!skipCommitedResource)
    {
        auto ueDesc = reinterpret_cast<UE_D3D12_RESOURCE_DESC*>(pDesc);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && ueDesc != nullptr &&
            ueDesc->bRequires64BitAtomicSupport)
        {
            skipCommitedResource = true;
            auto result = IGDExtProxy::CreateCommitedResource(pHeapProperties, HeapFlags, pDesc, InitialResourceState,
                                                              pOptimizedClearValue, riidResource, ppvResource);
            skipCommitedResource = false;

            return result;
        }
    }

    return o_CreateCommittedResource(device, pHeapProperties, HeapFlags, pDesc, InitialResourceState,
                                     pOptimizedClearValue, riidResource, ppvResource);
}

static bool skipPlacedResource = false;

static HRESULT hkCreatePlacedResource(ID3D12Device* device, ID3D12Heap* pHeap, UINT64 HeapOffset,
                                      D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState,
                                      const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource)
{
    if (!skipPlacedResource)
    {
        auto ueDesc = reinterpret_cast<UE_D3D12_RESOURCE_DESC*>(pDesc);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && ueDesc != nullptr &&
            ueDesc->bRequires64BitAtomicSupport)
        {
            skipPlacedResource = true;
            auto result = IGDExtProxy::CreatePlacedResource(pHeap, HeapOffset, pDesc, InitialState,
                                                            pOptimizedClearValue, riid, ppvResource);
            skipPlacedResource = false;

            return result;
        }
    }

    return o_CreatePlacedResource(device, pHeap, HeapOffset, pDesc, InitialState, pOptimizedClearValue, riid,
                                  ppvResource);
}

static bool skipGetResourceAllocationInfo = false;

static D3D12_RESOURCE_ALLOCATION_INFO hkGetResourceAllocationInfo(ID3D12Device* device, UINT visibleMask,
                                                                  UINT numResourceDescs,
                                                                  D3D12_RESOURCE_DESC* pResourceDescs)
{
    if (State::Instance().currentD3D12Device != device)
        device = State::Instance().currentD3D12Device;

    if (!skipGetResourceAllocationInfo)
    {
        auto ueDesc = reinterpret_cast<UE_D3D12_RESOURCE_DESC*>(pResourceDescs);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && ueDesc != nullptr &&
            ueDesc->bRequires64BitAtomicSupport)
        {
            skipGetResourceAllocationInfo = true;
            auto result = IGDExtProxy::GetResourceAllocationInfo(visibleMask, numResourceDescs, pResourceDescs);
            skipGetResourceAllocationInfo = false;

            return result;
        }
    }

    return o_GetResourceAllocationInfo(device, visibleMask, numResourceDescs, pResourceDescs);
}

static void hkCreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* pDesc,
                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    if (pDesc == nullptr || device == nullptr)
        return;

    D3D12_SAMPLER_DESC newDesc {};

    newDesc.AddressU = pDesc->AddressU;
    newDesc.AddressV = pDesc->AddressV;
    newDesc.AddressW = pDesc->AddressW;
    newDesc.BorderColor[0] = pDesc->BorderColor[0];
    newDesc.BorderColor[1] = pDesc->BorderColor[1];
    newDesc.BorderColor[2] = pDesc->BorderColor[2];
    newDesc.BorderColor[3] = pDesc->BorderColor[3];
    newDesc.ComparisonFunc = pDesc->ComparisonFunc;

    if (Config::Instance()->AnisotropyOverride.has_value())
    {
        bool force = false;

        switch (pDesc->Filter)
        {
        case D3D12_FILTER_MIN_MAG_MIP_LINEAR:
        case D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT:
            newDesc.Filter = D3D12_FILTER_ANISOTROPIC;
            force = true;
            break;

        case D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR:
        case D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT:
            newDesc.Filter = D3D12_FILTER_COMPARISON_ANISOTROPIC;
            force = true;
            break;

        case D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR:
        case D3D12_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT:
            newDesc.Filter = D3D12_FILTER_MINIMUM_ANISOTROPIC;
            force = true;
            break;

        case D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR:
        case D3D12_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT:
            newDesc.Filter = D3D12_FILTER_MAXIMUM_ANISOTROPIC;
            force = true;
            break;
        }

        if (force)
        {
            LOG_DEBUG("Overriding {2:X} to anisotropic filtering {0} -> {1}", pDesc->MaxAnisotropy,
                      Config::Instance()->AnisotropyOverride.value(), (UINT) newDesc.Filter);

            newDesc.MaxAnisotropy = Config::Instance()->AnisotropyOverride.value();
        }
    }
    else
    {
        newDesc.Filter = pDesc->Filter;
        newDesc.MaxAnisotropy = pDesc->MaxAnisotropy;
    }

    newDesc.MaxLOD = pDesc->MaxLOD;
    newDesc.MinLOD = pDesc->MinLOD;
    newDesc.MipLODBias = pDesc->MipLODBias;

    if ((newDesc.MipLODBias < 0.0f && newDesc.MinLOD != newDesc.MaxLOD) ||
        Config::Instance()->MipmapBiasOverrideAll.value_or_default())
    {
        if (Config::Instance()->MipmapBiasOverride.has_value())
        {
            LOG_DEBUG("Overriding mipmap bias {0} -> {1}", pDesc->MipLODBias,
                      Config::Instance()->MipmapBiasOverride.value());

            if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                newDesc.MipLODBias = Config::Instance()->MipmapBiasOverride.value();
            else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                newDesc.MipLODBias = newDesc.MipLODBias * Config::Instance()->MipmapBiasOverride.value();
            else
                newDesc.MipLODBias = newDesc.MipLODBias + Config::Instance()->MipmapBiasOverride.value();
        }

        if (State::Instance().lastMipBiasMax < newDesc.MipLODBias)
            State::Instance().lastMipBiasMax = newDesc.MipLODBias;

        if (State::Instance().lastMipBias > newDesc.MipLODBias)
            State::Instance().lastMipBias = newDesc.MipLODBias;
    }

    return o_CreateSampler(device, &newDesc, DestDescriptor);
}

static HRESULT hkCreateSamplerState(ID3D11Device* This, const D3D11_SAMPLER_DESC* pSamplerDesc,
                                    ID3D11SamplerState** ppSamplerState)
{
    if (pSamplerDesc == nullptr || This == nullptr)
        return E_INVALIDARG;

    if (_d3d12Captured)
        return o_CreateSamplerState(This, pSamplerDesc, ppSamplerState);

    LOG_FUNC();

    D3D11_SAMPLER_DESC newDesc {};

    newDesc.AddressU = pSamplerDesc->AddressU;
    newDesc.AddressV = pSamplerDesc->AddressV;
    newDesc.AddressW = pSamplerDesc->AddressW;
    newDesc.ComparisonFunc = pSamplerDesc->ComparisonFunc;
    newDesc.BorderColor[0] = pSamplerDesc->BorderColor[0];
    newDesc.BorderColor[1] = pSamplerDesc->BorderColor[1];
    newDesc.BorderColor[2] = pSamplerDesc->BorderColor[2];
    newDesc.BorderColor[3] = pSamplerDesc->BorderColor[3];
    newDesc.MinLOD = pSamplerDesc->MinLOD;
    newDesc.MaxLOD = pSamplerDesc->MaxLOD;

    if (Config::Instance()->AnisotropyOverride.has_value())
    {
        bool force = false;

        switch (pSamplerDesc->Filter)
        {
        case D3D11_FILTER_MIN_MAG_MIP_LINEAR:
        case D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:
            newDesc.Filter = D3D11_FILTER_ANISOTROPIC;
            force = true;
            break;

        case D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR:
        case D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT:
            newDesc.Filter = D3D11_FILTER_COMPARISON_ANISOTROPIC;
            force = true;
            break;

        case D3D11_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR:
        case D3D11_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT:
            newDesc.Filter = D3D11_FILTER_MINIMUM_ANISOTROPIC;
            force = true;
            break;

        case D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR:
        case D3D11_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT:
            newDesc.Filter = D3D11_FILTER_MAXIMUM_ANISOTROPIC;
            force = true;
            break;
        }

        if (force)
        {
            LOG_DEBUG("Overriding {2:X} to anisotropic filtering {0} -> {1}", pSamplerDesc->MaxAnisotropy,
                      Config::Instance()->AnisotropyOverride.value(), (UINT) newDesc.Filter);

            newDesc.MaxAnisotropy = Config::Instance()->AnisotropyOverride.value();
        }
    }
    else
    {
        newDesc.Filter = pSamplerDesc->Filter;
        newDesc.MaxAnisotropy = pSamplerDesc->MaxAnisotropy;
    }

    newDesc.MipLODBias = pSamplerDesc->MipLODBias;

    if ((newDesc.MipLODBias < 0.0f && newDesc.MinLOD != newDesc.MaxLOD) ||
        Config::Instance()->MipmapBiasOverrideAll.value_or_default())
    {
        if (Config::Instance()->MipmapBiasOverride.has_value())
        {
            LOG_DEBUG("Overriding mipmap bias {0} -> {1}", pSamplerDesc->MipLODBias,
                      Config::Instance()->MipmapBiasOverride.value());

            if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                newDesc.MipLODBias = Config::Instance()->MipmapBiasOverride.value();
            else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                newDesc.MipLODBias = newDesc.MipLODBias * Config::Instance()->MipmapBiasOverride.value();
            else
                newDesc.MipLODBias = newDesc.MipLODBias + Config::Instance()->MipmapBiasOverride.value();
        }

        if (State::Instance().lastMipBiasMax < newDesc.MipLODBias)
            State::Instance().lastMipBiasMax = newDesc.MipLODBias;

        if (State::Instance().lastMipBias > newDesc.MipLODBias)
            State::Instance().lastMipBias = newDesc.MipLODBias;
    }

    return o_CreateSamplerState(This, &newDesc, ppSamplerState);
}

#pragma endregion

#pragma region Public hook methods

void HooksDx::HookDx12()
{
    if (o_D3D12CreateDevice != nullptr)
        return;

    LOG_DEBUG("");

    o_D3D12CreateDevice = D3d12Proxy::Hook_D3D12CreateDevice(hkD3D12CreateDevice);
    o_D3D12SerializeRootSignature = D3d12Proxy::Hook_D3D12SerializeRootSignature(hkD3D12SerializeRootSignature);
    o_D3D12SerializeVersionedRootSignature =
        D3d12Proxy::Hook_D3D12SerializeVersionedRootSignature(hkD3D12SerializeVersionedRootSignature);
}

void HooksDx::HookDx11(HMODULE dx11Module)
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

        DetourTransactionCommit();
    }
}

void HooksDx::HookDxgi()
{
    if (o_CreateDXGIFactory != nullptr)
        return;

    LOG_DEBUG("");

    o_CreateDXGIFactory = DxgiProxy::Hook_CreateDxgiFactory(hkCreateDXGIFactory);
    o_CreateDXGIFactory1 = DxgiProxy::Hook_CreateDxgiFactory1(hkCreateDXGIFactory1);
    o_CreateDXGIFactory2 = DxgiProxy::Hook_CreateDxgiFactory2(hkCreateDXGIFactory2);
}

void HooksDx::ReleaseDx12SwapChain(HWND hwnd)
{
    State::Instance().currentSwapchain = nullptr;

    IFGFeature_Dx12* fg = State::Instance().currentFG;
    if (fg == nullptr)
        return;

    // Skip if it's already being released
    if (fg->Mutex.getOwner() == 1)
        return;

    if (fg->SwapchainContext() != nullptr)
        fg->ReleaseSwapchain(hwnd);
}

DXGI_FORMAT HooksDx::CurrentSwapchainFormat()
{
    if (State::Instance().currentSwapchain == nullptr)
        return DXGI_FORMAT_UNKNOWN;

    DXGI_SWAP_CHAIN_DESC scDesc {};
    if (State::Instance().currentSwapchain->GetDesc(&scDesc) != S_OK)
        return DXGI_FORMAT_UNKNOWN;

    return scDesc.BufferDesc.Format;
}

void HooksDx::UnHookDx()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_D3D11CreateDevice != nullptr)
    {
        DetourDetach(&(PVOID&) o_D3D11CreateDevice, hkD3D11CreateDevice);
        o_D3D11CreateDevice = nullptr;
    }

    if (o_D3D11On12CreateDevice != nullptr)
    {
        DetourDetach(&(PVOID&) o_D3D11On12CreateDevice, hkD3D11On12CreateDevice);
        o_D3D11On12CreateDevice = nullptr;
    }

    if (o_D3D12CreateDevice != nullptr)
    {
        DetourDetach(&(PVOID&) o_D3D12CreateDevice, hkD3D12CreateDevice);
        o_D3D12CreateDevice = nullptr;
    }

    if (o_CreateDXGIFactory1 != nullptr)
    {
        DetourDetach(&(PVOID&) o_CreateDXGIFactory1, hkCreateDXGIFactory1);
        o_CreateDXGIFactory1 = nullptr;
    }

    if (o_CreateDXGIFactory2 != nullptr)
    {
        DetourDetach(&(PVOID&) o_CreateDXGIFactory2, hkCreateDXGIFactory2);
        o_CreateDXGIFactory2 = nullptr;
    }

    if (oCreateSwapChain != nullptr)
    {
        DetourDetach(&(PVOID&) oCreateSwapChain, hkCreateSwapChain);
        oCreateSwapChain = nullptr;
    }

    if (oCreateSwapChainForHwnd != nullptr)
    {
        DetourDetach(&(PVOID&) oCreateSwapChainForHwnd, hkCreateSwapChainForHwnd);
        oCreateSwapChainForHwnd = nullptr;
    }

    if (o_CreateSampler != nullptr)
    {
        DetourDetach(&(PVOID&) o_CreateSampler, hkCreateSampler);
        o_CreateSampler = nullptr;
    }

    DetourTransactionCommit();
    _isInited = false;
}

#pragma endregion
