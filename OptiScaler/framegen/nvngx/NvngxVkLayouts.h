#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

// Streamline and the DLSSG replacements sometimes transition an image from VK_IMAGE_LAYOUT_UNDEFINED while it still
// holds a frame that is read next. RDNA2/3 reinitialize the DCC metadata on that and read the frame back as noise.
// Such barriers start from the image's last seen layout instead.
// Tracked images: the DLSSG parameters, plus anything barriered on the thread running the replacement's evaluate.
class NvngxVkLayouts
{
    inline static std::unordered_map<VkImage, VkImageLayout> _layouts;
    // ponytail: one global lock per barrier once DLSSG ran, shared_mutex or sharding if it shows in profiles
    inline static std::mutex _mutex;
    inline static std::atomic<bool> _active = false;

  public:
    inline static thread_local bool inEvaluate = false;

    static void Track(VkImage image)
    {
        std::scoped_lock lock(_mutex);
        _layouts.try_emplace(image, VK_IMAGE_LAYOUT_UNDEFINED);
        _active = true;
    }

    // Handles get reused, a new image must not inherit the old one's layout
    static void Forget(VkImage image)
    {
        if (!_active)
            return;

        std::scoped_lock lock(_mutex);
        _layouts.erase(image);
    }

    // Returns barriers, or storage holding the fixed copies
    template <typename Barrier>
    static const Barrier* Fixup(const Barrier* barriers, uint32_t count, std::vector<Barrier>& storage)
    {
        if (barriers == nullptr || (!_active && !inEvaluate))
            return barriers;

        std::scoped_lock lock(_mutex);

        if (inEvaluate)
            _active = true;

        for (uint32_t i = 0; i < count; i++)
        {
            auto it = inEvaluate ? _layouts.try_emplace(barriers[i].image, VK_IMAGE_LAYOUT_UNDEFINED).first
                                 : _layouts.find(barriers[i].image);

            if (it == _layouts.end())
                continue;

            if (barriers[i].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && it->second != VK_IMAGE_LAYOUT_UNDEFINED)
            {
                LOG_TRACE("Image {:X}: UNDEFINED -> {} now starts from {}", (size_t) barriers[i].image,
                          (int) barriers[i].newLayout, (int) it->second);

                if (storage.empty())
                    storage.assign(barriers, barriers + count);

                storage[i].oldLayout = it->second;
            }

            it->second = barriers[i].newLayout;
        }

        return storage.empty() ? barriers : storage.data();
    }

    static const VkDependencyInfo* Fixup(const VkDependencyInfo* info, VkDependencyInfo& copy,
                                         std::vector<VkImageMemoryBarrier2>& storage)
    {
        if (info == nullptr ||
            Fixup(info->pImageMemoryBarriers, info->imageMemoryBarrierCount, storage) == info->pImageMemoryBarriers)
            return info;

        copy = *info;
        copy.pImageMemoryBarriers = storage.data();
        return &copy;
    }
};
