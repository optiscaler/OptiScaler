#include "pch.h"
#include "VkwDx12_Inputs.h"

#include <State.h>

#include <framegen/IFGFeature_Dx12.h>
#include <with_dx12/vk_with_dx12_sc.h>
#include <shaders/depth_transfer/DT_Vk.h>
#include <shaders/resource_copy/RC_Vk.h>

#include <magic_enum.hpp>

#include <memory>
#include <optional>
#include <string>

namespace
{
enum class CopyMethod
{
    Depth,    // Depth formats can't be shared, the depth shader writes R32 float
    Shader,   // Samples the input's view, converts the format
    Transfer, // Keeps the stored values, for sRGB inputs and inputs without a view
};

struct CopyTarget
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    CopyMethod method = CopyMethod::Shader;
};

std::mutex inputsMutex;

VkDevice _device = VK_NULL_HANDLE;
VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
ID3D12Device* _dx12Device = nullptr;
std::vector<VkQueueFamilyProperties> _queueFamilies;

VkwDx12Image _shared[BUFFER_COUNT][ResourceTypeCOUNT] {};

// Inputs valid until present, the present copies them
std::optional<VkFGResource> _pending[BUFFER_COUNT][ResourceTypeCOUNT] {};

// One copy shader per input type, each dispatch takes the next of its descriptor sets
std::unique_ptr<ResourceCopy_Vk> _copies[ResourceTypeCOUNT];
std::unique_ptr<DepthTransfer_Vk> _depthTransfer;
VkFormat _depthTransferFormat = VK_FORMAT_UNDEFINED;

VkImageAspectFlags AspectMask(VkFormat format)
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

void ImageBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout,
                  VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess)
{
    VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { AspectMask(format), 0, 1, 0, 1 };

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
}

bool StorageFormat(VkFormat format)
{
    VkFormatProperties properties {};
    vkGetPhysicalDeviceFormatProperties(_physicalDevice, format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
}

// The shared format and how the input gets there, false if it can't be shared
bool SharedTarget(const VkFGResource& input, CopyTarget& target)
{
    if (input.type == FG_ResourceType::Depth)
    {
        target = { VK_FORMAT_R32_SFLOAT, CopyMethod::Depth };
        return input.view != VK_NULL_HANDLE;
    }

    if (input.type == FG_ResourceType::Velocity)
    {
        // The copy shader converts other motion vector formats
        target.format = VK_FORMAT_R16G16_SFLOAT;

        if (input.format == VK_FORMAT_R32G32_SFLOAT || input.format == VK_FORMAT_R16G16B16A16_SFLOAT)
            target.format = input.format;

        target.method = CopyMethod::Shader;
        return input.view != VK_NULL_HANDLE;
    }

    // Sampling would decode sRGB, a copy keeps the encoded values like the game's DX12 inputs
    auto format = IFGFeature_Vk::WithoutSrgb(input.format);

    if (format == VK_FORMAT_A8B8G8R8_UNORM_PACK32)
        format = VK_FORMAT_R8G8B8A8_UNORM;

    if (format != input.format || input.view == VK_NULL_HANDLE)
    {
        target = { format, CopyMethod::Transfer };
        return VkwDx12SC::CanShare(format);
    }

    // Formats the shader can't write or D3D12 can't share keep their precision in a channel order it can,
    // others become 16 bit float
    if (format == VK_FORMAT_B8G8R8A8_UNORM && !StorageFormat(format))
        format = VK_FORMAT_R8G8B8A8_UNORM;
    else if (format == VK_FORMAT_A2R10G10B10_UNORM_PACK32)
        format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;

    target.format = VkwDx12SC::CanShare(format) && StorageFormat(format) ? format : VK_FORMAT_R16G16B16A16_SFLOAT;
    target.method = CopyMethod::Shader;
    return true;
}

// A new device, the old one's objects went with it
void UseDevices(VkDevice device, VkPhysicalDevice physicalDevice, ID3D12Device* dx12Device)
{
    if (device == _device && dx12Device == _dx12Device)
        return;

    const bool sameDevice = device == _device;

    for (auto& slot : _shared)
    {
        for (auto& image : slot)
        {
            // Only the D3D12 device is new
            if (sameDevice)
            {
                VkwDx12SC::DestroySharedImage(_device, image);
                continue;
            }

            if (image.resource != nullptr)
                image.resource->Release();

            image = {};
        }
    }

    for (auto& slot : _pending)
    {
        for (auto& pending : slot)
            pending.reset();
    }

    if (!sameDevice)
    {
        for (auto& copy : _copies)
            (void) copy.release();

        (void) _depthTransfer.release();
        _depthTransferFormat = VK_FORMAT_UNDEFINED;
    }

    _device = device;
    _physicalDevice = physicalDevice;
    _dx12Device = dx12Device;

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    _queueFamilies.resize(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, _queueFamilies.data());
}

// The frame's shared texture of the input type, recreated when the format or size changed
bool PrepareShared(VkwDx12Image& shared, VkFormat format, uint32_t width, uint32_t height)
{
    if (shared.image != VK_NULL_HANDLE && shared.format == format && shared.width == width && shared.height == height)
        return true;

    // D3D12 is done with the slot, so is the Vulkan copy it waited for
    VkwDx12SC::DestroySharedImage(_device, shared);

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if (StorageFormat(format))
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;

    return VkwDx12SC::CreateSharedImage(_device, _physicalDevice, _dx12Device, shared, format, width, height, usage);
}

// Copies the whole input into the shared texture. Inputs in a known layout are moved to the one the copy reads and
// back, the upscaler's come without one and are read as they are.
bool CopyToShared(VkCommandBuffer cmdBuffer, const VkFGResource& input, const CopyTarget& target, VkwDx12Image& shared)
{
    const bool transfer = target.method == CopyMethod::Transfer;
    const auto readLayout = transfer ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const auto writeLayout = transfer ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
    const VkAccessFlags readAccess = transfer ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT;
    const VkAccessFlags writeAccess = transfer ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_SHADER_WRITE_BIT;
    const bool transition = input.layout != VK_IMAGE_LAYOUT_UNDEFINED && input.layout != readLayout;

    VkMemoryBarrier inputBarrier { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    inputBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    inputBarrier.dstAccessMask = readAccess;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1,
                         &inputBarrier, 0, nullptr, 0, nullptr);

    if (transition)
        ImageBarrier(cmdBuffer, input.image, input.format, input.layout, readLayout, VK_ACCESS_MEMORY_WRITE_BIT,
                     readAccess);

    ImageBarrier(cmdBuffer, shared.image, shared.format, VK_IMAGE_LAYOUT_UNDEFINED, writeLayout, 0, writeAccess);

    const VkExtent2D extent { input.imageWidth, input.imageHeight };
    bool copied = false;

    if (target.method == CopyMethod::Depth)
    {
        if (_depthTransfer == nullptr || _depthTransferFormat != input.format)
        {
            _depthTransfer =
                std::make_unique<DepthTransfer_Vk>("FG DepthTransfer", _device, _physicalDevice, input.format);
            _depthTransferFormat = input.format;
        }

        copied = _depthTransfer->CanRender() &&
                 _depthTransfer->Dispatch(_device, cmdBuffer, input.view, shared.view, extent);
    }
    else if (target.method == CopyMethod::Shader)
    {
        auto& copy = _copies[input.type];

        if (copy == nullptr)
        {
            auto name = "FG " + std::string(magic_enum::enum_name(input.type)) + "Copy";
            copy = std::make_unique<ResourceCopy_Vk>(name, _device, _physicalDevice);
        }

        copied = copy->CanRender() && copy->Dispatch(_device, cmdBuffer, input.view, shared.view, extent);
    }
    else
    {
        VkImageCopy region {};
        region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.extent = { extent.width, extent.height, 1 };

        const auto sourceLayout = input.layout != VK_IMAGE_LAYOUT_UNDEFINED ? readLayout : VK_IMAGE_LAYOUT_GENERAL;
        vkCmdCopyImage(cmdBuffer, input.image, sourceLayout, shared.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                       &region);
        copied = true;
    }

    ImageBarrier(cmdBuffer, shared.image, shared.format, writeLayout, VK_IMAGE_LAYOUT_GENERAL, writeAccess,
                 VK_ACCESS_MEMORY_READ_BIT);

    if (transition)
        ImageBarrier(cmdBuffer, input.image, input.format, readLayout, input.layout, readAccess,
                     VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);

    if (!copied)
    {
        LOG_ERROR("Can't copy {} for FG", magic_enum::enum_name(input.type));
        return false;
    }

    shared.layout = VK_IMAGE_LAYOUT_GENERAL;
    return true;
}
} // namespace

bool VkwDx12Inputs::Active()
{
    auto& state = State::Instance();
    return state.swapchainInteropApi == SwapchainInteropApi::VkwDx12 && state.currentFG != nullptr;
}

bool VkwDx12Inputs::SetResource(const VkFGResource& input)
{
    auto& state = State::Instance();
    auto fg = state.currentFG;

    if (fg == nullptr || state.swapchainInteropApi != SwapchainInteropApi::VkwDx12 || input.image == VK_NULL_HANDLE ||
        input.type >= ResourceTypeCOUNT)
    {
        return false;
    }

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    ID3D12Device* dx12Device = nullptr;

    if (!VkwDx12SC::InteropDevices(device, physicalDevice, dx12Device))
        return false;

    const auto type = input.type;
    const int index = input.frameIndex >= 0 && input.frameIndex < BUFFER_COUNT ? input.frameIndex : fg->GetIndex();

    // The FG takes the first input of each type a frame
    if (fg->HasResource(type, index))
        return false;

    // D3D12 is done with the slot's last inputs
    if (!VkwDx12SC::WaitForInputSlot(index))
        return false;

    std::scoped_lock lock(inputsMutex);
    UseDevices(device, physicalDevice, dx12Device);

    CopyTarget target {};

    if (!SharedTarget(input, target))
    {
        static bool logged[ResourceTypeCOUNT] {};

        if (!logged[type])
        {
            logged[type] = true;
            LOG_WARN("Can't share {} with D3D12, format: {}, view: {:X}", magic_enum::enum_name(type),
                     (UINT) input.format, (size_t) input.view);
        }

        return false;
    }

    auto& shared = _shared[index][type];

    if (!PrepareShared(shared, target.format, input.imageWidth, input.imageHeight))
        return false;

    // Inputs only valid now are copied with their command buffer, the present copies the others
    const bool copyNow = input.validity == FG_ResourceValidity::ValidNow && input.cmdBuffer != VK_NULL_HANDLE;

    if (copyNow && !CopyToShared(input.cmdBuffer, input, target, shared))
        return false;

    // The FG copies it at present, after D3D12 waited for the Vulkan frame
    Dx12Resource setResource {};
    setResource.type = type;
    setResource.cmdList = fg->GetUICommandList(index);
    setResource.resource = shared.resource;
    setResource.state = D3D12_RESOURCE_STATE_COMMON;
    setResource.validity = FG_ResourceValidity::ValidNow;
    setResource.width = input.width;
    setResource.height = input.height;
    setResource.frameIndex = index;

    if (setResource.cmdList == nullptr || !fg->SetResource(&setResource))
        return false;

    if (!copyNow)
        _pending[index][type] = input;

    VkwDx12SC::InputSlotUsed(index);

    LOG_DEBUG("(FG VkwDx12) {} {} for slot {}", magic_enum::enum_name(type), copyNow ? "copied" : "waits for present",
              index);

    return true;
}

void VkwDx12Inputs::RecordPresentCopies(VkCommandBuffer cmdBuffer, uint32_t queueFamily)
{
    auto fg = State::Instance().currentFG;

    if (fg == nullptr)
        return;

    std::scoped_lock lock(inputsMutex);

    if (_device == VK_NULL_HANDLE)
        return;

    // The copy shaders need a compute queue
    const bool compute =
        queueFamily < _queueFamilies.size() && (_queueFamilies[queueFamily].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    const auto index = fg->GetIndexWillBeDispatched();

    for (uint32_t i = 0; i < ResourceTypeCOUNT; i++)
    {
        auto& pending = _pending[index][i];

        if (!pending.has_value())
            continue;

        CopyTarget target {};

        if (!SharedTarget(*pending, target) || (!compute && target.method != CopyMethod::Transfer) ||
            !CopyToShared(cmdBuffer, *pending, target, _shared[index][i]))
        {
            LOG_WARN("{} isn't copied for FG slot {}", magic_enum::enum_name((FG_ResourceType) i), index);
        }

        pending.reset();
    }
}

void VkwDx12Inputs::Shutdown()
{
    std::scoped_lock lock(inputsMutex);

    if (_device == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(_device);

    for (auto& slot : _shared)
    {
        for (auto& image : slot)
            VkwDx12SC::DestroySharedImage(_device, image);
    }

    for (auto& slot : _pending)
    {
        for (auto& pending : slot)
            pending.reset();
    }

    for (auto& copy : _copies)
        copy.reset();

    _depthTransfer.reset();
    _depthTransferFormat = VK_FORMAT_UNDEFINED;
    _device = VK_NULL_HANDLE;
    _physicalDevice = VK_NULL_HANDLE;
    _dx12Device = nullptr;
}
