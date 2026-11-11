#pragma once

#include <proxies/Nvngx_RRDllProxy.h>
#include <misc/CustomProviders.h>
#include <NVNGX_Parameter.h>

#include <map>
#include <set>

class IFeature;

// DLSSD running the game's ray reconstruction, NVIDIA's or a custom one from Config::CustomRRUpscaler
class Nvngx_RR
{
  private:
    struct NoCaseLess
    {
        bool operator()(const std::string& a, const std::string& b) const { return _stricmp(a.c_str(), b.c_str()) < 0; }
    };

    // std::map so the pointers handed out stay valid
    static inline std::map<std::string, std::unique_ptr<Nvngx_RRDllProxy>, NoCaseLess> _modules;
    static inline std::set<std::string, NoCaseLess> _failed;
    static inline std::mutex _modulesMutex;

    // No DLSSD could start, FSR takes it until another pick
    static inline std::atomic<bool> _fallback = false;

    // Never erased, the pointers stay valid without the lock
    static std::vector<Nvngx_RRDllProxy*> LoadedModules();

  public:
    // Regular upscaler picked for ray reconstruction, none unless Config::RRAllowRegularUpscalers is on
    static std::optional<Upscaler> Regular();

    // The picked regular upscaler, else DLSSD, or FSR when none could start
    static Upscaler Backend(API api);

    static bool NativeAvailable();

    // NVIDIA's or a custom one could run, without loading any
    static bool DlssdAvailable() { return NativeAvailable() || !CustomProviders::Scan().dlssd.empty(); }

    // Opti reports ray reconstruction to the game: a custom DLSSD runs, or no DLSSD can and a regular upscaler
    // is picked for it. NVIDIA's reports itself. Without load only an already loaded custom one counts
    static bool Offered(API api, bool load = true);
    static bool OfferedAny() { return Offered(API::DX12) || Offered(API::Vulkan); }

    // Loaded once, nullptr when it can't run on the api or failed. Without load only an already loaded one is
    // found, parameter maps are made in places the dll can't be loaded from
    static Nvngx_RRDllProxy* Find(const std::string& name, API api, bool load = true);

    // Custom DLSSD running ray reconstruction, empty for NVIDIA's. The picked one, else the first one when
    // NVIDIA's can't run
    static std::string SelectedName(API api, bool load = true);
    static Nvngx_RRDllProxy* Selected(API api, bool load = true);

    // For the menu, without loading it, a failed one can be picked again
    static bool Listable(const std::string& name, API api);

    // The failed feature's custom one isn't used again, NVIDIA's failing turns on the FSR fallback, a failed regular
    // upscaler is unpicked. False for the FSR fallback, it isn't retried
    static bool InitFailed(IFeature* feature);
    static void Fallback() { _fallback = true; }
    static void ResetFailures();

    static void PopulateParameters(NVSDK_NGX_Parameter* InParameters, API api);
    static NVSDK_NGX_Result ReportSupported(NVSDK_NGX_FeatureRequirement* OutSupported);

    static void ShutdownDx12(ID3D12Device* InDevice);
    static void ShutdownVulkan(VkDevice InDevice);
};
