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
