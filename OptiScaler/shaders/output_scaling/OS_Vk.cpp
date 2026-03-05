#include "pch.h"
#include "OS_Vk.h"

#include "OS_Common.h"

#define A_CPU

#include "precompile/BCDS_bicubic_Shader_Vk.h"
#include "precompile/BCDS_catmull_Shader_Vk.h"
#include "precompile/bcds_lanczos2_Shader_Vk.h"
#include "precompile/bcds_lanczos3_Shader_Vk.h"
#include "precompile/bcds_kaiser2_Shader_Vk.h"
#include "precompile/bcds_kaiser3_Shader_Vk.h"
#include "precompile/BCDS_magc_Shader_Vk.h"

#include "precompile/BCUS_Shader_Vk.h"

#include "fsr1/ffx_fsr1.h"
#include "fsr1/FSR_EASU_Shader_Vk.h"

#include <Config.h>

static Constants constants {};
static UpscaleShaderConstants fsr1Constants {};

#pragma warning(disable : 4244)

OS_Vk::OS_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice, bool InUpsample)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    _upsample = InUpsample;

    if (InDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_FUNC();

    VkSamplerCreateInfo samplerInfo {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // Linear filtering
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    // Clamp to edge (matches your variable name)
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    // defaults
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(InDevice, &samplerInfo, nullptr, &_textureSampler) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create texture sampler!");
        return;
    }

    CreateDescriptorSetLayout();
    CreateConstantBuffer();
    CreateDescriptorPool();
    CreateDescriptorSets();

    std::vector<char> shaderCode;

    // fsr upscaling
    if (Config::Instance()->OutputScalingDownscaler.value_or_default() == Scaler::FSR1)
    {
        shaderCode = std::vector<char>(FSR_EASU_spv, FSR_EASU_spv + sizeof(FSR_EASU_spv));
    }
    else
    {
        if (_upsample)
        {
            shaderCode = std::vector<char>(bcus_spv, bcus_spv + sizeof(bcus_spv));
        }
        else
        {
            switch (Config::Instance()->OutputScalingDownscaler.value_or_default())
            {
            case Scaler::Bicubic:
                shaderCode = std::vector<char>(bcds_bicubic_spv, bcds_bicubic_spv + sizeof(bcds_bicubic_spv));
                break;

            case Scaler::CatmullRom:
                shaderCode = std::vector<char>(bcds_catmull_spv, bcds_catmull_spv + sizeof(bcds_catmull_spv));
                break;

            case Scaler::Lanczos2:
                shaderCode = std::vector<char>(bcds_lanczos2_spv, bcds_lanczos2_spv + sizeof(bcds_lanczos2_spv));
                break;

            case Scaler::Lanczos3:
                shaderCode = std::vector<char>(bcds_lanczos3_spv, bcds_lanczos3_spv + sizeof(bcds_lanczos3_spv));
                break;

            case Scaler::Kaiser2:
                shaderCode = std::vector<char>(bcds_kaiser2_spv, bcds_kaiser2_spv + sizeof(bcds_kaiser2_spv));
                break;

            case Scaler::Kaiser3:
                shaderCode = std::vector<char>(bcds_kaiser3_spv, bcds_kaiser3_spv + sizeof(bcds_kaiser3_spv));
                break;

            case Scaler::Magic:
                shaderCode = std::vector<char>(bcds_magc_spv, bcds_magc_spv + sizeof(bcds_magc_spv));
                break;

            default:
                shaderCode = std::vector<char>(bcds_bicubic_spv, bcds_bicubic_spv + sizeof(bcds_bicubic_spv));
                break;
            }
        }
    }
    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, shaderCode))
    {
        LOG_ERROR("Failed to create pipeline for RCAS_Vk");
        _init = false;
        return;
    }

    _init = true;
}

OS_Vk::~OS_Vk()
{
    if (_descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
        _descriptorPool = VK_NULL_HANDLE;
    }

    if (_descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(_device, _descriptorSetLayout, nullptr);
        _descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (_pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
        _pipelineLayout = VK_NULL_HANDLE;
    }

    if (_constantBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(_device, _constantBuffer, nullptr);
        _constantBuffer = VK_NULL_HANDLE;
    }

    if (_constantBufferMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(_device, _constantBufferMemory, nullptr);
        _constantBufferMemory = VK_NULL_HANDLE;
    }

    if (_textureSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(_device, _textureSampler, nullptr);
        _textureSampler = VK_NULL_HANDLE;
    }

    ReleaseImageResource();
}

void OS_Vk::CreateDescriptorSetLayout()
{
    // Binding 0: ConstantBuffer
    VkDescriptorSetLayoutBinding uboLayoutBinding {};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Source (Sampled Image) - NOT combined sampler for FSR
    VkDescriptorSetLayoutBinding sourceLayoutBinding {};
    sourceLayoutBinding.binding = 1;
    sourceLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    sourceLayoutBinding.descriptorCount = 1;
    sourceLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Dest (Storage Image)
    VkDescriptorSetLayoutBinding destLayoutBinding {};
    destLayoutBinding.binding = 2;
    destLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    destLayoutBinding.descriptorCount = 1;
    destLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    VkDescriptorSetLayoutBinding samplerLayoutBinding {};

    samplerLayoutBinding.binding = 3;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = VK_NULL_HANDLE; // &_textureSampler;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings = { uboLayoutBinding, sourceLayoutBinding, destLayoutBinding, samplerLayoutBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS)
    {
        LOG_ERROR("failed to create descriptor set layout!");
        return;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_descriptorSetLayout;

    if (vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_pipelineLayout) != VK_SUCCESS)
    {
        LOG_ERROR("failed to create pipeline layout!");
    }
}

void OS_Vk::CreateDescriptorPool()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) },
        { VK_DESCRIPTOR_TYPE_SAMPLER, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) }
    };

    VkDescriptorPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool) != VK_SUCCESS)
    {
        LOG_ERROR("failed to create descriptor pool!");
    }
}

void OS_Vk::CreateDescriptorSets()
{
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, _descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = _descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    _descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(_device, &allocInfo, _descriptorSets.data()) != VK_SUCCESS)
    {
        LOG_ERROR("failed to allocate descriptor sets!");
    }
}

void OS_Vk::UpdateDescriptorSet(VkCommandBuffer cmdList, int setIndex, VkImageView inputView, VkImageView outputView)
{
    VkDescriptorSet descriptorSet = _descriptorSets[setIndex];

    // 0: UBO
    VkDescriptorBufferInfo bufferInfo {};
    bufferInfo.buffer = _constantBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(Constants);

    VkWriteDescriptorSet descriptorWriteUBO {};
    descriptorWriteUBO.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWriteUBO.dstSet = descriptorSet;
    descriptorWriteUBO.dstBinding = 0;
    descriptorWriteUBO.dstArrayElement = 0;
    descriptorWriteUBO.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWriteUBO.descriptorCount = 1;
    descriptorWriteUBO.pBufferInfo = &bufferInfo;

    // 1: Source (Sampled Image or Combined Image Sampler)
    VkDescriptorImageInfo sourceInfo {};
    sourceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sourceInfo.imageView = inputView;
    sourceInfo.sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet descriptorWriteSource {};
    descriptorWriteSource.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWriteSource.dstSet = descriptorSet;
    descriptorWriteSource.dstBinding = 1;
    descriptorWriteSource.dstArrayElement = 0;
    descriptorWriteSource.descriptorCount = 1;
    descriptorWriteSource.pImageInfo = &sourceInfo;
    descriptorWriteSource.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

    // 2: Dest
    VkDescriptorImageInfo destInfo {};
    destInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    destInfo.imageView = outputView;
    destInfo.sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet descriptorWriteDest {};
    descriptorWriteDest.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWriteDest.dstSet = descriptorSet;
    descriptorWriteDest.dstBinding = 2;
    descriptorWriteDest.dstArrayElement = 0;
    descriptorWriteDest.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWriteDest.descriptorCount = 1;
    descriptorWriteDest.pImageInfo = &destInfo;

    std::vector<VkWriteDescriptorSet> descriptorWritesBuffer;

    VkDescriptorImageInfo imageInfo {};
    VkWriteDescriptorSet descriptorWriteSampler {};

    imageInfo.sampler = _textureSampler;

    descriptorWriteSampler.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWriteSampler.dstSet = descriptorSet;
    descriptorWriteSampler.dstBinding = 3;
    descriptorWriteSampler.dstArrayElement = 0;
    descriptorWriteSampler.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    descriptorWriteSampler.descriptorCount = 1;
    descriptorWriteSampler.pImageInfo = &imageInfo;

    descriptorWritesBuffer = { descriptorWriteUBO, descriptorWriteSource, descriptorWriteDest, descriptorWriteSampler };

    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(descriptorWritesBuffer.size()), descriptorWritesBuffer.data(),
                           0, nullptr);
}

bool OS_Vk::Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, VkImageView InResourceView,
                     VkImageView OutResourceView, VkExtent2D OutExtent)
{
    if (!_init || InDevice == VK_NULL_HANDLE || InCmdList == VK_NULL_HANDLE)
        return false;

    FsrEasuCon(fsr1Constants.const0, fsr1Constants.const1, fsr1Constants.const2, fsr1Constants.const3,
               State::Instance().currentFeature->TargetWidth(), State::Instance().currentFeature->TargetHeight(),
               State::Instance().currentFeature->TargetWidth(), State::Instance().currentFeature->TargetHeight(),
               State::Instance().currentFeature->DisplayWidth(), State::Instance().currentFeature->DisplayHeight());

    constants.srcWidth = State::Instance().currentFeature->TargetWidth();
    constants.srcHeight = State::Instance().currentFeature->TargetHeight();
    constants.destWidth = State::Instance().currentFeature->DisplayWidth();
    constants.destHeight = State::Instance().currentFeature->DisplayHeight();

    if (Config::Instance()->OutputScalingDownscaler.value_or_default() == Scaler::FSR1)
    {
        if (_mappedConstantBuffer)
        {
            memcpy(_mappedConstantBuffer, &fsr1Constants, sizeof(UpscaleShaderConstants));
        }
    }
    else
    {
        if (_mappedConstantBuffer)
        {
            memcpy(_mappedConstantBuffer, &constants, sizeof(Constants));
        }
    }

    // Prepare descriptors
    _currentSetIndex = (_currentSetIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    UpdateDescriptorSet(InCmdList, _currentSetIndex, InResourceView, OutResourceView);

    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);

    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1,
                            &_descriptorSets[_currentSetIndex], 0, nullptr);

    // Dispatch
    if (Config::Instance()->OutputScalingDownscaler.value_or_default() == Scaler::FSR1 || _upsample)
    {
        uint32_t groupX = (OutExtent.width + 15) / 16;
        uint32_t groupY = (OutExtent.height + 15) / 16;
        vkCmdDispatch(InCmdList, groupX, groupY, 1);
    }
    else
    {
        uint32_t groupX = (OutExtent.width + 7) / 8;
        uint32_t groupY = (OutExtent.height + 7) / 8;
        vkCmdDispatch(InCmdList, groupX, groupY, 1);
    }

    return true;
}

bool OS_Vk::CreateBufferResource(VkDevice device, VkPhysicalDevice physicalDevice, VkBuffer* buffer,
                                 VkDeviceMemory* memory, VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties)
{
    return Shader_Vk::CreateBufferResource(device, physicalDevice, buffer, memory, size, usage, properties);
}

void OS_Vk::SetBufferState(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize size, VkAccessFlags srcAccess,
                           VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
    Shader_Vk::SetBufferState(commandBuffer, buffer, size, srcAccess, dstAccess, srcStage, dstStage);
}

static uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    return 0;
}

bool OS_Vk::CreateImageResource(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
                                VkFormat format, VkImageUsageFlags usage)
{
    if (_intermediateImage != VK_NULL_HANDLE && _width == width && _height == height && _format == format)
        return true;

    _width = width;
    _height = height;
    _format = format;

    ReleaseImageResource();

    VkImageCreateInfo imageInfo {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    if (vkCreateImage(device, &imageInfo, nullptr, &_intermediateImage) != VK_SUCCESS)
    {
        LOG_ERROR("failed to create image!");
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, _intermediateImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &_intermediateMemory) != VK_SUCCESS)
    {
        LOG_ERROR("failed to allocate image memory!");
        return false;
    }

    vkBindImageMemory(device, _intermediateImage, _intermediateMemory, 0);

    VkImageViewCreateInfo viewInfo {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = _intermediateImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &_intermediateImageView) != VK_SUCCESS)
    {
        LOG_ERROR("failed to create image view!");
        return false;
    }

    return true;
}

void OS_Vk::ReleaseImageResource()
{
    if (_intermediateImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(_device, _intermediateImageView, nullptr);
        _intermediateImageView = VK_NULL_HANDLE;
    }

    if (_intermediateImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(_device, _intermediateImage, nullptr);
        _intermediateImage = VK_NULL_HANDLE;
    }

    if (_intermediateMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(_device, _intermediateMemory, nullptr);
        _intermediateMemory = VK_NULL_HANDLE;
    }
}

void OS_Vk::SetImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                           VkImageSubresourceRange subresourceRange)
{
    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = subresourceRange;

    // Basic setting, might need refinement based on exact usage
    barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        barrier.srcAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL)
    {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }

    if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    else if (newLayout == VK_IMAGE_LAYOUT_GENERAL)
    {
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmdBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void OS_Vk::CreateConstantBuffer()
{
    VkDeviceSize bufferSize;

    if (Config::Instance()->OutputScalingDownscaler.value_or_default() == Scaler::FSR1)
        bufferSize = sizeof(UpscaleShaderConstants);
    else
        bufferSize = sizeof(Constants);

    // Create buffer using Shader_Vk helper
    if (!Shader_Vk::CreateBufferResource(_device, _physicalDevice, &_constantBuffer, &_constantBufferMemory, bufferSize,
                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
        LOG_ERROR("Failed to create constant buffer!");
        return;
    }

    vkMapMemory(_device, _constantBufferMemory, 0, bufferSize, 0, &_mappedConstantBuffer);
}
