#pragma once

#include "low_latency_tech.h"

#include <vulkan/vulkan.h>

class AntiLagVk : public LowLatencyTech
{
  private:
    VkDevice device = VK_NULL_HANDLE;
    uint32_t max_fps = 0;

    // The input stage goes on simulation start markers with frame ids, without them on the sleep calls
    uint32_t sleeps_since_id_marker = UINT32_MAX / 2;
    std::optional<uint64_t> last_input_frame_id;

    // Out of band presents are the ones that reach the screen with frame generation
    size_t call_count = 0;
    size_t last_oob_present = 0;
    bool using_oob_present = false;

    void update(VkAntiLagStageAMD stage, std::optional<uint64_t> frame_id);

  public:
    AntiLagVk() : LowLatencyTech() {}

    // From LowLatencyTech
    bool init(IUnknown* pDevice) override;
    void deinit() override {}; // Not used by AntiLag VK

    LowLatencyMode get_mode() override { return LowLatencyMode::AntiLagVk; };
    void* get_tech_context() override;
    void set_fg_type(bool interpolated, uint64_t frame_id) override {}; // Not used by AntiLag VK
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
    void set_async_marker(IUnknown* pCommandQueue, const MarkerParams& marker_params) override
    {
        set_marker(nullptr, marker_params);
    };
};
