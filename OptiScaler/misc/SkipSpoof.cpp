#include "pch.h"
#include "SkipSpoof.h"

#include "State.h"
#include "Config.h"

uint64_t SkipSpoof::AddEntry(SkipSpoofType type)
{
    uint64_t id = nextId.fetch_add(1, std::memory_order_relaxed);

    SkipSpoofEntry entry;
    entry.id = id;
    entry.type = type;
    entry.threadId = std::this_thread::get_id();

    std::scoped_lock lock(entriesMutex);
    entries.push_back(std::move(entry));

    return id;
}

void SkipSpoof::RemoveEntry(uint64_t id)
{
    std::scoped_lock lock(entriesMutex);
    auto it =
        std::remove_if(entries.begin(), entries.end(), [id](const SkipSpoofEntry& entry) { return entry.id == id; });

    if (it != entries.end())
        entries.erase(it, entries.end());
}

bool SkipSpoof::ShouldSkip()
{
    const auto currentThreadId = std::this_thread::get_id();

    std::shared_lock lock(entriesMutex);
    for (const auto& entry : entries)
    {
        if (entry.type == SkipSpoofType::Global)
            return true;
        else if (entry.type == SkipSpoofType::Thread && entry.threadId == currentThreadId)
            return true;
    }

    return false;
}

bool SkipSpoofing()
{
    const bool dxgiSpoofing = Config::Instance()->DxgiSpoofing.value_or_default();
    bool skip = false;

    if (dxgiSpoofing)
        skip = SkipSpoof::ShouldSkip();

    if (skip)
    {
        if (dxgiSpoofing)
            LOG_TRACE("DxgiSpoofing: {}, skipSpoofing: {}, skipping spoofing", dxgiSpoofing, skip);
        else
            LOG_TRACE("DxgiSpoofing: {}, skipping spoofing", dxgiSpoofing);
    }

    return skip;
}

bool SkipVulkanSpoofing()
{
    const bool vulkanSpoofing = Config::Instance()->VulkanSpoofing.value_or_default();
    bool skip = false;

    if (vulkanSpoofing)
        skip = SkipSpoof::ShouldSkip();

    if (skip)
    {
        if (vulkanSpoofing)
            LOG_TRACE("VulkanSpoofing: {}, skipSpoofing: {}, skipping spoofing", vulkanSpoofing, skip);
        else
            LOG_TRACE("VulkanSpoofing: {}, skipping spoofing", vulkanSpoofing);
    }

    return skip;
}
