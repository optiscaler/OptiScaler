#pragma once

#include <vulkan/vulkan.h>

#include <shaders/dlssnr/DlssNr_Common.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>

// DLSS 5 Neural Rendering on Vulkan, natively.
//
// The model ships a complete Vulkan surface -- fourteen exported entry points against D3D12's ten,
// including both extension requirement queries -- so a Vulkan game has no need of the D3D12 bridge
// this pass reached it through before. What stopped it was never the model; it was the device. NGX
// loads its kernels through two NVIDIA vendor extensions no game enables, and a Vulkan device's
// extension list is fixed at creation, so asking afterwards gets a permanent no. OptiScaler already
// appends them in its vkCreateDevice hook, and Enshrouded confirmed the device is created with them.
//
// What differs from the D3D12 path, and why:
//
//   * The game's colour, depth and motion arrive already wrapped. NGX hands Vulkan resources over as
//     NVSDK_NGX_Resource_VK, so only this pass's own images need wrapping.
//   * Layouts are explicit. There is no equivalent of a D3D12 resource state promotion, so every
//     image is moved to the layout each dispatch needs and moved back.
//   * There is no root signature to save and restore, so no envelope. The bindless hazard that cost
//     007 First Light a device on D3D12 has no Vulkan counterpart.
//
// The composition shader is shared, compiled from the same source to SPIR-V. Any behavioural
// difference between the two backends is a bug rather than a design.

class Config;

namespace DlssNr
{

// Runs the model over what the upscaler just wrote, on the same command buffer.
//
// Everything Vulkan needs that D3D12 does not is passed rather than looked up: the device handles
// belong to the game's instance and there is no ambient place to find them from here.
//
// Safe to call every frame. It builds what it needs on first use and disables itself for the session
// rather than retrying into a crash.
void EvaluateAfterUpscaleVk(VkCommandBuffer cmdBuffer, NVSDK_NGX_Parameter* params, VkInstance instance,
                            VkPhysicalDevice physicalDevice, VkDevice device);

// Whether the native Vulkan path is up, and why not if it is not.
bool IsRunningVk();
const char* FailureReasonVk();

void ShutdownVk();

} // namespace DlssNr
