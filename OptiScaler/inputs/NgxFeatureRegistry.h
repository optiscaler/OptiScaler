#pragma once

#include <nvsdk_ngx_defs.h>
#include <mutex>
#include <optional>
#include <unordered_map>

class NgxFeatureRegistry
{
    std::mutex mutex;
    std::unordered_map<unsigned int, NVSDK_NGX_Feature> features;

  public:
    void Clear()
    {
        std::lock_guard lock(mutex);
        features.clear();
    }

    void Record(unsigned int handle, NVSDK_NGX_Feature feature)
    {
        std::lock_guard lock(mutex);
        features[handle] = feature;
    }

    void Forget(unsigned int handle)
    {
        std::lock_guard lock(mutex);
        features.erase(handle);
    }

    struct Snapshot
    {
        std::optional<NVSDK_NGX_Feature> feature;
        bool frameGenerationCreated = false;
    };

    Snapshot Read(unsigned int handle)
    {
        std::lock_guard lock(mutex);
        Snapshot result;
        if (auto it = features.find(handle); it != features.end())
            result.feature = it->second;
        for (const auto& [id, feature] : features)
            result.frameGenerationCreated |= feature == NVSDK_NGX_Feature_FrameGeneration;
        return result;
    }
};
