#pragma once

#include "low_latency_tech.h"
#include <latencyflex.h>

class LatencyFlex : public LowLatencyTech
{
  private:
    lfx::LatencyFleX* ctx = nullptr;

    std::mutex mutex;
    std::mutex deinit_mutex;

    uint32_t minimum_interval_us = 0;

    uint64_t last_sleep_framecount = 0;
    uint64_t simulation_framecount = 0;
    const uint64_t call_spot_switch_threshold = 20;

    uint64_t latency = 0;
    uint64_t frame_time = 1;
    uint64_t target = 0;
    uint64_t frame_id = 0;
    bool needs_reset = false;

    void lfx_sleep(uint64_t frame_id);
    void lfx_end_frame(uint64_t frame_id);

  public:
    LatencyFlex() : LowLatencyTech() {}

    // From LowLatencyTech
    bool init(IUnknown* pDevice) override;
    void deinit() override;

    LowLatencyMode get_mode() override { return LowLatencyMode::LatencyFlex; };
    void* get_tech_context() override;
    void set_fg_type(bool interpolated, uint64_t frame_id) override {}; // Not used by LFX
    void set_low_latency_override(ForceReflex low_latency_override) override
    {
        this->low_latency_override = low_latency_override;
    };
    void set_effective_fg_state(bool effective_fg_state) override { this->effective_fg_state = effective_fg_state; };

    bool is_enabled() override
    {
        return low_latency_override != ForceReflex::InGame ? low_latency_override == ForceReflex::ForceEnable
                                                           : low_latency_enabled;
    };

    void get_sleep_status(SleepParams* sleep_params) override;
    void set_sleep_mode(SleepMode* sleep_mode) override;
    void sleep(std::optional<uint32_t> frame_id) override;
    void set_marker(IUnknown* pDevice, const MarkerParams& marker_params) override;
    void set_async_marker(IUnknown* pCommandQueue, const MarkerParams& marker_params) override {}; // Not used by LFX
};
