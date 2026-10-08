#include "pch.h"
#include "input_reflex.h"
#include <nvapi/NvApiHooks.h>

// OptiScaler's own Streamline (DLSSG output) reaches the driver as before, it isn't a game input
template <typename Fn> static Fn OptiScalerCall(unsigned int id)
{
    if (!ReflexHooks::optiScalerCall)
        return nullptr;

    return reinterpret_cast<Fn>(ReflexHooks::getHookedReflex(id));
}

NvAPI_Status InputReflex::D3D_SetSleepMode(IUnknown* pDev, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams)
{
    if (!pSetSleepModeParams || !pDev)
        return NVAPI_INVALID_ARGUMENT;

    if (auto own = OptiScalerCall<decltype(&NvAPI_D3D_SetSleepMode)>(GET_ID(NvAPI_D3D_SetSleepMode)))
        return own(pDev, pSetSleepModeParams);

    SleepMode sleepMode {};
    sleepMode.low_latency_enabled = pSetSleepModeParams->bLowLatencyMode;
    sleepMode.low_latency_boost = pSetSleepModeParams->bLowLatencyBoost;
    sleepMode.minimum_interval_us = pSetSleepModeParams->minimumIntervalUs;
    sleepMode.use_markers_to_optimize = pSetSleepModeParams->bUseMarkersToOptimize;
    sleepMode.use_min_queue_time = pSetSleepModeParams->bUseMinQueueTime;

    auto result = InputCommon::set_sleep_mode(inputContext, pDev, &sleepMode);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
        return NVAPI_OK;
    else
        LOG_ERROR("set_sleep_mode result: {}", magic_enum::enum_name(result));

    if (result == InputResult::NoReadyOutput)
        return NVAPI_OK;
    else
        return NVAPI_ERROR;
}

NvAPI_Status InputReflex::D3D_GetSleepStatus(IUnknown* pDevice, NV_GET_SLEEP_STATUS_PARAMS* pGetSleepStatusParams)
{
    if (!pGetSleepStatusParams || !pDevice)
        return NVAPI_INVALID_ARGUMENT;

    if (ReflexHooks::optiScalerCall && NvApiHooks::o_NvAPI_QueryInterface != nullptr)
    {
        if (auto own = GET_INTERFACE(NvAPI_D3D_GetSleepStatus, NvApiHooks::o_NvAPI_QueryInterface))
            return own(pDevice, pGetSleepStatusParams);
    }

    SleepParams sleepParams {};

    auto result = InputCommon::get_sleep_status(inputContext, pDevice, &sleepParams);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
    {
        pGetSleepStatusParams->bLowLatencyMode = sleepParams.low_latency_enabled;
        pGetSleepStatusParams->bFsVrr = sleepParams.fullscreen_vrr;
        pGetSleepStatusParams->bCplVsyncOn = sleepParams.control_panel_vsync_override;
        pGetSleepStatusParams->sleepIntervalUs = sleepParams.sleep_interval_us;
        pGetSleepStatusParams->bUseGameSleep = sleepParams.use_game_sleep;
        pGetSleepStatusParams->bFullscreenIFlip = sleepParams.fullscreen_i_flip;
        pGetSleepStatusParams->fgMultiplier = sleepParams.fg_multiplier;

        return NVAPI_OK;
    }
    else
    {
        LOG_ERROR("get_sleep_status result: {}", magic_enum::enum_name(result));
    }

    if (result == InputResult::NoReadyOutput)
        return NVAPI_OK;
    else
        return NVAPI_ERROR;
}

NvAPI_Status InputReflex::D3D_Sleep(IUnknown* pDev)
{
    if (!pDev)
        return NVAPI_INVALID_ARGUMENT;

    if (auto own = OptiScalerCall<decltype(&NvAPI_D3D_Sleep)>(GET_ID(NvAPI_D3D_Sleep)))
        return own(pDev);

    auto result = InputCommon::sleep(inputContext, pDev);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
        return NVAPI_OK;
    else
        LOG_ERROR("sleep result: {}", magic_enum::enum_name(result));

    if (result == InputResult::NoReadyOutput)
        return NVAPI_OK;
    else
        return NVAPI_ERROR;
}

NvAPI_Status InputReflex::D3D_GetLatency(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams)
{
    if (!pDev || !pGetLatencyParams)
        return NVAPI_INVALID_ARGUMENT;

    if (auto own = OptiScalerCall<decltype(&NvAPI_D3D_GetLatency)>(GET_ID(NvAPI_D3D_GetLatency)))
        return own(pDev, pGetLatencyParams);

    auto result = InputCommon::get_latency(inputContext, pDev, pGetLatencyParams);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
        return NVAPI_OK;
    else if (result == InputResult::NotEnoughReports)
        return NVAPI_OK;
    else
        LOG_ERROR("get_latency result: {}", magic_enum::enum_name(result));

    if (result == InputResult::NoReadyOutput)
        return NVAPI_OK;
    else
        return NVAPI_ERROR;
}

NvAPI_Status InputReflex::D3D_SetLatencyMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams)
{
    if (!pDev || !pSetLatencyMarkerParams)
        return NVAPI_INVALID_ARGUMENT;

    if (auto own = OptiScalerCall<decltype(&NvAPI_D3D_SetLatencyMarker)>(GET_ID(NvAPI_D3D_SetLatencyMarker)))
        return own(pDev, pSetLatencyMarkerParams);

    MarkerParams markerParams {};

    markerParams.frame_id = pSetLatencyMarkerParams->frameID;
    markerParams.marker_type = (MarkerType) pSetLatencyMarkerParams->markerType; // requires enums to match

    auto result = InputCommon::set_marker(inputContext, pDev, markerParams);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
        return NVAPI_OK;
    else
        LOG_ERROR("set_marker result: {}", magic_enum::enum_name(result));

    if (result == InputResult::NoReadyOutput)
        return NVAPI_OK;
    else
        return NVAPI_ERROR;
}

NvAPI_Status InputReflex::D3D12_SetAsyncFrameMarker(ID3D12CommandQueue* pCommandQueue,
                                                    NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams)
{
    if (!pCommandQueue || !pSetAsyncFrameMarkerParams)
        return NVAPI_INVALID_ARGUMENT;

    if (auto own = OptiScalerCall<decltype(&NvAPI_D3D12_SetAsyncFrameMarker)>(GET_ID(NvAPI_D3D12_SetAsyncFrameMarker)))
        return own(pCommandQueue, pSetAsyncFrameMarkerParams);

    MarkerParams markerParams {};

    markerParams.frame_id = pSetAsyncFrameMarkerParams->frameID;
    markerParams.marker_type = (MarkerType) pSetAsyncFrameMarkerParams->markerType; // requires enums to match

    auto result = InputCommon::set_async_marker(inputContext, pCommandQueue, markerParams);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
        return NVAPI_OK;
    else
        LOG_ERROR("set_async_marker result: {}", magic_enum::enum_name(result));

    if (result == InputResult::NoReadyOutput)
        return NVAPI_OK;
    else
        return NVAPI_ERROR;
}