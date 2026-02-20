#include "pch.h"
#include "Reflex_Hooks.h"
#include <Config.h>

#include <nvapi/fakenvapi.h>

#include <magic_enum.hpp>

// #define LOG_REFLEX_CALLS

std::optional<TimingEntry> ReflexHooks::timingData[TimingType::TimingTypeCOUNT] {};

NvAPI_Status ReflexHooks::hkNvAPI_D3D_SetSleepMode(IUnknown* pDev, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif
    // Store for later so we can adjust the fps whenever we want
    memcpy(&_lastSleepParams, pSetSleepModeParams, sizeof(NV_SET_SLEEP_MODE_PARAMS));
    _lastSleepDev = pDev;

    if (State::Instance().gameQuirks & GameQuirk::HitmanReflexHacks)
        _lastSetSleepThread = std::this_thread::get_id();

    if (_minimumIntervalUs != 0)
        pSetSleepModeParams->minimumIntervalUs = _minimumIntervalUs;

    if (State::Instance().activeFgOutput == FGOutput::XeFG && fakenvapi::ForNvidia_SetSleepMode)
        return fakenvapi::ForNvidia_SetSleepMode(pDev, pSetSleepModeParams);
    else
        return o_NvAPI_D3D_SetSleepMode(pDev, pSetSleepModeParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D_Sleep(IUnknown* pDev)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    if (State::Instance().activeFgOutput == FGOutput::XeFG && fakenvapi::ForNvidia_Sleep)
        return fakenvapi::ForNvidia_Sleep(pDev);
    else
        return o_NvAPI_D3D_Sleep(pDev);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D_GetLatency(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    if (State::Instance().activeFgOutput == FGOutput::XeFG && fakenvapi::ForNvidia_GetLatency)
        return fakenvapi::ForNvidia_GetLatency(pDev, pGetLatencyParams);
    else
        return o_NvAPI_D3D_GetLatency(pDev, pGetLatencyParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D_SetLatencyMarker(IUnknown* pDev,
                                                       NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif
    _updatesWithoutMarker = 0;

    // LOG_DEBUG("frameID: {}, markerType: {}", pSetLatencyMarkerParams->frameID,
    //           magic_enum::enum_name(pSetLatencyMarkerParams->markerType));

    // Some games just stop sending any async markers when DLSSG is disabled, so a reset is needed
    if (_lastAsyncMarkerFrameId + 10 < pSetLatencyMarkerParams->frameID)
        _dlssgDetected = false;

    State::Instance().rtssReflexInjection = pSetLatencyMarkerParams->frameID >> 32;

    if (State::Instance().gameQuirks & GameQuirk::HitmanReflexHacks)
    {
        if ((pSetLatencyMarkerParams->markerType != PRESENT_START &&
             pSetLatencyMarkerParams->markerType != PRESENT_END) &&
            _lastSetSleepThread != std::this_thread::get_id())
        {
            LOG_TRACE("Skipping marker for a hack");
            return NVAPI_OK;
        }

        if (pSetLatencyMarkerParams->markerType == SIMULATION_END)
        {
            NV_LATENCY_MARKER_PARAMS newParams = *pSetLatencyMarkerParams;
            auto previousThread = _lastSetSleepThread;
            _lastSetSleepThread = std::this_thread::get_id();

            newParams.markerType = RENDERSUBMIT_START;
            hkNvAPI_D3D_SetLatencyMarker(pDev, &newParams);

            newParams.markerType = RENDERSUBMIT_END;
            hkNvAPI_D3D_SetLatencyMarker(pDev, &newParams);

            _lastSetSleepThread = previousThread;
        }
    }

    // For now this means that dlssg inputs require fakenvapi, as otherwise hooks won't be called
    if (pSetLatencyMarkerParams->markerType == RENDERSUBMIT_START && State::Instance().activeFgInput == FGInput::DLSSG)
    {
        static ID3D12Device* device12 = nullptr;
        static IUnknown* lastDevice = nullptr;

        if (pDev != lastDevice)
        {
            lastDevice = pDev;
            device12 = nullptr;
        }

        if (pDev && !device12)
        {
            pDev->QueryInterface(IID_PPV_ARGS(&device12));

            if (device12)
                device12->Release();
        }

        if (device12)
            State::Instance().slFGInputs.evaluateState(device12);
    }

    if (pSetLatencyMarkerParams->markerType == PRESENT_START && State::Instance().activeFgInput == FGInput::DLSSG)
        State::Instance().slFGInputs.markPresent(pSetLatencyMarkerParams->frameID);

    if (State::Instance().activeFgOutput == FGOutput::XeFG && fakenvapi::ForNvidia_SetLatencyMarker)
        return fakenvapi::ForNvidia_SetLatencyMarker(pDev, pSetLatencyMarkerParams);
    else
        return o_NvAPI_D3D_SetLatencyMarker(pDev, pSetLatencyMarkerParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D12_SetAsyncFrameMarker(ID3D12CommandQueue* pCommandQueue,
                                                            NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    _lastAsyncMarkerFrameId = pSetAsyncFrameMarkerParams->frameID;

    if (pSetAsyncFrameMarkerParams->markerType == OUT_OF_BAND_PRESENT_START)
    {
        constexpr size_t history_size = 12;
        static size_t counter = 0;
        static NvU64 previous_frame_ids[history_size] = {};

        previous_frame_ids[counter % history_size] = pSetAsyncFrameMarkerParams->frameID;
        counter++;

        size_t repeat_count = 0;

        for (size_t i = 1; i < history_size; i++)
        {
            // won't catch repeat frame ids across array wrap around
            if (previous_frame_ids[i] == previous_frame_ids[i - 1])
            {
                repeat_count++;
            }
        }

        if (_dlssgDetected && repeat_count == 0)
        {
            _dlssgDetected = false;
            LOG_DEBUG("DLSS FG no longer detected");
        }
        else if (!_dlssgDetected && repeat_count >= history_size / 2 - 1)
        {
            _dlssgDetected = true;
            LOG_DEBUG("DLSS FG detected");
        }
    }

    if (State::Instance().activeFgOutput == FGOutput::XeFG && fakenvapi::ForNvidia_SetAsyncFrameMarker)
        return fakenvapi::ForNvidia_SetAsyncFrameMarker(pCommandQueue, pSetAsyncFrameMarkerParams);
    else
        return o_NvAPI_D3D12_SetAsyncFrameMarker(pCommandQueue, pSetAsyncFrameMarkerParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_Vulkan_SetLatencyMarker(HANDLE vkDevice,
                                                          NV_VULKAN_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    _updatesWithoutMarker = 0;

    return o_NvAPI_Vulkan_SetLatencyMarker(vkDevice, pSetLatencyMarkerParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_Vulkan_SetSleepMode(HANDLE vkDevice,
                                                      NV_VULKAN_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif
    // Store for later so we can adjust the fps whenever we want
    memcpy(&_lastVkSleepParams, pSetSleepModeParams, sizeof(NV_VULKAN_SET_SLEEP_MODE_PARAMS));
    _lastVkSleepDev = vkDevice;

    if (_minimumIntervalUs != 0)
        pSetSleepModeParams->minimumIntervalUs = _minimumIntervalUs;

    return o_NvAPI_Vulkan_SetSleepMode(vkDevice, pSetSleepModeParams);
}

void ReflexHooks::hookReflex(PFN_NvApi_QueryInterface& queryInterface)
{
    if (!_inited)
    {
#ifdef _DEBUG
        LOG_FUNC();
#endif

        o_NvAPI_D3D_SetSleepMode = GET_INTERFACE(NvAPI_D3D_SetSleepMode, queryInterface);
        o_NvAPI_D3D_Sleep = GET_INTERFACE(NvAPI_D3D_Sleep, queryInterface);
        o_NvAPI_D3D_GetLatency = GET_INTERFACE(NvAPI_D3D_GetLatency, queryInterface);
        o_NvAPI_D3D_SetLatencyMarker = GET_INTERFACE(NvAPI_D3D_SetLatencyMarker, queryInterface);
        o_NvAPI_D3D12_SetAsyncFrameMarker = GET_INTERFACE(NvAPI_D3D12_SetAsyncFrameMarker, queryInterface);
        o_NvAPI_Vulkan_SetLatencyMarker = GET_INTERFACE(NvAPI_Vulkan_SetLatencyMarker, queryInterface);
        o_NvAPI_Vulkan_SetSleepMode = GET_INTERFACE(NvAPI_Vulkan_SetSleepMode, queryInterface);

        _inited = o_NvAPI_D3D_SetSleepMode && o_NvAPI_D3D_Sleep && o_NvAPI_D3D_GetLatency &&
                  o_NvAPI_D3D_SetLatencyMarker && o_NvAPI_D3D12_SetAsyncFrameMarker;

        if (_inited)
            LOG_DEBUG("Inited Reflex hooks");
    }
}

bool ReflexHooks::isDlssgDetected() { return _dlssgDetected; }

void ReflexHooks::setDlssgDetectedState(bool state) { _dlssgDetected = state; }

bool ReflexHooks::isReflexHooked() { return _inited; }

void* ReflexHooks::getHookedReflex(unsigned int InterfaceId)
{
    if (InterfaceId == GET_ID(NvAPI_D3D_SetSleepMode) && o_NvAPI_D3D_SetSleepMode)
    {
        return &hkNvAPI_D3D_SetSleepMode;
    }
    if (InterfaceId == GET_ID(NvAPI_D3D_Sleep) && o_NvAPI_D3D_Sleep)
    {
        return &hkNvAPI_D3D_Sleep;
    }
    if (InterfaceId == GET_ID(NvAPI_D3D_GetLatency) && o_NvAPI_D3D_GetLatency)
    {
        return &hkNvAPI_D3D_GetLatency;
    }
    if (InterfaceId == GET_ID(NvAPI_D3D_SetLatencyMarker) && o_NvAPI_D3D_SetLatencyMarker)
    {
        return &hkNvAPI_D3D_SetLatencyMarker;
    }
    if (InterfaceId == GET_ID(NvAPI_D3D12_SetAsyncFrameMarker) && o_NvAPI_D3D12_SetAsyncFrameMarker)
    {
        return &hkNvAPI_D3D12_SetAsyncFrameMarker;
    }
    if (InterfaceId == GET_ID(NvAPI_Vulkan_SetLatencyMarker) && o_NvAPI_Vulkan_SetLatencyMarker)
    {
        return &hkNvAPI_Vulkan_SetLatencyMarker;
    }
    if (InterfaceId == GET_ID(NvAPI_Vulkan_SetSleepMode) && o_NvAPI_Vulkan_SetSleepMode)
    {
        return &hkNvAPI_Vulkan_SetSleepMode;
    }

    return nullptr;
}

#define UPDATE_TIMING_ENTRY(name, type)                                                                                \
    if (frameReport.name##EndTime >= frameReport.name##StartTime)                                                      \
    {                                                                                                                  \
        double name##Pos = (double) (frameReport.name##StartTime - start) / rangeNs;                                   \
        double name##Length = (double) (frameReport.name##EndTime - frameReport.name##StartTime) / rangeNs;            \
        timingData[TimingType::type] = TimingEntry { .position = name##Pos, .length = name##Length };                  \
    }                                                                                                                  \
    else                                                                                                               \
    {                                                                                                                  \
        timingData[TimingType::type].reset();                                                                          \
    }

bool ReflexHooks::updateTimingData()
{
    bool canCall = ((State::Instance().activeFgOutput == FGOutput::XeFG && fakenvapi::ForNvidia_GetLatency) ||
                    o_NvAPI_D3D_GetLatency);

    if (!canCall || !_lastSleepDev)
        return false;

    NV_LATENCY_RESULT_PARAMS results {};
    results.version = NV_LATENCY_RESULT_PARAMS_VER;

    if (auto result = hkNvAPI_D3D_GetLatency(_lastSleepDev, &results); result != NVAPI_OK)
        return false;

    // 64th element have the latest data
    auto& frameReport = results.frameReport[63];

    uint64_t start = UINT64_MAX;
    uint64_t end = 0;

    // Please don't look, just thought it would be least work
    auto pTimes = (uint64_t*) &frameReport.simStartTime;
    for (auto i = 0; i < 11; i++)
    {
        auto& time = pTimes[i];
        if (time == 0)
            continue;

        if (time < start)
            start = time;

        if (time > end)
            end = time;
    }

    if (end < start)
        return false;

    double rangeNs = static_cast<double>(end - start);

    timingData[TimingType::TimeRange] = TimingEntry { .position = 0, .length = rangeNs };
    UPDATE_TIMING_ENTRY(sim, Simulation)
    UPDATE_TIMING_ENTRY(renderSubmit, RenderSubmit)
    UPDATE_TIMING_ENTRY(present, Present)
    UPDATE_TIMING_ENTRY(driver, Driver)
    UPDATE_TIMING_ENTRY(osRenderQueue, OsRenderQueue)
    UPDATE_TIMING_ENTRY(gpuRender, GpuRender)

    return true;
}

// For updating information about Reflex hooks
void ReflexHooks::update(bool optiFg_FgState, bool isVulkan)
{
    // We can still use just the markers to limit the fps with Reflex disabled
    // But need to fallback in case a game stops sending them for some reason
    _updatesWithoutMarker++;

    State::Instance().reflexShowWarning = false;

    if (_updatesWithoutMarker > 20 || !_inited)
    {
        State::Instance().reflexLimitsFps = false;
        return;
    }

    if (isVulkan)
    {
        // optiFg_FgState doesn't matter for vulkan
        // isUsingFakenvapi() because fakenvapi might override the reflex' setting and we don't know it
        State::Instance().reflexLimitsFps = fakenvapi::isUsingFakenvapi() || _lastVkSleepParams.bLowLatencyMode;
    }
    else
    {
        // Don't use when: Real Reflex markers + OptiFG + Reflex disabled, causes huge input latency
        State::Instance().reflexLimitsFps =
            fakenvapi::isUsingFakenvapi() || !optiFg_FgState || _lastSleepParams.bLowLatencyMode;
        State::Instance().reflexShowWarning = State::Instance().activeFgOutput != FGOutput::XeFG &&
                                              !fakenvapi::isUsingFakenvapi() && optiFg_FgState &&
                                              _lastSleepParams.bLowLatencyMode;
    }

    static float lastFps = 0;
    static bool lastReflexLimitsFps = State::Instance().reflexLimitsFps;

    // Reset required when toggling Reflex
    if (State::Instance().reflexLimitsFps != lastReflexLimitsFps)
    {
        lastReflexLimitsFps = State::Instance().reflexLimitsFps;
        lastFps = 0;
        setFPSLimit(0);
    }

    if (!State::Instance().reflexLimitsFps)
        return;

    float currentFps = Config::Instance()->FramerateLimit.value_or_default();
    static bool lastDlssgDetectedState = false;

    if (lastDlssgDetectedState != _dlssgDetected)
    {
        lastDlssgDetectedState = _dlssgDetected;
        setFPSLimit(currentFps);

        if (_dlssgDetected)
            LOG_DEBUG("DLSS FG detected");
        else
            LOG_DEBUG("DLSS FG no longer detected");
    }

    if ((optiFg_FgState && State::Instance().activeFgOutput == FGOutput::FSRFG) ||
        (_dlssgDetected && fakenvapi::isUsingFakenvapi()))
        currentFps /= 2;

    if (currentFps != lastFps)
    {
        setFPSLimit(currentFps);
        lastFps = currentFps;
    }
}

// 0 - disables the fps cap
void ReflexHooks::setFPSLimit(float fps)
{
    LOG_INFO("Set FPS Limit to: {}", fps);
    if (fps == 0.0)
        _minimumIntervalUs = 0;
    else
        _minimumIntervalUs = static_cast<uint32_t>(std::round(1'000'000 / fps));

    if (_lastSleepDev != nullptr)
    {
        NV_SET_SLEEP_MODE_PARAMS temp {};
        memcpy(&temp, &_lastSleepParams, sizeof(NV_SET_SLEEP_MODE_PARAMS));
        temp.minimumIntervalUs = _minimumIntervalUs;

        if (State::Instance().activeFgOutput == FGOutput::XeFG && fakenvapi::ForNvidia_SetSleepMode)
            fakenvapi::ForNvidia_SetSleepMode(_lastSleepDev, &temp);
        else
            o_NvAPI_D3D_SetSleepMode(_lastSleepDev, &temp);

    }

    if (_lastVkSleepDev != nullptr)
    {
        NV_VULKAN_SET_SLEEP_MODE_PARAMS temp {};
        memcpy(&temp, &_lastVkSleepParams, sizeof(NV_VULKAN_SET_SLEEP_MODE_PARAMS));
        temp.minimumIntervalUs = _minimumIntervalUs;
        o_NvAPI_Vulkan_SetSleepMode(_lastVkSleepDev, &temp);
    }
}
