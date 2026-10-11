#pragma once

#include "low_latency_tech.h"

// Only limits the FPS in the sleep call (or on the simulation start without sleep calls),
// doesn't try to lower the latency otherwise
class FpsLimiter : public LowLatencyTech
{
  private:
    uint32_t minimum_interval_us = 0;
    uint64_t next_frame_timestamp = 0;

    uint64_t last_sleep_framecount = 0;
    uint64_t simulation_framecount = 0;
    const uint64_t sleep_call_timeout = 20;

    void limit();

  public:
    FpsLimiter() : LowLatencyTech() {}

    // From LowLatencyTech
    bool init(IUnknown* pDevice) override { return true; };
    void deinit() override {};

    LowLatencyMode get_mode() override { return LowLatencyMode::FpsLimiter; };
    void* get_tech_context() override { return nullptr; };
    void set_fg_type(bool interpolated, uint64_t frame_id) override {};
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

    // Limits regardless of the game's low latency state, unless force disabled
    bool is_limiting() { return low_latency_override != ForceReflex::ForceDisable; }

    void get_sleep_status(SleepParams* sleep_params) override;
    void set_sleep_mode(SleepMode* sleep_mode) override;
    void sleep(std::optional<uint32_t> frame_id) override;
    void set_marker(IUnknown* pDevice, const MarkerParams& marker_params) override;
    void set_async_marker(IUnknown* pCommandQueue, const MarkerParams& marker_params) override {};
};
