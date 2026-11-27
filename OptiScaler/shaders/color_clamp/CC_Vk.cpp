#include <pch.h>
#include "CC_Vk.h"
#include "precompile/color_clamp_Shader_Vk.h"

ColorClamp_Vk::ColorClamp_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
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

    VkPipelineLayoutCreateInfo pipelineLayoutInfo { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_descriptorSetLayout;

    if (vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_pipelineLayout) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] vkCreatePipelineLayout failed", _name);
        return;
    }

    CreateDescriptorPool({ { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, _maxFramesInFlight },
                           { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _maxFramesInFlight } },
                         _maxFramesInFlight);
    CreateDescriptorSets(_descriptorSetLayout, _descriptorPool, _descriptorSets);

    std::vector<char> shaderCode(color_clamp_spv, color_clamp_spv + sizeof(color_clamp_spv));
    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, shaderCode))
    {
        LOG_ERROR("[{0}] Failed to create pipeline!", _name);
        return;
    }

    // CreateDescriptorSets sizes the vector before allocating, a failed allocation leaves null sets
    _init = _descriptorSets.size() == _maxFramesInFlight &&
            std::ranges::none_of(_descriptorSets, [](VkDescriptorSet set) { return set == VK_NULL_HANDLE; });
}

bool ColorClamp_Vk::CreateImage(uint32_t width, uint32_t height)
{
    return Shader_Vk::CreateImageResource(width, height, VK_FORMAT_R16G16B16A16_SFLOAT, 0);
}

bool ColorClamp_Vk::NeedsClamp(VkFormat InFormat)
{
    switch (InFormat)
    {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R16G16B16_SFLOAT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_R32G32B32_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        return true;

    default:
        return false;
    }
}

bool ColorClamp_Vk::Dispatch(VkCommandBuffer InCmdList, VkImageView InResourceView)
{
    if (!_init || InCmdList == VK_NULL_HANDLE || InResourceView == VK_NULL_HANDLE ||
        _intermediateImageView == VK_NULL_HANDLE)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _currentSetIndex = (_currentSetIndex + 1) % _maxFramesInFlight;

    VkDescriptorSet descriptorSet = _descriptorSets[_currentSetIndex];
    VkDescriptorImageInfo sourceInfo { VK_NULL_HANDLE, InResourceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo destInfo { VK_NULL_HANDLE, _intermediateImageView, VK_IMAGE_LAYOUT_GENERAL };

    VkWriteDescriptorSet writes[2] = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1,
                                         VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &sourceInfo, nullptr, nullptr },
                                       { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
                                         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &destInfo, nullptr, nullptr } };

    vkUpdateDescriptorSets(_device, _countof(writes), writes, 0, nullptr);

    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &descriptorSet, 0,
                            nullptr);

    vkCmdDispatch(InCmdList, (_width + InNumThreadsX - 1) / InNumThreadsX,
                  (_height + InNumThreadsY - 1) / InNumThreadsY, 1);

    return true;
}
