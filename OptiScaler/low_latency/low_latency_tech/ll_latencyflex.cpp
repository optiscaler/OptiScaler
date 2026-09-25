#include "pch.h"
#include "ll_latencyflex.h"
#include "config.h"
#include <nvapi/fakenvapi/log.h>

void LatencyFlex::lfx_sleep(uint64_t reflex_frame_id)
{
    if (!is_enabled())
        return;

    deinit_mutex.lock();

    static LFXMode previous_lfx_mode = (LFXMode) Config::Instance()->FN_LatencyFlexMode.value_or_default();
    LFXMode lfx_mode = (LFXMode) Config::Instance()->FN_LatencyFlexMode.value_or_default();

    if (previous_lfx_mode != lfx_mode)
        needs_reset = true;

    previous_lfx_mode = lfx_mode;

    if (needs_reset)
    {
        LOG_INFO("LFX Reset");
        eepy(200000000ULL);
        frame_id = 1;
        needs_reset = false;

        if (ctx)
            ctx->Reset();
    }

    uint64_t current_timestamp = Util::GetTimestamp();
    uint64_t timestamp;

    // Set FPS Limiter
    ctx->target_frame_time = 1000 * minimum_interval_us;

    if (lfx_mode == LFXMode::Conservative)
        lfx_end_frame(INVALID_ID); // it should not be using this frame id in the conservative mode

    mutex.lock();
    auto local_frame_id = lfx_mode == LFXMode::ReflexIDs ? reflex_frame_id : this->frame_id + 1;
    // log_event("lfx_get_wait_target", "{}", frame_id);
    if (ctx)
        target = ctx->GetWaitTarget(local_frame_id);
    mutex.unlock();

    if (target > current_timestamp)
    {
        static uint64_t timeout_events = 0;
        uint64_t timeout_timestamp = current_timestamp + 50000000ULL;
        if (target > timeout_timestamp)
        {
            // log_event("lfx_target_high", "{}", target - timeout_timestamp);
            timestamp = timeout_timestamp;
            timeout_events++;
            needs_reset = timeout_events > 5;
        }
        else
        {
            timestamp = target;
            timeout_events = 0;
        }
        // log_event("lfx_sleep", "{}", timestamp - current_timestamp);
        if (auto res = eepy(timestamp - current_timestamp); res)
            LOG_ERROR("Sleep command failed: {}", res);
    }
    else
    {
        timestamp = current_timestamp;
    }

    LOG_TRACE_LOWLATENCY("LatencyFlex Call Spot: {}",
                         current_call_spot == CallSpot::SimulationStart ? "SimulationStart" : "SleepCall");

    mutex.lock();
    this->frame_id++;
    // log_event("lfx_beginframe", "{}", frame_id);
    if (ctx)
        ctx->BeginFrame(local_frame_id, target, timestamp);
    mutex.unlock();

    deinit_mutex.unlock();
}

void LatencyFlex::lfx_end_frame(uint64_t reflex_frame_id)
{
    auto current_timestamp = Util::GetTimestamp();
    mutex.lock();
    auto frame_id = (LFXMode) Config::Instance()->FN_LatencyFlexMode.value_or_default() == LFXMode::ReflexIDs
                        ? reflex_frame_id
                        : this->frame_id;
    // log_event("lfx_endframe", "{}", frame_id);
    if (ctx)
        ctx->EndFrame(frame_id, current_timestamp, &latency, &frame_time);
    mutex.unlock();
    LOG_TRACE_LOWLATENCY("LFX latency: {}, frame_time: {}, current_timestamp: {}", latency, frame_time,
                         current_timestamp);
}

bool LatencyFlex::init(IUnknown* pDevice)
{
    if (!ctx)
    {
        ctx = new lfx::LatencyFleX();
        LOG_INFO("LatencyFleX initialized");
        return true;
    }

    LOG_ERROR("LatencyFleX already initialized, this should not happen");
    return false;
};

void LatencyFlex::deinit()
{
    deinit_mutex.lock();

    if (ctx)
    {
        delete ctx;
        ctx = nullptr;
        LOG_INFO("LatencyFlex deinitialized");
    }

    deinit_mutex.unlock();
};

void* LatencyFlex::get_tech_context() { return ctx; };

void LatencyFlex::get_sleep_status(SleepParams* sleep_params)
{
    sleep_params->low_latency_enabled = is_enabled();
    sleep_params->fullscreen_vrr = true;
    sleep_params->control_panel_vsync_override = false;
};

void LatencyFlex::set_sleep_mode(SleepMode* sleep_mode)
{
    // UNUSED:
    // low_latency_boost
    // use_markers_to_optimize

    low_latency_enabled = sleep_mode->low_latency_enabled;
    minimum_interval_us = sleep_mode->minimum_interval_us;
};

void LatencyFlex::sleep(std::optional<uint32_t> frame_id)
{
    if ((LFXMode) Config::Instance()->FN_LatencyFlexMode.value_or_default() != LFXMode::ReflexIDs)
    {
        last_sleep_framecount = simulation_framecount;

        if (current_call_spot == CallSpot::SleepCall)
        {
            if (frame_id.has_value())
                lfx_sleep(frame_id.value());
            else
                lfx_sleep(INVALID_ID);
        }
    }
};

void LatencyFlex::set_marker(IUnknown* pDevice, const MarkerParams& marker_params)
{
    switch (marker_params.marker_type)
    {
    case MarkerType::SIMULATION_START:
        simulation_framecount++;

        if (last_sleep_framecount + call_spot_switch_threshold < simulation_framecount)
            current_call_spot = CallSpot::SimulationStart;
        else
            current_call_spot = CallSpot::SleepCall;

        if (current_call_spot == CallSpot::SimulationStart)
            lfx_sleep(marker_params.frame_id);
        break;

    case MarkerType::RENDERSUBMIT_END:
        if ((LFXMode) Config::Instance()->FN_LatencyFlexMode.value_or_default() != LFXMode::Conservative)
            lfx_end_frame(marker_params.frame_id);
        break;
    }
};
