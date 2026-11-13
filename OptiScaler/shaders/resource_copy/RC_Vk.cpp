#include <pch.h>
#include "RC_Vk.h"
#include <Config.h>
#include "precompile/rc_offset_Shader_Vk.h"

struct RCParams_Vk
{
    uint32_t SrcOffset[2];
    uint32_t DstOffset[2];
    uint32_t Size[2];
};

ResourceCopy_Vk::ResourceCopy_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
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

    VkPushConstantRange pushConstantRange { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RCParams_Vk) };

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

    std::vector<char> shaderCode(rc_offset_spv, rc_offset_spv + sizeof(rc_offset_spv));
    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, shaderCode))
    {
        LOG_ERROR("[{0}] Failed to create pipeline!", _name);
        return;
    }

    // CreateDescriptorSets sizes the vector before allocating, a failed allocation leaves null sets
    _init = _descriptorSets.size() == _maxFramesInFlight &&
            std::ranges::none_of(_descriptorSets, [](VkDescriptorSet set) { return set == VK_NULL_HANDLE; });
}

VkFormat ResourceCopy_Vk::BufferFormat(VkFormat InFormat, bool InDepth) const
{
    // Depth formats can't be storage images, the shader reads the depth aspect as float
    if (InDepth)
        return VK_FORMAT_R32_SFLOAT;

    // sRGB, BGRA and others without storage support, float keeps the precision of what the shader reads
    VkFormatProperties properties {};
    vkGetPhysicalDeviceFormatProperties(_physicalDevice, InFormat, &properties);

    if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        return VK_FORMAT_R16G16B16A16_SFLOAT;

    return InFormat;
}

bool ResourceCopy_Vk::CreateGrowingImage(uint32_t width, uint32_t height, VkFormat format)
{
    if (_intermediateImage != VK_NULL_HANDLE && _format == format)
    {
        width = std::max(width, _width);
        height = std::max(height, _height);
    }

    return Shader_Vk::CreateImageResource(width, height, format, 0);
}

bool ResourceCopy_Vk::Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, VkImageView InResourceView,
                               VkImageView OutResourceView, VkExtent2D OutExtent, VkOffset2D InSrc, VkOffset2D InDst)
{
    if (!_init || InDevice == VK_NULL_HANDLE || InCmdList == VK_NULL_HANDLE || InResourceView == VK_NULL_HANDLE ||
        OutResourceView == VK_NULL_HANDLE)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _currentSetIndex = (_currentSetIndex + 1) % _maxFramesInFlight;

    VkDescriptorSet descriptorSet = _descriptorSets[_currentSetIndex];
    VkDescriptorImageInfo sourceInfo { VK_NULL_HANDLE, InResourceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo destInfo { VK_NULL_HANDLE, OutResourceView, VK_IMAGE_LAYOUT_GENERAL };

    VkWriteDescriptorSet writes[2] = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1,
                                         VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &sourceInfo, nullptr, nullptr },
                                       { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
                                         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &destInfo, nullptr, nullptr } };

    vkUpdateDescriptorSets(_device, _countof(writes), writes, 0, nullptr);

    RCParams_Vk params {};
    params.SrcOffset[0] = (uint32_t) InSrc.x;
    params.SrcOffset[1] = (uint32_t) InSrc.y;
    params.DstOffset[0] = (uint32_t) InDst.x;
    params.DstOffset[1] = (uint32_t) InDst.y;
    params.Size[0] = OutExtent.width;
    params.Size[1] = OutExtent.height;

    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &descriptorSet, 0,
                            nullptr);
    vkCmdPushConstants(InCmdList, _pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);

    uint32_t dispatchWidth = (OutExtent.width + InNumThreadsX - 1) / InNumThreadsX;
    uint32_t dispatchHeight = (OutExtent.height + InNumThreadsY - 1) / InNumThreadsY;
    vkCmdDispatch(InCmdList, dispatchWidth, dispatchHeight, 1);

    return true;
}
