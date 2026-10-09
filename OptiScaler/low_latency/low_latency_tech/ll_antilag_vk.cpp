#include "pch.h"
#include "ll_antilag_vk.h"
#include <hooks/Vulkan_Hooks.h>

// fakenvapi starts it without a device, its markers bring one
bool AntiLagVk::init(IUnknown* pDevice)
{
    device = (VkDevice) pDevice;
    return VulkanHooks::o_vkAntiLagUpdateAMD != nullptr;
}

void* AntiLagVk::get_tech_context() { return nullptr; }

void AntiLagVk::update(VkAntiLagStageAMD stage, std::optional<uint64_t> frame_id)
{
    if (device == VK_NULL_HANDLE || VulkanHooks::o_vkAntiLagUpdateAMD == nullptr)
        return;

    VkAntiLagPresentationInfoAMD presentationInfo = {};
    presentationInfo.sType = VK_STRUCTURE_TYPE_ANTI_LAG_PRESENTATION_INFO_AMD;
    presentationInfo.stage = stage;
    presentationInfo.frameIndex = frame_id.value_or(0);

    VkAntiLagDataAMD antiLagData = {};
    antiLagData.sType = VK_STRUCTURE_TYPE_ANTI_LAG_DATA_AMD;
    antiLagData.mode = is_enabled() ? VK_ANTI_LAG_MODE_ON_AMD : VK_ANTI_LAG_MODE_OFF_AMD;
    antiLagData.maxFPS = max_fps;

    // Without a frame id it's only the delay, as AntiLag 2's update
    antiLagData.pPresentationInfo = frame_id.has_value() ? &presentationInfo : nullptr;

    LOG_TRACE_LOWLATENCY("AntiLag {}: {}, status: {}", stage == VK_ANTI_LAG_STAGE_INPUT_AMD ? "Input" : "Present",
                         frame_id.value_or(0), is_enabled());

    VulkanHooks::o_vkAntiLagUpdateAMD(device, &antiLagData);
}

void AntiLagVk::get_sleep_status(SleepParams* sleep_params)
{
    sleep_params->low_latency_enabled = is_enabled();
    sleep_params->fullscreen_vrr = true;
    sleep_params->control_panel_vsync_override = false;
}

void AntiLagVk::set_sleep_mode(SleepMode* sleep_mode)
{
    // UNUSED:
    // low_latency_boost
    // use_markers_to_optimize

    low_latency_enabled = sleep_mode->low_latency_enabled;
    max_fps =
        sleep_mode->minimum_interval_us > 0 ? (uint32_t) std::round(1000000.0f / sleep_mode->minimum_interval_us) : 0;
}

void AntiLagVk::sleep(std::optional<uint32_t> frame_id)
{
    // Inputs that sleep with the frame id (AntiLag Vk) also send its simulation start
    if (frame_id.has_value())
    {
        if (!last_input_frame_id.has_value() || (uint32_t) *last_input_frame_id != *frame_id)
        {
            last_input_frame_id = *frame_id;
            update(VK_ANTI_LAG_STAGE_INPUT_AMD, *frame_id);
        }

        return;
    }

    // The simulation start markers with frame ids are better
    if (++sleeps_since_id_marker > 20)
        update(VK_ANTI_LAG_STAGE_INPUT_AMD, std::nullopt);
}

void AntiLagVk::set_marker(IUnknown* pDevice, const MarkerParams& marker_params)
{
    if (pDevice != nullptr)
        device = (VkDevice) pDevice;

    call_count++;

    if (marker_params.marker_type == MarkerType::OUT_OF_BAND_PRESENT_START)
    {
        last_oob_present = call_count;
        using_oob_present = true;
    }

    if (using_oob_present && call_count - last_oob_present > 10)
        using_oob_present = false;

    // Frame id 0 is what inputs without frame ids send, their sleep calls are the input stage
    if (marker_params.frame_id == 0)
        return;

    if (marker_params.marker_type == MarkerType::SIMULATION_START)
    {
        sleeps_since_id_marker = 0;

        if (!last_input_frame_id.has_value() || (uint32_t) *last_input_frame_id != (uint32_t) marker_params.frame_id)
        {
            last_input_frame_id = marker_params.frame_id;
            update(VK_ANTI_LAG_STAGE_INPUT_AMD, marker_params.frame_id);
        }
    }

    if ((marker_params.marker_type == MarkerType::PRESENT_START && !using_oob_present) ||
        marker_params.marker_type == MarkerType::OUT_OF_BAND_PRESENT_START)
    {
        update(VK_ANTI_LAG_STAGE_PRESENT_AMD, marker_params.frame_id);
    }
}
