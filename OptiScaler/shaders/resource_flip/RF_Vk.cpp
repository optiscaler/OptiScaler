#include <pch.h>
#include "RF_Vk.h"

#include <Config.h>

#include "precompiled/RF_Shader_Vk.h"

struct RFParams_Vk
{
    uint32_t width;
    uint32_t height;
    uint32_t offset;
    uint32_t velocity;
};

RF_Vk::RF_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    if (InDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    VkDescriptorSetLayoutBinding bindings[2] = { CreateBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
                                                 CreateBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) };

    VkDescriptorSetLayoutCreateInfo layoutInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.bindingCount = _countof(bindings);
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] vkCreateDescriptorSetLayout failed", _name);
        return;
    }

    VkPushConstantRange pushConstantRange { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RFParams_Vk) };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_pipelineLayout) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] vkCreatePipelineLayout failed", _name);
        return;
    }

    CreateDescriptorPool({ { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, _maxFramesInFlight },
                           { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _maxFramesInFlight } },
                         _maxFramesInFlight);
    CreateDescriptorSets(_descriptorSetLayout, _descriptorPool, _descriptorSets);

    std::vector<char> shaderCode(RF_spv, RF_spv + sizeof(RF_spv));
    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, shaderCode))
    {
        LOG_ERROR("[{0}] Failed to create pipeline!", _name);
        return;
    }

    _init = _descriptorSets.size() == _maxFramesInFlight;
}

bool RF_Vk::Dispatch(VkCommandBuffer InCmdList, VkImageView InResourceView, VkImageView OutResourceView,
                     VkExtent2D InImageExtent, uint32_t width, uint32_t height, bool velocity)
{
    if (!_init || InCmdList == VK_NULL_HANDLE || InResourceView == VK_NULL_HANDLE ||
        OutResourceView == VK_NULL_HANDLE || width == 0 || height == 0)
    {
        return false;
    }

    LOG_DEBUG("[{0}] Start!", _name);

    _currentSetIndex = (_currentSetIndex + 1) % _maxFramesInFlight;
    VkDescriptorSet currentSet = _descriptorSets[_currentSetIndex];

    VkDescriptorImageInfo sourceInfo { VK_NULL_HANDLE, InResourceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo destInfo { VK_NULL_HANDLE, OutResourceView, VK_IMAGE_LAYOUT_GENERAL };

    VkWriteDescriptorSet writes[2] = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, currentSet, 0, 0, 1,
                                         VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &sourceInfo, nullptr, nullptr },
                                       { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, currentSet, 1, 0, 1,
                                         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &destInfo, nullptr, nullptr } };

    vkUpdateDescriptorSets(_device, _countof(writes), writes, 0, nullptr);

    RFParams_Vk params {};
    params.width = width - 1;
    params.height = height - 1;
    params.offset = Config::Instance()->FGResourceFlipOffset.value_or_default() && InImageExtent.height > height
                        ? InImageExtent.height - height
                        : 0;
    params.velocity = velocity ? 1 : 0;

    LOG_DEBUG("Width: {}, Height: {}, Offset: {}", params.width, params.height, params.offset);

    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &currentSet, 0, nullptr);
    vkCmdPushConstants(InCmdList, _pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);

    // Covers the used area moved down by the offset
    uint32_t dispatchWidth = (width + InNumThreadsX - 1) / InNumThreadsX;
    uint32_t dispatchHeight = (height + params.offset + InNumThreadsY - 1) / InNumThreadsY;
    vkCmdDispatch(InCmdList, dispatchWidth, dispatchHeight, 1);

    return true;
}
