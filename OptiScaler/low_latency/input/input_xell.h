#pragma once

#include <xell.h>
#include <xell_d3d12.h>
#include "input_common.h"

class InputXeLL
{
    static inline uint64_t lastContextId = 0;
    struct _xell_input_handle_t
    {
        uint64_t id {};
        InputContext inputContext { .caller = LowLatencyInput::XeLL,
                                    .localContext = false,
                                    .noFrameId = false,
                                    .markerMode = InputMarkerMode::FullMarkers };
        ID3D12Device* device {};

        ID3D12CommandQueue* d3d12AppQueue {};
        void* displayInfo {}; // could be freed, somehow make a copy

        // Unsure what to do with those, just store for now
        uint32_t setFgEnabledParam1 {};
        uint32_t setFgEnabledParam2 {};
        uint32_t setGeneratedFramesCountFrameId {};
        uint32_t framesCount {};
    };

  public:
    typedef struct _xell_input_handle_t* xell_input_handle_t;

    // Common
    static xell_result_t DestroyContext(xell_input_handle_t context);
    static xell_result_t SetSleepMode(xell_input_handle_t context, const xell_sleep_params_t* param);
    static xell_result_t GetSleepMode(xell_input_handle_t context, xell_sleep_params_t* param);
    static xell_result_t Sleep(xell_input_handle_t context, uint32_t frame_id);
    static xell_result_t AddMarkerData(xell_input_handle_t context, uint32_t frame_id,
                                       xell_latency_marker_type_t marker);
    static xell_result_t GetVersion(xell_version_t* pVersion);
    static xell_result_t SetLoggingCallback(xell_input_handle_t context, xell_logging_level_t loggingLevel,
                                            xell_app_log_callback_t loggingCallback);
    static xell_result_t GetFramesReports(xell_input_handle_t context, xell_frame_report_t* outdata);

    // D3D12
    static xell_result_t D3D12CreateContext(ID3D12Device* device, xell_input_handle_t* out_context);

    // From dll exports
    static xell_result_t AILGetDecision(void* param1, void* param2);
    static uint32_t AILGetVersion(); // return 1
    static bool AILIsSupportedDevice(uint32_t param1);
    static xell_result_t D3D12SetAppQueue(xell_input_handle_t context, ID3D12CommandQueue* appQueue);
    static xell_result_t GetContextParameterP(xell_input_handle_t context, uint32_t param1,
                                              uint64_t param2); // return 0
    static xell_result_t GetLastPresentStartFrameId(xell_input_handle_t context, uint32_t* p_frame_id);
    static xell_result_t QueryInterface(xell_input_handle_t context, LPCSTR lpProcName,
                                        FARPROC* outFunc); // outFunc contains GetProcAddress called on libxell.dll
                                                           // or internal context when lpProcName == nullptr
    static xell_result_t SetContextParameterP(xell_input_handle_t context, uint32_t param1,
                                              uint64_t param2); // return 0
    static xell_result_t SetDisplayInfo(xell_input_handle_t context, void* displayInfo);
    static xell_result_t SetFgEnabled(xell_input_handle_t context, uint32_t param1,
                                      uint32_t param2); // param1 might be the Fg state to set
    static xell_result_t SetGeneratedFramesCount(xell_input_handle_t context, uint32_t param1,
                                                 uint32_t framesCount); // param1 might be frameId, not sure for both
};

extern "C"
{
    XELL_EXPORT xell_result_t xellD3D12SetAppQueue(xell_context_handle_t context, ID3D12CommandQueue* appQueue);
    XELL_EXPORT xell_result_t xellSetDisplayInfo(xell_context_handle_t context, void* displayInfo);
    XELL_EXPORT xell_result_t xellSetFgEnabled(xell_context_handle_t context, uint32_t param1, uint32_t param2);
    XELL_EXPORT xell_result_t xellSetGeneratedFramesCount(xell_context_handle_t context, uint32_t frameId,
                                                          uint32_t framesCount);
    XELL_EXPORT xell_result_t xellGetLastPresentStartFrameId(xell_context_handle_t context, uint32_t* p_frame_id);
    XELL_EXPORT xell_result_t xellQueryInterface(xell_context_handle_t context, LPCSTR lpProcName, FARPROC* outFunc);
    XELL_EXPORT xell_result_t xellGetContextParameterP(xell_context_handle_t context, uint32_t param1, uint64_t param2);
    XELL_EXPORT xell_result_t xellSetContextParameterP(xell_context_handle_t context, uint32_t param1, uint64_t param2);
    XELL_EXPORT xell_result_t xellAILGetDecision(void* param1, void* param2);
    XELL_EXPORT uint32_t xellAILGetVersion();
    XELL_EXPORT bool xellAILIsSupportedDevice(uint32_t param1);
}