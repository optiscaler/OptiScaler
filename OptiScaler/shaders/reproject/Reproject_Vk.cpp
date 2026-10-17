#include "pch.h"
#include <State.h>

#include "Reproject_Vk.h"
#include "precompile/reproject_Shader_Vk.h"
#include "precompile/reproject_mask_h_Shader_Vk.h"
#include "precompile/reproject_mask_v_Shader_Vk.h"
#include "precompile/reproject_latch_Shader_Vk.h"
#include "precompile/reproject_cache_Shader_Vk.h"
#include "precompile/reproject_stats_Shader_Vk.h"

#include "mouseInputs/RawInputHook.h"

// Bindings of the precompiled shaders, their HLSL registers shifted by type, see precompile/build_vk.bat
namespace Binding
{
enum : uint32_t
{
    Params = 0, // b0
    LiveInput,

    Hudless = 10, // t0
    PresentCopy,
    Depth,
    FakePresent,
    DepthMask,
    DepthMaskTemp,
    PrevCacheColor,
    PrevCacheGeo,
    CacheColor,
    CacheGeo,
    Velocity,

    Present = 30, // u0
    DepthMaskTempOut,
    DepthMaskOut,
    Latched,
    CacheColorOut,
    CacheGeoOut,
    StatsOut,

    PointSampler = 50, // s0
    BilinearSampler,
};
}

// Formats of the storage images, fixed by the vk::image_format of the shaders
constexpr VkFormat MaskFormat = VK_FORMAT_R8_UNORM;
constexpr VkFormat CacheColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat CacheGeoFormat = VK_FORMAT_R16G16_SFLOAT;

// Everything after waits for everything before, the passes read what the ones before them wrote
static void Barrier(VkCommandBuffer cmdBuffer)
{
    VkMemoryBarrier barrier { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);
}

static void ImageBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS };

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
}

bool Reproject_Vk::CreateBuffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible)
{
    auto properties = hostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                  : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (!CreateBufferResource(_device, _physicalDevice, &buffer.buffer, &buffer.memory, size, usage, properties))
    {
        LOG_ERROR("[{0}] Can't create a buffer of {1} bytes", _name, size);
        return false;
    }

    if (hostVisible && vkMapMemory(_device, buffer.memory, 0, size, 0, &buffer.mapped) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] Can't map a buffer of {1} bytes", _name, size);
        return false;
    }

    return true;
}

void Reproject_Vk::DestroyBuffer(Buffer& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(_device, buffer.buffer, nullptr);

    // Unmaps it too
    if (buffer.memory != VK_NULL_HANDLE)
        vkFreeMemory(_device, buffer.memory, nullptr);

    buffer = {};
}

bool Reproject_Vk::CreateImage(Image& image, VkFormat format, uint32_t width, uint32_t height, bool cube)
{
    VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = cube ? 6 : 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(_device, &imageInfo, nullptr, &image.image) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] vkCreateImage failed, {1}x{2}, format: {3}", _name, width, height, (UINT) format);
        return false;
    }

    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements(_device, image.image, &requirements);

    VkMemoryAllocateInfo allocInfo { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex =
        FindMemoryType(_physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(_device, &allocInfo, nullptr, &image.memory) != VK_SUCCESS ||
        vkBindImageMemory(_device, image.image, image.memory, 0) != VK_SUCCESS)
    {
        LOG_ERROR("[{0}] Can't allocate memory for a {1}x{2} image", _name, width, height);
        DestroyImage(image);
        return false;
    }

    VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = image.image;
    viewInfo.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, imageInfo.arrayLayers };

    bool viewsCreated = vkCreateImageView(_device, &viewInfo, nullptr, &image.view) == VK_SUCCESS;

    if (cube)
    {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewsCreated = viewsCreated && vkCreateImageView(_device, &viewInfo, nullptr, &image.arrayView) == VK_SUCCESS;
    }

    if (!viewsCreated)
    {
        LOG_ERROR("[{0}] vkCreateImageView failed", _name);
        DestroyImage(image);
        return false;
    }

    image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    image.width = width;
    image.height = height;
    return true;
}

void Reproject_Vk::DestroyImage(Image& image)
{
    if (image.arrayView != VK_NULL_HANDLE)
        vkDestroyImageView(_device, image.arrayView, nullptr);

    if (image.view != VK_NULL_HANDLE)
        vkDestroyImageView(_device, image.view, nullptr);

    if (image.image != VK_NULL_HANDLE)
        vkDestroyImage(_device, image.image, nullptr);

    if (image.memory != VK_NULL_HANDLE)
        vkFreeMemory(_device, image.memory, nullptr);

    image = {};
}

bool Reproject_Vk::CreateMaskImages(uint32_t width, uint32_t height)
{
    if (_depthMask.image != VK_NULL_HANDLE)
    {
        if (_depthMask.width == width && _depthMask.height == height)
            return true;

        LOG_WARN("[{0}] Release {1}x{2}, new one: {3}x{4}", _name, _depthMask.width, _depthMask.height, width, height);
    }

    DestroyImage(_depthMaskTemp);
    DestroyImage(_depthMask);

    return CreateImage(_depthMaskTemp, MaskFormat, width, height, false) &&
           CreateImage(_depthMask, MaskFormat, width, height, false);
}

bool Reproject_Vk::CreateCacheImages(uint32_t size)
{
    if (_cacheColor[0].image != VK_NULL_HANDLE)
    {
        if (_cacheColor[0].width == size)
            return true;

        LOG_WARN("[{0}] Release cache {1}, new one: {2}", _name, _cacheColor[0].width, size);
    }

    // Contents are undefined until the first update
    _cacheValid = false;

    auto release = [&]()
    {
        for (int i = 0; i < 2; i++)
        {
            DestroyImage(_cacheColor[i]);
            DestroyImage(_cacheGeo[i]);
        }
    };

    release();

    for (int i = 0; i < 2; i++)
    {
        if (!CreateImage(_cacheColor[i], CacheColorFormat, size, size, true) ||
            !CreateImage(_cacheGeo[i], CacheGeoFormat, size, size, true))
        {
            release();
            return false;
        }
    }

    return true;
}

void Reproject_Vk::ToGeneral(VkCommandBuffer cmdBuffer, Image& image)
{
    if (image.layout == VK_IMAGE_LAYOUT_GENERAL)
        return;

    ImageBarrier(cmdBuffer, image.image, image.layout, VK_IMAGE_LAYOUT_GENERAL);
    image.layout = VK_IMAGE_LAYOUT_GENERAL;
}

Reproject_Vk::Reproject_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    if (InDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    // Not every GPU can write these as storage images
    for (auto format : { MaskFormat, CacheGeoFormat })
    {
        VkFormatProperties properties {};
        vkGetPhysicalDeviceFormatProperties(_physicalDevice, format, &properties);

        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0)
        {
            LOG_ERROR("[{0}] Format {1} isn't supported as a storage image", _name, (UINT) format);
            return;
        }
    }

    // One for each command buffer of the FG output, so a set is only rewritten once the GPU is done with it
    _maxFramesInFlight = BUFFER_COUNT;

    // Immutable samplers of the set layout, CreateSampler always creates _textureSampler which stays the bilinear one
    CreateSampler(VK_FILTER_NEAREST);
    _pointSampler = std::exchange(_textureSampler, VK_NULL_HANDLE);
    CreateSampler(VK_FILTER_LINEAR);

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkDescriptorPoolSize> poolSizes;

    auto addBindings = [&](VkDescriptorType type, std::initializer_list<uint32_t> numbers)
    {
        for (auto number : numbers)
            bindings.push_back(CreateBinding(number, type));

        poolSizes.push_back({ type, (uint32_t) numbers.size() * _maxFramesInFlight });
    };

    addBindings(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { Binding::Params, Binding::LiveInput });
    addBindings(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                { Binding::Hudless, Binding::PresentCopy, Binding::Depth, Binding::FakePresent, Binding::DepthMask,
                  Binding::DepthMaskTemp, Binding::PrevCacheColor, Binding::PrevCacheGeo, Binding::CacheColor,
                  Binding::CacheGeo, Binding::Velocity });
    addBindings(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { Binding::Present, Binding::DepthMaskTempOut, Binding::DepthMaskOut,
                                                    Binding::CacheColorOut, Binding::CacheGeoOut });
    addBindings(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, { Binding::Latched, Binding::StatsOut });

    addBindings(VK_DESCRIPTOR_TYPE_SAMPLER, { Binding::PointSampler, Binding::BilinearSampler });
    bindings[bindings.size() - 2].pImmutableSamplers = &_pointSampler;
    bindings.back().pImmutableSamplers = &_textureSampler;

    CreateLayouts(bindings);
    CreateDescriptorPool(poolSizes, _maxFramesInFlight);
    CreateDescriptorSets(_descriptorSetLayout, _descriptorPool, _descriptorSets);

    struct
    {
        VkPipeline* pipeline;
        const unsigned char* code;
        size_t size;
    } shaders[] = {
        { &_pipeline, reproject_spv, sizeof(reproject_spv) },
        { &_maskPipelineH, reproject_mask_h_spv, sizeof(reproject_mask_h_spv) },
        { &_maskPipelineV, reproject_mask_v_spv, sizeof(reproject_mask_v_spv) },
        { &_latchPipeline, reproject_latch_spv, sizeof(reproject_latch_spv) },
        { &_cachePipeline, reproject_cache_spv, sizeof(reproject_cache_spv) },
        { &_statsPipeline, reproject_stats_spv, sizeof(reproject_stats_spv) },
    };

    for (auto& shader : shaders)
    {
        std::vector<char> code(shader.code, shader.code + shader.size);

        if (!CreateComputePipeline(_device, _pipelineLayout, shader.pipeline, code))
        {
            LOG_ERROR("[{0}] Failed to create a compute pipeline", _name);
            return;
        }
    }

    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties(_physicalDevice, &properties);

    const auto alignment = properties.limits.minUniformBufferOffsetAlignment;
    _paramsStride = (sizeof(ReprojectionParams) + alignment - 1) / alignment * alignment;

    if (!CreateBuffer(_params, _paramsStride * _maxFramesInFlight, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true) ||
        !CreateBuffer(_liveInput, sizeof(ReprojectionLiveInput), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true) ||
        !CreateBuffer(_latched, 4 * sizeof(DirectX::XMFLOAT4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false) ||
        !CreateBuffer(_stats, StatsCount * sizeof(uint32_t),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false))
    {
        return;
    }

    for (uint32_t i = 0; i < StatsLatency; i++)
    {
        if (!CreateBuffer(_statsReadback[i], StatsCount * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
            return;

        _statsMapped[i] = static_cast<const uint32_t*>(_statsReadback[i].mapped);
    }

    std::memset(_liveInput.mapped, 0, sizeof(ReprojectionLiveInput));

    if (!CreateImage(_emptyCube, CacheColorFormat, 1, 1, true))
        return;

    _init = !_descriptorSets.empty() && _descriptorSets.back() != VK_NULL_HANDLE;

    if (_init)
        InputCollection::getInstance().registerLiveSink(_liveInput.mapped);

    RawInputHook::getInstance().start();
}

bool Reproject_Vk::Dispatch(VkCommandBuffer cmdBuffer, ReprojectionParams& params, VkImageView present,
                            VkExtent2D presentExtent, VkImageView hudless, VkImageView depth, VkImageView velocity)
{
    if (!_init || cmdBuffer == VK_NULL_HANDLE || present == VK_NULL_HANDLE || hudless == VK_NULL_HANDLE ||
        depth == VK_NULL_HANDLE || params.DepthWidth == 0 || params.DepthHeight == 0)
    {
        return false;
    }

    ScopedGpuTime_Vk scopedGpuTime(GpuTime.get(), cmdBuffer);

    // Earlier dispatches might still use what gets recreated
    const bool outputResized =
        _intermediateImage != VK_NULL_HANDLE && (_width != presentExtent.width || _height != presentExtent.height);
    const bool maskResized = _depthMask.image != VK_NULL_HANDLE &&
                             (_depthMask.width != params.DepthWidth || _depthMask.height != params.DepthHeight);
    const bool cacheResized =
        params.CacheEnabled && _cacheColor[0].image != VK_NULL_HANDLE && _cacheColor[0].width != params.CacheSize;

    if (outputResized || maskResized || cacheResized)
        vkDeviceWaitIdle(_device);

    if (!CreateImageResource(presentExtent.width, presentExtent.height, OutputFormat,
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
        !CreateMaskImages(params.DepthWidth, params.DepthHeight))
    {
        return false;
    }

    bool cacheReady = params.CacheEnabled && CreateCacheImages(params.CacheSize);
    auto plan = PlanDispatch(params, cacheReady, velocity != VK_NULL_HANDLE);

    _currentSetIndex = (_currentSetIndex + 1) % _maxFramesInFlight;
    auto set = _descriptorSets[_currentSetIndex];

    const auto paramsOffset = _paramsStride * _currentSetIndex;
    std::memcpy(static_cast<char*>(_params.mapped) + paramsOffset, &params, sizeof(params));

    VkDescriptorBufferInfo bufferInfos[4] {};
    VkDescriptorImageInfo imageInfos[16] {};
    VkWriteDescriptorSet writes[20] {};
    uint32_t bufferCount = 0;
    uint32_t imageCount = 0;
    uint32_t writeCount = 0;

    auto write = [&](uint32_t binding, VkDescriptorType type) -> VkWriteDescriptorSet&
    {
        auto& entry = writes[writeCount++];
        entry = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        entry.dstSet = set;
        entry.dstBinding = binding;
        entry.descriptorCount = 1;
        entry.descriptorType = type;
        return entry;
    };

    auto buffer =
        [&](uint32_t binding, VkDescriptorType type, const Buffer& source, VkDeviceSize offset, VkDeviceSize range)
    {
        auto& info = bufferInfos[bufferCount++];
        info = { source.buffer, offset, range };
        write(binding, type).pBufferInfo = &info;
    };

    auto image = [&](uint32_t binding, VkDescriptorType type, VkImageView view, VkImageLayout layout)
    {
        auto& info = imageInfos[imageCount++];
        info = { VK_NULL_HANDLE, view, layout };
        write(binding, type).pImageInfo = &info;
    };

    const auto sampled = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    const auto storage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    const auto readOnly = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const auto general = VK_IMAGE_LAYOUT_GENERAL;

    buffer(Binding::Params, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _params, paramsOffset, sizeof(ReprojectionParams));
    buffer(Binding::LiveInput, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, _liveInput, 0, sizeof(ReprojectionLiveInput));
    buffer(Binding::Latched, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _latched, 0, VK_WHOLE_SIZE);
    buffer(Binding::StatsOut, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _stats, 0, VK_WHOLE_SIZE);

    image(Binding::Hudless, sampled, hudless, readOnly);
    image(Binding::PresentCopy, sampled, present, readOnly);
    image(Binding::Depth, sampled, depth, readOnly);
    image(Binding::DepthMask, sampled, _depthMask.view, general);
    image(Binding::DepthMaskTemp, sampled, _depthMaskTemp.view, general);
    image(Binding::Present, storage, GetImageView(), general);
    image(Binding::DepthMaskTempOut, storage, _depthMaskTemp.view, general);
    image(Binding::DepthMaskOut, storage, _depthMask.view, general);

    // Only read for generated frames, the Vulkan outputs don't reproject those
    image(Binding::FakePresent, sampled, present, readOnly);

    // Passes that don't get recorded don't need theirs
    if (cacheReady)
    {
        image(Binding::PrevCacheColor, sampled, _cacheColor[plan.prevCache].view, general);
        image(Binding::PrevCacheGeo, sampled, _cacheGeo[plan.prevCache].view, general);
        image(Binding::CacheColor, sampled, _cacheColor[plan.newestCache].view, general);
        image(Binding::CacheGeo, sampled, _cacheGeo[plan.newestCache].view, general);
    }
    else
    {
        image(Binding::CacheColor, sampled, _emptyCube.view, general);
        image(Binding::CacheGeo, sampled, _emptyCube.view, general);
    }

    if (plan.cacheUpdate)
    {
        image(Binding::CacheColorOut, storage, _cacheColor[plan.newestCache].arrayView, general);
        image(Binding::CacheGeoOut, storage, _cacheGeo[plan.newestCache].arrayView, general);
    }

    if (plan.statsUpdate)
        image(Binding::Velocity, sampled, velocity, readOnly);

    vkUpdateDescriptorSets(_device, writeCount, writes, 0, nullptr);

    ToGeneral(cmdBuffer, _depthMaskTemp);
    ToGeneral(cmdBuffer, _depthMask);
    ToGeneral(cmdBuffer, _emptyCube);

    if (cacheReady)
    {
        for (int i = 0; i < 2; i++)
        {
            ToGeneral(cmdBuffer, _cacheColor[i]);
            ToGeneral(cmdBuffer, _cacheGeo[i]);
        }
    }

    // What the previous dispatch left there isn't needed
    ImageBarrier(cmdBuffer, GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    // The buffers are reused from the previous dispatch
    Barrier(cmdBuffer);

    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 0, 1, &set, 0, nullptr);

    auto dispatch = [&](VkPipeline pipeline, uint32_t x, uint32_t y, uint32_t z)
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdDispatch(cmdBuffer, x, y, z);
        Barrier(cmdBuffer);
    };

    // Depth mask prepass, writes straight to _depthMask when CutoffExpandPx == 0
    uint32_t maskDispatchWidth = (params.DepthWidth + InNumThreadsX - 1) / InNumThreadsX;
    uint32_t maskDispatchHeight = (params.DepthHeight + InNumThreadsY - 1) / InNumThreadsY;

    dispatch(_maskPipelineH, maskDispatchWidth, maskDispatchHeight, 1);

    if (params.CutoffExpandPx > 0)
        dispatch(_maskPipelineV, maskDispatchWidth, maskDispatchHeight, 1);

    // Motion vector statistics, a single group
    if (plan.statsUpdate)
    {
        dispatch(_statsPipeline, 1, 1, 1);

        auto& readback = _statsReadback[plan.statsSlot];
        VkBufferCopy region { 0, 0, StatsCount * sizeof(uint32_t) };
        vkCmdCopyBuffer(cmdBuffer, _stats.buffer, readback.buffer, 1, &region);

        SetBufferState(cmdBuffer, readback.buffer, VK_WHOLE_SIZE, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
    }

    // Direction cache update, once per rendered frame
    if (plan.cacheUpdate)
    {
        uint32_t cacheDispatchSize = (params.CacheSize + 7) / 8;
        dispatch(_cachePipeline, cacheDispatchSize, cacheDispatchSize, 6);
    }

    // Late latch, as close as possible to the reprojection itself
    dispatch(_latchPipeline, 1, 1, 1);

    uint32_t dispatchWidth = (presentExtent.width + InNumThreadsX - 1) / InNumThreadsX;
    uint32_t dispatchHeight = (presentExtent.height + InNumThreadsY - 1) / InNumThreadsY;

    dispatch(_pipeline, dispatchWidth, dispatchHeight, 1);

    ImageBarrier(cmdBuffer, GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    PlanRecorded(plan);
    return true;
}

Reproject_Vk::~Reproject_Vk()
{
    // Always stop the input thread from writing into the mapped memory
    InputCollection::getInstance().unregisterLiveSink(_liveInput.mapped);
    RawInputHook::getInstance().stop();

    if (_device == VK_NULL_HANDLE)
        return;

    for (auto pipeline : { _maskPipelineH, _maskPipelineV, _latchPipeline, _cachePipeline, _statsPipeline })
    {
        if (pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(_device, pipeline, nullptr);
    }

    if (_pointSampler != VK_NULL_HANDLE)
        vkDestroySampler(_device, _pointSampler, nullptr);

    for (auto buffer : { &_params, &_liveInput, &_latched, &_stats })
        DestroyBuffer(*buffer);

    for (auto& readback : _statsReadback)
        DestroyBuffer(readback);

    for (auto image :
         { &_depthMaskTemp, &_depthMask, &_cacheColor[0], &_cacheColor[1], &_cacheGeo[0], &_cacheGeo[1], &_emptyCube })
    {
        DestroyImage(*image);
    }
}
