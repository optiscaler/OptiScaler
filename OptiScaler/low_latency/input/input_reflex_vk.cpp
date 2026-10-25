#include "pch.h"
#include "input_reflex_vk.h"

#include "input_reflex.h"

#include <hooks/Vulkan_Hooks.h>
#include <low_latency/low_latency_tech/ll_reflex_vk.h>

#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

static void LogResult(InputResult result, const char* call)
{
    if (result != InputResult::Ok && result != InputResult::UsingDifferentInput &&
        result != InputResult::NotEnoughReports && result != InputResult::NoReadyOutput)
    {
        LOG_ERROR("{} result: {}", call, magic_enum::enum_name(result));
    }
}

const InputContext& InputReflexVk::Context(void* returnAddress)
{
    return InputReflex::FromOptiScaler(returnAddress) ? optiScalerContext : inputContext;
}

VkResult VKAPI_CALL InputReflexVk::SetLatencySleepMode(VkDevice device, VkSwapchainKHR swapchain,
                                                       const VkLatencySleepModeInfoNV* pSleepModeInfo)
{
    if (device == VK_NULL_HANDLE || swapchain == VK_NULL_HANDLE)
        return VK_ERROR_INITIALIZATION_FAILED;

    // The game's swapchain has the low latency mode, the Reflex output uses it
    VulkanHooks::SetLowLatencySwapchain(swapchain);

    // Without the info low latency is off
    SleepMode sleepMode {};
    sleepMode.use_markers_to_optimize = true;

    if (pSleepModeInfo != nullptr)
    {
        sleepMode.low_latency_enabled = pSleepModeInfo->lowLatencyMode;
        sleepMode.low_latency_boost = pSleepModeInfo->lowLatencyBoost;
        sleepMode.minimum_interval_us = pSleepModeInfo->minimumIntervalUs;
    }

    auto& context = Context(_ReturnAddress());

    // OptiScaler's Streamline always asks for low latency with DLSSG. With only its present markers the driver's low
    // latency sleep costs a lot of frames, its sleep still limits. Force State can enable it.
    if (&context == &optiScalerContext)
        sleepMode.low_latency_enabled = false;

    LogResult(InputCommon::set_sleep_mode(context, (IUnknown*) device, &sleepMode), "set_sleep_mode");
    return VK_SUCCESS;
}

VkResult VKAPI_CALL InputReflexVk::LatencySleep(VkDevice device, VkSwapchainKHR swapchain,
                                                const VkLatencySleepInfoNV* pSleepInfo)
{
    if (device == VK_NULL_HANDLE || swapchain == VK_NULL_HANDLE || pSleepInfo == nullptr)
        return VK_ERROR_INITIALIZATION_FAILED;

    VulkanHooks::SetLowLatencySwapchain(swapchain);

    LogResult(InputCommon::sleep(Context(_ReturnAddress()), (IUnknown*) device), "sleep");

    // The game waits for it whatever the output did, the Reflex output's sleep has its own
    VkSemaphoreSignalInfo signalInfo {};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    signalInfo.semaphore = pSleepInfo->signalSemaphore;
    signalInfo.value = pSleepInfo->value;

    return VulkanHooks::SignalSemaphore(device, &signalInfo);
}

void VKAPI_CALL InputReflexVk::SetLatencyMarker(VkDevice device, VkSwapchainKHR swapchain,
                                                const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo)
{
    if (device == VK_NULL_HANDLE || swapchain == VK_NULL_HANDLE || pLatencyMarkerInfo == nullptr)
        return;

    VulkanHooks::SetLowLatencySwapchain(swapchain);

    // Out of band markers are ordinary markers here
    MarkerParams markerParams {};
    markerParams.frame_id = pLatencyMarkerInfo->presentID;
    markerParams.marker_type = FromVkLatencyMarker(pLatencyMarkerInfo->marker);

    LogResult(InputCommon::set_marker(Context(_ReturnAddress()), (IUnknown*) device, markerParams), "set_marker");
}

void VKAPI_CALL InputReflexVk::GetLatencyTimings(VkDevice device, VkSwapchainKHR swapchain,
                                                 VkGetLatencyMarkerInfoNV* pLatencyMarkerInfo)
{
    if (device == VK_NULL_HANDLE || swapchain == VK_NULL_HANDLE || pLatencyMarkerInfo == nullptr)
        return;

    // The driver's own with the Reflex output on the game's swapchain
    if (auto reflex = InputCommon::reflex_vk_output(); reflex != nullptr &&
                                                       VulkanHooks::LowLatencySwapchain() == swapchain &&
                                                       reflex->get_latency(pLatencyMarkerInfo) == VK_SUCCESS)
    {
        return;
    }

    FrameReport reports[NVAPI_BUFFER_SIZE] {};
    auto result = InputCommon::get_frame_reports(Context(_ReturnAddress()), (IUnknown*) device, reports);
    LogResult(result, "get_frame_reports");

    // Oldest first, the empty ones are at the start
    uint32_t available = 0;

    if (result == InputResult::Ok)
    {
        for (auto& report : reports)
            available += report.frameID != 0 ? 1 : 0;
    }

    if (pLatencyMarkerInfo->pTimings == nullptr)
    {
        pLatencyMarkerInfo->timingCount = available;
        return;
    }

    // The latest ones
    auto count = std::min(pLatencyMarkerInfo->timingCount, available);
    auto first = NVAPI_BUFFER_SIZE - count;

    for (uint32_t i = 0; i < count; i++)
    {
        auto& in = reports[first + i];
        auto& out = pLatencyMarkerInfo->pTimings[i];

        out.presentID = in.frameID;
        out.inputSampleTimeUs = in.inputSampleTime;
        out.simStartTimeUs = in.simStartTime;
        out.simEndTimeUs = in.simEndTime;
        out.renderSubmitStartTimeUs = in.renderSubmitStartTime;
        out.renderSubmitEndTimeUs = in.renderSubmitEndTime;
        out.presentStartTimeUs = in.presentStartTime;
        out.presentEndTimeUs = in.presentEndTime;
        out.driverStartTimeUs = in.driverStartTime;
        out.driverEndTimeUs = in.driverEndTime;
        out.osRenderQueueStartTimeUs = in.osRenderQueueStartTime;
        out.osRenderQueueEndTimeUs = in.osRenderQueueEndTime;
        out.gpuRenderStartTimeUs = in.gpuRenderStartTime;
        out.gpuRenderEndTimeUs = in.gpuRenderEndTime;
    }

    pLatencyMarkerInfo->timingCount = count;
}

void VKAPI_CALL InputReflexVk::QueueNotifyOutOfBand(VkQueue queue, const VkOutOfBandQueueTypeInfoNV* pQueueTypeInfo)
{
    if (queue == VK_NULL_HANDLE || pQueueTypeInfo == nullptr)
        return;

    // Only the Reflex output has a use for it
    if (auto reflex = InputCommon::reflex_vk_output())
        reflex->notify_out_of_band(queue, pQueueTypeInfo->queueType);
}
