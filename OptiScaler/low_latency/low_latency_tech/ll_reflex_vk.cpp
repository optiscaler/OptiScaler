#include "pch.h"
#include "ll_reflex_vk.h"

#include <hooks/Vulkan_Hooks.h>
#include "ll_reflex.h"

bool ReflexVk::init(IUnknown* pDevice)
{
    if (pDevice == nullptr)
        return false;

    if (VulkanHooks::o_vkSetLatencySleepModeNV == nullptr || VulkanHooks::o_vkLatencySleepNV == nullptr ||
        VulkanHooks::o_vkSetLatencyMarkerNV == nullptr || VulkanHooks::o_vkCreateSemaphore == nullptr)
    {
        LOG_INFO("Reflex needs VK_NV_low_latency2");
        return false;
    }

    auto vkDevice = (VkDevice) pDevice;

    o_WaitSemaphores = (PFN_vkWaitSemaphores) VulkanHooks::GetDeviceProcAddr(vkDevice, "vkWaitSemaphores");

    if (o_WaitSemaphores == nullptr)
        o_WaitSemaphores = (PFN_vkWaitSemaphores) VulkanHooks::GetDeviceProcAddr(vkDevice, "vkWaitSemaphoresKHR");

    o_DestroySemaphore = (PFN_vkDestroySemaphore) VulkanHooks::GetDeviceProcAddr(vkDevice, "vkDestroySemaphore");

    if (o_WaitSemaphores == nullptr || o_DestroySemaphore == nullptr)
        return false;

    // The driver signals it when the sleep ends
    VkSemaphoreTypeCreateInfo timelineInfo {};
    timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;

    VkSemaphoreCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &timelineInfo;

    if (VulkanHooks::o_vkCreateSemaphore(vkDevice, &createInfo, nullptr, &semaphore) != VK_SUCCESS)
    {
        LOG_ERROR("Can't create the Reflex semaphore");
        return false;
    }

    device = vkDevice;
    semaphore_value = 0;
    LOG_INFO("Reflex Vulkan initialized");
    return true;
}

void ReflexVk::deinit()
{
    // Leave the swapchain without low latency for the next output
    if (auto current = swapchain(); current != VK_NULL_HANDLE && applied_swapchain == current)
    {
        VkLatencySleepModeInfoNV info {};
        info.sType = VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV;
        VulkanHooks::o_vkSetLatencySleepModeNV(device, current, &info);
    }

    if (semaphore != VK_NULL_HANDLE)
        o_DestroySemaphore(device, semaphore, nullptr);

    semaphore = VK_NULL_HANDLE;
    applied_swapchain = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
    LOG_INFO("Reflex Vulkan deinitialized");
}

VkSwapchainKHR ReflexVk::swapchain()
{
    if (device == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    return VulkanHooks::LowLatencySwapchain();
}

bool ReflexVk::is_enabled()
{
    auto force = RealReflexForceState();
    return force != ForceReflex::InGame ? force == ForceReflex::ForceEnable : low_latency_enabled;
}

void ReflexVk::apply_sleep_mode(VkSwapchainKHR swapchain)
{
    if (!sleep_mode_known)
        return;

    VkLatencySleepModeInfoNV info {};
    info.sType = VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV;
    info.lowLatencyMode = is_enabled();
    info.lowLatencyBoost = sleep_mode.low_latency_boost;
    info.minimumIntervalUs = sleep_mode.minimum_interval_us;

    auto result = VulkanHooks::o_vkSetLatencySleepModeNV(device, swapchain, &info);
    applied_enabled = info.lowLatencyMode;
    applied_swapchain = swapchain;

    LOG_INFO("Reflex Vulkan sleep mode, low latency: {}, boost: {}, interval: {} us, result: {}",
             (int) info.lowLatencyMode, (int) info.lowLatencyBoost, info.minimumIntervalUs, (int) result);
}

void ReflexVk::get_sleep_status(SleepParams* sleep_params)
{
    // VK_NV_low_latency2 has no sleep status
    sleep_params->low_latency_enabled = is_enabled();
    sleep_params->fullscreen_vrr = true;
    sleep_params->control_panel_vsync_override = false;
}

void ReflexVk::set_sleep_mode(SleepMode* sleep_mode)
{
    // Some games set the same sleep mode every frame
    if (sleep_mode_known && is_enabled() == applied_enabled && *sleep_mode == this->sleep_mode)
        return;

    low_latency_enabled = sleep_mode->low_latency_enabled;
    this->sleep_mode = *sleep_mode;
    sleep_mode_known = true;

    if (auto current = swapchain(); current != VK_NULL_HANDLE)
        apply_sleep_mode(current);
}

void ReflexVk::sleep(std::optional<uint32_t> frame_id)
{
    auto current = swapchain();

    if (current == VK_NULL_HANDLE)
        return;

    // A new swapchain starts without the sleep mode, Force State in the menu can change at any time
    if (current != applied_swapchain || (sleep_mode_known && is_enabled() != applied_enabled))
        apply_sleep_mode(current);

    // Without a sleep mode the driver might never signal
    if (current != applied_swapchain)
        return;

    VkLatencySleepInfoNV info {};
    info.sType = VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV;
    info.signalSemaphore = semaphore;
    info.value = ++semaphore_value;

    if (VulkanHooks::o_vkLatencySleepNV(device, current, &info) != VK_SUCCESS)
        return;

    VkSemaphoreWaitInfo waitInfo {};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &semaphore;
    waitInfo.pValues = &info.value;

    // A frame never waits a second, more is a driver that won't signal
    if (auto result = o_WaitSemaphores(device, &waitInfo, 1'000'000'000); result != VK_SUCCESS)
        LOG_WARN("Reflex Vulkan sleep wait: {}", (int) result);
}

void ReflexVk::set_marker(IUnknown* pDevice, const MarkerParams& marker_params)
{
    auto current = swapchain();
    auto marker = ToVkLatencyMarker(marker_params.marker_type);

    if (current == VK_NULL_HANDLE || !marker.has_value())
        return;

    VkSetLatencyMarkerInfoNV info {};
    info.sType = VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV;
    info.presentID = marker_params.frame_id;
    info.marker = *marker;

    VulkanHooks::o_vkSetLatencyMarkerNV(device, current, &info);
}

void ReflexVk::set_async_marker(IUnknown* pCommandQueue, const MarkerParams& marker_params)
{
    // Out of band markers are ordinary markers in VK_NV_low_latency2
    set_marker(nullptr, marker_params);
}

VkResult ReflexVk::get_latency(VkGetLatencyMarkerInfoNV* latency_info)
{
    auto current = swapchain();

    if (current == VK_NULL_HANDLE || VulkanHooks::o_vkGetLatencyTimingsNV == nullptr)
        return VK_ERROR_INITIALIZATION_FAILED;

    VulkanHooks::o_vkGetLatencyTimingsNV(device, current, latency_info);
    return VK_SUCCESS;
}

void ReflexVk::notify_out_of_band(VkQueue queue, VkOutOfBandQueueTypeNV queue_type) const
{
    if (queue == VK_NULL_HANDLE || VulkanHooks::o_vkQueueNotifyOutOfBandNV == nullptr)
        return;

    VkOutOfBandQueueTypeInfoNV info {};
    info.sType = VK_STRUCTURE_TYPE_OUT_OF_BAND_QUEUE_TYPE_INFO_NV;
    info.queueType = queue_type;

    VulkanHooks::o_vkQueueNotifyOutOfBandNV(queue, &info);
}
