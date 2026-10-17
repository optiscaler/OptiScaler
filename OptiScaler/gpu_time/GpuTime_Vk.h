#pragma once

#include "SysUtils.h"
#include <vulkan/vulkan.h>
#include <optional>
#include <array>
#include <cstdint>
#include <mutex>

class GpuTime_Vk
{
    static constexpr int QUERY_BUFFER_COUNT = 3;

    VkDevice _device = VK_NULL_HANDLE;
    VkQueryPool _queryPool = VK_NULL_HANDLE;
    double _timestampPeriod = 1.0;
    std::array<bool, QUERY_BUFFER_COUNT> _trigger {};

    int _currentFrameIndex = 0;
    bool _init = false;

    // The Vulkan w/Dx12 path records the start in the game's CB and the end in a copy-back CB.
    // A slot cannot be reused until that copy-back submit signals its timeline semaphore.
    enum class InteropSlotState : uint8_t
    {
        Free,
        Recording,
        Ended,
        Submitted,
        Quarantined
    };

    struct InteropSlot
    {
        InteropSlotState state = InteropSlotState::Free;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        uint64_t value = 0;
        uint64_t serial = 0;
    };

    std::array<InteropSlot, QUERY_BUFFER_COUNT> _interopSlots {};
    std::mutex _interopMutex;
    PFN_vkGetSemaphoreCounterValue _getSemaphoreCounterValue = nullptr;
    bool _counterProcChecked = false;
    int _recordingInteropSlot = -1;
    uint64_t _nextInteropSerial = 0;
    uint64_t _lastReadyInteropSerial = 0;
    std::optional<double> _readyInteropTime;
    uint64_t _skippedInteropSamples = 0;

    void PollInteropLocked();
    bool ResolveInteropCounterLocked();

  public:
    GpuTime_Vk(VkDevice device, VkPhysicalDevice physicalDevice);
    ~GpuTime_Vk();

    void Start(VkCommandBuffer cmdBuffer);
    void End(VkCommandBuffer cmdBuffer);

    std::optional<double> ReadGpuTime();

    // Vulkan w/Dx12-only timing API. The ordinary Start/End/ReadGpuTime path is unchanged.
    bool StartInterop(VkCommandBuffer cmdBuffer);
    void EndInterop(VkCommandBuffer cmdBuffer);
    void SubmitInterop(VkSemaphore copyBackSemaphore, uint64_t copyBackValue);
    void AbortInterop();
    std::optional<double> ReadInteropGpuTime();
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
