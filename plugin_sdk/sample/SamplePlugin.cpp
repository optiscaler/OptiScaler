// Sample OptiScaler plugin, uses every part of plugin_sdk/optiscaler_plugin.h
//
// Open SamplePlugin.vcxproj in Visual Studio, select Release | x64 and build it. Copy
// bin/Release/SamplePlugin.asi to the plugins folder next to OptiScaler and set LoadAsiPlugins=true
// in OptiScaler.ini. Two tabs show up under PLUGINS in the menu and
// the settings are kept in the [Plugin.SamplePlugin] section of OptiScaler.ini.

#include <Windows.h>

#include <cstdio>

#include "../optiscaler_plugin.h"

namespace
{
const OptiPluginApi* api = nullptr;
OptiPlugin* self = nullptr;

const char* const qualityNames[] = { "Low", "Medium", "High", "Ultra" };
constexpr int32_t qualityCount = sizeof(qualityNames) / sizeof(qualityNames[0]);

const char* const logLevelNames[] = { "Debug", "Info", "Warn", "Error" };
constexpr int32_t logLevelCount = sizeof(logLevelNames) / sizeof(logLevelNames[0]);

struct Settings
{
    bool enabled = true;
    float strength = 0.5f;
    int32_t samples = 4;
    int32_t quality = 2;
    char label[64] = "Hello from the sample plugin";
    float tint[4] = { 0.3f, 0.7f, 1.0f, 1.0f };
};

Settings settings;
bool unsaved = false;
int32_t clickCount = 0;
int32_t clickLogLevel = OptiLog_Debug;

void LoadSettings()
{
    const Settings defaults;

    settings.enabled = api->GetBool(self, "Enabled", defaults.enabled);
    settings.strength = api->GetFloat(self, "Strength", defaults.strength);
    settings.samples = api->GetInt(self, "Samples", defaults.samples);
    settings.quality = api->GetInt(self, "Quality", defaults.quality);
    api->GetString(self, "Label", settings.label, sizeof(settings.label), defaults.label);

    // Colors don't have their own type, they are kept as separate floats
    const char* channelKeys[] = { "TintR", "TintG", "TintB", "TintA" };
    for (int i = 0; i < 4; i++)
        settings.tint[i] = api->GetFloat(self, channelKeys[i], defaults.tint[i]);

    unsaved = false;
}

void StoreTint()
{
    const char* channelKeys[] = { "TintR", "TintG", "TintB", "TintA" };
    for (int i = 0; i < 4; i++)
        api->SetFloat(self, channelKeys[i], settings.tint[i]);
}

void ResetSettings()
{
    const char* keys[] = { "Enabled", "Strength", "Samples", "Quality", "Label", "TintR", "TintG", "TintB", "TintA" };
    for (const char* key : keys)
        api->ResetValue(self, key);

    LoadSettings();
    unsaved = true;
}

void DrawMainTab(void* /*userData*/)
{
    api->Section("General");

    // Set* only changes the value in memory, Save writes the file
    if (api->Checkbox("Enabled", &settings.enabled))
    {
        api->SetBool(self, "Enabled", settings.enabled);
        unsaved = true;
    }
    api->ItemTooltip("Turns the whole effect on or off");

    // Everything below can't be used while the effect is off
    api->Disabled(!settings.enabled);

    if (api->SliderFloat("Strength", &settings.strength, 0.0f, 1.0f, "%.2f"))
    {
        api->SetFloat(self, "Strength", settings.strength);
        unsaved = true;
    }
    api->ItemTooltip("0.0 - no effect\n1.0 - full effect");

    // Logged once when the slider is released, not on every frame of the drag
    if (api->ItemDeactivatedAfterEdit())
    {
        char text[64];
        snprintf(text, sizeof(text), "Strength set to %.2f", settings.strength);
        api->Log(self, OptiLog_Debug, text);
    }

    if (api->SliderInt("Samples", &settings.samples, 1, 16, nullptr))
    {
        api->SetInt(self, "Samples", settings.samples);
        unsaved = true;
    }

    // Ultra needs 8 samples, below that it stays in the list but can't be picked
    const uint32_t disabledQualities = settings.samples < 8 ? 1u << 3 : 0u;

    if (api->ComboEx("Quality", &settings.quality, qualityNames, qualityCount, disabledQualities))
    {
        api->SetInt(self, "Quality", settings.quality);
        unsaved = true;
    }

    api->Section("Appearance");

    if (api->InputText("Label", settings.label, sizeof(settings.label)))
    {
        api->SetString(self, "Label", settings.label);
        unsaved = true;
    }

    if (api->ColorEdit("Tint", settings.tint, true))
    {
        StoreTint();
        unsaved = true;
    }

    api->TextColored(settings.tint[0], settings.tint[1], settings.tint[2], settings.tint[3], settings.label);

    api->Disabled(false);

    api->Section("Settings");

    if (unsaved)
        api->TextColored(1.0f, 0.8f, 0.0f, 1.0f, "There are unsaved changes");
    else
        api->TextDisabled("Everything is saved");

    api->Spacing();

    if (api->Button("Save"))
    {
        if (api->SaveSettings(self))
        {
            unsaved = false;
            api->Log(self, OptiLog_Info, "Settings saved");
        }
        else
        {
            api->Log(self, OptiLog_Error, "Couldn't save the settings");
        }
    }
    api->ItemTooltip("Writes OptiScaler.ini, the Save INI button of the menu does the same");

    api->SameLine();

    if (api->Button("Reload"))
        LoadSettings();
    api->ItemTooltip("Throws away the unsaved changes");

    api->SameLine();

    if (api->Button("Defaults"))
        ResetSettings();
    api->ItemTooltip("Removes the keys from the ini, save to make it permanent");
}

void DrawInfoTab(void* userData)
{
    auto counter = static_cast<int32_t*>(userData);

    api->Section("Plugin API");

    char text[128];
    snprintf(text, sizeof(text), "OptiScaler API version %u, table size %u bytes", api->version, api->size);
    api->Text(text);

    snprintf(text, sizeof(text), "Built against version %d, table size %zu bytes", OPTI_PLUGIN_API_VERSION,
             sizeof(OptiPluginApi));
    api->TextDisabled(text);

    api->Separator();

    snprintf(text, sizeof(text), "Menu scale: %.2f, menu open: %s", api->GetMenuScale(),
             api->IsMenuOpen() ? "yes" : "no");
    api->Text(text);

    api->Section("Counter");

    // Not kept in the ini, only for this session
    api->Combo("Log clicks as", &clickLogLevel, logLevelNames, logLevelCount);

    // userData given to RegisterTab comes back here
    if (api->Button("Click me"))
    {
        (*counter)++;

        snprintf(text, sizeof(text), "Button clicked %d times", *counter);
        api->Log(self, static_cast<OptiLogLevel>(clickLogLevel), text);
    }

    api->SameLine();

    snprintf(text, sizeof(text), "Clicked %d times", *counter);
    api->Text(text);
}
} // namespace

extern "C" __declspec(dllexport) bool OptiPluginInit(const OptiPluginApi* optiApi, OptiPlugin* plugin)
{
    // An older OptiScaler doesn't have everything this plugin uses
    if (!OPTI_API_HAS(optiApi, ItemDeactivatedAfterEdit))
        return false;

    api = optiApi;
    self = plugin;

    LoadSettings();

    api->RegisterTab(self, "Sample", "Settings of the sample plugin", DrawMainTab, nullptr);
    api->RegisterTab(self, "Sample Info", "What the plugin API reports", DrawInfoTab, &clickCount);

    api->Log(self, OptiLog_Info, "Sample plugin initialized");

    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
