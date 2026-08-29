#include <pch.h>
#include "DT_Vk.h"
#include "precompile/dt_Shader_Vk.h"
#include "precompile/dt_int_Shader_Vk.h"
#include <Config.h>

// Helper function to determine if format is integer or float
static bool IsIntegerFormat(VkFormat format)
{
    switch (format)
    {
    // Integer formats
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8_SINT:
    case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R8G8B8_UINT:
    case VK_FORMAT_R8G8B8_SINT:
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16B16_UINT:
    case VK_FORMAT_R16G16B16_SINT:
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32B32_UINT:
    case VK_FORMAT_R32G32B32_SINT:
    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_R32G32B32A32_SINT:
    case VK_FORMAT_R64_UINT:
    case VK_FORMAT_R64_SINT:
    case VK_FORMAT_R64G64_UINT:
    case VK_FORMAT_R64G64_SINT:
    case VK_FORMAT_R64G64B64_UINT:
    case VK_FORMAT_R64G64B64_SINT:
    case VK_FORMAT_R64G64B64A64_UINT:
    case VK_FORMAT_R64G64B64A64_SINT:
    case VK_FORMAT_B8G8R8_UINT:
    case VK_FORMAT_B8G8R8_SINT:
    case VK_FORMAT_B8G8R8A8_UINT:
    case VK_FORMAT_B8G8R8A8_SINT:
    case VK_FORMAT_A8B8G8R8_UINT_PACK32:
    case VK_FORMAT_A8B8G8R8_SINT_PACK32:
    case VK_FORMAT_A2R10G10B10_UINT_PACK32:
    case VK_FORMAT_A2R10G10B10_SINT_PACK32:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:
    case VK_FORMAT_A2B10G10R10_SINT_PACK32:
        return true;

    // Float and depth formats
    default:
        return false;
    }
}

DepthTransfer_Vk::DepthTransfer_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice,
                                   VkFormat InFormat)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    if (InDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    // Setup Layouts
    std::vector<VkDescriptorSetLayoutBinding> bindings = { CreateBinding(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
                                                           CreateBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) };
    CreateLayouts(bindings);

    // Setup Descriptor Pools & Sets
    std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, _maxFramesInFlight },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, _maxFramesInFlight } };
    CreateDescriptorPool(poolSizes, _maxFramesInFlight);
    CreateDescriptorSets(_descriptorSetLayout, _descriptorPool, _descriptorSets);

    // Load precompiled shader based on format
    std::vector<char> shaderCode;
    _isInteger = IsIntegerFormat(InFormat);
    if (_isInteger)
    {
        LOG_INFO("[{0}] Using integer shader for format", _name);
        shaderCode = std::vector<char>(dt_int_spv, dt_int_spv + sizeof(dt_int_spv));
    }
    else
    {
        LOG_INFO("[{0}] Using float shader for format", _name);
        shaderCode = std::vector<char>(dt_spv, dt_spv + sizeof(dt_spv));
    }

    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, shaderCode))
    {
        LOG_ERROR("[{0}] Failed to create pipeline!", _name);
        _init = false;
        return;
    }

    _init = true;
}

bool DepthTransfer_Vk::InitializeViews(VkImageView InResourceView, VkImageView OutResourceView)
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

bool DepthTransfer_Vk::Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, VkImageView InResourceView,
                                VkImageView OutResourceView, VkExtent2D OutExtent)
{
    if (!_init || InDevice == VK_NULL_HANDLE || InCmdList == VK_NULL_HANDLE || InResourceView == VK_NULL_HANDLE ||
        OutResourceView == VK_NULL_HANDLE)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    if (!InitializeViews(InResourceView, OutResourceView))
        return false;

    // Advance frame index
    _currentSetIndex = (_currentSetIndex + 1) % _maxFramesInFlight;
    VkDescriptorSet currentSet = _descriptorSets[_currentSetIndex];

    // Build Descriptor Writes
    VkDescriptorImageInfo sourceInfo { VK_NULL_HANDLE, InResourceView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo destInfo { VK_NULL_HANDLE, OutResourceView, VK_IMAGE_LAYOUT_GENERAL };

    std::vector<VkWriteDescriptorSet> writes = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, currentSet, 0, 0, 1,
                                                   VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &sourceInfo, nullptr, nullptr },
                                                 { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, currentSet, 1, 0, 1,
                                                   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &destInfo, nullptr, nullptr } };

    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    vkCmdBindPipeline(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
    vkCmdBindDescriptorSets(InCmdList, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &currentSet, 0, nullptr);

    // Dispatch
    uint32_t dispatchWidth = (OutExtent.width + InNumThreadsX - 1) / InNumThreadsX;
    uint32_t dispatchHeight = (OutExtent.height + InNumThreadsY - 1) / InNumThreadsY;
    vkCmdDispatch(InCmdList, dispatchWidth, dispatchHeight, 1);

    return true;
}
