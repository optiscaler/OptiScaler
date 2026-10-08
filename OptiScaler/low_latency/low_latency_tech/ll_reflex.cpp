#include "pch.h"
#include "ll_reflex.h"

#include <hooks/LibraryLoad_Hooks.h>
#include <nvapi/NvApiHooks.h>

bool Reflex::init(IUnknown* pDevice)
{
    if (pDevice == nullptr)
        return false;

    if (NvApiHooks::o_NvAPI_QueryInterface == nullptr)
        LibraryLoadHooks::LoadNvApi();

    // fakenvapi's Reflex is a translation of its own, only the driver's is Reflex
    if (NvApiHooks::o_NvAPI_QueryInterface == nullptr || fakenvapi::isUsingAsMainNvapi())
    {
        LOG_INFO("Reflex needs NVIDIA's nvapi");
        return false;
    }

    auto queryInterface = NvApiHooks::o_NvAPI_QueryInterface;
    ReflexHooks::hookReflex(queryInterface);

    // A game without Reflex might never have initialized nvapi
    if (auto initialize = GET_INTERFACE(NvAPI_Initialize, queryInterface);
        initialize == nullptr || initialize() != NVAPI_OK)
        return false;

    o_SetSleepMode = (decltype(o_SetSleepMode)) ReflexHooks::getHookedReflex(GET_ID(NvAPI_D3D_SetSleepMode));
    o_Sleep = (decltype(o_Sleep)) ReflexHooks::getHookedReflex(GET_ID(NvAPI_D3D_Sleep));
    o_SetLatencyMarker =
        (decltype(o_SetLatencyMarker)) ReflexHooks::getHookedReflex(GET_ID(NvAPI_D3D_SetLatencyMarker));
    o_SetAsyncFrameMarker =
        (decltype(o_SetAsyncFrameMarker)) ReflexHooks::getHookedReflex(GET_ID(NvAPI_D3D12_SetAsyncFrameMarker));
    o_GetLatency = (decltype(o_GetLatency)) ReflexHooks::getHookedReflex(GET_ID(NvAPI_D3D_GetLatency));
    o_GetSleepStatus = GET_INTERFACE(NvAPI_D3D_GetSleepStatus, queryInterface);
    o_SetReflexSync = GET_INTERFACE(NvAPI_D3D_SetReflexSync, queryInterface); // Optional, R530+

    if (o_SetSleepMode == nullptr || o_Sleep == nullptr || o_SetLatencyMarker == nullptr ||
        o_SetAsyncFrameMarker == nullptr || o_GetLatency == nullptr || o_GetSleepStatus == nullptr)
    {
        return false;
    }

    device = pDevice;
    LOG_INFO("Reflex initialized");
    return true;
}

void Reflex::deinit()
{
    // Leave the driver without low latency for the next output
    if (device != nullptr)
    {
        NV_SET_SLEEP_MODE_PARAMS params {};
        params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
        o_SetSleepMode(device, &params);
    }

    device = nullptr;
    LOG_INFO("Reflex deinitialized");
}

bool Reflex::is_enabled()
{
    auto force = Config::Instance()->FN_ForceReflex.value_or_default();
    return force != ForceReflex::InGame ? force == ForceReflex::ForceEnable : low_latency_enabled;
}

void Reflex::apply_sleep_mode()
{
    if (device == nullptr || !sleep_mode_known)
        return;

    NV_SET_SLEEP_MODE_PARAMS params {};
    params.version = NV_SET_SLEEP_MODE_PARAMS_VER;
    params.bLowLatencyMode = is_enabled();
    params.bLowLatencyBoost = sleep_mode.low_latency_boost;
    params.minimumIntervalUs = sleep_mode.minimum_interval_us;
    params.bUseMarkersToOptimize = sleep_mode.use_markers_to_optimize;
    params.bUseMinQueueTime = sleep_mode.use_min_queue_time;

    auto result = o_SetSleepMode(device, &params);
    applied_enabled = params.bLowLatencyMode;

    LOG_INFO("Reflex sleep mode, low latency: {}, boost: {}, interval: {} us, result: {}", (int) params.bLowLatencyMode,
             (int) params.bLowLatencyBoost, params.minimumIntervalUs, (int) result);
}

void Reflex::get_sleep_status(SleepParams* sleep_params)
{
    NV_GET_SLEEP_STATUS_PARAMS status {};
    status.version = NV_GET_SLEEP_STATUS_PARAMS_VER;

    if (device == nullptr || o_GetSleepStatus(device, &status) != NVAPI_OK)
    {
        sleep_params->low_latency_enabled = is_enabled();
        return;
    }

    sleep_params->low_latency_enabled = status.bLowLatencyMode;
    sleep_params->fullscreen_vrr = status.bFsVrr;
    sleep_params->control_panel_vsync_override = status.bCplVsyncOn;
    sleep_params->sleep_interval_us = status.sleepIntervalUs;
    sleep_params->use_game_sleep = status.bUseGameSleep;
    sleep_params->fullscreen_i_flip = status.bFullscreenIFlip;
    sleep_params->fg_multiplier = status.fgMultiplier;
}

void Reflex::set_sleep_mode(SleepMode* sleep_mode)
{
    // Some games set the same sleep mode every frame
    if (sleep_mode_known && is_enabled() == applied_enabled && *sleep_mode == this->sleep_mode)
        return;

    low_latency_enabled = sleep_mode->low_latency_enabled;
    this->sleep_mode = *sleep_mode;
    sleep_mode_known = true;
    apply_sleep_mode();
}

void Reflex::sleep(std::optional<uint32_t> frame_id)
{
    if (device == nullptr)
        return;

    // Force State in the menu can change at any time
    if (sleep_mode_known && is_enabled() != applied_enabled)
        apply_sleep_mode();

    o_Sleep(device);
}

void Reflex::set_marker(IUnknown* pDevice, const MarkerParams& marker_params)
{
    NV_LATENCY_MARKER_PARAMS params {};
    params.version = NV_LATENCY_MARKER_PARAMS_VER;
    params.frameID = marker_params.frame_id;
    params.markerType = (NV_LATENCY_MARKER_TYPE) marker_params.marker_type; // Enums match

    if (auto target = pDevice != nullptr ? pDevice : device)
        o_SetLatencyMarker(target, &params);
}

void Reflex::set_async_marker(IUnknown* pCommandQueue, const MarkerParams& marker_params)
{
    NV_ASYNC_FRAME_MARKER_PARAMS params {};
    params.version = NV_ASYNC_FRAME_MARKER_PARAMS_VER;
    params.frameID = marker_params.frame_id;
    params.markerType = (NV_LATENCY_MARKER_TYPE) marker_params.marker_type;

    if (pCommandQueue != nullptr)
        o_SetAsyncFrameMarker((ID3D12CommandQueue*) pCommandQueue, &params);
}

void Reflex::set_reflex_sync(uint32_t frameMultiplier) const
{
    if (o_SetReflexSync == nullptr || device == nullptr)
        return;

    // Only the multiplier, the rest stays 0 as DLSSG sends it
    NV_SET_REFLEX_SYNC_PARAMS params {};
    params.version = NV_SET_REFLEX_SYNC_PARAMS_VER;
    params.fgMultiplier = (NvU8) frameMultiplier;

    o_SetReflexSync(device, &params);
}

NvAPI_Status Reflex::get_latency(NV_LATENCY_RESULT_PARAMS* latency_params) const
{
    if (device == nullptr)
        return NVAPI_INVALID_ARGUMENT;

    return o_GetLatency(device, latency_params);
}
