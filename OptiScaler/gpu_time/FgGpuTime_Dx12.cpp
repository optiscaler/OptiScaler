#include "pch.h"
#include "FgGpuTime_Dx12.h"

#include <gpu_time/FgTimingHookThreads.h>
#include <Util.h>
#include <detours/detours.h>

#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace
{
using Microsoft::WRL::ComPtr;

// Private COM data retires with the actual object, so recycled pointer values
// cannot inherit a previous command recording or queue timeline.
constexpr GUID RecordingGuid = { 0xa9b57fc6, 0xcb68, 0x47b2, { 0x81, 0x0c, 0x43, 0x16, 0x64, 0xf9, 0x12, 0x16 } };
constexpr GUID QueueGuid = { 0x9ce44071, 0x4975, 0x4147, { 0xa1, 0x96, 0xdd, 0x21, 0x10, 0x22, 0xcb, 0xcf } };
constexpr GUID DeviceGuid = { 0x6352be6a, 0x6d71, 0x4584, { 0xba, 0xd7, 0xc6, 0x57, 0x95, 0x0d, 0xec, 0x6b } };
constexpr GUID AllocatorGuid = { 0xb411984c, 0x4156, 0x4a3e, { 0xa4, 0xa3, 0xb4, 0xc9, 0x89, 0x36, 0x11, 0x22 } };

constexpr size_t MaxSamples = 32;
constexpr size_t MaxQueues = 32;
constexpr size_t MaxCompletionQueues = 8;

struct QueueTimeline
{
    ComPtr<ID3D12Fence> fence;
    UINT64 frequency = 0;
    UINT64 nextValue = 0;
    std::mutex submitMutex;
};

struct Completion
{
    std::shared_ptr<QueueTimeline> queue;
    UINT64 value;
};

struct Sample
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12Resource> readback;
    std::weak_ptr<FgTiming::Channel> channel;
    uint64_t epoch = 0;
    uint64_t frameId = 0;
    uint32_t generatedFrames = 0;
    UINT64 frequency = 0;
    std::vector<Completion> completions;
    size_t recordingPins = 0;
    size_t allocatorPins = 0;
    size_t submitting = 0;
    uint64_t executions = 0;
    bool busy = false;
    bool ended = false;
    bool publish = false;
    bool published = false;
    bool quarantined = false;
};

struct Pool
{
    std::mutex mutex;
    std::array<std::shared_ptr<Sample>, MaxSamples> samples;
};

Pool& GetPool()
{
    // COM private data can outlive backend and static teardown. Do not release
    // GPU objects during DLL_PROCESS_DETACH; normal retirement happens in Poll.
    static auto* pool = new Pool;
    return *pool;
}

std::atomic<size_t> liveRecordings { 0 };
std::atomic<size_t> liveQueues { 0 };

struct AllocatorGeneration
{
    // Own samples without owning the allocator or command-list COM object.
    // Native private-data destruction or successful allocator Reset retires
    // every GPU use of this generation, including unobserved submissions.
    bool retired = false;
    std::vector<std::shared_ptr<Sample>> samples;
};

void RetireAllocator(const std::shared_ptr<AllocatorGeneration>& generation)
{
    std::lock_guard lock(GetPool().mutex);
    if (generation->retired)
        return;
    generation->retired = true;
    for (auto& sample : generation->samples)
        --sample->allocatorPins;
    generation->samples.clear();
}

class PrivateData : public IUnknown
{
    std::atomic<ULONG> _refs { 1 };

  protected:
    virtual ~PrivateData() = default;

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (iid != IID_IUnknown)
            return E_NOINTERFACE;
        *object = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        auto refs = --_refs;
        if (refs == 0)
            delete this;
        return refs;
    }
};

class RecordingData final : public PrivateData
{
  public:
    bool active = true;
    bool closed = false;
    bool closeSucceeded = false;
    bool counted = false;
    std::shared_ptr<AllocatorGeneration> allocator;
    std::vector<std::shared_ptr<Sample>> samples;

    ~RecordingData() override
    {
        auto& pool = GetPool();
        std::lock_guard lock(pool.mutex);
        for (auto& sample : samples)
            --sample->recordingPins;
        if (counted)
            --liveRecordings;
    }
};

class AllocatorData final : public PrivateData
{
  public:
    std::shared_ptr<AllocatorGeneration> generation = std::make_shared<AllocatorGeneration>();
    ~AllocatorData() override { RetireAllocator(generation); }
};

class QueueData final : public PrivateData
{
  public:
    std::shared_ptr<QueueTimeline> timeline;
    QueueData() { ++liveQueues; }
    ~QueueData() override { --liveQueues; }
};

class DeviceData final : public PrivateData
{
  public:
    // Separate DIRECT/COMPUTE probes: a game's DIRECT queue is not evidence
    // that a COMPUTE queue uses the same hooked implementation.
    std::array<int, 2> probes {};
    std::mutex mutex;
};

template <class T> ComPtr<T> Canonical(T* object)
{
    if (!object)
        return {};
    T* real = nullptr;
    if (Util::CheckForRealObject("FG GPU timing", object, reinterpret_cast<IUnknown**>(&real)))
        return ComPtr<T>(real);
    return ComPtr<T>(object);
}

template <class T> ComPtr<T> GetData(ID3D12Object* object, REFGUID guid)
{
    IUnknown* data = nullptr;
    UINT size = sizeof(data);
    if (FAILED(object->GetPrivateData(guid, &size, &data)) || size != sizeof(data) || !data)
        return {};
    ComPtr<T> result;
    // GetPrivateData AddRefs interfaces installed with SetPrivateDataInterface.
    result.Attach(static_cast<T*>(data));
    return result;
}

using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
using CloseFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*);
using AllocatorResetFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandAllocator*);

std::mutex hookMutex;
std::mutex queueDataMutex;
std::mutex deviceDataMutex;
std::mutex allocatorDataMutex;
ExecuteFn originalExecute = nullptr;
ResetFn originalReset = nullptr;
CloseFn originalClose = nullptr;
AllocatorResetFn originalAllocatorReset = nullptr;
void* executeAddress = nullptr;
void* resetAddress = nullptr;
void* closeAddress = nullptr;
void* allocatorResetAddress = nullptr;
bool executeAttempted = false;
bool listAttempted = false;
bool allocatorAttempted = false;

bool HookAllocator(ID3D12CommandAllocator* allocator);

std::shared_ptr<AllocatorGeneration> GetAllocatorGeneration(ID3D12CommandAllocator* allocator)
{
    std::lock_guard lock(allocatorDataMutex);
    auto data = GetData<AllocatorData>(allocator, AllocatorGuid);
    if (!data)
    {
        data.Attach(new AllocatorData);
        if (FAILED(allocator->SetPrivateDataInterface(AllocatorGuid, data.Get())))
            return {};
    }
    return data->generation;
}

HRESULT STDMETHODCALLTYPE ObserveAllocatorReset(ID3D12CommandAllocator* allocator)
{
    const auto result = originalAllocatorReset(allocator);
    if (SUCCEEDED(result))
    {
        std::lock_guard lock(allocatorDataMutex);
        auto data = GetData<AllocatorData>(allocator, AllocatorGuid);
        if (data)
        {
            // A valid allocator Reset requires all executions using its
            // command storage to be complete, including unobserved replays.
            RetireAllocator(data->generation);
            allocator->SetPrivateDataInterface(AllocatorGuid, nullptr);
        }
    }
    return result;
}

std::shared_ptr<QueueTimeline> GetTimeline(ID3D12CommandQueue* queue)
{
    std::lock_guard lock(queueDataMutex);
    auto data = GetData<QueueData>(queue, QueueGuid);
    if (data)
        return data->timeline;
    if (liveQueues.load() >= MaxQueues)
        return {};

    ComPtr<ID3D12Device> device;
    if (FAILED(queue->GetDevice(IID_PPV_ARGS(device.GetAddressOf()))))
        return {};
    auto timeline = std::make_shared<QueueTimeline>();
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(timeline->fence.GetAddressOf()))))
        return {};
    if (FAILED(queue->GetTimestampFrequency(&timeline->frequency)) || timeline->frequency == 0)
        return {};

    data.Attach(new QueueData);
    data->timeline = timeline;
    if (FAILED(queue->SetPrivateDataInterface(QueueGuid, data.Get())))
        return {};
    return timeline;
}

HRESULT STDMETHODCALLTYPE ObserveReset(ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator,
                                       ID3D12PipelineState* initialState)
{
    auto result = originalReset(list, allocator, initialState);
    if (SUCCEEDED(result))
    {
        auto recording = GetData<RecordingData>(list, RecordingGuid);
        if (recording)
        {
            {
                std::lock_guard lock(GetPool().mutex);
                recording->active = false;
            }
            // Replace outside the pool lock: RecordingData's destructor takes
            // it. Only lists requested for FG timing get allocator metadata.
            ComPtr<RecordingData> next;
            next.Attach(new RecordingData);
            auto realAllocator = Canonical(allocator);
            if (realAllocator && HookAllocator(realAllocator.Get()))
                next->allocator = GetAllocatorGeneration(realAllocator.Get());
            list->SetPrivateDataInterface(RecordingGuid, next.Get());
        }
    }
    return result;
}

HRESULT STDMETHODCALLTYPE ObserveClose(ID3D12GraphicsCommandList* list)
{
    auto result = originalClose(list);
    auto recording = GetData<RecordingData>(list, RecordingGuid);
    if (recording)
    {
        std::lock_guard lock(GetPool().mutex);
        recording->closed = true;
        recording->closeSucceeded = SUCCEEDED(result);
        if (FAILED(result))
            for (auto& sample : recording->samples)
                sample->publish = false;
    }
    return result;
}

void STDMETHODCALLTYPE ObserveExecute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    if (liveRecordings.load() == 0 || !lists)
    {
        originalExecute(queue, count, lists);
        return;
    }

    std::vector<ComPtr<RecordingData>> recordings;
    for (UINT index = 0; index < count; ++index)
    {
        if (lists[index])
        {
            auto recording = GetData<RecordingData>(lists[index], RecordingGuid);
            if (recording)
                recordings.push_back(std::move(recording));
        }
    }
    if (recordings.empty())
    {
        originalExecute(queue, count, lists);
        return;
    }

    auto timeline = GetTimeline(queue);
    // One queue owns one fence timeline. Serialize only tagged execute+signal
    // pairs so two CPU submitters cannot assign/signals values out of order.
    std::unique_lock<std::mutex> submitLock;
    if (timeline)
        submitLock = std::unique_lock(timeline->submitMutex);

    std::vector<std::shared_ptr<Sample>> samples;
    auto& pool = GetPool();
    {
        std::lock_guard lock(pool.mutex);
        for (auto& recording : recordings)
        {
            for (auto& sample : recording->samples)
            {
                ++sample->submitting;
                ++sample->executions;
                if (!recording->active || !recording->closeSucceeded || !sample->ended || sample->executions != 1)
                    sample->publish = false;
                samples.push_back(sample);
            }
        }
    }

    originalExecute(queue, count, lists);

    const UINT64 value = timeline && timeline->nextValue != UINT64_MAX ? ++timeline->nextValue : 0;
    const bool signalled = value != 0 && SUCCEEDED(queue->Signal(timeline->fence.Get(), value));
    {
        std::lock_guard lock(pool.mutex);
        for (auto& sample : samples)
        {
            --sample->submitting;
            if (!signalled)
            {
                // No completion proof: retain the small, bounded resource slot
                // until device loss, rather than release an in-flight query.
                sample->quarantined = true;
                sample->publish = false;
                continue;
            }
            sample->frequency = timeline->frequency;
            auto existing = std::find_if(sample->completions.begin(), sample->completions.end(),
                                         [&](const Completion& item) { return item.queue == timeline; });
            if (existing != sample->completions.end())
                existing->value = value;
            else if (sample->completions.size() < MaxCompletionQueues)
                sample->completions.push_back({ timeline, value });
            else
            {
                sample->quarantined = true;
                sample->publish = false;
            }
        }
    }
}

bool HookList(ID3D12GraphicsCommandList* list)
{
    auto table = *reinterpret_cast<void***>(list);
    std::lock_guard lock(hookMutex);
    if (originalReset && originalClose)
        return table[10] == resetAddress && table[9] == closeAddress;
    if (listAttempted)
        return false;
    listAttempted = true;
    FgTimingHookThreads threads;
    if (!threads.Prepare())
    {
        LOG_WARN("FG GPU timing command-list thread preparation unavailable");
        return false;
    }
    resetAddress = table[10];
    closeAddress = table[9];
    originalReset = reinterpret_cast<ResetFn>(resetAddress);
    originalClose = reinterpret_cast<CloseFn>(closeAddress);

    LONG result = DetourTransactionBegin();
    if (result == NO_ERROR)
    {
        result = DetourAttach(reinterpret_cast<PVOID*>(&originalReset), ObserveReset);
        if (result == NO_ERROR)
            result = DetourAttach(reinterpret_cast<PVOID*>(&originalClose), ObserveClose);
        if (result == NO_ERROR)
            result = threads.Enlist();
        if (result == NO_ERROR)
            result = DetourTransactionCommit();
        else
            DetourTransactionAbort();
    }
    if (result != NO_ERROR)
    {
        originalReset = nullptr;
        originalClose = nullptr;
        LOG_WARN("FG GPU timing command-list observation unavailable: {}", result);
        return false;
    }
    return true;
}

bool HookAllocator(ID3D12CommandAllocator* allocator)
{
    auto address = (*reinterpret_cast<void***>(allocator))[8];
    std::lock_guard lock(hookMutex);
    if (originalAllocatorReset)
        return address == allocatorResetAddress;
    if (allocatorAttempted)
        return false;
    allocatorAttempted = true;
    FgTimingHookThreads threads;
    if (!threads.Prepare())
    {
        LOG_WARN("FG GPU timing allocator thread preparation unavailable");
        return false;
    }
    allocatorResetAddress = address;
    originalAllocatorReset = reinterpret_cast<AllocatorResetFn>(address);
    LONG result = DetourTransactionBegin();
    if (result == NO_ERROR)
    {
        result = DetourAttach(reinterpret_cast<PVOID*>(&originalAllocatorReset), ObserveAllocatorReset);
        if (result == NO_ERROR)
            result = threads.Enlist();
        if (result == NO_ERROR)
            result = DetourTransactionCommit();
        else
            DetourTransactionAbort();
    }
    if (result != NO_ERROR)
    {
        originalAllocatorReset = nullptr;
        LOG_WARN("FG GPU timing allocator observation unavailable: {}", result);
        return false;
    }
    return true;
}

bool ProbeQueue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
{
    if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_COMPUTE)
        return false;
    ComPtr<DeviceData> data;
    {
        std::lock_guard lock(deviceDataMutex);
        data = GetData<DeviceData>(device, DeviceGuid);
        if (!data)
        {
            data.Attach(new DeviceData);
            if (FAILED(device->SetPrivateDataInterface(DeviceGuid, data.Get())))
                return false;
        }
    }
    std::lock_guard lock(data->mutex);
    auto& probe = data->probes[type == D3D12_COMMAND_LIST_TYPE_COMPUTE ? 1 : 0];
    if (probe != 0)
        return probe > 0;
    probe = -1;
    D3D12_COMMAND_QUEUE_DESC description {};
    description.Type = type;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&description, IID_PPV_ARGS(queue.GetAddressOf()))) ||
        !FgGpuTime_Dx12::HookQueue(queue.Get()))
        return false;
    probe = 1;
    return true;
}

std::shared_ptr<Sample> AllocateSample(ID3D12Device* device)
{
    auto& pool = GetPool();
    std::lock_guard lock(pool.mutex);
    for (auto& entry : pool.samples)
    {
        if (entry && entry->busy)
            continue;
        if (!entry || entry->device.Get() != device)
        {
            auto sample = std::make_shared<Sample>();
            sample->device = device;
            D3D12_QUERY_HEAP_DESC queryDescription {};
            queryDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            queryDescription.Count = 2;
            if (FAILED(device->CreateQueryHeap(&queryDescription, IID_PPV_ARGS(sample->queries.GetAddressOf()))))
                return {};
            D3D12_HEAP_PROPERTIES heap {};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC buffer {};
            buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = 2 * sizeof(UINT64);
            buffer.Height = 1;
            buffer.DepthOrArraySize = 1;
            buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1;
            buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                       D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                       IID_PPV_ARGS(sample->readback.GetAddressOf()))))
                return {};
            entry = std::move(sample);
        }
        entry->busy = true;
        entry->ended = false;
        entry->publish = false;
        entry->published = false;
        entry->quarantined = false;
        entry->executions = 0;
        entry->frequency = 0;
        return entry;
    }
    return {};
}
} // namespace

struct ScopedFgGpuTime_Dx12::Impl
{
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<RecordingData> recording;
    std::shared_ptr<Sample> sample;
};

ScopedFgGpuTime_Dx12::ScopedFgGpuTime_Dx12(ID3D12GraphicsCommandList* commandList,
                                           const std::shared_ptr<FgTiming::Channel>& channel, uint64_t frameId,
                                           uint32_t generatedFrames)
{
    if (!commandList || !channel || frameId == 0)
        return;
    auto list = Canonical(commandList);
    if (!HookList(list.Get()))
        return;
    ComPtr<ID3D12Device> suppliedDevice;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(suppliedDevice.GetAddressOf()))))
        return;
    auto device = Canonical(suppliedDevice.Get());
    // The command-list interface does not expose its creation node mask.
    // Do not place a default-node timestamp heap on another linked GPU node.
    if (device->GetNodeCount() != 1 || !ProbeQueue(device.Get(), list->GetType()) ||
        FAILED(device->GetDeviceRemovedReason()))
        return;

    auto recording = GetData<RecordingData>(list.Get(), RecordingGuid);
    if (!recording)
    {
        recording.Attach(new RecordingData);
        list->SetPrivateDataInterface(RecordingGuid, recording.Get());
        // A command list does not expose its current allocator. Observe its
        // next successful Reset before writing any query into command storage.
        return;
    }

    FgGpuTime_Dx12::Poll();
    {
        std::lock_guard lock(GetPool().mutex);
        if (!recording->active || recording->closed || !recording->allocator || recording->allocator->retired)
            return;
    }
    auto sample = AllocateSample(device.Get());
    if (!sample)
        return;
    {
        std::lock_guard lock(GetPool().mutex);
        if (!recording->active || recording->closed || recording->allocator->retired)
        {
            sample->busy = false;
            return;
        }
        sample->channel = channel;
        sample->epoch = channel->Epoch();
        sample->frameId = frameId;
        sample->generatedFrames = generatedFrames;
        ++sample->recordingPins;
        ++sample->allocatorPins;
        recording->allocator->samples.push_back(sample);
        recording->samples.push_back(sample);
        if (!recording->counted)
        {
            recording->counted = true;
            ++liveRecordings;
        }
        list->EndQuery(sample->queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    }
    _impl = std::make_unique<Impl>();
    _impl->list = std::move(list);
    _impl->recording = std::move(recording);
    _impl->sample = std::move(sample);
}

ScopedFgGpuTime_Dx12::~ScopedFgGpuTime_Dx12() { End(false); }

void ScopedFgGpuTime_Dx12::End(bool succeeded)
{
    if (!_impl)
        return;
    {
        std::lock_guard lock(GetPool().mutex);
        auto& sample = _impl->sample;
        if (_impl->recording->active && !_impl->recording->closed)
        {
            _impl->list->EndQuery(sample->queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            _impl->list->ResolveQueryData(sample->queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                          sample->readback.Get(), 0);
            sample->ended = true;
            sample->publish = succeeded;
        }
    }
    // Do not retain the command-list object after recording. Its private COM
    // data keeps the slot alive; observed queue fences cover submitted work.
    _impl.reset();
}

bool FgGpuTime_Dx12::HookQueue(ID3D12CommandQueue* commandQueue)
{
    auto queue = Canonical(commandQueue);
    if (!queue)
        return false;
    const auto type = queue->GetDesc().Type;
    if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_COMPUTE)
        return false;
    auto address = (*reinterpret_cast<void***>(queue.Get()))[10];
    {
        std::lock_guard lock(hookMutex);
        if (originalExecute)
        {
            if (address != executeAddress)
                return false;
        }
        else
        {
            if (executeAttempted)
                return false;
            executeAttempted = true;
            FgTimingHookThreads threads;
            if (!threads.Prepare())
            {
                LOG_WARN("FG GPU timing queue thread preparation unavailable");
                return false;
            }
            executeAddress = address;
            originalExecute = reinterpret_cast<ExecuteFn>(address);
            LONG result = DetourTransactionBegin();
            if (result == NO_ERROR)
            {
                result = DetourAttach(reinterpret_cast<PVOID*>(&originalExecute), ObserveExecute);
                if (result == NO_ERROR)
                    result = threads.Enlist();
                if (result == NO_ERROR)
                    result = DetourTransactionCommit();
                else
                    DetourTransactionAbort();
            }
            if (result != NO_ERROR)
            {
                originalExecute = nullptr;
                LOG_WARN("FG GPU timing queue observation unavailable: {}", result);
                return false;
            }
        }
    }
    return GetTimeline(queue.Get()) != nullptr;
}

void FgGpuTime_Dx12::Poll()
{
    struct Result
    {
        std::shared_ptr<FgTiming::Channel> channel;
        uint64_t epoch;
        uint64_t frameId;
        double milliseconds;
        uint32_t generatedFrames;
    };
    std::vector<Result> results;
    auto& pool = GetPool();
    {
        std::lock_guard lock(pool.mutex);
        for (auto& sample : pool.samples)
        {
            if (!sample || !sample->busy || sample->submitting != 0)
                continue;
            bool complete = true;
            bool removed = false;
            for (auto& item : sample->completions)
            {
                auto value = item.queue->fence->GetCompletedValue();
                if (value == UINT64_MAX)
                    removed = true;
                else if (value < item.value)
                    complete = false;
            }
            if (sample->quarantined)
            {
                removed = removed || FAILED(sample->device->GetDeviceRemovedReason());
                // Allocator retirement independently covers every GPU use,
                // even when a queue Signal failed or its implementation was
                // unobserved. Known fence values must also retire below.
                if (!removed && (sample->recordingPins != 0 || sample->allocatorPins != 0))
                    continue;
            }
            if (!complete && !removed)
                continue;

            // An observed submission fence alone does not prevent a later,
            // unobserved replay from writing the same readback. Allocator
            // retirement makes the CPU read safe. Supported FG producers reset
            // between dispatches; arbitrary third-party replay chains cannot be
            // identified here. In particular, an unobserved replay on a different
            // timestamp domain would not provide its frequency. Such chains are
            // unsupported; observed replays never publish.
            if (!removed && sample->allocatorPins == 0 && sample->publish && !sample->published &&
                sample->executions == 1 && sample->frequency != 0)
            {
                if (auto channel = sample->channel.lock(); channel && channel->Epoch() == sample->epoch)
                {
                    UINT64* timestamps = nullptr;
                    const D3D12_RANGE readRange { 0, 2 * sizeof(UINT64) };
                    if (SUCCEEDED(sample->readback->Map(0, &readRange, reinterpret_cast<void**>(&timestamps))))
                    {
                        if (timestamps && timestamps[1] >= timestamps[0])
                        {
                            auto ms = (timestamps[1] - timestamps[0]) * 1000.0 / sample->frequency;
                            if (std::isfinite(ms))
                                results.push_back({ std::move(channel), sample->epoch, sample->frameId, ms,
                                                    sample->generatedFrames });
                        }
                        const D3D12_RANGE writeRange { 0, 0 };
                        sample->readback->Unmap(0, &writeRange);
                    }
                }
                sample->published = true;
            }
            sample->completions.clear();
            if (sample->recordingPins == 0 && sample->allocatorPins == 0)
            {
                sample->busy = false;
                auto channel = sample->channel.lock();
                if (removed || !channel || channel->Epoch() != sample->epoch)
                    sample.reset();
            }
        }
    }
    // Pool slots retire independently, so their indices do not imply frame
    // order. Preserve every completed sample accepted by Channel's dedup rule.
    std::stable_sort(results.begin(), results.end(),
                     [](const Result& a, const Result& b) { return a.frameId < b.frameId; });
    for (auto& result : results)
        result.channel->Publish(result.epoch, result.frameId, result.milliseconds, result.generatedFrames);
}
