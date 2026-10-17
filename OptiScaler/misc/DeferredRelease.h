#pragma once
#include "SysUtils.h"

#include <functional>

// Destroys GPU objects once no queue can still reference them.
// An entry runs after enough presents and enough wall time have passed. Presents bound how far the GPU
// can lag behind, the minimum age covers FG libraries that read our resources from their own queues.
// When presents aren't hooked (no overlay menu and no FG), entries run after a longer fallback age instead.
namespace DeferredRelease
{
// owner ties the entry to an API device that can go away before the entry runs, like a VkDevice
void Enqueue(std::function<void()> destroy, uint64_t owner = 0);

// Call once per present
void Process();

// Releases what's ready without counting a present. For per frame paths that run even when
// the swapchain isn't hooked, like upscaler evaluation, so entries still go after a fallback age.
void Collect();

// Forgets owned entries of every other owner without running them. Their device is most likely destroyed already,
// destroying the objects then would be invalid, leaking them is the safe option.
void DropOwnersExcept(uint64_t keep);

template <typename T> void Release(T*& object)
{
    if (object == nullptr)
        return;

    auto* captured = object;
    object = nullptr;

    Enqueue([captured]() { captured->Release(); });
}
} // namespace DeferredRelease
