#pragma once

#include <memory>
#include "DlssNr_Status.h"
#include <shaders/Shader_Vk.h>
#include <shaders/dlssnr/DlssNr_Common.h>

class DlssNr_Vk;
struct DlssNrFrameInfo_Vk;
namespace DlssNr
{
// Each shader instance owns its model feature, images, parameter block and timing queries.
class ModelVk
{
    struct Impl;
    std::unique_ptr<Impl> _impl;

  public:
    explicit ModelVk(DlssNr_Vk& shader);
    ~ModelVk();
    bool Evaluate(VkCommandBuffer cmd, const VkImageInfo& colour, const VkImageInfo& depth, const VkImageInfo& motion,
                  const VkImageInfo& output, const DlssNrFrameInfo_Vk& frame, VkInstance instance,
                  VkPhysicalDevice physicalDevice, VkDevice device, VkImageLayout inputLayout);
};
} // namespace DlssNr
