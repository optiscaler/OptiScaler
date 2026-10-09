#include "pch.h"
#include "input_xell.h"
#include <proxies/XeLL_Proxy.h>
#include <inputs/FG/XeFG_Inputs_Dx12.h>

// Context free calls can be answered by the real XeLL
static FARPROC RealExport(const char* name)
{
    auto module = XeLLProxy::Module();
    return module != nullptr ? KernelBaseProxy::GetProcAddress_()(module, name) : nullptr;
}

// Calls XeFG makes on the real XeLL, older libxell versions might not have them
template <typename Fn, typename... Args> static xell_result_t CallReal(Fn real, Args... args)
{
    return real != nullptr ? real(args...) : XELL_RESULT_ERROR_NOT_IMPLEMENTED;
}

bool InputXeLL::CreateReal(xell_input_handle_t context)
{
    if (context->real != nullptr)
        return true;

    auto create = XeLLProxy::RealD3D12CreateContext();

    if (create == nullptr || create(context->device, &context->real) != XELL_RESULT_SUCCESS)
    {
        LOG_ERROR("Couldn't create the real XeLL context");
        context->real = nullptr;
        return false;
    }

    // libxell's handle is the same two fields
    auto real = (xell_input_handle_t) context->real;
    context->impl = real->impl;
    context->magic = real->magic;

    return true;
}

xell_result_t InputXeLL::SetRealSleepMode(xell_input_handle_t context)
{
    auto params = context->sleepParams;

    // OptiScaler's own contexts get their limit from the XeLL output
    if (auto limit = fpsLimitUs.load(); limit != 0 && !context->inputContext.localContext)
        params.minimumIntervalUs = limit;

    return XeLLProxy::RealSetSleepMode()(context->real, &params);
}

xell_result_t InputXeLL::D3D12CreateLocalContext(ID3D12Device* device, xell_input_handle_t* out_context)
{
    if (!device || !out_context)
        return XELL_RESULT_ERROR_INVALID_ARGUMENT;

    auto context = new _xell_input_handle_t();
    context->device = device;
    context->inputContext.localContext = true;

    if (!CreateReal(context))
    {
        delete context;
        return XELL_RESULT_ERROR_UNKNOWN;
    }

    *out_context = context;
    return XELL_RESULT_SUCCESS;
}

void InputXeLL::AttachXeFG(void* xellContext)
{
    auto context = gameContext.load();

    // Not a context OptiScaler gave out, it's already the real XeLL's
    if (context == nullptr || context != xellContext || context->real != nullptr || !CreateReal(context))
        return;

    LOG_INFO("The game's XeFG got its XeLL context");

    SetRealSleepMode(context);
}

void InputXeLL::SetXeFGEnabled(bool enabled)
{
    // The XeFG passthrough stays native
    if (XeFGInputs::Passthrough() || native.exchange(enabled) == enabled)
        return;

    LOG_INFO("The game's XeFG {}, its XeLL {}", enabled ? "enabled" : "disabled",
             enabled ? "passes through" : "is the low latency input again");

    // XeFG needs the game's low latency mode on the real XeLL
    if (auto context = gameContext.load(); enabled && context != nullptr && context->real != nullptr)
        SetRealSleepMode(context);
}

bool InputXeLL::LimitFps(uint32_t intervalUs)
{
    // Only game's latest context is limited
    auto context = gameContext.load();

    if (!native || context == nullptr || context->real == nullptr)
        return false;

    if (fpsLimitUs.exchange(intervalUs) != intervalUs)
        SetRealSleepMode(context);

    return context->sleepParams.bLowLatencyMode;
}

// Common
xell_result_t InputXeLL::DestroyContext(xell_input_handle_t context)
{
    if (!context)
        return XELL_RESULT_ERROR_INVALID_CONTEXT;

    auto expected = context;
    gameContext.compare_exchange_strong(expected, nullptr);

    if (context->real)
        XeLLProxy::RealDestroyContext()(context->real);

    delete context;
    return XELL_RESULT_SUCCESS;
}
xell_result_t InputXeLL::SetSleepMode(xell_input_handle_t context, const xell_sleep_params_t* param)
{
    if (!context)
        return XELL_RESULT_ERROR_INVALID_CONTEXT;

    if (!param)
        return XELL_RESULT_ERROR_INVALID_ARGUMENT;

    context->sleepParams = *param;

    if (context->real)
    {
        auto result = SetRealSleepMode(context);

        if (PassesThrough(context))
            return result;
    }

    SleepMode sleepMode {};
    sleepMode.low_latency_enabled = param->bLowLatencyMode;
    sleepMode.low_latency_boost = param->bLowLatencyBoost;
    sleepMode.minimum_interval_us = param->minimumIntervalUs;

    auto result = InputCommon::set_sleep_mode(context->inputContext, context->device, &sleepMode);

    if (result != InputResult::Ok && result != InputResult::UsingDifferentInput)
        LOG_ERROR("set_sleep_mode result: {}", magic_enum::enum_name(result));

    return XELL_RESULT_SUCCESS;
}
xell_result_t InputXeLL::GetSleepMode(xell_input_handle_t context, xell_sleep_params_t* param)
{
    if (!context)
        return XELL_RESULT_ERROR_INVALID_CONTEXT;

    if (!param)
        return XELL_RESULT_ERROR_INVALID_ARGUMENT;

    if (context->real)
        return XeLLProxy::RealGetSleepMode()(context->real, param);

    if (!context->device)
        return XELL_RESULT_ERROR_DEVICE;

    SleepParams sleepParams {};

    auto result = InputCommon::get_sleep_status(context->inputContext, context->device, &sleepParams);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
    {
        param->bLowLatencyMode = sleepParams.low_latency_enabled;
        param->bLowLatencyBoost = sleepParams.low_latency_boost;
        param->minimumIntervalUs = sleepParams.minimum_interval_us;

        // TODO: not fully filled out

        return XELL_RESULT_SUCCESS;
    }
    else
    {
        LOG_ERROR("get_sleep_status result: {}", magic_enum::enum_name(result));
        return XELL_RESULT_ERROR_UNKNOWN;
    }
}
xell_result_t InputXeLL::Sleep(xell_input_handle_t context, uint32_t frame_id)
{
    if (!context)
        return XELL_RESULT_ERROR_INVALID_CONTEXT;

    if (PassesThrough(context))
        return XeLLProxy::RealSleep()(context->real, frame_id);

    if (!context->device)
        return XELL_RESULT_ERROR_DEVICE;

    auto result = InputCommon::sleep(context->inputContext, context->device, frame_id);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
        return XELL_RESULT_SUCCESS;
    else
        LOG_ERROR("sleep result: {}", magic_enum::enum_name(result));

    return XELL_RESULT_ERROR_UNKNOWN;
}
xell_result_t InputXeLL::AddMarkerData(xell_input_handle_t context, uint32_t frame_id,
                                       xell_latency_marker_type_t marker)
{
    if (!context)
        return XELL_RESULT_ERROR_INVALID_CONTEXT;

    // XeFG's own markers (80860000, 80860001), only for the real XeLL
    if (PassesThrough(context) || (marker >= XELL_MARKER_COUNT && context->real))
        return XeLLProxy::RealAddMarkerData()(context->real, frame_id, marker);

    if (marker >= XELL_MARKER_COUNT)
        return XELL_RESULT_SUCCESS;

    MarkerParams markerParams {};
    markerParams.frame_id = frame_id;
    markerParams.marker_type = (MarkerType) marker; // Those should match 1:1

    auto result = InputCommon::set_marker(context->inputContext, context->device, markerParams);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
        return XELL_RESULT_SUCCESS;
    else
        LOG_ERROR("set_marker result: {}", magic_enum::enum_name(result));

    return XELL_RESULT_ERROR_UNKNOWN;
}
xell_result_t InputXeLL::GetVersion(xell_version_t* pVersion)
{
    if (!pVersion)
        return XELL_RESULT_ERROR_INVALID_ARGUMENT;

    // XeFG checks the version of the XeLL it is paired with, report the real XeLL's
    auto result = XELL_RESULT_ERROR_UNKNOWN;

    if (auto real = (decltype(&xellGetVersion)) RealExport("xellGetVersion"))
        result = real(pVersion);

    if (result != XELL_RESULT_SUCCESS)
    {
        pVersion->major = 1;
        pVersion->minor = 3;
        pVersion->patch = 1;
    }

    return XELL_RESULT_SUCCESS;
}
xell_result_t InputXeLL::SetLoggingCallback(xell_input_handle_t context, xell_logging_level_t loggingLevel,
                                            xell_app_log_callback_t loggingCallback)
{
    if (!context)
        return XELL_RESULT_ERROR_INVALID_CONTEXT;

    if (PassesThrough(context))
        return XeLLProxy::RealSetLoggingCallback()(context->real, loggingLevel, loggingCallback);

    return XELL_RESULT_SUCCESS;
}
xell_result_t InputXeLL::GetFramesReports(xell_input_handle_t context, xell_frame_report_t* outdata)
{
    if (!context)
        return XELL_RESULT_ERROR_INVALID_CONTEXT;

    if (!outdata)
        return XELL_RESULT_ERROR_INVALID_ARGUMENT;

    if (PassesThrough(context))
        return XeLLProxy::RealGetFramesReports()(context->real, outdata);

    auto result = InputCommon::get_latency(context->inputContext, context->device, outdata);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
        return XELL_RESULT_SUCCESS;
    else
        LOG_ERROR("get_latency result: {}", magic_enum::enum_name(result));

    return XELL_RESULT_ERROR_UNKNOWN;
}

// D3D12
xell_result_t InputXeLL::D3D12CreateContext(ID3D12Device* device, xell_input_handle_t* out_context)
{
    if (!device || !out_context)
        return XELL_RESULT_ERROR_INVALID_ARGUMENT;

    auto context = new _xell_input_handle_t();
    context->device = device;

    // The XeFG passthrough decided at startup, the game's XeFG is the frame generation
    if (XeFGInputs::Passthrough())
    {
        native = true;
        CreateReal(context);
    }

    gameContext = context;
    *out_context = context;

    return XELL_RESULT_SUCCESS;
}

// From dll exports
xell_result_t InputXeLL::AILGetDecision(void* param1, void* param2)
{
    if (auto real = (decltype(&AILGetDecision)) RealExport("xellAILGetDecision"))
        return real(param1, param2);

    return XELL_RESULT_ERROR_UNKNOWN;
}
uint32_t InputXeLL::AILGetVersion()
{
    if (auto real = (decltype(&AILGetVersion)) RealExport("xellAILGetVersion"))
        return real();

    return 1;
}
bool InputXeLL::AILIsSupportedDevice(uint32_t param1)
{
    if (auto real = (decltype(&AILIsSupportedDevice)) RealExport("xellAILIsSupportedDevice"))
        return real(param1);

    return false;
}

// The rest only XeFG calls, on the real XeLL also while disabled. A context without one has no XeFG on it.
xell_result_t InputXeLL::D3D12SetAppQueue(xell_input_handle_t context, ID3D12CommandQueue* appQueue)
{
    if (!context)
        return XELL_RESULT_ERROR_INVALID_CONTEXT;

    return context->real ? CallReal(XeLLProxy::RealD3D12SetAppQueue(), context->real, appQueue) : XELL_RESULT_SUCCESS;
}
xell_result_t InputXeLL::GetContextParameterP(xell_input_handle_t context, uint32_t param1, uint64_t param2)
{
    return XELL_RESULT_SUCCESS;
}
xell_result_t InputXeLL::GetLastPresentStartFrameId(xell_input_handle_t context, uint32_t* p_frame_id)
{
    if (!context)
        return XELL_RESULT_ERROR_INVALID_CONTEXT;

    if (!p_frame_id)
        return XELL_RESULT_ERROR_INVALID_ARGUMENT;

    if (PassesThrough(context))
        return CallReal(XeLLProxy::RealGetLastPresentStartFrameId(), context->real, p_frame_id);

    *p_frame_id = (uint32_t) InputCommon::get_last_present_start_frame_id();

    return XELL_RESULT_SUCCESS;
}
xell_result_t InputXeLL::QueryInterface(xell_input_handle_t context, LPCSTR lpProcName, FARPROC* outFunc)
{
    if (!outFunc)
        return XELL_RESULT_ERROR_INVALID_ARGUMENT;

    *outFunc = nullptr;

    if (!lpProcName)
    {
        // libxell returns its context behind the handle
        *outFunc = (FARPROC) (context != nullptr && context->impl != nullptr ? context->impl : context);
        return XELL_RESULT_SUCCESS;
    }

    std::string procName(lpProcName);

    if (procName.contains("xellDestroyContext"))
        *outFunc = (FARPROC) &DestroyContext;
    else if (procName.contains("xellSetSleepMode"))
        *outFunc = (FARPROC) &SetSleepMode;
    else if (procName.contains("xellGetSleepMode"))
        *outFunc = (FARPROC) &GetSleepMode;
    else if (procName.contains("xellSleep"))
        *outFunc = (FARPROC) &Sleep;
    else if (procName.contains("xellAddMarkerData"))
        *outFunc = (FARPROC) &AddMarkerData;
    else if (procName.contains("xellGetVersion"))
        *outFunc = (FARPROC) &GetVersion;
    else if (procName.contains("xellSetLoggingCallback"))
        *outFunc = (FARPROC) &SetLoggingCallback;
    else if (procName.contains("xellGetFramesReports"))
        *outFunc = (FARPROC) &GetFramesReports;
    else if (procName.contains("xellD3D12CreateContext"))
        *outFunc = (FARPROC) &D3D12CreateContext;
    else if (procName.contains("xellAILGetDecision"))
        *outFunc = (FARPROC) &AILGetDecision;
    else if (procName.contains("xellAILGetVersion"))
        *outFunc = (FARPROC) &AILGetVersion;
    else if (procName.contains("xellAILIsSupportedDevice"))
        *outFunc = (FARPROC) &AILIsSupportedDevice;
    else if (procName.contains("xellD3D12SetAppQueue"))
        *outFunc = (FARPROC) &D3D12SetAppQueue;
    else if (procName.contains("xellGetContextParameterP"))
        *outFunc = (FARPROC) &GetContextParameterP;
    else if (procName.contains("xellGetLastPresentStartFrameId"))
        *outFunc = (FARPROC) &GetLastPresentStartFrameId;
    else if (procName.contains("xellQueryInterface"))
        *outFunc = (FARPROC) &QueryInterface;
    else if (procName.contains("xellSetContextParameterP"))
        *outFunc = (FARPROC) &SetContextParameterP;
    else if (procName.contains("xellSetDisplayInfo"))
        *outFunc = (FARPROC) &SetDisplayInfo;
    else if (procName.contains("xellSetFgEnabled"))
        *outFunc = (FARPROC) &SetFgEnabled;
    else if (procName.contains("xellSetGeneratedFramesCount"))
        *outFunc = (FARPROC) &SetGeneratedFramesCount;

    if (*outFunc)
        return XELL_RESULT_SUCCESS;

    return XELL_RESULT_ERROR_UNKNOWN;
}

xell_result_t InputXeLL::SetContextParameterP(xell_input_handle_t context, uint32_t param1, uint64_t param2)
{
    return XELL_RESULT_SUCCESS;
}
xell_result_t InputXeLL::SetDisplayInfo(xell_input_handle_t context, void* displayInfo)
{
    if (!context)
        return XELL_RESULT_ERROR_INVALID_CONTEXT;

    return context->real ? CallReal(XeLLProxy::RealSetDisplayInfo(), context->real, displayInfo) : XELL_RESULT_SUCCESS;
}
xell_result_t InputXeLL::SetFgEnabled(xell_input_handle_t context, uint32_t enabled, uint32_t frameId)
{
    if (!context)
        return XELL_RESULT_ERROR_INVALID_CONTEXT;

    return context->real ? CallReal(XeLLProxy::RealSetFgEnabled(), context->real, enabled, frameId)
                         : XELL_RESULT_SUCCESS;
}

xell_result_t InputXeLL::SetGeneratedFramesCount(xell_input_handle_t context, uint32_t frameId, uint32_t framesCount)
{
    if (!context)
        return XELL_RESULT_ERROR_INVALID_CONTEXT;

    return context->real ? CallReal(XeLLProxy::RealSetGeneratedFramesCount(), context->real, frameId, framesCount)
                         : XELL_RESULT_SUCCESS;
}

#ifdef LOW_LATENCY_INPUTS

XELL_EXPORT xell_result_t xellDestroyContext(xell_context_handle_t context)
{
    return InputXeLL::DestroyContext((InputXeLL::xell_input_handle_t) context);
}

XELL_EXPORT xell_result_t xellSetSleepMode(xell_context_handle_t context, const xell_sleep_params_t* param)
{
    return InputXeLL::SetSleepMode((InputXeLL::xell_input_handle_t) context, param);
}

XELL_EXPORT xell_result_t xellGetSleepMode(xell_context_handle_t context, xell_sleep_params_t* param)
{
    return InputXeLL::GetSleepMode((InputXeLL::xell_input_handle_t) context, param);
}

XELL_EXPORT xell_result_t xellSleep(xell_context_handle_t context, uint32_t frame_id)
{
    return InputXeLL::Sleep((InputXeLL::xell_input_handle_t) context, frame_id);
}

XELL_EXPORT xell_result_t xellAddMarkerData(xell_context_handle_t context, uint32_t frame_id,
                                            xell_latency_marker_type_t marker)
{
    return InputXeLL::AddMarkerData((InputXeLL::xell_input_handle_t) context, frame_id, marker);
}

XELL_EXPORT xell_result_t xellGetVersion(xell_version_t* pVersion) { return InputXeLL::GetVersion(pVersion); }

XELL_EXPORT xell_result_t xellSetLoggingCallback(xell_context_handle_t hContext, xell_logging_level_t loggingLevel,
                                                 xell_app_log_callback_t loggingCallback)
{
    return InputXeLL::SetLoggingCallback((InputXeLL::xell_input_handle_t) hContext, loggingLevel, loggingCallback);
}

XELL_EXPORT xell_result_t xellGetFramesReports(xell_context_handle_t context, xell_frame_report_t* outdata)
{
    return InputXeLL::GetFramesReports((InputXeLL::xell_input_handle_t) context, outdata);
}

XELL_EXPORT xell_result_t xellD3D12CreateContext(ID3D12Device* device, xell_context_handle_t* out_context)
{
    return InputXeLL::D3D12CreateContext(device, (InputXeLL::xell_input_handle_t*) out_context);
}

XELL_EXPORT xell_result_t xellD3D12SetAppQueue(xell_context_handle_t context, ID3D12CommandQueue* appQueue)
{
    return InputXeLL::D3D12SetAppQueue((InputXeLL::xell_input_handle_t) context, appQueue);
}

XELL_EXPORT xell_result_t xellSetDisplayInfo(xell_context_handle_t context, void* displayInfo)
{
    return InputXeLL::SetDisplayInfo((InputXeLL::xell_input_handle_t) context, displayInfo);
}
XELL_EXPORT xell_result_t xellSetFgEnabled(xell_context_handle_t context, uint32_t param1, uint32_t param2)
{
    return InputXeLL::SetFgEnabled((InputXeLL::xell_input_handle_t) context, param1, param2);
}

XELL_EXPORT xell_result_t xellSetGeneratedFramesCount(xell_context_handle_t context, uint32_t frameId,
                                                      uint32_t framesCount)
{
    return InputXeLL::SetGeneratedFramesCount((InputXeLL::xell_input_handle_t) context, frameId, framesCount);
}

XELL_EXPORT xell_result_t xellGetLastPresentStartFrameId(xell_context_handle_t context, uint32_t* p_frame_id)
{
    return InputXeLL::GetLastPresentStartFrameId((InputXeLL::xell_input_handle_t) context, p_frame_id);
};

// The rest of libxell's exports, so a redirected libxell never sees our contexts
XELL_EXPORT xell_result_t xellQueryInterface(xell_context_handle_t context, LPCSTR lpProcName, FARPROC* outFunc)
{
    return InputXeLL::QueryInterface((InputXeLL::xell_input_handle_t) context, lpProcName, outFunc);
}

XELL_EXPORT xell_result_t xellGetContextParameterP(xell_context_handle_t context, uint32_t param1, uint64_t param2)
{
    return InputXeLL::GetContextParameterP((InputXeLL::xell_input_handle_t) context, param1, param2);
}

XELL_EXPORT xell_result_t xellSetContextParameterP(xell_context_handle_t context, uint32_t param1, uint64_t param2)
{
    return InputXeLL::SetContextParameterP((InputXeLL::xell_input_handle_t) context, param1, param2);
}

XELL_EXPORT xell_result_t xellAILGetDecision(void* param1, void* param2)
{
    return InputXeLL::AILGetDecision(param1, param2);
}

XELL_EXPORT uint32_t xellAILGetVersion() { return InputXeLL::AILGetVersion(); }

XELL_EXPORT bool xellAILIsSupportedDevice(uint32_t param1) { return InputXeLL::AILIsSupportedDevice(param1); }

#endif
