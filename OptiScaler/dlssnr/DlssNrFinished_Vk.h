#pragma once
#include <shaders/dlssnr/DlssNr_Vk.h>
#include <memory>

namespace DlssNr
{
// Per-upscaler presentation state. Global entry points only dispatch to live owners.
class FinishedVk
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    FinishedVk(DlssNr_Vk& shader, VkDevice device, VkPhysicalDevice physicalDevice);
    ~FinishedVk();
    void Capture(VkCommandBuffer cmd, const VkImageInfo& depth, const VkImageInfo& motion,
                 const DlssNrFrameInfo_Vk& frame, VkInstance instance);
    void Submitted(VkQueue queue, VkCommandBuffer cmd);
    void Reset(VkCommandBuffer cmd);
    void ResetPool(VkCommandPool pool);
    bool Present(VkQueue queue, VkPresentInfoKHR* present);
};
void FinishedVkSwapchain(VkDevice device, VkSwapchainKHR swapchain, const VkSwapchainCreateInfoKHR& info);
void FinishedVkSubmitted(VkQueue queue, VkCommandBuffer cmd);
void FinishedVkReset(VkCommandBuffer cmd);
void FinishedVkResetPool(VkCommandPool pool);
void FinishedVkPresent(VkQueue queue, VkPresentInfoKHR* present);
std::string FinishedVkStatus();
} // namespace DlssNr
