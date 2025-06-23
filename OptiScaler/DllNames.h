#pragma once

#include <pch.h>

#define DEFINE_NAME_VECTORS(varName, libName)                                                                          \
    inline std::vector<std::string> varName##Names = { libName ".dll", libName };                                      \
    inline std::vector<std::wstring> varName##NamesW = { L##libName L".dll", L##libName };

inline std::vector<std::string> dllNames;
inline std::vector<std::wstring> dllNamesW;

//"rtsshooks64.dll", "rtsshooks64", "rtsshooks.dll", "rtsshooks",

inline std::vector<std::string> overlayNames = { "eosovh-win32-shipping.dll",
                                                 "eosovh-win32-shipping",
                                                 "eosovh-win64-shipping.dll",
                                                 "eosovh-win64-shipping", // Epic
                                                 "gameoverlayrenderer64",
                                                 "gameoverlayrenderer64.dll",
                                                 "gameoverlayrenderer",
                                                 "gameoverlayrenderer.dll", // Steam
                                                 "socialclubd3d12renderer",
                                                 "socialclubd3d12renderer.dll", // Rockstar
                                                 "owutils.dll",
                                                 "owutils", // Overwolf
                                                 "galaxy.dll",
                                                 "galaxy",
                                                 "galaxy64.dll",
                                                 "galaxy64", // GOG Galaxy
                                                 "discordoverlay.dll",
                                                 "discordoverlay",
                                                 "discordoverlay64.dll",
                                                 "discordoverlay64", // Discord
                                                 "overlay64",
                                                 "overlay64.dll",
                                                 "overlay",
                                                 "overlay.dll" }; // Ubisoft

inline std::vector<std::wstring> overlayNamesW = { L"eosovh-win32-shipping.dll",
                                                   L"eosovh-win32-shipping",
                                                   L"eosovh-win64-shipping.dll",
                                                   L"eosovh-win64-shipping",
                                                   L"gameoverlayrenderer64",
                                                   L"gameoverlayrenderer64.dll",
                                                   L"gameoverlayrenderer",
                                                   L"gameoverlayrenderer.dll",
                                                   L"socialclubd3d12renderer",
                                                   L"socialclubd3d12renderer.dll",
                                                   L"owutils.dll",
                                                   L"owutils",
                                                   L"galaxy.dll",
                                                   L"galaxy",
                                                   L"galaxy64.dll",
                                                   L"galaxy64",
                                                   L"discordoverlay.dll",
                                                   L"discordoverlay",
                                                   L"discordoverlay64.dll",
                                                   L"discordoverlay64",
                                                   L"overlay64",
                                                   L"overlay64.dll",
                                                   L"overlay",
                                                   L"overlay.dll" };

inline std::vector<std::string> blockOverlayNames = { "eosovh-win32-shipping.dll",
                                                      "eosovh-win32-shipping",
                                                      "eosovh-win64-shipping.dll",
                                                      "eosovh-win64-shipping",
                                                      "gameoverlayrenderer64",
                                                      "gameoverlayrenderer64.dll",
                                                      "gameoverlayrenderer",
                                                      "gameoverlayrenderer.dll",
                                                      "owclient.dll",
                                                      "owclient"
                                                      "galaxy.dll",
                                                      "galaxy",
                                                      "galaxy64.dll",
                                                      "galaxy64",
                                                      "discordoverlay.dll",
                                                      "discordoverlay",
                                                      "discordoverlay64.dll",
                                                      "discordoverlay64",
                                                      "overlay64",
                                                      "overlay64.dll",
                                                      "overlay",
                                                      "overlay.dll" };

inline std::vector<std::wstring> blockOverlayNamesW = { L"eosovh-win32-shipping.dll",
                                                        L"eosovh-win32-shipping",
                                                        L"eosovh-win64-shipping.dll",
                                                        L"eosovh-win64-shipping",
                                                        L"gameoverlayrenderer64",
                                                        L"gameoverlayrenderer64.dll",
                                                        L"gameoverlayrenderer",
                                                        L"gameoverlayrenderer.dll",
                                                        L"owclient.dll",
                                                        L"owclient",
                                                        L"galaxy.dll",
                                                        L"galaxy",
                                                        L"galaxy64.dll",
                                                        L"galaxy64",
                                                        L"discordoverlay.dll",
                                                        L"discordoverlay",
                                                        L"discordoverlay64.dll",
                                                        L"discordoverlay64",
                                                        L"overlay64",
                                                        L"overlay64.dll",
                                                        L"overlay",
                                                        L"overlay.dll" };

DEFINE_NAME_VECTORS(dx11, "d3d11");
DEFINE_NAME_VECTORS(dx12, "d3d12");
DEFINE_NAME_VECTORS(dxgi, "dxgi");
DEFINE_NAME_VECTORS(vk, "vulkan-1");

DEFINE_NAME_VECTORS(nvngx, "nvngx");
DEFINE_NAME_VECTORS(nvngxDlss, "nvngx_dlss");
DEFINE_NAME_VECTORS(nvapi, "nvapi64");
DEFINE_NAME_VECTORS(slInterposer, "sl.interposer");
DEFINE_NAME_VECTORS(slDlss, "sl.dlss");
DEFINE_NAME_VECTORS(slDlssg, "sl.dlss_g");
DEFINE_NAME_VECTORS(slReflex, "sl.reflex");
DEFINE_NAME_VECTORS(slCommon, "sl.common");

DEFINE_NAME_VECTORS(xess, "libxess");
DEFINE_NAME_VECTORS(xessDx11, "libxess_dx11");

DEFINE_NAME_VECTORS(fsr2, "ffx_fsr2_api_x64");
DEFINE_NAME_VECTORS(fsr2BE, "ffx_fsr2_api_dx12_x64");

DEFINE_NAME_VECTORS(fsr3, "ffx_fsr3upscaler_x64");
DEFINE_NAME_VECTORS(fsr3BE, "ffx_backend_dx12_x64");

DEFINE_NAME_VECTORS(ffxDx12, "amd_fidelityfx_dx12");
DEFINE_NAME_VECTORS(ffxVk, "amd_fidelityfx_vk");

inline static bool CheckDllName(std::string* dllName, std::vector<std::string>* namesList)
{
    for (size_t i = 0; i < namesList->size(); i++)
    {
        auto name = namesList->at(i);
        auto pos = dllName->rfind(name);

        if (pos != std::string::npos && pos == (dllName->size() - name.size()))
            return true;
    }

    return false;
}

inline static bool CheckDllNameW(std::wstring* dllName, std::vector<std::wstring>* namesList)
{
    for (size_t i = 0; i < namesList->size(); i++)
    {
        auto name = namesList->at(i);
        auto pos = dllName->rfind(name);

        if (pos != std::string::npos && pos == (dllName->size() - name.size()))
            return true;
    }

    return false;
}
