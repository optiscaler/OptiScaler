#pragma once

#include "SysUtils.h"

#include <hudfix/Hudfix_Dx12.h>
#include <framegen/IFGFeature_Dx12.h>

#include <ankerl/unordered_dense.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <vector>

// #define DEBUG_TRACKING

#ifdef DEBUG_TRACKING
static void TestResource(const ResourceInfo* info)
{
    if (info == nullptr || info->buffer == nullptr)
        return;

    auto desc = info->buffer->GetDesc();

    if (desc.Width != info->width || desc.Height != info->height || desc.Format != info->format)
    {
        LOG_TRACK("Resource mismatch: {:X}, info: {:X}", (size_t) info->buffer, (size_t) info);

        // LOG_WARN("Resource mismatch: {:X}, info: {:X}", (size_t) info->buffer, (size_t) info);
        //__debugbreak();
    }
}
#endif

#define USE_SPINLOCK_MUTEX

#ifdef USE_SPINLOCK_MUTEX

// #define USE_PERF_SPINLOCK

#ifdef __cpp_lib_hardware_interference_size
constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
constexpr size_t CACHE_LINE_SIZE = 64;
#endif
#else
#ifdef __cpp_lib_hardware_interference_size
constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size * 2;
#else
constexpr size_t CACHE_LINE_SIZE = 128;
#endif
#endif

#ifdef USE_SPINLOCK_MUTEX
#ifdef USE_PERF_SPINLOCK
class SpinLock
{
    std::atomic<bool> _lock = { false };

  public:
    void lock()
    {
        int backoff = 1;

        while (true)
        {
            // 1. Optimistic Read (TTAS)
            // Using 'relaxed' because we don't need ordering until we actually acquire.
            if (!_lock.load(std::memory_order_relaxed))
            {

                // 2. Attempt Acquire
                // 'acquire' ensures no memory ops move before this lock
                if (!_lock.exchange(true, std::memory_order_acquire))
                {
                    return; // Success
                }
            }

            // 3. Pause instruction to help HT and branch prediction
            _mm_pause();
        }
    }

    void unlock()
    {
        // 'release' ensures all memory ops are finished before unlocking
        _lock.store(false, std::memory_order_release);
    }
};
#else
struct SpinLock
{
    std::atomic<bool> _lock = { false };

    __forceinline void lock()
    {
        // Fast path: try to grab immediately
        if (!_lock.exchange(true, std::memory_order_acquire))
            return;

        int backoff = 1;
        while (true)
        {
            while (_lock.load(std::memory_order_relaxed))
            {
                for (int i = 0; i < backoff; ++i)
                    _mm_pause();

                backoff = std::min(backoff * 2, 64);
            }

            if (!_lock.exchange(true, std::memory_order_acquire))
                return;
        }
    }

    __forceinline void unlock() { _lock.store(false, std::memory_order_release); }
};
#endif
#endif

struct HeapInfo;

struct TrackedResourceSlot
{
    std::weak_ptr<HeapInfo> heap;
    uint64_t heapVersion = 0;
    UINT index = 0;
};

inline ankerl::unordered_dense::map<ID3D12Resource*, std::vector<TrackedResourceSlot>> _trackedResources;
inline std::mutex _trackedResourcesMutex;
inline std::mutex _resourceLifetimeMutex;

// Smaller info for descriptor tracking
struct DescriptorResourceInfo
{
    ID3D12Resource* buffer = nullptr;
    UINT64 width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    ResourceType type = SRV;

    void Store(const ResourceInfo& source) noexcept
    {
        buffer = source.buffer;
        width = source.width;
        height = source.height;
        format = source.format;
        flags = source.flags;
        type = source.type;
    }

    void Load(ResourceInfo& target) const noexcept
    {
        target = {};
        target.buffer = buffer;
        target.width = width;
        target.height = height;
        target.format = format;
        target.flags = flags;
        target.type = type;
        target.lifetimeTracked = true;
    }
};

struct HeapInfo : public std::enable_shared_from_this<HeapInfo>
{
    // Avoiding one lock object per descriptor
    static constexpr size_t DESCRIPTOR_LOCK_STRIPE_COUNT = 256;
    static_assert((DESCRIPTOR_LOCK_STRIPE_COUNT & (DESCRIPTOR_LOCK_STRIPE_COUNT - 1)) == 0);

    mutable std::array<std::shared_mutex, DESCRIPTOR_LOCK_STRIPE_COUNT> descriptorLocks;

    ID3D12DescriptorHeap* heap = nullptr;
    SIZE_T cpuStart = 0;
    SIZE_T cpuEnd = 0;
    SIZE_T gpuStart = 0;
    SIZE_T gpuEnd = 0;
    UINT numDescriptors = 0;
    UINT increment = 0;
    UINT type = 0;
    std::shared_ptr<DescriptorResourceInfo[]> info;
    UINT lastOffset = 0;
    std::atomic<bool> active { true };
    std::atomic<uint64_t> version { 0 };

    HeapInfo(ID3D12DescriptorHeap* heap, SIZE_T cpuStart, SIZE_T cpuEnd, SIZE_T gpuStart, SIZE_T gpuEnd,
             UINT numResources, UINT increment, UINT type)
        : heap(heap), cpuStart(cpuStart), cpuEnd(cpuEnd), gpuStart(gpuStart), gpuEnd(gpuEnd),
          numDescriptors(numResources), increment(increment), type(type), info(new DescriptorResourceInfo[numResources])
    {
        static std::atomic<uint64_t> globalHeapVersion { 1 };
        version.store(globalHeapVersion.fetch_add(1, std::memory_order_relaxed), std::memory_order_relaxed);
    }

    bool GetCpuIndex(SIZE_T cpuHandle, UINT& index) const
    {
        if (increment == 0 || cpuHandle < cpuStart || cpuHandle >= cpuEnd)
            return false;

        auto calculated = (cpuHandle - cpuStart) / increment;
        if (calculated >= numDescriptors)
            return false;

        index = static_cast<UINT>(calculated);
        return true;
    }

    bool GetGpuIndex(SIZE_T gpuHandle, UINT& index) const
    {
        if (increment == 0 || gpuHandle < gpuStart || gpuHandle >= gpuEnd)
            return false;

        auto calculated = (gpuHandle - gpuStart) / increment;
        if (calculated >= numDescriptors)
            return false;

        index = static_cast<UINT>(calculated);
        return true;
    }

    bool GetByIndex(UINT index, ResourceInfo& outInfo) const
    {
        if (!active.load(std::memory_order_acquire) || index >= numDescriptors)
            return false;

        std::shared_lock lock(GetDescriptorLock(index));
        if (!active.load(std::memory_order_acquire) || info[index].buffer == nullptr)
            return false;

        info[index].Load(outInfo);

#ifdef DEBUG_TRACKING
        TestResource(&outInfo);
#endif

        return true;
    }

    void SetByIndex(UINT index, const ResourceInfo& setInfo)
    {
        if (!active.load(std::memory_order_acquire) || index >= numDescriptors)
            return;

        std::unique_lock lock(GetDescriptorLock(index));
        if (!active.load(std::memory_order_acquire))
            return;

#ifdef DEBUG_TRACKING
        TestResource(&setInfo);
#endif

        if (info[index].buffer != setInfo.buffer)
        {
            DetachFromOldResourceLocked(index);
            info[index].Store(setInfo);
            AttachToNewResourceLocked(index);
        }
        else
        {
            info[index].Store(setInfo);
        }
    }

    void ClearByIndex(UINT index)
    {
        if (!active.load(std::memory_order_acquire) || index >= numDescriptors)
            return;

        std::unique_lock lock(GetDescriptorLock(index));
        if (!active.load(std::memory_order_acquire))
            return;

        ClearSlotLocked(index, true);
    }

    // Caller must hold the descriptor stripe exclusively.
    void DetachFromOldResourceLocked(UINT index)
    {
        auto* oldResource = info[index].buffer;
        if (oldResource == nullptr)
            return;

        std::scoped_lock lock(_trackedResourcesMutex);
        LOG_TRACK("Heap: {:X}, Index: {}, Resource: {:X}, Res: {}x{}, Format: {}", (size_t) this, index,
                  (size_t) oldResource, info[index].width, info[index].height, (UINT) info[index].format);

        auto it = _trackedResources.find(oldResource);
        if (it == _trackedResources.end())
            return;

        const auto currentVersion = version.load(std::memory_order_relaxed);
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(), [currentVersion, index](const TrackedResourceSlot& slot)
                                 { return slot.heapVersion == currentVersion && slot.index == index; }),
                  vec.end());
    }

    void AttachToNewResourceLocked(UINT index)
    {
        auto* newResource = info[index].buffer;
        if (newResource == nullptr)
            return;

        std::scoped_lock lock(_trackedResourcesMutex);
        LOG_TRACK("Heap: {:X}, Index: {}, Resource: {:X}, Res: {}x{}, Format: {}", (size_t) this, index,
                  (size_t) newResource, info[index].width, info[index].height, (UINT) info[index].format);

        const auto currentVersion = version.load(std::memory_order_relaxed);
        auto it = _trackedResources.find(newResource);
        if (it == _trackedResources.end())
        {
            info[index].buffer = nullptr;
            return;
        }

        auto& vec = it->second;
        auto found = std::find_if(vec.begin(), vec.end(), [currentVersion, index](const TrackedResourceSlot& slot)
                                  { return slot.heapVersion == currentVersion && slot.index == index; });

        if (found == vec.end())
            vec.push_back({ shared_from_this(), currentVersion, index });
    }

    bool GetByCpuHandle(SIZE_T cpuHandle, ResourceInfo& outInfo) const
    {
        if (!active.load(std::memory_order_acquire))
            return false;

        UINT index = 0;
        if (!GetCpuIndex(cpuHandle, index))
            return false;

        std::shared_lock lock(GetDescriptorLock(index));
        if (!active.load(std::memory_order_acquire) || info[index].buffer == nullptr)
            return false;

        info[index].Load(outInfo);

#ifdef DEBUG_TRACKING
        TestResource(&outInfo);
#endif

        return true;
    }

    bool GetByGpuHandle(SIZE_T gpuHandle, ResourceInfo& outInfo) const
    {
        if (!active.load(std::memory_order_acquire))
            return false;

        UINT index = 0;
        if (!GetGpuIndex(gpuHandle, index))
            return false;

        std::shared_lock lock(GetDescriptorLock(index));
        if (!active.load(std::memory_order_acquire) || info[index].buffer == nullptr)
            return false;

        info[index].Load(outInfo);

#ifdef DEBUG_TRACKING
        TestResource(&outInfo);
#endif

        return true;
    }

    void SetByCpuHandle(SIZE_T cpuHandle, const ResourceInfo& setInfo)
    {
        if (!active.load(std::memory_order_acquire))
            return;

        UINT index = 0;
        if (!GetCpuIndex(cpuHandle, index))
            return;

        std::unique_lock lock(GetDescriptorLock(index));
        if (!active.load(std::memory_order_acquire))
            return;

#ifdef DEBUG_TRACKING
        TestResource(&setInfo);
#endif

        if (info[index].buffer != setInfo.buffer)
        {
            DetachFromOldResourceLocked(index);
            info[index].Store(setInfo);
            AttachToNewResourceLocked(index);
        }
        else
        {
            info[index].Store(setInfo);
        }
    }

    void SetByGpuHandle(SIZE_T gpuHandle, const ResourceInfo& setInfo)
    {
        if (!active.load(std::memory_order_acquire))
            return;

        UINT index = 0;
        if (!GetGpuIndex(gpuHandle, index))
            return;

        std::unique_lock lock(GetDescriptorLock(index));
        if (!active.load(std::memory_order_acquire))
            return;

#ifdef DEBUG_TRACKING
        TestResource(&setInfo);
#endif

        if (info[index].buffer != setInfo.buffer)
        {
            DetachFromOldResourceLocked(index);
            info[index].Store(setInfo);
            AttachToNewResourceLocked(index);
        }
        else
        {
            info[index].Store(setInfo);
        }
    }

    void ClearByCpuHandle(SIZE_T cpuHandle)
    {
        if (!active.load(std::memory_order_acquire))
            return;

        UINT index = 0;
        if (!GetCpuIndex(cpuHandle, index))
            return;

        std::unique_lock lock(GetDescriptorLock(index));
        if (!active.load(std::memory_order_acquire))
            return;

        ClearSlotLocked(index, true);
    }

    void ClearByGpuHandle(SIZE_T gpuHandle)
    {
        if (!active.load(std::memory_order_acquire))
            return;

        UINT index = 0;
        if (!GetGpuIndex(gpuHandle, index))
            return;

        std::unique_lock lock(GetDescriptorLock(index));
        if (!active.load(std::memory_order_acquire))
            return;

        ClearSlotLocked(index, true);
    }

    void ClearSlotIfMatches(UINT index, ID3D12Resource* resource)
    {
        if (!active.load(std::memory_order_acquire) || index >= numDescriptors)
            return;

        std::unique_lock lock(GetDescriptorLock(index));
        if (!active.load(std::memory_order_acquire) || info[index].buffer != resource)
            return;

        info[index].buffer = nullptr;
    }

    bool DeactivateAndClear()
    {
        if (!active.exchange(false, std::memory_order_acq_rel))
            return false;

        std::array<std::unique_lock<std::shared_mutex>, DESCRIPTOR_LOCK_STRIPE_COUNT> locks;
        for (size_t i = 0; i < DESCRIPTOR_LOCK_STRIPE_COUNT; ++i)
            locks[i] = std::unique_lock<std::shared_mutex>(descriptorLocks[i]);

        std::scoped_lock trackedLock(_trackedResourcesMutex);
        for (UINT index = 0; index < numDescriptors; ++index)
        {
            auto* resource = info[index].buffer;
            if (resource == nullptr)
                continue;

            if (auto it = _trackedResources.find(resource); it != _trackedResources.end())
            {
                const auto currentVersion = version.load(std::memory_order_relaxed);
                auto& vec = it->second;
                vec.erase(std::remove_if(vec.begin(), vec.end(),
                                         [currentVersion, index](const TrackedResourceSlot& slot)
                                         { return slot.heapVersion == currentVersion && slot.index == index; }),
                          vec.end());
            }

            info[index].buffer = nullptr;
        }

        info.reset();

        return true;
    }

  private:
    std::shared_mutex& GetDescriptorLock(UINT index) const
    {
        return descriptorLocks[index & (DESCRIPTOR_LOCK_STRIPE_COUNT - 1)];
    }

    void ClearSlotLocked(UINT index, bool detach)
    {
        if (info[index].buffer != nullptr)
        {
            LOG_TRACK("Resource: {:X}, Res: {}x{}, Format: {}", (size_t) info[index].buffer, info[index].width,
                      info[index].height, (UINT) info[index].format);

            if (detach)
                DetachFromOldResourceLocked(index);
        }

        info[index].buffer = nullptr;
    }
};

struct ResourceHeapInfo
{
    SIZE_T cpuStart = NULL;
    SIZE_T gpuStart = NULL;
};

// Command list info
struct RootSignatureInfo;

struct CommandListBindingState
{
    static constexpr size_t MAX_ROOT_PARAMETERS = 64;

    std::array<SIZE_T, MAX_ROOT_PARAMETERS> graphicsTables {};
    std::array<SIZE_T, MAX_ROOT_PARAMETERS> computeTables {};
    uint64_t graphicsTableMask = 0;
    uint64_t computeTableMask = 0;

    std::array<SIZE_T, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets {};
    UINT renderTargetCount = 0;
    bool renderTargetsContiguous = false;

    ID3D12RootSignature* graphicsRootSignature = nullptr;
    ID3D12RootSignature* computeRootSignature = nullptr;
    std::shared_ptr<RootSignatureInfo> graphicsRootSignatureInfo;
    std::shared_ptr<RootSignatureInfo> computeRootSignatureInfo;
    ID3D12DescriptorHeap* cbvSrvUavHeap = nullptr;
    std::shared_ptr<HeapInfo> cbvSrvUavHeapInfo;
};

struct RootDescriptorRangeInfo
{
    UINT offset = 0;
    UINT count = 0;
    D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
};

struct RootParameterTableInfo
{
    UINT firstRange = 0;
    UINT rangeCount = 0;
    D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
};

struct RootSignatureInfo
{
    std::array<RootParameterTableInfo, CommandListBindingState::MAX_ROOT_PARAMETERS> parameters {};
    std::vector<RootDescriptorRangeInfo> ranges;
    bool pixelShaderRootAccess = true;
};

#ifdef USE_SPINLOCK_MUTEX
using BindingStateMutex = SpinLock;
#else
using BindingStateMutex = std::mutex;
#endif

struct alignas(CACHE_LINE_SIZE) BindingStateShard
{
    BindingStateMutex mutex;
    ankerl::unordered_dense::map<ID3D12GraphicsCommandList*, std::unique_ptr<CommandListBindingState>> map;
};

#ifdef USE_SPINLOCK_MUTEX
// Force each struct to start on a new cache line
struct alignas(CACHE_LINE_SIZE) CommandListShard
{
    SpinLock mutex;
    ankerl::unordered_dense::map<ID3D12GraphicsCommandList*,
                                 ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo>>
        map;

    char padding[CACHE_LINE_SIZE - ((sizeof(SpinLock) + sizeof(void*)) % CACHE_LINE_SIZE)] = {};
};
#else
struct alignas(CACHE_LINE_SIZE) CommandListShard
{
    std::mutex mutex;
    ankerl::unordered_dense::map<ID3D12GraphicsCommandList*,
                                 ankerl::unordered_dense::map<ID3D12Resource*, ResourceInfo>>
        map;

    char padding[CACHE_LINE_SIZE - ((sizeof(std::mutex) + sizeof(void*)) % CACHE_LINE_SIZE)] = {};
};
#endif

class ResTrack_Dx12
{
  private:
    inline static bool _presentDone = true;
    inline static bool _useShards = false;
    inline static std::atomic<bool> _bindingTrackingEnabled { false };

    inline static std::mutex _bindingStateMutex;
    inline static ankerl::unordered_dense::map<ID3D12GraphicsCommandList*, std::unique_ptr<CommandListBindingState>>
        _bindingStates;

    inline static std::mutex _rootSignatureInfoMutex;
    inline static ankerl::unordered_dense::map<ID3D12RootSignature*, std::shared_ptr<RootSignatureInfo>>
        _rootSignatureInfos;

    inline static ULONG64 _lastHudlessFrame = 0;
    inline static std::mutex _hudlessMutex;
    inline static void* _hudlessMutexQueue = nullptr;

    static bool IsHudFixActive();

    static CommandListBindingState* GetOrCreateBindingState(ID3D12GraphicsCommandList* commandList);
    static CommandListBindingState* FindBindingState(ID3D12GraphicsCommandList* commandList);
    static void ResetBindingState(ID3D12GraphicsCommandList* commandList);
    static void RemoveBindingState(ID3D12GraphicsCommandList* commandList);
    static void ClearBindingStates();
    static void __stdcall CommandListDestroyed(void* data);
    static std::shared_ptr<RootSignatureInfo> FindRootSignatureInfo(ID3D12RootSignature* rootSignature);
    static void __stdcall RootSignatureDestroyed(void* data);

    static bool ResolveGraphicsBinding(const HeapInfo* boundHeap, SIZE_T gpuHandle, ResourceInfo& outInfo);
    static bool ResolveComputeBinding(const HeapInfo* boundHeap, SIZE_T gpuHandle, ResourceInfo& outInfo);
    static bool ResolveRenderTargetBinding(SIZE_T cpuHandle, ResourceInfo& outInfo);
    static bool ProcessDescriptorTableBinding(ID3D12GraphicsCommandList* commandList, const RootSignatureInfo* rootInfo,
                                              UINT rootParameterIndex, const HeapInfo* boundHeap, SIZE_T baseHandle,
                                              UINT captureInfo, bool graphics);
    static bool ProcessGraphicsBindings(ID3D12GraphicsCommandList* commandList, UINT captureInfo);
    static bool ProcessComputeBindings(ID3D12GraphicsCommandList* commandList, UINT captureInfo);

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
    static HRESULT hkReset(ID3D12GraphicsCommandList* This, ID3D12CommandAllocator* pAllocator,
                           ID3D12PipelineState* pInitialState);
    static void hkClearState(ID3D12GraphicsCommandList* This, ID3D12PipelineState* pPipelineState);

    static void hkDrawInstanced(ID3D12GraphicsCommandList* This, UINT VertexCountPerInstance, UINT InstanceCount,
                                UINT StartVertexLocation, UINT StartInstanceLocation);
    static void hkDrawIndexedInstanced(ID3D12GraphicsCommandList* This, UINT IndexCountPerInstance, UINT InstanceCount,
                                       UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);
    static void hkDispatch(ID3D12GraphicsCommandList* This, UINT ThreadGroupCountX, UINT ThreadGroupCountY,
                           UINT ThreadGroupCountZ);

    static void hkCreateRenderTargetView(ID3D12Device* This, ID3D12Resource* pResource,
                                         D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
                                         D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
    static void hkCreateShaderResourceView(ID3D12Device* This, ID3D12Resource* pResource,
                                           D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
                                           D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
    static void hkCreateUnorderedAccessView(ID3D12Device* This, ID3D12Resource* pResource,
                                            ID3D12Resource* pCounterResource, D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc,
                                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

    static HRESULT hkCreateDescriptorHeap(ID3D12Device* This, D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc,
                                          REFIID riid, void** ppvHeap);

    static void __stdcall ResourceDestroyed(void* data);

    static void HookCommandList(ID3D12Device* InDevice);

    static bool CheckResource(ID3D12Resource* resource, ResourceInfo* outInfo = nullptr);

    static bool CheckForRealObject(const std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);

    static bool CreateBufferResource(ID3D12Device* InDevice, ResourceInfo* InSource, D3D12_RESOURCE_STATES InState,
                                     ID3D12Resource** OutResource);

    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

    static SIZE_T GetGPUHandle(ID3D12Device* This, SIZE_T cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type);
    static SIZE_T GetCPUHandle(ID3D12Device* This, SIZE_T gpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE type);

    static std::shared_ptr<HeapInfo> GetHeapByCpuHandleCBV(SIZE_T cpuHandle);
    static std::shared_ptr<HeapInfo> GetHeapByCpuHandleRTV(SIZE_T cpuHandle);
    static std::shared_ptr<HeapInfo> GetHeapByCpuHandleSRV(SIZE_T cpuHandle);
    static std::shared_ptr<HeapInfo> GetHeapByCpuHandleUAV(SIZE_T cpuHandle);
    static std::shared_ptr<HeapInfo> GetHeapByCpuHandle(SIZE_T cpuHandle);
    static std::shared_ptr<HeapInfo> GetHeapByGpuHandleGR(SIZE_T gpuHandle);
    static std::shared_ptr<HeapInfo> GetHeapByGpuHandleCR(SIZE_T gpuHandle);

    // Sharding
    inline static constexpr size_t SHARD_COUNT = 16;
    inline static CommandListShard _hudlessShards[BUFFER_COUNT][SHARD_COUNT];
    inline static BindingStateShard _bindingShards[SHARD_COUNT];

    inline static size_t GetShardIndex(ID3D12GraphicsCommandList* ptr)
    {
        auto addr = (UINT64) ptr;
        return (addr >> 4) % SHARD_COUNT;
    }

  public:
    static void RegisterRootSignature(ID3D12RootSignature* rootSignature,
                                      const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc);
    static void OnSetDescriptorHeaps(ID3D12GraphicsCommandList* commandList, UINT numDescriptorHeaps,
                                     ID3D12DescriptorHeap* const* descriptorHeaps);
    static void OnSetGraphicsRootSignature(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* rootSignature);
    static void OnSetComputeRootSignature(ID3D12GraphicsCommandList* commandList, ID3D12RootSignature* rootSignature);

    static void HookDevice(ID3D12Device* device);
    static void ReleaseHooks();
    static void ReleaseDeviceHooks();
    static void ClearPossibleHudless();
    static bool TrackResourceRelease(ID3D12Resource* resource);
};
