#include "pch.h"
#include "HudCopy_Vk.h"
#include <Config.h>
#include <State.h>
#include "precompile/HudCopy_Shader_Vk.h"

HudCopy_Vk::HudCopy_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    if (InDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    // Setup descriptor layouts (0: Params, 1: Hudless, 2: PresentCopy, 3: PresentOut)
    CreateSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    CreateConstantBuffer(sizeof(InternalHudCopyParams));

    std::vector<VkDescriptorSetLayoutBinding> bindings = { CreateBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
                                                           CreateBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
                                                           CreateBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
                                                           CreateBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) };
    CreateLayouts(bindings);

    std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _maxFramesInFlight },
                                                    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                      _maxFramesInFlight * 2 },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _maxFramesInFlight } };
    CreateDescriptorPool(poolSizes, _maxFramesInFlight);
    CreateDescriptorSets(_descriptorSetLayout, _descriptorPool, _descriptorSets);

    std::vector<char> shaderCode(HudCopy_spv, HudCopy_spv + sizeof(HudCopy_spv));
    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, shaderCode))
    {
        LOG_ERROR("[{0}] Failed to create pipeline!", _name);
        _init = false;
        return;
    }

    _init = true;
}

bool HudCopy_Vk::Dispatch(VkCommandBuffer InCmdList, const VkImageInfo& InHudlessInfo, VkImageLayout HudlessState,
                          const VkImageInfo& InPresentInfo, VkImageLayout PresentState, float HudDetectionThreshold)
{
    if (!_init || InCmdList == VK_NULL_HANDLE || InHudlessInfo.Image == VK_NULL_HANDLE ||
        InPresentInfo.Image == VK_NULL_HANDLE)
        return false;

    if (!CreateImageResource(InPresentInfo.Width, InPresentInfo.Height, InPresentInfo.Format,
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    {
        LOG_ERROR("[{0}] Failed to create or resize intermediate buffer", _name);
        return false;
    }

    _currentSetIndex = (_currentSetIndex + 1) % _maxFramesInFlight;

    VkImageSubresourceRange copySubresourceRange = InPresentInfo.SubresourceRange;
    copySubresourceRange.baseMipLevel = 0;
    copySubresourceRange.levelCount = 1;
    copySubresourceRange.baseArrayLayer = 0;
    copySubresourceRange.layerCount = 1;

    SetImageLayout(InCmdList, InPresentInfo.Image, PresentState, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   copySubresourceRange);
    SetImageLayout(InCmdList, _intermediateImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   copySubresourceRange);

    VkImageCopy copyRegion {};
    copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.extent = { InPresentInfo.Width, InPresentInfo.Height, 1 };

    vkCmdCopyImage(InCmdList, InPresentInfo.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _intermediateImage,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    SetImageLayout(InCmdList, InPresentInfo.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, copySubresourceRange);
    SetImageLayout(InCmdList, _intermediateImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                   copySubresourceRange);
    SetImageLayout(InCmdList, InHudlessInfo.Image, HudlessState, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   InHudlessInfo.SubresourceRange);

    InternalHudCopyParams constants {};
    constants.DiffThreshold = HudDetectionThreshold;

    if (_mappedConstantBuffer)
        memcpy(_mappedConstantBuffer, &constants, sizeof(InternalHudCopyParams));

    VkDescriptorSet descriptorSet = _descriptorSets[_currentSetIndex];

    VkDescriptorBufferInfo bufferInfo { _constantBuffer, 0, sizeof(InternalHudCopyParams) };
    VkDescriptorImageInfo hudlessInfo { _textureSampler, InHudlessInfo.ImageView,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo presentInfo { _textureSampler, InPresentInfo.ImageView,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo destUAVInfo { VK_NULL_HANDLE, _intermediateImageView, VK_IMAGE_LAYOUT_GENERAL };

    std::vector<VkWriteDescriptorSet> descriptorWritesBuffer = {
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          nullptr, &bufferInfo, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &hudlessInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 2, 0, 1,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &presentInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          &destUAVInfo, nullptr, nullptr }
    };

    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(descriptorWritesBuffer.size()), descriptorWritesBuffer.data(),
                           0, nullptr);

    // Bind & Dispatch
    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &descriptorSet, 0,
                            nullptr);

    uint32_t dispatchWidth = (InPresentInfo.Width + InNumThreadsX - 1) / InNumThreadsX;
    uint32_t dispatchHeight = (InPresentInfo.Height + InNumThreadsY - 1) / InNumThreadsY;
    vkCmdDispatch(InCmdList, dispatchWidth, dispatchHeight, 1);

    // 3. Transition and copy the internal computed target back to 'present'
    SetImageLayout(InCmdList, _intermediateImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   copySubresourceRange);
    SetImageLayout(InCmdList, InPresentInfo.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copySubresourceRange);

    vkCmdCopyImage(InCmdList, _intermediateImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, InPresentInfo.Image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    // 4. Restore original resource states
    SetImageLayout(InCmdList, InPresentInfo.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, PresentState,
                   copySubresourceRange);
    SetImageLayout(InCmdList, InHudlessInfo.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, HudlessState,
                   InHudlessInfo.SubresourceRange);

    return true;
}
