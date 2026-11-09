#pragma once

#include "Config.h"

#include <plugins/PluginApi.h>
#include <proxies/KernelBase_Proxy.h>
#include <proxies/Ntdll_Proxy.h>

#include <filesystem>

// Custom providers are dlls in custom/<name>/<kind> of the Opti folder, or ones an ASI plugin registered with
// RegisterProvider. Each kind a name has is an entry in the upscaler or FG lists:
//   custom/<name>/ffx        DX12 FFX upscaler, on DX11 and Vulkan through DX12 interop
//   custom/<name>/ffx_vk     Vulkan FFX upscaler
//   custom/<name>/ffx_fg     DX12 FFX frame generation and its swapchain, an FSR FG provider
//   custom/<name>/ffx_fg_vk  Vulkan FFX frame generation and its swapchain, an FSR FG provider
//   custom/<name>/dlss       NGX runtime that runs DLSS on DX12 and Vulkan, on DX11 through DX12 interop
//   custom/<name>/dlssg      nvngx_dlssg replacement, an FG Nvngx entry
//   custom/<name>/dlssd      nvngx_dlssd replacement, a ray reconstruction (RRUpscaler) entry
// The dll can have any name. When the folder has more dlls, like ones the dll loads itself, the standard name of the
// kind is used (amd_fidelityfx_upscaler_dx12.dll, nvngx.dll...), else the first one. A Vulkan FFX dll other than
// amd_fidelityfx_vk.dll takes the V2 backend type of FFX SDK 2.x.
namespace CustomProviders
{
// Same values as OptiProviderKind
enum class Kind : uint32_t
{
    Ffx,
    FfxVk,
    FfxFg,
    FfxFgVk,
    Dlss,
    Dlssg,
    Dlssd,
    Count
};

static_assert((uint32_t) Kind::Dlssd == OptiProvider_Dlssd);

struct KindInfo
{
    const wchar_t* folder;
    // Fixed size, an initializer_list in an inline function's static differs between translation units
    const wchar_t* standardNames[2];
};

inline const KindInfo& Info(Kind kind)
{
    static const KindInfo infos[] = {
        { L"ffx", { L"amd_fidelityfx_upscaler_dx12.dll" } },
        { L"ffx_vk", { L"amd_fidelityfx_upscaler_vk.dll", L"amd_fidelityfx_vk.dll" } },
        { L"ffx_fg", { L"amd_fidelityfx_framegeneration_dx12.dll", L"amd_fidelityfx_dx12.dll" } },
        { L"ffx_fg_vk", { L"amd_fidelityfx_framegeneration_vk.dll", L"amd_fidelityfx_vk.dll" } },
        { L"dlss", { L"nvngx.dll" } },
        { L"dlssg", { L"nvngx_dlssg.dll" } },
        { L"dlssd", { L"nvngx_dlssd.dll" } },
    };

    return infos[(size_t) kind];
}

inline std::filesystem::path Root()
{
    return std::filesystem::path(Config::Instance()->MainDllPath.value()) / L"custom";
}

inline std::filesystem::path Folder(const std::string& name) { return Root() / string_to_wstring(name); }

// The dll in custom/<name>/<kind>, empty when there's none
inline std::filesystem::path DllPath(const std::string& name, Kind kind)
{
    auto folder = Folder(name) / Info(kind).folder;
    std::error_code error;

    for (auto standardName : Info(kind).standardNames)
    {
        if (standardName != nullptr && std::filesystem::exists(folder / standardName, error))
            return folder / standardName;
    }

    for (auto& entry : std::filesystem::directory_iterator(folder, error))
    {
        if (_wcsicmp(entry.path().extension().c_str(), L".dll") == 0)
            return entry.path();
    }

    return {};
}

// Where the exports of a custom provider come from, a dll from its folder or a plugin's resolver
struct Exports
{
    HMODULE dll = nullptr;
    std::filesystem::path path; // Empty for a plugin
    OptiGetProcFn getProc = nullptr;
    void* userData = nullptr;

    void* operator()(const char* exportName) const
    {
        if (getProc != nullptr)
            return getProc(userData, exportName);

        return (void*) KernelBaseProxy::GetProcAddress_()(dll, exportName);
    }

    explicit operator bool() const { return dll != nullptr || getProc != nullptr; }

    // The legacy FFX SDK 1.x dll takes FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK, the rest the V2 backend type
    bool IsLegacyFfxVk() const { return _wcsicmp(path.filename().c_str(), L"amd_fidelityfx_vk.dll") == 0; }
};

struct PluginProvider
{
    std::string name;
    Kind kind;
    OptiGetProcFn getProc;
    void* userData;
};

inline std::mutex pluginMutex;
inline std::vector<PluginProvider> pluginProviders;

// Folder names aren't case sensitive
inline bool Contains(const std::vector<std::string>& names, const std::string& name)
{
    return std::ranges::any_of(names, [&name](const std::string& n) { return _stricmp(n.c_str(), name.c_str()) == 0; });
}

// Names with that kind, the folders are scanned once and the plugin ones are added as they register
inline std::vector<std::string> List(Kind kind)
{
    static const auto folders = []
    {
        std::array<std::vector<std::string>, (size_t) Kind::Count> found;
        std::error_code error;

        for (auto& entry : std::filesystem::directory_iterator(Root(), error))
        {
            auto name = wstring_to_string(entry.path().filename().wstring());

            for (size_t k = 0; k < found.size(); k++)
            {
                if (!DllPath(name, (Kind) k).empty())
                    found[k].push_back(name);
            }
        }

        return found;
    }();

    auto names = folders[(size_t) kind];

    std::scoped_lock lock(pluginMutex);

    for (auto& provider : pluginProviders)
    {
        if (provider.kind == kind && !Contains(names, provider.name))
            names.push_back(provider.name);
    }

    return names;
}

inline bool Has(const std::string& name, Kind kind) { return !name.empty() && Contains(List(kind), name); }

// Exports of the name's kind, a plugin's first, then the folder's dll. Empty when there are none.
inline Exports Load(const std::string& name, Kind kind)
{
    {
        std::scoped_lock lock(pluginMutex);

        for (auto& provider : pluginProviders)
        {
            if (provider.kind == kind && _stricmp(provider.name.c_str(), name.c_str()) == 0)
            {
                LOG_INFO("Using custom {} from a plugin", name);
                return { .getProc = provider.getProc, .userData = provider.userData };
            }
        }
    }

    auto path = DllPath(name, kind);
    auto dll = path.empty() ? nullptr : NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, NULL);

    if (dll == nullptr)
    {
        LOG_ERROR("Can't load custom {} from {}", name, wstring_to_string(Folder(name).wstring()));
        return {};
    }

    LOG_INFO("Loaded custom {} from {}", name, wstring_to_string(path.wstring()));

    // Folder name is the plugin name, the dlls themselves mostly have the standard names
    PluginApi::InitPlugin(dll, (PFN_OptiPluginInit) KernelBaseProxy::GetProcAddress_()(dll, OPTI_PLUGIN_INIT_NAME),
                          name);

    return { .dll = dll, .path = path };
}

inline CustomOptional<std::string, NoDefault>& NameSetting(API api)
{
    auto cfg = Config::Instance();
    return api == API::DX11     ? cfg->CustomUpscalerDx11
           : api == API::Vulkan ? cfg->CustomUpscalerVk
                                : cfg->CustomUpscaler;
}

inline CustomOptional<Upscaler, SoftDefault>& UpscalerSetting(API api)
{
    auto cfg = Config::Instance();
    return api == API::DX11 ? cfg->Dx11Upscaler : api == API::Vulkan ? cfg->VulkanUpscaler : cfg->Dx12Upscaler;
}

// Upscalers a custom name runs as on the API, the native one first
inline std::vector<Upscaler> Upscalers(const std::string& name, API api)
{
    std::vector<Upscaler> upscalers;

    auto add = [&](Upscaler upscaler, Kind kind)
    {
        if (Has(name, kind))
            upscalers.push_back(upscaler);
    };

    if (api == API::DX11)
    {
        add(Upscaler::FFX_on12, Kind::Ffx);
        add(Upscaler::DLSS_on12, Kind::Dlss);
    }
    else if (api == API::Vulkan)
    {
        add(Upscaler::FFX, Kind::FfxVk);
        add(Upscaler::FFX_on12, Kind::Ffx);
        add(Upscaler::DLSS, Kind::Dlss);
    }
    else
    {
        add(Upscaler::FFX, Kind::Ffx);
        add(Upscaler::DLSS, Kind::Dlss);
    }

    return upscalers;
}

inline bool Runs(const std::string& name, API api, Upscaler upscaler)
{
    return std::ranges::contains(Upscalers(name, api), upscaler);
}

// Custom name picked for the API when it runs as that upscaler, empty for the regular ones
inline std::string Selected(API api, Upscaler upscaler)
{
    auto name = NameSetting(api).value_or("");
    return Runs(name, api, upscaler) ? name : "";
}

// Custom name from the API's upscaler setting in the ini as the upscaler it runs as.
// It's kept while a plugin can still register it.
inline void ResolveUpscaler(API api)
{
    auto& setting = NameSetting(api);
    auto name = setting.value_or("");

    if (name.empty())
        return;

    if (auto upscalers = Upscalers(name, api); !upscalers.empty())
    {
        UpscalerSetting(api).set_from_config(upscalers[0]);
    }
    else if (!Config::Instance()->LoadAsiPlugins.value_or_default())
    {
        LOG_WARN("{} isn't an upscaler or a custom upscaler for API {} in {}", name, (int) api,
                 wstring_to_string(Root().wstring()));
        setting.reset();
    }
}

// Custom name of FGNvngxReplacement, FSRFGProvider or RRUpscaler from the ini, kept while a plugin can register it
inline void ResolveName(std::string& name, std::initializer_list<Kind> kinds, const char* key)
{
    if (name.empty() || std::ranges::any_of(kinds, [&name](Kind kind) { return Has(name, kind); }) ||
        Config::Instance()->LoadAsiPlugins.value_or_default())
    {
        return;
    }

    LOG_WARN("{}={} isn't a replacement or a custom provider in {}", key, name, wstring_to_string(Root().wstring()));
    name.clear();
}

inline void ResolveAll()
{
    for (auto api : { API::DX11, API::DX12, API::Vulkan })
        ResolveUpscaler(api);

    auto& state = State::Instance();
    ResolveName(state.fgNvngxCustom, { Kind::Dlssg }, "FGNvngxReplacement");

    if (state.fgNvngxCustom.empty() &&
        Config::Instance()->FGNvngxReplacement.value_or_default() == FGNvngxReplacement::Custom)
    {
        Config::Instance()->FGNvngxReplacement.reset();
    }

    auto fsrFg = Config::Instance()->FSRFGProvider.value_or("");
    ResolveName(fsrFg, { Kind::FfxFg, Kind::FfxFgVk }, "FSRFGProvider");

    if (fsrFg.empty())
        Config::Instance()->FSRFGProvider.reset();

    auto rr = Config::Instance()->CustomRRUpscaler.value_or("");
    ResolveName(rr, { Kind::Dlssd }, "RRUpscaler");

    if (rr.empty())
        Config::Instance()->CustomRRUpscaler.reset();
}

// From the plugin API, a plugin's name replaces a folder with the same name
inline bool Register(const std::string& name, Kind kind, OptiGetProcFn getProc, void* userData)
{
    if (name.empty() || kind >= Kind::Count || getProc == nullptr)
        return false;

    {
        std::scoped_lock lock(pluginMutex);

        auto same = [&](const PluginProvider& p)
        { return p.kind == kind && _stricmp(p.name.c_str(), name.c_str()) == 0; };

        if (std::ranges::any_of(pluginProviders, same))
        {
            LOG_WARN("Custom {} ({}) is already registered", name, wstring_to_string(Info(kind).folder));
            return false;
        }

        pluginProviders.push_back({ name, kind, getProc, userData });
    }

    LOG_INFO("Registered custom {} ({})", name, wstring_to_string(Info(kind).folder));

    // The ini can name it before it's registered
    for (auto api : { API::DX11, API::DX12, API::Vulkan })
    {
        if (_stricmp(NameSetting(api).value_or("").c_str(), name.c_str()) == 0)
            ResolveUpscaler(api);
    }

    return true;
}
} // namespace CustomProviders
