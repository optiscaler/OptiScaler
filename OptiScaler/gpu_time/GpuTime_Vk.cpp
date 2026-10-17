#include "pch.h"
#include "GpuTime_Vk.h"

#include <State.h>

GpuTime_Vk::GpuTime_Vk(VkDevice device, VkPhysicalDevice physicalDevice) : _device(device)
{
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("GpuTime_Vk created with null device");
        return;
    }

    // Start and End timestamps per buffer
    VkQueryPoolCreateInfo queryPoolInfo {};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = QUERY_BUFFER_COUNT * 2;

    auto result = vkCreateQueryPool(device, &queryPoolInfo, nullptr, &_queryPool);

    if (result != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateQueryPool error: {}", (int) result);
        _queryPool = VK_NULL_HANDLE;
        return;
    }

    VkPhysicalDeviceProperties deviceProperties {};
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    _timestampPeriod = deviceProperties.limits.timestampPeriod;

    _init = true;
}

GpuTime_Vk::~GpuTime_Vk()
{
    if (State::Instance().isShuttingDown)
        return;

    if (_queryPool != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(_device, _queryPool, nullptr);
        _queryPool = VK_NULL_HANDLE;
    }
}

void GpuTime_Vk::Start(VkCommandBuffer cmdBuffer)
{
    if (_init && _queryPool != VK_NULL_HANDLE)
    {
        _currentFrameIndex = (_currentFrameIndex + 1) % QUERY_BUFFER_COUNT;

        vkCmdResetQueryPool(cmdBuffer, _queryPool, _currentFrameIndex * 2, 2);
        vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, _queryPool, _currentFrameIndex * 2);
    }
}

void GpuTime_Vk::End(VkCommandBuffer cmdBuffer)
{
    if (_init && _queryPool != VK_NULL_HANDLE)
    {
        vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _queryPool, _currentFrameIndex * 2 + 1);

        _trigger[_currentFrameIndex] = true;
    }
}

std::optional<double> GpuTime_Vk::ReadGpuTime()
{
    std::optional<double> elapsedTimeMs = std::nullopt;

    if (!_init || _queryPool == VK_NULL_HANDLE)
        return elapsedTimeMs;

    // Try to read the oldest frame's timestamps
    uint32_t previousFrameIndex = (_currentFrameIndex + 1) % QUERY_BUFFER_COUNT;

    if (!_trigger[previousFrameIndex])
        return elapsedTimeMs;

    uint64_t timestamps[2] {};
    auto result = vkGetQueryPoolResults(_device, _queryPool, previousFrameIndex * 2, 2, sizeof(timestamps), timestamps,
                                        sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

    // Results not available yet, the slot will get overwritten by the next Start anyway
    if (result != VK_SUCCESS)
        return elapsedTimeMs;

    _trigger[previousFrameIndex] = false;

    if (timestamps[1] < timestamps[0])
        return elapsedTimeMs;

    elapsedTimeMs = (timestamps[1] - timestamps[0]) * _timestampPeriod / 1e6;

    return elapsedTimeMs;
}

// Interop timestamps are issued on two Vulkan command buffers, separated by D3D12 work.
// Only the copy-back timeline signal proves that BOTH timestamps belong to a finished submit.
bool GpuTime_Vk::ResolveInteropCounterLocked()
{
    if (!_counterProcChecked)
    {
        _counterProcChecked = true;
        if (_device != VK_NULL_HANDLE)
        {
            auto proc = vkGetDeviceProcAddr(_device, "vkGetSemaphoreCounterValue");
            if (!proc)
                proc = vkGetDeviceProcAddr(_device, "vkGetSemaphoreCounterValueKHR");
            _getSemaphoreCounterValue = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(proc);
        }

        if (!_getSemaphoreCounterValue)
            LOG_WARN("Vulkan w/Dx12 GPU timing disabled: timeline semaphore counter query unavailable");
    }

    return _getSemaphoreCounterValue != nullptr;
}

void GpuTime_Vk::PollInteropLocked()
{
    if (!_init || _queryPool == VK_NULL_HANDLE || !ResolveInteropCounterLocked())
        return;

    for (int index = 0; index < QUERY_BUFFER_COUNT; ++index)
    {
        auto& slot = _interopSlots[index];
        if (slot.state != InteropSlotState::Submitted)
            continue;

        uint64_t completedValue = 0;
        const VkResult counterResult = _getSemaphoreCounterValue(_device, slot.semaphore, &completedValue);
        if (counterResult != VK_SUCCESS || completedValue < slot.value)
            continue;

        // The submit's signal follows the copy-back CB's end timestamp. No WAIT_BIT: the CPU never stalls.
        // WITH_AVAILABILITY also guards against invalid/old query contents if a driver has unexpected latency.
        uint64_t timestamps[4] {}; // start, startAvailable, end, endAvailable
        const VkResult queryResult =
            vkGetQueryPoolResults(_device, _queryPool, index * 2, 2, sizeof(timestamps), timestamps,
                                  2 * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

        if (queryResult == VK_NOT_READY)
            continue;

        if (queryResult != VK_SUCCESS)
        {
            LOG_WARN("Vulkan w/Dx12 GPU timestamp query failed: {}", static_cast<int>(queryResult));
            continue;
        }

        if (timestamps[1] == 0 || timestamps[3] == 0)
            continue;

        if (timestamps[2] >= timestamps[0])
        {
            const double milliseconds = (timestamps[2] - timestamps[0]) * _timestampPeriod / 1e6;
            if (milliseconds > 0.0 && milliseconds < 5000.0 && slot.serial > _lastReadyInteropSerial)
            {
                // Keep only the newest valid sample: Nukem may generate several presents per Evaluate.
                _readyInteropTime = milliseconds;
                _lastReadyInteropSerial = slot.serial;
            }
        }

        // Safe to reset only after both a completed timeline value AND available timestamp results.
        slot = {};
    }
}

bool GpuTime_Vk::StartInterop(VkCommandBuffer cmdBuffer)
{
    if (!_init || _queryPool == VK_NULL_HANDLE || cmdBuffer == VK_NULL_HANDLE)
        return false;

    std::lock_guard lock(_interopMutex);
    if (!ResolveInteropCounterLocked())
        return false;

    PollInteropLocked();

    if (_recordingInteropSlot >= 0)
    {
        LOG_WARN("Vulkan w/Dx12 GPU timing received overlapping Evaluate; skipping timing sample");
        return false;
    }

    for (int index = 0; index < QUERY_BUFFER_COUNT; ++index)
    {
        auto& slot = _interopSlots[index];
        if (slot.state != InteropSlotState::Free)
            continue;

        slot.state = InteropSlotState::Recording;
        slot.serial = ++_nextInteropSerial;
        _recordingInteropSlot = index;

        vkCmdResetQueryPool(cmdBuffer, _queryPool, index * 2, 2);
        vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, _queryPool, index * 2);
        return true;
    }

    if (++_skippedInteropSamples <= 4 || (_skippedInteropSamples % 512) == 0)
        LOG_WARN("Vulkan w/Dx12 GPU timing: all {} query slots busy; skipping sample ({})", QUERY_BUFFER_COUNT,
                 _skippedInteropSamples);
    return false;
}

void GpuTime_Vk::EndInterop(VkCommandBuffer cmdBuffer)
{
    std::lock_guard lock(_interopMutex);
    if (_recordingInteropSlot < 0 || cmdBuffer == VK_NULL_HANDLE)
        return;

    auto& slot = _interopSlots[_recordingInteropSlot];
    if (slot.state != InteropSlotState::Recording)
        return;

    vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _queryPool, _recordingInteropSlot * 2 + 1);
    slot.state = InteropSlotState::Ended;
}

void GpuTime_Vk::SubmitInterop(VkSemaphore copyBackSemaphore, uint64_t copyBackValue)
{
    std::lock_guard lock(_interopMutex);
    if (_recordingInteropSlot < 0)
        return;

    auto& slot = _interopSlots[_recordingInteropSlot];
    if (slot.state == InteropSlotState::Ended && copyBackSemaphore != VK_NULL_HANDLE && copyBackValue != 0)
    {
        slot.semaphore = copyBackSemaphore;
        slot.value = copyBackValue;
        slot.state = InteropSlotState::Submitted;
    }
    else
    {
        // The original CB may still be submitted; never reuse a query whose start was recorded there.
        slot.state = InteropSlotState::Quarantined;
    }

    _recordingInteropSlot = -1;
}

void GpuTime_Vk::AbortInterop()
{
    std::lock_guard lock(_interopMutex);
    if (_recordingInteropSlot < 0)
        return;

    // There is no safe GPU completion marker for an aborted interop submit. A start timestamp
    // was already recorded in the application's CB, which may still be submitted later.
    // Quarantine instead of risking an in-flight vkCmdResetQueryPool on the same query slot.
    _interopSlots[_recordingInteropSlot].state = InteropSlotState::Quarantined;
    _recordingInteropSlot = -1;
}

std::optional<double> GpuTime_Vk::ReadInteropGpuTime()
{
    std::lock_guard lock(_interopMutex);
    PollInteropLocked();
    auto result = _readyInteropTime;
    _readyInteropTime.reset();
    return result;
}
