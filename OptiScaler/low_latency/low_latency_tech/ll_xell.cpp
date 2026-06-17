#include "pch.h"
#include "ll_xell.h"

#include <magic_enum.hpp>
#include <proxies/XeLL_Proxy.h>
#include <nvapi/fakenvapi/log.h>

void XeLL::xell_sleep(uint32_t frame_id)
{
    sent_sleep_frame_ids[frame_id % 64] = true;

    // Don't call XeLL when trying to disable XeLL with XeFG active
    if (!inited_using_context || is_enabled())
    {
        LOG_TRACE_LOWLATENCY("Sleeping with frame_id: {}", frame_id);
        o_xellSleep(xell_context, frame_id);
    }
}

void XeLL::add_marker(uint32_t frame_id, xell_latency_marker_type_t marker)
{
    if (!sent_sleep_frame_ids[frame_id % 64])
    {
        LOG_DEBUG("Skipping reporting {} for XeLL because sleep wasn't sent for frame id: {}",
                  magic_enum::enum_name(marker), frame_id);
        return;
    }

    if (!inited_using_context || is_enabled())
        o_xellAddMarkerData(xell_context, frame_id, marker);
}

bool XeLL::init(IUnknown* pDevice)
{
    if (!pDevice)
    {
        LOG_ERROR("Invalid pointer");
        return false;
    }

    ID3D12Device* dx12_pDevice = nullptr;
    HRESULT hr = pDevice->QueryInterface(__uuidof(ID3D12Device), reinterpret_cast<void**>(&dx12_pDevice));
    if (hr != S_OK)
        return false;

    auto result = o_xellD3D12CreateContext(dx12_pDevice, &xell_context) == XELL_RESULT_SUCCESS;

    if (result)
        XellHooks::blockExternalContexts(true);

    return result;
}

bool XeLL::init_using_ctx(void* context)
{
    if (!XeLLProxy::InitXeLL())
    {
        LOG_ERROR("XeLL init_using_ctx failed to load libxell.dll");
        return false;
    }

    if (!context)
    {
        LOG_ERROR("XeLL handed over to fakenvapi but the context is null");
        return false;
    }

    if (xell_context)
    {
        if (o_xellDestroyContext(xell_context) == XELL_RESULT_SUCCESS)
            LOG_INFO("destroy o_xell_ctx");
        else
            LOG_ERROR("destroy o_xell_ctx error");
    }
    xell_context = (xell_context_handle_t) context;

    // Context is handled and held inside XeLLProxy
    inited_using_context = true;
    XellHooks::blockExternalContexts(true);
    LOG_INFO("XeLL initialized using existing context: {:X}", (uint64_t) xell_context);

    return true;
}

void XeLL::deinit()
{
    XellHooks::blockExternalContexts(false);

    if (inited_using_context)
    {
        // Let XeFG handle the context as XeLL can't be destroyed before XeFG
        LOG_INFO("XeLL deinit called while inited using context, skipping deinitialization");
        inited_using_context = false;
    }
    else
    {
        o_xellDestroyContext(xell_context);
        LOG_INFO("XeLL deinitialized");
    }
}

void* XeLL::get_tech_context() { return xell_context; }

void XeLL::get_sleep_status(SleepParams* sleep_params)
{
    xell_sleep_params_t xell_sleep_params {};
    auto result = o_xellGetSleepMode(xell_context, &xell_sleep_params) == XELL_RESULT_SUCCESS;

    sleep_params->low_latency_enabled = xell_sleep_params.bLowLatencyMode;
    sleep_params->fullscreen_vrr = true;
    sleep_params->control_panel_vsync_override = false;
}

void XeLL::set_sleep_mode(SleepMode* sleep_mode)
{
    xell_sleep_params_t xell_sleep_params {};

    low_latency_enabled = sleep_mode->low_latency_enabled;

    // Always report XeLL as enabled when XeFG is enabled
    if (inited_using_context)
        xell_sleep_params.bLowLatencyMode = true;
    else
        xell_sleep_params.bLowLatencyMode = is_enabled();

    xell_sleep_params.minimumIntervalUs = sleep_mode->minimum_interval_us;
    xell_sleep_params.bLowLatencyBoost = sleep_mode->low_latency_boost;

    static uint32_t last_bLowLatencyMode = 0;
    static uint32_t last_minimumIntervalUs = 0;
    static uint32_t last_bLowLatencyBoost = 0;

    // With ForceXeLL we have FG enabled but not actually working
    // but their FPS limit thinks that the FG is working
    if (Config::Instance()->ForceXeLL.value_or_default())
        xell_sleep_params.minimumIntervalUs /= 2;

    if (xell_sleep_params.bLowLatencyMode != last_bLowLatencyMode ||
        xell_sleep_params.minimumIntervalUs != last_minimumIntervalUs ||
        xell_sleep_params.bLowLatencyBoost != last_bLowLatencyBoost)
    {
        auto result = o_xellSetSleepMode(xell_context, &xell_sleep_params) == XELL_RESULT_SUCCESS;

        last_bLowLatencyMode = xell_sleep_params.bLowLatencyMode;
        last_minimumIntervalUs = xell_sleep_params.minimumIntervalUs;
        last_bLowLatencyBoost = xell_sleep_params.bLowLatencyBoost;
    }
}

void XeLL::set_marker(IUnknown* pDevice, const MarkerParams& marker_params)
{
    if (!pDevice)
    {
        LOG_ERROR("Invalid pointer");
        return;
    }

    // XeLL frame ids are uint64_t
    auto frame_id = (uint32_t) marker_params.frame_id;

    switch (marker_params.marker_type)
    {
    case MarkerType::SIMULATION_START:
        simulation_start_last_id = marker_params.frame_id;

        // Call sleep just before simulation start if sleep isn't getting called
        if (sleep_last_id + 10 < simulation_start_last_id)
            xell_sleep(frame_id);

        add_marker(frame_id, XELL_SIMULATION_START);
        break;
    case MarkerType::SIMULATION_END:
        add_marker(frame_id, XELL_SIMULATION_END);
        break;
    case MarkerType::RENDERSUBMIT_START:
        add_marker(frame_id, XELL_RENDERSUBMIT_START);
        break;
    case MarkerType::RENDERSUBMIT_END:
        add_marker(frame_id, XELL_RENDERSUBMIT_END);
        break;
    case MarkerType::PRESENT_START:
        add_marker(frame_id, XELL_PRESENT_START);
        break;
    case MarkerType::PRESENT_END:
        add_marker(frame_id, XELL_PRESENT_END);
        break;
    // case MarkerType::INPUT_SAMPLE:
    //     add_marker(marker_params.frame_id, XELL_INPUT_SAMPLE);
    // break;
    default:
        break;
    }
}

void XeLL::set_async_marker(IUnknown* pCommandQueue, const MarkerParams& marker_params)
{
    if (!pCommandQueue)
    {
        LOG_ERROR("Invalid pointer");
        return;
    }

    // XeLL frame ids are uint64_t
    auto frame_id = (uint32_t) marker_params.frame_id;

    switch (marker_params.marker_type)
    {
    case MarkerType::OUT_OF_BAND_RENDERSUBMIT_START:
        add_marker(frame_id, (xell_latency_marker_type_t) 80860000);
        break;
    case MarkerType::OUT_OF_BAND_RENDERSUBMIT_END:
        add_marker(frame_id, (xell_latency_marker_type_t) 80860001);
        break;
    default:
        break;
    }
}

void XeLL::sleep(std::optional<uint32_t> frame_id)
{
    if (frame_id.has_value())
        xell_sleep(frame_id.value());
    else
    {
        // This can either be better than sleeping in XELL_SIMULATION_START
        // or be a total mess if +1 is not correct
        sleep_last_id = simulation_start_last_id + 1;
        xell_sleep((uint32_t) sleep_last_id);
    }
}

xell_result_t XeLL::xellD3D12SetAppQueue(ID3D12CommandQueue* appQueue) const
{
    if (!o_xellD3D12SetAppQueue)
        return XELL_RESULT_ERROR_UNKNOWN;

    return o_xellD3D12SetAppQueue(xell_context, appQueue);
}

xell_result_t XeLL::xellSetDisplayInfo(void* displayInfo) const
{
    if (!o_xellSetDisplayInfo)
        return XELL_RESULT_ERROR_UNKNOWN;

    return o_xellSetDisplayInfo(xell_context, displayInfo);
}

xell_result_t XeLL::xellSetFgEnabled(uint32_t param1, uint32_t param2) const
{
    if (!o_xellSetFgEnabled)
        return XELL_RESULT_ERROR_UNKNOWN;

    return o_xellSetFgEnabled(xell_context, param1, param2);
}

xell_result_t XeLL::xellSetGeneratedFramesCount(uint32_t param1, uint32_t framesCount) const
{
    if (!o_xellSetGeneratedFramesCount)
        return XELL_RESULT_ERROR_UNKNOWN;

    return o_xellSetGeneratedFramesCount(xell_context, param1, framesCount);
}

xell_result_t XeLL::xellGetLastPresentStartFrameId(uint32_t* p_frame_id) const
{
    if (!o_xellGetLastPresentStartFrameId)
        return XELL_RESULT_ERROR_UNKNOWN;

    return o_xellGetLastPresentStartFrameId(xell_context, p_frame_id);
}

xell_result_t XeLL::xellGetFramesReports(xell_frame_report_t* outdata) const
{
    if (!o_xellGetFramesReports)
        return XELL_RESULT_ERROR_UNKNOWN;

    return o_xellGetFramesReports(xell_context, outdata);
}
