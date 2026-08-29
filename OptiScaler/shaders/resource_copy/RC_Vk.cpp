#include <pch.h>
#include "RC_Vk.h"
#include <Config.h>
#include "precompile/rc_Shader_Vk.h"

ResourceCopy_Vk::ResourceCopy_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    if (InDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    // Setup descriptor layouts
    std::vector<VkDescriptorSetLayoutBinding> bindings = { CreateBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
                                                           CreateBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) };
    CreateLayouts(bindings);

    // Setup descriptor pool sizes and allocate sets
    std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, _maxFramesInFlight },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _maxFramesInFlight } };
    CreateDescriptorPool(poolSizes, _maxFramesInFlight);
    CreateDescriptorSets(_descriptorSetLayout, _descriptorPool, _descriptorSets);

    // Load precompiled shader and create compute pipeline
    std::vector<char> shaderCode(rc_spv, rc_spv + sizeof(rc_spv));
    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, shaderCode))
    {
        LOG_ERROR("[{0}] Failed to create pipeline!", _name);
        _init = false;
        return;
    }

    _init = true;
}

bool ResourceCopy_Vk::InitializeViews(VkImageView InResourceView, VkImageView OutResourceView)
{
    if (!_init || InResourceView == VK_NULL_HANDLE || OutResourceView == VK_NULL_HANDLE)
        return false;

    if (InResourceView != _currentInResourceView || OutResourceView != _currentOutResourceView)
    {
        _currentInResourceView = InResourceView;
        _currentOutResourceView = OutResourceView;
        return true;
    }

    return true;
}

bool ResourceCopy_Vk::Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, VkImageView InResourceView,
                               VkImageView OutResourceView, VkExtent2D OutExtent)
{
    if (!_init || InDevice == VK_NULL_HANDLE || InCmdList == VK_NULL_HANDLE || InResourceView == VK_NULL_HANDLE ||
        OutResourceView == VK_NULL_HANDLE)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    if (!InitializeViews(InResourceView, OutResourceView))
        return false;

    _currentSetIndex = (_currentSetIndex + 1) % _maxFramesInFlight;

    VkDescriptorSet descriptorSet = _descriptorSets[_currentSetIndex];
    VkDescriptorImageInfo sourceInfo { VK_NULL_HANDLE, InResourceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo destInfo { VK_NULL_HANDLE, OutResourceView, VK_IMAGE_LAYOUT_GENERAL };

    std::vector<VkWriteDescriptorSet> descriptorWritesBuffer = {
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          &sourceInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          &destInfo, nullptr, nullptr }
    };

    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(descriptorWritesBuffer.size()), descriptorWritesBuffer.data(),
                           0, nullptr);

    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1,
                            &_descriptorSets[_currentSetIndex], 0, nullptr);

    uint32_t dispatchWidth = (OutExtent.width + InNumThreadsX - 1) / InNumThreadsX;
    uint32_t dispatchHeight = (OutExtent.height + InNumThreadsY - 1) / InNumThreadsY;
    vkCmdDispatch(InCmdList, dispatchWidth, dispatchHeight, 1);

    return true;
}
