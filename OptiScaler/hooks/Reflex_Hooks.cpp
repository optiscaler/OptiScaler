#include "pch.h"
#include "Reflex_Hooks.h"
#include <Config.h>

#include <nvapi/fakenvapi.h>

#include <proxies/Streamline_Proxy.h>

#include <magic_enum.hpp>
#include <nvapi/fakenvapi/nvapi_calls.h>

#include <math.h>
#include <imgui/ImGuiNotify.hpp>

static inline uint64_t _lastFrameId[20] = { 0 };
static inline IUnknown* _lastDev[20] = { 0 };

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

    if (State::Instance().activeFgOutput == FGOutput::XeFG)
        return nvapi_calls::NvAPI_D3D_SetSleepMode(pDev, pSetSleepModeParams);

    return o_NvAPI_D3D_SetSleepMode(pDev, pSetSleepModeParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D_Sleep(IUnknown* pDev)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    static bool skip = false;
    if (State::Instance().activeFgOutput == FGOutput::DLSSG &&
        Config::Instance()->FGDLSSGUseGamesReflexMarkers.value_or_default() && State::Instance().currentFG &&
        State::Instance().currentFG->IsActive() && !State::Instance().currentFG->IsPaused())
    {
        if (!skip)
        {
            uint32_t frameCount = (uint32_t) _lastFrameId[SIMULATION_START] + 1;

            sl::FrameToken* frameToken;
            StreamlineProxy::GetNewFrameToken()(frameToken, &frameCount);

            LOG_TRACE("Sleep for frame {}", frameCount);

            skip = true;
            StreamlineProxy::ReflexSleep()(*frameToken);
            skip = false;

            return NVAPI_OK;
        }
        else
        {
            _lastSleepDev = pDev;
            return o_NvAPI_D3D_Sleep(pDev);
        }
    }

    if (State::Instance().activeFgOutput == FGOutput::XeFG)
        return nvapi_calls::NvAPI_D3D_Sleep(pDev);

    _lastSleepDev = pDev;
    return o_NvAPI_D3D_Sleep(pDev);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D_GetLatency(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    if (State::Instance().activeFgOutput == FGOutput::XeFG)
        return nvapi_calls::NvAPI_D3D_GetLatency(pDev, pGetLatencyParams);

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
    {
        _FgNumFramesToGenerate = 0;
    }

    // RTSS reflex markers have a frameid marker in the high bits
    if (pSetLatencyMarkerParams->frameID >> 32)
    {
        if (!State::Instance().rtssReflexInjection && State::Instance().activeFgOutput == FGOutput::XeFG)
        {
            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            notification.setTitle("RTSS + XeFG detected");
            notification.setContent(
                "RTSS Reflex Injection is known to cause issues.\nEspecially when using XeFG.\nPlease disable it.");
            ImGui::InsertNotification(notification);
        }

        State::Instance().rtssReflexInjection = true;
    }

    // TODO: reflexFrameId gets constantly changed, up and down depending on the marker
    State::Instance().reflexFrameId = pSetLatencyMarkerParams->frameID;

    // if (pSetLatencyMarkerParams->markerType == PRESENT_END)
    _lastFrameId[pSetLatencyMarkerParams->markerType] = pSetLatencyMarkerParams->frameID;
    _lastDev[pSetLatencyMarkerParams->markerType] = pDev;

    static bool skip[20] = {};

    if (pSetLatencyMarkerParams->markerType == SIMULATION_START)
        _lastMarkerFrame = State::Instance().fgLastFrame;

    if (State::Instance().activeFgOutput == FGOutput::DLSSG && StreamlineProxy::IsD3D12Inited() &&
        Config::Instance()->FGDLSSGUseGamesReflexMarkers.value_or_default() && State::Instance().currentFG &&
        State::Instance().currentFG->IsActive() && !State::Instance().currentFG->IsPaused())
    {
        sl::PCLMarker marker {};
        bool noMarker = false;

        switch (pSetLatencyMarkerParams->markerType)
        {
        case SIMULATION_START:
            marker = sl::PCLMarker::eSimulationStart;
            break;

        case SIMULATION_END:
            marker = sl::PCLMarker::eSimulationEnd;
            break;

        case RENDERSUBMIT_START:
            marker = sl::PCLMarker::eRenderSubmitStart;
            break;

        case RENDERSUBMIT_END:
            marker = sl::PCLMarker::eRenderSubmitEnd;
            break;

        case PRESENT_START:
            marker = sl::PCLMarker::ePresentStart;
            break;

        case PRESENT_END:
            marker = sl::PCLMarker::ePresentEnd;
            break;

        case INPUT_SAMPLE:
            marker = sl::PCLMarker::eControllerInputSample;
            break;

        case TRIGGER_FLASH:
            marker = sl::PCLMarker::eTriggerFlash;
            break;

        case PC_LATENCY_PING:
            marker = sl::PCLMarker::eDeltaTCalculation;
            break;

        case OUT_OF_BAND_RENDERSUBMIT_START:
            marker = sl::PCLMarker::eOutOfBandRenderSubmitStart;
            break;

        case OUT_OF_BAND_RENDERSUBMIT_END:
            marker = sl::PCLMarker::eOutOfBandRenderSubmitEnd;
            break;

        case OUT_OF_BAND_PRESENT_START:
            marker = sl::PCLMarker::eOutOfBandPresentStart;
            break;

        case OUT_OF_BAND_PRESENT_END:
            marker = sl::PCLMarker::eOutOfBandPresentEnd;
            break;

        default:
            noMarker = true;
        }

        auto index = (UINT) marker;

        if (!noMarker && !skip[index])
        {
            if (pSetLatencyMarkerParams->markerType == PRESENT_START && State::Instance().currentFG != nullptr)
            {
                auto frameCount = State::Instance().currentFG->FrameCount();
                if (pSetLatencyMarkerParams->frameID - frameCount > 1)
                    LOG_WARN("FrameId Moved too much??? {} -> {}", frameCount, pSetLatencyMarkerParams->frameID);

                if (pSetLatencyMarkerParams->frameID != frameCount)
                    State::Instance().currentFG->SetFrameCount(pSetLatencyMarkerParams->frameID);

                State::Instance().reflexFrameId = pSetLatencyMarkerParams->frameID;
            }

            sl::FrameToken* frameToken;
            uint32_t frameCount = (uint32_t) pSetLatencyMarkerParams->frameID;
            StreamlineProxy::GetNewFrameToken()(frameToken, &frameCount);

            LOG_TRACE("{} for frame {}", magic_enum::enum_name(marker), frameCount);

            skip[index] = true;
            StreamlineProxy::PCLSetMarker()(marker, *frameToken);
            skip[index] = false;

            return NvAPI_Status::NVAPI_OK;
        }
        else
        {
            if (noMarker)
                return NVAPI_OK;
            else
                return o_NvAPI_D3D_SetLatencyMarker(pDev, pSetLatencyMarkerParams);
        }
    }

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

    if (State::Instance().activeFgOutput == FGOutput::XeFG)
        return nvapi_calls::NvAPI_D3D_SetLatencyMarker(pDev, pSetLatencyMarkerParams);

    return o_NvAPI_D3D_SetLatencyMarker(pDev, pSetLatencyMarkerParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D12_SetAsyncFrameMarker(ID3D12CommandQueue* pCommandQueue,
                                                            NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    _lastAsyncMarkerFrameId = pSetAsyncFrameMarkerParams->frameID;

    // if (pSetAsyncFrameMarkerParams->markerType == OUT_OF_BAND_PRESENT_START)
    //{
    //     constexpr size_t history_size = 20;
    //     static size_t counter = 0;
    //     static NvU64 previous_frame_ids[history_size] = {};

    //    previous_frame_ids[counter % history_size] = pSetAsyncFrameMarkerParams->frameID;
    //    counter++;

    //    size_t valid_count = (counter < history_size) ? counter : history_size;

    //    size_t max_run_length = 1;
    //    size_t current_run = 1;

    //    for (size_t i = 1; i < valid_count; i++)
    //    {
    //        if (previous_frame_ids[i] == previous_frame_ids[i - 1])
    //        {
    //            current_run++;
    //        }
    //        else
    //        {
    //            max_run_length = std::max(max_run_length, current_run);
    //            current_run = 1;
    //        }
    //    }

    //    max_run_length = std::max(max_run_length, current_run);

    //    int detected_mode = 0;
    //    if (max_run_length >= 4)
    //        detected_mode = 3;
    //    else if (max_run_length == 3)
    //        detected_mode = 2;
    //    else if (max_run_length == 2)
    //        detected_mode = 1;

    //    // --- Stability filter ---
    //    static int candidate_mode = 0; // No FG at start
    //    static int candidate_count = 0;
    //    constexpr int stability_threshold = 4;

    //    if (detected_mode == candidate_mode)
    //    {
    //        candidate_count++;
    //    }
    //    else
    //    {
    //        candidate_mode = detected_mode;
    //        candidate_count = 1;
    //    }

    //    // Redundant
    //    // max_run_length = std::max(max_run_length, current_run);

    //    // Only commit after stable detection
    //    if (candidate_count >= stability_threshold)
    //    {
    //        if (candidate_mode == 0 && _FgNumFramesToGenerate > 0)
    //        {
    //            _FgNumFramesToGenerate = 0;
    //            LOG_DEBUG("DLSS FG no longer detected");
    //        }
    //        else if (_FgNumFramesToGenerate != candidate_mode)
    //        {
    //            _FgNumFramesToGenerate = candidate_mode;

    //            LOG_DEBUG("DLSS FG detected: {}x mode", candidate_mode + 1);
    //        }
    //    }
    //}

    if (State::Instance().activeFgOutput == FGOutput::XeFG)
        return nvapi_calls::NvAPI_D3D12_SetAsyncFrameMarker(pCommandQueue, pSetAsyncFrameMarkerParams);

    return o_NvAPI_D3D12_SetAsyncFrameMarker(pCommandQueue, pSetAsyncFrameMarkerParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_Vulkan_SetLatencyMarker(HANDLE vkDevice,
                                                          NV_VULKAN_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    _updatesWithoutMarker = 0;
    State::Instance().reflexFrameId = pSetLatencyMarkerParams->frameID;

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

NvAPI_Status ReflexHooks::hkNvAPI_Vulkan_GetLatency(HANDLE vkDevice, NV_VULKAN_LATENCY_RESULT_PARAMS* pGetLatencyParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    return o_NvAPI_Vulkan_GetLatency(vkDevice, pGetLatencyParams);
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
        o_NvAPI_Vulkan_GetLatency = GET_INTERFACE(NvAPI_Vulkan_GetLatency, queryInterface);

        _inited = o_NvAPI_D3D_SetSleepMode && o_NvAPI_D3D_Sleep && o_NvAPI_D3D_GetLatency &&
                  o_NvAPI_D3D_SetLatencyMarker && o_NvAPI_D3D12_SetAsyncFrameMarker &&
                  o_NvAPI_Vulkan_SetLatencyMarker && o_NvAPI_Vulkan_SetSleepMode && o_NvAPI_Vulkan_GetLatency;

        if (_inited)
            LOG_DEBUG("Inited Reflex hooks");
    }
}

uint8_t ReflexHooks::dlssgFrameCountToGenerate() { return _FgNumFramesToGenerate; }

void ReflexHooks::setDlssgFrameCount(uint8_t count) { _FgNumFramesToGenerate = count; }

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
    if (InterfaceId == GET_ID(NvAPI_Vulkan_GetLatency) && o_NvAPI_Vulkan_GetLatency)
    {
        return &hkNvAPI_Vulkan_GetLatency;
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
    if (!_inited)
        return false;

    auto processFrameReport = [&](const auto& frameReport) -> bool
    {
        uint64_t start = UINT64_MAX;
        uint64_t end = 0;

        // Please don't look, just thought it would be least work
        auto pTimes = (const uint64_t*) &frameReport.simStartTime;
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

        if (frameReport.frameID != 0)
            State::Instance().reflexFrameId = frameReport.frameID;

        return true;
    };

    if (_lastSleepDev && o_NvAPI_D3D_GetLatency)
    {
        // Not calling free on this but it's static so hopefully fine
        static NV_LATENCY_RESULT_PARAMS* results = new NV_LATENCY_RESULT_PARAMS();
        results->version = NV_LATENCY_RESULT_PARAMS_VER;

        if (auto result = hkNvAPI_D3D_GetLatency(_lastSleepDev, results); result != NVAPI_OK)
        {
            LOG_WARN("NvAPI_D3D_GetLatency failed: {}", magic_enum::enum_name(result));
            return false;
        }

        // 64th element has the latest data
        return processFrameReport(results->frameReport[63]);
    }

    if (_lastVkSleepDev && o_NvAPI_Vulkan_GetLatency)
    {
        // Not calling free on this but it's static so hopefully fine
        static NV_VULKAN_LATENCY_RESULT_PARAMS* results = new NV_VULKAN_LATENCY_RESULT_PARAMS();
        results->version = NV_VULKAN_LATENCY_RESULT_PARAMS_VER;

        if (auto result = hkNvAPI_Vulkan_GetLatency(_lastVkSleepDev, results); result != NVAPI_OK)
        {
            LOG_WARN("NvAPI_Vulkan_GetLatency failed: {}", magic_enum::enum_name(result));
            return false;
        }

        // 64th element has the latest data
        return processFrameReport(results->frameReport[63]);
    }

    return false;
}

// For updating information about Reflex hooks
void ReflexHooks::update(bool fgActive, bool isVulkan)
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

    if (isVulkan && _lastVkSleepDev)
    {
        // fgActive doesn't matter for vulkan
        // isUsingAsMainNvapi() because fakenvapi might override the reflex' setting and we don't know it
        State::Instance().reflexLimitsFps = fakenvapi::isUsingAsMainNvapi() || _lastVkSleepParams.bLowLatencyMode;
    }
    else if (_lastSleepDev)
    {
        // Don't use when: Real Reflex markers + FSR FG + Reflex disabled, causes huge input latency
        State::Instance().reflexLimitsFps =
            fakenvapi::isUsingAsMainNvapi() || !fgActive || _lastSleepParams.bLowLatencyMode;
        State::Instance().reflexShowWarning = State::Instance().activeFgOutput == FGOutput::FSRFG &&
                                              !fakenvapi::isUsingAsMainNvapi() && fgActive &&
                                              _lastSleepParams.bLowLatencyMode;
    }
    else
    {
        State::Instance().reflexLimitsFps = false;
    }

    // Disable reflex fps limiting when reflex is force disabled
    // TODO: It changes without pressing apply, XeLL with XeFG can't actually be disabled so this is wrong
    //
    // Wait for fakenvapi to be merged into Opti for better integration
    //
    // if (State::Instance().reflexLimitsFps && (fakenvapi::isUsingAsMainNvapi() ||
    // fakenvapi::isUsingOnNvidia())
    // &&
    //    Config::Instance()->FN_ForceReflex.value_or_default() == 1)
    //{
    //    State::Instance().reflexLimitsFps = false;
    //}

    static float lastFps = 0;
    static bool lastReflexLimitsFps = State::Instance().reflexLimitsFps;
    static LowLatencyMode lastLowLatencyMode = fakenvapi::getCurrentMode();

    // Reset required when toggling Reflex
    if (State::Instance().reflexLimitsFps != lastReflexLimitsFps)
    {
        lastReflexLimitsFps = State::Instance().reflexLimitsFps;
        lastFps = 0;
        setFPSLimit(0);
    }
    else if (fakenvapi::isUsingAsMainNvapi() && lastLowLatencyMode != fakenvapi::getCurrentMode())
    {
        lastLowLatencyMode = fakenvapi::getCurrentMode();
        lastFps = 0;
        setFPSLimit(0);
    }

    if (!State::Instance().reflexLimitsFps)
        return;

    float currentFps = Config::Instance()->FramerateLimit.value_or_default();
    static uint8_t lastFgNumFramesToGenerate = 0;

    if (lastFgNumFramesToGenerate != _FgNumFramesToGenerate)
    {
        if (fakenvapi::isUsingAsMainNvapi())
        {
            // Don't reload when using Dynamic MFG
            if (State::Instance().dlssgLastSetMode != sl::DLSSGMode::eDynamic &&
                !Config::Instance()->FGDLSSGForceDMFG.value_or_default())
            {
                State::Instance().fakenvapiReloadLowLatency = true;
            }

            // fakenvapi's latency techs fall apart with more than 1 fake frame
            if (Config::Instance()->FN_ForceReflex.value_or_default() != ForceReflex::ForceEnable &&
                (State::Instance().dlssgLastSetMode == sl::DLSSGMode::eDynamic || _FgNumFramesToGenerate > 1))
            {
                Config::Instance()->FN_ForceReflex.set_volatile_value(ForceReflex::ForceDisable);
            }
        }

        lastFgNumFramesToGenerate = _FgNumFramesToGenerate;
        setFPSLimit(currentFps);

        if (_FgNumFramesToGenerate == 0)
            LOG_DEBUG("DLSS FG no longer detected");
        else
            LOG_DEBUG("DLSS FG detected, mode: {}x", _FgNumFramesToGenerate + 1);
    }

    // TODO: replace fgActive with _FgNumFramesToGenerate
    // _FgNumFramesToGenerate needs to be correctly updated
    if (fgActive && State::Instance().activeFgOutput == FGOutput::FSRFG)
    {
        currentFps /= 2;
    }
    else if (_FgNumFramesToGenerate > 0 && fakenvapi::isUsingAsMainNvapi() &&
             fakenvapi::getCurrentMode() != LowLatencyMode::XeLL)
    {
        currentFps /= (_FgNumFramesToGenerate + 1);
    }

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

        if (State::Instance().activeFgOutput == FGOutput::XeFG)
            nvapi_calls::NvAPI_D3D_SetSleepMode(_lastSleepDev, &temp);
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

bool ReflexHooks::gameIsSendingMarkers()
{
    return _lastMarkerFrame != 0 && ((State::Instance().fgLastFrame < _lastMarkerFrame) ||
                                     (State::Instance().fgLastFrame - _lastMarkerFrame) < 5);
}
