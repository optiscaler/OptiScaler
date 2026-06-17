#include "pch.h"
#include "Xell_Hooks.h"
#include <detours/detours.h>
#include <magic_enum.hpp>
#include <proxies/XeLL_Proxy.h>

#define HOOK(name)                                                                                                     \
    if (o_##name)                                                                                                      \
    DetourAttach(&(PVOID&) o_##name, hk##name)

bool XellHooks::Hook()
{
    if (!XeLLProxy::InitXeLL())
        return false;

    if (o_xellDestroyContext)
    {
        // spdlog::info("Already hooked");
        return true;
    }

    o_xellDestroyContext = XeLLProxy::DestroyContext();
    o_xellSetSleepMode = XeLLProxy::SetSleepMode();
    o_xellGetSleepMode = XeLLProxy::GetSleepMode();
    o_xellSleep = XeLLProxy::Sleep();
    o_xellAddMarkerData = XeLLProxy::AddMarkerData();
    o_xellGetVersion = XeLLProxy::GetVersion();
    o_xellSetLoggingCallback = XeLLProxy::SetLoggingCallback();
    o_xellD3D12CreateContext = XeLLProxy::D3D12CreateContext();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    HOOK(xellDestroyContext);
    HOOK(xellSetSleepMode);
    HOOK(xellGetSleepMode);
    HOOK(xellSleep);
    HOOK(xellAddMarkerData);
    HOOK(xellGetVersion);
    HOOK(xellSetLoggingCallback);
    HOOK(xellD3D12CreateContext);

    DetourTransactionCommit();

    return true;
}

void XellHooks::setOurContext(xell_context_handle_t context) { ourContext = context; }

void XellHooks::blockExternalContexts(bool state) { blockExternal = state; }

// Use context to determine blocking, otherwise just block calls from exe
bool XellHooks::shouldBlock(xell_context_handle_t context)
{
    if (context && ourContext != context && gamesContext != context)
        gamesContext = context;

    if (!blockExternal)
        return false;

    if (context)
        return ourContext != context;
    else
        return GetModuleHandle(nullptr) == Util::GetCallerModule(_ReturnAddress());
}

bool XellHooks::canLimit() { return gamesContextCanLimitFps; }

// only DX12
bool XellHooks::update()
{
    if (!o_xellGetSleepMode || !o_xellSetSleepMode || blockExternal || gamesContext == nullptr)
        return false;

    xell_sleep_params_t currentParams;
    o_xellGetSleepMode(gamesContext, &currentParams);

    if (!currentParams.bLowLatencyMode)
        return false;

    gamesContextCanLimitFps = true;

    static float lastFpslimit = 0.0f;
    if (lastFpslimit == Config::Instance()->FramerateLimit.value_or_default())
        return false;
    lastFpslimit = Config::Instance()->FramerateLimit.value_or_default();
    if (lastFpslimit <= 0.0f)
        currentParams.minimumIntervalUs = 0u;
    else
        currentParams.minimumIntervalUs = static_cast<uint32_t>(std::round(1'000'000 / lastFpslimit));

    return o_xellSetSleepMode(gamesContext, &currentParams) == XELL_RESULT_SUCCESS;
}

xell_result_t XellHooks::hkxellDestroyContext(xell_context_handle_t context)
{
    if (shouldBlock(context))
        return XELL_RESULT_SUCCESS;
    // spdlog::info("xellDestroyContext");
    return o_xellDestroyContext(context);
}
xell_result_t XellHooks::hkxellSetSleepMode(xell_context_handle_t context, const xell_sleep_params_t* param)
{
    if (shouldBlock(context))
        return XELL_RESULT_SUCCESS;
    // spdlog::info("xellSetSleepMode");
    return o_xellSetSleepMode(context, param);
}
xell_result_t XellHooks::hkxellGetSleepMode(xell_context_handle_t context, xell_sleep_params_t* param)
{
    if (shouldBlock(context))
        return XELL_RESULT_SUCCESS;
    // spdlog::info("xellGetSleepMode");
    return o_xellGetSleepMode(context, param);
}
xell_result_t XellHooks::hkxellSleep(xell_context_handle_t context, uint32_t frame_id)
{
    if (shouldBlock(context))
        return XELL_RESULT_SUCCESS;
    // spdlog::info("xellSleep: {}", frame_id);
    return o_xellSleep(context, frame_id);
}
xell_result_t XellHooks::hkxellAddMarkerData(xell_context_handle_t context, uint32_t frame_id,
                                             xell_latency_marker_type_t marker)
{
    if (shouldBlock(context))
        return XELL_RESULT_SUCCESS;
    // spdlog::info("xellAddMarkerData {}: {}", magic_enum::enum_name(marker), frame_id);
    return o_xellAddMarkerData(context, frame_id, marker);
}
xell_result_t XellHooks::hkxellGetVersion(xell_version_t* pVersion)
{
    if (shouldBlock(nullptr))
        return XELL_RESULT_SUCCESS;
    // spdlog::info("xellGetVersion");
    return o_xellGetVersion(pVersion);
}
xell_result_t XellHooks::hkxellSetLoggingCallback(xell_context_handle_t hContext, xell_logging_level_t loggingLevel,
                                                  xell_app_log_callback_t loggingCallback)
{
    if (shouldBlock(nullptr))
        return XELL_RESULT_SUCCESS;
    // spdlog::info("xellSetLoggingCallback");
    return o_xellSetLoggingCallback(hContext, loggingLevel, loggingCallback);
}
xell_result_t XellHooks::hkxellD3D12CreateContext(ID3D12Device* device, xell_context_handle_t* out_context)
{
    if (shouldBlock(nullptr))
        return XELL_RESULT_SUCCESS;
    // spdlog::info("xellD3D12CreateContext");
    return o_xellD3D12CreateContext(device, out_context);
}
