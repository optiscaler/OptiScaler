#include "pch.h"
#include "IFGFeature_Vk.h"

#include <Config.h>
#include <State.h>
#include <misc/DeferredRelease.h>

#include <magic_enum.hpp>
#include <bit>

VkFormat IFGFeature_Vk::WithoutSrgb(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R8G8B8A8_SRGB:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    default:
        return format;
    }
}

static VkImageAspectFlags AspectMask(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
        return VK_IMAGE_ASPECT_DEPTH_BIT;

    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

    case VK_FORMAT_S8_UINT:
        return VK_IMAGE_ASPECT_STENCIL_BIT;

    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

void IFGFeature_Vk::ImageBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout,
                                 VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { AspectMask(format), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
}

void IFGFeature_Vk::PrepareDevice(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo& createInfo)
{
    _physicalDevice = physicalDevice;

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

    std::vector<uint32_t> used(familyCount, 0);
    _gameQueueFamily = UINT32_MAX;

    for (uint32_t i = 0; i < createInfo.queueCreateInfoCount; i++)
    {
        auto& info = createInfo.pQueueCreateInfos[i];

        if (info.queueFamilyIndex >= familyCount)
            continue;

        used[info.queueFamilyIndex] += info.queueCount;

        // Games present from their graphics queue
        if (_gameQueueFamily == UINT32_MAX && info.flags == 0 &&
            (families[info.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            _gameQueueFamily = info.queueFamilyIndex;
        }
    }

    // Graphics and compute queues can always transfer
    auto caps = [](VkQueueFlags flags)
    {
        if (flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
            flags |= VK_QUEUE_TRANSFER_BIT;

        return flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT);
    };

    _queueCreateInfos.assign(createInfo.pQueueCreateInfos,
                             createInfo.pQueueCreateInfos + createInfo.queueCreateInfoCount);

    for (auto& queue : _extraQueues)
    {
        // A family with a free queue and the fewest capabilities beyond the needed ones
        queue.family = UINT32_MAX;
        int fewestExtra = INT_MAX;

        for (uint32_t family = 0; family < familyCount; family++)
        {
            auto familyCaps = caps(families[family].queueFlags);

            if ((familyCaps & queue.flags) != queue.flags || used[family] >= families[family].queueCount)
                continue;

            if (queue.present && !vkGetPhysicalDeviceWin32PresentationSupportKHR(physicalDevice, family))
                continue;

            if (int extra = std::popcount(familyCaps & ~queue.flags); extra < fewestExtra)
            {
                fewestExtra = extra;
                queue.family = family;
            }
        }

        if (queue.family == UINT32_MAX)
        {
            LOG_WARN("No free queue for flags: {:X}, present: {}", queue.flags, queue.present);
            continue;
        }

        used[queue.family]++;

        // Only one create info per family with the same flags
        auto it = std::ranges::find_if(_queueCreateInfos, [&](const VkDeviceQueueCreateInfo& info)
                                       { return info.queueFamilyIndex == queue.family && info.flags == 0; });

        if (it == _queueCreateInfos.end())
        {
            _queueCreateInfos.push_back({ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, queue.family, 0 });
            it = _queueCreateInfos.end() - 1;
        }

        queue.index = it->queueCount++;
        LOG_INFO("Reserved queue family: {}, index: {}", queue.family, queue.index);
    }

    // Game's priorities, ours at the end
    _queuePriorities.resize(_queueCreateInfos.size());

    for (size_t i = 0; i < _queueCreateInfos.size(); i++)
    {
        auto& info = _queueCreateInfos[i];
        auto& priorities = _queuePriorities[i];
        priorities.assign(info.queueCount, 1.0f);

        if (i < createInfo.queueCreateInfoCount)
        {
            auto& gameInfo = createInfo.pQueueCreateInfos[i];
            std::copy_n(gameInfo.pQueuePriorities, gameInfo.queueCount, priorities.begin());
        }

        info.pQueuePriorities = priorities.data();
    }

    createInfo.queueCreateInfoCount = (uint32_t) _queueCreateInfos.size();
    createInfo.pQueueCreateInfos = _queueCreateInfos.data();
}

void IFGFeature_Vk::OnDeviceCreated(VkDevice device)
{
    _device = device;

    if (_gameQueueFamily != UINT32_MAX)
        vkGetDeviceQueue(device, _gameQueueFamily, 0, &_gameQueue);

    for (auto& queue : _extraQueues)
    {
        if (queue.family != UINT32_MAX)
            vkGetDeviceQueue(device, queue.family, queue.index, &queue.queue);
    }
}

bool IFGFeature_Vk::OwnsQueue(VkQueue queue) const
{
    return std::ranges::any_of(_extraQueues, [&](const ExtraQueue& extra)
                               { return extra.queue != VK_NULL_HANDLE && extra.queue == queue; });
}

void IFGFeature_Vk::DestroyCopy(ImageCopy& copy)
{
    if (copy.view != VK_NULL_HANDLE || copy.image != VK_NULL_HANDLE || copy.memory != VK_NULL_HANDLE)
    {
        DeferredRelease::Enqueue(
            [device = _device, view = copy.view, image = copy.image, memory = copy.memory]()
            {
                if (view != VK_NULL_HANDLE)
                    vkDestroyImageView(device, view, nullptr);

                if (image != VK_NULL_HANDLE)
                    vkDestroyImage(device, image, nullptr);

                if (memory != VK_NULL_HANDLE)
                    vkFreeMemory(device, memory, nullptr);
            },
            (uint64_t) _device);
    }

    copy = {};
}

const IFGFeature_Vk::ImageCopy* IFGFeature_Vk::OwnCopy(const VkFGResource& resource, int fIndex) const
{
    if (fIndex < 0 || fIndex >= BUFFER_COUNT)
        return nullptr;

    auto& copy = _copies[fIndex][resource.type];
    return copy.image != VK_NULL_HANDLE && copy.image == resource.image ? &copy : nullptr;
}

bool IFGFeature_Vk::CreateCopy(ImageCopy& copy, FG_ResourceType type, VkFormat format, uint32_t width, uint32_t height,
                               VkImageUsageFlags usage)
{
    if (copy.format == format && copy.width == width && copy.height == height)
        return true;

    DestroyCopy(copy);

    VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(_device, &imageInfo, nullptr, &copy.image) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateImage failed for {}", magic_enum::enum_name(type));
        return false;
    }

    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements(_device, copy.image, &requirements);

    VkPhysicalDeviceMemoryProperties memoryProperties {};
    vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memoryProperties);

    VkMemoryAllocateInfo allocInfo { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = UINT32_MAX;

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
        if ((requirements.memoryTypeBits & (1 << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            allocInfo.memoryTypeIndex = i;
            break;
        }
    }

    if (allocInfo.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(_device, &allocInfo, nullptr, &copy.memory) != VK_SUCCESS ||
        vkBindImageMemory(_device, copy.image, copy.memory, 0) != VK_SUCCESS)
    {
        LOG_ERROR("Can't allocate memory for {}", magic_enum::enum_name(type));
        DestroyCopy(copy);
        return false;
    }

    // Shaders sample only the depth of depth/stencil images
    auto viewAspect = AspectMask(format);
    if (viewAspect & VK_IMAGE_ASPECT_DEPTH_BIT)
        viewAspect = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = copy.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = { viewAspect, 0, 1, 0, 1 };

    if (vkCreateImageView(_device, &viewInfo, nullptr, &copy.view) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateImageView failed for {}", magic_enum::enum_name(type));
        DestroyCopy(copy);
        return false;
    }

    copy.format = format;
    copy.width = width;
    copy.height = height;

    LOG_DEBUG("{} copy created, {}x{}, format: {}", magic_enum::enum_name(type), copy.width, copy.height,
              (UINT) format);

    return true;
}

bool IFGFeature_Vk::CopyImage(VkCommandBuffer cmdBuffer, VkFGResource& resource, ImageCopy& copy, VkFormat format)
{
    if (!CreateCopy(copy, resource.type, format, resource.imageWidth, resource.imageHeight,
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
    {
        return false;
    }

    auto aspect = AspectMask(resource.format);

    ImageBarrier(cmdBuffer, resource.image, resource.format, resource.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    ImageBarrier(cmdBuffer, copy.image, copy.format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // A blit would decode/encode sRGB, a copy keeps the stored values as they are
    if (WithoutSrgb(format) == WithoutSrgb(resource.format))
    {
        VkImageCopy region {};
        region.srcSubresource = { aspect, 0, 0, 1 };
        region.dstSubresource = { aspect, 0, 0, 1 };
        region.extent = { copy.width, copy.height, 1 };
        vkCmdCopyImage(cmdBuffer, resource.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, copy.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    else
    {
        // Converts between color formats
        VkImageBlit region {};
        region.srcSubresource = { aspect, 0, 0, 1 };
        region.dstSubresource = { aspect, 0, 0, 1 };
        region.srcOffsets[1] = { (int32_t) copy.width, (int32_t) copy.height, 1 };
        region.dstOffsets[1] = region.srcOffsets[1];
        vkCmdBlitImage(cmdBuffer, resource.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, copy.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
    }

    ImageBarrier(cmdBuffer, resource.image, resource.format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, resource.layout);

    resource.image = copy.image;
    resource.view = copy.view;
    resource.format = copy.format;
    resource.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    return true;
}

bool IFGFeature_Vk::FlipResource(VkCommandBuffer cmdBuffer, VkFGResource& resource, int fIndex)
{
    auto type = resource.type;
    auto velocity = type == FG_ResourceType::Velocity;

    // Depth formats can't be storage images
    auto format = VK_FORMAT_R32_SFLOAT;

    if (velocity)
    {
        VkFormatProperties properties {};
        vkGetPhysicalDeviceFormatProperties(_physicalDevice, resource.format, &properties);

        format = (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
                     ? resource.format
                     : VK_FORMAT_R16G16B16A16_SFLOAT;
    }

    auto& flipped = _flipped[fIndex][type];

    if (!CreateCopy(flipped, type, format, resource.imageWidth, resource.imageHeight,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT))
    {
        return false;
    }

    auto& flip = velocity ? _mvFlip : _depthFlip;

    if (flip == nullptr)
        flip = std::make_unique<RF_Vk>(velocity ? "VelocityFlip" : "DepthFlip", _device, _physicalDevice);

    if (!flip->IsInit())
        return false;

    // Can be the game's image, its layout is restored after the flip
    const auto sourceLayout = resource.layout;
    ImageBarrier(cmdBuffer, resource.image, resource.format, sourceLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    ImageBarrier(cmdBuffer, flipped.image, flipped.format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    const bool flipResult =
        flip->Dispatch(cmdBuffer, resource.view, flipped.view, { resource.imageWidth, resource.imageHeight },
                       resource.width, resource.height, velocity);

    ImageBarrier(cmdBuffer, resource.image, resource.format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sourceLayout);

    if (!flipResult)
    {
        LOG_ERROR("Can't flip {}", magic_enum::enum_name(type));
        return false;
    }

    ImageBarrier(cmdBuffer, flipped.image, flipped.format, VK_IMAGE_LAYOUT_GENERAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    LOG_TRACE("Setting {} from flip, index: {}", magic_enum::enum_name(type), fIndex);
    resource.image = flipped.image;
    resource.view = flipped.view;
    resource.format = flipped.format;
    resource.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    return true;
}

bool IFGFeature_Vk::SetResource(VkFGResource* inputResource)
{
    if (inputResource == nullptr || inputResource->image == VK_NULL_HANDLE || !IsActive() || IsPaused())
        return false;

    // Late sent resources come with their frame's index
    auto fIndex = inputResource->frameIndex >= 0 ? inputResource->frameIndex : GetIndex();
    auto type = inputResource->type;
    auto config = Config::Instance();

    std::unique_lock lock(_resourceMutex[fIndex]);

    if (auto it = _frameResources[fIndex].find(type);
        it != _frameResources[fIndex].end() && it->second.validity == FG_ResourceValidity::ValidNow)
    {
        return false;
    }

    if (type == FG_ResourceType::HudlessColor &&
        (config->FGDisableHudless.value_or_default() ||
         (!_noHudless[fIndex] && config->FGOnlyAcceptFirstHudless.value_or_default())))
    {
        return false;
    }

    if (type == FG_ResourceType::UIColor && config->FGDisableUI.value_or_default())
        return false;

    auto resource = *inputResource;

    auto willFlip = State::Instance().activeFgInput == FGInput::Upscaler && config->FGResourceFlip.value_or_default() &&
                    (type == FG_ResourceType::Depth || type == FG_ResourceType::Velocity);

    if (resource.validity == FG_ResourceValidity::ValidNow || willFlip)
    {
        if (resource.cmdBuffer == VK_NULL_HANDLE)
        {
            LOG_ERROR("{}, no cmdBuffer to copy with!", magic_enum::enum_name(type));
            return false;
        }

        // The flipped image is our own copy already, flipping straight from the source saves one.
        // Without a view the shader can't read the source, it gets copied first.
        const bool flipped =
            willFlip && resource.view != VK_NULL_HANDLE && FlipResource(resource.cmdBuffer, resource, fIndex);

        if (!flipped)
        {
            if (!CopyImage(resource.cmdBuffer, resource, _copies[fIndex][type], resource.format))
                return false;

            if (willFlip)
                FlipResource(resource.cmdBuffer, resource, fIndex);
        }
    }

    _frameResources[fIndex][type] = resource;

    if (type == FG_ResourceType::UIColor)
        _noUi[fIndex] = false;
    else if (type == FG_ResourceType::HudlessColor)
        _noHudless[fIndex] = false;
    else if (type == FG_ResourceType::Distortion)
        _noDistortionField[fIndex] = false;

    SetResourceReady(type, fIndex);

    LOG_TRACE("_frameResources[{}][{}]: {:X}", fIndex, magic_enum::enum_name(type), (size_t) resource.image);
    return true;
}

void IFGFeature_Vk::RemoveUiFromHudless(VkCommandBuffer cmdBuffer, bool premultiplied)
{
    auto hudless = GetResource(FG_ResourceType::HudlessColor);
    auto ui = GetResource(FG_ResourceType::UIColor);

    if (!hudless || !ui || hudless->view == VK_NULL_HANDLE || ui->view == VK_NULL_HANDLE)
        return;

    if (_uiUnblend == nullptr)
        _uiUnblend = std::make_unique<UiUnblend_Vk>("UiUnblend", _device, _physicalDevice);

    if (!_uiUnblend->IsInit())
        return;

    auto imageInfo = [](const VkFGResource& resource) -> VkImageInfo
    {
        return { resource.view,   resource.image,      { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
                 resource.format, resource.imageWidth, resource.imageHeight };
    };

    _uiUnblend->Dispatch(cmdBuffer, imageInfo(*ui), ui->layout, imageInfo(*hudless), hudless->layout, premultiplied);
}

VkCommandBuffer IFGFeature_Vk::BeginCommandBuffer(int index)
{
    // Fence is created last, without it the rest might be missing too
    if (_fence[index] == VK_NULL_HANDLE)
    {
        VkCommandPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = _gameQueueFamily;

        if (_commandPool[index] == VK_NULL_HANDLE &&
            vkCreateCommandPool(_device, &poolInfo, nullptr, &_commandPool[index]) != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateCommandPool failed for {}", index);
            return VK_NULL_HANDLE;
        }

        VkCommandBufferAllocateInfo allocInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocInfo.commandPool = _commandPool[index];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (_commandBuffer[index] == VK_NULL_HANDLE &&
            vkAllocateCommandBuffers(_device, &allocInfo, &_commandBuffer[index]) != VK_SUCCESS)
        {
            LOG_ERROR("vkAllocateCommandBuffers failed for {}", index);
            return VK_NULL_HANDLE;
        }

        VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateFence(_device, &fenceInfo, nullptr, &_fence[index]) != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateFence failed for {}", index);
            return VK_NULL_HANDLE;
        }
    }

    if (vkWaitForFences(_device, 1, &_fence[index], VK_TRUE, 1'000'000'000) != VK_SUCCESS)
    {
        LOG_ERROR("Waiting for the FG command buffer {} timed out", index);
        return VK_NULL_HANDLE;
    }

    vkResetCommandPool(_device, _commandPool[index], 0);

    VkCommandBufferBeginInfo beginInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(_commandBuffer[index], &beginInfo);

    return _commandBuffer[index];
}

void IFGFeature_Vk::WaitForCommandBuffers()
{
    for (auto fence : _fence)
    {
        if (fence != VK_NULL_HANDLE)
            vkWaitForFences(_device, 1, &fence, VK_TRUE, 1'000'000'000);
    }
}

void IFGFeature_Vk::DestroyCommandBuffers()
{
    WaitForCommandBuffers();

    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        if (_fence[i] != VK_NULL_HANDLE)
            vkDestroyFence(_device, _fence[i], nullptr);

        // Frees the command buffer too
        if (_commandPool[i] != VK_NULL_HANDLE)
            vkDestroyCommandPool(_device, _commandPool[i], nullptr);

        _fence[i] = VK_NULL_HANDLE;
        _commandPool[i] = VK_NULL_HANDLE;
        _commandBuffer[i] = VK_NULL_HANDLE;
    }
}

std::optional<VkFGResource> IFGFeature_Vk::GetResource(FG_ResourceType type, int index)
{
    if (index < 0)
        index = GetIndex();

    std::shared_lock lock(_resourceMutex[index]);

    if (auto it = _frameResources[index].find(type); it != _frameResources[index].end())
        return it->second;

    return std::nullopt;
}

bool IFGFeature_Vk::HasResource(FG_ResourceType type, int index)
{
    if (index < 0)
        index = GetIndex();

    std::shared_lock lock(_resourceMutex[index]);
    return _frameResources[index].contains(type);
}

void IFGFeature_Vk::NewFrame()
{
    if (_waitingNewFrameData)
    {
        LOG_DEBUG("Re-activating FG");
        UpdateTarget();
        Activate();
        _waitingNewFrameData = false;
    }

    auto fIndex = GetIndex();

    std::unique_lock lock(_resourceMutex[fIndex]);
    _frameResources[fIndex].clear();
    _lastFGFramePresentId = _fgFramePresentId;
}

IFGFeature_Vk::~IFGFeature_Vk()
{
    if (_device == VK_NULL_HANDLE)
        return;

    for (auto& frameCopies : _copies)
    {
        for (auto& copy : frameCopies)
            DestroyCopy(copy);
    }

    for (auto& frameCopies : _flipped)
    {
        for (auto& copy : frameCopies)
            DestroyCopy(copy);
    }
}
