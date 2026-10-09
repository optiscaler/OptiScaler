#pragma once

#include "low_latency_tech.h"

#include <vulkan/vulkan.h>

// VK_NV_low_latency2 has no PC latency ping, the out of band markers come one earlier
inline std::optional<VkLatencyMarkerNV> ToVkLatencyMarker(MarkerType type)
{
    if (type == MarkerType::PC_LATENCY_PING)
        return std::nullopt;

    auto value = (uint32_t) type;
    return (VkLatencyMarkerNV) (value > (uint32_t) MarkerType::PC_LATENCY_PING ? value - 1 : value);
}

inline MarkerType FromVkLatencyMarker(VkLatencyMarkerNV marker)
{
    auto value = (uint32_t) marker;
    return (MarkerType) (value >= (uint32_t) MarkerType::PC_LATENCY_PING ? value + 1 : value);
}

// NVIDIA Reflex for Vulkan through VK_NV_low_latency2 (nvapi's Vulkan Reflex is deprecated). Works on the swapchain
// created with low latency mode, OptiScaler enables it on swapchains it sees.
class ReflexVk : public LowLatencyTech
{
    VkDevice device = VK_NULL_HANDLE;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    uint64_t semaphore_value = 0;

    SleepMode sleep_mode {};
    bool sleep_mode_known = false;
    bool applied_enabled = false;
    VkSwapchainKHR applied_swapchain = VK_NULL_HANDLE; // Sleep mode is per swapchain

    PFN_vkWaitSemaphores o_WaitSemaphores = nullptr;
    PFN_vkDestroySemaphore o_DestroySemaphore = nullptr;

    // The swapchain the calls go to, VK_NULL_HANDLE until there is one with low latency mode
    VkSwapchainKHR swapchain();
    void apply_sleep_mode(VkSwapchainKHR swapchain);

  public:
    ReflexVk() : LowLatencyTech() {}

    // From LowLatencyTech
    bool init(IUnknown* pDevice) override;
    void deinit() override;

    LowLatencyMode get_mode() override { return LowLatencyMode::Reflex; };
    void* get_tech_context() override { return device; };
    void set_fg_type(bool interpolated, uint64_t frame_id) override {}; // Out of band markers carry this for Reflex
    void set_low_latency_override(ForceReflex low_latency_override) override
    {
        this->low_latency_override = low_latency_override;
    };
    void set_effective_fg_state(bool effective_fg_state) override { this->effective_fg_state = effective_fg_state; };

    bool is_enabled() override;

    void get_sleep_status(SleepParams* sleep_params) override;
    void set_sleep_mode(SleepMode* sleep_mode) override;
    void sleep(std::optional<uint32_t> frame_id) override;
    void set_marker(IUnknown* pDevice, const MarkerParams& marker_params) override;
    void set_async_marker(IUnknown* pCommandQueue, const MarkerParams& marker_params) override;

    // For passthrough
    VkResult get_latency(VkGetLatencyMarkerInfoNV* latency_info);
    void notify_out_of_band(VkQueue queue, VkOutOfBandQueueTypeNV queue_type) const;
};
