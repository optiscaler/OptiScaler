#pragma once

#include "Config.h"

#include <plugins/PluginApi.h>
#include <proxies/KernelBase_Proxy.h>
#include <proxies/Ntdll_Proxy.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

// Dlls in custom/<name>/<kind> of the Opti folder, each kind a folder has is an entry in the upscaler or FG list:
//   custom/<name>/ffx      DX12 FFX upscaler
//   custom/<name>/ffx_vk   Vulkan FFX upscaler
//   custom/<name>/dlss     NGX runtime that runs DLSS
//   custom/<name>/dlssg    nvngx_dlssg replacement, an FG Nvngx entry
// The dll can have any name. When the folder has more dlls, like ones the dll loads itself, the standard name
// is used (amd_fidelityfx_upscaler_dx12.dll, amd_fidelityfx_upscaler_vk.dll then amd_fidelityfx_vk.dll, nvngx.dll,
// nvngx_dlssg.dll), else the first one. A Vulkan dll with another name is taken as amd_fidelityfx_upscaler_vk.dll.
// For dlssg the first dll with NGX's entry points comes before the first one (DlssgDllPath).
namespace CustomProviders
{
// Whether the dll file exports one of `names`. Read from the file's export table: nothing is loaded or run.
inline bool FileExportsAny(const std::filesystem::path& path, std::initializer_list<std::string_view> names)
{
    std::ifstream file(path, std::ios::binary);

    if (!file)
        return false;

    const std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto at = [&data](size_t offset, size_t size) -> const char*
    { return offset <= data.size() && size <= data.size() - offset ? data.data() + offset : nullptr; };

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(at(0, sizeof(IMAGE_DOS_HEADER)));

    if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
        return false;

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(at(size_t(dos->e_lfanew), sizeof(IMAGE_NT_HEADERS64)));

    if (nt == nullptr || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return false;

    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];

    if (directory.VirtualAddress == 0 || directory.Size == 0)
        return false;

    const WORD sectionCount = nt->FileHeader.NumberOfSections;
    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        at(size_t(dos->e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader,
           sectionCount * sizeof(IMAGE_SECTION_HEADER)));

    if (sections == nullptr)
        return false;

    const auto offsetOf = [&](DWORD rva) -> size_t
    {
        for (WORD i = 0; i < sectionCount; i++)
        {
            const auto& section = sections[i];
            const DWORD size = std::max(section.Misc.VirtualSize, section.SizeOfRawData);

            if (rva >= section.VirtualAddress && rva - section.VirtualAddress < size)
                return size_t(rva - section.VirtualAddress) + section.PointerToRawData;
        }

        return SIZE_MAX;
    };

    const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
        at(offsetOf(directory.VirtualAddress), sizeof(IMAGE_EXPORT_DIRECTORY)));

    if (exports == nullptr)
        return false;

    const auto* nameRvas = reinterpret_cast<const DWORD*>(
        at(offsetOf(exports->AddressOfNames), size_t(exports->NumberOfNames) * sizeof(DWORD)));

    if (nameRvas == nullptr)
        return false;

    for (DWORD i = 0; i < exports->NumberOfNames; i++)
    {
        const size_t offset = offsetOf(nameRvas[i]);
        const char* begin = at(offset, 1);

        if (begin == nullptr)
            continue;

        const std::string_view exported(begin, strnlen(begin, data.size() - offset));

        if (std::ranges::find(names, exported) != names.end())
            return true;
    }

    return false;
}

inline std::filesystem::path Root()
{
    return std::filesystem::path(Config::Instance()->MainDllPath.value()) / L"custom";
}

inline std::filesystem::path Folder(const std::string& name) { return Root() / string_to_wstring(name); }

// The dll in custom/<name>/<kind>, empty when there's none
inline std::filesystem::path DllPath(const std::string& name, const wchar_t* kind,
                                     std::initializer_list<const wchar_t*> standardNames)
{
    auto folder = Folder(name) / kind;
    std::error_code error;

    for (auto standardName : standardNames)
    {
        if (std::filesystem::exists(folder / standardName, error))
            return folder / standardName;
    }

    for (auto& entry : std::filesystem::directory_iterator(folder, error))
    {
        if (_wcsicmp(entry.path().extension().c_str(), L".dll") == 0)
            return entry.path();
    }

    return {};
}

inline std::filesystem::path FfxDllPath(const std::string& name)
{
    return DllPath(name, L"ffx", { L"amd_fidelityfx_upscaler_dx12.dll" });
}

inline std::filesystem::path FfxVkDllPath(const std::string& name)
{
    return DllPath(name, L"ffx_vk", { L"amd_fidelityfx_upscaler_vk.dll", L"amd_fidelityfx_vk.dll" });
}

// The legacy FFX SDK 1.x dll takes FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK, the rest the V2 backend type
inline bool IsLegacyFfxVkDll(const std::filesystem::path& path)
{
    return _wcsicmp(path.filename().c_str(), L"amd_fidelityfx_vk.dll") == 0;
}

inline std::filesystem::path NgxDllPath(const std::string& name) { return DllPath(name, L"dlss", { L"nvngx.dll" }); }

// nvngx_dlssg.dll, else the first dll with NGX's entry points, else the first dll. A provider's own dependencies can
// sit beside it and come first in the folder (such as XeSS's libxell.dll and libxess_fg.dll): taken as the provider,
// they made it unavailable.
inline std::filesystem::path DlssgDllPath(const std::string& name)
{
    auto folder = Folder(name) / L"dlssg";
    std::error_code error;

    if (std::filesystem::exists(folder / L"nvngx_dlssg.dll", error))
        return folder / L"nvngx_dlssg.dll";

    std::filesystem::path first;

    for (auto& entry : std::filesystem::directory_iterator(folder, error))
    {
        if (_wcsicmp(entry.path().extension().c_str(), L".dll") != 0)
            continue;

        if (FileExportsAny(entry.path(), { "NVSDK_NGX_D3D12_Init", "NVSDK_NGX_VULKAN_Init" }))
            return entry.path();

        if (first.empty())
            first = entry.path();
    }

    return first;
}

// nullptr when the path is empty or the dll can't be loaded
inline HMODULE LoadDll(const std::string& name, const std::filesystem::path& path)
{
    auto dll = path.empty() ? nullptr : NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, NULL);

    if (dll == nullptr)
    {
        if (path.empty())
        {
            LOG_ERROR("Can't load custom {}: no dll in {}", name, wstring_to_string(Folder(name).wstring()));
            return nullptr;
        }

        // ERROR_DLL_INIT_FAILED: the dll refused to load, e.g. a provider that needs another module loaded first
        const DWORD error = GetLastError();
        LOG_ERROR("Can't load custom {} from {}, error {}{}", name, wstring_to_string(path.wstring()), error,
                  error == ERROR_DLL_INIT_FAILED ? " (the dll refused to load; it needs something this process lacks)"
                                                 : "");
        return nullptr;
    }

    LOG_INFO("Loaded custom {} from {}", name, wstring_to_string(path.wstring()));

    // Folder name is the plugin name, the dlls themselves mostly have the standard names
    PluginApi::InitPlugin(dll, (PFN_OptiPluginInit) KernelBaseProxy::GetProcAddress_()(dll, OPTI_PLUGIN_INIT_NAME),
                          name);

    return dll;
}

struct Names
{
    std::vector<std::string> ffx;
    std::vector<std::string> ffxVk;
    std::vector<std::string> dlss;
    std::vector<std::string> dlssg;
};

// Scanned once
inline const Names& Scan()
{
    static const auto names = []
    {
        Names found;
        std::error_code error;

        for (auto& entry : std::filesystem::directory_iterator(Root(), error))
        {
            auto name = wstring_to_string(entry.path().filename().wstring());

            if (!FfxDllPath(name).empty())
                found.ffx.push_back(name);

            if (!FfxVkDllPath(name).empty())
                found.ffxVk.push_back(name);

            if (!NgxDllPath(name).empty())
                found.dlss.push_back(name);

            if (!DlssgDllPath(name).empty())
                found.dlssg.push_back(name);
        }

        return found;
    }();

    return names;
}

// Folder names aren't case sensitive
inline bool Contains(const std::vector<std::string>& names, const std::string& name)
{
    return std::ranges::any_of(names, [&name](const std::string& n) { return _stricmp(n.c_str(), name.c_str()) == 0; });
}

inline bool IsFfx(const std::string& name) { return Contains(Scan().ffx, name); }
inline bool IsFfxVk(const std::string& name) { return Contains(Scan().ffxVk, name); }
inline bool IsDlss(const std::string& name) { return Contains(Scan().dlss, name); }
inline bool IsDlssg(const std::string& name) { return Contains(Scan().dlssg, name); }

// Dx12Upscaler from the ini as a custom folder's kind, a folder with both is used as FFX
inline void ResolveDx12Upscaler()
{
    auto cfg = Config::Instance();
    auto name = cfg->CustomUpscaler.value_or("");

    if (name.empty())
        return;

    if (IsFfx(name))
    {
        cfg->Dx12Upscaler.set_from_config(Upscaler::FFX);
    }
    else if (IsDlss(name))
    {
        cfg->Dx12Upscaler.set_from_config(Upscaler::DLSS);
    }
    else
    {
        LOG_WARN("Dx12Upscaler={} isn't an upscaler or a folder in {}", name, wstring_to_string(Root().wstring()));
        cfg->CustomUpscaler.reset();
    }
}

// VulkanUpscaler from the ini as a custom folder, only FFX ones run on Vulkan
inline void ResolveVulkanUpscaler()
{
    auto cfg = Config::Instance();
    auto name = cfg->CustomUpscalerVk.value_or("");

    if (name.empty())
        return;

    if (IsFfxVk(name))
    {
        cfg->VulkanUpscaler.set_from_config(Upscaler::FFX);
    }
    else
    {
        LOG_WARN("VulkanUpscaler={} isn't an upscaler or a folder with ffx_vk in {}", name,
                 wstring_to_string(Root().wstring()));
        cfg->CustomUpscalerVk.reset();
    }
}

// FGNvngxReplacement from the ini as a custom folder with dlssg
inline void ResolveFGNvngx()
{
    auto& name = State::Instance().fgNvngxCustom;

    if (name.empty())
        return;

    if (!IsDlssg(name))
    {
        LOG_WARN("FGNvngxReplacement={} isn't a replacement or a folder with dlssg in {}", name,
                 wstring_to_string(Root().wstring()));
        name.clear();
        Config::Instance()->FGNvngxReplacement.reset();
    }
}
} // namespace CustomProviders
