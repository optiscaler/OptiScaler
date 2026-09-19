#include "pch.h"
#include "ResTrack_dx12.h"

#include <Config.h>
#include <State.h>
#include <Util.h>

#include <menu/menu_overlay_dx.h>

#include <algorithm>
#include <future>

#include <d3dcommon.h>
#include <detours/detours.h>
#include <magic_enum_utility.hpp>
#include <include/d3dx/d3dx12.h>

#ifndef STDMETHODCALLTYPE
#include <Unknwn.h> // or <objbase.h> to get STDMETHODCALLTYPE
#endif

#ifdef USE_SPINLOCK_MUTEX
#define LOCK_GUARD(mutex) std::lock_guard<SpinLock> name(mutex)
#else
#define LOCK_GUARD(mutex) std::lock_guard<std::mutex> name(mutex)
#endif

// Device hooks for FG
typedef void(STDMETHODCALLTYPE* PFN_CreateRenderTargetView)(ID3D12Device* This, ID3D12Resource* pResource,
                                                            D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
                                                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_CreateShaderResourceView)(ID3D12Device* This, ID3D12Resource* pResource,
                                                              D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
                                                              D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_CreateUnorderedAccessView)(ID3D12Device* This, ID3D12Resource* pResource,
                                                               ID3D12Resource* pCounterResource,
                                                               D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc,
                                                               D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_CreateDepthStencilView)(ID3D12Device* This, ID3D12Resource* pResource,
                                                            const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc,
                                                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_CreateConstantBufferView)(ID3D12Device* This,
                                                              const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc,
                                                              D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

typedef void(STDMETHODCALLTYPE* PFN_CreateSampler)(ID3D12Device* This, const D3D12_SAMPLER_DESC* pDesc,
                                                   D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateDescriptorHeap)(ID3D12Device* This,
                                                             D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc,
                                                             REFIID riid, void** ppvHeap);
typedef ULONG(STDMETHODCALLTYPE* PFN_HeapRelease)(ID3D12DescriptorHeap* This);
typedef void(STDMETHODCALLTYPE* PFN_CopyDescriptors)(ID3D12Device* This, UINT NumDestDescriptorRanges,
                                                     D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts,
                                                     UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges,
                                                     D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts,
                                                     UINT* pSrcDescriptorRangeSizes,
                                                     D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
typedef void(STDMETHODCALLTYPE* PFN_CopyDescriptorsSimple)(ID3D12Device* This, UINT NumDescriptors,
                                                           D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
                                                           D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
                                                           D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);

// Command list hooks for HUDfix
typedef void(STDMETHODCALLTYPE* PFN_OMSetRenderTargets)(ID3D12GraphicsCommandList* This,
                                                        UINT NumRenderTargetDescriptors,
                                                        D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
                                                        BOOL RTsSingleHandleToDescriptorRange,
                                                        D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_SetGraphicsRootDescriptorTable)(ID3D12GraphicsCommandList* This,
                                                                    UINT RootParameterIndex,
                                                                    D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_SetComputeRootDescriptorTable)(ID3D12GraphicsCommandList* This,
                                                                   UINT RootParameterIndex,
                                                                   D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
typedef void(STDMETHODCALLTYPE* PFN_DrawIndexedInstanced)(ID3D12GraphicsCommandList* This, UINT IndexCountPerInstance,
                                                          UINT InstanceCount, UINT StartIndexLocation,
                                                          INT BaseVertexLocation, UINT StartInstanceLocation);
typedef void(STDMETHODCALLTYPE* PFN_DrawInstanced)(ID3D12GraphicsCommandList* This, UINT VertexCountPerInstance,
                                                   UINT InstanceCount, UINT StartVertexLocation,
                                                   UINT StartInstanceLocation);
typedef void(STDMETHODCALLTYPE* PFN_Dispatch)(ID3D12GraphicsCommandList* This, UINT ThreadGroupCountX,
                                              UINT ThreadGroupCountY, UINT ThreadGroupCountZ);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Reset)(ID3D12GraphicsCommandList* This, ID3D12CommandAllocator* pAllocator,
                                              ID3D12PipelineState* pInitialState);
typedef void(STDMETHODCALLTYPE* PFN_ClearState)(ID3D12GraphicsCommandList* This, ID3D12PipelineState* pPipelineState);

// Original method calls for device
static PFN_CreateRenderTargetView o_CreateRenderTargetView = nullptr;
static PFN_CreateShaderResourceView o_CreateShaderResourceView = nullptr;
static PFN_CreateUnorderedAccessView o_CreateUnorderedAccessView = nullptr;
static PFN_CreateDepthStencilView o_CreateDepthStencilView = nullptr;
static PFN_CreateConstantBufferView o_CreateConstantBufferView = nullptr;
static PFN_CreateSampler o_CreateSampler = nullptr;

static PFN_CreateDescriptorHeap o_CreateDescriptorHeap = nullptr;
static PFN_HeapRelease o_HeapRelease = nullptr;
static PFN_CopyDescriptors o_CopyDescriptors = nullptr;
static PFN_CopyDescriptorsSimple o_CopyDescriptorsSimple = nullptr;

// Original method calls for command list
static PFN_Dispatch o_Dispatch = nullptr;
static PFN_DrawInstanced o_DrawInstanced = nullptr;
static PFN_DrawIndexedInstanced o_DrawIndexedInstanced = nullptr;
static PFN_Reset o_Reset = nullptr;
static PFN_ClearState o_ClearState = nullptr;

static PFN_OMSetRenderTargets o_OMSetRenderTargets = nullptr;
static PFN_SetGraphicsRootDescriptorTable o_SetGraphicsRootDescriptorTable = nullptr;
static PFN_SetComputeRootDescriptorTable o_SetComputeRootDescriptorTable = nullptr;

static std::mutex _hudlessTrackMutex;
static ankerl::unordered_dense::map<ID3D12GraphicsCommandList*,
                                    ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo>>
    fgPossibleHudless[BUFFER_COUNT];

// heaps section

static std::shared_mutex _heapRegistryMutex;
static std::vector<std::shared_ptr<HeapInfo>> fgHeaps;

struct HeapCacheTLS
{
    unsigned genSeen = 0;
    std::shared_ptr<HeapInfo> heap;
    uint64_t heapVersion = 0;
};

static thread_local HeapCacheTLS cache;
static thread_local HeapCacheTLS cacheRTV;
static thread_local HeapCacheTLS cacheCBV;
static thread_local HeapCacheTLS cacheSRV;
static thread_local HeapCacheTLS cacheUAV;
static std::atomic<unsigned> gHeapGeneration { 1 };

static thread_local HeapCacheTLS cacheGR;
static thread_local HeapCacheTLS cacheCR;

void __stdcall ResTrack_Dx12::ResourceDestroyed(void* data)
{
    if (data == nullptr || State::Instance().isShuttingDown)
        return;

    auto* resource = static_cast<ID3D12Resource*>(data);
    std::vector<TrackedResourceSlot> toClean;

    {
        std::lock_guard lock(_trackedResourcesMutex);
        if (auto it = _trackedResources.find(resource); it != _trackedResources.end())
        {
            toClean = std::move(it->second);
            _trackedResources.erase(it);
        }
    }

    Hudfix_Dx12::RemoveResourceFromTracking(resource);

    // Clean descriptor slots
    for (const auto& slot : toClean)
    {
        if (auto heap = slot.heap.lock())
            heap->ClearSlotIfMatches(slot.index, resource);
    }
}

bool ResTrack_Dx12::TrackResourceRelease(ID3D12Resource* resource)
{
    if (resource == nullptr || State::Instance().isShuttingDown)
        return false;

    {
        std::lock_guard trackedLock(_trackedResourcesMutex);
        if (_trackedResources.contains(resource))
            return true;
    }

    // Only new registrations take this mutex
    std::lock_guard lifetimeLock(_resourceLifetimeMutex);

    {
        std::lock_guard trackedLock(_trackedResourcesMutex);
        if (_trackedResources.contains(resource))
            return true;
    }

    ID3DDestructionNotifier* notifier = nullptr;
    auto result = resource->QueryInterface(IID_PPV_ARGS(&notifier));
    if (FAILED(result) || notifier == nullptr)
    {
        LOG_DEBUG("ID3DDestructionNotifier is not available for resource {:X}, result: {:X}", (size_t) resource,
                  (UINT) result);
        return false;
    }

    UINT callbackId = 0;
    result = notifier->RegisterDestructionCallback(&ResTrack_Dx12::ResourceDestroyed, resource, &callbackId);
    if (FAILED(result))
    {
        LOG_WARN("Can't register destruction callback for resource {:X}, result: {:X}", (size_t) resource,
                 (UINT) result);
        notifier->Release();
        return false;
    }

    {
        std::lock_guard trackedLock(_trackedResourcesMutex);
        _trackedResources.try_emplace(resource);
    }

    notifier->Release();
    return true;
}

bool ResTrack_Dx12::CheckResource(ID3D12Resource* resource, ResourceInfo* outInfo)
{
    if (State::Instance().isShuttingDown)
        return false;

    auto resDesc = resource->GetDesc();

    if (resDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        return false;

    // depth etc
    if (resDesc.DepthOrArraySize != 1 || resDesc.SampleDesc.Count != 1)
        return false;

    // depth, rt, video etc
    constexpr auto unsupportedFlags =
        D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
        D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE |
        D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY;

    // Early reject
    if ((resDesc.Flags & unsupportedFlags) != 0)
        return false;

    auto& s = State::Instance();
    const uint32_t width = s.currentSwapchainDesc.BufferDesc.Width;
    const uint32_t height = s.currentSwapchainDesc.BufferDesc.Height;

    if (resDesc.Height != height || resDesc.Width != width)
    {
        // Need to make these tolarances global
        const auto toleranceX = width / 20;
        const auto toleranceY = height / 20;

        if (!(resDesc.Height >= height - toleranceY && resDesc.Height <= height + toleranceY &&
              resDesc.Width >= width - toleranceX && resDesc.Width <= width + toleranceX))
        {
            return false;
        }
    }

    if (outInfo != nullptr)
    {
        if (!TrackResourceRelease(resource))
            return false;

        outInfo->buffer = resource;
        outInfo->width = resDesc.Width;
        outInfo->height = resDesc.Height;
        outInfo->format = resDesc.Format;
        outInfo->flags = resDesc.Flags;
        outInfo->lifetimeTracked = true;
    }

    return true;
}

inline static IID streamlineRiid {};
inline static std::once_flag streamlineRiidInitFlag;

bool ResTrack_Dx12::CheckForRealObject(const std::string functionName, IUnknown* pObject, IUnknown** ppRealObject)
{
    std::call_once(streamlineRiidInitFlag,
                   []() { IIDFromString(L"{ADEC44E2-61F0-45C3-AD9F-1B37379284FF}", &streamlineRiid); });

    auto qResult = pObject->QueryInterface(streamlineRiid, (void**) ppRealObject);

    if (qResult == S_OK && *ppRealObject != nullptr)
    {
        LOG_INFO("{} Streamline proxy found!", functionName);
        (*ppRealObject)->Release();
        return true;
    }

    return false;
}

#pragma region Resource methods

bool ResTrack_Dx12::CreateBufferResource(ID3D12Device* InDevice, ResourceInfo* InSource, D3D12_RESOURCE_STATES InState,
                                         ID3D12Resource** OutResource)
{
    if (InDevice == nullptr || InSource == nullptr || InSource->buffer == nullptr)
        return false;

    if (*OutResource != nullptr)
    {
        auto bufDesc = (*OutResource)->GetDesc();

        if (bufDesc.Width != (UINT64) (InSource->width) || bufDesc.Height != (UINT) (InSource->height) ||
            bufDesc.Format != InSource->format)
        {
            (*OutResource)->Release();
            (*OutResource) = nullptr;
        }
        else
            return true;
    }

    D3D12_HEAP_PROPERTIES heapProperties;
    D3D12_HEAP_FLAGS heapFlags;
    HRESULT hr = InSource->buffer->GetHeapProperties(&heapProperties, &heapFlags);

    if (hr != S_OK)
    {
        LOG_ERROR("GetHeapProperties result: {0:X}", (UINT64) hr);
        return false;
    }

    D3D12_RESOURCE_DESC texDesc = InSource->buffer->GetDesc();
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = InDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &texDesc, InState, nullptr,
                                           IID_PPV_ARGS(OutResource));

    if (hr != S_OK)
    {
        LOG_ERROR("CreateCommittedResource result: {0:X}", (UINT64) hr);
        return false;
    }

    (*OutResource)->SetName(L"fgHudlessSCBufferCopy");
    return true;
}

void ResTrack_Dx12::ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                    D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState)
{
    if (InBeforeState == InAfterState)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = InResource;
    barrier.Transition.StateBefore = InBeforeState;
    barrier.Transition.StateAfter = InAfterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    InCommandList->ResourceBarrier(1, &barrier);
}

#pragma endregion

#pragma region Heap helpers

SIZE_T ResTrack_Dx12::GetGPUHandle(ID3D12Device* This, SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    std::shared_lock lock(_heapRegistryMutex);
    for (const auto& heap : fgHeaps)
    {
        if (heap != nullptr && heap->active.load(std::memory_order_acquire) && heap->cpuStart <= cpuHandle &&
            heap->cpuEnd > cpuHandle && heap->gpuStart != 0)
        {
            auto incSize = This->GetDescriptorHandleIncrementSize(type);
            auto addr = cpuHandle - heap->cpuStart;
            auto index = addr / incSize;
            return heap->gpuStart + (index * incSize);
        }
    }

    return NULL;
}

SIZE_T ResTrack_Dx12::GetCPUHandle(ID3D12Device* This, SIZE_T gpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    std::shared_lock lock(_heapRegistryMutex);
    for (const auto& heap : fgHeaps)
    {
        if (heap != nullptr && heap->active.load(std::memory_order_acquire) && heap->gpuStart <= gpuHandle &&
            heap->gpuEnd > gpuHandle && heap->cpuStart != 0)
        {
            auto incSize = This->GetDescriptorHandleIncrementSize(type);
            auto addr = gpuHandle - heap->gpuStart;
            auto index = addr / incSize;
            return heap->cpuStart + (index * incSize);
        }
    }

    return NULL;
}

static std::shared_ptr<HeapInfo> FindHeapByCpuHandle(SIZE_T cpuHandle, HeapCacheTLS& heapCache)
{
    const auto currentGen = gHeapGeneration.load(std::memory_order_acquire);
    auto* cachedHeap = heapCache.heap.get();
    if (heapCache.genSeen == currentGen && cachedHeap != nullptr &&
        cachedHeap->version.load(std::memory_order_relaxed) == heapCache.heapVersion &&
        cachedHeap->active.load(std::memory_order_acquire) && cachedHeap->cpuStart <= cpuHandle &&
        cpuHandle < cachedHeap->cpuEnd)
    {
        return heapCache.heap;
    }

    std::shared_lock lock(_heapRegistryMutex);
    const auto registryGen = gHeapGeneration.load(std::memory_order_acquire);
    for (const auto& heap : fgHeaps)
    {
        if (heap != nullptr && heap->active.load(std::memory_order_acquire) && heap->cpuStart <= cpuHandle &&
            cpuHandle < heap->cpuEnd)
        {
            heapCache.genSeen = registryGen;
            heapCache.heap = heap;
            heapCache.heapVersion = heap->version.load(std::memory_order_relaxed);
            return heap;
        }
    }

    heapCache.genSeen = registryGen;
    heapCache.heapVersion = 0;
    heapCache.heap.reset();
    return nullptr;
}

static std::shared_ptr<HeapInfo> FindHeapByGpuHandle(SIZE_T gpuHandle, HeapCacheTLS& heapCache)
{
    if (gpuHandle == NULL)
        return nullptr;

    const auto currentGen = gHeapGeneration.load(std::memory_order_acquire);
    auto* cachedHeap = heapCache.heap.get();
    if (heapCache.genSeen == currentGen && cachedHeap != nullptr &&
        cachedHeap->version.load(std::memory_order_relaxed) == heapCache.heapVersion &&
        cachedHeap->active.load(std::memory_order_acquire) && cachedHeap->gpuStart <= gpuHandle &&
        gpuHandle < cachedHeap->gpuEnd)
    {
        return heapCache.heap;
    }

    std::shared_lock lock(_heapRegistryMutex);
    const auto registryGen = gHeapGeneration.load(std::memory_order_acquire);
    for (const auto& heap : fgHeaps)
    {
        if (heap != nullptr && heap->active.load(std::memory_order_acquire) && heap->gpuStart <= gpuHandle &&
            gpuHandle < heap->gpuEnd)
        {
            heapCache.genSeen = registryGen;
            heapCache.heap = heap;
            heapCache.heapVersion = heap->version.load(std::memory_order_relaxed);
            return heap;
        }
    }

    heapCache.genSeen = registryGen;
    heapCache.heapVersion = 0;
    heapCache.heap.reset();
    return nullptr;
}

std::shared_ptr<HeapInfo> ResTrack_Dx12::GetHeapByCpuHandleCBV(SIZE_T cpuHandle)
{
    return FindHeapByCpuHandle(cpuHandle, cacheCBV);
}

std::shared_ptr<HeapInfo> ResTrack_Dx12::GetHeapByCpuHandleRTV(SIZE_T cpuHandle)
{
    return FindHeapByCpuHandle(cpuHandle, cacheRTV);
}

std::shared_ptr<HeapInfo> ResTrack_Dx12::GetHeapByCpuHandleSRV(SIZE_T cpuHandle)
{
    return FindHeapByCpuHandle(cpuHandle, cacheSRV);
}

std::shared_ptr<HeapInfo> ResTrack_Dx12::GetHeapByCpuHandleUAV(SIZE_T cpuHandle)
{
    return FindHeapByCpuHandle(cpuHandle, cacheUAV);
}

std::shared_ptr<HeapInfo> ResTrack_Dx12::GetHeapByCpuHandle(SIZE_T cpuHandle)
{
    return FindHeapByCpuHandle(cpuHandle, cache);
}

std::shared_ptr<HeapInfo> ResTrack_Dx12::GetHeapByGpuHandleGR(SIZE_T gpuHandle)
{
    return FindHeapByGpuHandle(gpuHandle, cacheGR);
}

std::shared_ptr<HeapInfo> ResTrack_Dx12::GetHeapByGpuHandleCR(SIZE_T gpuHandle)
{
    return FindHeapByGpuHandle(gpuHandle, cacheCR);
}

#pragma endregion

#pragma region Hudless methods

static bool IsDescriptorEnabled(ResourceType type)
{
    auto* config = Config::Instance();

    switch (type)
    {
    case RTV:
        return !config->FGHudfixDisableRTV.value_or_default();

    case SRV:
        return !config->FGHudfixDisableSRV.value_or_default();

    case UAV:
        return !config->FGHudfixDisableUAV.value_or_default();

    default:
        return false;
    }
}

bool ResTrack_Dx12::IsHudFixActive()
{
    if (!Config::Instance()->FGEnabled.value_or_default() || !Config::Instance()->FGHUDFix.value_or_default())
    {
        LOG_TRACK(
            "!Config::Instance()->FGEnabled.value_or_default() || !Config::Instance()->FGHUDFix.value_or_default()");
        return false;
    }

    if (State::Instance().currentFG == nullptr || State::Instance().currentFeature == nullptr ||
        State::Instance().fgChanged)
    {
        LOG_TRACK("State::Instance().currentFG == nullptr || State::Instance().currentFeature == nullptr || "
                  "State::Instance().fgChanged");
        return false;
    }

    if (!State::Instance().currentFG->IsActive())
    {
        LOG_TRACK("!State::Instance().currentFG->IsActive()");
        return false;
    }

    if (!_presentDone)
    {
        LOG_TRACK("!_presentDone");
        return false;
    }

    if (Hudfix_Dx12::SkipHudlessChecks())
    {
        LOG_TRACK("!Hudfix_Dx12::SkipHudlessChecks()");
        return false;
    }

    if (!Hudfix_Dx12::IsResourceCheckActive())
    {
        // LOG_TRACK("!Hudfix_Dx12::IsResourceCheckActive()");
        return false;
    }

    return true;
}

#pragma endregion

#pragma region Resource input hooks

void ResTrack_Dx12::hkCreateRenderTargetView(ID3D12Device* This, ID3D12Resource* pResource,
                                             D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
                                             D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    // force hdr for swapchain buffer
    if (pResource != nullptr && pDesc != nullptr && Config::Instance()->ForceHDR.value_or_default())
    {
        for (size_t i = 0; i < State::Instance().scBuffers.size(); i++)
        {
            if (State::Instance().scBuffers[i] == pResource)
            {
                if (Config::Instance()->UseHDR10.value_or_default())
                    pDesc->Format = DXGI_FORMAT_R10G10B10A2_UNORM;
                else
                    pDesc->Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

                break;
            }
        }
    }

    o_CreateRenderTargetView(This, pResource, pDesc, DestDescriptor);

    ResourceInfo resInfo {};
    if (pResource == nullptr || !CheckResource(pResource, &resInfo))
    {
        auto heap = GetHeapByCpuHandleRTV(DestDescriptor.ptr);

        if (heap != nullptr)
            heap->ClearByCpuHandle(DestDescriptor.ptr);

        return;
    }

    // if (!CheckResource(pResource))
    //     return;

    auto heap = GetHeapByCpuHandleRTV(DestDescriptor.ptr);
    if (heap != nullptr)
    {
        resInfo.type = RTV;
        resInfo.captureInfo = CaptureInfo::CreateRTV;
        heap->SetByCpuHandle(DestDescriptor.ptr, resInfo);
    }
    // else
    //{
    //     LOG_TRACK("Heap not found for RTV: {:X}", DestDescriptor.ptr);
    // }
}

void ResTrack_Dx12::hkCreateShaderResourceView(ID3D12Device* This, ID3D12Resource* pResource,
                                               D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
                                               D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    // force hdr for swapchain buffer
    if (pResource != nullptr && pDesc != nullptr && Config::Instance()->ForceHDR.value_or_default())
    {
        for (size_t i = 0; i < State::Instance().scBuffers.size(); i++)
        {
            if (State::Instance().scBuffers[i] == pResource)
            {
                if (Config::Instance()->UseHDR10.value_or_default())
                    pDesc->Format = DXGI_FORMAT_R10G10B10A2_UNORM;
                else
                    pDesc->Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

                break;
            }
        }
    }

    o_CreateShaderResourceView(This, pResource, pDesc, DestDescriptor);

    ResourceInfo resInfo {};
    if (pResource == nullptr || !CheckResource(pResource, &resInfo))
    {
        auto heap = GetHeapByCpuHandleSRV(DestDescriptor.ptr);

        if (heap != nullptr)
            heap->ClearByCpuHandle(DestDescriptor.ptr);

        return;
    }

    // if (!CheckResource(pResource))
    //     return;

    auto heap = GetHeapByCpuHandleSRV(DestDescriptor.ptr);
    if (heap != nullptr)
    {
        resInfo.type = SRV;
        resInfo.captureInfo = CaptureInfo::CreateSRV;
        heap->SetByCpuHandle(DestDescriptor.ptr, resInfo);
    }
    // else
    //{
    //     LOG_TRACK("Heap not found for SRV: {:X}", DestDescriptor.ptr);
    // }
}

void ResTrack_Dx12::hkCreateUnorderedAccessView(ID3D12Device* This, ID3D12Resource* pResource,
                                                ID3D12Resource* pCounterResource,
                                                D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc,
                                                D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    if (pResource != nullptr && pDesc != nullptr && Config::Instance()->ForceHDR.value_or_default())
    {
        for (size_t i = 0; i < State::Instance().scBuffers.size(); i++)
        {
            if (State::Instance().scBuffers[i] == pResource)
            {
                if (Config::Instance()->UseHDR10.value_or_default())
                    pDesc->Format = DXGI_FORMAT_R10G10B10A2_UNORM;
                else
                    pDesc->Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

                break;
            }
        }
    }

    o_CreateUnorderedAccessView(This, pResource, pCounterResource, pDesc, DestDescriptor);

    ResourceInfo resInfo {};
    if (pResource == nullptr || !CheckResource(pResource, &resInfo))
    {
        auto heap = GetHeapByCpuHandleUAV(DestDescriptor.ptr);

        if (heap != nullptr)
            heap->ClearByCpuHandle(DestDescriptor.ptr);

        return;
    }

    // if (!CheckResource(pResource))
    //     return;

    auto heap = GetHeapByCpuHandleUAV(DestDescriptor.ptr);
    if (heap != nullptr)
    {
        resInfo.type = UAV;
        resInfo.captureInfo = CaptureInfo::CreateUAV;
        heap->SetByCpuHandle(DestDescriptor.ptr, resInfo);
    }
    // else
    //{
    //     LOG_TRACK("Heap not found for UAV: {:X}", DestDescriptor.ptr);
    // }
}

#pragma endregion

#pragma region Heap hooks

static ULONG STDMETHODCALLTYPE hkHeapRelease(ID3D12DescriptorHeap* This)
{
    if (State::Instance().isShuttingDown)
        return o_HeapRelease(This);

    std::shared_ptr<HeapInfo> heapInfo;
    {
        std::shared_lock lock(_heapRegistryMutex);
        for (const auto& heap : fgHeaps)
        {
            if (heap != nullptr && heap->heap == This && heap->active.load(std::memory_order_acquire))
            {
                heapInfo = heap;
                break;
            }
        }
    }

    if (heapInfo == nullptr)
        return o_HeapRelease(This);

    This->AddRef();
    if (o_HeapRelease(This) <= 1)
    {
        bool deactivated = false;
        {
            std::unique_lock lock(_heapRegistryMutex);
            deactivated = heapInfo->DeactivateAndClear();
        }

        if (deactivated)
        {
            LOG_INFO("Heap released: {:X}", (size_t) This);
            gHeapGeneration.fetch_add(1, std::memory_order_release);
        }
    }

    return o_HeapRelease(This);
}

HRESULT ResTrack_Dx12::hkCreateDescriptorHeap(ID3D12Device* This, D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc,
                                              REFIID riid, void** ppvHeap)
{
    auto result = o_CreateDescriptorHeap(This, pDescriptorHeapDesc, riid, ppvHeap);

    if (State::Instance().skipHeapCapture)
        return result;

    // try to calculate handle ranges for heap
    if (result == S_OK && (pDescriptorHeapDesc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
                           pDescriptorHeapDesc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV))
    {
        auto heap = (ID3D12DescriptorHeap*) (*ppvHeap);

        if (!o_HeapRelease)
        {
            PVOID* vtbl = *(PVOID**) heap;
            o_HeapRelease = (PFN_HeapRelease) vtbl[2];
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&) o_HeapRelease, hkHeapRelease);
            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to hook Heap Release: {:X}", detourResult);
                o_HeapRelease = nullptr;
            }
        }

        auto increment = This->GetDescriptorHandleIncrementSize(pDescriptorHeapDesc->Type);
        auto numDescriptors = pDescriptorHeapDesc->NumDescriptors;
        auto cpuStart = (SIZE_T) (heap->GetCPUDescriptorHandleForHeapStart().ptr);
        auto cpuEnd = cpuStart + (increment * numDescriptors);
        auto gpuStart = (SIZE_T) (heap->GetGPUDescriptorHandleForHeapStart().ptr);
        auto gpuEnd = gpuStart + (increment * numDescriptors);
        auto type = (UINT) pDescriptorHeapDesc->Type;

        LOG_TRACE("Heap: {:X}, Heap type: {}, Cpu: {}-{}, Gpu: {}-{}, Desc count: {}", (size_t) *ppvHeap, type,
                  cpuStart, cpuEnd, gpuStart, gpuEnd, numDescriptors);
        {
            std::unique_lock lock(_heapRegistryMutex);
            size_t count = fgHeaps.size();
            bool foundEmpty = false;
            for (size_t i = 0; i < count; i++)
            {
                if (fgHeaps[i] != nullptr && !fgHeaps[i]->active.load(std::memory_order_acquire))
                {

                    fgHeaps[i] = std::make_shared<HeapInfo>(heap, cpuStart, cpuEnd, gpuStart, gpuEnd, numDescriptors,
                                                            increment, type);

                    gHeapGeneration.fetch_add(1, std::memory_order_release);
                    foundEmpty = true;
                    LOG_DEBUG("Reusing empty heap slot: {}", i);
                    break;
                }
            }

            if (!foundEmpty)
            {
                // Reallocate vector if needed
                if (fgHeaps.capacity() == fgHeaps.size())
                    fgHeaps.reserve(fgHeaps.size() + 65536);

                fgHeaps.push_back(std::make_shared<HeapInfo>(heap, cpuStart, cpuEnd, gpuStart, gpuEnd, numDescriptors,
                                                             increment, type));

                gHeapGeneration.fetch_add(1, std::memory_order_release);
                LOG_DEBUG("Adding new heap slot: {}", fgHeaps.size() - 1);
            }
        }
    }
    else
    {
        if (ppvHeap != nullptr && *ppvHeap != nullptr)
        {
            auto heap = (ID3D12DescriptorHeap*) (*ppvHeap);
            LOG_TRACE("Skipping, Heap type: {}, Cpu: {}, Gpu: {}", (UINT) pDescriptorHeapDesc->Type,
                      heap->GetCPUDescriptorHandleForHeapStart().ptr, heap->GetGPUDescriptorHandleForHeapStart().ptr);
        }
    }

    return result;
}

void ResTrack_Dx12::hkCopyDescriptors(ID3D12Device* This, UINT NumDestDescriptorRanges,
                                      D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts,
                                      UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges,
                                      D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts,
                                      UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType)
{
    o_CopyDescriptors(This, NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes,
                      NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes, DescriptorHeapsType);

    // Early exit conditions - consistent validation
    if (DescriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
        DescriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
        return;

    if (NumDestDescriptorRanges == 0 || pDestDescriptorRangeStarts == nullptr)
        return;

    if (!Config::Instance()->FGAlwaysTrackHeaps.value_or_default() && !IsHudFixActive())
        return;

    const UINT inc = This->GetDescriptorHandleIncrementSize(DescriptorHeapsType);

    // Validate that we have source descriptors to copy
    bool haveSources = (NumSrcDescriptorRanges > 0 && pSrcDescriptorRangeStarts != nullptr);

    // Track positions in both source and destination ranges
    UINT srcRangeIndex = 0;
    UINT srcOffsetInRange = 0;
    UINT destRangeIndex = 0;
    UINT destOffsetInRange = 0;

    // Cache heap and direct-index state for each active range.
    std::shared_ptr<HeapInfo> cachedDestHeap;
    SIZE_T cachedDestRangeStart = 0;
    UINT cachedDestRangeSize = 0;
    UINT cachedDestBaseIndex = 0;
    bool cachedDestRangeFits = false;
    std::shared_ptr<HeapInfo> cachedSrcHeap;
    SIZE_T cachedSrcRangeStart = 0;
    UINT cachedSrcRangeSize = 0;
    UINT cachedSrcBaseIndex = 0;
    bool cachedSrcRangeFits = false;

    // Process all destination descriptors
    while (destRangeIndex < NumDestDescriptorRanges)
    {
        // Update destination heap cache if we've moved to a new range
        if (destOffsetInRange == 0)
        {
            cachedDestRangeStart = pDestDescriptorRangeStarts[destRangeIndex].ptr;
            cachedDestRangeSize =
                (pDestDescriptorRangeSizes == nullptr) ? 1 : pDestDescriptorRangeSizes[destRangeIndex];
            cachedDestHeap = GetHeapByCpuHandle(cachedDestRangeStart);
            cachedDestRangeFits = cachedDestHeap != nullptr &&
                                  cachedDestHeap->GetCpuIndex(cachedDestRangeStart, cachedDestBaseIndex) &&
                                  cachedDestRangeSize <= cachedDestHeap->numDescriptors - cachedDestBaseIndex;
        }

        // Get or update source information
        ResourceInfo srcInfo {};
        bool haveSrcInfo = false;
        if (haveSources && srcRangeIndex < NumSrcDescriptorRanges)
        {
            // Update source heap cache if we've moved to a new range
            if (srcOffsetInRange == 0)
            {
                cachedSrcRangeStart = pSrcDescriptorRangeStarts[srcRangeIndex].ptr;
                cachedSrcRangeSize =
                    (pSrcDescriptorRangeSizes == nullptr) ? 1 : pSrcDescriptorRangeSizes[srcRangeIndex];
                cachedSrcHeap = GetHeapByCpuHandle(cachedSrcRangeStart);
                cachedSrcRangeFits = cachedSrcHeap != nullptr &&
                                     cachedSrcHeap->GetCpuIndex(cachedSrcRangeStart, cachedSrcBaseIndex) &&
                                     cachedSrcRangeSize <= cachedSrcHeap->numDescriptors - cachedSrcBaseIndex;
            }

            if (cachedSrcHeap != nullptr)
            {
                if (cachedSrcRangeFits)
                {
                    haveSrcInfo = cachedSrcHeap->GetByIndex(cachedSrcBaseIndex + srcOffsetInRange, srcInfo);
                }
                else
                {
                    const SIZE_T srcHandle = cachedSrcRangeStart + (static_cast<SIZE_T>(srcOffsetInRange) * inc);
                    haveSrcInfo = cachedSrcHeap->GetByCpuHandle(srcHandle, srcInfo);
                }
            }

            // Advance source position
            srcOffsetInRange++;
            if (srcOffsetInRange >= cachedSrcRangeSize)
            {
                srcOffsetInRange = 0;
                srcRangeIndex++;
            }
        }

        if (cachedDestHeap != nullptr)
        {
            if (cachedDestRangeFits)
            {
                const auto destIndex = cachedDestBaseIndex + destOffsetInRange;
                if (haveSrcInfo)
                    cachedDestHeap->SetByIndex(destIndex, srcInfo);
                else
                    cachedDestHeap->ClearByIndex(destIndex);
            }
            else
            {
                const SIZE_T destHandle = cachedDestRangeStart + (static_cast<SIZE_T>(destOffsetInRange) * inc);
                if (haveSrcInfo)
                    cachedDestHeap->SetByCpuHandle(destHandle, srcInfo);
                else
                    cachedDestHeap->ClearByCpuHandle(destHandle);
            }
        }

        // Advance destination position
        destOffsetInRange++;
        if (destOffsetInRange >= cachedDestRangeSize)
        {
            destOffsetInRange = 0;
            destRangeIndex++;
        }
    }
}

void ResTrack_Dx12::hkCopyDescriptorsSimple(ID3D12Device* This, UINT NumDescriptors,
                                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
                                            D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
                                            D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType)
{
    o_CopyDescriptorsSimple(This, NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart,
                            DescriptorHeapsType);

    if (DescriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
        DescriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
        return;

    if (!Config::Instance()->FGAlwaysTrackHeaps.value_or_default() && !IsHudFixActive())
        return;

    if (NumDescriptors == 0)
        return;

    auto srcHeap = SrcDescriptorRangeStart.ptr != 0 ? GetHeapByCpuHandle(SrcDescriptorRangeStart.ptr) : nullptr;
    auto dstHeap = GetHeapByCpuHandle(DestDescriptorRangeStart.ptr);

    UINT srcBaseIndex = 0;
    UINT dstBaseIndex = 0;
    const bool srcRangeFits = SrcDescriptorRangeStart.ptr == 0 ||
                              (srcHeap != nullptr && srcHeap->GetCpuIndex(SrcDescriptorRangeStart.ptr, srcBaseIndex) &&
                               NumDescriptors <= srcHeap->numDescriptors - srcBaseIndex);
    const bool dstRangeFits = dstHeap != nullptr && dstHeap->GetCpuIndex(DestDescriptorRangeStart.ptr, dstBaseIndex) &&
                              NumDescriptors <= dstHeap->numDescriptors - dstBaseIndex;

    if (srcRangeFits && dstRangeFits)
    {
        for (UINT i = 0; i < NumDescriptors; ++i)
        {
            ResourceInfo buffer {};
            if (srcHeap != nullptr && srcHeap->GetByIndex(srcBaseIndex + i, buffer))
                dstHeap->SetByIndex(dstBaseIndex + i, buffer);
            else
                dstHeap->ClearByIndex(dstBaseIndex + i);
        }

        return;
    }

    // Old behavior for malformed/cross ranges.
    const auto size = This->GetDescriptorHandleIncrementSize(DescriptorHeapsType);

    for (UINT i = 0; i < NumDescriptors; ++i)
    {
        std::shared_ptr<HeapInfo> srcHeap;
        SIZE_T srcHandle = 0;

        // source
        if (SrcDescriptorRangeStart.ptr != 0)
        {
            srcHandle = SrcDescriptorRangeStart.ptr + i * size;
            srcHeap = GetHeapByCpuHandle(srcHandle);
        }

        auto destHandle = DestDescriptorRangeStart.ptr + i * size;
        auto dstHeap = GetHeapByCpuHandle(destHandle);

        // destination
        if (dstHeap == nullptr)
            continue;

        if (srcHeap == nullptr)
        {
            dstHeap->ClearByCpuHandle(destHandle);
            continue;
        }

        ResourceInfo buffer {};
        if (!srcHeap->GetByCpuHandle(srcHandle, buffer))
        {
            dstHeap->ClearByCpuHandle(destHandle);
            continue;
        }

        dstHeap->SetByCpuHandle(destHandle, buffer);
    }
}

#pragma endregion

#pragma region Commandlist state

void ResTrack_Dx12::RemoveBindingState(ID3D12GraphicsCommandList* commandList)
{
    if (commandList == nullptr)
        return;

    if (!_useShards)
    {
        std::lock_guard<std::mutex> lock(_bindingStateMutex);
        _bindingStates.erase(commandList);
        return;
    }

    auto& shard = _bindingShards[GetShardIndex(commandList)];
    std::lock_guard<BindingStateMutex> lock(shard.mutex);
    shard.map.erase(commandList);
}

void ResTrack_Dx12::ClearBindingStates()
{
    if (!_useShards)
    {
        std::lock_guard<std::mutex> lock(_bindingStateMutex);
        _bindingStates.clear();
        return;
    }

    for (auto& shard : _bindingShards)
    {
        std::lock_guard<BindingStateMutex> lock(shard.mutex);
        shard.map.clear();
    }
}

CommandListBindingState* ResTrack_Dx12::GetOrCreateBindingState(ID3D12GraphicsCommandList* commandList)
{
    if (!_bindingTrackingEnabled.load(std::memory_order_acquire) || commandList == nullptr ||
        State::Instance().isShuttingDown)
        return nullptr;

    CommandListBindingState* state = nullptr;
    bool created = false;

    if (!_useShards)
    {
        std::lock_guard<std::mutex> lock(_bindingStateMutex);
        auto [it, inserted] = _bindingStates.try_emplace(commandList);
        if (inserted)
            it->second = std::make_unique<CommandListBindingState>();
        state = it->second.get();
        created = inserted;
    }
    else
    {
        auto& shard = _bindingShards[GetShardIndex(commandList)];
        std::lock_guard<BindingStateMutex> lock(shard.mutex);
        auto [it, inserted] = shard.map.try_emplace(commandList);
        if (inserted)
            it->second = std::make_unique<CommandListBindingState>();
        state = it->second.get();
        created = inserted;
    }

    if (!created)
        return state;

    ID3DDestructionNotifier* notifier = nullptr;
    auto result = commandList->QueryInterface(IID_PPV_ARGS(&notifier));
    if (FAILED(result) || notifier == nullptr)
    {
        LOG_DEBUG("ID3DDestructionNotifier is not available for commandlist {:X}, result: {:X}", (size_t) commandList,
                  (UINT) result);
        RemoveBindingState(commandList);
        return nullptr;
    }

    UINT callbackId = 0;
    result = notifier->RegisterDestructionCallback(&ResTrack_Dx12::CommandListDestroyed, commandList, &callbackId);
    notifier->Release();

    if (FAILED(result))
    {
        LOG_DEBUG("Can't register commandlist remove callback for {:X}, result: {:X}", (size_t) commandList,
                  (UINT) result);
        RemoveBindingState(commandList);
        return nullptr;
    }

    return state;
}

CommandListBindingState* ResTrack_Dx12::FindBindingState(ID3D12GraphicsCommandList* commandList)
{
    if (commandList == nullptr)
        return nullptr;

    if (!_useShards)
    {
        std::lock_guard<std::mutex> lock(_bindingStateMutex);
        auto it = _bindingStates.find(commandList);
        return it != _bindingStates.end() ? it->second.get() : nullptr;
    }

    auto& shard = _bindingShards[GetShardIndex(commandList)];
    std::lock_guard<BindingStateMutex> lock(shard.mutex);
    auto it = shard.map.find(commandList);
    return it != shard.map.end() ? it->second.get() : nullptr;
}

template <typename RootParameterT>
static void BuildRootSignatureInfo(RootSignatureInfo& info, UINT numParameters, const RootParameterT* parameters)
{
    if (parameters == nullptr)
        return;

    const auto parameterCount =
        std::min<UINT>(numParameters, static_cast<UINT>(CommandListBindingState::MAX_ROOT_PARAMETERS));

    for (UINT parameterIndex = 0; parameterIndex < parameterCount; ++parameterIndex)
    {
        const auto& parameter = parameters[parameterIndex];
        if (parameter.ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
            parameter.DescriptorTable.NumDescriptorRanges == 0 ||
            parameter.DescriptorTable.pDescriptorRanges == nullptr)
            continue;

        auto& tableInfo = info.parameters[parameterIndex];
        tableInfo.firstRange = static_cast<UINT>(info.ranges.size());
        tableInfo.visibility = parameter.ShaderVisibility;

        UINT nextOffset = 0;
        bool nextOffsetValid = true;

        for (UINT rangeIndex = 0; rangeIndex < parameter.DescriptorTable.NumDescriptorRanges; ++rangeIndex)
        {
            const auto& range = parameter.DescriptorTable.pDescriptorRanges[rangeIndex];

            UINT offset = range.OffsetInDescriptorsFromTableStart;
            bool offsetValid = true;
            if (offset == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
            {
                offsetValid = nextOffsetValid;
                offset = nextOffset;
            }

            if (offsetValid && (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
                                range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV))
            {
                info.ranges.push_back({ offset, range.NumDescriptors, range.RangeType });
            }

            if (!offsetValid || range.NumDescriptors == UINT_MAX || offset > UINT_MAX - range.NumDescriptors)
            {
                nextOffsetValid = false;
            }
            else
            {
                nextOffset = offset + range.NumDescriptors;
                nextOffsetValid = true;
            }
        }

        tableInfo.rangeCount = static_cast<UINT>(info.ranges.size()) - tableInfo.firstRange;
    }
}

std::shared_ptr<RootSignatureInfo> ResTrack_Dx12::FindRootSignatureInfo(ID3D12RootSignature* rootSignature)
{
    if (rootSignature == nullptr)
        return nullptr;

    std::lock_guard<std::mutex> lock(_rootSignatureInfoMutex);
    auto it = _rootSignatureInfos.find(rootSignature);
    return it != _rootSignatureInfos.end() ? it->second : nullptr;
}

void __stdcall ResTrack_Dx12::RootSignatureDestroyed(void* data)
{
    if (data == nullptr || State::Instance().isShuttingDown)
        return;

    std::lock_guard<std::mutex> lock(_rootSignatureInfoMutex);
    _rootSignatureInfos.erase(static_cast<ID3D12RootSignature*>(data));
}

void ResTrack_Dx12::RegisterRootSignature(ID3D12RootSignature* rootSignature,
                                          const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc)
{
    if (rootSignature == nullptr || desc == nullptr)
        return;

    auto info = std::make_shared<RootSignatureInfo>();
    switch (desc->Version)
    {
    case D3D_ROOT_SIGNATURE_VERSION_1_0:
        BuildRootSignatureInfo(*info, desc->Desc_1_0.NumParameters, desc->Desc_1_0.pParameters);
        info->pixelShaderRootAccess =
            (desc->Desc_1_0.Flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS) == 0;
        break;

    case D3D_ROOT_SIGNATURE_VERSION_1_1:
        BuildRootSignatureInfo(*info, desc->Desc_1_1.NumParameters, desc->Desc_1_1.pParameters);
        info->pixelShaderRootAccess =
            (desc->Desc_1_1.Flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS) == 0;
        break;

    case D3D_ROOT_SIGNATURE_VERSION_1_2:
        BuildRootSignatureInfo(*info, desc->Desc_1_2.NumParameters, desc->Desc_1_2.pParameters);
        info->pixelShaderRootAccess =
            (desc->Desc_1_2.Flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS) == 0;
        break;

    default:
        return;
    }

    ID3DDestructionNotifier* notifier = nullptr;
    auto result = rootSignature->QueryInterface(IID_PPV_ARGS(&notifier));
    if (FAILED(result) || notifier == nullptr)
    {
        LOG_DEBUG("ID3DDestructionNotifier is not available for root signature {:X}, result: {:X}",
                  (size_t) rootSignature, (UINT) result);
        return;
    }

    UINT callbackId = 0;
    result = notifier->RegisterDestructionCallback(&ResTrack_Dx12::RootSignatureDestroyed, rootSignature, &callbackId);
    notifier->Release();

    if (FAILED(result))
    {
        LOG_DEBUG("Can't register root signature remove callback for {:X}, result: {:X}", (size_t) rootSignature,
                  (UINT) result);
        return;
    }

    std::lock_guard<std::mutex> lock(_rootSignatureInfoMutex);
    _rootSignatureInfos.insert_or_assign(rootSignature, std::move(info));
}

void ResTrack_Dx12::ResetBindingState(ID3D12GraphicsCommandList* commandList)
{
    auto* state = FindBindingState(commandList);
    if (state == nullptr)
        return;

    state->graphicsTableMask = 0;
    state->computeTableMask = 0;
    state->renderTargetCount = 0;
    state->renderTargetsContiguous = false;
    state->graphicsRootSignature = nullptr;
    state->computeRootSignature = nullptr;
    state->graphicsRootSignatureInfo.reset();
    state->computeRootSignatureInfo.reset();
    state->cbvSrvUavHeap = nullptr;
    state->cbvSrvUavHeapInfo.reset();
}

void __stdcall ResTrack_Dx12::CommandListDestroyed(void* data)
{
    if (data == nullptr || State::Instance().isShuttingDown)
        return;
    RemoveBindingState(static_cast<ID3D12GraphicsCommandList*>(data));
}

void ResTrack_Dx12::OnSetDescriptorHeaps(ID3D12GraphicsCommandList* commandList, UINT numDescriptorHeaps,
                                         ID3D12DescriptorHeap* const* descriptorHeaps)
{
    auto* state = GetOrCreateBindingState(commandList);
    if (state == nullptr)
        return;

    ID3D12DescriptorHeap* cbvSrvUavHeap = nullptr;
    if (descriptorHeaps != nullptr)
    {
        for (UINT i = 0; i < numDescriptorHeaps; ++i)
        {
            auto* heap = descriptorHeaps[i];
            if (heap != nullptr && heap->GetDesc().Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
            {
                cbvSrvUavHeap = heap;
                break;
            }
        }
    }

    if (state->cbvSrvUavHeap == cbvSrvUavHeap)
    {
        auto* trackedHeap = state->cbvSrvUavHeapInfo.get();
        if (cbvSrvUavHeap == nullptr)
        {
            state->cbvSrvUavHeapInfo.reset();
            return;
        }

        if (trackedHeap != nullptr && trackedHeap->active.load(std::memory_order_acquire) &&
            trackedHeap->heap == cbvSrvUavHeap)
            return;

        const auto gpuStart = cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart().ptr;
        auto heapInfo = gpuStart != 0 ? GetHeapByGpuHandleGR(gpuStart) : nullptr;
        if (heapInfo != nullptr && heapInfo->heap != cbvSrvUavHeap)
            heapInfo.reset();

        state->cbvSrvUavHeapInfo = std::move(heapInfo);
        return;
    }

    state->graphicsTableMask = 0;
    state->computeTableMask = 0;
    state->cbvSrvUavHeap = cbvSrvUavHeap;
    state->cbvSrvUavHeapInfo.reset();

    if (cbvSrvUavHeap != nullptr)
    {
        const auto gpuStart = cbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart().ptr;
        auto heapInfo = gpuStart != 0 ? GetHeapByGpuHandleGR(gpuStart) : nullptr;
        if (heapInfo != nullptr && heapInfo->heap == cbvSrvUavHeap)
            state->cbvSrvUavHeapInfo = std::move(heapInfo);
    }
}

void ResTrack_Dx12::OnSetGraphicsRootSignature(ID3D12GraphicsCommandList* commandList,
                                               ID3D12RootSignature* rootSignature)
{
    auto* state = GetOrCreateBindingState(commandList);
    if (state == nullptr)
        return;

    if (state->graphicsRootSignature != rootSignature)
    {
        state->graphicsTableMask = 0;
        state->graphicsRootSignature = rootSignature;
        state->graphicsRootSignatureInfo = FindRootSignatureInfo(rootSignature);
    }
}

void ResTrack_Dx12::OnSetComputeRootSignature(ID3D12GraphicsCommandList* commandList,
                                              ID3D12RootSignature* rootSignature)
{
    auto* state = GetOrCreateBindingState(commandList);
    if (state == nullptr)
        return;

    if (state->computeRootSignature != rootSignature)
    {
        state->computeTableMask = 0;
        state->computeRootSignature = rootSignature;
        state->computeRootSignatureInfo = FindRootSignatureInfo(rootSignature);
    }
}

bool ResTrack_Dx12::ResolveGraphicsBinding(const HeapInfo* boundHeap, SIZE_T gpuHandle, ResourceInfo& outInfo)
{
    if (gpuHandle == 0)
        return false;

    std::shared_ptr<HeapInfo> fallbackHeap;
    auto* heap = boundHeap;
    if (heap == nullptr || !heap->active.load(std::memory_order_acquire) || gpuHandle < heap->gpuStart ||
        gpuHandle >= heap->gpuEnd)
    {
        fallbackHeap = GetHeapByGpuHandleGR(gpuHandle);
        heap = fallbackHeap.get();
    }

    if (heap == nullptr || !heap->GetByGpuHandle(gpuHandle, outInfo) || outInfo.buffer == nullptr ||
        !IsDescriptorEnabled(outInfo.type))
        return false;

    outInfo.state =
        outInfo.type == UAV ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    outInfo.captureInfo = CaptureInfo::SetGR;
    return true;
}

bool ResTrack_Dx12::ResolveComputeBinding(const HeapInfo* boundHeap, SIZE_T gpuHandle, ResourceInfo& outInfo)
{
    if (gpuHandle == 0)
        return false;

    std::shared_ptr<HeapInfo> fallbackHeap;
    auto* heap = boundHeap;
    if (heap == nullptr || !heap->active.load(std::memory_order_acquire) || gpuHandle < heap->gpuStart ||
        gpuHandle >= heap->gpuEnd)
    {
        fallbackHeap = GetHeapByGpuHandleCR(gpuHandle);
        heap = fallbackHeap.get();
    }

    if (heap == nullptr || !heap->GetByGpuHandle(gpuHandle, outInfo) || outInfo.buffer == nullptr ||
        !IsDescriptorEnabled(outInfo.type))
        return false;

    outInfo.state =
        outInfo.type == UAV ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    outInfo.captureInfo = CaptureInfo::SetCR;
    return true;
}

bool ResTrack_Dx12::ProcessDescriptorTableBinding(ID3D12GraphicsCommandList* commandList,
                                                  const RootSignatureInfo* rootInfo, UINT rootParameterIndex,
                                                  const HeapInfo* boundHeap, SIZE_T baseHandle, UINT captureInfo,
                                                  bool graphics)
{
    if (baseHandle == 0)
        return false;

    // Missing metadata keeps the pre-F14 base-descriptor behavior.
    if (rootInfo == nullptr)
    {
        ResourceInfo candidate {};
        const bool resolved = graphics ? ResolveGraphicsBinding(boundHeap, baseHandle, candidate)
                                       : ResolveComputeBinding(boundHeap, baseHandle, candidate);
        if (!resolved)
            return false;

        candidate.captureInfo |= captureInfo;
        return Hudfix_Dx12::CheckForHudless(commandList, &candidate, candidate.state);
    }

    if (rootParameterIndex >= CommandListBindingState::MAX_ROOT_PARAMETERS)
        return false;

    const auto& tableInfo = rootInfo->parameters[rootParameterIndex];
    if (tableInfo.rangeCount == 0)
        return false;

    if (graphics && (!rootInfo->pixelShaderRootAccess || (tableInfo.visibility != D3D12_SHADER_VISIBILITY_ALL &&
                                                          tableInfo.visibility != D3D12_SHADER_VISIBILITY_PIXEL)))
        return false;

    std::shared_ptr<HeapInfo> fallbackHeap;
    auto* heap = boundHeap;
    if (heap == nullptr || !heap->active.load(std::memory_order_acquire) || baseHandle < heap->gpuStart ||
        baseHandle >= heap->gpuEnd)
    {
        fallbackHeap = graphics ? GetHeapByGpuHandleGR(baseHandle) : GetHeapByGpuHandleCR(baseHandle);
        heap = fallbackHeap.get();
    }

    UINT baseIndex = 0;
    if (heap == nullptr || !heap->GetGpuIndex(baseHandle, baseIndex))
        return false;

    auto* config = Config::Instance();
    const bool srvEnabled = !config->FGHudfixDisableSRV.value_or_default();
    const bool uavEnabled = !config->FGHudfixDisableUAV.value_or_default();

    // Bound the complete descriptor table, not each individual range. A root table can contain many
    // ranges, so a per-range cap can still create large Draw/Dispatch spikes.
    static constexpr UINT MAX_DESCRIPTORS_PER_TABLE = 16;
    UINT remainingDescriptorBudget = MAX_DESCRIPTORS_PER_TABLE;

    // Descriptor tables often alias the same resource in multiple slots/ranges. Avoid sending the
    // same resource/type pair through the HUDless policy more than once during this table scan.
    std::array<ID3D12Resource*, MAX_DESCRIPTORS_PER_TABLE> seenResources {};
    std::array<ResourceType, MAX_DESCRIPTORS_PER_TABLE> seenTypes {};
    UINT seenCount = 0;

    const UINT rangeEnd = tableInfo.firstRange + tableInfo.rangeCount;
    for (UINT rangeIndex = tableInfo.firstRange; rangeIndex < rangeEnd && remainingDescriptorBudget > 0; ++rangeIndex)
    {
        const auto& range = rootInfo->ranges[rangeIndex];
        if ((range.type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && !srvEnabled) ||
            (range.type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && !uavEnabled))
            continue;

        const uint64_t firstIndex64 = static_cast<uint64_t>(baseIndex) + range.offset;
        if (firstIndex64 >= heap->numDescriptors)
            continue;

        const auto firstIndex = static_cast<UINT>(firstIndex64);
        const UINT available = heap->numDescriptors - firstIndex;

        // Unbounded bindless ranges stay at one descriptor. Bounded ranges consume the shared table
        // budget so many small ranges cannot multiply the hot-path cost.
        const UINT descriptorCount =
            range.count == UINT_MAX ? 1
                                    : std::min<UINT>(std::min<UINT>(range.count, available), remainingDescriptorBudget);

        for (UINT descriptorOffset = 0; descriptorOffset < descriptorCount; ++descriptorOffset)
        {
            ResourceInfo candidate {};
            if (!heap->GetByIndex(firstIndex + descriptorOffset, candidate) || candidate.buffer == nullptr)
                continue;

            if ((range.type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && candidate.type != SRV) ||
                (range.type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && candidate.type != UAV))
                continue;

            bool duplicate = false;
            for (UINT seenIndex = 0; seenIndex < seenCount; ++seenIndex)
            {
                if (seenResources[seenIndex] == candidate.buffer && seenTypes[seenIndex] == candidate.type)
                {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate)
                continue;

            seenResources[seenCount] = candidate.buffer;
            seenTypes[seenCount] = candidate.type;
            ++seenCount;

            candidate.state = candidate.type == UAV ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                                                    : (graphics ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                                                : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            candidate.captureInfo = graphics ? CaptureInfo::SetGR : CaptureInfo::SetCR;
            candidate.captureInfo |= captureInfo;

            if (Hudfix_Dx12::CheckForHudless(commandList, &candidate, candidate.state))
                return true;
        }

        remainingDescriptorBudget -= descriptorCount;
    }

    return false;
}

bool ResTrack_Dx12::ResolveRenderTargetBinding(SIZE_T cpuHandle, ResourceInfo& outInfo)
{
    if (cpuHandle == 0)
        return false;

    auto heap = GetHeapByCpuHandleRTV(cpuHandle);
    if (heap == nullptr || !heap->GetByCpuHandle(cpuHandle, outInfo) || outInfo.buffer == nullptr ||
        !IsDescriptorEnabled(outInfo.type))
        return false;

    outInfo.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    outInfo.captureInfo = CaptureInfo::OMSetRTV;
    return true;
}

bool ResTrack_Dx12::ProcessGraphicsBindings(ID3D12GraphicsCommandList* commandList, UINT captureInfo)
{
    if (!_bindingTrackingEnabled.load(std::memory_order_acquire) ||
        Config::Instance()->FGImmediateCapture.value_or_default())
        return false;

    auto* state = FindBindingState(commandList);
    if (state == nullptr)
        return false;

    if (Hudfix_Dx12::SkipHudlessChecks())
        return true;

    if (!Config::Instance()->FGHudfixDisableSGR.value_or_default())
    {
        auto mask = state->graphicsTableMask;
        for (UINT index = 0; mask != 0 && index < CommandListBindingState::MAX_ROOT_PARAMETERS; ++index, mask >>= 1)
        {
            if ((mask & 1) == 0)
                continue;

            if (ProcessDescriptorTableBinding(commandList, state->graphicsRootSignatureInfo.get(), index,
                                              state->cbvSrvUavHeapInfo.get(), state->graphicsTables[index], captureInfo,
                                              true))
                return true;
        }
    }

    if (!Config::Instance()->FGHudfixDisableOM.value_or_default() && state->renderTargetCount > 0)
    {
        if (state->renderTargetsContiguous)
        {
            const auto baseHandle = state->renderTargets[0];
            auto heap = GetHeapByCpuHandleRTV(baseHandle);
            if (heap != nullptr)
            {
                for (UINT i = 0; i < state->renderTargetCount; ++i)
                {
                    ResourceInfo candidate {};
                    if (!ResolveRenderTargetBinding(baseHandle + (static_cast<SIZE_T>(i) * heap->increment), candidate))
                        continue;
                    candidate.captureInfo |= captureInfo;
                    if (Hudfix_Dx12::CheckForHudless(commandList, &candidate, candidate.state))
                        return true;
                }
            }
        }
        else
        {
            for (UINT i = 0; i < state->renderTargetCount; ++i)
            {
                ResourceInfo candidate {};
                if (!ResolveRenderTargetBinding(state->renderTargets[i], candidate))
                    continue;
                candidate.captureInfo |= captureInfo;
                if (Hudfix_Dx12::CheckForHudless(commandList, &candidate, candidate.state))
                    return true;
            }
        }
    }

    return true;
}

bool ResTrack_Dx12::ProcessComputeBindings(ID3D12GraphicsCommandList* commandList, UINT captureInfo)
{
    if (!_bindingTrackingEnabled.load(std::memory_order_acquire) ||
        Config::Instance()->FGImmediateCapture.value_or_default())
        return false;

    auto* state = FindBindingState(commandList);
    if (state == nullptr)
        return false;

    if (Hudfix_Dx12::SkipHudlessChecks())
        return true;

    if (!Config::Instance()->FGHudfixDisableSCR.value_or_default())
    {
        auto mask = state->computeTableMask;
        for (UINT index = 0; mask != 0 && index < CommandListBindingState::MAX_ROOT_PARAMETERS; ++index, mask >>= 1)
        {
            if ((mask & 1) == 0)
                continue;

            if (ProcessDescriptorTableBinding(commandList, state->computeRootSignatureInfo.get(), index,
                                              state->cbvSrvUavHeapInfo.get(), state->computeTables[index], captureInfo,
                                              false))
                return true;
        }
    }

    return true;
}

#pragma endregion

#pragma region Shader input hooks

void ResTrack_Dx12::hkSetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* This, UINT RootParameterIndex,
                                                     D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    bool persistentBinding = false;
    if (This != MenuOverlayDx::MenuCommandList() && !Hudfix_Dx12::SkipHudlessChecks())
    {
        if (auto* state = GetOrCreateBindingState(This);
            state != nullptr && RootParameterIndex < CommandListBindingState::MAX_ROOT_PARAMETERS)
        {
            persistentBinding = true;
            state->graphicsTables[RootParameterIndex] = BaseDescriptor.ptr;
            const auto bit = UINT64_C(1) << RootParameterIndex;
            if (BaseDescriptor.ptr != 0)
                state->graphicsTableMask |= bit;
            else
                state->graphicsTableMask &= ~bit;

            if (BaseDescriptor.ptr != 0 && (state->cbvSrvUavHeapInfo == nullptr ||
                                            !state->cbvSrvUavHeapInfo->active.load(std::memory_order_acquire)))
            {
                if (auto heap = GetHeapByGpuHandleGR(BaseDescriptor.ptr))
                {
                    if (state->cbvSrvUavHeap != nullptr && state->cbvSrvUavHeap != heap->heap)
                        heap.reset();

                    if (heap != nullptr)
                    {
                        state->cbvSrvUavHeap = heap->heap;
                        state->cbvSrvUavHeapInfo = std::move(heap);
                    }
                }
            }
        }
    }

    const bool immediateCapture = Config::Instance()->FGImmediateCapture.value_or_default();
    if (persistentBinding && !immediateCapture)
    {
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    // Consistent early exit - always call original function
    auto shouldTrack = !Config::Instance()->FGHudfixDisableSGR.value_or_default() && BaseDescriptor.ptr != 0 &&
                       IsHudFixActive() && !Hudfix_Dx12::SkipHudlessChecks() &&
                       This != MenuOverlayDx::MenuCommandList();

    if (!shouldTrack)
    {
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    auto heap = GetHeapByGpuHandleGR(BaseDescriptor.ptr);
    if (heap == nullptr)
    {
        LOG_DEBUG_ONLY("No heap for handle: {:X}", BaseDescriptor.ptr);
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    ResourceInfo capturedBuffer {};
    if (!heap->GetByGpuHandle(BaseDescriptor.ptr, capturedBuffer) || capturedBuffer.buffer == nullptr)
    {
        LOG_DEBUG_ONLY("No resource at RootParameterIndex: {}, CommandList: {:X}, gpuHandle: {:X}", RootParameterIndex,
                       (SIZE_T) This, BaseDescriptor.ptr);
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    if (!IsDescriptorEnabled(capturedBuffer.type))
    {
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    LOG_DEBUG_ONLY("CommandList: {:X}, Resource: {:X}", (size_t) This, (size_t) capturedBuffer.buffer);

    // Only proceed with tracking if we have a valid buffer
    capturedBuffer.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    capturedBuffer.captureInfo = CaptureInfo::SetGR;

    // Track the resource
    bool capturedImmediately = false;
    if (immediateCapture)
    {
        capturedImmediately = Hudfix_Dx12::CheckForHudless(This, &capturedBuffer, capturedBuffer.state);
    }

    if (!capturedImmediately && (!persistentBinding || immediateCapture))
    {
        auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

        if (!_useShards)
        {
            std::lock_guard<std::mutex> lock(_hudlessTrackMutex);

            if (!fgPossibleHudless[fIndex].contains(This))
            {
                ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> newMap;
                newMap.reserve(32);
                fgPossibleHudless[fIndex].insert_or_assign(This, std::move(newMap));
            }

            LOG_TRACK("Tracking Resource: {:X}, Desc: {:X}", (size_t) capturedBuffer.buffer, BaseDescriptor.ptr);
            fgPossibleHudless[fIndex][This].insert_or_assign(capturedBuffer.buffer, capturedBuffer);
        }
        else
        {
            size_t shardIdx = GetShardIndex(This);
            auto& shard = _hudlessShards[fIndex][shardIdx];

            LOCK_GUARD(shard.mutex);

            if (!shard.map.contains(This))
            {
                ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> newMap;
                newMap.reserve(32);
                shard.map.insert_or_assign(This, std::move(newMap));
            }

            LOG_TRACK("CmdList: {:X}, Tracking Resource: {:X}, Desc: {:X}, Format: {}", (size_t) This,
                      (size_t) capturedBuffer.buffer, BaseDescriptor.ptr, (UINT) capturedBuffer.format);

            shard.map[This].insert_or_assign(capturedBuffer.buffer, capturedBuffer);
        }
    }

    o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
}

#pragma endregion

#pragma region Shader output hooks

void ResTrack_Dx12::hkOMSetRenderTargets(ID3D12GraphicsCommandList* This, UINT NumRenderTargetDescriptors,
                                         D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
                                         BOOL RTsSingleHandleToDescriptorRange,
                                         D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor)
{
    bool persistentBinding = false;
    if (This != MenuOverlayDx::MenuCommandList() && !Hudfix_Dx12::SkipHudlessChecks())
    {
        if (auto* state = GetOrCreateBindingState(This); state != nullptr)
        {
            persistentBinding = true;
            state->renderTargetCount = 0;
            state->renderTargetsContiguous = false;

            if (NumRenderTargetDescriptors > 0 && pRenderTargetDescriptors != nullptr)
            {
                state->renderTargetCount =
                    std::min<UINT>(NumRenderTargetDescriptors, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
                state->renderTargetsContiguous = RTsSingleHandleToDescriptorRange != FALSE;

                if (state->renderTargetsContiguous)
                    state->renderTargets[0] = pRenderTargetDescriptors[0].ptr;
                else
                    for (UINT i = 0; i < state->renderTargetCount; ++i)
                        state->renderTargets[i] = pRenderTargetDescriptors[i].ptr;
            }
        }
    }

    const bool immediateCapture = Config::Instance()->FGImmediateCapture.value_or_default();
    if (persistentBinding && !immediateCapture)
    {
        o_OMSetRenderTargets(This, NumRenderTargetDescriptors, pRenderTargetDescriptors,
                             RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
        return;
    }

    // Consistent early exit validation
    auto shouldTrack = !Config::Instance()->FGHudfixDisableOM.value_or_default() && NumRenderTargetDescriptors > 0 &&
                       pRenderTargetDescriptors != nullptr && IsHudFixActive() && !Hudfix_Dx12::SkipHudlessChecks() &&
                       This != MenuOverlayDx::MenuCommandList();

    if (!shouldTrack)
    {
        o_OMSetRenderTargets(This, NumRenderTargetDescriptors, pRenderTargetDescriptors,
                             RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
        return;
    }

    LOG_DEBUG_ONLY("NumRenderTargetDescriptors: {}", NumRenderTargetDescriptors);

    auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

    // Process render targets
    for (size_t i = 0; i < NumRenderTargetDescriptors; i++)
    {
        std::shared_ptr<HeapInfo> heap;
        D3D12_CPU_DESCRIPTOR_HANDLE handle {};

        // Get the appropriate handle
        if (RTsSingleHandleToDescriptorRange)
        {
            heap = GetHeapByCpuHandleRTV(pRenderTargetDescriptors[0].ptr);
            if (heap == nullptr)
            {
                LOG_DEBUG_ONLY("No heap at index: {}", i);
                continue;
            }

            handle.ptr = pRenderTargetDescriptors[0].ptr + (i * heap->increment);
        }
        else
        {
            handle = pRenderTargetDescriptors[i];
            heap = GetHeapByCpuHandleRTV(handle.ptr);
            if (heap == nullptr)
            {
                LOG_DEBUG_ONLY("No heap at index: {}", i);
                continue;
            }
        }

        ResourceInfo capturedBuffer {};
        if (!heap->GetByCpuHandle(handle.ptr, capturedBuffer) || capturedBuffer.buffer == nullptr)
        {
            LOG_DEBUG_ONLY("No resource at index: {}, cpu: {:X}", i, handle.ptr);
            continue;
        }

        if (!IsDescriptorEnabled(capturedBuffer.type))
            continue;

        // Valid resource found, update state
        capturedBuffer.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        capturedBuffer.captureInfo = CaptureInfo::OMSetRTV;

        // Check for immediate capture
        bool capturedImmediately = false;
        if (immediateCapture)
        {
            capturedImmediately = Hudfix_Dx12::CheckForHudless(This, &capturedBuffer, capturedBuffer.state);
            if (capturedImmediately)
                break; // Early exit if captured
        }

        // Track for later processing
        if (!capturedImmediately && (!persistentBinding || immediateCapture))
        {
            if (!_useShards)
            {
                std::lock_guard<std::mutex> lock(_hudlessTrackMutex);

                if (!fgPossibleHudless[fIndex].contains(This))
                {
                    ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> newMap;
                    newMap.reserve(32);
                    fgPossibleHudless[fIndex].insert_or_assign(This, std::move(newMap));
                }

                LOG_TRACK("Tracking Resource: {:X}, Desc: {:X}", (size_t) capturedBuffer.buffer, handle.ptr);
                fgPossibleHudless[fIndex][This].insert_or_assign(capturedBuffer.buffer, capturedBuffer);
            }
            else
            {
                size_t shardIdx = GetShardIndex(This);
                auto& shard = _hudlessShards[fIndex][shardIdx];

                LOCK_GUARD(shard.mutex);

                if (!shard.map.contains(This))
                {
                    ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> newMap;
                    newMap.reserve(32);
                    shard.map.insert_or_assign(This, std::move(newMap));
                }

                LOG_TRACK("CmdList: {:X}, Tracking Resource: {:X}, Desc: {:X}, Format: {}", (size_t) This,
                          (size_t) capturedBuffer.buffer, handle.ptr, (UINT) capturedBuffer.format);

                shard.map[This].insert_or_assign(capturedBuffer.buffer, capturedBuffer);
            }
        }
    }

    o_OMSetRenderTargets(This, NumRenderTargetDescriptors, pRenderTargetDescriptors, RTsSingleHandleToDescriptorRange,
                         pDepthStencilDescriptor);
}

#pragma endregion

#pragma region Compute paramter hooks

void ResTrack_Dx12::hkSetComputeRootDescriptorTable(ID3D12GraphicsCommandList* This, UINT RootParameterIndex,
                                                    D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    bool persistentBinding = false;
    if (This != MenuOverlayDx::MenuCommandList() && !Hudfix_Dx12::SkipHudlessChecks())
    {
        if (auto* state = GetOrCreateBindingState(This);
            state != nullptr && RootParameterIndex < CommandListBindingState::MAX_ROOT_PARAMETERS)
        {
            persistentBinding = true;
            state->computeTables[RootParameterIndex] = BaseDescriptor.ptr;
            const auto bit = UINT64_C(1) << RootParameterIndex;
            if (BaseDescriptor.ptr != 0)
                state->computeTableMask |= bit;
            else
                state->computeTableMask &= ~bit;

            if (BaseDescriptor.ptr != 0 && (state->cbvSrvUavHeapInfo == nullptr ||
                                            !state->cbvSrvUavHeapInfo->active.load(std::memory_order_acquire)))
            {
                if (auto heap = GetHeapByGpuHandleCR(BaseDescriptor.ptr))
                {
                    if (state->cbvSrvUavHeap != nullptr && state->cbvSrvUavHeap != heap->heap)
                        heap.reset();

                    if (heap != nullptr)
                    {
                        state->cbvSrvUavHeap = heap->heap;
                        state->cbvSrvUavHeapInfo = std::move(heap);
                    }
                }
            }
        }
    }

    const bool immediateCapture = Config::Instance()->FGImmediateCapture.value_or_default();
    if (persistentBinding && !immediateCapture)
    {
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    // Consistent early exit - always call original function
    auto shouldTrack = !Config::Instance()->FGHudfixDisableSCR.value_or_default() && BaseDescriptor.ptr != 0 &&
                       IsHudFixActive() && !Hudfix_Dx12::SkipHudlessChecks() &&
                       This != MenuOverlayDx::MenuCommandList();

    if (!shouldTrack)
    {
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    auto heap = GetHeapByGpuHandleCR(BaseDescriptor.ptr);
    if (heap == nullptr)
    {
        LOG_DEBUG_ONLY("No heap for handle: {:X}", BaseDescriptor.ptr);
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    ResourceInfo capturedBuffer {};
    if (!heap->GetByGpuHandle(BaseDescriptor.ptr, capturedBuffer) || capturedBuffer.buffer == nullptr)
    {
        LOG_DEBUG_ONLY("No resource at RootParameterIndex: {}, CommandList: {:X}, gpuHandle: {:X}", RootParameterIndex,
                       (SIZE_T) This, BaseDescriptor.ptr);
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    if (!IsDescriptorEnabled(capturedBuffer.type))
    {
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    LOG_DEBUG_ONLY("CommandList: {:X}, Resource: {:X}", (size_t) This, (size_t) capturedBuffer.buffer);

    // Only proceed with tracking if we have a valid buffer
    if (capturedBuffer.type == UAV)
        capturedBuffer.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    else
        capturedBuffer.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    capturedBuffer.captureInfo = CaptureInfo::SetCR;

    // Track the resource
    bool capturedImmediately = false;
    if (immediateCapture)
    {
        capturedImmediately = Hudfix_Dx12::CheckForHudless(This, &capturedBuffer, capturedBuffer.state);
    }

    if (!capturedImmediately && (!persistentBinding || immediateCapture))
    {
        auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

        if (!_useShards)
        {
            std::lock_guard<std::mutex> lock(_hudlessTrackMutex);

            if (!fgPossibleHudless[fIndex].contains(This))
            {
                ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> newMap;
                newMap.reserve(32);
                fgPossibleHudless[fIndex].insert_or_assign(This, std::move(newMap));
            }

            LOG_TRACK("Tracking Resource: {:X}, Desc: {:X}", (size_t) capturedBuffer.buffer, BaseDescriptor.ptr);
            fgPossibleHudless[fIndex][This].insert_or_assign(capturedBuffer.buffer, capturedBuffer);
        }
        else
        {
            size_t shardIdx = GetShardIndex(This);
            auto& shard = _hudlessShards[fIndex][shardIdx];

            LOCK_GUARD(shard.mutex);

            if (!shard.map.contains(This))
            {
                ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> newMap;
                newMap.reserve(32);
                shard.map.insert_or_assign(This, std::move(newMap));
            }

            LOG_TRACK("CmdList: {:X}, Tracking Resource: {:X}, Desc: {:X}, Format: {}", (size_t) This,
                      (size_t) capturedBuffer.buffer, BaseDescriptor.ptr, (UINT) capturedBuffer.format);

            shard.map[This].insert_or_assign(capturedBuffer.buffer, capturedBuffer);
        }
    }

    o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
}

#pragma endregion

HRESULT ResTrack_Dx12::hkReset(ID3D12GraphicsCommandList* This, ID3D12CommandAllocator* pAllocator,
                               ID3D12PipelineState* pInitialState)
{
    const auto result = o_Reset(This, pAllocator, pInitialState);
    if (SUCCEEDED(result))
        ResetBindingState(This);
    return result;
}

void ResTrack_Dx12::hkClearState(ID3D12GraphicsCommandList* This, ID3D12PipelineState* pPipelineState)
{
    o_ClearState(This, pPipelineState);
    ResetBindingState(This);
}

#pragma region Shader finalizer hooks

// Capture if render target matches, wait for DrawIndexed
void ResTrack_Dx12::hkDrawInstanced(ID3D12GraphicsCommandList* This, UINT VertexCountPerInstance, UINT InstanceCount,
                                    UINT StartVertexLocation, UINT StartInstanceLocation)
{
    o_DrawInstanced(This, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);

    if (!IsHudFixActive())
    {
        LOG_TRACK("Skipping {:X}", (size_t) This);
        return;
    }

    LOG_TRACK("CmdList: {:X}", (size_t) This);

    if (!Config::Instance()->FGHudfixDisableDI.value_or_default() &&
        ProcessGraphicsBindings(This, CaptureInfo::DrawInstanced))
        return;

    auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

    if (!_useShards)
    {
        if (This == MenuOverlayDx::MenuCommandList())
        {
            std::lock_guard<std::mutex> lock(_hudlessTrackMutex);
            fgPossibleHudless[fIndex].erase(This);
            return;
        }

        ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> val0;
        {
            std::lock_guard<std::mutex> lock(_hudlessTrackMutex);

            if (fgPossibleHudless[fIndex].size() == 0 || !fgPossibleHudless[fIndex].contains(This))
                return;

            val0 = std::move(fgPossibleHudless[fIndex][This]);
            fgPossibleHudless[fIndex].erase(This);
        }

        do
        {
            // if this command list does not have entries skip
            if (val0.size() == 0)
                break;

            if (Config::Instance()->FGHudfixDisableDI.value_or_default())
                break;

            for (auto& [key, val] : val0)
            {
                val.captureInfo |= CaptureInfo::DrawInstanced;

                if (Hudfix_Dx12::CheckForHudless(This, &val, val.state))
                    break;
            }

        } while (false);
    }
    else
    {
        size_t shardIdx = GetShardIndex(This);
        auto& shard = _hudlessShards[fIndex][shardIdx];

        if (This == MenuOverlayDx::MenuCommandList())
        {
            LOCK_GUARD(shard.mutex);

            shard.map.erase(This);
            return;
        }

        ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> val0;
        {

            LOCK_GUARD(shard.mutex);

            // if can't find output skip
            if (shard.map.size() == 0)
            {
                LOG_DEBUG_ONLY("Early exit");
                return;
            }

            if (!shard.map.contains(This))
                return;

            val0 = std::move(shard.map[This]);
            shard.map.erase(This);
        }

        do
        {
            // if this command list does not have entries skip
            if (val0.size() == 0)
                break;

            if (Config::Instance()->FGHudfixDisableDI.value_or_default())
                break;

            for (auto& [key, val] : val0)
            {
                val.captureInfo |= CaptureInfo::DrawInstanced;

                if (Hudfix_Dx12::CheckForHudless(This, &val, val.state))
                    break;
            }

        } while (false);
    }
}

void ResTrack_Dx12::hkDrawIndexedInstanced(ID3D12GraphicsCommandList* This, UINT IndexCountPerInstance,
                                           UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation,
                                           UINT StartInstanceLocation)
{
    o_DrawIndexedInstanced(This, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation,
                           StartInstanceLocation);

    if (!IsHudFixActive())
    {
        LOG_TRACK("Skipping CmdList: {:X}", (size_t) This);
        return;
    }

    LOG_TRACK("CmdList: {:X}", (size_t) This);

    if (!Config::Instance()->FGHudfixDisableDII.value_or_default() &&
        ProcessGraphicsBindings(This, CaptureInfo::DrawIndexedInstanced))
        return;

    auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

    if (!_useShards)
    {
        if (This == MenuOverlayDx::MenuCommandList())
        {
            std::lock_guard<std::mutex> lock(_hudlessTrackMutex);
            fgPossibleHudless[fIndex].erase(This);
            return;
        }

        ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> val0;
        {
            std::lock_guard<std::mutex> lock(_hudlessTrackMutex);

            if (fgPossibleHudless[fIndex].size() == 0 || !fgPossibleHudless[fIndex].contains(This))
                return;

            val0 = std::move(fgPossibleHudless[fIndex][This]);
            fgPossibleHudless[fIndex].erase(This);
        }

        do
        {
            // if this command list does not have entries skip
            if (val0.size() == 0)
                break;

            if (Config::Instance()->FGHudfixDisableDII.value_or_default())
                break;

            for (auto& [key, val] : val0)
            {
                val.captureInfo |= CaptureInfo::DrawIndexedInstanced;

                if (Hudfix_Dx12::CheckForHudless(This, &val, val.state))
                    break;
            }

        } while (false);
    }
    else
    {
        size_t shardIdx = GetShardIndex(This);
        auto& shard = _hudlessShards[fIndex][shardIdx];

        if (This == MenuOverlayDx::MenuCommandList())
        {
            LOCK_GUARD(shard.mutex);

            shard.map.erase(This);
            return;
        }

        ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> val0;
        {
            LOCK_GUARD(shard.mutex);

            // if can't find output skip
            if (shard.map.size() == 0)
            {
                LOG_DEBUG_ONLY("Early exit");
                return;
            }

            if (!shard.map.contains(This))
                return;

            val0 = std::move(shard.map[This]);
            shard.map.erase(This);
        }

        do
        {
            // if this command list does not have entries skip
            if (val0.size() == 0)
                break;

            if (Config::Instance()->FGHudfixDisableDII.value_or_default())
                break;

            for (auto& [key, val] : val0)
            {
                val.captureInfo |= CaptureInfo::DrawIndexedInstanced;

                if (Hudfix_Dx12::CheckForHudless(This, &val, val.state))
                    break;
            }

        } while (false);
    }
}

void ResTrack_Dx12::hkDispatch(ID3D12GraphicsCommandList* This, UINT ThreadGroupCountX, UINT ThreadGroupCountY,
                               UINT ThreadGroupCountZ)
{
    o_Dispatch(This, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);

    if (!IsHudFixActive())
    {
        LOG_TRACK("Skipping {:X}", (size_t) This);
        return;
    }

    LOG_TRACK("CmdList: {:X}", (size_t) This);

    if (!Config::Instance()->FGHudfixDisableDispatch.value_or_default() &&
        ProcessComputeBindings(This, CaptureInfo::Dispatch))
        return;

    auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

    if (!_useShards)
    {
        if (This == MenuOverlayDx::MenuCommandList())
        {
            std::lock_guard<std::mutex> lock(_hudlessTrackMutex);
            fgPossibleHudless[fIndex].erase(This);
            return;
        }

        ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> val0;
        {
            std::lock_guard<std::mutex> lock(_hudlessTrackMutex);

            if (fgPossibleHudless[fIndex].size() == 0 || !fgPossibleHudless[fIndex].contains(This))
                return;

            val0 = std::move(fgPossibleHudless[fIndex][This]);
            fgPossibleHudless[fIndex].erase(This);
        }

        do
        {
            // if this command list does not have entries skip
            if (val0.size() == 0)
                break;

            if (Config::Instance()->FGHudfixDisableDispatch.value_or_default())
                break;

            for (auto& [key, val] : val0)
            {
                val.captureInfo |= CaptureInfo::Dispatch;

                if (Hudfix_Dx12::CheckForHudless(This, &val, val.state))
                    break;
            }
        } while (false);
    }
    else
    {
        size_t shardIdx = GetShardIndex(This);
        auto& shard = _hudlessShards[fIndex][shardIdx];

        if (This == MenuOverlayDx::MenuCommandList())
        {
            LOCK_GUARD(shard.mutex);

            shard.map.erase(This);
            return;
        }

        ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> val0;
        {

            LOCK_GUARD(shard.mutex);

            // if can't find output skip
            if (shard.map.size() == 0)
            {
                LOG_DEBUG_ONLY("Early exit");
                return;
            }

            if (!shard.map.contains(This))
                return;

            val0 = std::move(shard.map[This]);
            shard.map.erase(This);
        }

        do
        {
            // if this command list does not have entries skip
            if (val0.size() == 0)
                break;

            if (Config::Instance()->FGHudfixDisableDispatch.value_or_default())
                break;

            for (auto& [key, val] : val0)
            {
                val.captureInfo |= CaptureInfo::Dispatch;
                if (Hudfix_Dx12::CheckForHudless(This, &val, val.state))
                {
                    break;
                }
            }
        } while (false);
    }
}

#pragma endregion

void ResTrack_Dx12::HookCommandList(ID3D12Device* InDevice)
{

    if (o_OMSetRenderTargets != nullptr)
        return;

    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12CommandAllocator* commandAllocator = nullptr;

    if (InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)) == S_OK)
    {
        if (InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr,
                                        IID_PPV_ARGS(&commandList)) == S_OK)
        {
            ID3D12GraphicsCommandList* realCL = nullptr;
            if (!CheckForRealObject(__FUNCTION__, commandList, (IUnknown**) &realCL))
                realCL = commandList;

            // Get the vtable pointer
            PVOID* pVTable = *(PVOID**) realCL;
            const bool persistentBindings = Config::Instance()->FGHudfixPersistentBindings.value_or_default();

            // Persistent command-list binding invalidation
            if (persistentBindings)
            {
                o_Reset = (PFN_Reset) pVTable[10];
                o_ClearState = (PFN_ClearState) pVTable[11];
            }

            // hudless shader
            o_OMSetRenderTargets = (PFN_OMSetRenderTargets) pVTable[46];
            o_SetGraphicsRootDescriptorTable = (PFN_SetGraphicsRootDescriptorTable) pVTable[32];

            o_DrawInstanced = (PFN_DrawInstanced) pVTable[12];
            o_DrawIndexedInstanced = (PFN_DrawIndexedInstanced) pVTable[13];
            o_Dispatch = (PFN_Dispatch) pVTable[14];

            // hudless compute
            o_SetComputeRootDescriptorTable = (PFN_SetComputeRootDescriptorTable) pVTable[31];

            if (o_OMSetRenderTargets != nullptr)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                // Only needed for hudfix
                if (State::Instance().activeFgInput == FGInput::Upscaler)
                {
                    if (o_Reset != nullptr)
                        DetourAttach(&(PVOID&) o_Reset, hkReset);
                    if (o_ClearState != nullptr)
                        DetourAttach(&(PVOID&) o_ClearState, hkClearState);

                    if (o_OMSetRenderTargets != nullptr)
                        DetourAttach(&(PVOID&) o_OMSetRenderTargets, hkOMSetRenderTargets);

                    if (o_SetGraphicsRootDescriptorTable != nullptr)
                        DetourAttach(&(PVOID&) o_SetGraphicsRootDescriptorTable, hkSetGraphicsRootDescriptorTable);

                    if (o_SetComputeRootDescriptorTable != nullptr)
                        DetourAttach(&(PVOID&) o_SetComputeRootDescriptorTable, hkSetComputeRootDescriptorTable);

                    if (o_DrawIndexedInstanced != nullptr)
                        DetourAttach(&(PVOID&) o_DrawIndexedInstanced, hkDrawIndexedInstanced);

                    if (o_DrawInstanced != nullptr)
                        DetourAttach(&(PVOID&) o_DrawInstanced, hkDrawInstanced);

                    if (o_Dispatch != nullptr)
                        DetourAttach(&(PVOID&) o_Dispatch, hkDispatch);
                }

                auto detourResult = DetourTransactionCommit();
                if (detourResult != NO_ERROR)
                {
                    LOG_ERROR("Failed to hook CommandList methods: {:X}", detourResult);
                    _bindingTrackingEnabled.store(false, std::memory_order_release);
                    o_Reset = nullptr;
                    o_ClearState = nullptr;
                    o_OMSetRenderTargets = nullptr;
                    o_SetGraphicsRootDescriptorTable = nullptr;
                    o_DrawInstanced = nullptr;
                    o_DrawIndexedInstanced = nullptr;
                    o_Dispatch = nullptr;
                    o_SetComputeRootDescriptorTable = nullptr;
                }
                else if (State::Instance().activeFgInput == FGInput::Upscaler)
                {
                    _bindingTrackingEnabled.store(persistentBindings, std::memory_order_release);
                }
            }

            commandList->Close();
            commandList->Release();
        }

        commandAllocator->Reset();
        commandAllocator->Release();
    }
}

void ResTrack_Dx12::HookDevice(ID3D12Device* device)
{
    if (o_CreateDescriptorHeap != nullptr || State::Instance().activeFgInput == FGInput::NvngxFG)
        return;

    if (device == nullptr)
        return;

    bool initializeTracking = false;
    {
        std::unique_lock lock(_heapRegistryMutex);
        if (fgHeaps.capacity() < 65536)
        {
            fgHeaps.reserve(65536);
            initializeTracking = true;
        }
    }

    if (initializeTracking)
    {
        _useShards = Config::Instance()->FGUseShards.value_or_default();

        {
            std::scoped_lock lock(_trackedResourcesMutex);
            _trackedResources.reserve(1024);
        }

        if (Config::Instance()->FGHudfixPersistentBindings.value_or_default())
        {
            if (!_useShards)
            {
                std::lock_guard<std::mutex> lock(_bindingStateMutex);
                _bindingStates.reserve(256);
            }
            else
            {
                for (auto& shard : _bindingShards)
                {
                    std::lock_guard<BindingStateMutex> lock(shard.mutex);
                    shard.map.reserve(32);
                }
            }
        }
    }

    LOG_FUNC();

    ID3D12Device* realDevice = nullptr;
    if (!CheckForRealObject(__FUNCTION__, device, (IUnknown**) &realDevice))
        realDevice = device;

    // Get the vtable pointer
    PVOID* pVTable = *(PVOID**) realDevice;

    // Hudfix
    o_CreateDescriptorHeap = (PFN_CreateDescriptorHeap) pVTable[14];
    o_CreateShaderResourceView = (PFN_CreateShaderResourceView) pVTable[18];
    o_CreateUnorderedAccessView = (PFN_CreateUnorderedAccessView) pVTable[19];
    o_CreateRenderTargetView = (PFN_CreateRenderTargetView) pVTable[20];
    o_CreateSampler = (PFN_CreateSampler) pVTable[22];
    o_CopyDescriptors = (PFN_CopyDescriptors) pVTable[23];
    o_CopyDescriptorsSimple = (PFN_CopyDescriptorsSimple) pVTable[24];

    // o_CreateDepthStencilView = (PFN_CreateDepthStencilView) pVTable[21];
    // o_CreateConstantBufferView = (PFN_CreateConstantBufferView) pVTable[17];

    // Apply the detour

    if (o_CreateDescriptorHeap != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_CreateDescriptorHeap != nullptr)
            DetourAttach(&(PVOID&) o_CreateDescriptorHeap, hkCreateDescriptorHeap);

        if (o_CreateRenderTargetView != nullptr)
            DetourAttach(&(PVOID&) o_CreateRenderTargetView, hkCreateRenderTargetView);

        if (o_CreateShaderResourceView != nullptr)
            DetourAttach(&(PVOID&) o_CreateShaderResourceView, hkCreateShaderResourceView);

        if (o_CreateUnorderedAccessView != nullptr)
            DetourAttach(&(PVOID&) o_CreateUnorderedAccessView, hkCreateUnorderedAccessView);

        if (o_CopyDescriptors != nullptr)
            DetourAttach(&(PVOID&) o_CopyDescriptors, hkCopyDescriptors);

        if (o_CopyDescriptorsSimple != nullptr)
            DetourAttach(&(PVOID&) o_CopyDescriptorsSimple, hkCopyDescriptorsSimple);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook Descriptor methods: {:X}", detourResult);
            o_CreateDescriptorHeap = nullptr;
            o_CreateRenderTargetView = nullptr;
            o_CreateShaderResourceView = nullptr;
            o_CreateUnorderedAccessView = nullptr;
            o_CopyDescriptors = nullptr;
            o_CopyDescriptorsSimple = nullptr;
        }
    }

    HookCommandList(device);
}

void ResTrack_Dx12::ReleaseDeviceHooks()
{
    LOG_DEBUG("");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_CreateDescriptorHeap != nullptr)
        DetourDetach(&(PVOID&) o_CreateDescriptorHeap, hkCreateDescriptorHeap);

    if (o_CreateRenderTargetView != nullptr)
        DetourDetach(&(PVOID&) o_CreateRenderTargetView, hkCreateRenderTargetView);

    if (o_CreateShaderResourceView != nullptr)
        DetourDetach(&(PVOID&) o_CreateShaderResourceView, hkCreateShaderResourceView);

    if (o_CreateUnorderedAccessView != nullptr)
        DetourDetach(&(PVOID&) o_CreateUnorderedAccessView, hkCreateUnorderedAccessView);

    if (o_CopyDescriptors != nullptr)
        DetourDetach(&(PVOID&) o_CopyDescriptors, hkCopyDescriptors);

    if (o_CopyDescriptorsSimple != nullptr)
        DetourDetach(&(PVOID&) o_CopyDescriptorsSimple, hkCopyDescriptorsSimple);

    // CommandList
    if (o_Reset != nullptr)
        DetourDetach(&(PVOID&) o_Reset, hkReset);
    if (o_ClearState != nullptr)
        DetourDetach(&(PVOID&) o_ClearState, hkClearState);

    if (o_OMSetRenderTargets != nullptr)
        DetourDetach(&(PVOID&) o_OMSetRenderTargets, hkOMSetRenderTargets);

    if (o_SetGraphicsRootDescriptorTable != nullptr)
        DetourDetach(&(PVOID&) o_SetGraphicsRootDescriptorTable, hkSetGraphicsRootDescriptorTable);

    if (o_SetComputeRootDescriptorTable != nullptr)
        DetourDetach(&(PVOID&) o_SetComputeRootDescriptorTable, hkSetComputeRootDescriptorTable);

    if (o_DrawIndexedInstanced != nullptr)
        DetourDetach(&(PVOID&) o_DrawIndexedInstanced, hkDrawIndexedInstanced);

    if (o_DrawInstanced != nullptr)
        DetourDetach(&(PVOID&) o_DrawInstanced, hkDrawInstanced);

    if (o_Dispatch != nullptr)
        DetourDetach(&(PVOID&) o_Dispatch, hkDispatch);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook DX12 methods: {:X}", detourResult);
    }
    else
    {
        // Device
        o_CreateDescriptorHeap = nullptr;
        o_CreateRenderTargetView = nullptr;
        o_CreateShaderResourceView = nullptr;
        o_CreateUnorderedAccessView = nullptr;
        o_CopyDescriptors = nullptr;
        o_CopyDescriptorsSimple = nullptr;

        // CommandList
        o_Reset = nullptr;
        o_ClearState = nullptr;
        o_OMSetRenderTargets = nullptr;
        o_SetGraphicsRootDescriptorTable = nullptr;
        o_SetComputeRootDescriptorTable = nullptr;
        o_DrawIndexedInstanced = nullptr;
        o_DrawInstanced = nullptr;
        o_Dispatch = nullptr;

        _bindingTrackingEnabled.store(false, std::memory_order_release);
        ClearBindingStates();
    }
}

void ResTrack_Dx12::ReleaseHooks()
{
    LOG_DEBUG("");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // if (o_CreateDescriptorHeap != nullptr)
    //     DetourDetach(&(PVOID&) o_CreateDescriptorHeap, hkCreateDescriptorHeap);

    // if (o_CreateRenderTargetView != nullptr)
    //     DetourDetach(&(PVOID&) o_CreateRenderTargetView, hkCreateRenderTargetView);

    // if (o_CreateShaderResourceView != nullptr)
    //     DetourDetach(&(PVOID&) o_CreateShaderResourceView, hkCreateShaderResourceView);

    // if (o_CreateUnorderedAccessView != nullptr)
    //     DetourDetach(&(PVOID&) o_CreateUnorderedAccessView, hkCreateUnorderedAccessView);

    // if (o_CopyDescriptors != nullptr)
    //     DetourDetach(&(PVOID&) o_CopyDescriptors, hkCopyDescriptors);

    // if (o_CopyDescriptorsSimple != nullptr)
    //     DetourDetach(&(PVOID&) o_CopyDescriptorsSimple, hkCopyDescriptorsSimple);

    // o_CreateDescriptorHeap = nullptr;
    // o_CreateRenderTargetView = nullptr;
    // o_CreateShaderResourceView = nullptr;
    // o_CreateUnorderedAccessView = nullptr;
    // o_CopyDescriptors = nullptr;
    // o_CopyDescriptorsSimple = nullptr;

    if (o_Reset != nullptr)
        DetourDetach(&(PVOID&) o_Reset, hkReset);

    if (o_ClearState != nullptr)
        DetourDetach(&(PVOID&) o_ClearState, hkClearState);

    if (o_OMSetRenderTargets != nullptr)
        DetourDetach(&(PVOID&) o_OMSetRenderTargets, hkOMSetRenderTargets);

    if (o_SetGraphicsRootDescriptorTable != nullptr)
        DetourDetach(&(PVOID&) o_SetGraphicsRootDescriptorTable, hkSetGraphicsRootDescriptorTable);

    if (o_SetComputeRootDescriptorTable != nullptr)
        DetourDetach(&(PVOID&) o_SetComputeRootDescriptorTable, hkSetComputeRootDescriptorTable);

    if (o_DrawIndexedInstanced != nullptr)
        DetourDetach(&(PVOID&) o_DrawIndexedInstanced, hkDrawIndexedInstanced);

    if (o_DrawInstanced != nullptr)
        DetourDetach(&(PVOID&) o_DrawInstanced, hkDrawInstanced);

    if (o_Dispatch != nullptr)
        DetourDetach(&(PVOID&) o_Dispatch, hkDispatch);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook CommandList methods: {:X}", detourResult);
    }
    else
    {
        o_Reset = nullptr;
        o_ClearState = nullptr;
        o_OMSetRenderTargets = nullptr;
        o_SetGraphicsRootDescriptorTable = nullptr;
        o_SetComputeRootDescriptorTable = nullptr;
        o_DrawIndexedInstanced = nullptr;
        o_DrawInstanced = nullptr;
        o_Dispatch = nullptr;

        _bindingTrackingEnabled.store(false, std::memory_order_release);

        ClearBindingStates();
    }
}

void ResTrack_Dx12::ClearPossibleHudless()
{
    LOG_DEBUG("");

    auto hfIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

    if (!_useShards)
    {
        std::lock_guard<std::mutex> lock(_hudlessTrackMutex);
        fgPossibleHudless[hfIndex].clear();
    }
    else
    {
        for (size_t i = 0; i < SHARD_COUNT; i++)
        {
            auto& shard = _hudlessShards[hfIndex][i];

            LOCK_GUARD(shard.mutex);

            shard.map.clear();
        }
    }
}
