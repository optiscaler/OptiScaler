#pragma once

#include "low_latency/low_latency_tech/low_latency_tech.h"

#include <dxgi.h>
#include <d3d12.h>

#include "low_latency/ll_util.h"
#include <optional>

class LowLatency
{
  private:
    std::atomic<std::shared_ptr<LowLatencyTech>> currently_active_tech;
    FrameReport frame_reports[FRAME_REPORTS_BUFFER_SIZE] {};
    bool fg;
    uint32_t delay_deinit = 0;
    SleepMode last_sleep_mode {};

    void update_effective_fg_state();
    void update_enabled_override();

    // D3D
    bool update_low_latency_tech(IUnknown* pDevice);
    void get_latency_result(NV_LATENCY_RESULT_PARAMS* pGetLatencyParams);
    void add_marker_to_report(NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams);

    // Vulkan
    bool update_low_latency_tech(HANDLE vkDevice);
    void get_latency_result(NV_VULKAN_LATENCY_RESULT_PARAMS* pGetLatencyParams);
    void add_marker_to_report(NV_VULKAN_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams);

  public:
    LowLatency() = default;
    ~LowLatency() { deinit_current_tech(); };

    bool deinit_current_tech();
    bool get_low_latency_tech_context(void** low_latency_tech_context, LowLatencyMode* low_latency_tech);

    bool is_low_latency_enabled();

    // D3D
    NvAPI_Status Sleep(IUnknown* pDevice);
    NvAPI_Status SetSleepMode(IUnknown* pDevice, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams);
    NvAPI_Status GetSleepStatus(IUnknown* pDevice, NV_GET_SLEEP_STATUS_PARAMS* pGetSleepStatusParams);
    NvAPI_Status SetLatencyMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams);
    NvAPI_Status SetAsyncFrameMarker(ID3D12CommandQueue* pCommandQueue,
                                     NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams);
    NvAPI_Status GetLatency(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams);

    // Vulkan
    NvAPI_Status Sleep(HANDLE vkDevice);
    NvAPI_Status SetSleepMode(HANDLE vkDevice, NV_VULKAN_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams);
    NvAPI_Status GetSleepStatus(HANDLE vkDevice, NV_VULKAN_GET_SLEEP_STATUS_PARAMS* pGetSleepStatusParams);
    NvAPI_Status SetLatencyMarker(HANDLE vkDevice, NV_VULKAN_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams);
    NvAPI_Status GetLatency(HANDLE vkDevice, NV_VULKAN_LATENCY_RESULT_PARAMS* pGetLatencyParams);
};

// This is a wrapper for the entire LowLatency abstraction layer
// If you need a specific *tech* context then you need to query this abstraction layer
class LowLatencyCtx
{
  public:
    static void init()
    {
        if (!lowlatency_ctx)
            lowlatency_ctx = new LowLatency();
    }

    static void shutdown()
    {
        delete lowlatency_ctx;
        lowlatency_ctx = nullptr;
    }

    static LowLatency* get() { return lowlatency_ctx; }

  private:
    static LowLatency* lowlatency_ctx;
};
