#pragma once

#include <vector>
#include <atomic>
#include <thread>
#include <shared_mutex>

enum class SkipSpoofType
{
    // OneShot, // Applied only once, then removed
    Thread, // Applied only to the current thread
    Global, // Applies to every thread, not just the current one
};

// TODO: Unused, do we need it?
enum class SkipSpoofApi
{
    Dxgi,
    Vulkan,
    All,
};

struct SkipSpoofEntry
{
    uint64_t id;
    SkipSpoofType type;
    std::thread::id threadId;
};

class SkipSpoof
{
    inline static std::vector<SkipSpoofEntry> entries;
    inline static std::shared_mutex entriesMutex;
    inline static std::atomic<uint64_t> nextId { 1 };

  public:
    static uint64_t AddEntry(SkipSpoofType type);
    static void RemoveEntry(uint64_t id);
    static bool ShouldSkip();
};

bool SkipSpoofing();