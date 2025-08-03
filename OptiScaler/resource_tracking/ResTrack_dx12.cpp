#include "ResTrack_dx12.h"

#include <Config.h>
#include <State.h>
#include <Util.h>

#include <menu/menu_overlay_dx.h>

#include <algorithm>
#include <future>

#include <include/d3dx/d3dx12.h>
#include <detours/detours.h>

#include <initguid.h>
#include <guiddef.h>

DEFINE_GUID(GUID_Tracking, 0x12345678, 0x1234, 0x1234, 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef);

static UINT _trackMark = 1;

// Device hooks for FG
typedef void (*PFN_CreateRenderTargetView)(ID3D12Device* This, ID3D12Resource* pResource,
                                           D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
                                           D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void (*PFN_CreateShaderResourceView)(ID3D12Device* This, ID3D12Resource* pResource,
                                             D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
                                             D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void (*PFN_CreateUnorderedAccessView)(ID3D12Device* This, ID3D12Resource* pResource,
                                              ID3D12Resource* pCounterResource, D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc,
                                              D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void (*PFN_CreateDepthStencilView)(ID3D12Device* This, ID3D12Resource* pResource,
                                           const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc,
                                           D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef void (*PFN_CreateConstantBufferView)(ID3D12Device* This, const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc,
                                             D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

typedef void (*PFN_CreateSampler)(ID3D12Device* This, const D3D12_SAMPLER_DESC* pDesc,
                                  D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

typedef HRESULT (*PFN_CreateDescriptorHeap)(ID3D12Device* This, D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc,
                                            REFIID riid, void** ppvHeap);
typedef void (*PFN_CopyDescriptors)(ID3D12Device* This, UINT NumDestDescriptorRanges,
                                    D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts,
                                    UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges,
                                    D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts,
                                    UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
typedef void (*PFN_CopyDescriptorsSimple)(ID3D12Device* This, UINT NumDescriptors,
                                          D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
                                          D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
                                          D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);

// Command list hooks for FG
typedef void (*PFN_OMSetRenderTargets)(ID3D12GraphicsCommandList* This, UINT NumRenderTargetDescriptors,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
                                       BOOL RTsSingleHandleToDescriptorRange,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor);
typedef void (*PFN_SetGraphicsRootDescriptorTable)(ID3D12GraphicsCommandList* This, UINT RootParameterIndex,
                                                   D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
typedef void (*PFN_SetComputeRootDescriptorTable)(ID3D12GraphicsCommandList* This, UINT RootParameterIndex,
                                                  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
typedef void (*PFN_DrawIndexedInstanced)(ID3D12GraphicsCommandList* This, UINT IndexCountPerInstance,
                                         UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation,
                                         UINT StartInstanceLocation);
typedef void (*PFN_DrawInstanced)(ID3D12GraphicsCommandList* This, UINT VertexCountPerInstance, UINT InstanceCount,
                                  UINT StartVertexLocation, UINT StartInstanceLocation);
typedef void (*PFN_Dispatch)(ID3D12GraphicsCommandList* This, UINT ThreadGroupCountX, UINT ThreadGroupCountY,
                             UINT ThreadGroupCountZ);
typedef void (*PFN_ExecuteBundle)(ID3D12GraphicsCommandList* This, ID3D12GraphicsCommandList* pCommandList);
typedef void (*PFN_Close)(ID3D12GraphicsCommandList* This);

typedef void (*PFN_ExecuteCommandLists)(ID3D12CommandQueue* This, UINT NumCommandLists,
                                        ID3D12CommandList* const* ppCommandLists);

typedef ULONG (*PFN_Release)(ID3D12Resource* This);

// Original method calls for device
static PFN_CreateRenderTargetView o_CreateRenderTargetView = nullptr;
static PFN_CreateShaderResourceView o_CreateShaderResourceView = nullptr;
static PFN_CreateUnorderedAccessView o_CreateUnorderedAccessView = nullptr;
static PFN_CreateDepthStencilView o_CreateDepthStencilView = nullptr;
static PFN_CreateConstantBufferView o_CreateConstantBufferView = nullptr;
static PFN_CreateSampler o_CreateSampler = nullptr;

static PFN_CreateDescriptorHeap o_CreateDescriptorHeap = nullptr;
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

// heaps
static std::unique_ptr<HeapInfo> fgHeaps[1000];
static UINT fgHeapIndex = 0;

static void* _hudlessCmdList = nullptr;
static void* _inputsCmdList = nullptr;

struct HeapCacheTLS
{
    int index = -1;
    unsigned genSeen = 0;
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
    if (State::Instance().currentSwapchain == nullptr || State::Instance().isShuttingDown)
        return false;

    DXGI_SWAP_CHAIN_DESC scDesc {};
    if (State::Instance().currentSwapchain->GetDesc(&scDesc) != S_OK)
    {
        LOG_WARN("Can't get swapchain desc!");
        return false;
    }

    auto resDesc = resource->GetDesc();
    if (resDesc.Height != scDesc.BufferDesc.Height || resDesc.Width != scDesc.BufferDesc.Width)
    {
        return Config::Instance()->FGRelaxedResolutionCheck.value_or_default() &&
               resDesc.Height >= scDesc.BufferDesc.Height - 32 && resDesc.Height <= scDesc.BufferDesc.Height + 32 &&
               resDesc.Width >= scDesc.BufferDesc.Width - 32 && resDesc.Width <= scDesc.BufferDesc.Width + 32;
    }

    return true;
}

// possibleHudless lisy by cmdlist
static ankerl::unordered_dense::map<ID3D12GraphicsCommandList*,
                                    ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo>>
    fgPossibleHudless[BUFFER_COUNT];

static std::shared_mutex heapMutex;
static std::mutex hudlessMutex;

inline static IID streamlineRiid {};
bool ResTrack_Dx12::CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject)
{
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

    return false;
}

#pragma region Resource methods

bool ResTrack_Dx12::CreateBufferResource(ID3D12Device* InDevice, ResourceInfo* InSource, D3D12_RESOURCE_STATES InState,
                                         ID3D12Resource** OutResource)
{
    if (InDevice == nullptr || InSource == nullptr)
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
    std::shared_lock<std::shared_mutex> lock(heapMutex);
    for (UINT i = 0; i < fgHeapIndex; i++)
    {
        auto val = fgHeaps[i].get();
        if (val->cpuStart <= cpuHandle && val->cpuEnd >= cpuHandle && val->gpuStart != 0)
        {
            auto incSize = This->GetDescriptorHandleIncrementSize(type);
            auto addr = cpuHandle - val->cpuStart;
            auto index = addr / incSize;
            auto gpuAddr = val->gpuStart + (index * incSize);

            return gpuAddr;
        }
    }

    return NULL;
}

SIZE_T ResTrack_Dx12::GetCPUHandle(ID3D12Device* This, SIZE_T gpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    std::shared_lock<std::shared_mutex> lock(heapMutex);
    for (UINT i = 0; i < fgHeapIndex; i++)
    {
        auto val = fgHeaps[i].get();
        if (val->gpuStart <= gpuHandle && val->gpuEnd >= gpuHandle && val->cpuStart != 0)
        {
            auto incSize = This->GetDescriptorHandleIncrementSize(type);
            auto addr = gpuHandle - val->gpuStart;
            auto index = addr / incSize;
            auto cpuAddr = val->cpuStart + (index * incSize);

            return cpuAddr;
        }
    }

    return NULL;
}

HeapInfo* ResTrack_Dx12::GetHeapByCpuHandleCBV(SIZE_T cpuHandle)
{
    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    if (cacheCBV.genSeen == currentGen && cacheCBV.index != -1)
    {
        auto heapInfo = fgHeaps[cacheCBV.index].get();

        if (heapInfo->cpuStart <= cpuHandle && cpuHandle < heapInfo->cpuEnd)
            return heapInfo;
    }

    for (size_t i = 0; i < fgHeapIndex; i++)
    {
        if (fgHeaps[i]->cpuStart <= cpuHandle && fgHeaps[i]->cpuEnd > cpuHandle)
        {
            cacheCBV.index = i;
            cacheCBV.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    return nullptr;
}

HeapInfo* ResTrack_Dx12::GetHeapByCpuHandleRTV(SIZE_T cpuHandle)
{
    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    if (cacheRTV.genSeen == currentGen && cacheRTV.index != -1)
    {
        auto heapInfo = fgHeaps[cacheRTV.index].get();

        if (heapInfo->cpuStart <= cpuHandle && cpuHandle < heapInfo->cpuEnd)
            return heapInfo;
    }

    for (size_t i = 0; i < fgHeapIndex; i++)
    {
        if (fgHeaps[i]->cpuStart <= cpuHandle && fgHeaps[i]->cpuEnd > cpuHandle)
        {
            cacheRTV.index = i;
            cacheRTV.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    return nullptr;
}

HeapInfo* ResTrack_Dx12::GetHeapByCpuHandleSRV(SIZE_T cpuHandle)
{
    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    if (cacheSRV.genSeen == currentGen && cacheSRV.index != -1)
    {
        auto heapInfo = fgHeaps[cacheSRV.index].get();

        if (heapInfo->cpuStart <= cpuHandle && cpuHandle < heapInfo->cpuEnd)
            return heapInfo;
    }

    for (size_t i = 0; i < fgHeapIndex; i++)
    {
        if (fgHeaps[i]->cpuStart <= cpuHandle && fgHeaps[i]->cpuEnd > cpuHandle)
        {
            cacheSRV.index = i;
            cacheSRV.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    return nullptr;
}

HeapInfo* ResTrack_Dx12::GetHeapByCpuHandleUAV(SIZE_T cpuHandle)
{
    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    if (cacheUAV.genSeen == currentGen && cacheUAV.index != -1)
    {
        auto heapInfo = fgHeaps[cacheUAV.index].get();

        if (heapInfo->cpuStart <= cpuHandle && cpuHandle < heapInfo->cpuEnd)
            return heapInfo;
    }

    for (size_t i = 0; i < fgHeapIndex; i++)
    {
        if (fgHeaps[i]->cpuStart <= cpuHandle && fgHeaps[i]->cpuEnd > cpuHandle)
        {
            cacheUAV.index = i;
            cacheUAV.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    return nullptr;
}

HeapInfo* ResTrack_Dx12::GetHeapByCpuHandle(SIZE_T cpuHandle)
{
    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    {
        std::shared_lock<std::shared_mutex> lock(heapMutex);

        if (cache.genSeen == currentGen && cache.index != -1)
        {
            auto heapInfo = fgHeaps[cache.index].get();

            if (heapInfo->cpuStart <= cpuHandle && cpuHandle < heapInfo->cpuEnd)
                return heapInfo;
        }
    }

    for (size_t i = 0; i < fgHeapIndex; i++)
    {
        if (fgHeaps[i]->cpuStart <= cpuHandle && fgHeaps[i]->cpuEnd > cpuHandle)
        {
            cache.index = i;
            cache.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    return nullptr;
}

HeapInfo* ResTrack_Dx12::GetHeapByGpuHandleGR(SIZE_T gpuHandle)
{
    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    {
        std::shared_lock<std::shared_mutex> lock(heapMutex);

        if (cacheGR.genSeen == currentGen && cacheGR.index != -1)
        {
            auto heapInfo = fgHeaps[cacheGR.index].get();

            if (heapInfo->gpuStart <= gpuHandle && gpuHandle < heapInfo->gpuEnd)
                return heapInfo;
        }
    }

    for (size_t i = 0; i < fgHeapIndex; i++)
    {
        if (fgHeaps[i]->cpuStart <= gpuHandle && fgHeaps[i]->gpuEnd > gpuHandle)
        {
            cacheGR.index = i;
            cacheGR.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    return nullptr;
}

HeapInfo* ResTrack_Dx12::GetHeapByGpuHandleCR(SIZE_T gpuHandle)
{
    if (gpuHandle == NULL)
        return nullptr;

    unsigned currentGen = gHeapGeneration.load(std::memory_order_relaxed);

    {
        std::shared_lock<std::shared_mutex> lock(heapMutex);

        if (cacheCR.genSeen == currentGen && cacheCR.index != -1)
        {
            auto heapInfo = fgHeaps[cacheCR.index].get();

            if (heapInfo->gpuStart <= gpuHandle && gpuHandle < heapInfo->gpuEnd)
                return heapInfo;
        }
    }

    for (size_t i = 0; i < fgHeapIndex; i++)
    {
        if (fgHeaps[i]->cpuStart <= gpuHandle && fgHeaps[i]->gpuEnd > gpuHandle)
        {
            cacheCR.index = i;
            cacheCR.genSeen = currentGen;
            return fgHeaps[i].get();
        }
    }

    return nullptr;
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
        return false;
    }

    if (State::Instance().currentFG == nullptr || State::Instance().currentFeature == nullptr ||
        State::Instance().FGchanged)
    {
        return false;
    }

    if (!State::Instance().currentFG->IsActive())
    {
        return false;
    }

    if (!_presentDone)
    {
        return false;
    }

    if (Hudfix_Dx12::SkipHudlessChecks())
    {
        return false;
    }

    if (!Hudfix_Dx12::IsResourceCheckActive())
    {
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

    if (pResource == nullptr || pDesc == nullptr || pDesc->ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2D ||
        !CheckResource(pResource))
    {

        auto heap = GetHeapByCpuHandleSRV(DestDescriptor.ptr);

        if (heap != nullptr)
            heap->ClearByCpuHandle(DestDescriptor.ptr);

        return;
    }

    if (pResource == nullptr || pDesc == nullptr || pDesc->ViewDimension != D3D12_RTV_DIMENSION_TEXTURE2D)
        return;

    auto gpuHandle = GetGPUHandle(This, DestDescriptor.ptr, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    ResourceHeapInfo info {};
    info.cpuStart = DestDescriptor.ptr;
    info.gpuStart = gpuHandle;

    ResourceInfo resInfo {};
    FillResourceInfo(pResource, &resInfo);
    resInfo.type = RTV;

    auto heap = GetHeapByCpuHandleRTV(DestDescriptor.ptr);
    if (heap != nullptr)
        heap->SetByCpuHandle(DestDescriptor.ptr, resInfo);
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

    if (pResource == nullptr || pDesc == nullptr || pDesc->ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2D ||
        !CheckResource(pResource))
    {

        auto heap = GetHeapByCpuHandleSRV(DestDescriptor.ptr);

        if (heap != nullptr)
            heap->ClearByCpuHandle(DestDescriptor.ptr);

        return;
    }

    if (pResource == nullptr || pDesc == nullptr || pDesc->ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2D)
        return;

    auto gpuHandle = GetGPUHandle(This, DestDescriptor.ptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ResourceHeapInfo info {};
    info.cpuStart = DestDescriptor.ptr;
    info.gpuStart = gpuHandle;

    ResourceInfo resInfo {};
    FillResourceInfo(pResource, &resInfo);
    resInfo.type = SRV;

    auto heap = GetHeapByCpuHandleSRV(DestDescriptor.ptr);
    if (heap != nullptr)
        heap->SetByCpuHandle(DestDescriptor.ptr, resInfo);
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

    if (pResource == nullptr || pDesc == nullptr || pDesc->ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2D ||
        !CheckResource(pResource))
    {
        auto heap = GetHeapByCpuHandleSRV(DestDescriptor.ptr);

        if (heap != nullptr)
            heap->ClearByCpuHandle(DestDescriptor.ptr);

        return;
    }

    auto gpuHandle = GetGPUHandle(This, DestDescriptor.ptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ResourceHeapInfo info {};
    info.cpuStart = DestDescriptor.ptr;
    info.gpuStart = gpuHandle;

    ResourceInfo resInfo {};
    FillResourceInfo(pResource, &resInfo);
    resInfo.type = UAV;

    auto heap = GetHeapByCpuHandleUAV(DestDescriptor.ptr);
    if (heap != nullptr)
        heap->SetByCpuHandle(DestDescriptor.ptr, resInfo);
}

#pragma endregion

void ResTrack_Dx12::hkExecuteCommandLists(ID3D12CommandQueue* This, UINT NumCommandLists,
                                          ID3D12CommandList* const* ppCommandLists)
{
    auto signal = false;
    auto fg = State::Instance().currentFG;
    auto index = fg == nullptr ? 0 : fg->GetIndex();

    if ((State::Instance().activeFgInput == FGInput::Upscaler || State::Instance().activeFgInput == FGInput::DLSSG) &&
        (_hudlessCmdList != nullptr || _inputsCmdList != nullptr))
    {
        int cmdListCount = 0;
        int targetListCount = (_inputsCmdList == nullptr ? 0 : 1) + (fg->NoHudless() ? 0 : 1);

        for (size_t i = 0; i < NumCommandLists; i++)
        {
            if (_inputsCmdList != nullptr && _inputsCmdList == ppCommandLists[i])
            {
                LOG_DEBUG("_inputsCmdList: {:X}", (size_t) _inputsCmdList);
                cmdListCount++;
                _inputsCmdList = nullptr;
            }

            if (_hudlessCmdList != nullptr && _hudlessCmdList == ppCommandLists[i])
            {
                LOG_DEBUG("_hudlessCmdList: {:X}", (size_t) _hudlessCmdList);
                cmdListCount++;
                _hudlessCmdList = nullptr;
            }
        }

        if (cmdListCount == targetListCount)
        {
            std::vector<ID3D12CommandList*> ppCmdLists;

            for (size_t i = 0; i < NumCommandLists; i++)
            {
                ppCmdLists.push_back(ppCommandLists[i]);
            }

            auto fgCmdList = fg->GetCommandList();
            ppCmdLists.push_back(fgCmdList);

            LOG_DEBUG("Add fg command list: {:X}", (size_t) fgCmdList);

            o_ExecuteCommandLists(This, NumCommandLists + 1, ppCmdLists.data());

            return;
        }
    }

    o_ExecuteCommandLists(This, NumCommandLists, ppCommandLists);
}

#pragma region Heap hooks

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
            std::unique_lock<std::shared_mutex> lock(heapMutex);
            fgHeaps[fgHeapIndex] = std::make_unique<HeapInfo>(heap, cpuStart, cpuEnd, gpuStart, gpuEnd, numDescriptors,
                                                              increment, type, fgHeapIndex);
            fgHeapIndex++;
            gHeapGeneration.fetch_add(1, std::memory_order_relaxed);
        }
    }
    else
    {
        if (*ppvHeap != nullptr)
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

    _trMutex.lock();

    if (This->AddRef() == 2 && _trackedResources.contains(This))
    {
        auto vector = &_trackedResources[This];

        LOG_TRACK("Resource: {:X}, Heaps: {}", (size_t) This, vector->size());

        for (size_t i = 0; i < vector->size(); i++)
        {
            LOG_TRACK("  Resource: {:X}, Clearing: {:X}", (size_t) This, (size_t) vector->at(i));
            vector->at(i)->buffer = nullptr;
            vector->at(i)->lastUsedFrame = 0;
        }

        _trackedResources.erase(This);
    }

    o_Release(This);

    _trMutex.unlock();

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

    if (DescriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
        DescriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
        return;

    if (pDestDescriptorRangeStarts == nullptr || (*pDestDescriptorRangeStarts).ptr == 0 ||
        pDestDescriptorRangeSizes == nullptr)
        return;

    if (!Config::Instance()->FGAlwaysTrackHeaps.value_or_default() && !IsHudFixActive())
        return;

    // make copies, just in case
    D3D12_CPU_DESCRIPTOR_HANDLE* destRangeStarts = pDestDescriptorRangeStarts;
    UINT* destRangeSizes = pDestDescriptorRangeSizes;
    D3D12_CPU_DESCRIPTOR_HANDLE* srcRangeStarts = pSrcDescriptorRangeStarts;
    UINT* srcRangeSizes = pSrcDescriptorRangeSizes;

    LOG_DEBUG_ONLY("NumDestDescriptorRanges: {}, pDestDescriptorRangeStarts: {:X}, pDestDescriptorRangeSizes: {}",
                   NumDestDescriptorRanges,
                   (pDestDescriptorRangeStarts == nullptr || (*pDestDescriptorRangeStarts).ptr == 0)
                       ? 9999
                       : (size_t) (*pDestDescriptorRangeStarts).ptr,
                   (pDestDescriptorRangeSizes == nullptr) ? 9999 : *pDestDescriptorRangeSizes);
    LOG_DEBUG_ONLY("NumSrcDescriptorRanges: {}, pSrcDescriptorRangeStarts: {:X}, pSrcDescriptorRangeSizes: {}",
                   NumSrcDescriptorRanges,
                   (pSrcDescriptorRangeStarts == nullptr || (*pSrcDescriptorRangeStarts).ptr == 0)
                       ? 9999
                       : (size_t) (*pSrcDescriptorRangeStarts).ptr,
                   (pSrcDescriptorRangeSizes == nullptr) ? 9999 : *pSrcDescriptorRangeSizes);

    auto size = This->GetDescriptorHandleIncrementSize(DescriptorHeapsType);

    if (srcRangeStarts != nullptr && (*srcRangeStarts).ptr != 0 && srcRangeSizes != nullptr)
    {
        LOG_DEBUG_ONLY("Src based loop");

        size_t destRangeIndex = 0;
        size_t destIndex = 0;

        for (size_t i = 0; i < NumSrcDescriptorRanges; i++)
        {
            UINT copyCount = 1;

            if (srcRangeSizes != nullptr)
                copyCount = srcRangeSizes[i];

            LOG_DEBUG_ONLY("CopyCount[{}]: {}", i, copyCount);

            for (size_t j = 0; j < copyCount; j++)
            {
                LOG_DEBUG_ONLY("srcRangeIndex: {}, srcIndex: {}, dstRangeIndex: {}, dstIndex: {}", i, j, destRangeIndex,
                               destIndex);

                // source
                auto srcHandle = srcRangeStarts[i].ptr + j * size;
                auto srcHeap = GetHeapByCpuHandle(srcHandle);
                auto destHandle = destRangeStarts[destRangeIndex].ptr + destIndex * size;
                auto dstHeap = GetHeapByCpuHandle(destHandle);

                LOG_DEBUG_ONLY("srcHeap: {:X}, dstHeap: {:X}", srcHandle, destHandle);

                if (srcHeap == nullptr)
                {
                    if (dstHeap != nullptr)
                        dstHeap->ClearByCpuHandle(destHandle);

                    if (destRangeSizes == nullptr || destRangeSizes[destRangeIndex] == destIndex)
                    {
                        destIndex = 0;
                        destRangeIndex++;
                    }
                    else
                    {
                        destIndex++;
                    }

                    continue;
                }

                auto buffer = srcHeap->GetByCpuHandle(srcHandle);

                // destination
                if (dstHeap == nullptr)
                {
                    if (destRangeSizes == nullptr || destRangeSizes[destRangeIndex] == destIndex)
                    {
                        destIndex = 0;
                        destRangeIndex++;
                    }
                    else
                    {
                        destIndex++;
                    }

                    continue;
                }

                if (buffer == nullptr)
                {
                    dstHeap->ClearByCpuHandle(destHandle);

                    if (destRangeSizes == nullptr || destRangeSizes[destRangeIndex] == destIndex)
                    {
                        destIndex = 0;
                        destRangeIndex++;
                    }
                    else
                    {
                        destIndex++;
                    }

                    continue;
                }

                dstHeap->SetByCpuHandle(destHandle, *buffer);

                if (destRangeSizes == nullptr || destRangeSizes[destRangeIndex] == destIndex)
                {
                    destIndex = 0;
                    destRangeIndex++;
                }
                else
                {
                    destIndex++;
                }
            }
        }
    }
    else
    {
        LOG_DEBUG_ONLY("Dst based loop");

        size_t srcRangeIndex = 0;
        size_t srcIndex = 0;

        for (size_t i = 0; i < NumDestDescriptorRanges; i++)
        {
            UINT copyCount = 1;

            if (destRangeSizes != nullptr)
                copyCount = destRangeSizes[i];

            LOG_DEBUG_ONLY("CopyCount[{}]: {}", i, copyCount);

            for (size_t j = 0; j < copyCount; j++)
            {
                LOG_DEBUG_ONLY("dstRangeIndex: {}, dstIndex: {}, srcRangeIndex: {}, srcIndex: {}", i, j, srcRangeIndex,
                               srcIndex);

                HeapInfo* srcHeap = nullptr;
                unsigned long long srcHandle = 0;

                // source
                if (srcRangeStarts != nullptr && (*srcRangeStarts).ptr != 0)
                {
                    srcHandle = srcRangeStarts[srcRangeIndex].ptr + srcIndex * size;
                    srcHeap = GetHeapByCpuHandle(srcHandle);
                }

                auto destHandle = destRangeStarts[i].ptr + j * size;
                auto dstHeap = GetHeapByCpuHandle(destHandle);

                LOG_DEBUG_ONLY("dstHeap: {:X}, srcHeap: {:X}", destHandle, srcHandle);

                if (srcHeap == nullptr)
                {
                    if (dstHeap != nullptr)
                        dstHeap->ClearByCpuHandle(destHandle);

                    if (srcRangeSizes == nullptr || srcRangeSizes[srcRangeIndex] == srcIndex)
                    {
                        srcIndex = 0;
                        srcRangeIndex++;
                    }
                    else
                    {
                        srcIndex++;
                    }

                    continue;
                }

                auto buffer = srcHeap->GetByCpuHandle(srcHandle);

                // destination
                if (dstHeap == nullptr)
                {
                    if (srcRangeSizes == nullptr || srcRangeSizes[srcRangeIndex] == srcIndex)
                    {
                        srcIndex = 0;
                        srcRangeIndex++;
                    }
                    else
                    {
                        srcIndex++;
                    }

                    continue;
                }

                if (buffer == nullptr)
                {
                    dstHeap->ClearByCpuHandle(destHandle);

                    if (srcRangeSizes == nullptr || srcRangeSizes[srcRangeIndex] == srcIndex)
                    {
                        srcIndex = 0;
                        srcRangeIndex++;
                    }
                    else
                    {
                        srcIndex++;
                    }

                    continue;
                }

                dstHeap->SetByCpuHandle(destHandle, *buffer);

                if (srcRangeSizes == nullptr || srcRangeSizes[srcRangeIndex] == srcIndex)
                {
                    srcIndex = 0;
                    srcRangeIndex++;
                }
                else
                {
                    srcIndex++;
                }
            }
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
        HeapInfo* srcHeap = nullptr;
        unsigned long long srcHandle = 0;

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

        auto buffer = srcHeap->GetByCpuHandle(srcHandle);

        if (buffer == nullptr)
        {
            dstHeap->ClearByCpuHandle(destHandle);
            continue;
        }

        dstHeap->SetByCpuHandle(destHandle, *buffer);
    }
}

#pragma endregion

#pragma region Shader input hooks

void ResTrack_Dx12::hkSetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* This, UINT RootParameterIndex,
                                                     D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    if (BaseDescriptor.ptr == 0 || !IsHudFixActive() || Hudfix_Dx12::SkipHudlessChecks())
    {
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    if (This == MenuOverlayDx::MenuCommandList() /*|| IsFGCommandList(This)*/)
    {
        LOG_DEBUG_ONLY("Menu cmdlist: {} || fgCommandList: {}", This == MenuOverlayDx::MenuCommandList(),
                       IsFGCommandList(This));
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    auto heap = GetHeapByGpuHandleGR(BaseDescriptor.ptr);
    if (heap == nullptr)
    {
        LOG_DEBUG_ONLY("No heap!");
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    auto capturedBuffer = heap->GetByGpuHandle(BaseDescriptor.ptr);
    if (capturedBuffer == nullptr || capturedBuffer->buffer == nullptr)
    {
        LOG_DEBUG_ONLY("Miss RootParameterIndex: {1}, CommandList: {0:X}, gpuHandle: {2}", (SIZE_T) This,
                       RootParameterIndex, BaseDescriptor.ptr);
        o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    capturedBuffer->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    do
    {
        if (Config::Instance()->FGImmediateCapture.value_or_default())
        {
            if (Hudfix_Dx12::CheckForHudless(__FUNCTION__, This, capturedBuffer, capturedBuffer->state))
            {
                SetHudlessCmdList(This);
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(hudlessMutex);

            auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

            if (!fgPossibleHudless[fIndex].contains(This))
            {
                ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> newMap;
                fgPossibleHudless[fIndex].insert_or_assign(This, newMap);
            }

            LOG_TRACK("AddRef Resource: {:X}, Desc: {:X}", (size_t) capturedBuffer->buffer, BaseDescriptor.ptr);
            fgPossibleHudless[fIndex][This].insert_or_assign(capturedBuffer->buffer, *capturedBuffer);
        }
    } while (false);

    o_SetGraphicsRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
}

#pragma endregion

#pragma region Shader output hooks

void ResTrack_Dx12::hkOMSetRenderTargets(ID3D12GraphicsCommandList* This, UINT NumRenderTargetDescriptors,
                                         D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
                                         BOOL RTsSingleHandleToDescriptorRange,
                                         D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor)
{
    if (NumRenderTargetDescriptors == 0 || pRenderTargetDescriptors == nullptr || !IsHudFixActive() ||
        Hudfix_Dx12::SkipHudlessChecks())
    {
        o_OMSetRenderTargets(This, NumRenderTargetDescriptors, pRenderTargetDescriptors,
                             RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
        return;
    }

    LOG_DEBUG_ONLY("NumRenderTargetDescriptors: {}", NumRenderTargetDescriptors);

    if (This == MenuOverlayDx::MenuCommandList() /*|| IsFGCommandList(This)*/)
    {
        LOG_DEBUG_ONLY("Menu cmdlist: {} || fgCommandList: {}", This == MenuOverlayDx::MenuCommandList(),
                       IsFGCommandList(This));
        o_OMSetRenderTargets(This, NumRenderTargetDescriptors, pRenderTargetDescriptors,
                             RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
        return;
    }

    {
        for (size_t i = 0; i < NumRenderTargetDescriptors; i++)
        {
            HeapInfo* heap = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE handle {};

            if (RTsSingleHandleToDescriptorRange)
            {
                heap = GetHeapByCpuHandleRTV(pRenderTargetDescriptors[0].ptr);
                if (heap == nullptr)
                {
                    LOG_DEBUG_ONLY("No heap!");
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
                    LOG_DEBUG_ONLY("No heap!");
                    continue;
                }
            }

            auto capturedBuffer = heap->GetByCpuHandle(handle.ptr);
            if (capturedBuffer == nullptr || capturedBuffer->buffer == nullptr)
            {
                LOG_DEBUG_ONLY("Miss index: {0}, cpu: {1}", i, handle.ptr);
                continue;
            }

            capturedBuffer->state = D3D12_RESOURCE_STATE_RENDER_TARGET;

            if (Config::Instance()->FGImmediateCapture.value_or_default())
            {
                if (Hudfix_Dx12::CheckForHudless(__FUNCTION__, This, capturedBuffer, capturedBuffer->state))
                {
                    SetHudlessCmdList(This);
                    break;
                }
            }

            auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

            {
                // check for command list
                std::lock_guard<std::mutex> lock(hudlessMutex);

                if (!fgPossibleHudless[fIndex].contains(This))
                {
                    ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> newMap;
                    fgPossibleHudless[fIndex].insert_or_assign(This, newMap);
                }

                // add found resource
                LOG_TRACK("AddRef Resource: {:X}, Desc: {:X}", (size_t) capturedBuffer->buffer, handle.ptr);
                fgPossibleHudless[fIndex][This].insert_or_assign(capturedBuffer->buffer, *capturedBuffer);
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
    if (BaseDescriptor.ptr == 0 || !IsHudFixActive() || Hudfix_Dx12::SkipHudlessChecks())
    {
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    LOG_DEBUG_ONLY("");

    if (This == MenuOverlayDx::MenuCommandList() /*|| IsFGCommandList(This)*/)
    {
        LOG_DEBUG_ONLY("Menu cmdlist: {} || fgCommandList: {}", This == MenuOverlayDx::MenuCommandList(),
                       IsFGCommandList(This));
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    auto heap = GetHeapByGpuHandleCR(BaseDescriptor.ptr);
    if (heap == nullptr)
    {
        LOG_DEBUG_ONLY("No heap!");
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    auto capturedBuffer = heap->GetByGpuHandle(BaseDescriptor.ptr);
    if (capturedBuffer == nullptr || capturedBuffer->buffer == nullptr)
    {
        LOG_DEBUG_ONLY("Miss RootParameterIndex: {1}, CommandList: {0:X}, gpuHandle: {2}", (SIZE_T) This,
                       RootParameterIndex, BaseDescriptor.ptr);
        o_SetComputeRootDescriptorTable(This, RootParameterIndex, BaseDescriptor);
        return;
    }

    LOG_DEBUG_ONLY("CommandList: {:X}", (size_t) This);

    if (capturedBuffer->type == UAV)
        capturedBuffer->state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    else
        capturedBuffer->state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    do
    {
        if (Config::Instance()->FGImmediateCapture.value_or_default())
        {
            if (Hudfix_Dx12::CheckForHudless(__FUNCTION__, This, capturedBuffer, capturedBuffer->state))
            {
                SetHudlessCmdList(This);
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(hudlessMutex);

            auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

            if (!fgPossibleHudless[fIndex].contains(This))
            {
                ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> newMap;
                fgPossibleHudless[fIndex].insert_or_assign(This, newMap);
            }

            LOG_TRACK("AddRef Resource: {:X}, Desc: {:X}", (size_t) capturedBuffer->buffer, BaseDescriptor.ptr);
            fgPossibleHudless[fIndex][This].insert_or_assign(capturedBuffer->buffer, *capturedBuffer);
        }
    } while (false);

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
        return;

    auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

    if (This == MenuOverlayDx::MenuCommandList())
    {
        std::lock_guard<std::mutex> lock(hudlessMutex);
        fgPossibleHudless[fIndex][This].clear();
        return;
    }

    {
        ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> val0;
        {
            std::lock_guard<std::mutex> lock(hudlessMutex);

            // if can't find output skip
            if (fgPossibleHudless[fIndex].size() == 0 || !fgPossibleHudless[fIndex].contains(This))
            {
                LOG_DEBUG_ONLY("Early exit");
                return;
            }

            val0 = fgPossibleHudless[fIndex][This];

            do
            {
                // if this command list does not have entries skip
                if (val0.size() == 0)
                    break;

                for (auto& [key, val] : val0)
                {
                    std::lock_guard<std::mutex> lock(_drawMutex);

                    if (Hudfix_Dx12::CheckForHudless(__FUNCTION__, This, &val, val.state))
                    {
                        SetHudlessCmdList(This);
                        break;
                    }
                }
            } while (false);

            fgPossibleHudless[fIndex][This].clear();
        }

        LOG_DEBUG_ONLY("Clear");
    }
}

void ResTrack_Dx12::hkDrawIndexedInstanced(ID3D12GraphicsCommandList* This, UINT IndexCountPerInstance,
                                           UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation,
                                           UINT StartInstanceLocation)
{
    o_DrawIndexedInstanced(This, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation,
                           StartInstanceLocation);

    if (!IsHudFixActive())
        return;

    auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

    if (This == MenuOverlayDx::MenuCommandList())
    {
        std::lock_guard<std::mutex> lock(hudlessMutex);

        fgPossibleHudless[fIndex][This].clear();

        return;
    }

    {
        ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> val0;

        {
            std::lock_guard<std::mutex> lock(hudlessMutex);

            // if can't find output skip
            if (fgPossibleHudless[fIndex].size() == 0 || !fgPossibleHudless[fIndex].contains(This))
            {
                LOG_DEBUG_ONLY("Early exit");
                return;
            }

            val0 = fgPossibleHudless[fIndex][This];

            do
            {
                // if this command list does not have entries skip
                if (val0.size() == 0)
                    break;

                for (auto& [key, val] : val0)
                {
                    // LOG_DEBUG("Waiting _drawMutex {:X}", (size_t)val.buffer);
                    std::lock_guard<std::mutex> lock(_drawMutex);

                    if (Hudfix_Dx12::CheckForHudless(__FUNCTION__, This, &val, val.state))
                    {
                        SetHudlessCmdList(This);
                        break;
                    }
                }
            } while (false);

            fgPossibleHudless[fIndex][This].clear();
        }

        LOG_DEBUG_ONLY("Clear");
    }
}

void ResTrack_Dx12::hkExecuteBundle(ID3D12GraphicsCommandList* This, ID3D12GraphicsCommandList* pCommandList)
{
    IFGFeature_Dx12* fg = State::Instance().currentFG;

    if (fg->IsActive() && fg->TargetFrame() < fg->FrameCount())
    {
        auto index = fg->FrameCount() % BUFFER_COUNT;

        // LOG_TRACE("index: {}, bundle cmdList: {:X}, cmdList: {:X}", index, (size_t) This, (size_t) pCommandList);

        if (pCommandList == _hudlessCommandList[index])
        {
            LOG_DEBUG("Hudless cmdlist[{}]: {:X}", index, (size_t) This);
            _hudlessCommandList[index] = This;
        }
        else if (pCommandList == _inputsCommandList[index])
        {
            LOG_DEBUG("Upscaler cmdlist[{}]: {:X}", index, (size_t) This);
            _inputsCommandList[index] = This;
        }
    }

    o_ExecuteBundle(This, pCommandList);
}

void ResTrack_Dx12::hkClose(ID3D12GraphicsCommandList* This)
{
    auto fg = State::Instance().currentFG;
    auto index = fg != nullptr ? fg->GetIndex() : 0;

    if ((State::Instance().activeFgInput == FGInput::Upscaler || State::Instance().activeFgInput == FGInput::DLSSG) &&
        fg != nullptr && (_inputsCommandList[index] != nullptr || _hudlessCommandList[index] != nullptr))
    {

        if (fg->IsActive() && fg->TargetFrame() < fg->FrameCount())
        {
            if (!fg->HudlessReady())
            {
                if (This == _hudlessCommandList[index])
                {
                    LOG_DEBUG("Hudless CmdList: {:X}", (size_t) This);

                    fg->SetHudlessReady();

                    _hudlessCmdList = _hudlessCommandList[index];
                    _hudlessCommandList[index] = nullptr;
                }
            }

            if (!fg->UpscalerInputsReady())
            {
                if (This == _inputsCommandList[index])
                {
                    LOG_DEBUG("Upscaler CmdList: {:X}", (size_t) This);

                    fg->SetVelocityReady();
                    fg->SetDepthReady();

                    // Technically we don't check if UI wasn't ready but it should be fine
                    fg->SetUIReady();

                    _inputsCmdList = _inputsCommandList[index];
                    _inputsCommandList[index] = nullptr;
                }
            }
        }
    }

    o_Close(This);
}

void ResTrack_Dx12::hkDispatch(ID3D12GraphicsCommandList* This, UINT ThreadGroupCountX, UINT ThreadGroupCountY,
                               UINT ThreadGroupCountZ)
{
    o_Dispatch(This, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);

    if (!IsHudFixActive())
        return;

    auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

    if (This == MenuOverlayDx::MenuCommandList())
    {
        std::lock_guard<std::mutex> lock(hudlessMutex);
        fgPossibleHudless[fIndex][This].clear();
        return;
    }

    {
        ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo> val0;

        {
            std::lock_guard<std::mutex> lock(hudlessMutex);

            // if can't find output skip
            if (fgPossibleHudless[fIndex].size() == 0 || !fgPossibleHudless[fIndex].contains(This))
            {
                LOG_DEBUG_ONLY("Early exit");
                return;
            }

            val0 = fgPossibleHudless[fIndex][This];

            do
            {
                // if this command list does not have entries skip
                if (val0.size() == 0)
                    break;

                for (auto& [key, val] : val0)
                {
                    // LOG_DEBUG("Waiting _drawMutex {:X}", (size_t)val.buffer);
                    std::lock_guard<std::mutex> lock(_drawMutex);

                    if (Hudfix_Dx12::CheckForHudless(__FUNCTION__, This, &val, val.state))
                    {
                        SetHudlessCmdList(This);
                        break;
                    }
                }
            } while (false);

            fgPossibleHudless[fIndex][This].clear();
        }

        LOG_DEBUG_ONLY("Clear");
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
            DetourTransactionCommit();

            o_Release(tmp); // drop temp
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

                // if (o_ExecuteBundle != nullptr)
                //     DetourAttach(&(PVOID&) o_ExecuteBundle, hkExecuteBundle);

                if (o_Close != nullptr)
                    DetourAttach(&(PVOID&) o_Close, hkClose);

                DetourTransactionCommit();
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
        // Get the vtable pointer
        PVOID* pVTable = *(PVOID**) queue;

        o_ExecuteCommandLists = (PFN_ExecuteCommandLists) pVTable[10];

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_ExecuteCommandLists != nullptr)
            DetourAttach(&(PVOID&) o_ExecuteCommandLists, hkExecuteCommandLists);

        DetourTransactionCommit();

        queue->Release();
    }
}

void ResTrack_Dx12::HookDevice(ID3D12Device* device)
{
    if (o_CreateDescriptorHeap != nullptr || device == nullptr)
        return;

    LOG_FUNC();

    ID3D12Device* realDevice = nullptr;
    if (!CheckForRealObject(__FUNCTION__, device, (IUnknown**) &realDevice))
        realDevice = device;

    // Get the vtable pointer
    PVOID* pVTable = *(PVOID**) realDevice;

    // hudless
    o_CreateDescriptorHeap = (PFN_CreateDescriptorHeap) pVTable[14];
    o_CreateConstantBufferView = (PFN_CreateConstantBufferView) pVTable[17];
    o_CreateShaderResourceView = (PFN_CreateShaderResourceView) pVTable[18];
    o_CreateUnorderedAccessView = (PFN_CreateUnorderedAccessView) pVTable[19];
    o_CreateRenderTargetView = (PFN_CreateRenderTargetView) pVTable[20];
    o_CreateDepthStencilView = (PFN_CreateDepthStencilView) pVTable[21];
    o_CreateSampler = (PFN_CreateSampler) pVTable[22];
    o_CopyDescriptors = (PFN_CopyDescriptors) pVTable[23];
    o_CopyDescriptorsSimple = (PFN_CopyDescriptorsSimple) pVTable[24];

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

        DetourTransactionCommit();
    }

    HookToQueue(device);
    HookCommandList(device);
    HookResource(device);
}

void ResTrack_Dx12::ClearPossibleHudless()
{
    LOG_DEBUG("");

    std::lock_guard<std::mutex> lock(hudlessMutex);

    auto fIndex = Hudfix_Dx12::ActivePresentFrame() % BUFFER_COUNT;

    fgPossibleHudless[fIndex].clear();

    _hudlessCmdList = nullptr;
    _inputsCmdList = nullptr;
}

void ResTrack_Dx12::SetInputsCmdList(ID3D12GraphicsCommandList* cmdList)
{
    auto fg = State::Instance().currentFG;
    if (fg != nullptr && fg->IsActive())
    {
        auto index = fg->FrameCount() % BUFFER_COUNT;
        LOG_DEBUG("cmdList[{}]: {:X}", index, (size_t) cmdList);
        _inputsCommandList[index] = cmdList;
    }
}

void ResTrack_Dx12::SetHudlessCmdList(ID3D12GraphicsCommandList* cmdList)
{
    auto fg = State::Instance().currentFG;
    if (fg != nullptr && fg->IsActive())
    {
        auto index = fg->FrameCount() % BUFFER_COUNT;
        LOG_DEBUG("cmdList[{}]: {:X}", index, (size_t) cmdList);
        _hudlessCommandList[index] = cmdList;
    }
}
