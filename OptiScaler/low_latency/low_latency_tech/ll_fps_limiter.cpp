#include "pch.h"
#include "ll_fps_limiter.h"

void FpsLimiter::limit()
{
    uint64_t interval_ns = 1000ULL * minimum_interval_us;
    uint64_t now = Util::GetTimestamp();

    if (interval_ns == 0 || !is_limiting())
    {
        next_frame_timestamp = 0;
        return;
    }

    // Behind schedule (or way ahead after a change), start pacing from now instead of catching up
    if (next_frame_timestamp <= now || next_frame_timestamp - now > interval_ns)
    {
        next_frame_timestamp = now + interval_ns;
        return;
    }

    if (auto res = eepy(next_frame_timestamp - now); res)
        LOG_ERROR("Sleep command failed: {}", res);

    next_frame_timestamp += interval_ns;
}

void FpsLimiter::get_sleep_status(SleepParams* sleep_params)
{
    sleep_params->low_latency_enabled = is_enabled();
    sleep_params->fullscreen_vrr = true;
    sleep_params->control_panel_vsync_override = false;
};

void FpsLimiter::set_sleep_mode(SleepMode* sleep_mode)
{
    low_latency_enabled = sleep_mode->low_latency_enabled;
    minimum_interval_us = sleep_mode->minimum_interval_us;
};

void FpsLimiter::sleep(std::optional<uint32_t> frame_id)
{
    last_sleep_framecount = simulation_framecount;
    limit();
};

void FpsLimiter::set_marker(IUnknown* pDevice, const MarkerParams& marker_params)
{
    if (marker_params.marker_type != MarkerType::SIMULATION_START)
        return;

    simulation_framecount++;

    // Only when the game doesn't send sleep calls
    if (last_sleep_framecount + sleep_call_timeout < simulation_framecount)
        limit();
};
