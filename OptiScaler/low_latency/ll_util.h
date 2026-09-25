#pragma once

#include "SysUtils.h"
#include <nvapi.h>
#include <string>
#include <map>
#include <cstdint>

// #define LOWLATENCY_TRACE_LOGGING

#ifdef LOWLATENCY_TRACE_LOGGING
#define LOG_TRACE_LOWLATENCY(msg, ...) LOG_TRACE(msg, ##__VA_ARGS__)
#else
#define LOG_TRACE_LOWLATENCY(msg, ...)                                                                                 \
    {                                                                                                                  \
    }
#endif

#define FRAME_REPORTS_BUFFER_SIZE 70
#define NVAPI_BUFFER_SIZE 64

struct FrameReport
{
    uint64_t frameID;
    uint64_t inputSampleTime;
    uint64_t simStartTime;
    uint64_t simEndTime;
    uint64_t renderSubmitStartTime;
    uint64_t renderSubmitEndTime;
    uint64_t presentStartTime;
    uint64_t presentEndTime;
    uint64_t driverStartTime;
    uint64_t driverEndTime;
    uint64_t osRenderQueueStartTime;
    uint64_t osRenderQueueEndTime;
    uint64_t gpuRenderStartTime;
    uint64_t gpuRenderEndTime;
    uint32_t gpuActiveRenderTimeUs;
    uint32_t gpuFrameTimeUs;
    uint64_t cameraConstructedTime;
    uint32_t crossAdapterCopyTimeUs;
    uint32_t aiFrameTimeUs;
    uint8_t rsvd[104];
};

// Normalized values, position + length <= 1
struct TimingEntry
{
    double position;
    double length;
};

void tonvss(NvAPI_ShortString nvss, std::string str);

#define INSERT_AND_RETURN_WHEN_EQUALS(method)                                                                          \
    if (std::string(it->func) == #method)                                                                              \
        return fakenvapi::idToFuncMapping.insert({ id, (void*) nvapi_calls::method }).first->second;

// https://learn.microsoft.com/en-us/windows/win32/sync/using-waitable-timer-objects
inline int timer_sleep(int64_t hundred_ns)
{
    static HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due_time;

    due_time.QuadPart = -hundred_ns;

    if (!timer)
        return 1;

    if (!SetWaitableTimerEx(timer, &due_time, 0, NULL, NULL, NULL, 0))
        return 2;

    if (WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0)
        return 3;

    return 0;
};

inline int busywait_sleep(int64_t ns)
{
    auto current_time = Util::GetTimestamp();
    auto wait_until = current_time + ns;
    while (current_time < wait_until)
    {
        current_time = Util::GetTimestamp();
    }
    return 0;
}

inline int eepy(int64_t ns)
{
    constexpr int64_t busywait_threshold = 2'000'000; // 2ms

    int status;

    auto current_time = Util::GetTimestamp();
    if (ns <= busywait_threshold)
        status = busywait_sleep(ns);
    else
        status = timer_sleep((ns - busywait_threshold) / 100);

    if (int64_t sleep_deviation = ns - (Util::GetTimestamp() - current_time); sleep_deviation > 0 && !status)
        status = busywait_sleep(sleep_deviation);

    return status;
}

// function taken from jp7677's dxvk-nvapi project licensed under MIT
inline std::string from_error_nr(const int16_t error_nr)
{
    static const std::map<int16_t, std::string> errors { { -1, "NVAPI_ERROR" },
                                                         { -2, "NVAPI_LIBRARY_NOT_FOUND" },
                                                         { -3, "NVAPI_NO_IMPLEMENTATION" },
                                                         { -4, "NVAPI_API_NOT_INITIALIZED" },
                                                         { -5, "NVAPI_INVALID_ARGUMENT" },
                                                         { -6, "NVAPI_NVIDIA_DEVICE_NOT_FOUND" },
                                                         { -7, "NVAPI_END_ENUMERATION" },
                                                         { -8, "NVAPI_INVALID_HANDLE" },
                                                         { -9, "NVAPI_INCOMPATIBLE_STRUCT_VERSION" },
                                                         { -10, "NVAPI_HANDLE_INVALIDATED" },
                                                         { -11, "NVAPI_OPENGL_CONTEXT_NOT_CURRENT" },
                                                         { -14, "NVAPI_INVALID_POINTER" },
                                                         { -12, "NVAPI_NO_GL_EXPERT" },
                                                         { -13, "NVAPI_INSTRUMENTATION_DISABLED" },
                                                         { -15, "NVAPI_NO_GL_NSIGHT" },
                                                         { -100, "NVAPI_EXPECTED_LOGICAL_GPU_HANDLE" },
                                                         { -101, "NVAPI_EXPECTED_PHYSICAL_GPU_HANDLE" },
                                                         { -102, "NVAPI_EXPECTED_DISPLAY_HANDLE" },
                                                         { -103, "NVAPI_INVALID_COMBINATION" },
                                                         { -104, "NVAPI_NOT_SUPPORTED" },
                                                         { -105, "NVAPI_PORTID_NOT_FOUND" },
                                                         { -106, "NVAPI_EXPECTED_UNATTACHED_DISPLAY_HANDLE" },
                                                         { -107, "NVAPI_INVALID_PERF_LEVEL" },
                                                         { -108, "NVAPI_DEVICE_BUSY" },
                                                         { -109, "NVAPI_NV_PERSIST_FILE_NOT_FOUND" },
                                                         { -110, "NVAPI_PERSIST_DATA_NOT_FOUND" },
                                                         { -111, "NVAPI_EXPECTED_TV_DISPLAY" },
                                                         { -112, "NVAPI_EXPECTED_TV_DISPLAY_ON_DCONNECTOR" },
                                                         { -113, "NVAPI_NO_ACTIVE_SLI_TOPOLOGY" },
                                                         { -114, "NVAPI_SLI_RENDERING_MODE_NOTALLOWED" },
                                                         { -115, "NVAPI_EXPECTED_DIGITAL_FLAT_PANEL" },
                                                         { -116, "NVAPI_ARGUMENT_EXCEED_MAX_SIZE" },
                                                         { -117, "NVAPI_DEVICE_SWITCHING_NOT_ALLOWED" },
                                                         { -118, "NVAPI_TESTING_CLOCKS_NOT_SUPPORTED" },
                                                         { -119, "NVAPI_UNKNOWN_UNDERSCAN_CONFIG" },
                                                         { -120, "NVAPI_TIMEOUT_RECONFIGURING_GPU_TOPO" },
                                                         { -121, "NVAPI_DATA_NOT_FOUND" },
                                                         { -122, "NVAPI_EXPECTED_ANALOG_DISPLAY" },
                                                         { -123, "NVAPI_NO_VIDLINK" },
                                                         { -124, "NVAPI_REQUIRES_REBOOT" },
                                                         { -125, "NVAPI_INVALID_HYBRID_MODE" },
                                                         { -126, "NVAPI_MIXED_TARGET_TYPES" },
                                                         { -127, "NVAPI_SYSWOW64_NOT_SUPPORTED" },
                                                         { -128, "NVAPI_IMPLICIT_SET_GPU_TOPOLOGY_CHANGE_NOT_ALLOWED" },
                                                         { -129, "NVAPI_REQUEST_USER_TO_CLOSE_NON_MIGRATABLE_APPS" },
                                                         { -130, "NVAPI_OUT_OF_MEMORY" },
                                                         { -131, "NVAPI_WAS_STILL_DRAWING" },
                                                         { -132, "NVAPI_FILE_NOT_FOUND" },
                                                         { -133, "NVAPI_TOO_MANY_UNIQUE_STATE_OBJECTS" },
                                                         { -134, "NVAPI_INVALID_CALL" },
                                                         { -135, "NVAPI_D3D10_1_LIBRARY_NOT_FOUND" },
                                                         { -136, "NVAPI_FUNCTION_NOT_FOUND" },
                                                         { -137, "NVAPI_INVALID_USER_PRIVILEGE" },
                                                         { -138, "NVAPI_EXPECTED_NON_PRIMARY_DISPLAY_HANDLE" },
                                                         { -139, "NVAPI_EXPECTED_COMPUTE_GPU_HANDLE" },
                                                         { -140, "NVAPI_STEREO_NOT_INITIALIZED" },
                                                         { -141, "NVAPI_STEREO_REGISTRY_ACCESS_FAILED" },
                                                         { -142, "NVAPI_STEREO_REGISTRY_PROFILE_TYPE_NOT_SUPPORTED" },
                                                         { -143, "NVAPI_STEREO_REGISTRY_VALUE_NOT_SUPPORTED" },
                                                         { -144, "NVAPI_STEREO_NOT_ENABLED" },
                                                         { -145, "NVAPI_STEREO_NOT_TURNED_ON" },
                                                         { -146, "NVAPI_STEREO_INVALID_DEVICE_INTERFACE" },
                                                         { -147, "NVAPI_STEREO_PARAMETER_OUT_OF_RANGE" },
                                                         { -148, "NVAPI_STEREO_FRUSTUM_ADJUST_MODE_NOT_SUPPORTED" },
                                                         { -149, "NVAPI_TOPO_NOT_POSSIBLE" },
                                                         { -150, "NVAPI_MODE_CHANGE_FAILED" },
                                                         { -151, "NVAPI_D3D11_LIBRARY_NOT_FOUND" },
                                                         { -152, "NVAPI_INVALID_ADDRESS" },
                                                         { -153, "NVAPI_STRING_TOO_SMALL" },
                                                         { -154, "NVAPI_MATCHING_DEVICE_NOT_FOUND" },
                                                         { -155, "NVAPI_DRIVER_RUNNING" },
                                                         { -156, "NVAPI_DRIVER_NOTRUNNING" },
                                                         { -157, "NVAPI_ERROR_DRIVER_RELOAD_REQUIRED" },
                                                         { -158, "NVAPI_SET_NOT_ALLOWED" },
                                                         { -159, "NVAPI_ADVANCED_DISPLAY_TOPOLOGY_REQUIRED" },
                                                         { -160, "NVAPI_SETTING_NOT_FOUND" },
                                                         { -161, "NVAPI_SETTING_SIZE_TOO_LARGE" },
                                                         { -162, "NVAPI_TOO_MANY_SETTINGS_IN_PROFILE" },
                                                         { -163, "NVAPI_PROFILE_NOT_FOUND" },
                                                         { -164, "NVAPI_PROFILE_NAME_IN_USE" },
                                                         { -165, "NVAPI_PROFILE_NAME_EMPTY" },
                                                         { -166, "NVAPI_EXECUTABLE_NOT_FOUND" },
                                                         { -167, "NVAPI_EXECUTABLE_ALREADY_IN_USE" },
                                                         { -168, "NVAPI_DATATYPE_MISMATCH" },
                                                         { -169, "NVAPI_PROFILE_REMOVED" },
                                                         { -170, "NVAPI_UNREGISTERED_RESOURCE" },
                                                         { -171, "NVAPI_ID_OUT_OF_RANGE" },
                                                         { -172, "NVAPI_DISPLAYCONFIG_VALIDATION_FAILED" },
                                                         { -173, "NVAPI_DPMST_CHANGED" },
                                                         { -174, "NVAPI_INSUFFICIENT_BUFFER" },
                                                         { -175, "NVAPI_ACCESS_DENIED" },
                                                         { -176, "NVAPI_MOSAIC_NOT_ACTIVE" },
                                                         { -177, "NVAPI_SHARE_RESOURCE_RELOCATED" },
                                                         { -178, "NVAPI_REQUEST_USER_TO_DISABLE_DWM" },
                                                         { -179, "NVAPI_D3D_DEVICE_LOST" },
                                                         { -180, "NVAPI_INVALID_CONFIGURATION" },
                                                         { -181, "NVAPI_STEREO_HANDSHAKE_NOT_DONE" },
                                                         { -182, "NVAPI_EXECUTABLE_PATH_IS_AMBIGUOUS" },
                                                         { -183, "NVAPI_DEFAULT_STEREO_PROFILE_IS_NOT_DEFINED" },
                                                         { -184, "NVAPI_DEFAULT_STEREO_PROFILE_DOES_NOT_EXIST" },
                                                         { -185, "NVAPI_CLUSTER_ALREADY_EXISTS" },
                                                         { -186, "NVAPI_DPMST_DISPLAY_ID_EXPECTED" },
                                                         { -187, "NVAPI_INVALID_DISPLAY_ID" },
                                                         { -188, "NVAPI_STREAM_IS_OUT_OF_SYNC" },
                                                         { -189, "NVAPI_INCOMPATIBLE_AUDIO_DRIVER" },
                                                         { -190, "NVAPI_VALUE_ALREADY_SET" },
                                                         { -191, "NVAPI_TIMEOUT" },
                                                         { -192, "NVAPI_GPU_WORKSTATION_FEATURE_INCOMPLETE" },
                                                         { -193, "NVAPI_STEREO_INIT_ACTIVATION_NOT_DONE" },
                                                         { -194, "NVAPI_SYNC_NOT_ACTIVE" },
                                                         { -195, "NVAPI_SYNC_MASTER_NOT_FOUND" },
                                                         { -196, "NVAPI_INVALID_SYNC_TOPOLOGY" },
                                                         { -197, "NVAPI_ECID_SIGN_ALGO_UNSUPPORTED" },
                                                         { -198, "NVAPI_ECID_KEY_VERIFICATION_FAILED" },
                                                         { -199, "NVAPI_FIRMWARE_OUT_OF_DATE" },
                                                         { -200, "NVAPI_FIRMWARE_REVISION_NOT_SUPPORTED" },
                                                         { -201, "NVAPI_LICENSE_CALLER_AUTHENTICATION_FAILED" },
                                                         { -202, "NVAPI_D3D_DEVICE_NOT_REGISTERED" },
                                                         { -203, "NVAPI_RESOURCE_NOT_ACQUIRED" },
                                                         { -204, "NVAPI_TIMING_NOT_SUPPORTED" },
                                                         { -205, "NVAPI_HDCP_ENCRYPTION_FAILED" },
                                                         { -206, "NVAPI_PCLK_LIMITATION_FAILED" },
                                                         { -207, "NVAPI_NO_CONNECTOR_FOUND" },
                                                         { -208, "NVAPI_HDCP_DISABLED" },
                                                         { -209, "NVAPI_API_IN_USE" },
                                                         { -210, "NVAPI_NVIDIA_DISPLAY_NOT_FOUND" },
                                                         { -211, "NVAPI_PRIV_SEC_VIOLATION" },
                                                         { -212, "NVAPI_INCORRECT_VENDOR" },
                                                         { -213, "NVAPI_DISPLAY_IN_USE" },
                                                         { -214, "NVAPI_UNSUPPORTED_CONFIG_NON_HDCP_HMD" },
                                                         { -215, "NVAPI_MAX_DISPLAY_LIMIT_REACHED" },
                                                         { -216, "NVAPI_INVALID_DIRECT_MODE_DISPLAY" },
                                                         { -217, "NVAPI_GPU_IN_DEBUG_MODE" },
                                                         { -218, "NVAPI_D3D_CONTEXT_NOT_FOUND" },
                                                         { -219, "NVAPI_STEREO_VERSION_MISMATCH" },
                                                         { -220, "NVAPI_GPU_NOT_POWERED" },
                                                         { -221, "NVAPI_ERROR_DRIVER_RELOAD_IN_PROGRESS" },
                                                         { -222, "NVAPI_WAIT_FOR_HW_RESOURCE" },
                                                         { -223, "NVAPI_REQUIRE_FURTHER_HDCP_ACTION" },
                                                         { -224, "NVAPI_DISPLAY_MUX_TRANSITION_FAILED" } };

    auto it = errors.find(error_nr);
    return it != errors.end() ? it->second : "UNKNOWN_ERROR";
}
