#pragma once

#include <vulkan/vulkan.h>
#include <cstring>
#include <mutex>
#include <unordered_map>

// The public FFX format enum has no RG16 normalized entries. For supported
// providers, keep its RGBA8 normalized descriptor alias but restore the
// actual NGX motion-vector view format when the provider creates its own view.
// The provider owns that new view; the game's original view is never borrowed.
class FSR4MotionVectorViewsVk
{
    struct SdkFunctions
    {
        PFN_vkGetDeviceProcAddr getDeviceProcAddr;
        PFN_vkCreateImageView createImageView;
        size_t users = 0;
    };

  public:
    // Keep the SDK callback valid until its context has been destroyed. Multiple
    // contexts may share a device, but must agree on the original function chain.
    class SdkDevice
    {
      public:
        SdkDevice(VkDevice device, PFN_vkGetDeviceProcAddr next)
        {
            if (device == VK_NULL_HANDLE || next == nullptr || next == GetDeviceProcAddr)
                return;

            const auto createView = reinterpret_cast<PFN_vkCreateImageView>(next(device, "vkCreateImageView"));
            if (createView == nullptr)
                return;

            std::scoped_lock lock(_sdkMutex);
            auto [entry, inserted] = _sdkDevices.try_emplace(device, SdkFunctions { next, createView });
            if (!inserted && (entry->second.getDeviceProcAddr != next || entry->second.createImageView != createView))
                return;

            ++entry->second.users;
            _device = device;
        }

        ~SdkDevice()
        {
            if (!IsReady())
                return;

            std::scoped_lock lock(_sdkMutex);
            auto entry = _sdkDevices.find(_device);
            if (entry != _sdkDevices.end() && --entry->second.users == 0)
                _sdkDevices.erase(entry);
        }

        SdkDevice(const SdkDevice&) = delete;
        SdkDevice& operator=(const SdkDevice&) = delete;
        bool IsReady() const { return _device != VK_NULL_HANDLE; }

        static PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* name)
        {
            const auto functions = FindSdkFunctions(device);
            if (functions.getDeviceProcAddr == nullptr)
                return nullptr;

            if (name != nullptr && std::strcmp(name, "vkCreateImageView") == 0)
                return reinterpret_cast<PFN_vkVoidFunction>(CreateSdkImageView);

            return functions.getDeviceProcAddr(device, name);
        }

      private:
        VkDevice _device = VK_NULL_HANDLE;
    };

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
    static SdkFunctions FindSdkFunctions(VkDevice device)
    {
        std::scoped_lock lock(_sdkMutex);
        const auto entry = _sdkDevices.find(device);
        return entry != _sdkDevices.end() ? entry->second : SdkFunctions {};
    }

    static VkResult VKAPI_CALL CreateSdkImageView(VkDevice device, const VkImageViewCreateInfo* info,
                                                  const VkAllocationCallbacks* allocator, VkImageView* view)
    {
        return CreateImageView(FindSdkFunctions(device).createImageView, device, info, allocator, view);
    }

    inline static std::mutex _sdkMutex;
    inline static std::unordered_map<VkDevice, SdkFunctions> _sdkDevices;

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
