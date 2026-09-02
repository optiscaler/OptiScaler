#include "pch.h"

#include "DlssNr_Vk.h"

#include "precompile/DlssNr_Shader_Vk.h"

#include <algorithm>
#include <cstring>

DlssNr_Vk::DlssNr_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    if (InDevice == VK_NULL_HANDLE || InPhysicalDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("DLSS-NR Vulkan pass: no device");
        _init = false;
        return;
    }

    _maxFramesInFlight = kFramesInFlight;

    // Linear, because the resolve reads the model's answer at a different size than it writes -- the
    // model may have run at a reduced resolution and the edit has to be stretched back over the frame.
    // The D3D12 pass uses a linear sampler for the same reason and the two must agree.
    CreateSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    // The constant ring. A uniform buffer binding can be offset into, but only to a multiple of the
    // device's own alignment, so the stride is the struct rounded up rather than the struct itself.
    VkPhysicalDeviceProperties props {};
    vkGetPhysicalDeviceProperties(_physicalDevice, &props);

    const VkDeviceSize alignment = std::max<VkDeviceSize>(props.limits.minUniformBufferOffsetAlignment, 1);
    _slotStride = ((sizeof(DlssNrConstants) + alignment - 1) / alignment) * alignment;

    if (!CreateBufferResource(_device, _physicalDevice, &_constantBuffer, &_constantBufferMemory,
                              _slotStride * kSlots, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
        LOG_ERROR("DLSS-NR Vulkan pass: could not allocate the constant ring");
        _init = false;
        return;
    }

    if (vkMapMemory(_device, _constantBufferMemory, 0, _slotStride * kSlots, 0, &_mappedConstantBuffer) != VK_SUCCESS)
    {
        LOG_ERROR("DLSS-NR Vulkan pass: could not map the constant ring");
        _init = false;
        return;
    }

    // The layout mirrors the [[vk::binding]] numbers in dlssnr.hlsl, entry for entry. Combined image
    // samplers for the reads: the shader declares its sampler separately, and a combined descriptor
    // satisfies a separately declared sampled image with the sampler half simply unused.
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        CreateBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),          // Params
        CreateBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),  // gSource
        CreateBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),  // gModel
        CreateBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),  // gOriginal
        CreateBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),  // gMotion
        CreateBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),           // gTarget
        CreateBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),           // gKeep
        CreateBinding(7, VK_DESCRIPTOR_TYPE_SAMPLER),                 // gLinear
    };

    CreateLayouts(bindings);

    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSlots },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * kSlots },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * kSlots },
        { VK_DESCRIPTOR_TYPE_SAMPLER, kSlots },
    };

    CreateDescriptorPool(poolSizes, kSlots);

    // One set per slot rather than per frame: two dispatches in the same frame need two sets, or the
    // second overwrites bindings the first has not consumed yet.
    _maxFramesInFlight = kSlots;
    CreateDescriptorSets(_descriptorSetLayout, _descriptorPool, _descriptorSets);
    _maxFramesInFlight = kFramesInFlight;

    if (_descriptorSets.size() < kSlots)
    {
        LOG_ERROR("DLSS-NR Vulkan pass: expected {} descriptor sets, got {}", kSlots, _descriptorSets.size());
        _init = false;
        return;
    }

    std::vector<char> shaderCode(dlssnr_spv, dlssnr_spv + sizeof(dlssnr_spv));

    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, shaderCode))
    {
        LOG_ERROR("DLSS-NR Vulkan pass: could not create the compute pipeline");
        _init = false;
        return;
    }

    _init = true;
    LOG_INFO("DLSS-NR Vulkan pass up: {} constant slots, stride {}", kSlots, (uint64_t) _slotStride);
}

DlssNr_Vk::~DlssNr_Vk()
{
    if (_device == VK_NULL_HANDLE)
        return;

    if (_dummyView != VK_NULL_HANDLE)
        vkDestroyImageView(_device, _dummyView, nullptr);

    if (_dummyImage != VK_NULL_HANDLE)
        vkDestroyImage(_device, _dummyImage, nullptr);

    if (_dummyMemory != VK_NULL_HANDLE)
        vkFreeMemory(_device, _dummyMemory, nullptr);
}

// One pixel, R16G16B16A16_SFLOAT so it is legal for both a sampled read and a storage write, moved
// once into GENERAL and left there. Its content is never read: it exists because Vulkan rejects a
// descriptor set with an unwritten binding, and the shader declares all seven resources at file scope
// whichever mode is running.
bool DlssNr_Vk::CreateDummy(VkCommandBuffer cmdList)
{
    if (_dummyReady)
        return true;

    if (_dummyImage == VK_NULL_HANDLE)
    {
        VkImageCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        info.extent = { 1, 1, 1 };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(_device, &info, nullptr, &_dummyImage) != VK_SUCCESS)
        {
            LOG_ERROR("DLSS-NR Vulkan pass: could not create the placeholder image");
            return false;
        }

        VkMemoryRequirements req {};
        vkGetImageMemoryRequirements(_device, _dummyImage, &req);

        VkMemoryAllocateInfo alloc {};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex =
            FindMemoryType(_physicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(_device, &alloc, nullptr, &_dummyMemory) != VK_SUCCESS ||
            vkBindImageMemory(_device, _dummyImage, _dummyMemory, 0) != VK_SUCCESS)
        {
            LOG_ERROR("DLSS-NR Vulkan pass: could not back the placeholder image");
            return false;
        }

        VkImageViewCreateInfo view {};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = _dummyImage;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        if (vkCreateImageView(_device, &view, nullptr, &_dummyView) != VK_SUCCESS)
        {
            LOG_ERROR("DLSS-NR Vulkan pass: could not view the placeholder image");
            return false;
        }
    }

    // GENERAL satisfies both a sampled read and a storage write, so the placeholder can stand in for
    // either kind of slot without ever changing layout again.
    VkImageSubresourceRange range { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    SetImageLayout(cmdList, _dummyImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, range);

    _dummyReady = true;
    return true;
}

void DlssNr_Vk::WriteDescriptors(VkDescriptorSet set, VkDeviceSize constantOffset, VkImageView source,
                                 VkImageView model, VkImageView original, VkImageView motion, VkImageView target,
                                 VkImageView keep)
{
    VkDescriptorBufferInfo bufferInfo { _constantBuffer, constantOffset, sizeof(DlssNrConstants) };

    // A read slot standing in for nothing is bound in GENERAL, which is the layout the placeholder is
    // left in. A real read is in SHADER_READ_ONLY_OPTIMAL, which the caller arranged.
    const auto readInfo = [&](VkImageView v)
    {
        return VkDescriptorImageInfo { _textureSampler, v != VK_NULL_HANDLE ? v : _dummyView,
                                       v != VK_NULL_HANDLE ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                           : VK_IMAGE_LAYOUT_GENERAL };
    };

    const auto writeInfo = [&](VkImageView v)
    {
        return VkDescriptorImageInfo { VK_NULL_HANDLE, v != VK_NULL_HANDLE ? v : _dummyView,
                                       VK_IMAGE_LAYOUT_GENERAL };
    };

    VkDescriptorImageInfo sourceInfo = readInfo(source);
    VkDescriptorImageInfo modelInfo = readInfo(model);
    VkDescriptorImageInfo originalInfo = readInfo(original);
    VkDescriptorImageInfo motionInfo = readInfo(motion);
    VkDescriptorImageInfo targetInfo = writeInfo(target);
    VkDescriptorImageInfo keepInfo = writeInfo(keep);
    VkDescriptorImageInfo samplerInfo { _textureSampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };

    const VkWriteDescriptorSet writes[] = {
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr,
          &bufferInfo, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          &sourceInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          &modelInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          &originalInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          &motionInfo, nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 5, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &targetInfo,
          nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 6, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &keepInfo,
          nullptr, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 7, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &samplerInfo,
          nullptr, nullptr },
    };

    vkUpdateDescriptorSets(_device, (uint32_t) (sizeof(writes) / sizeof(writes[0])), writes, 0, nullptr);
}

bool DlssNr_Vk::Dispatch(VkCommandBuffer InCmdList, const DlssNrConstants& InConstants, uint32_t InThreadsX,
                         uint32_t InThreadsY, VkImageView InSource, VkImageView InModel, VkImageView InOriginal,
                         VkImageView InMotion, VkImageView InTarget, VkImageView InKeep)
{
    if (!CanRender() || InCmdList == VK_NULL_HANDLE)
        return false;

    if (InTarget == VK_NULL_HANDLE)
    {
        LOG_ERROR("DLSS-NR Vulkan pass: a dispatch with nothing to write");
        return false;
    }

    if (!CreateDummy(InCmdList))
        return false;

    const uint32_t slot = _slot;
    _slot = (_slot + 1) % kSlots;

    const VkDeviceSize offset = _slotStride * slot;
    std::memcpy((char*) _mappedConstantBuffer + offset, &InConstants, sizeof(DlssNrConstants));

    WriteDescriptors(_descriptorSets[slot], offset, InSource, InModel, InOriginal, InMotion, InTarget, InKeep);

    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &_descriptorSets[slot], 0,
                            nullptr);

    // The shader's thread group is 8x8, the same as the D3D12 path.
    const uint32_t groupsX = (InThreadsX + 7) / 8;
    const uint32_t groupsY = (InThreadsY + 7) / 8;

    vkCmdDispatch(InCmdList, groupsX, groupsY, 1);

    // What this dispatch wrote, the next one reads. Left to the caller and it is a race that shows up
    // as a frame of stale detail rather than as an error, which is the worst kind to chase.
    VkMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(InCmdList, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);

    return true;
}
