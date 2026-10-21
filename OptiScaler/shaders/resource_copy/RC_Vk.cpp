#include <pch.h>

#include "RC_Vk.h"

#include <Config.h>

#include "precompile/rc_Shader_Vk.h"

ResourceCopy_Vk::ResourceCopy_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : ResourceCopy_Vk(InName, InDevice, InPhysicalDevice, std::vector<char>(rc_spv, rc_spv + sizeof(rc_spv)))
{
}

ResourceCopy_Vk::ResourceCopy_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice,
                                 const std::vector<char>& InShaderCode)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    if (InDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    CreateDescriptorSetLayout();
    CreateDescriptorPool();
    CreateDescriptorSets();

    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, InShaderCode))
    {
        LOG_ERROR("[{0}] Failed to create pipeline!", _name);
        _init = false;
        return;
    }

    _init = true;
}

ResourceCopy_Vk::~ResourceCopy_Vk()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

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

    ReleaseImageResource();
}

void ResourceCopy_Vk::CreateDescriptorSetLayout()
{
    // Binding 0: Source (Sampled Image)
    VkDescriptorSetLayoutBinding sourceLayoutBinding {};
    sourceLayoutBinding.binding = 0;
    sourceLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    sourceLayoutBinding.descriptorCount = 1;
    sourceLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Dest (Storage Image)
    VkDescriptorSetLayoutBinding destLayoutBinding {};
    destLayoutBinding.binding = 1;
    destLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    destLayoutBinding.descriptorCount = 1;
    destLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> bindings = { sourceLayoutBinding, destLayoutBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] failed to create descriptor set layout!", _name);
        return;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_descriptorSetLayout;

    if (vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_pipelineLayout) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] failed to create pipeline layout!", _name);
    }
}

void ResourceCopy_Vk::CreateDescriptorPool()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) }
    };

    VkDescriptorPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] failed to create descriptor pool!", _name);
    }
}

void ResourceCopy_Vk::CreateDescriptorSets()
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
        LOG_ERROR("[{0}] failed to allocate descriptor sets!", _name);
    }
}

void ResourceCopy_Vk::UpdateDescriptorSet(VkCommandBuffer cmdList, int setIndex, VkImageView inputView,
                                          VkImageView outputView)
{
    VkDescriptorSet descriptorSet = _descriptorSets[setIndex];

    // 0: Source
    VkDescriptorImageInfo sourceInfo {};
    sourceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sourceInfo.imageView = inputView;

    VkWriteDescriptorSet descriptorWriteSource {};
    descriptorWriteSource.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWriteSource.dstSet = descriptorSet;
    descriptorWriteSource.dstBinding = 0;
    descriptorWriteSource.dstArrayElement = 0;
    descriptorWriteSource.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    descriptorWriteSource.descriptorCount = 1;
    descriptorWriteSource.pImageInfo = &sourceInfo;

    // 1: Dest
    VkDescriptorImageInfo destInfo {};
    destInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    destInfo.imageView = outputView;
    destInfo.sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet descriptorWriteDest {};
    descriptorWriteDest.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWriteDest.dstSet = descriptorSet;
    descriptorWriteDest.dstBinding = 1;
    descriptorWriteDest.dstArrayElement = 0;
    descriptorWriteDest.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWriteDest.descriptorCount = 1;
    descriptorWriteDest.pImageInfo = &destInfo;

    std::vector<VkWriteDescriptorSet> descriptorWritesBuffer = { descriptorWriteSource, descriptorWriteDest };

    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(descriptorWritesBuffer.size()), descriptorWritesBuffer.data(),
                           0, nullptr);
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

    // Prepare descriptors
    _currentSetIndex = (_currentSetIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    UpdateDescriptorSet(InCmdList, _currentSetIndex, InResourceView, OutResourceView);

    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);

    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1,
                            &_descriptorSets[_currentSetIndex], 0, nullptr);

    // Dispatch
    uint32_t dispatchWidth = (OutExtent.width + InNumThreadsX - 1) / InNumThreadsX;
    uint32_t dispatchHeight = (OutExtent.height + InNumThreadsY - 1) / InNumThreadsY;
    vkCmdDispatch(InCmdList, dispatchWidth, dispatchHeight, 1);

    return true;
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

bool ResourceCopy_Vk::CreateImageResource(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width,
                                          uint32_t height, VkFormat format, VkImageUsageFlags usage)
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
        LOG_ERROR("[{0}] failed to create image!", _name);
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
        LOG_ERROR("[{0}] failed to allocate image memory!", _name);
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
        LOG_ERROR("[{0}] failed to create image view!", _name);
        return false;
    }

    return true;
}

void ResourceCopy_Vk::ReleaseImageResource()
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

void ResourceCopy_Vk::SetImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout,
                                     VkImageLayout newLayout, VkImageSubresourceRange subresourceRange)
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
