#include "ReflexHooks.h"
#include <Config.h>

#include "fakenvapi.h"

// #define LOG_REFLEX_CALLS

NvAPI_Status ReflexHooks::hkNvAPI_D3D_SetSleepMode(IUnknown* pDev, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif
    // Store for later so we can adjust the fps whenever we want
    memcpy(&_lastSleepParams, pSetSleepModeParams, sizeof(NV_SET_SLEEP_MODE_PARAMS));
    _lastSleepDev = pDev;

    if (_minimumIntervalUs != 0)
        pSetSleepModeParams->minimumIntervalUs = _minimumIntervalUs;

    return o_NvAPI_D3D_SetSleepMode(pDev, pSetSleepModeParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D_Sleep(IUnknown* pDev)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    return o_NvAPI_D3D_Sleep(pDev);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D_GetLatency(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    return o_NvAPI_D3D_GetLatency(pDev, pGetLatencyParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D_SetLatencyMarker(IUnknown* pDev,
                                                       NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif
    _updatesWithoutMarker = 0;

    // Some games just stop sending any async markers when DLSSG is disabled, so a reset is needed
    if (_lastAsyncMarkerFrameId + 10 < pSetLatencyMarkerParams->frameID)
        _dlssgDetected = false;

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
#ifdef _DEBUG
    LOG_FUNC();
#endif

    if (!_inited)
    {
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
        State::Instance().reflexShowWarning =
            !fakenvapi::isUsingFakenvapi() && optiFg_FgState && _lastSleepParams.bLowLatencyMode;
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

    if (optiFg_FgState || (_dlssgDetected && fakenvapi::isUsingFakenvapi()))
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
