#include "pch.h"
#include "RCAS_Vk.h"
#include "precompile/RCAS_Shader_Vk.h"
#include "precompile/da_das_sharpen_Shader_Vk.h"
#include "precompile/da_rcas_sharpen_Shader_Vk.h"
#include <Config.h>

RCAS_Vk::RCAS_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    if (InDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_FUNC();

    // 1. Setup Standard RCAS (Using Base Resources)
    CreateSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    CreateConstantBuffer(sizeof(InternalConstants));

    std::vector<VkDescriptorSetLayoutBinding> bindings = { CreateBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
                                                           CreateBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
                                                           CreateBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
                                                           CreateBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) };
    CreateLayouts(bindings);

    std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _maxFramesInFlight },
                                                    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                      2 * _maxFramesInFlight },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _maxFramesInFlight } };
    CreateDescriptorPool(poolSizes, _maxFramesInFlight);
    CreateDescriptorSets(_descriptorSetLayout, _descriptorPool, _descriptorSets);

    // 2. Setup DA & DASDA Shared Resources
    std::vector<VkDescriptorSetLayoutBinding> bindingsDA = {
        CreateBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
        CreateBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        CreateBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        CreateBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER), CreateBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
    };

    VkDescriptorSetLayoutCreateInfo layoutInfoDA {};
    layoutInfoDA.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfoDA.bindingCount = static_cast<uint32_t>(bindingsDA.size());
    layoutInfoDA.pBindings = bindingsDA.data();
    vkCreateDescriptorSetLayout(_device, &layoutInfoDA, nullptr, &_descriptorSetLayoutDA);

    VkPipelineLayoutCreateInfo pipelineLayoutInfoDA {};
    pipelineLayoutInfoDA.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfoDA.setLayoutCount = 1;
    pipelineLayoutInfoDA.pSetLayouts = &_descriptorSetLayoutDA;
    vkCreatePipelineLayout(_device, &pipelineLayoutInfoDA, nullptr, &_pipelineLayoutDA);

    std::vector<VkDescriptorPoolSize> poolSizesDA = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _maxFramesInFlight },
                                                      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                        3 * _maxFramesInFlight },
                                                      { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _maxFramesInFlight } };

    VkDescriptorPoolCreateInfo poolInfoDA {};
    poolInfoDA.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfoDA.poolSizeCount = static_cast<uint32_t>(poolSizesDA.size());
    poolInfoDA.pPoolSizes = poolSizesDA.data();
    poolInfoDA.maxSets = _maxFramesInFlight;
    vkCreateDescriptorPool(_device, &poolInfoDA, nullptr, &_descriptorPoolDA);

    // Reuse base class definition to safely allocate descriptor sets
    Shader_Vk::CreateDescriptorSets(_descriptorSetLayoutDA, _descriptorPoolDA, _descriptorSetsDA);

    Shader_Vk::CreateBufferResource(_device, _physicalDevice, &_constantBufferDA, &_constantBufferMemoryDA,
                                    sizeof(InternalConstantsDA), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkMapMemory(_device, _constantBufferMemoryDA, 0, sizeof(InternalConstantsDA), 0, &_mappedConstantBufferDA);

    // 3. Compile Pipelines
    std::vector<char> shaderCode(rcas_spv, rcas_spv + sizeof(rcas_spv));
    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, shaderCode))
    {
        LOG_ERROR("Failed to create pipeline for RCAS");
        _init = false;
        return;
    }

    std::vector<char> shaderCodeDA(da_rcas_sharpen_spv, da_rcas_sharpen_spv + sizeof(da_rcas_sharpen_spv));
    if (!CreateComputePipeline(_device, _pipelineLayoutDA, &_pipelineDA, shaderCodeDA))
    {
        LOG_ERROR("Failed to create pipeline for DA RCAS");
        _init = false;
        return;
    }

    std::vector<char> shaderCodeDASDA(da_das_sharpen_spv, da_das_sharpen_spv + sizeof(da_das_sharpen_spv));
    if (!CreateComputePipeline(_device, _pipelineLayoutDA, &_pipelineDASDA, shaderCodeDASDA))
    {
        LOG_ERROR("Failed to create pipeline for DASDA RCAS");
        _init = false;
        return;
    }

    _init = true;
}

RCAS_Vk::~RCAS_Vk()
{
    if (_pipelineDA != VK_NULL_HANDLE)
        vkDestroyPipeline(_device, _pipelineDA, nullptr);
    if (_pipelineDASDA != VK_NULL_HANDLE)
        vkDestroyPipeline(_device, _pipelineDASDA, nullptr);
    if (_descriptorPoolDA != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(_device, _descriptorPoolDA, nullptr);
    if (_descriptorSetLayoutDA != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(_device, _descriptorSetLayoutDA, nullptr);
    if (_pipelineLayoutDA != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(_device, _pipelineLayoutDA, nullptr);
    if (_constantBufferDA != VK_NULL_HANDLE)
        vkDestroyBuffer(_device, _constantBufferDA, nullptr);
    if (_constantBufferMemoryDA != VK_NULL_HANDLE)
        vkFreeMemory(_device, _constantBufferMemoryDA, nullptr);
}

void RCAS_Vk::UpdateDescriptorSet(VkCommandBuffer cmdList, int setIndex, VkImageView inputView, VkImageView motionView,
                                  VkImageView outputView)
{
    if (motionView == VK_NULL_HANDLE)
        motionView = inputView;

    VkDescriptorSet descriptorSet = _descriptorSets[setIndex];
    VkDescriptorBufferInfo bufferInfo { _constantBuffer, 0, sizeof(InternalConstants) };
    VkDescriptorImageInfo sourceInfo { _textureSampler, inputView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo motionInfo { _textureSampler, motionView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo destInfo { VK_NULL_HANDLE, outputView, VK_IMAGE_LAYOUT_GENERAL };

    std::vector<VkWriteDescriptorSet> writes = {
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          nullptr, &bufferInfo, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &sourceInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 2, 0, 1,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &motionInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          &destInfo, nullptr, nullptr }
    };

    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void RCAS_Vk::UpdateDescriptorSetDA(VkDescriptorSet descriptorSet, VkBuffer constantBuffer, VkImageView inputView,
                                    VkImageView motionView, VkImageView depthView, VkImageView outputView)
{
    if (motionView == VK_NULL_HANDLE)
        motionView = inputView;

    VkDescriptorBufferInfo bufferInfo { constantBuffer, 0, sizeof(InternalConstantsDA) };
    VkDescriptorImageInfo sourceInfo { _textureSampler, inputView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo motionInfo { _textureSampler, motionView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo depthInfo { _textureSampler, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo destInfo { VK_NULL_HANDLE, outputView, VK_IMAGE_LAYOUT_GENERAL };

    std::vector<VkWriteDescriptorSet> writes = {
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          nullptr, &bufferInfo, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &sourceInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 2, 0, 1,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &motionInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 3, 0, 1,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 4, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          &destInfo, nullptr, nullptr }
    };
    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

bool RCAS_Vk::DispatchRCAS(VkCommandBuffer InCmdList, RcasConstants InConstants, const VkImageInfo& InResourceInfo,
                           const VkImageInfo& InMotionVectorsInfo, const VkImageInfo& OutResourceInfo)
{
    InternalConstants constants {};
    constants.OutputWidth = OutResourceInfo.Width;
    constants.OutputHeight = OutResourceInfo.Height;
    constants.MotionWidth = InMotionVectorsInfo.Width;
    constants.MotionHeight = InMotionVectorsInfo.Height;
    FillMotionConstants(constants, InConstants);

    if (_mappedConstantBuffer)
        memcpy(_mappedConstantBuffer, &constants, sizeof(InternalConstants));

    _currentSetIndex = (_currentSetIndex + 1) % _maxFramesInFlight;
    UpdateDescriptorSet(InCmdList, _currentSetIndex, InResourceInfo.ImageView, InMotionVectorsInfo.ImageView,
                        OutResourceInfo.ImageView);

    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1,
                            &_descriptorSets[_currentSetIndex], 0, nullptr);

    uint32_t groupX = (constants.OutputWidth + 15) / 16;
    uint32_t groupY = (constants.OutputHeight + 15) / 16;
    vkCmdDispatch(InCmdList, groupX, groupY, 1);
    return true;
}

bool RCAS_Vk::DispatchDepthAdaptive(VkCommandBuffer InCmdList, RcasConstants InConstants,
                                    const VkImageInfo& InResourceInfo, const VkImageInfo& InMotionVectorsInfo,
                                    const VkImageInfo& OutResourceInfo, VkImageInfo* InDepthInfo, bool isDAS)
{
    VkPipeline targetPipeline = isDAS ? _pipelineDASDA : _pipelineDA;
    if (InDepthInfo == VK_NULL_HANDLE || targetPipeline == VK_NULL_HANDLE)
        return false;

    InternalConstantsDA constants {};
    constants.OutputWidth = OutResourceInfo.Width;
    constants.OutputHeight = OutResourceInfo.Height;
    constants.MotionWidth = InMotionVectorsInfo.Width;
    constants.MotionHeight = InMotionVectorsInfo.Height;
    constants.DepthWidth = InDepthInfo->Width;
    constants.DepthHeight = InDepthInfo->Height;
    FillMotionConstants(constants, InConstants);

    if (_mappedConstantBufferDA)
        memcpy(_mappedConstantBufferDA, &constants, sizeof(InternalConstantsDA));

    _currentSetIndex = (_currentSetIndex + 1) % _maxFramesInFlight;
    UpdateDescriptorSetDA(_descriptorSetsDA[_currentSetIndex], _constantBufferDA, InResourceInfo.ImageView,
                          InMotionVectorsInfo.ImageView, InDepthInfo->ImageView, OutResourceInfo.ImageView);

    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, targetPipeline);
    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayoutDA, 0, 1,
                            &_descriptorSetsDA[_currentSetIndex], 0, nullptr);

    uint32_t groupX = (constants.OutputWidth + 15) / 16;
    uint32_t groupY = (constants.OutputHeight + 15) / 16;
    vkCmdDispatch(InCmdList, groupX, groupY, 1);
    return true;
}

bool RCAS_Vk::Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, RcasConstants InConstants,
                       const VkImageInfo& InResourceInfo, const VkImageInfo& InMotionVectorsInfo,
                       const VkImageInfo& OutResourceInfo, VkImageInfo* InDepthInfo)
{
    if (!_init || InDevice == VK_NULL_HANDLE || InCmdList == VK_NULL_HANDLE ||
        State::Instance().currentFeature == nullptr)
        return false;

    auto sharpnessShader = Config::Instance()->SharpnessShader.value_or_default();
    if (sharpnessShader == SharpenShader::LocalContrastDepthAware)
    {
        return DispatchDepthAdaptive(InCmdList, InConstants, InResourceInfo, InMotionVectorsInfo, OutResourceInfo,
                                     InDepthInfo, true);
    }
    else if (sharpnessShader == SharpenShader::DepthAware)
    {
        return DispatchDepthAdaptive(InCmdList, InConstants, InResourceInfo, InMotionVectorsInfo, OutResourceInfo,
                                     InDepthInfo, false);
    }
    else if (sharpnessShader == SharpenShader::RCAS)
    {
        return DispatchRCAS(InCmdList, InConstants, InResourceInfo, InMotionVectorsInfo, OutResourceInfo);
    }

    return false;
}
