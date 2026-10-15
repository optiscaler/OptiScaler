#pragma once

#include "SysUtils.h"

#include "../../plugin_sdk/optiscaler_plugin.h"

#include <imgui/imgui.h>

// OptiScaler side of plugin_sdk/optiscaler_plugin.h
class PluginApi
{
  public:
    struct TabInfo
    {
        std::string name;
        std::string description;
    };

    // Calls OptiPluginInit of a loaded ASI plugin, once per module
    static void InitPlugin(HMODULE module, PFN_OptiPluginInit init, const std::filesystem::path& path);

    static std::vector<TabInfo> GetTabs();

    // Draws a registered tab inside of the menu, colorMap adjusts colors for the HDR menu
    static void DrawTab(size_t index, float menuScale, ImVec4 (*colorMap)(const ImVec4&));
};
