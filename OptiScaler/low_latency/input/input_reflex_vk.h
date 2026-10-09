#pragma once

#include "input_common.h"

#include <vulkan/vulkan.h>

// The game's Vulkan Reflex through VK_NV_low_latency2, it needs the driver's extension
class InputReflexVk
{
    const static inline InputContext inputContext { .caller = LowLatencyInput::Reflex,
                                                    .noFrameId = false,
                                                    .markerMode = InputMarkerMode::FullMarkers,
                                                    .api = API::Vulkan };

  public:
    static VkResult VKAPI_CALL SetLatencySleepMode(VkDevice device, VkSwapchainKHR swapchain,
                                                   const VkLatencySleepModeInfoNV* pSleepModeInfo);
    static VkResult VKAPI_CALL LatencySleep(VkDevice device, VkSwapchainKHR swapchain,
                                            const VkLatencySleepInfoNV* pSleepInfo);
    static void VKAPI_CALL SetLatencyMarker(VkDevice device, VkSwapchainKHR swapchain,
                                            const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo);
    static void VKAPI_CALL GetLatencyTimings(VkDevice device, VkSwapchainKHR swapchain,
                                             VkGetLatencyMarkerInfoNV* pLatencyMarkerInfo);
    static void VKAPI_CALL QueueNotifyOutOfBand(VkQueue queue, const VkOutOfBandQueueTypeInfoNV* pQueueTypeInfo);
};
