#pragma once

#include "low_latency_tech.h"

#include <ffx_antilag2_dx12.h>
#include <ffx_antilag2_dx11.h>

class AntiLag2 : public LowLatencyTech
{
  private:
    AMD::AntiLag2DX12::Context dx12_ctx = {};
    AMD::AntiLag2DX11::Context dx11_ctx = {};
    ID3D12Device* dx12_device = nullptr;

    std::mutex amdxc64_load_mutex;
    uint64_t amdxc64_load_times = 0;

    uint32_t minimum_interval_us = 0;

    uint64_t last_sleep_framecount = 0;
    uint64_t simulation_framecount = 0;
    const uint64_t call_spot_switch_threshold = 20;

    uint64_t previous_oob_frame_id = INVALID_ID;
    bool previous_interpolated = false;

    HRESULT al2_sleep();

  public:
    AntiLag2() : LowLatencyTech() {}

    // From LowLatencyTech
    bool init(IUnknown* pDevice) override;
    void deinit() override;

    LowLatencyMode get_mode() override { return LowLatencyMode::AntiLag2; };
    void* get_tech_context() override;
    void set_fg_type(bool interpolated, uint64_t frame_id) override;
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
    void set_async_marker(IUnknown* pCommandQueue, const MarkerParams& marker_params) override;
};
