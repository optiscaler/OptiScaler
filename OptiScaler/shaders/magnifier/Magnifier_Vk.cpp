#include "pch.h"
#include "Magnifier_Vk.h"

#include <Config.h>
#include "precompile/Magnifier_Shader_Vk.h"

Magnifier_Vk::Magnifier_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    if (InDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    // Setup descriptor layouts
    CreateSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    CreateConstantBuffer(sizeof(InternalMagnifierParams));

    std::vector<VkDescriptorSetLayoutBinding> bindings = { CreateBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
                                                           CreateBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
                                                           CreateBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) };
    CreateLayouts(bindings);

    // Setup descriptor pool sizes and allocate sets
    std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _maxFramesInFlight },
                                                    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, _maxFramesInFlight },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _maxFramesInFlight } };
    CreateDescriptorPool(poolSizes, _maxFramesInFlight);
    CreateDescriptorSets(_descriptorSetLayout, _descriptorPool, _descriptorSets);

    // Load precompiled shader and create compute pipeline
    std::vector<char> shaderCode(Magnifier_spv, Magnifier_spv + sizeof(Magnifier_spv));
    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, shaderCode))
    {
        LOG_ERROR("[{0}] Failed to create pipeline!", _name);
        _init = false;
        return;
    }

    _init = true;
}

bool Magnifier_Vk::InitializeViews(VkImageView InResourceView, VkImageView OutResourceView)
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

bool Magnifier_Vk::Dispatch(VkCommandBuffer InCmdList, const VkImageInfo& InResourceInfo,
                            const VkImageInfo& OutResourceInfo)
{
    if (!_init || InCmdList == VK_NULL_HANDLE)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    if (!InitializeViews(InResourceInfo.ImageView, OutResourceInfo.ImageView))
        return false;

    _currentSetIndex = (_currentSetIndex + 1) % _maxFramesInFlight;

    InternalMagnifierParams constants {};

    FilloutStruct((float) OutResourceInfo.Width, (float) OutResourceInfo.Height, constants);

    if (_mappedConstantBuffer)
        memcpy(_mappedConstantBuffer, &constants, sizeof(InternalMagnifierParams));

    VkDescriptorSet descriptorSet = _descriptorSets[_currentSetIndex];

    VkDescriptorBufferInfo bufferInfo { _constantBuffer, 0, sizeof(InternalMagnifierParams) };
    VkDescriptorImageInfo sourceInfo { _textureSampler, InResourceInfo.ImageView,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo destInfo { VK_NULL_HANDLE, OutResourceInfo.ImageView, VK_IMAGE_LAYOUT_GENERAL };

    std::vector<VkWriteDescriptorSet> descriptorWritesBuffer = {
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          nullptr, &bufferInfo, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &sourceInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          &destInfo, nullptr, nullptr }
    };

    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(descriptorWritesBuffer.size()), descriptorWritesBuffer.data(),
                           0, nullptr);

    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1,
                            &_descriptorSets[_currentSetIndex], 0, nullptr);

    uint32_t dispatchWidth = (OutResourceInfo.Width + InNumThreadsX - 1) / InNumThreadsX;
    uint32_t dispatchHeight = (OutResourceInfo.Height + InNumThreadsY - 1) / InNumThreadsY;
    vkCmdDispatch(InCmdList, dispatchWidth, dispatchHeight, 1);

    return true;
}
