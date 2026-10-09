#include "pch.h"
#include "input_reflex.h"
#include <nvapi/NvApiHooks.h>

#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

// OptiScaler's own Streamline (DLSSG output): its calls marked as OptiScaler's, and the ones its modules make on
// Streamline's own threads (the async markers of the generated frames' presents)
static bool FromOptiScaler(void* returnAddress)
{
    if (ReflexHooks::optiScalerCall)
        return true;

    auto& state = State::Instance();

    if (state.optiSlInterposer == nullptr)
        return false;

    auto module = Util::GetCallerModule(returnAddress);

    return module != nullptr &&
           (module == state.optiSlInterposer || module == state.optiSlCommon || module == state.optiSlDLSSG ||
            module == state.optiSlReflex || module == state.optiSlPCL || module == state.optiDLSSG);
}

// With NVIDIA's nvapi OptiScaler's Streamline reaches the driver, with fakenvapi it's the OptiScaler input
template <typename Fn> static Fn ToDriver(unsigned int id)
{
    if (fakenvapi::isUsingAsMainNvapi())
        return nullptr;

    return reinterpret_cast<Fn>(ReflexHooks::getHookedReflex(id));
}

static NvAPI_Status ToNvApi(InputResult result, const char* call)
{
    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput ||
        result == InputResult::NotEnoughReports)
    {
        return NVAPI_OK;
    }

    LOG_ERROR("{} result: {}", call, magic_enum::enum_name(result));

    return result == InputResult::NoReadyOutput ? NVAPI_OK : NVAPI_ERROR;
}

NvAPI_Status InputReflex::D3D_SetSleepMode(IUnknown* pDev, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams)
{
    if (!pSetSleepModeParams || !pDev)
        return NVAPI_INVALID_ARGUMENT;

    bool own = FromOptiScaler(_ReturnAddress());

    if (own)
    {
        if (auto driver = ToDriver<decltype(&NvAPI_D3D_SetSleepMode)>(GET_ID(NvAPI_D3D_SetSleepMode)))
            return driver(pDev, pSetSleepModeParams);

        optiScalerLowLatency = pSetSleepModeParams->bLowLatencyMode;
    }
    else
    {
        ReflexHooks::gameSetSleepMode();
    }

    SleepMode sleepMode {};
    sleepMode.low_latency_enabled = pSetSleepModeParams->bLowLatencyMode;
    sleepMode.low_latency_boost = pSetSleepModeParams->bLowLatencyBoost;
    sleepMode.minimum_interval_us = pSetSleepModeParams->minimumIntervalUs;
    sleepMode.use_markers_to_optimize = pSetSleepModeParams->bUseMarkersToOptimize;
    sleepMode.use_min_queue_time = pSetSleepModeParams->bUseMinQueueTime;

    return ToNvApi(InputCommon::set_sleep_mode(own ? optiScalerContext : inputContext, pDev, &sleepMode),
                   "set_sleep_mode");
}

NvAPI_Status InputReflex::D3D_GetSleepStatus(IUnknown* pDevice, NV_GET_SLEEP_STATUS_PARAMS* pGetSleepStatusParams)
{
    if (!pGetSleepStatusParams || !pDevice)
        return NVAPI_INVALID_ARGUMENT;

    bool own = FromOptiScaler(_ReturnAddress());

    if (own && !fakenvapi::isUsingAsMainNvapi() && NvApiHooks::o_NvAPI_QueryInterface != nullptr)
    {
        if (auto driver = GET_INTERFACE(NvAPI_D3D_GetSleepStatus, NvApiHooks::o_NvAPI_QueryInterface))
            return driver(pDevice, pGetSleepStatusParams);
    }

    SleepParams sleepParams {};

    auto result = InputCommon::get_sleep_status(own ? optiScalerContext : inputContext, pDevice, &sleepParams);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
    {
        // OptiScaler's Streamline reads back the low latency it set, the game's input decides the output's
        pGetSleepStatusParams->bLowLatencyMode = own ? optiScalerLowLatency : sleepParams.low_latency_enabled;
        pGetSleepStatusParams->bFsVrr = sleepParams.fullscreen_vrr;
        pGetSleepStatusParams->bCplVsyncOn = sleepParams.control_panel_vsync_override;
        pGetSleepStatusParams->sleepIntervalUs = sleepParams.sleep_interval_us;
        pGetSleepStatusParams->bUseGameSleep = sleepParams.use_game_sleep;
        pGetSleepStatusParams->bFullscreenIFlip = sleepParams.fullscreen_i_flip;
        pGetSleepStatusParams->fgMultiplier = sleepParams.fg_multiplier;

        return NVAPI_OK;
    }

    if (own)
        pGetSleepStatusParams->bLowLatencyMode = optiScalerLowLatency;

    return ToNvApi(result, "get_sleep_status");
}

NvAPI_Status InputReflex::D3D_Sleep(IUnknown* pDev)
{
    if (!pDev)
        return NVAPI_INVALID_ARGUMENT;

    if (FromOptiScaler(_ReturnAddress()))
    {
        if (auto driver = ToDriver<decltype(&NvAPI_D3D_Sleep)>(GET_ID(NvAPI_D3D_Sleep)))
            return driver(pDev);

        return ToNvApi(InputCommon::sleep(optiScalerContext, pDev), "sleep");
    }

    auto result = InputCommon::sleep(inputContext, pDev);

    // The Reflex output's sleep goes through OptiScaler's Streamline itself, with the other outputs it still sleeps
    // for the game so DLSSG follows its frames
    if (InputCommon::active_output() != LowLatencyMode::Reflex)
        ReflexHooks::streamlineSleep();

    return ToNvApi(result, "sleep");
}

NvAPI_Status InputReflex::D3D_GetLatency(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams)
{
    if (!pDev || !pGetLatencyParams)
        return NVAPI_INVALID_ARGUMENT;

    bool own = FromOptiScaler(_ReturnAddress());

    if (own)
    {
        if (auto driver = ToDriver<decltype(&NvAPI_D3D_GetLatency)>(GET_ID(NvAPI_D3D_GetLatency)))
            return driver(pDev, pGetLatencyParams);
    }

    return ToNvApi(InputCommon::get_latency(own ? optiScalerContext : inputContext, pDev, pGetLatencyParams),
                   "get_latency");
}

// The game's marker after OptiScaler's tracking and game quirks. OptiScaler's Streamline reaches the Reflex output
// itself with a marker it took, the other outputs get the game's.
NvAPI_Status InputReflex::SendGameMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* params, bool toStreamline)
{
    MarkerParams markerParams {};
    markerParams.frame_id = params->frameID;
    markerParams.marker_type = (MarkerType) params->markerType; // requires enums to match

    return ToNvApi(InputCommon::set_marker(inputContext, pDev, markerParams,
                                           !toStreamline || InputCommon::active_output() != LowLatencyMode::Reflex),
                   "set_marker");
}

NvAPI_Status InputReflex::D3D_SetLatencyMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams)
{
    if (!pDev || !pSetLatencyMarkerParams)
        return NVAPI_INVALID_ARGUMENT;

    if (FromOptiScaler(_ReturnAddress()))
    {
        if (auto driver = ToDriver<decltype(&NvAPI_D3D_SetLatencyMarker)>(GET_ID(NvAPI_D3D_SetLatencyMarker)))
            return driver(pDev, pSetLatencyMarkerParams);

        MarkerParams markerParams {};
        markerParams.frame_id = pSetLatencyMarkerParams->frameID;
        markerParams.marker_type = (MarkerType) pSetLatencyMarkerParams->markerType;

        return ToNvApi(InputCommon::set_marker(optiScalerContext, pDev, markerParams), "set_marker");
    }

    return ReflexHooks::processGameMarker(pDev, pSetLatencyMarkerParams, &SendGameMarker);
}

NvAPI_Status InputReflex::D3D12_SetAsyncFrameMarker(ID3D12CommandQueue* pCommandQueue,
                                                    NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams)
{
    if (!pCommandQueue || !pSetAsyncFrameMarkerParams)
        return NVAPI_INVALID_ARGUMENT;

    bool own = FromOptiScaler(_ReturnAddress());

    if (own)
    {
        if (auto driver = ToDriver<decltype(&NvAPI_D3D12_SetAsyncFrameMarker)>(GET_ID(NvAPI_D3D12_SetAsyncFrameMarker)))
        {
            return driver(pCommandQueue, pSetAsyncFrameMarkerParams);
        }
    }

    ReflexHooks::trackAsyncMarker(pSetAsyncFrameMarkerParams->frameID);

    MarkerParams markerParams {};
    markerParams.frame_id = pSetAsyncFrameMarkerParams->frameID;
    markerParams.marker_type = (MarkerType) pSetAsyncFrameMarkerParams->markerType; // requires enums to match

    return ToNvApi(
        InputCommon::set_async_marker(own ? optiScalerAsyncContext : inputContext, pCommandQueue, markerParams),
        "set_async_marker");
}
