#pragma once

#include "SysUtils.h"
#include <vulkan/vulkan.h>
#include <optional>
#include <array>

class GpuTime_Vk
{
    static constexpr int QUERY_BUFFER_COUNT = 3;

    VkDevice _device = VK_NULL_HANDLE;
    VkQueryPool _queryPool = VK_NULL_HANDLE;
    double _timestampPeriod = 1.0;
    std::array<bool, QUERY_BUFFER_COUNT> _trigger {};

    int _currentFrameIndex = 0;
    bool _init = false;

  public:
    GpuTime_Vk(VkDevice device, VkPhysicalDevice physicalDevice);
    ~GpuTime_Vk();

    void Start(VkCommandBuffer cmdBuffer);
    void End(VkCommandBuffer cmdBuffer);

    std::optional<double> ReadGpuTime();
};

class ScopedGpuTime_Vk
{
    GpuTime_Vk* _gpuTime;
    VkCommandBuffer _cmdBuffer;

  public:
    ScopedGpuTime_Vk(GpuTime_Vk* gpuTime, VkCommandBuffer cmdBuffer) : _gpuTime(gpuTime), _cmdBuffer(cmdBuffer)
    {
        if (_gpuTime && _cmdBuffer)
            _gpuTime->Start(_cmdBuffer);
    }

    ~ScopedGpuTime_Vk()
    {
        if (_gpuTime && _cmdBuffer)
            _gpuTime->End(_cmdBuffer);
    }
};
