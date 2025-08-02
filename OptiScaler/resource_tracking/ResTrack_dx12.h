#pragma once

#include <pch.h>

#include <hudfix/Hudfix_Dx12.h>

#include <ankerl/unordered_dense.h>

// Test resources if they are valid or not
// #define DEBUG_TRACKING

#ifdef DEBUG_TRACKING
#define LOG_TRACK(msg, ...) spdlog::debug(__FUNCTION__ " " msg, ##__VA_ARGS__)
#else
#define LOG_TRACK(msg, ...)
#endif

#ifdef DEBUG_TRACKING
static void TestResource(ResourceInfo* info)
{
    if (info == nullptr || info->buffer == nullptr)
        return;

    auto desc = info->buffer->GetDesc();

    if (desc.Width != info->width || desc.Height != info->height || desc.Format != info->format)
    {
        LOG_WARN("Resource mismatch: {:X}, info: {:X}", (size_t) info->buffer, (size_t) info);
        __debugbreak();
    }
}
#endif

static ankerl::unordered_dense::map<ID3D12Resource*, std::vector<ResourceInfo*>> _trackedResources;
static std::mutex _trMutex;
static std::shared_mutex _heapMutex[1000];

typedef struct HeapInfo
{
    ID3D12DescriptorHeap* heap = nullptr;
    SIZE_T cpuStart = NULL;
    SIZE_T cpuEnd = NULL;
    SIZE_T gpuStart = NULL;
    SIZE_T gpuEnd = NULL;
    UINT numDescriptors = 0;
    UINT increment = 0;
    UINT type = 0;
    std::shared_ptr<ResourceInfo[]> info;
    UINT lastOffset = 0;
    UINT mutexIndex = 0;

    HeapInfo(ID3D12DescriptorHeap* heap, SIZE_T cpuStart, SIZE_T cpuEnd, SIZE_T gpuStart, SIZE_T gpuEnd,
             UINT numResources, UINT increment, UINT type, UINT mutexIndex)
        : cpuStart(cpuStart), cpuEnd(cpuEnd), gpuStart(gpuStart), gpuEnd(gpuEnd), numDescriptors(numResources),
          increment(increment), info(new ResourceInfo[numResources]), type(type), heap(heap), mutexIndex(mutexIndex)
    {
        for (size_t i = 0; i < numDescriptors; i++)
        {
            info[i].buffer = nullptr;
        }
    }

    ResourceInfo* GetByCpuHandle(SIZE_T cpuHandle) const
    {
        auto index = (cpuHandle - cpuStart) / increment;

        if (index >= numDescriptors)
            return nullptr;

        {
            if (info[index].buffer == nullptr)
                return nullptr;
        }

#ifdef DEBUG_TRACKING
        TestResource(&info[index]);
#endif

        return &info[index];
    }

    ResourceInfo* GetByGpuHandle(SIZE_T gpuHandle) const
    {
        auto index = (gpuHandle - gpuStart) / increment;

        if (index >= numDescriptors)
            return nullptr;

        {
            if (info[index].buffer == nullptr)
                return nullptr;
        }

#ifdef DEBUG_TRACKING
        TestResource(&info[index]);
#endif

        return &info[index];
    }

    void SetByCpuHandle(SIZE_T cpuHandle, ResourceInfo setInfo) const
    {
        auto index = (cpuHandle - cpuStart) / increment;

        if (index >= numDescriptors)
            return;

#ifdef DEBUG_TRACKING
        TestResource(&setInfo);
#endif

        info[index] = setInfo;

        {
            _trMutex.lock();

            if (_trackedResources.contains(setInfo.buffer))
                _trackedResources[setInfo.buffer].push_back(&info[index]);
            else
                _trackedResources[setInfo.buffer] = { &info[index] };

            LOG_TRACK("Add resource: {:X} to info: {:X}, Res: {}x{}", (size_t) setInfo.buffer, (size_t) &info[index],
                      setInfo.width, setInfo.height);

            _trMutex.unlock();
        }
    }

    void SetByGpuHandle(SIZE_T gpuHandle, ResourceInfo setInfo) const
    {
        auto index = (gpuHandle - gpuStart) / increment;

        if (index >= numDescriptors)
            return;

#ifdef DEBUG_TRACKING
        TestResource(&setInfo);
#endif

        info[index] = setInfo;

        {
            _trMutex.lock();

            if (_trackedResources.contains(setInfo.buffer))
                _trackedResources[setInfo.buffer].push_back(&info[index]);
            else
                _trackedResources[setInfo.buffer] = { &info[index] };

            LOG_TRACK("Add resource: {:X} to info: {:X}, Res: {}x{}", (size_t) setInfo.buffer, (size_t) &info[index],
                      setInfo.width, setInfo.height);

            _trMutex.unlock();
        }
    }

    void ClearByCpuHandle(SIZE_T cpuHandle) const
    {
        auto index = (cpuHandle - cpuStart) / increment;

        if (index >= numDescriptors)
            return;

        _trMutex.lock();

        if (info[index].buffer != nullptr)
        {
            LOG_TRACK("Resource: {:X}, Res: {}x{}", (size_t) info[index].buffer, info[index].width, info[index].height);

            if (_trackedResources.contains(info[index].buffer))
            {
                auto vector = &_trackedResources[info[index].buffer];

                for (size_t i = 0; i < vector->size(); i++)
                {
                    if (vector->at(i) == &info[index])
                    {
                        LOG_TRACK("Erase from _trackedResources info: {:X}", (size_t) vector->at(i));
                        vector->erase(vector->begin() + i);
                        break;
                    }
                }
            }
        }

        _trMutex.unlock();

        info[index].buffer = nullptr;
        info[index].lastUsedFrame = 0;
    }

    void ClearByGpuHandle(SIZE_T gpuHandle) const
    {
        auto index = (gpuHandle - gpuStart) / increment;

        if (index >= numDescriptors)
            return;

        _trMutex.lock();

        if (info[index].buffer != nullptr)
        {
            LOG_TRACK("Resource: {:X}, Res: {}x{}", (size_t) info[index].buffer, info[index].width, info[index].height);

            if (_trackedResources.contains(info[index].buffer))
            {
                auto vector = &_trackedResources[info[index].buffer];

                for (size_t i = 0; i < vector->size(); i++)
                {
                    if (vector->at(i) == &info[index])
                    {
                        LOG_TRACK("Erase from _trackedResources info: {:X}", (size_t) vector->at(i));
                        vector->erase(vector->begin() + i);
                        break;
                    }
                }
            }
        }

        _trMutex.unlock();

        info[index].buffer = nullptr;
        info[index].lastUsedFrame = 0;
    }
};

typedef struct ResourceHeapInfo
{
    SIZE_T cpuStart = NULL;
    SIZE_T gpuStart = NULL;
};

class ResTrack_Dx12
{
  private:
    inline static bool _presentDone = true;
    inline static std::mutex _drawMutex;

    inline static ID3D12GraphicsCommandList* _hudlessCommandList[BUFFER_COUNT] = { nullptr, nullptr, nullptr, nullptr };
    inline static ID3D12GraphicsCommandList* _inputsCommandList[BUFFER_COUNT] = { nullptr, nullptr, nullptr, nullptr };

    inline static ULONG64 _lastHudlessFrame = 0;
    inline static std::mutex _hudlessMutex;
    inline static void* _hudlessMutexQueue = nullptr;

    static bool IsHudFixActive();

    // static bool IsFGCommandList(IUnknown* cmdList);

    static void hkCopyDescriptors(ID3D12Device* This, UINT NumDestDescriptorRanges,
                                  D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts,
                                  UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges,
                                  D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts,
                                  UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
    static void hkCopyDescriptorsSimple(ID3D12Device* This, UINT NumDescriptors,
                                        D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
                                        D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
                                        D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);

    static void hkSetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* This, UINT RootParameterIndex,
                                                 D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
    static void hkOMSetRenderTargets(ID3D12GraphicsCommandList* This, UINT NumRenderTargetDescriptors,
                                     D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
                                     BOOL RTsSingleHandleToDescriptorRange,
                                     D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor);
    static void hkSetComputeRootDescriptorTable(ID3D12GraphicsCommandList* This, UINT RootParameterIndex,
                                                D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);

    static void hkDrawInstanced(ID3D12GraphicsCommandList* This, UINT VertexCountPerInstance, UINT InstanceCount,
                                UINT StartVertexLocation, UINT StartInstanceLocation);
    static void hkDrawIndexedInstanced(ID3D12GraphicsCommandList* This, UINT IndexCountPerInstance, UINT InstanceCount,
                                       UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);
    static void hkDispatch(ID3D12GraphicsCommandList* This, UINT ThreadGroupCountX, UINT ThreadGroupCountY,
                           UINT ThreadGroupCountZ);

    static void hkExecuteBundle(ID3D12GraphicsCommandList* This, ID3D12GraphicsCommandList* pCommandList);

    static void hkClose(ID3D12GraphicsCommandList* This);

    static void hkCreateRenderTargetView(ID3D12Device* This, ID3D12Resource* pResource,
                                         D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
                                         D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
    static void hkCreateShaderResourceView(ID3D12Device* This, ID3D12Resource* pResource,
                                           D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
                                           D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
    static void hkCreateUnorderedAccessView(ID3D12Device* This, ID3D12Resource* pResource,
                                            ID3D12Resource* pCounterResource, D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc,
                                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

    static void hkExecuteCommandLists(ID3D12CommandQueue* This, UINT NumCommandLists,
                                      ID3D12CommandList* const* ppCommandLists);

    static HRESULT hkCreateDescriptorHeap(ID3D12Device* This, D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc,
                                          REFIID riid, void** ppvHeap);

    static ULONG hkRelease(ID3D12Resource* This);

    static void HookCommandList(ID3D12Device* InDevice);
    static void HookToQueue(ID3D12Device* InDevice);
    static void HookResource(ID3D12Device* InDevice);

    static bool CheckResource(ID3D12Resource* resource);

    static bool CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);

    static bool CreateBufferResource(ID3D12Device* InDevice, ResourceInfo* InSource, D3D12_RESOURCE_STATES InState,
                                     ID3D12Resource** OutResource);

    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

    static SIZE_T GetGPUHandle(ID3D12Device* This, SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type);
    static SIZE_T GetCPUHandle(ID3D12Device* This, SIZE_T gpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type);

    static HeapInfo* GetHeapByCpuHandleCBV(SIZE_T cpuHandle);
    static HeapInfo* GetHeapByCpuHandleRTV(SIZE_T cpuHandle);
    static HeapInfo* GetHeapByCpuHandleSRV(SIZE_T cpuHandle);
    static HeapInfo* GetHeapByCpuHandleUAV(SIZE_T cpuHandle);
    static HeapInfo* GetHeapByCpuHandle(SIZE_T cpuHandle);
    static HeapInfo* GetHeapByGpuHandleGR(SIZE_T gpuHandle);
    static HeapInfo* GetHeapByGpuHandleCR(SIZE_T gpuHandle);

    static void FillResourceInfo(ID3D12Resource* resource, ResourceInfo* info);

  public:
    static void HookDevice(ID3D12Device* device);
    static void ClearPossibleHudless();
    static void SetInputsCmdList(ID3D12GraphicsCommandList* cmdList);
    static void SetHudlessCmdList(ID3D12GraphicsCommandList* cmdList);
};
