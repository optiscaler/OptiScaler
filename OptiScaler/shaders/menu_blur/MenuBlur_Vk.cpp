#include "pch.h"
#include "MenuBlur_Vk.h"

#include <Config.h>
#include <imgui/imgui_impl_vulkan.h>
#include "precompile/MenuBlur_Shader_Vk.h"

static constexpr VkFormat BlurFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

bool MenuBlur_Vk::CreateImage(BlurImage& OutImage, uint32_t InWidth, uint32_t InHeight, VkFormat InFormat,
                              VkImageUsageFlags InUsage)
{
    VkImageCreateInfo imageInfo {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { InWidth, InHeight, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = InFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = InUsage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(_device, &imageInfo, nullptr, &OutImage.Image) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] vkCreateImage failed", _name);
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(_device, OutImage.Image, &memRequirements);

    VkMemoryAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        FindMemoryType(_physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (allocInfo.memoryTypeIndex == (uint32_t) -1 ||
        vkAllocateMemory(_device, &allocInfo, nullptr, &OutImage.Memory) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] vkAllocateMemory failed", _name);
        return false;
    }

    vkBindImageMemory(_device, OutImage.Image, OutImage.Memory, 0);

    VkImageViewCreateInfo viewInfo {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = OutImage.Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = InFormat;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (vkCreateImageView(_device, &viewInfo, nullptr, &OutImage.View) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] vkCreateImageView failed", _name);
        return false;
    }

    return true;
}

void MenuBlur_Vk::DestroyImage(BlurImage& InImage)
{
    if (InImage.View != VK_NULL_HANDLE)
        vkDestroyImageView(_device, InImage.View, nullptr);

    if (InImage.Image != VK_NULL_HANDLE)
        vkDestroyImage(_device, InImage.Image, nullptr);

    if (InImage.Memory != VK_NULL_HANDLE)
        vkFreeMemory(_device, InImage.Memory, nullptr);

    InImage = {};
}

void MenuBlur_Vk::ReleaseImages()
{
    // ImGui backend can be already shut down, its descriptor pool is reset with it then
    if (_imguiTexture != VK_NULL_HANDLE && ImGui::GetCurrentContext() != nullptr &&
        ImGui::GetIO().BackendRendererUserData != nullptr)
    {
        ImGui_ImplVulkan_RemoveTexture(_imguiTexture);
    }

    _imguiTexture = VK_NULL_HANDLE;

    DestroyImage(_copy);
    DestroyImage(_blurA);
    DestroyImage(_blurB);

    _extent = {};
    _swapchainFormat = VK_FORMAT_UNDEFINED;
}

bool MenuBlur_Vk::Prepare(VkFormat InFormat, VkExtent2D InExtent, VkImageUsageFlags InUsage)
{
    if (!_init)
        return false;

    // Swapchain needs to be created with transfer src usage to make a copy
    if ((InUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0)
        return false;

    if (_copy.Image != VK_NULL_HANDLE)
    {
        if (_extent.width == InExtent.width && _extent.height == InExtent.height && _swapchainFormat == InFormat)
            return true;

        // Images can still be used by previous frames
        vkDeviceWaitIdle(_device);
        ReleaseImages();
    }

    uint32_t width = (InExtent.width + DownsampleFactor - 1) / DownsampleFactor;
    uint32_t height = (InExtent.height + DownsampleFactor - 1) / DownsampleFactor;

    if (!CreateImage(_copy, InExtent.width, InExtent.height, InFormat,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) ||
        !CreateImage(_blurA, width, height, BlurFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) ||
        !CreateImage(_blurB, width, height, BlurFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
    {
        ReleaseImages();
        return false;
    }

    // All images stay in GENERAL layout
    const VkImageView inputs[PassSetCount] = { _copy.View, _blurA.View, _blurB.View };
    const VkImageView outputs[PassSetCount] = { _blurA.View, _blurB.View, _blurA.View };

    for (uint32_t i = 0; i < PassSetCount; i++)
    {
        VkDescriptorImageInfo inputInfo { _textureSampler, inputs[i], VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo outputInfo { VK_NULL_HANDLE, outputs[i], VK_IMAGE_LAYOUT_GENERAL };

        VkWriteDescriptorSet writes[2] = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _passSets[i], 0, 0, 1,
                                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &inputInfo, nullptr, nullptr },
                                           { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, _passSets[i], 1, 0, 1,
                                             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &outputInfo, nullptr, nullptr } };

        vkUpdateDescriptorSets(_device, 2, writes, 0, nullptr);
    }

    _imguiTexture = ImGui_ImplVulkan_AddTexture(_textureSampler, _blurA.View, VK_IMAGE_LAYOUT_GENERAL);

    if (_imguiTexture == VK_NULL_HANDLE)
    {
        LOG_ERROR("[{0}] ImGui_ImplVulkan_AddTexture failed", _name);
        ReleaseImages();
        return false;
    }

    _extent = InExtent;
    _swapchainFormat = InFormat;
    _uvScale = ImVec2((float) InExtent.width / (float) (width * DownsampleFactor),
                      (float) InExtent.height / (float) (height * DownsampleFactor));

    LOG_DEBUG("[{0}] Created blur images {1}x{2}", _name, width, height);

    return true;
}

void MenuBlur_Vk::ComputeBarrier(VkCommandBuffer InCmdBuffer, VkPipelineStageFlags InDstStage,
                                 VkAccessFlags InDstAccess)
{
    VkMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = InDstAccess;

    vkCmdPipelineBarrier(InCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, InDstStage, 0, 1, &barrier, 0, nullptr, 0,
                         nullptr);
}

void MenuBlur_Vk::DispatchPass(VkCommandBuffer InCmdBuffer, PassSet InSet, BlurMode InMode, float InSpacing)
{
    InternalMenuBlurParams params {};
    params.Mode = (uint32_t) InMode;
    params.Spacing = InSpacing;

    vkCmdBindDescriptorSets(InCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &_passSets[InSet], 0,
                            nullptr);
    vkCmdPushConstants(InCmdBuffer, _pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);

    uint32_t width = (_extent.width + DownsampleFactor - 1) / DownsampleFactor;
    uint32_t height = (_extent.height + DownsampleFactor - 1) / DownsampleFactor;
    vkCmdDispatch(InCmdBuffer, (width + 7) / 8, (height + 7) / 8, 1);
}

bool MenuBlur_Vk::Dispatch(VkCommandBuffer InCmdBuffer, VkImage InSwapchainImage)
{
    if (!_init || _copy.Image == VK_NULL_HANDLE || InCmdBuffer == VK_NULL_HANDLE || InSwapchainImage == VK_NULL_HANDLE)
    {
        return false;
    }

    const VkImageSubresourceRange range { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    // Swapchain image to transfer src, own images are fully overwritten so their content can be discarded.
    // Source stages also cover the reads of the previous frame (compute for the copy, fragment for the menu)
    {
        VkImageMemoryBarrier barriers[4] = {};

        for (auto& barrier : barriers)
        {
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange = range;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        }

        barriers[0].image = InSwapchainImage;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        barriers[1].image = _copy.Image;
        barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        barriers[2].image = _blurA.Image;
        barriers[2].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        barriers[3].image = _blurB.Image;
        barriers[3].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(InCmdBuffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                             nullptr, _countof(barriers), barriers);
    }

    VkImageCopy region {};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent = { _extent.width, _extent.height, 1 };

    vkCmdCopyImage(InCmdBuffer, InSwapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _copy.Image,
                   VK_IMAGE_LAYOUT_GENERAL, 1, &region);

    // Swapchain image back for the menu render pass, copy to compute read
    {
        VkImageMemoryBarrier swapchainBarrier {};
        swapchainBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        swapchainBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapchainBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapchainBarrier.subresourceRange = range;
        swapchainBarrier.image = InSwapchainImage;
        swapchainBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        swapchainBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        swapchainBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkMemoryBarrier copyBarrier {};
        copyBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        copyBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(InCmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 1,
                             &copyBarrier, 0, nullptr, 1, &swapchainBarrier);
    }

    // Strength scales the distance between taps, 1.0 is a regular 9 tap gaussian
    float spacing = Config::Instance()->MenuBlurStrength.value_or_default();

    vkCmdBindPipeline(InCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);

    DispatchPass(InCmdBuffer, DownsampleSet, BlurMode::Downsample, spacing);

    if (spacing > 0.0f)
    {
        for (uint32_t i = 0; i < BlurIterations; i++)
        {
            ComputeBarrier(InCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            DispatchPass(InCmdBuffer, HorizontalSet, BlurMode::Horizontal, spacing);

            ComputeBarrier(InCmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            DispatchPass(InCmdBuffer, VerticalSet, BlurMode::Vertical, spacing);
        }
    }

    ComputeBarrier(InCmdBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

    return true;
}

MenuBlur_Vk::MenuBlur_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    if (InDevice == VK_NULL_HANDLE || InPhysicalDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("InDevice or InPhysicalDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    CreateSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    VkDescriptorSetLayoutBinding bindings[2] = {
        CreateBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        CreateBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = _countof(bindings);
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] vkCreateDescriptorSetLayout failed", _name);
        return;
    }

    VkPushConstantRange pushConstantRange { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(InternalMenuBlurParams) };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_pipelineLayout) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] vkCreatePipelineLayout failed", _name);
        return;
    }

    CreateDescriptorPool({ { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, PassSetCount },
                           { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, PassSetCount } },
                         PassSetCount);

    if (_descriptorPool == VK_NULL_HANDLE || _textureSampler == VK_NULL_HANDLE)
        return;

    VkDescriptorSetLayout setLayouts[PassSetCount] = { _descriptorSetLayout, _descriptorSetLayout,
                                                       _descriptorSetLayout };

    VkDescriptorSetAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = _descriptorPool;
    allocInfo.descriptorSetCount = PassSetCount;
    allocInfo.pSetLayouts = setLayouts;

    if (vkAllocateDescriptorSets(_device, &allocInfo, _passSets) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] vkAllocateDescriptorSets failed", _name);
        return;
    }

    std::vector<char> shaderCode(MenuBlur_spv, MenuBlur_spv + sizeof(MenuBlur_spv));
    if (!CreateComputePipeline(_device, _pipelineLayout, &_pipeline, shaderCode))
    {
        LOG_ERROR("[{0}] Failed to create pipeline!", _name);
        return;
    }

    _init = true;
}

MenuBlur_Vk::~MenuBlur_Vk() { ReleaseImages(); }
