#pragma once

#include <Util.h>

#include <intrin.h>
#include <set>

// Modules using an NGX instance, the game, Streamline and mods can share one
class NVNGXClients
{
  private:
    std::mutex _mutex;
    std::set<HMODULE> _modules;

  public:
    void Add(void* returnAddress)
    {
        auto module = Util::GetCallerModule(returnAddress);

        // Nested calls and Opti's own inputs, they never shut down
        if (module == dllModule)
            return;

        std::lock_guard<std::mutex> lock(_mutex);

        if (_modules.insert(module).second)
            LOG_INFO("{}", Util::WhoIsTheCaller(returnAddress));
    }

    // False while another module still uses the instance, also for a module's second shutdown
    bool Remove(void* returnAddress)
    {
        auto module = Util::GetCallerModule(returnAddress);

        if (module == dllModule)
            return true;

        std::lock_guard<std::mutex> lock(_mutex);

        _modules.erase(module);

        if (_modules.empty())
            return true;

        LOG_INFO("Skipping shutdown from {}, {} other module(s) still use NGX", Util::WhoIsTheCaller(returnAddress),
                 _modules.size());

        return false;
    }
};

void NVNGXDx11DeviceDestroyed(IUnknown* device);
void NVNGXDx12DeviceDestroyed(IUnknown* device);
void NVNGXVkDeviceDestroyed(VkDevice device);

template <typename FeatureType> struct ContextData
{
    std::unique_ptr<FeatureType> feature;
    NVSDK_NGX_Parameter* createParams = nullptr;
    bool createParamsFromGame = false; // The game's table, never destroyed by Opti
    int changeBackendCounter = 0;
    UINT64 lastEvalFrame = 0;
};
