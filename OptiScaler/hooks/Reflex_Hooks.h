#pragma once
#include <d3d12.h>
#include <nvapi/NvApiTypes.h>

#include "Hook_Utils.h"
#include "low_latency/ll_util.h"

enum TimingType : uint32_t
{
    TimeRange, // in ns, value stored in length
    Simulation,
    RenderSubmit,
    Present,
    Driver,
    OsRenderQueue,
    GpuRender,

    TimingTypeCOUNT
};

class ReflexHooks
{
    inline static bool _inited = false;
    inline static uint32_t _minimumIntervalUs = 0;
    inline static NV_SET_SLEEP_MODE_PARAMS _lastSleepParams {};
    inline static IUnknown* _lastSleepDev = nullptr;
    inline static uint8_t _FgNumFramesToGenerate = 0;
    inline static uint64_t _lastAsyncMarkerFrameId = 0;
    inline static uint64_t _updatesWithoutMarker = 0;

    inline static uint64_t _lastMarkerFrame = 0;

    inline static NV_VULKAN_SET_SLEEP_MODE_PARAMS _lastVkSleepParams {};
    inline static HANDLE _lastVkSleepDev = nullptr;

    inline static std::thread::id _lastSetSleepThread {};

    // D3D
    inline static decltype(&NvAPI_D3D_SetSleepMode) o_NvAPI_D3D_SetSleepMode = nullptr;
    inline static decltype(&NvAPI_D3D_GetSleepStatus) o_NvAPI_D3D_GetSleepStatus = nullptr;
    inline static decltype(&NvAPI_D3D_Sleep) o_NvAPI_D3D_Sleep = nullptr;
    inline static decltype(&NvAPI_D3D_GetLatency) o_NvAPI_D3D_GetLatency = nullptr;
    inline static decltype(&NvAPI_D3D_SetLatencyMarker) o_NvAPI_D3D_SetLatencyMarker = nullptr;
    inline static decltype(&NvAPI_D3D12_SetAsyncFrameMarker) o_NvAPI_D3D12_SetAsyncFrameMarker = nullptr;
    inline static decltype(&NvAPI_D3D_SetReflexSync) o_NvAPI_D3D_SetReflexSync = nullptr;

    static NvAPI_Status hkNvAPI_D3D_SetSleepMode(IUnknown* pDev, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams);
    static NvAPI_Status hkNvAPI_D3D_Sleep(IUnknown* pDev);
    static NvAPI_Status hkNvAPI_D3D_GetLatency(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams);
    static NvAPI_Status hkNvAPI_D3D_SetLatencyMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams);
    static NvAPI_Status hkNvAPI_D3D_SetReflexSync(IUnknown* pDev, NV_SET_REFLEX_SYNC_PARAMS* pSetReflexSyncParams);
    static NvAPI_Status hkNvAPI_D3D12_SetAsyncFrameMarker(ID3D12CommandQueue* pCommandQueue,
                                                          NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams);
    static NvAPI_Status sendMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams,
                                   bool toStreamline);

    // Vulkan
    inline static decltype(&NvAPI_Vulkan_SetLatencyMarker) o_NvAPI_Vulkan_SetLatencyMarker = nullptr;
    inline static decltype(&NvAPI_Vulkan_SetSleepMode) o_NvAPI_Vulkan_SetSleepMode = nullptr;
    inline static decltype(&NvAPI_Vulkan_GetLatency) o_NvAPI_Vulkan_GetLatency = nullptr;

    static NvAPI_Status hkNvAPI_Vulkan_SetLatencyMarker(HANDLE vkDevice,
                                                        NV_VULKAN_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams);
    static NvAPI_Status hkNvAPI_Vulkan_SetSleepMode(HANDLE vkDevice,
                                                    NV_VULKAN_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams);
    static NvAPI_Status hkNvAPI_Vulkan_GetLatency(HANDLE vkDevice, NV_VULKAN_LATENCY_RESULT_PARAMS* pGetLatencyParams);

    VALIDATE_MEMBER_HOOK(hkNvAPI_D3D_SetSleepMode, decltype(&NvAPI_D3D_SetSleepMode))
    VALIDATE_MEMBER_HOOK(hkNvAPI_D3D_Sleep, decltype(&NvAPI_D3D_Sleep))
    VALIDATE_MEMBER_HOOK(hkNvAPI_D3D_GetLatency, decltype(&NvAPI_D3D_GetLatency))
    VALIDATE_MEMBER_HOOK(hkNvAPI_D3D_SetLatencyMarker, decltype(&NvAPI_D3D_SetLatencyMarker))
    VALIDATE_MEMBER_HOOK(hkNvAPI_D3D12_SetAsyncFrameMarker, decltype(&NvAPI_D3D12_SetAsyncFrameMarker))
    VALIDATE_MEMBER_HOOK(hkNvAPI_Vulkan_SetLatencyMarker, decltype(&NvAPI_Vulkan_SetLatencyMarker))
    VALIDATE_MEMBER_HOOK(hkNvAPI_Vulkan_SetSleepMode, decltype(&NvAPI_Vulkan_SetSleepMode))
    VALIDATE_MEMBER_HOOK(hkNvAPI_Vulkan_GetLatency, decltype(&NvAPI_Vulkan_GetLatency))

  public:
    static std::optional<TimingEntry> timingData[TimingType::TimingTypeCOUNT];

    // Set while OptiScaler's own Streamline (DLSSG output) is called, its Reflex calls aren't a game input
    inline static thread_local bool optiScalerCall = false;

    static void hookReflex(PFN_NvApi_QueryInterface& queryInterface);
    static void* getHookedReflexSync(PFN_NvApi_QueryInterface& queryInterface);
    static uint8_t dlssgFrameCountToGenerate();
    static void setDlssgFrameCount(uint8_t count);
    static bool isReflexHooked();
    static void* getHookedReflex(unsigned int InterfaceId);
    static bool updateTimingData();
    static bool gameIsSendingMarkers();

    // Delivers a game's marker on, toStreamline when OptiScaler's Streamline (DLSSG output) took it instead
    using MarkerSend = NvAPI_Status (*)(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* params, bool toStreamline);

    // The game's Reflex markers: tracking, game quirks and passing them to OptiScaler's Streamline
    static NvAPI_Status processGameMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams,
                                          MarkerSend send);

    // OptiScaler's Streamline sleeping for the game, false when it doesn't
    static bool streamlineSleep();

    // The game's Reflex sleep mode thread, for game quirks
    static void gameSetSleepMode();

    // Async markers that don't reach the driver hooks, a game stops sending them when DLSSG is disabled
    static void trackAsyncMarker(uint64_t frameId) { _lastAsyncMarkerFrameId = frameId; }

    // For updating information about Reflex hooks
    static void update(bool optiFg_FgState, bool isVulkan);

    // 0 - disables the fps cap
    static void setFPSLimit(float fps);
};

// Marks OptiScaler's own Reflex calls for the scope
struct ScopedOptiScalerReflex
{
    bool previous = ReflexHooks::optiScalerCall;
    ScopedOptiScalerReflex() { ReflexHooks::optiScalerCall = true; }
    ~ScopedOptiScalerReflex() { ReflexHooks::optiScalerCall = previous; }
};
