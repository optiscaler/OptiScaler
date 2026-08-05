#include "pch.h"
#include "ResTrack_dx12.h"

#include <Config.h>
#include <State.h>
#include <Util.h>

#include <menu/menu_overlay_dx.h>

#include <algorithm>
#include <future>

#include <magic_enum_utility.hpp>
#include <include/d3dx/d3dx12.h>
#include <detours/detours.h>

#ifndef STDMETHODCALLTYPE
#include <Unknwn.h> // or <objbase.h> to get STDMETHODCALLTYPE
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

// Command list hooks for FG
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
typedef void(STDMETHODCALLTYPE* PFN_ExecuteBundle)(ID3D12GraphicsCommandList* This,
                                                   ID3D12GraphicsCommandList* pCommandList);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Close)(ID3D12GraphicsCommandList* This);

typedef void(STDMETHODCALLTYPE* PFN_ExecuteCommandLists)(ID3D12CommandQueue* This, UINT NumCommandLists,
                                                         ID3D12CommandList* const* ppCommandLists);

typedef ULONG(STDMETHODCALLTYPE* PFN_Release)(ID3D12Resource* This);

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
static PFN_ExecuteBundle o_ExecuteBundle = nullptr;
static PFN_Close o_Close = nullptr;

static PFN_ExecuteCommandLists o_ExecuteCommandLists = nullptr;
static PFN_Release o_Release = nullptr;

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

static std::set<void*> _notFoundCmdLists;
static std::unordered_map<FG_ResourceType, void*> _resCmdList[BUFFER_COUNT];

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

bool ResTrack_Dx12::CheckResource(ID3D12Resource* resource)
{
    if (State::Instance().isShuttingDown)
        return false;

    auto resDesc = resource->GetDesc();

    if (resDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        return false;

    auto& s = State::Instance();

    if (resDesc.Height != s.currentSwapchainDesc.BufferDesc.Height ||
        resDesc.Width != s.currentSwapchainDesc.BufferDesc.Width)
    {
        auto result = Config::Instance()->FGRelaxedResolutionCheck.value_or_default() &&
                      resDesc.Height >= s.currentSwapchainDesc.BufferDesc.Height - 32 &&
                      resDesc.Height <= s.currentSwapchainDesc.BufferDesc.Height + 32 &&
                      resDesc.Width >= s.currentSwapchainDesc.BufferDesc.Width - 32 &&
                      resDesc.Width <= s.currentSwapchainDesc.BufferDesc.Width + 32;

        // LOG_TRACK("Resource: {}x{} ({}), Swapchain: {}x{} ({}), Relaxed Result: {}", resDesc.Width, resDesc.Height,
        //           (UINT) resDesc.Format, scDesc.BufferDesc.Width, scDesc.BufferDesc.Height,
        //           (UINT) scDesc.BufferDesc.Format, result);

        return result;
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

void ResTrack_Dx12::FillResourceInfo(ID3D12Resource* resource, ResourceInfo* info)
{
    auto desc = resource->GetDesc();
    info->buffer = resource;
    info->width = desc.Width;
    info->height = desc.Height;
    info->format = desc.Format;
    info->flags = desc.Flags;
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
        State::Instance().FGchanged)
    {
        LOG_TRACK("State::Instance().currentFG == nullptr || State::Instance().currentFeature == nullptr || "
                  "State::Instance().FGchanged");
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
        for (size_t i = 0; i < State::Instance().SCbuffers.size(); i++)
        {
            if (State::Instance().SCbuffers[i] == pResource)
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

    if (Config::Instance()->FGHudfixDisableRTV.value_or_default())
        return;

    if (pResource == nullptr || !CheckResource(pResource))
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
        ResourceInfo resInfo {};
        FillResourceInfo(pResource, &resInfo);
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
        for (size_t i = 0; i < State::Instance().SCbuffers.size(); i++)
        {
            if (State::Instance().SCbuffers[i] == pResource)
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

    if (Config::Instance()->FGHudfixDisableSRV.value_or_default())
        return;

    if (pResource == nullptr || !CheckResource(pResource))
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
        ResourceInfo resInfo {};
        FillResourceInfo(pResource, &resInfo);
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
        for (size_t i = 0; i < State::Instance().SCbuffers.size(); i++)
        {
            if (State::Instance().SCbuffers[i] == pResource)
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

    if (Config::Instance()->FGHudfixDisableUAV.value_or_default())
        return;

    if (pResource == nullptr || !CheckResource(pResource))
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
        ResourceInfo resInfo {};
        FillResourceInfo(pResource, &resInfo);
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

void ResTrack_Dx12::hkExecuteCommandLists(ID3D12CommandQueue* This, UINT NumCommandLists,
                                          ID3D12CommandList* const* ppCommandLists)
{
    auto fg = State::Instance().currentFG;

    if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
    {
        LOG_TRACK("NumCommandLists: {}", NumCommandLists);

        std::vector<FG_ResourceType> found;
        auto fIndex = fg->GetIndex();

        do
        {
            std::lock_guard<std::mutex> lock2(_resourceCommandListMutex);

            if (!_notFoundCmdLists.empty())
            {
                for (size_t i = 0; i < NumCommandLists; i++)
                {
                    if (_notFoundCmdLists.contains(ppCommandLists[i]))
                    {
                        LOG_WARN("Found last frames cmdList: {:X}", (size_t) ppCommandLists[i]);
                        _notFoundCmdLists.erase(ppCommandLists[i]);
                    }
                }
            }

            if (_resCmdList[fIndex].empty())
                break;

            for (size_t i = 0; i < NumCommandLists; i++)
            {
                LOG_TRACK("ppCommandLists[{}]: {:X}", i, (size_t) ppCommandLists[i]);

                for (const auto& pair : _resCmdList[fIndex])
                {
                    if (pair.second == ppCommandLists[i])
                    {
                        LOG_DEBUG("found {} cmdList: {:X}, queue: {:X}", (UINT) pair.first, (size_t) pair.second,
                                  (size_t) This);
                        fg->SetResourceReady(pair.first);
                        found.push_back(pair.first);
                    }
                }

                for (size_t i = 0; i < found.size(); i++)
                {
                    _resCmdList[fIndex].erase(found[i]);
                }

                if (_resCmdList[fIndex].empty())
                    break;
            }

        } while (false);

        if (!found.empty())
        {
            o_ExecuteCommandLists(This, NumCommandLists, ppCommandLists);

            for (size_t i = 0; i < found.size(); i++)
            {
                fg->SetCommandQueue(found[i], This);
            }

            return;
        }
    }

    LOG_TRACK("Done NumCommandLists: {}", NumCommandLists);

    o_ExecuteCommandLists(This, NumCommandLists, ppCommandLists);
}

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

                    fgHeaps[i] = std::make_shared<HeapInfo>(heap, cpuStart, cpuEnd, gpuStart, gpuEnd,
                                                           numDescriptors, increment, type);

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

                fgHeaps.push_back(std::make_shared<HeapInfo>(heap, cpuStart, cpuEnd, gpuStart, gpuEnd,
                                                            numDescriptors, increment, type));

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

ULONG ResTrack_Dx12::hkRelease(ID3D12Resource* This)
{
    if (State::Instance().isShuttingDown)
        return o_Release(This);

    std::vector<TrackedResourceSlot> toClean;
    {
        std::lock_guard lock(_trackedResourcesMutex);

        This->AddRef();
        auto refCount = o_Release(This);

        if (refCount <= 1)
        {
            if (auto it = _trackedResources.find(This); it != _trackedResources.end())
            {
                toClean = std::move(it->second);
                _trackedResources.erase(it);
            }

            State::Instance().capturedHudlesses.erase(This);
        }
    }

    // Clean descriptor slots outside the reverse-index lock.
    for (const auto& slot : toClean)
    {
        if (auto heap = slot.heap.lock())
            heap->ClearSlotIfMatches(slot.index, This);
    }

    return o_Release(This);
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

    // Cache for heap lookups to avoid repeated lookups within the same range
    std::shared_ptr<HeapInfo> cachedDestHeap;
    SIZE_T cachedDestRangeStart = 0;
    UINT cachedDestRangeSize = 0;
    std::shared_ptr<HeapInfo> cachedSrcHeap;
    SIZE_T cachedSrcRangeStart = 0;
    UINT cachedSrcRangeSize = 0;

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
        }

        // Calculate current destination handle
        const SIZE_T destHandle = cachedDestRangeStart + (static_cast<SIZE_T>(destOffsetInRange) * inc);

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
            }

            // Calculate current source handle
            const SIZE_T srcHandle = cachedSrcRangeStart + (static_cast<SIZE_T>(srcOffsetInRange) * inc);

            if (cachedSrcHeap != nullptr)
                haveSrcInfo = cachedSrcHeap->GetByCpuHandle(srcHandle, srcInfo);

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
            if (haveSrcInfo)
                cachedDestHeap->SetByCpuHandle(destHandle, srcInfo);
            else
                cachedDestHeap->ClearByCpuHandle(destHandle);
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

    auto size = This->GetDescriptorHandleIncrementSize(DescriptorHeapsType);

    for (size_t i = 0; i < NumDescriptors; i++)
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

#pragma region Shader input hooks

void ResTrack_Dx12::hkSetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* This, UINT RootParameterIndex,
                                                     D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
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

    LOG_DEBUG_ONLY("CommandList: {:X}, Resource: {:X}", (size_t) This, (size_t) capturedBuffer.buffer);

    // Only proceed with tracking if we have a valid buffer
    capturedBuffer.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    capturedBuffer.captureInfo = CaptureInfo::SetGR;

    // Track the resource
    bool capturedImmediately = false;
    if (Config::Instance()->FGImmediateCapture.value_or_default())
    {
        capturedImmediately = Hudfix_Dx12::CheckForHudless(This, &capturedBuffer, capturedBuffer.state);
    }

    if (!capturedImmediately)
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

#ifdef USE_SPINLOCK_MUTEX
            std::lock_guard<SpinLock> lock(shard.mutex);
#else
            std::lock_guard<std::mutex> lock(shard.mutex);
#endif

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

        // Valid resource found, update state
        capturedBuffer.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        capturedBuffer.captureInfo = CaptureInfo::OMSetRTV;

        // Check for immediate capture
        bool capturedImmediately = false;
        if (Config::Instance()->FGImmediateCapture.value_or_default())
        {
            capturedImmediately = Hudfix_Dx12::CheckForHudless(This, &capturedBuffer, capturedBuffer.state);
            if (capturedImmediately)
                break; // Early exit if captured
        }

        // Track for later processing
        if (!capturedImmediately)
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

#ifdef USE_SPINLOCK_MUTEX
                std::lock_guard<SpinLock> lock(shard.mutex);
#else
                std::lock_guard<std::mutex> lock(shard.mutex);
#endif

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

    LOG_DEBUG_ONLY("CommandList: {:X}, Resource: {:X}", (size_t) This, (size_t) capturedBuffer.buffer);

    // Only proceed with tracking if we have a valid buffer
    if (capturedBuffer.type == UAV)
        capturedBuffer.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    else
        capturedBuffer.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    capturedBuffer.captureInfo = CaptureInfo::SetCR;

    // Track the resource
    bool capturedImmediately = false;
    if (Config::Instance()->FGImmediateCapture.value_or_default())
    {
        capturedImmediately = Hudfix_Dx12::CheckForHudless(This, &capturedBuffer, capturedBuffer.state);
    }

    if (!capturedImmediately)
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

#ifdef USE_SPINLOCK_MUTEX
            std::lock_guard<SpinLock> lock(shard.mutex);
#else
            std::lock_guard<std::mutex> lock(shard.mutex);
#endif

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

            std::lock_guard<std::mutex> lock(_drawMutex);
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

        if (This == MenuOverlayDx::MenuCommandList() && shard.map.contains(This))
        {
#ifdef USE_SPINLOCK_MUTEX
            std::lock_guard<SpinLock> lock(shard.mutex);
#else
            std::lock_guard<std::mutex> lock(shard.mutex);
#endif

            shard.map.erase(This);
            return;
        }

        ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> val0;
        {

#ifdef USE_SPINLOCK_MUTEX
            std::lock_guard<SpinLock> lock(shard.mutex);
#else
            std::lock_guard<std::mutex> lock(shard.mutex);
#endif

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
                std::lock_guard<std::mutex> lock(_drawMutex);

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

            std::lock_guard<std::mutex> lock(_drawMutex);
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

        if (This == MenuOverlayDx::MenuCommandList() && shard.map.contains(This))
        {
#ifdef USE_SPINLOCK_MUTEX
            std::lock_guard<SpinLock> lock(shard.mutex);
#else
            std::lock_guard<std::mutex> lock(shard.mutex);
#endif

            shard.map.erase(This);
            return;
        }

        ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> val0;
        {

#ifdef USE_SPINLOCK_MUTEX
            std::lock_guard<SpinLock> lock(shard.mutex);
#else
            std::lock_guard<std::mutex> lock(shard.mutex);
#endif

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
                // LOG_DEBUG("Waiting _drawMutex {:X}", (size_t)val.buffer);
                std::lock_guard<std::mutex> lock(_drawMutex);

                val.captureInfo |= CaptureInfo::DrawIndexedInstanced;

                if (Hudfix_Dx12::CheckForHudless(This, &val, val.state))
                    break;
            }

        } while (false);
    }
}

void ResTrack_Dx12::hkExecuteBundle(ID3D12GraphicsCommandList* This, ID3D12GraphicsCommandList* pCommandList)
{
    LOG_FUNC();

    IFGFeature_Dx12* fg = State::Instance().currentFG;
    auto index = fg != nullptr ? fg->GetIndex() : 0;

    {
        std::lock_guard<std::mutex> lock(_resourceCommandListMutex);

        if (fg != nullptr && fg->IsActive() && (_resourceCommandList[index].size() > 0 || !_resCmdList[index].empty()))
        {
            if (_notFoundCmdLists.contains(pCommandList))
                LOG_WARN("Found last frames cmdList: {:X}", (size_t) This);

            auto& frameCmdList = _resourceCommandList[index];
            for (std::unordered_map<FG_ResourceType, ID3D12GraphicsCommandList*>::iterator it = frameCmdList.begin();
                 it != frameCmdList.end(); ++it)
            {
                if (it->second == pCommandList)
                    it->second = This;
            }

            for (std::unordered_map<FG_ResourceType, void*>::iterator it = _resCmdList[index].begin();
                 it != _resCmdList[index].end(); ++it)
            {
                if (it->second == pCommandList)
                    it->second = This;
            }
        }
    }

    o_ExecuteBundle(This, pCommandList);
}

HRESULT ResTrack_Dx12::hkClose(ID3D12GraphicsCommandList* This)
{
    auto fg = State::Instance().currentFG;
    auto index = fg != nullptr ? fg->GetIndex() : 0;

    if (fg != nullptr && fg->IsActive() && !fg->IsPaused() && _resourceCommandList[index].size() > 0)
    {
        LOG_TRACK("CmdList: {:X}", (size_t) This);

        std::lock_guard<std::mutex> lock(_resourceCommandListMutex);

        if (_notFoundCmdLists.contains(This))
            LOG_WARN("Found last frames cmdList: {:X}", (size_t) This);

        std::vector<FG_ResourceType> found;

        for (const auto& pair : _resourceCommandList[index])
        {
            if (This == pair.second)
            {
                if (!fg->IsResourceReady(pair.first))
                {
                    LOG_DEBUG("{} cmdList: {:X}", (UINT) pair.first, (size_t) This);
                    _resCmdList[index][pair.first] = pair.second;
                    found.push_back(pair.first);
                }
            }
        }

        for (size_t i = 0; i < found.size(); i++)
        {
            _resourceCommandList[index].erase(found[i]);
        }
    }

    return o_Close(This);
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

            std::lock_guard<std::mutex> lock(_drawMutex);
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

        if (This == MenuOverlayDx::MenuCommandList() && shard.map.contains(This))
        {
#ifdef USE_SPINLOCK_MUTEX
            std::lock_guard<SpinLock> lock(shard.mutex);
#else
            std::lock_guard<std::mutex> lock(shard.mutex);
#endif

            shard.map.erase(This);
            return;
        }

        ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> val0;
        {

#ifdef USE_SPINLOCK_MUTEX
            std::lock_guard<SpinLock> lock(shard.mutex);
#else
            std::lock_guard<std::mutex> lock(shard.mutex);
#endif

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
                // LOG_DEBUG("Waiting _drawMutex {:X}", (size_t)val.buffer);
                std::lock_guard<std::mutex> lock(_drawMutex);

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

void ResTrack_Dx12::HookResource(ID3D12Device* InDevice)
{
    if (o_Release != nullptr)
        return;

    ID3D12Resource* tmp = nullptr;
    auto d = CD3DX12_RESOURCE_DESC::Buffer(4);
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    HRESULT hr = InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &d,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&tmp));

    if (hr == S_OK)
    {
        PVOID* pVTable = *(PVOID**) tmp;
        o_Release = (PFN_Release) pVTable[2];

        if (o_Release != nullptr)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&) o_Release, hkRelease);
            auto detourResult = DetourTransactionCommit();

            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to hook Heap Release: {:X}", detourResult);
                o_Release = nullptr;
                tmp->Release();
            }
            else
            {
                o_Release(tmp); // drop temp
            }
        }
        else
        {
            tmp->Release();
        }
    }
}

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

            // hudless shader
            o_OMSetRenderTargets = (PFN_OMSetRenderTargets) pVTable[46];
            o_SetGraphicsRootDescriptorTable = (PFN_SetGraphicsRootDescriptorTable) pVTable[32];

            o_DrawInstanced = (PFN_DrawInstanced) pVTable[12];
            o_DrawIndexedInstanced = (PFN_DrawIndexedInstanced) pVTable[13];
            o_Dispatch = (PFN_Dispatch) pVTable[14];
            o_Close = (PFN_Close) pVTable[9];

            // hudless compute
            o_SetComputeRootDescriptorTable = (PFN_SetComputeRootDescriptorTable) pVTable[31];

            o_ExecuteBundle = (PFN_ExecuteBundle) pVTable[27];

            if (o_OMSetRenderTargets != nullptr)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                // Only needed for hudfix
                if (State::Instance().activeFgInput == FGInput::Upscaler)
                {
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

                if (o_Close != nullptr)
                    DetourAttach(&(PVOID&) o_Close, hkClose);

                if (o_ExecuteBundle != nullptr)
                    DetourAttach(&(PVOID&) o_ExecuteBundle, hkExecuteBundle);

                auto detourResult = DetourTransactionCommit();
                if (detourResult != NO_ERROR)
                {
                    LOG_ERROR("Failed to hook CommandList methods: {:X}", detourResult);
                    o_OMSetRenderTargets = nullptr;
                    o_SetGraphicsRootDescriptorTable = nullptr;
                    o_DrawInstanced = nullptr;
                    o_DrawIndexedInstanced = nullptr;
                    o_Dispatch = nullptr;
                    o_Close = nullptr;
                    o_SetComputeRootDescriptorTable = nullptr;
                    o_ExecuteBundle = nullptr;
                }
            }

            commandList->Close();
            commandList->Release();
        }

        commandAllocator->Reset();
        commandAllocator->Release();
    }
}

void ResTrack_Dx12::HookToQueue(ID3D12Device* InDevice)
{
    if (o_ExecuteCommandLists != nullptr)
        return;

    ID3D12CommandQueue* queue = nullptr;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    auto hr = InDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue));

    if (hr == S_OK)
    {
        ID3D12CommandQueue* realQueue = nullptr;
        if (!CheckForRealObject(__FUNCTION__, queue, (IUnknown**) &realQueue))
            realQueue = queue;

        // Get the vtable pointer
        PVOID* pVTable = *(PVOID**) realQueue;

        o_ExecuteCommandLists = (PFN_ExecuteCommandLists) pVTable[10];

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_ExecuteCommandLists != nullptr)
            DetourAttach(&(PVOID&) o_ExecuteCommandLists, hkExecuteCommandLists);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook CommandList methods: {:X}", detourResult);
            o_ExecuteCommandLists = nullptr;
        }

        queue->Release();
    }
}

void ResTrack_Dx12::HookDevice(ID3D12Device* device)
{
    if (o_CreateDescriptorHeap != nullptr || State::Instance().activeFgInput == FGInput::Nukems)
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
        std::scoped_lock lock(_trackedResourcesMutex);
        _trackedResources.reserve(1024);
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

    HookToQueue(device);
    HookCommandList(device);
    HookResource(device);
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

    // Queue
    if (o_ExecuteCommandLists != nullptr)
        DetourDetach(&(PVOID&) o_ExecuteCommandLists, hkExecuteCommandLists);

    // CommandList
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

    if (o_Close != nullptr)
        DetourDetach(&(PVOID&) o_Close, hkClose);

    if (o_ExecuteBundle != nullptr)
        DetourDetach(&(PVOID&) o_ExecuteBundle, hkExecuteBundle);

    // Resource
    if (o_Release != nullptr)
        DetourDetach(&(PVOID&) o_Release, hkRelease);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook Resource methods: {:X}", detourResult);
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

        // Queue
        o_ExecuteCommandLists = nullptr;

        // CommandList
        o_OMSetRenderTargets = nullptr;
        o_SetGraphicsRootDescriptorTable = nullptr;
        o_SetComputeRootDescriptorTable = nullptr;
        o_DrawIndexedInstanced = nullptr;
        o_DrawInstanced = nullptr;
        o_Dispatch = nullptr;
        o_Close = nullptr;
        o_ExecuteBundle = nullptr;

        // Resource
        o_Release = nullptr;
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

    // if (o_ExecuteCommandLists != nullptr)
    //     DetourAttach(&(PVOID&) o_ExecuteCommandLists, hkExecuteCommandLists);

    // o_ExecuteCommandLists = nullptr;

    // if (o_Release != nullptr)
    //     DetourAttach(&(PVOID&) o_Release, hkRelease);

    // o_Release = nullptr;

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

    if (o_Close != nullptr)
        DetourDetach(&(PVOID&) o_Close, hkClose);

    if (o_ExecuteBundle != nullptr)
        DetourDetach(&(PVOID&) o_ExecuteBundle, hkExecuteBundle);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook CommandList methods: {:X}", detourResult);
    }
    else
    {
        o_OMSetRenderTargets = nullptr;
        o_SetGraphicsRootDescriptorTable = nullptr;
        o_SetComputeRootDescriptorTable = nullptr;
        o_DrawIndexedInstanced = nullptr;
        o_DrawInstanced = nullptr;
        o_Dispatch = nullptr;
        o_Close = nullptr;
        o_ExecuteBundle = nullptr;
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

#ifdef USE_SPINLOCK_MUTEX
            std::lock_guard<SpinLock> lock(shard.mutex);
#else
            std::lock_guard<std::mutex> lock(shard.mutex);
#endif

            shard.map.clear();
        }
    }

    std::lock_guard<std::mutex> lock2(_resourceCommandListMutex);

    auto fg = State::Instance().currentFG;
    if (fg != nullptr)
    {
        auto fIndex = fg->GetIndex();

        if (_notFoundCmdLists.size() > 10)
            _notFoundCmdLists.clear();

        for (const auto& pair : _resourceCommandList[fIndex])
        {
            LOG_WARN("{} cmdList: {:X}, not closed!", (UINT) pair.first, (size_t) pair.second);
            _notFoundCmdLists.insert(pair.second);
        }

        _resourceCommandList[fIndex].clear();

        for (const auto& pair : _resCmdList[fIndex])
        {
            LOG_WARN("{} cmdList: {:X}, not executed!", (UINT) pair.first, (size_t) pair.second);
            _notFoundCmdLists.insert(pair.second);
        }

        _resCmdList[fIndex].clear();
    }
}

void ResTrack_Dx12::SetResourceCmdList(FG_ResourceType type, ID3D12GraphicsCommandList* cmdList)
{
    auto fg = State::Instance().currentFG;
    if (fg != nullptr && fg->IsActive())
    {
        auto index = fg->GetIndex();

        ID3D12GraphicsCommandList* realCmdList = nullptr;
        if (!CheckForRealObject(__FUNCTION__, cmdList, (IUnknown**) &realCmdList))
            realCmdList = cmdList;

        _resourceCommandList[index][type] = realCmdList;
        LOG_DEBUG("_resourceCommandList[{}][{}]: {:X}", index, magic_enum::enum_name(type), (size_t) realCmdList);
    }
}
