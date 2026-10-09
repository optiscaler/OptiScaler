#pragma once

#include <xell.h>
#include <xell_d3d12.h>
#include "input_common.h"

class InputXeLL
{
    struct _xell_input_handle_t
    {
        // libxell's own handle starts with these, XeFG reads them directly: the 'XeFG' magic and the context behind
        // it. Copied from the real XeLL context once there is one.
        uint64_t magic {};
        void* impl {};

        // The real XeLL context, OptiScaler's own contexts and the game's once its XeFG uses it. Calls on a context
        // with one go straight to it, the others are the XeLL input.
        xell_context_handle_t real {};

        InputContext inputContext { .caller = LowLatencyInput::XeLL,
                                    .localContext = false,
                                    .noFrameId = false,
                                    .markerMode = InputMarkerMode::FullMarkers,
                                    .api = API::DX12 };
        ID3D12Device* device {};
        xell_sleep_params_t sleepParams {}; // The game's, to pass on once it goes to the real XeLL
    };

  public:
    typedef struct _xell_input_handle_t* xell_input_handle_t;

  private:
    static inline std::atomic<xell_input_handle_t> gameContext = nullptr; // The game's latest context
    static inline std::atomic_bool native = false;                        // The game's XeFG is enabled
    static inline std::atomic_uint32_t fpsLimitUs = 0;

    static bool CreateReal(xell_input_handle_t context);

    // OptiScaler's own contexts always, the game's while its XeFG is enabled
    static bool PassesThrough(xell_input_handle_t context)
    {
        return context->real != nullptr && (context->inputContext.localContext || native);
    }
    static xell_result_t SetRealSleepMode(xell_input_handle_t context);

  public:
    // OptiScaler's own context, it goes straight to the real XeLL (the XeLL output)
    static xell_result_t D3D12CreateLocalContext(ID3D12Device* device, xell_input_handle_t* out_context);

    // The game's XeFG got its XeLL context, it reads the real XeLL context behind it. Games do it at startup, also
    // with another frame generation in use.
    static void AttachXeFG(void* xellContext);

    // The game's XeFG enabled or disabled, while enabled the game's XeLL runs as without OptiScaler
    static void SetXeFGEnabled(bool enabled);

    // The game's XeLL runs as without OptiScaler, only the FPS limit is OptiScaler's
    static bool IsNative() { return native; }

    // OptiScaler's FPS limit on the game's XeLL when native, 0 = none. True when it can limit.
    static bool LimitFps(uint32_t intervalUs);

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
    static xell_result_t SetFgEnabled(xell_input_handle_t context, uint32_t enabled, uint32_t frameId);
    static xell_result_t SetGeneratedFramesCount(xell_input_handle_t context, uint32_t frameId,
                                                 uint32_t framesCount); // framesCount 0 - 3
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