#include "pch.h"
#include "input_antilag_vk.h"

static void LogResult(InputResult result, const char* call)
{
    if (result != InputResult::Ok && result != InputResult::UsingDifferentInput && result != InputResult::NoReadyOutput)
    {
        LOG_ERROR("{} result: {}", call, magic_enum::enum_name(result));
    }
}

void VKAPI_CALL InputAntiLagVk::AntiLagUpdate(VkDevice device, const VkAntiLagDataAMD* pData)
{
    if (device == VK_NULL_HANDLE || pData == nullptr)
        return;

    auto presentationInfo = pData->pPresentationInfo;
    auto& context = presentationInfo != nullptr ? inputContext : noFrameIdContext;
    auto pDevice = (IUnknown*) device;

    // Every update carries the mode, once a frame is enough
    if (presentationInfo == nullptr || presentationInfo->stage == VK_ANTI_LAG_STAGE_INPUT_AMD)
    {
        // Driver control follows AMD's driver settings, which OptiScaler can't see
        SleepMode sleepMode {};
        sleepMode.low_latency_enabled = pData->mode == VK_ANTI_LAG_MODE_ON_AMD;
        sleepMode.minimum_interval_us =
            pData->maxFPS > 0 ? static_cast<uint32_t>(std::round(1'000'000.0 / pData->maxFPS)) : 0;

        LogResult(InputCommon::set_sleep_mode(context, pDevice, &sleepMode), "set_sleep_mode");
    }

    MarkerParams markerParams {};

    if (presentationInfo == nullptr)
    {
        // The delay before processing the input, as AntiLag 2 for D3D
        LogResult(InputCommon::sleep(context, pDevice), "sleep");

        markerParams.frame_id = 0;
        markerParams.marker_type = MarkerType::SIMULATION_START;
    }
    else if (presentationInfo->stage == VK_ANTI_LAG_STAGE_INPUT_AMD)
    {
        LogResult(InputCommon::sleep(context, pDevice, (uint32_t) presentationInfo->frameIndex), "sleep");

        markerParams.frame_id = presentationInfo->frameIndex;
        markerParams.marker_type = MarkerType::SIMULATION_START;
    }
    else
    {
        // Before vkQueuePresentKHR
        markerParams.frame_id = presentationInfo->frameIndex;
        markerParams.marker_type = MarkerType::PRESENT_START;
    }

    LogResult(InputCommon::set_marker(context, pDevice, markerParams), "set_marker");
}
