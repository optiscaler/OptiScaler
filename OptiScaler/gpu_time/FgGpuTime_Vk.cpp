#include "pch.h"
#include "FgGpuTime_Vk.h"
#include "FgTimingHookThreads.h"

#include <hooks/Vulkan_Hooks.h>
#include <detours/detours.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
constexpr size_t MaxDevices = 4;
constexpr size_t MaxPools = 256;
constexpr size_t MaxBuffers = 1024;
constexpr size_t MaxSyncObjects = 1024;
constexpr size_t MaxQueryPools = 48;
constexpr size_t MaxQueues = 16;
constexpr size_t MaxSignals = 16;

struct SemaphoreState
{
    uint64_t generation = 0;
    uint64_t lastSignal = 0;
    VkQueue queue = VK_NULL_HANDLE;
    bool timeline = false;
    bool gpuOnly = true;
};

struct FenceState
{
    uint64_t generation = 0;
};

struct Sample
{
    std::shared_ptr<FgTiming::Channel> channel;
    uint64_t epoch = 0;
    uint64_t frameId = 0;
    uint64_t serial = 0;
    uint32_t generatedFrames = 0;
    uint32_t family = UINT32_MAX;
    VkFence fence = VK_NULL_HANDLE;
    uint64_t fenceGeneration = 0;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    uint64_t semaphoreGeneration = 0;
    uint64_t semaphoreValue = 0;
    bool ended = false;
    bool successful = false;
    bool submitted = false;
    bool submissionPending = false;
    bool completed = false;
};

struct BufferState
{
    VkCommandPool pool = VK_NULL_HANDLE;
    VkQueryPool queries = VK_NULL_HANDLE;
    uint32_t family = UINT32_MAX;
    bool primary = false;
    bool recording = false;
    bool oneTime = false;
    bool invalidated = true;
    bool quarantined = false;
    std::optional<Sample> sample;
};

struct PoolState
{
    uint32_t family = UINT32_MAX;
    bool protectedPool = false;
};

struct DeviceState
{
    VkDevice device = VK_NULL_HANDLE;
    double timestampPeriod = 0.0;
    uint64_t nextGeneration = 0;
    size_t queryCount = 0;
    PFN_vkCreateQueryPool createQueryPool = nullptr;
    PFN_vkDestroyQueryPool destroyQueryPool = nullptr;
    PFN_vkCmdResetQueryPool resetQueries = nullptr;
    PFN_vkCmdWriteTimestamp writeTimestamp = nullptr;
    PFN_vkGetQueryPoolResults getQueries = nullptr;
    PFN_vkGetFenceStatus getFenceStatus = nullptr;
    PFN_vkGetSemaphoreCounterValue getCounter = nullptr;
    std::vector<VkQueueFamilyProperties> families;
    std::unordered_map<VkQueue, uint32_t> queues;
    std::unordered_map<VkCommandPool, PoolState> pools;
    std::unordered_map<VkCommandBuffer, BufferState> buffers;
    std::unordered_map<VkSemaphore, SemaphoreState> semaphores;
    std::unordered_map<VkFence, FenceState> fences;
    std::vector<BufferState> retired;
};

// Vulkan lifecycle calls are already externally synchronized for the relevant objects. The mutex
// protects the small observation tables across the SDK's worker threads; no GPU wait is performed.
std::recursive_mutex mutex;
std::unordered_map<VkDevice, DeviceState> devices;
uint64_t nextSerial = 0;

#define FG_VK_FUNCTIONS(X)                                                                                             \
    X(DestroyDevice, PFN_vkDestroyDevice)                                                                              \
    X(CreateCommandPool, PFN_vkCreateCommandPool)                                                                      \
    X(DestroyCommandPool, PFN_vkDestroyCommandPool)                                                                    \
    X(ResetCommandPool, PFN_vkResetCommandPool)                                                                        \
    X(AllocateCommandBuffers, PFN_vkAllocateCommandBuffers)                                                            \
    X(FreeCommandBuffers, PFN_vkFreeCommandBuffers)                                                                    \
    X(BeginCommandBuffer, PFN_vkBeginCommandBuffer)                                                                    \
    X(EndCommandBuffer, PFN_vkEndCommandBuffer)                                                                        \
    X(ResetCommandBuffer, PFN_vkResetCommandBuffer)                                                                    \
    X(CreateSemaphore, PFN_vkCreateSemaphore)                                                                          \
    X(DestroySemaphore, PFN_vkDestroySemaphore)                                                                        \
    X(SignalSemaphore, PFN_vkSignalSemaphore)                                                                          \
    X(SignalSemaphoreKHR, PFN_vkSignalSemaphoreKHR)                                                                    \
    X(CreateFence, PFN_vkCreateFence)                                                                                  \
    X(DestroyFence, PFN_vkDestroyFence)                                                                                \
    X(ResetFences, PFN_vkResetFences)                                                                                  \
    X(QueueSubmit, PFN_vkQueueSubmit)                                                                                  \
    X(QueueSubmit2, PFN_vkQueueSubmit2)                                                                                \
    X(QueueSubmit2KHR, PFN_vkQueueSubmit2KHR)

#define DECLARE_ORIGINAL(name, type) type original##name = nullptr;
FG_VK_FUNCTIONS(DECLARE_ORIGINAL)
#undef DECLARE_ORIGINAL

DeviceState* FindDevice(VkDevice device)
{
    auto it = devices.find(device);
    return it == devices.end() ? nullptr : &it->second;
}

DeviceState* FindBuffer(VkCommandBuffer commandBuffer, BufferState*& buffer)
{
    for (auto& [handle, device] : devices)
    {
        auto it = device.buffers.find(commandBuffer);
        if (it != device.buffers.end())
        {
            buffer = &it->second;
            return &device;
        }
    }
    buffer = nullptr;
    return nullptr;
}

template <typename T> const T* FindNext(const void* next, VkStructureType type)
{
    // Bound traversal even if an unexpected vendor chain reaches this observer.
    for (size_t i = 0; next && i < 32; ++i)
    {
        auto header = static_cast<const VkBaseInStructure*>(next);
        if (header->sType == type)
            return static_cast<const T*>(next);
        next = header->pNext;
    }
    return nullptr;
}

bool Completed(DeviceState& device, const Sample& sample)
{
    if (sample.fence != VK_NULL_HANDLE)
    {
        auto it = device.fences.find(sample.fence);
        if (it != device.fences.end() && it->second.generation == sample.fenceGeneration &&
            device.getFenceStatus(device.device, sample.fence) == VK_SUCCESS)
            return true;
    }

    if (sample.semaphore != VK_NULL_HANDLE)
    {
        auto it = device.semaphores.find(sample.semaphore);
        if (it != device.semaphores.end() && it->second.generation == sample.semaphoreGeneration &&
            it->second.timeline && it->second.gpuOnly && device.getCounter)
        {
            uint64_t value = 0;
            if (device.getCounter(device.device, sample.semaphore, &value) == VK_SUCCESS &&
                value >= sample.semaphoreValue)
                return true;
        }
    }
    return false;
}

void PollBuffer(DeviceState& device, BufferState& buffer)
{
    if (!buffer.sample || !buffer.sample->submitted || buffer.sample->submissionPending || buffer.sample->completed)
        return;

    auto& sample = *buffer.sample;
    if (!Completed(device, sample))
        return;

    // The current submission has completed, including its in-command-buffer reset and both writes.
    // Query availability on its own would also expose old results before that GPU reset executes.
    sample.completed = true;
    if (!sample.successful || sample.family >= device.families.size())
        return;

    uint64_t timestamps[2] {};
    if (device.getQueries(device.device, buffer.queries, 0, 2, sizeof(timestamps), timestamps, sizeof(uint64_t),
                          VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
        return;

    auto milliseconds = FgTiming::VulkanMilliseconds(
        timestamps[0], timestamps[1], device.families[sample.family].timestampValidBits, device.timestampPeriod);
    if (milliseconds)
        sample.channel->Publish(sample.epoch, sample.frameId, *milliseconds, sample.generatedFrames);
}

void DeleteQueries(DeviceState& device, BufferState& buffer)
{
    if (buffer.queries != VK_NULL_HANDLE)
    {
        device.destroyQueryPool(device.device, buffer.queries, nullptr);
        buffer.queries = VK_NULL_HANDLE;
        --device.queryCount;
    }
}

void RetireDevice(VkDevice handle)
{
    auto it = devices.find(handle);
    if (it == devices.end())
        return;

    // The owner's completed teardown or valid vkDestroyDevice retirement establishes that work
    // using these children is finished. Do not poll sync objects that may already have been destroyed.
    for (auto& [commandBuffer, buffer] : it->second.buffers)
    {
        if (buffer.sample)
            buffer.sample->channel->Reset();
        DeleteQueries(it->second, buffer);
    }
    for (auto& buffer : it->second.retired)
    {
        if (buffer.sample)
            buffer.sample->channel->Reset();
        DeleteQueries(it->second, buffer);
    }
    devices.erase(it);
}

VKAPI_ATTR void VKAPI_CALL HookDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator)
{
    PFN_vkDestroyDevice destroyFunction = nullptr;
    {
        std::lock_guard lock(mutex);
        destroyFunction = originalDestroyDevice;
        // Release injected children while the device is alive, and remove the device from Poll()
        // before forwarding destruction. This also covers an earlier failed SDK swapchain teardown.
        RetireDevice(device);
    }
    destroyFunction(device, allocator);
}

void InvalidateRecording(DeviceState& device, BufferState& buffer)
{
    PollBuffer(device, buffer);
    buffer.recording = false;
    buffer.oneTime = false;
    buffer.invalidated = true;
    if (buffer.sample && buffer.sample->submitted && !buffer.sample->completed)
    {
        // A reset without a usable completion token cannot prove that these queries are safe to reuse.
        buffer.quarantined = true;
        buffer.sample->successful = false;
    }
    else
    {
        buffer.sample.reset();
    }
}

void RetireBuffer(DeviceState& device, VkCommandBuffer commandBuffer)
{
    auto it = device.buffers.find(commandBuffer);
    if (it == device.buffers.end())
        return;

    InvalidateRecording(device, it->second);
    if (it->second.sample && it->second.sample->submitted && !it->second.sample->completed)
        device.retired.push_back(std::move(it->second));
    else
        DeleteQueries(device, it->second);
    device.buffers.erase(it);
}

void PollDevice(DeviceState& device)
{
    for (auto& [handle, buffer] : device.buffers)
        PollBuffer(device, buffer);

    for (auto it = device.retired.begin(); it != device.retired.end();)
    {
        PollBuffer(device, *it);
        if (!it->sample || it->sample->completed)
        {
            DeleteQueries(device, *it);
            it = device.retired.erase(it);
        }
        else
            ++it;
    }
}

struct Signal
{
    VkSemaphore semaphore = VK_NULL_HANDLE;
    uint64_t value = 0;
    bool allCommands = false;
};

std::optional<Signal> ObserveSignals(DeviceState& device, VkQueue queue, const Signal* signals, size_t count)
{
    std::optional<Signal> completion;
    for (size_t i = 0; i < count; ++i)
    {
        auto it = device.semaphores.find(signals[i].semaphore);
        if (it == device.semaphores.end() || !it->second.timeline)
            continue;

        auto& semaphore = it->second;
        if (!signals[i].allCommands || signals[i].value <= semaphore.lastSignal ||
            (semaphore.queue != VK_NULL_HANDLE && semaphore.queue != queue))
        {
            // An earlier-stage/other-queue/host signal must never satisfy the FG completion token.
            semaphore.gpuOnly = false;
            continue;
        }
        semaphore.queue = queue;
        semaphore.lastSignal = signals[i].value;
        if (semaphore.gpuOnly && !completion)
            completion = signals[i];
    }
    return completion;
}

struct SubmissionRecords
{
    std::array<uint64_t, MaxDevices * MaxQueryPools> serials {};
    size_t count = 0;

    void Add(uint64_t serial)
    {
        if (count < serials.size())
            serials[count++] = serial;
    }

    bool Contains(uint64_t serial) const
    {
        return std::find(serials.begin(), serials.begin() + count, serial) != serials.begin() + count;
    }
};

void ObserveSubmit(VkQueue queue, VkFence fence, const VkCommandBuffer* commandBuffers, size_t count,
                   const Signal* signals, size_t signalCount, bool supported, SubmissionRecords& pending)
{
    for (auto& [handle, device] : devices)
    {
        auto completion = ObserveSignals(device, queue, signals, signalCount);
        auto queueIt = device.queues.find(queue);
        auto fenceIt = device.fences.find(fence);
        for (size_t i = 0; i < count; ++i)
        {
            auto it = device.buffers.find(commandBuffers[i]);
            if (it == device.buffers.end() || !it->second.sample)
                continue;

            auto& buffer = it->second;
            auto& sample = *buffer.sample;
            // One-time SDK recordings must be submitted exactly once before their next observed reset.
            if (sample.submitted || !buffer.oneTime || buffer.recording || !sample.ended || buffer.quarantined)
            {
                sample.successful = false;
                sample.fence = VK_NULL_HANDLE;
                sample.semaphore = VK_NULL_HANDLE;
                sample.completed = false;
                buffer.quarantined = true;
            }
            sample.submitted = true;
            sample.submissionPending = true;
            pending.Add(sample.serial);
            if (!supported || queueIt == device.queues.end() || queueIt->second != buffer.family || buffer.quarantined)
            {
                sample.successful = false;
                continue;
            }

            sample.family = queueIt->second;
            if (fence != VK_NULL_HANDLE && fenceIt != device.fences.end())
            {
                sample.fence = fence;
                sample.fenceGeneration = fenceIt->second.generation;
            }
            if (completion)
            {
                sample.semaphore = completion->semaphore;
                sample.semaphoreValue = completion->value;
                sample.semaphoreGeneration = device.semaphores.at(completion->semaphore).generation;
            }
        }
    }
}

void CompleteSubmit(const SubmissionRecords& pending, VkResult result)
{
    auto finish = [&](BufferState& buffer)
    {
        if (!buffer.sample || !pending.Contains(buffer.sample->serial))
            return;
        auto& sample = *buffer.sample;
        sample.submissionPending = false;
        if (result != VK_SUCCESS)
        {
            sample.successful = false;
            sample.fence = VK_NULL_HANDLE;
            sample.semaphore = VK_NULL_HANDLE;
            // A failed first submission did not execute the recording. An unexpected resubmission
            // remains quarantined because the earlier execution may still be using its queries.
            if (!buffer.quarantined && result != VK_ERROR_DEVICE_LOST)
                sample.submitted = false;
        }
    };
    for (auto& [handle, device] : devices)
    {
        for (auto& [commandBuffer, buffer] : device.buffers)
            finish(buffer);
        for (auto& buffer : device.retired)
            finish(buffer);
    }
}

void InvalidateUnobservedSignal(VkSemaphore semaphore)
{
    for (auto& [handle, device] : devices)
        if (auto it = device.semaphores.find(semaphore); it != device.semaphores.end())
            it->second.gpuOnly = false;
}

VKAPI_ATTR VkResult VKAPI_CALL HookCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* info,
                                                     const VkAllocationCallbacks* allocator, VkCommandPool* pool)
{
    std::lock_guard lock(mutex);
    auto result = originalCreateCommandPool(device, info, allocator, pool);
    if (auto state = FindDevice(device);
        result == VK_SUCCESS && state && info && pool && state->pools.size() < MaxPools)
        state->pools[*pool] = { info->queueFamilyIndex, (info->flags & VK_COMMAND_POOL_CREATE_PROTECTED_BIT) != 0 };
    return result;
}

VKAPI_ATTR void VKAPI_CALL HookDestroyCommandPool(VkDevice device, VkCommandPool pool,
                                                  const VkAllocationCallbacks* allocator)
{
    std::lock_guard lock(mutex);
    if (auto state = FindDevice(device))
    {
        for (auto it = state->buffers.begin(); it != state->buffers.end();)
        {
            auto current = it++;
            if (current->second.pool == pool)
                RetireBuffer(*state, current->first);
        }
        state->pools.erase(pool);
    }
    originalDestroyCommandPool(device, pool, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL HookResetCommandPool(VkDevice device, VkCommandPool pool, VkCommandPoolResetFlags flags)
{
    std::lock_guard lock(mutex);
    if (auto state = FindDevice(device))
        for (auto& [handle, buffer] : state->buffers)
            if (buffer.pool == pool)
                PollBuffer(*state, buffer);

    auto result = originalResetCommandPool(device, pool, flags);
    if (auto state = FindDevice(device); result == VK_SUCCESS && state)
        for (auto& [handle, buffer] : state->buffers)
            if (buffer.pool == pool)
                InvalidateRecording(*state, buffer);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL HookAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* info,
                                                          VkCommandBuffer* commandBuffers)
{
    std::lock_guard lock(mutex);
    auto result = originalAllocateCommandBuffers(device, info, commandBuffers);
    if (auto state = FindDevice(device); result == VK_SUCCESS && state && info && commandBuffers)
    {
        auto pool = state->pools.find(info->commandPool);
        if (pool == state->pools.end() || pool->second.protectedPool)
            return result;

        for (uint32_t i = 0; i < info->commandBufferCount && state->buffers.size() < MaxBuffers; ++i)
        {
            RetireBuffer(*state, commandBuffers[i]);
            BufferState buffer;
            buffer.pool = info->commandPool;
            buffer.family = pool->second.family;
            buffer.primary = info->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            state->buffers.emplace(commandBuffers[i], std::move(buffer));
        }
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL HookFreeCommandBuffers(VkDevice device, VkCommandPool pool, uint32_t count,
                                                  const VkCommandBuffer* commandBuffers)
{
    std::lock_guard lock(mutex);
    if (auto state = FindDevice(device); state && commandBuffers)
        for (uint32_t i = 0; i < count; ++i)
            RetireBuffer(*state, commandBuffers[i]);
    originalFreeCommandBuffers(device, pool, count, commandBuffers);
}

VKAPI_ATTR VkResult VKAPI_CALL HookBeginCommandBuffer(VkCommandBuffer commandBuffer,
                                                      const VkCommandBufferBeginInfo* info)
{
    {
        std::lock_guard lock(mutex);
        BufferState* previous = nullptr;
        if (auto device = FindBuffer(commandBuffer, previous))
            PollBuffer(*device, *previous);
    }
    const auto result = originalBeginCommandBuffer(commandBuffer, info);
    std::lock_guard lock(mutex);
    BufferState* buffer = nullptr;
    auto device = FindBuffer(commandBuffer, buffer);
    if (device && result == VK_SUCCESS && info)
    {
        InvalidateRecording(*device, *buffer);
        buffer->recording = true;
        buffer->oneTime = (info->flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != 0 &&
                          (info->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) == 0;
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL HookEndCommandBuffer(VkCommandBuffer commandBuffer)
{
    const auto result = originalEndCommandBuffer(commandBuffer);
    std::lock_guard lock(mutex);
    BufferState* buffer = nullptr;
    if (FindBuffer(commandBuffer, buffer))
    {
        buffer->recording = false;
        if (result != VK_SUCCESS && buffer->sample)
            buffer->sample->successful = false;
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL HookResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
{
    std::lock_guard lock(mutex);
    BufferState* buffer = nullptr;
    auto device = FindBuffer(commandBuffer, buffer);
    if (device)
        PollBuffer(*device, *buffer);
    auto result = originalResetCommandBuffer(commandBuffer, flags);
    if (device && result == VK_SUCCESS)
        InvalidateRecording(*device, *buffer);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL HookCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* info,
                                                   const VkAllocationCallbacks* allocator, VkSemaphore* semaphore)
{
    std::lock_guard lock(mutex);
    auto result = originalCreateSemaphore(device, info, allocator, semaphore);
    if (auto state = FindDevice(device);
        result == VK_SUCCESS && state && info && semaphore && state->semaphores.size() < MaxSyncObjects)
    {
        auto type = FindNext<VkSemaphoreTypeCreateInfo>(info->pNext, VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
        SemaphoreState value;
        value.generation = ++state->nextGeneration;
        value.timeline = type && type->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE;
        value.lastSignal = type ? type->initialValue : 0;
        state->semaphores[*semaphore] = value;
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL HookDestroySemaphore(VkDevice device, VkSemaphore semaphore,
                                                const VkAllocationCallbacks* allocator)
{
    std::lock_guard lock(mutex);
    if (auto state = FindDevice(device))
    {
        PollDevice(*state);
        state->semaphores.erase(semaphore);
    }
    originalDestroySemaphore(device, semaphore, allocator);
}

VkResult ObserveSignalSemaphore(PFN_vkSignalSemaphore signalFunction, VkDevice device,
                                const VkSemaphoreSignalInfo* info)
{
    std::lock_guard lock(mutex);
    // Invalidate before the host signal can make a pending GPU token appear ready.
    if (auto state = FindDevice(device); state && info)
        if (auto it = state->semaphores.find(info->semaphore); it != state->semaphores.end())
            it->second.gpuOnly = false;
    return signalFunction(device, info);
}

VKAPI_ATTR VkResult VKAPI_CALL HookSignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo* info)
{
    return ObserveSignalSemaphore(originalSignalSemaphore, device, info);
}

VKAPI_ATTR VkResult VKAPI_CALL HookSignalSemaphoreKHR(VkDevice device, const VkSemaphoreSignalInfo* info)
{
    return ObserveSignalSemaphore(originalSignalSemaphoreKHR, device, info);
}

VKAPI_ATTR VkResult VKAPI_CALL HookCreateFence(VkDevice device, const VkFenceCreateInfo* info,
                                               const VkAllocationCallbacks* allocator, VkFence* fence)
{
    std::lock_guard lock(mutex);
    auto result = originalCreateFence(device, info, allocator, fence);
    if (auto state = FindDevice(device);
        result == VK_SUCCESS && state && fence && state->fences.size() < MaxSyncObjects)
        state->fences[*fence] = { ++state->nextGeneration };
    return result;
}

VKAPI_ATTR void VKAPI_CALL HookDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* allocator)
{
    std::lock_guard lock(mutex);
    if (auto state = FindDevice(device))
    {
        PollDevice(*state);
        state->fences.erase(fence);
    }
    originalDestroyFence(device, fence, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL HookResetFences(VkDevice device, uint32_t count, const VkFence* fences)
{
    std::lock_guard lock(mutex);
    // Drain the old signaled generations before reset; a later signal belongs to a different submission.
    if (auto state = FindDevice(device))
        PollDevice(*state);
    auto result = originalResetFences(device, count, fences);
    if (auto state = FindDevice(device); result == VK_SUCCESS && state && fences)
        for (uint32_t i = 0; i < count; ++i)
            if (auto it = state->fences.find(fences[i]); it != state->fences.end())
                it->second.generation = ++state->nextGeneration;
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL HookQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo* submits,
                                               VkFence fence)
{
    SubmissionRecords pending;
    {
        std::lock_guard lock(mutex);
        if (submits)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto& submit = submits[i];
                auto timeline = FindNext<VkTimelineSemaphoreSubmitInfo>(
                    submit.pNext, VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO);
                const bool valuesValid = timeline &&
                                         timeline->signalSemaphoreValueCount == submit.signalSemaphoreCount &&
                                         timeline->pSignalSemaphoreValues;
                std::array<Signal, MaxSignals> signals {};
                const auto signalCount = std::min<size_t>(submit.signalSemaphoreCount, signals.size());
                for (size_t j = 0; j < signalCount; ++j)
                    signals[j] = { submit.pSignalSemaphores[j], valuesValid ? timeline->pSignalSemaphoreValues[j] : 0,
                                   valuesValid };

                // The bounded stack array must not hide a later signal of a semaphore used by an older sample.
                for (size_t j = signalCount; j < submit.signalSemaphoreCount; ++j)
                    InvalidateUnobservedSignal(submit.pSignalSemaphores[j]);

                const bool supported =
                    submit.signalSemaphoreCount <= MaxSignals &&
                    !FindNext<VkDeviceGroupSubmitInfo>(submit.pNext, VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO);
                ObserveSubmit(queue, fence, submit.pCommandBuffers, submit.commandBufferCount, signals.data(),
                              signalCount, supported, pending);
            }
        }
    }
    // Keep independent game/FG queue submissions concurrent. Metadata is pending until this call
    // returns successfully; lifecycle callbacks cannot publish or recycle that pending recording.
    const auto result = originalQueueSubmit(queue, count, submits, fence);
    {
        std::lock_guard lock(mutex);
        CompleteSubmit(pending, result);
    }
    return result;
}

VkResult ObserveQueueSubmit2(PFN_vkQueueSubmit2 submitFunction, VkQueue queue, uint32_t count,
                             const VkSubmitInfo2* submits, VkFence fence)
{
    SubmissionRecords pending;
    {
        std::lock_guard lock(mutex);
        if (submits)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto& submit = submits[i];
                std::array<Signal, MaxSignals> signals {};
                const auto signalCount = std::min<size_t>(submit.signalSemaphoreInfoCount, signals.size());
                for (size_t j = 0; j < signalCount; ++j)
                {
                    const auto& signal = submit.pSignalSemaphoreInfos[j];
                    const bool allCommands =
                        (signal.stageMask & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) != 0 && signal.deviceIndex == 0;
                    signals[j] = { signal.semaphore, signal.value, allCommands };
                }
                for (size_t j = signalCount; j < submit.signalSemaphoreInfoCount; ++j)
                    InvalidateUnobservedSignal(submit.pSignalSemaphoreInfos[j].semaphore);
                const bool supported =
                    submit.signalSemaphoreInfoCount <= MaxSignals && (submit.flags & VK_SUBMIT_PROTECTED_BIT) == 0;
                // Observe signals once per submit even when there are no command buffers, so another queue's
                // signal cannot make an earlier FG token appear complete.
                ObserveSubmit(queue, fence, nullptr, 0, signals.data(), signalCount, supported, pending);
                for (uint32_t j = 0; j < submit.commandBufferInfoCount; ++j)
                {
                    auto commandBuffer = submit.pCommandBufferInfos[j].commandBuffer;
                    ObserveSubmit(queue, fence, &commandBuffer, 1, nullptr, 0,
                                  supported && submit.pCommandBufferInfos[j].deviceMask == 1, pending);
                    // The signal ledger is already updated, so attach the matching current token explicitly.
                    BufferState* buffer = nullptr;
                    if (auto device = FindBuffer(commandBuffer, buffer);
                        device && buffer->sample && !buffer->quarantined)
                        for (size_t k = 0; k < signalCount; ++k)
                            if (auto it = device->semaphores.find(signals[k].semaphore);
                                it != device->semaphores.end() && it->second.timeline && it->second.gpuOnly &&
                                it->second.queue == queue && signals[k].allCommands &&
                                it->second.lastSignal == signals[k].value)
                            {
                                auto& sample = *buffer->sample;
                                sample.semaphore = signals[k].semaphore;
                                sample.semaphoreValue = signals[k].value;
                                sample.semaphoreGeneration = it->second.generation;
                                break;
                            }
                }
            }
        }
    }
    const auto result = submitFunction(queue, count, submits, fence);
    {
        std::lock_guard lock(mutex);
        CompleteSubmit(pending, result);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL HookQueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits,
                                                VkFence fence)
{
    return ObserveQueueSubmit2(originalQueueSubmit2, queue, count, submits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL HookQueueSubmit2KHR(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits,
                                                   VkFence fence)
{
    return ObserveQueueSubmit2(originalQueueSubmit2KHR, queue, count, submits, fence);
}

struct HookEntry
{
    const char* name;
    PVOID* original;
    PVOID hook;
};

#define HOOK_ENTRY(name, type)                                                                                         \
    { "vk" #name, reinterpret_cast<PVOID*>(&original##name), reinterpret_cast<PVOID>(Hook##name) },
HookEntry entries[] = { FG_VK_FUNCTIONS(HOOK_ENTRY) };
#undef HOOK_ENTRY
std::array<PFN_vkVoidFunction, std::size(entries)> implementation {};
bool hooked = false;
} // namespace

bool FgGpuTime_Vk::Hook(VkDevice device, VkPhysicalDevice physicalDevice)
{
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE)
        return false;

    std::lock_guard lock(mutex);
    if (devices.contains(device))
        return true;
    if (devices.size() >= MaxDevices)
        return false;

    std::array<PFN_vkVoidFunction, std::size(entries)> functions {};
    bool hasHostSignal = false;
    for (size_t i = 0; i < std::size(entries); ++i)
    {
        functions[i] = VulkanHooks::GetDeviceProcAddr(device, entries[i].name);
        const bool hostSignal = std::strcmp(entries[i].name, "vkSignalSemaphore") == 0 ||
                                std::strcmp(entries[i].name, "vkSignalSemaphoreKHR") == 0;
        const bool optional = hostSignal || std::strcmp(entries[i].name, "vkQueueSubmit2") == 0 ||
                              std::strcmp(entries[i].name, "vkQueueSubmit2KHR") == 0;
        hasHostSignal |= hostSignal && functions[i] != nullptr;
        if ((!functions[i] && !optional) || (hooked && functions[i] != implementation[i]))
            return false;
    }
    if (!hasHostSignal)
        return false;

    DeviceState state;
    state.device = device;
#define LOAD_QUERY(member, type, name)                                                                                 \
    state.member = reinterpret_cast<type>(VulkanHooks::GetDeviceProcAddr(device, name));
    LOAD_QUERY(createQueryPool, PFN_vkCreateQueryPool, "vkCreateQueryPool")
    LOAD_QUERY(destroyQueryPool, PFN_vkDestroyQueryPool, "vkDestroyQueryPool")
    LOAD_QUERY(resetQueries, PFN_vkCmdResetQueryPool, "vkCmdResetQueryPool")
    LOAD_QUERY(writeTimestamp, PFN_vkCmdWriteTimestamp, "vkCmdWriteTimestamp")
    LOAD_QUERY(getQueries, PFN_vkGetQueryPoolResults, "vkGetQueryPoolResults")
    LOAD_QUERY(getFenceStatus, PFN_vkGetFenceStatus, "vkGetFenceStatus")
    LOAD_QUERY(getCounter, PFN_vkGetSemaphoreCounterValue, "vkGetSemaphoreCounterValue")
#undef LOAD_QUERY
    if (!state.getCounter)
        state.getCounter = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(
            VulkanHooks::GetDeviceProcAddr(device, "vkGetSemaphoreCounterValueKHR"));
    if (!state.createQueryPool || !state.destroyQueryPool || !state.resetQueries || !state.writeTimestamp ||
        !state.getQueries || !state.getFenceStatus || !state.getCounter)
        return false;

    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    state.timestampPeriod = properties.limits.timestampPeriod;
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    if (familyCount == 0 || familyCount > 256 || !std::isfinite(state.timestampPeriod) || state.timestampPeriod <= 0.0)
        return false;
    state.families.resize(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, state.families.data());
    state.families.resize(familyCount);

    if (!hooked)
    {
        FgTimingHookThreads threads;
        if (!threads.Prepare())
            return false;
        auto result = DetourTransactionBegin();
        if (result != NO_ERROR)
            return false;
        std::array<bool, std::size(entries)> selected {};
        for (size_t i = 0; result == NO_ERROR && i < std::size(entries); ++i)
        {
            *entries[i].original = reinterpret_cast<PVOID>(functions[i]);
            selected[i] = functions[i] != nullptr &&
                          std::find(functions.begin(), functions.begin() + i, functions[i]) == functions.begin() + i;
            if (selected[i])
                result = DetourAttach(entries[i].original, entries[i].hook);
        }
        if (result == NO_ERROR)
            result = threads.Enlist();
        if (result != NO_ERROR)
        {
            DetourTransactionAbort();
            return false;
        }
        if (DetourTransactionCommit() != NO_ERROR)
        {
            DetourTransactionAbort();
            return false;
        }
        implementation = functions;
        hooked = true;
    }

    devices.emplace(device, std::move(state));
    return true;
}

void FgGpuTime_Vk::RegisterQueue(VkDevice device, VkQueue queue, uint32_t family)
{
    std::lock_guard lock(mutex);
    if (auto state = FindDevice(device); state && queue != VK_NULL_HANDLE && family < state->families.size())
    {
        if (auto it = state->queues.find(queue); it != state->queues.end())
        {
            if (it->second != family)
                it->second = UINT32_MAX; // Conflicting provenance stays unavailable until ForgetDevice.
        }
        else if (state->queues.size() < MaxQueues)
            state->queues.emplace(queue, family);
    }
}

void FgGpuTime_Vk::Poll()
{
    std::unique_lock lock(mutex, std::try_to_lock);
    if (lock.owns_lock())
        for (auto& [handle, device] : devices)
            PollDevice(device);
}

void FgGpuTime_Vk::ForgetDevice(VkDevice device)
{
    std::lock_guard lock(mutex);
    auto it = devices.find(device);
    if (it == devices.end())
        return;

    RetireDevice(device);

    // Ordinary game threads can still be inside these observers after SDK workers have joined,
    // including callbacks which captured an original trampoline before waiting for the metadata
    // lock. Keep trampolines installed for process lifetime; only timing objects/metadata retire.
}

ScopedFgGpuTime_Vk::ScopedFgGpuTime_Vk(VkDevice device, VkCommandBuffer commandBuffer,
                                       std::shared_ptr<FgTiming::Channel> channel, uint64_t frameId,
                                       uint32_t generatedFrames)
{
    if (device == VK_NULL_HANDLE || commandBuffer == VK_NULL_HANDLE || !channel || frameId == 0)
        return;

    std::lock_guard lock(mutex);
    auto state = FindDevice(device);
    if (!state)
        return;
    auto it = state->buffers.find(commandBuffer);
    if (it == state->buffers.end())
        return;

    auto& buffer = it->second;
    if (!buffer.primary || !buffer.recording || !buffer.oneTime || !buffer.invalidated || buffer.quarantined ||
        buffer.sample || buffer.family >= state->families.size() ||
        state->families[buffer.family].timestampValidBits == 0 ||
        state->families[buffer.family].timestampValidBits > 64)
        return;

    if (buffer.queries == VK_NULL_HANDLE)
    {
        if (state->queryCount >= MaxQueryPools)
            return;
        VkQueryPoolCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = 2;
        if (state->createQueryPool(device, &info, nullptr, &buffer.queries) != VK_SUCCESS)
            return;
        ++state->queryCount;
    }

    Sample sample;
    sample.channel = std::move(channel);
    sample.epoch = sample.channel->Epoch();
    sample.frameId = frameId;
    sample.generatedFrames = generatedFrames;
    sample.serial = ++nextSerial;
    buffer.sample = std::move(sample);
    buffer.invalidated = false;
    state->resetQueries(commandBuffer, buffer.queries, 0, 2);
    state->writeTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, buffer.queries, 0);
    _device = device;
    _commandBuffer = commandBuffer;
    _serial = buffer.sample->serial;
}

ScopedFgGpuTime_Vk::~ScopedFgGpuTime_Vk() { End(false); }

void ScopedFgGpuTime_Vk::End(bool success)
{
    const auto serial = std::exchange(_serial, uint64_t { 0 });
    if (serial == 0)
        return;

    std::lock_guard lock(mutex);
    auto device = FindDevice(_device);
    if (!device)
        return;
    auto it = device->buffers.find(_commandBuffer);
    if (it == device->buffers.end() || !it->second.sample || it->second.sample->serial != serial ||
        !it->second.recording)
        return;

    auto& buffer = it->second;
    device->writeTimestamp(_commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, buffer.queries, 1);
    buffer.sample->ended = true;
    buffer.sample->successful = success;
}
