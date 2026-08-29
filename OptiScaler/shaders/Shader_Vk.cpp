#include "pch.h"
#include "Shader_Vk.h"
#include "Util.h"

Shader_Vk::Shader_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : _name(InName), _device(InDevice), _physicalDevice(InPhysicalDevice)
{
}

Shader_Vk::~Shader_Vk()
{
    if (_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(_device, _pipeline, nullptr);
        _pipeline = VK_NULL_HANDLE;
    }

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

// Implement the missing CreateDescriptorPool
void Shader_Vk::CreateDescriptorPool(const std::vector<VkDescriptorPoolSize>& poolSizes, uint32_t maxSets)
{
    VkDescriptorPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;

    if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] failed to create descriptor pool!", _name);
    }
}

// Implement the missing CreateSampler
void Shader_Vk::CreateSampler(VkFilter filter, VkSamplerAddressMode addressMode)
{
    VkSamplerCreateInfo samplerInfo {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(_device, &samplerInfo, nullptr, &_textureSampler) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] failed to create texture sampler!", _name);
    }
}

uint32_t Shader_Vk::FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                                   VkMemoryPropertyFlags properties)
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

    LOG_ERROR("Failed to find suitable memory type!");
    return -1;
}

bool Shader_Vk::CreateComputePipeline(VkDevice device, VkPipelineLayout pipelineLayout, VkPipeline* pipeline,
                                      const std::vector<char>& shaderCode, const char* entryPoint)
{
    VkShaderModule shaderModule;
    VkShaderModuleCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = shaderCode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create shader module!");
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo {};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = entryPoint;

    VkComputePipelineCreateInfo pipelineInfo {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = pipelineLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, pipeline) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create compute pipeline!");
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, shaderModule, nullptr);
    return true;
}

bool Shader_Vk::CreateBufferResource(VkDevice device, VkPhysicalDevice physicalDevice, VkBuffer* buffer,
                                     VkDeviceMemory* memory, VkDeviceSize size, VkBufferUsageFlags usage,
                                     VkMemoryPropertyFlags properties)
{
    if (*buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
    }

    if (*memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, *memory, nullptr);
        *memory = VK_NULL_HANDLE;
    }

    VkBufferCreateInfo bufferInfo {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, buffer) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create buffer!");
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, *buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, memory) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to allocate buffer memory!");
        return false;
    }

    vkBindBufferMemory(device, *buffer, *memory, 0);

    LOG_DEBUG("Created buffer size: {}", size);
    return true;
}

void Shader_Vk::SetBufferState(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize size,
                               VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                               VkPipelineStageFlags dstStage)
{
    VkBufferMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = size;

    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

VkDescriptorSetLayoutBinding Shader_Vk::CreateBinding(uint32_t binding, VkDescriptorType descriptorType,
                                                      uint32_t descriptorCount, VkShaderStageFlags stageFlags)
{
    VkDescriptorSetLayoutBinding layoutBinding {};
    layoutBinding.binding = binding;
    layoutBinding.descriptorType = descriptorType;
    layoutBinding.descriptorCount = descriptorCount;
    layoutBinding.stageFlags = stageFlags;
    layoutBinding.pImmutableSamplers = nullptr;
    return layoutBinding;
}

void Shader_Vk::CreateLayouts(const std::vector<VkDescriptorSetLayoutBinding>& bindings)
{
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

void Shader_Vk::CreateDescriptorSets(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorPool descriptorPool,
                                     std::vector<VkDescriptorSet>& descriptorSets)
{
    std::vector<VkDescriptorSetLayout> layouts(_maxFramesInFlight, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(_maxFramesInFlight);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(_maxFramesInFlight);
    if (vkAllocateDescriptorSets(_device, &allocInfo, descriptorSets.data()) != VK_SUCCESS)
    {
        LOG_ERROR("failed to allocate depth adaptive descriptor sets!");
    }
}

bool Shader_Vk::CreateImageResource(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage)
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

    if (vkCreateImage(_device, &imageInfo, nullptr, &_intermediateImage) != VK_SUCCESS)
    {
        LOG_ERROR("failed to create image!");
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(_device, _intermediateImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        FindMemoryType(_physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(_device, &allocInfo, nullptr, &_intermediateMemory) != VK_SUCCESS)
    {
        LOG_ERROR("failed to allocate image memory!");
        return false;
    }

    vkBindImageMemory(_device, _intermediateImage, _intermediateMemory, 0);

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

    if (vkCreateImageView(_device, &viewInfo, nullptr, &_intermediateImageView) != VK_SUCCESS)
    {
        LOG_ERROR("failed to create image view!");
        return false;
    }

    return true;
}

void Shader_Vk::ReleaseImageResource()
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

void Shader_Vk::SetImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout,
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

void Shader_Vk::CreateConstantBuffer(VkDeviceSize bufferSize)
{
    if (!CreateBufferResource(_device, _physicalDevice, &_constantBuffer, &_constantBufferMemory, bufferSize,
                              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
        LOG_ERROR("Failed to create constant buffer!");
        return;
    }

    vkMapMemory(_device, _constantBufferMemory, 0, bufferSize, 0, &_mappedConstantBuffer);
}
