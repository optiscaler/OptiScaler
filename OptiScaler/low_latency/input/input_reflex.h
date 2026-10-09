#pragma once

#include <nvapi/NvApiTypes.h>
#include "input_common.h"

#include <vulkan/vulkan.h>

class InputReflex
{
    static inline ID3D12Device* device = nullptr;
    const static inline InputContext inputContext { .caller = LowLatencyInput::Reflex,
                                                    .noFrameId = false,
                                                    .markerMode = InputMarkerMode::FullMarkers };

    // OptiScaler's own Streamline (DLSSG output) with fakenvapi: the input when the game has none of its own
    const static inline InputContext optiScalerContext { .caller = LowLatencyInput::OptiScaler,
                                                         .noFrameId = false,
                                                         .markerMode = InputMarkerMode::FullMarkers };

    // The async markers of its generated frames' presents, the output needs them with any input
    const static inline InputContext optiScalerAsyncContext { .caller = LowLatencyInput::OptiScaler,
                                                              .localContext = true,
                                                              .noFrameId = false,
                                                              .markerMode = InputMarkerMode::FullMarkers };

    // What OptiScaler's Streamline set, it expects to read it back
    static inline bool optiScalerLowLatency = false;

    // nvapi's Vulkan Reflex, deprecated for VK_NV_low_latency2 (InputReflexVk)
    const static inline InputContext vulkanContext { .caller = LowLatencyInput::Reflex,
                                                     .noFrameId = false,
                                                     .markerMode = InputMarkerMode::FullMarkers,
                                                     .vulkan = true };

    // The game waits on it after each sleep
    static inline VkSemaphore vkSemaphore = VK_NULL_HANDLE;

    static NvAPI_Status SendGameMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* params, bool toStreamline);

  public:
    static NvAPI_Status D3D_SetSleepMode(IUnknown* pDev, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams);
    static NvAPI_Status D3D_GetSleepStatus(IUnknown* pDevice, NV_GET_SLEEP_STATUS_PARAMS* pGetSleepStatusParams);
    static NvAPI_Status D3D_Sleep(IUnknown* pDev);
    static NvAPI_Status D3D_GetLatency(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams);
    static NvAPI_Status D3D_SetLatencyMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams);
    static NvAPI_Status D3D12_SetAsyncFrameMarker(ID3D12CommandQueue* pCommandQueue,
                                                  NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams);

    // Vulkan
    static NvAPI_Status Vulkan_InitLowLatencyDevice(HANDLE vkDevice, HANDLE* signalSemaphoreHandle);
    static NvAPI_Status Vulkan_DestroyLowLatencyDevice(HANDLE vkDevice);
    static NvAPI_Status Vulkan_GetSleepStatus(HANDLE vkDevice,
                                              NV_VULKAN_GET_SLEEP_STATUS_PARAMS* pGetSleepStatusParams);
    static NvAPI_Status Vulkan_SetSleepMode(HANDLE vkDevice, NV_VULKAN_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams);
    static NvAPI_Status Vulkan_Sleep(HANDLE vkDevice, NvU64 signalValue);
    static NvAPI_Status Vulkan_GetLatency(HANDLE vkDevice, NV_VULKAN_LATENCY_RESULT_PARAMS* pGetLatencyParams);
    static NvAPI_Status Vulkan_SetLatencyMarker(HANDLE vkDevice,
                                                NV_VULKAN_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams);
    static NvAPI_Status Vulkan_NotifyOutOfBandVkQueue(HANDLE vkDevice, HANDLE queueHandle,
                                                      NV_VULKAN_OUT_OF_BAND_QUEUE_TYPE queueType);
};
