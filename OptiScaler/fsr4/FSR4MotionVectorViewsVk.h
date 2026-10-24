#pragma once

#include <vulkan/vulkan.h>

// The public FFX format enum has no RG16 normalized entries. For the driver
// provider only, keep its RGBA8 normalized descriptor alias but restore the
// actual NGX motion-vector view format when the provider creates its own view.
// The provider owns that new view; the game's original view is never borrowed.
class FSR4MotionVectorViewsVk
{
  public:
    static bool IsEligible(VkFormat format, const VkImageSubresourceRange& range)
    {
        return (format == VK_FORMAT_R16G16_SNORM || format == VK_FORMAT_R16G16_UNORM) &&
               range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT && range.baseMipLevel == 0 &&
               (range.levelCount == 1 || range.levelCount == VK_REMAINING_MIP_LEVELS) && range.baseArrayLayer == 0 &&
               (range.layerCount == 1 || range.layerCount == VK_REMAINING_ARRAY_LAYERS);
    }

    FSR4MotionVectorViewsVk(bool enabled, VkDevice device, VkImage image, VkFormat format,
                            const VkImageSubresourceRange& range)
        : _previous(_current), _device(device), _image(image), _format(format),
          _range { range.aspectMask, range.baseMipLevel, 1, range.baseArrayLayer, 1 },
          _enabled(enabled && device != VK_NULL_HANDLE && image != VK_NULL_HANDLE && IsEligible(format, range))
    {
        _current = this;
    }

    ~FSR4MotionVectorViewsVk() { _current = _previous; }
    FSR4MotionVectorViewsVk(const FSR4MotionVectorViewsVk&) = delete;
    FSR4MotionVectorViewsVk& operator=(const FSR4MotionVectorViewsVk&) = delete;

    static VkResult CreateImageView(PFN_vkCreateImageView next, VkDevice device, const VkImageViewCreateInfo* info,
                                    const VkAllocationCallbacks* allocator, VkImageView* view)
    {
        if (next == nullptr)
            return VK_ERROR_INITIALIZATION_FAILED;

        auto scope = _current;
        if (scope == nullptr || !scope->_enabled || device != scope->_device || info == nullptr ||
            info->image != scope->_image)
            return next(device, info, allocator, view);

        const auto alias =
            scope->_format == VK_FORMAT_R16G16_SNORM ? VK_FORMAT_R8G8B8A8_SNORM : VK_FORMAT_R8G8B8A8_UNORM;
        if (info->format != alias || info->viewType != VK_IMAGE_VIEW_TYPE_2D)
            return next(device, info, allocator, view);

        auto corrected = *info;
        corrected.format = scope->_format;
        // Motion-vector loads use LOD/layer zero. Match the FFX resource's
        // single-mip, single-layer description even if NGX uses REMAINING_*.
        corrected.subresourceRange = scope->_range;
        return next(device, &corrected, allocator, view);
    }

  private:
    // Resource registration creates its views synchronously during FFX Dispatch.
    // Limit the alias to that call/thread; no stale VkImage handles survive it.
    inline static thread_local FSR4MotionVectorViewsVk* _current = nullptr;
    FSR4MotionVectorViewsVk* _previous;
    VkDevice _device;
    VkImage _image;
    VkFormat _format;
    VkImageSubresourceRange _range;
    bool _enabled;
};
