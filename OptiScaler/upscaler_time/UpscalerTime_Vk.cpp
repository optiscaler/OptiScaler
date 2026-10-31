#include "pch.h"
#include "UpscalerTime_Vk.h"

#include <State.h>

void UpscalerTimeVk::Init(VkDevice device, VkPhysicalDevice pd)
{
    VkQueryPoolCreateInfo queryPoolInfo = {};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = 2; // Start and End timestamps

    vkCreateQueryPool(device, &queryPoolInfo, nullptr, &_queryPool);

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(pd, &deviceProperties);
    _timeStampPeriod = deviceProperties.limits.timestampPeriod;
}

void UpscalerTimeVk::UpscaleStart(VkCommandBuffer cmdBuffer)
{
    if (_queryPool == VK_NULL_HANDLE)
        return;

    vkCmdResetQueryPool(cmdBuffer, _queryPool, 0, 2);
    vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, _queryPool, 0);
}

void UpscalerTimeVk::UpscaleEnd(VkCommandBuffer cmdBuffer)
{
    if (_queryPool == VK_NULL_HANDLE)
        return;

    vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _queryPool, 1);
    _vkUpscaleTrig = true;
}

void UpscalerTimeVk::ReadUpscalingTime(VkDevice device)
{
    if (_vkUpscaleTrig && _queryPool != VK_NULL_HANDLE)
    {
        // Retrieve timestamps
        // Stays zero (skipped below) when results are VK_NOT_READY
        uint64_t timestamps[2] {};
        vkGetQueryPoolResults(device, _queryPool, 0, 2, sizeof(timestamps), timestamps, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT);

        // Calculate elapsed time in milliseconds
        double elapsedTimeMs = (timestamps[1] - timestamps[0]) * _timeStampPeriod / 1e6;

        if (elapsedTimeMs > 0.0 && elapsedTimeMs < 5000.0)
        {
            State::Instance().frameTimeMutex.lock();
            State::Instance().upscaleTimes.push_back(elapsedTimeMs);
            State::Instance().upscaleTimes.pop_front();
            State::Instance().frameTimeMutex.unlock();
        }
    }

    _vkUpscaleTrig = false;
}
