#pragma once

#include "SysUtils.h"

#include <proxies/KernelBase_Proxy.h>

#include <cwctype> // for std::towlower

#define DEFINE_NAME_VECTORS(varName, ...)                                                                              \
    inline std::vector<std::string> varName##Names = []                                                                \
    {                                                                                                                  \
        std::vector<std::string> v;                                                                                    \
        const char* libs[] = { __VA_ARGS__ };                                                                          \
        for (auto lib : libs)                                                                                          \
        {                                                                                                              \
            v.emplace_back(std::string(lib) + ".dll");                                                                 \
            v.emplace_back(std::string(lib));                                                                          \
        }                                                                                                              \
        return v;                                                                                                      \
    }();                                                                                                               \
    inline std::vector<std::wstring> varName##NamesW = []                                                              \
    {                                                                                                                  \
        std::vector<std::wstring> v;                                                                                   \
        const char* libs[] = { __VA_ARGS__ };                                                                          \
        for (auto lib : libs)                                                                                          \
        {                                                                                                              \
            std::string narrow(lib);                                                                                   \
            std::wstring wide(narrow.begin(), narrow.end());                                                           \
            v.emplace_back(wide + L".dll");                                                                            \
            v.emplace_back(wide);                                                                                      \
        }                                                                                                              \
        return v;                                                                                                      \
    }();

inline std::vector<std::string> dllNames;
inline std::vector<std::wstring> dllNamesW;

//"rtsshooks64.dll", "rtsshooks64", "rtsshooks.dll", "rtsshooks",

// clang-format off
DEFINE_NAME_VECTORS(overlay, "eosovh-win32-shipping",
                             "eosovh-win64-shipping",   // Epic
                             "gameoverlayrenderer",
                             "gameoverlayrenderer64",     // Steam
                             "socialclubd3d12renderer", // Rockstar
                             "owutils",                 // Overwolf
                             "owclient",
                             "galaxy",
                             "galaxy64",                // GOG Galaxy
                             "discordhook",
                             "discordhook64",
                             "discordoverlay",
                             "discordoverlay64",        // Discord
                             "overlay",
                             "overlay64"                  // Ubisoft
);

DEFINE_NAME_VECTORS(blockOverlay, "eosovh-win32-shipping",
                                  "eosovh-win64-shipping",
                                  "gameoverlayrenderer",
                                  "gameoverlayrenderer64",
                                  "owclient",
                                  "galaxy",
                                  "galaxy64",
                                  "discordhook",
                                  "discordhook64",
                                  "discordoverlay",
                                  "discordoverlay64",
                                  "overlay",
                                  "overlay64"
);

inline std::vector<std::wstring> blockedDllNamesW = { L"windhawk.dll", L"mactype.dll", L"mactype64.dll" };

DEFINE_NAME_VECTORS(skipDxgiWrapping, "eosovh-win32-shipping",
                                      "eosovh-win64-shipping",
                                      "gameoverlayrenderer",
                                      "gameoverlayrenderer64",
                                      "socialclubd3d12renderer",
                                      "owclient",
                                      "owutils",
                                      "galaxy",
                                      "galaxy64",
                                      "discordhook",
                                      "discordhook64",
                                      "discordoverlay",
                                      "discordoverlay64",
                                      "overlay",
                                      "overlay64", // Overlays ended
                                      "d3d11",
                                      "d3d12",
                                      "d3d12core" // DirectX ended
/*
                                      "libxell.dll",
                                      "libxess.dll",
                                      "libxess_dx11.dll",
                                      "igxess.dll",
                                      "igxess2.dll",
                                      "libxess_fg.dll",
                                      "igxess_fg.dll", // xess ended
                                      "intelcontrollib.dll",
                                      "igdext64.dll",
                                      "igdgmm64.dll", // intel drivers ended
                                      "ffx_fsr2_api_x64.dll",
                                      "ffx_fsr2_api_dx12_x64.dll",
                                      "ffx_fsr3upscaler_x64.dll",
                                      "ffx_backend_dx12_x64.dll",
                                      "amd_fidelityfx_dx12.dll",
                                      "amd_fidelityfx_loader_dx12.dll",
                                      "amd_fidelityfx_upscaler_dx12.dll",
                                      "amd_fidelityfx_framegeneration_dx12.dll", // fsr ended
                                      "nvcamera64.dll",                          // nvcamera?
*/
);
// clang-format on

DEFINE_NAME_VECTORS(eosOverlay, "eosovh-win32-shipping", "eosovh-win64-shipping");

DEFINE_NAME_VECTORS(dx11, "d3d11");
DEFINE_NAME_VECTORS(dx12, "d3d12");
DEFINE_NAME_VECTORS(dx12agility, "d3d12core");
DEFINE_NAME_VECTORS(dxgi, "dxgi");
DEFINE_NAME_VECTORS(vk, "vulkan-1");

DEFINE_NAME_VECTORS(nvngx, "nvngx", "_nvngx");
DEFINE_NAME_VECTORS(nvngxDlss, "nvngx_dlss");
DEFINE_NAME_VECTORS(nvapi, "nvapi64");
DEFINE_NAME_VECTORS(slInterposer, "sl.interposer");
DEFINE_NAME_VECTORS(slDlss, "sl.dlss");
DEFINE_NAME_VECTORS(slDlssg, "sl.dlss_g");
DEFINE_NAME_VECTORS(slReflex, "sl.reflex");
DEFINE_NAME_VECTORS(slPcl, "sl.pcl");
DEFINE_NAME_VECTORS(slCommon, "sl.common");

DEFINE_NAME_VECTORS(xess, "libxess");
DEFINE_NAME_VECTORS(xessDx11, "libxess_dx11");

DEFINE_NAME_VECTORS(fsr2, "ffx_fsr2_api_x64");
DEFINE_NAME_VECTORS(fsr2BE, "ffx_fsr2_api_dx12_x64");

DEFINE_NAME_VECTORS(fsr3, "ffx_fsr3upscaler_x64");
DEFINE_NAME_VECTORS(fsr3BE, "ffx_backend_dx12_x64");

DEFINE_NAME_VECTORS(amdxc64, "amdxc64");
DEFINE_NAME_VECTORS(ffxDx12, "amd_fidelityfx_dx12", "amd_fidelityfx_loader_dx12");
DEFINE_NAME_VECTORS(ffxDx12Upscaler, "amd_fidelityfx_upscaler_dx12");
DEFINE_NAME_VECTORS(ffxDx12FG, "amd_fidelityfx_framegeneration_dx12");
DEFINE_NAME_VECTORS(ffxDx12Denoiser, "amd_fidelityfx_denoiser_dx12");
DEFINE_NAME_VECTORS(ffxDx12Radiance, "amd_fidelityfx_radiancecache_dx12");
DEFINE_NAME_VECTORS(ffxVk, "amd_fidelityfx_upscaler_vk", "amd_fidelityfx_vk");
DEFINE_NAME_VECTORS(uell, "main");

inline static bool CompareFileName(std::string* first, std::string* second)
{
    if (first->size() < second->size())
        return false;

    auto start = first->size() - second->size();

    bool match = true;
    for (size_t j = 0; j < second->size(); ++j)
    {
        if (std::tolower(static_cast<unsigned char>((*first)[start + j])) !=
            std::tolower(static_cast<unsigned char>((*second)[j])))
        {
            return false;
        }
    }

    return true;
}

inline static bool CompareFileNameW(std::wstring* first, std::wstring* second)
{
    if (first->size() < second->size())
        return false;

    auto start = first->size() - second->size();

    bool match = true;
    for (size_t j = 0; j < second->size(); ++j)
    {
        if (std::towlower((*first)[start + j]) != std::towlower((*second)[j]))
        {
            return false;
        }
    }

    return true;
}

inline static bool CheckDllName(std::string* dllName, std::vector<std::string>* namesList)
{
    for (auto& name : *namesList)
    {
        if (CompareFileName(dllName, &name))
            return true;
    }

    return false;
}

inline static bool CheckDllNameW(std::wstring* dllName, std::vector<std::wstring>* namesList)
{
    for (auto& name : *namesList)
    {
        if (CompareFileNameW(dllName, &name))
            return true;
    }

    return false;
}

inline static HMODULE GetDllNameModule(std::vector<std::string>* namesList)
{
    for (size_t i = 0; i < namesList->size(); i++)
    {
        auto name = namesList->at(i);
        auto module = KernelBaseProxy::GetModuleHandleA_()(name.c_str());

        if (module != nullptr)
            return module;
    }

    return nullptr;
}

inline static HMODULE GetDllNameWModule(std::vector<std::wstring>* namesList)
{
    for (size_t i = 0; i < namesList->size(); i++)
    {
        auto name = namesList->at(i);
        auto module = KernelBaseProxy::GetModuleHandleW_()(name.c_str());

        if (module != nullptr)
            return module;
    }

    return nullptr;
}
