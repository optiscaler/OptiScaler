#pragma once

#include <dxgi.h>
#include <d3d12.h>

#include <magic_enum.hpp>
#include "low_latency/ll_util.h"

#define INVALID_ID 0xFFFFFFFFFFFFFFFF

enum class CallSpot
{
    SleepCall = 0,
    InputSample = 1,
    SimulationStart = 2
};

struct SleepParams
{
    bool low_latency_enabled;
    bool low_latency_boost;
    uint32_t minimum_interval_us; // 0 -> no fps limit
    uint32_t sleep_interval_us;   // length of the last sleep
    bool fullscreen_vrr;
    bool control_panel_vsync_override;
    bool use_game_sleep;
    bool fullscreen_i_flip;
    uint8_t fg_multiplier;
};

struct SleepMode
{
    bool low_latency_enabled;
    bool low_latency_boost;
    uint32_t minimum_interval_us; // 0 -> no fps limit
    bool use_markers_to_optimize; // TODO: log this if false
    bool use_min_queue_time;

    bool operator==(const SleepMode&) const = default;
};

enum class MarkerType
{
    SIMULATION_START = 0,
    SIMULATION_END = 1,
    RENDERSUBMIT_START = 2,
    RENDERSUBMIT_END = 3,
    PRESENT_START = 4,
    PRESENT_END = 5,
    INPUT_SAMPLE = 6,
    TRIGGER_FLASH = 7,
    PC_LATENCY_PING = 8,
    OUT_OF_BAND_RENDERSUBMIT_START = 9,
    OUT_OF_BAND_RENDERSUBMIT_END = 10,
    OUT_OF_BAND_PRESENT_START = 11,
    OUT_OF_BAND_PRESENT_END = 12,
};

struct MarkerParams
{
    uint64_t frame_id;
    MarkerType marker_type;
    std::optional<bool> interpolated {}; // Frame type of OUT_OF_BAND_PRESENT_START when the input knows it (AntiLag 2)
};

class LowLatencyTech
{
  protected:
    CallSpot current_call_spot = CallSpot::SimulationStart;
    ForceReflex low_latency_override = ForceReflex::InGame;
    bool low_latency_enabled = false;
    bool effective_fg_state = false;
    bool forced_mode = false;

  public:
    LowLatencyTech()
        : current_call_spot(CallSpot::SimulationStart), low_latency_override(ForceReflex::InGame),
          low_latency_enabled(false), effective_fg_state(false), forced_mode(false)
    {
    }
    virtual ~LowLatencyTech() {}

    virtual bool init(IUnknown* pDevice) = 0;
    virtual void deinit() = 0;

    virtual LowLatencyMode get_mode() = 0;
    virtual void* get_tech_context() = 0;
    virtual void set_fg_type(bool interpolated, uint64_t frame_id) = 0;
    virtual void set_low_latency_override(ForceReflex low_latency_override) = 0;
    virtual void set_effective_fg_state(bool effective_fg_state) = 0;
    virtual void set_forced_mode(bool forced_mode) { this->forced_mode = forced_mode; };

    virtual bool is_enabled() = 0;

    virtual void get_sleep_status(SleepParams* sleep_params) = 0;
    virtual void set_sleep_mode(SleepMode* sleep_mode) = 0;
    virtual void sleep(std::optional<uint32_t> frame_id = std::nullopt) = 0;
    virtual void set_marker(IUnknown* pDevice, const MarkerParams& marker_params) = 0;
    virtual void set_async_marker(IUnknown* pCommandQueue, const MarkerParams& marker_params) = 0;
};