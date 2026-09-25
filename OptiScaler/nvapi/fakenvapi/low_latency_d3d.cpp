#include "pch.h"
#include "low_latency.h"

#include "low_latency/low_latency_tech/ll_antilag2.h"
#include "low_latency/low_latency_tech/ll_latencyflex.h"
#include "low_latency/low_latency_tech/ll_xell.h"

#include "log.h"
#include "config.h"
#include <hooks/Reflex_Hooks.h>
#include <framegen/nvngx/Nvngx_FG.h>

// private
bool LowLatency::update_low_latency_tech(IUnknown* pDevice)
{
    if (!pDevice)
    {
        LOG_ERROR("Invalid pointer");
        return false;
    }

    if (!currently_active_tech.load())
    {
        if (forced_low_latency_tech == LowLatencyMode::XeLL)
        {
            auto new_tech_xell = std::make_shared<XeLL>();
            if (new_tech_xell->init(pDevice))
            {
                LOG_INFO("LowLatency algo: XeLL (forced)");

                new_tech_xell->set_forced_mode(true);

                SleepMode sleepMode {};
                sleepMode.low_latency_enabled = true;
                new_tech_xell->set_sleep_mode(&sleepMode);

                currently_active_tech.store(std::move(new_tech_xell));
                return true;
            }
        }

        // Don't use AL2 and XeLL when using DMFG or MFG
        if (!Config::Instance()->FN_ForceLatencyFlex.value_or_default() &&
            State::Instance().dlssgDetectedInterpolationCount <= 1 &&
            State::Instance().dlssgLastSetMode != sl::DLSSGMode::eDynamic)
        {
            auto new_tech_al2 = std::make_shared<AntiLag2>();
            if (new_tech_al2->init(pDevice))
            {
                LOG_INFO("LowLatency algo: FSR Anti-Lag 2.0");
                new_tech_al2->set_sleep_mode(&last_sleep_mode);
                currently_active_tech.store(std::move(new_tech_al2));
                return true;
            }

            auto new_tech_xell = std::make_shared<XeLL>();
            if (new_tech_xell->init(pDevice))
            {
                LOG_INFO("LowLatency algo: XeLL");
                new_tech_xell->set_sleep_mode(&last_sleep_mode);
                currently_active_tech.store(std::move(new_tech_xell));
                return true;
            }
        }

        auto new_tech = std::make_shared<LatencyFlex>();
        if (new_tech->init(pDevice))
        {
            LOG_INFO("LowLatency algo: LatencyFlex");
            new_tech->set_sleep_mode(&last_sleep_mode);
            currently_active_tech.store(std::move(new_tech));
            return true;
        }
    }

    static bool last_force_latencyflex = Config::Instance()->FN_ForceLatencyFlex.value_or_default();
    bool force_latencyflex = Config::Instance()->FN_ForceLatencyFlex.value_or_default();
    bool change_detected = last_force_latencyflex != force_latencyflex;
    last_force_latencyflex = force_latencyflex;

    // Don't allow for deinit with forced mode
    if (forced_low_latency_tech != LowLatencyMode::None)
        return true;

    if (State::Instance().fakenvapiReloadLowLatency)
    {
        change_detected = true;
        State::Instance().fakenvapiReloadLowLatency = false;
    }

    auto try_reinit = [&]() -> bool
    {
        if (!deinit_current_tech())
        {
            LOG_ERROR("Couldn't deinitialize low latency tech");
            return false;
        }
        return update_low_latency_tech(pDevice);
    };

    // FSR FG might still be using AntiLag 2, give Opti time to set AL2 context to null
    if (change_detected)
    {
        bool al2 = false;
        bool xell = false;
        {
            auto current_tech = currently_active_tech.load();
            if (current_tech)
            {
                auto mode = current_tech->get_mode();
                al2 = mode == LowLatencyMode::AntiLag2;
                xell = mode == LowLatencyMode::XeLL;
            }
        }

        if (al2 || xell)
        {
            delay_deinit = 50;
        }
        else
        {
            return try_reinit();
        }
    }

    if (delay_deinit > 0)
    {
        if (--delay_deinit == 0)
            return try_reinit();
    }

    return true;
}

void LowLatency::get_latency_result(NV_LATENCY_RESULT_PARAMS* pGetLatencyParams)
{
    if (pGetLatencyParams->version != NV_LATENCY_RESULT_PARAMS_VER1)
    {
        LOG_ERROR("GetLatency: Unsupported version {}", pGetLatencyParams->version);
        return;
    }

    // Assume no frame reports collected yet, report all zeros
    if (frame_reports[FRAME_REPORTS_BUFFER_SIZE - 1].frameID == 0)
    {
        std::memset(pGetLatencyParams->frameReport, 0, sizeof(pGetLatencyParams->frameReport));
        // spdlog::warn("GetLatency: Not enough data to report");
        return;
    }

    // Sort frame reports, find the oldest
    size_t minIdx = 0;
    uint64_t minID = frame_reports[0].frameID;
    for (size_t i = 1; i < FRAME_REPORTS_BUFFER_SIZE; i++)
    {
        if (frame_reports[i].frameID < minID)
        {
            minID = frame_reports[i].frameID;
            minIdx = i;
        }
    }

    // Copy starting from older before wrapping around
    size_t firstChunk = std::min<uint64_t>(NVAPI_BUFFER_SIZE, FRAME_REPORTS_BUFFER_SIZE - minIdx);
    std::memcpy(pGetLatencyParams->frameReport, frame_reports + minIdx, firstChunk * sizeof(FrameReport));

    // Copy the rest after wrapping around
    if (firstChunk < NVAPI_BUFFER_SIZE)
    {
        std::memcpy(pGetLatencyParams->frameReport + firstChunk, frame_reports,
                    (NVAPI_BUFFER_SIZE - firstChunk) * sizeof(FrameReport));
    }
}

void LowLatency::add_marker_to_report(NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams)
{
    auto current_timestamp = Util::GetTimestamp() / 1000;
    static auto last_sim_start = current_timestamp;
    static auto _2nd_last_sim_start = current_timestamp;
    auto current_report = &frame_reports[pSetLatencyMarkerParams->frameID % FRAME_REPORTS_BUFFER_SIZE];

    if (current_report->frameID != pSetLatencyMarkerParams->frameID)
    {
        *current_report = FrameReport {};
    }

    current_report->frameID = pSetLatencyMarkerParams->frameID;
    current_report->gpuFrameTimeUs = (uint32_t) (last_sim_start - _2nd_last_sim_start);
    current_report->gpuActiveRenderTimeUs = 100;
    current_report->driverStartTime = current_timestamp;
    current_report->driverEndTime = current_timestamp + 100;
    current_report->gpuRenderStartTime = current_timestamp;
    current_report->gpuRenderEndTime = current_timestamp + 100;
    current_report->osRenderQueueStartTime = current_timestamp;
    current_report->osRenderQueueEndTime = current_timestamp + 100;
    switch (pSetLatencyMarkerParams->markerType)
    {
    case SIMULATION_START:
        _2nd_last_sim_start = last_sim_start;
        last_sim_start = Util::GetTimestamp() / 1000;
        current_report->simStartTime = last_sim_start;
        break;
    case SIMULATION_END:
        current_report->simEndTime = Util::GetTimestamp() / 1000;
        break;
    case RENDERSUBMIT_START:
        current_report->renderSubmitStartTime = Util::GetTimestamp() / 1000;
        break;
    case RENDERSUBMIT_END:
        current_report->renderSubmitEndTime = Util::GetTimestamp() / 1000;
        break;
    case PRESENT_START:
        current_report->presentStartTime = Util::GetTimestamp() / 1000;
        break;
    case PRESENT_END:
        current_report->presentEndTime = Util::GetTimestamp() / 1000;
        break;
    case INPUT_SAMPLE:
        current_report->inputSampleTime = Util::GetTimestamp() / 1000;
        break;
    default:
        break;
    }
}

// public
NvAPI_Status LowLatency::Sleep(IUnknown* pDevice)
{
    if (!update_low_latency_tech(pDevice))
        return ERROR();

    if (auto current_tech = currently_active_tech.load())
        current_tech->sleep();

    return OK();
}

NvAPI_Status LowLatency::SetSleepMode(IUnknown* pDevice, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams)
{
    if (!update_low_latency_tech(pDevice))
        return ERROR();

    last_sleep_mode.low_latency_enabled = pSetSleepModeParams->bLowLatencyMode;
    last_sleep_mode.low_latency_boost = pSetSleepModeParams->bLowLatencyBoost;
    last_sleep_mode.minimum_interval_us = pSetSleepModeParams->minimumIntervalUs;
    last_sleep_mode.use_markers_to_optimize = pSetSleepModeParams->bUseMarkersToOptimize;

    if (auto current_tech = currently_active_tech.load())
        current_tech->set_sleep_mode(&last_sleep_mode);

    return OK();
}

NvAPI_Status LowLatency::GetSleepStatus(IUnknown* pDevice, NV_GET_SLEEP_STATUS_PARAMS* pGetSleepStatusParams)
{
    if (!update_low_latency_tech(pDevice))
        return ERROR();

    SleepParams sleep_params {};

    if (auto current_tech = currently_active_tech.load())
        current_tech->get_sleep_status(&sleep_params);

    pGetSleepStatusParams->bLowLatencyMode = sleep_params.low_latency_enabled;
    pGetSleepStatusParams->bFsVrr = sleep_params.fullscreen_vrr;
    pGetSleepStatusParams->bCplVsyncOn = sleep_params.control_panel_vsync_override;

    return OK();
}

NvAPI_Status LowLatency::SetLatencyMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams)
{
    if (!update_low_latency_tech(pDev))
        return ERROR();

    update_effective_fg_state();

    update_enabled_override();

    add_marker_to_report(pSetLatencyMarkerParams);

    MarkerParams marker_params {};

    marker_params.frame_id = pSetLatencyMarkerParams->frameID;
    marker_params.marker_type = (MarkerType) pSetLatencyMarkerParams->markerType; // requires enums to match

    if (auto current_tech = currently_active_tech.load())
        current_tech->set_marker(pDev, marker_params);

    LOG_TRACE_FAKENVAPI("{}: {}", magic_enum::enum_name(marker_params.marker_type), marker_params.frame_id);

    return NVAPI_OK;
}

NvAPI_Status LowLatency::SetAsyncFrameMarker(ID3D12CommandQueue* pCommandQueue,
                                             NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams)
{
    if (!currently_active_tech.load()) // can't init using ID3D12CommandQueue, can only check if available
        return ERROR();

    MarkerParams marker_params {};

    marker_params.frame_id = pSetAsyncFrameMarkerParams->frameID;
    marker_params.marker_type = (MarkerType) pSetAsyncFrameMarkerParams->markerType; // requires enums to match

    if (marker_params.marker_type == MarkerType::OUT_OF_BAND_PRESENT_START)
    {
        constexpr size_t history_size = 12;
        static size_t counter = 0;
        static NvU64 previous_frame_ids[history_size] = {};

        previous_frame_ids[counter % history_size] = pSetAsyncFrameMarkerParams->frameID;
        counter++;

        int repeat_count = 0;

        for (size_t i = 1; i < history_size; i++)
        {
            // won't catch repeat frame ids across array wrap around
            if (previous_frame_ids[i] == previous_frame_ids[i - 1])
            {
                repeat_count++;
            }
        }

        if (fg && repeat_count == 0)
            fg = false;
        else if (!fg && repeat_count >= history_size / 2 - 1)
            fg = true;

        update_effective_fg_state();
    }

    if (auto current_tech = currently_active_tech.load())
        current_tech->set_async_marker(pCommandQueue, marker_params);

    LOG_TRACE_FAKENVAPI("Async {}: {}", magic_enum::enum_name(marker_params.marker_type), marker_params.frame_id);

    return NVAPI_OK;
}

NvAPI_Status LowLatency::GetLatency(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams)
{
    if (!update_low_latency_tech(pDev))
        return ERROR();

    get_latency_result(pGetLatencyParams);

    return OK();
}
