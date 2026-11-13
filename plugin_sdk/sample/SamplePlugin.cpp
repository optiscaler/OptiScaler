// Sample OptiScaler plugin, uses every part of plugin_sdk/optiscaler_plugin.h
//
// Open SamplePlugin.vcxproj in Visual Studio, select Release | x64 and build it. Copy
// bin/Release/SamplePlugin.asi to the plugins folder next to OptiScaler and set LoadAsiPlugins=true
// in OptiScaler.ini. Two tabs show up under PLUGINS in the menu and
// the settings are kept in the [Plugin.SamplePlugin] section of OptiScaler.ini.
// It also registers two upscalers, "Sample FSR" and "Sample DLSS", that always fail.

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <iterator>

#include "../optiscaler_plugin.h"

namespace
{
const OptiPluginApi* api = nullptr;
OptiPlugin* self = nullptr;

const char* const qualityNames[] = { "Low", "Medium", "High", "Ultra" };
constexpr int32_t qualityCount = sizeof(qualityNames) / sizeof(qualityNames[0]);

// Ultra needs at least this many samples
constexpr int32_t ultraQuality = 3;
constexpr int32_t ultraMinSamples = 8;

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

        // Too few samples for Ultra, step down to High
        if (settings.samples < ultraMinSamples && settings.quality == ultraQuality)
        {
            settings.quality = ultraQuality - 1;
            api->SetInt(self, "Quality", settings.quality);
        }

        unsaved = true;
    }

    // With too few samples Ultra stays in the list but can't be picked
    bool disabledQualities[qualityCount] = {};
    disabledQualities[ultraQuality] = settings.samples < ultraMinSamples;

    if (api->Combo("Quality", &settings.quality, qualityNames, qualityCount, disabledQualities))
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

    snprintf(text, sizeof(text), "Menu open: %s", api->IsMenuOpen() ? "yes" : "no");
    api->Text(text);

    api->Section("Counter");

    // Not kept in the ini, only for this session
    api->Combo("Log clicks as", &clickLogLevel, logLevelNames, logLevelCount, nullptr);

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

// Providers ------------------------------------------------------------------------------------------------
// "Sample FSR" and "Sample DLSS" show up in the upscaler lists. Every function fails, so picking one only
// shows how OptiScaler handles a provider that errors out. A real provider would include the FFX API (ffx_api.h)
// and NGX (nvsdk_ngx.h) headers, these stubs only take the right number of arguments.

constexpr uint32_t FfxReturnError = 1;         // FFX_API_RETURN_ERROR
constexpr uint32_t NgxResultFail = 0xBAD00000; // NVSDK_NGX_Result_Fail

uint32_t NgxFailWith1(void*) { return NgxResultFail; }
uint32_t FfxFailWith2(void*, void*) { return FfxReturnError; }
uint32_t FfxFailWith3(void*, void*, void*) { return FfxReturnError; }
uint32_t NgxFailWith4(void*, void*, void*, void*) { return NgxResultFail; }
uint32_t NgxFailWith5(void*, void*, void*, void*, void*) { return NgxResultFail; }
uint32_t NgxFailWith7(void*, void*, void*, void*, void*, void*, void*) { return NgxResultFail; }

struct Export
{
    const char* name;
    void* function;
};

// Same names as the exports of amd_fidelityfx_upscaler_dx12.dll
const Export ffxExports[] = {
    { "ffxCreateContext", (void*) FfxFailWith3 }, { "ffxDestroyContext", (void*) FfxFailWith2 },
    { "ffxConfigure", (void*) FfxFailWith2 },     { "ffxQuery", (void*) FfxFailWith2 },
    { "ffxDispatch", (void*) FfxFailWith2 },
};

// Same names as the exports of nvngx.dll, the ones OptiScaler needs to run DLSS on D3D12 and Vulkan
const Export ngxExports[] = {
    { "NVSDK_NGX_D3D12_Init_Ext", (void*) NgxFailWith5 },
    { "NVSDK_NGX_D3D12_CreateFeature", (void*) NgxFailWith4 },
    { "NVSDK_NGX_D3D12_EvaluateFeature", (void*) NgxFailWith4 },
    { "NVSDK_NGX_D3D12_ReleaseFeature", (void*) NgxFailWith1 },
    { "NVSDK_NGX_VULKAN_Init_Ext", (void*) NgxFailWith7 },
    { "NVSDK_NGX_VULKAN_CreateFeature", (void*) NgxFailWith4 },
    { "NVSDK_NGX_VULKAN_EvaluateFeature", (void*) NgxFailWith4 },
    { "NVSDK_NGX_VULKAN_ReleaseFeature", (void*) NgxFailWith1 },
};

// userData is the export table of the provider
template <size_t N> void* GetProc(void* userData, const char* exportName)
{
    for (const auto& entry : *(const Export(*)[N]) userData)
    {
        if (strcmp(entry.name, exportName) == 0)
            return entry.function;
    }

    return nullptr; // Not provided, OptiScaler skips what it doesn't need
}

void RegisterProviders()
{
    if (!OPTI_API_HAS(api, RegisterProvider))
    {
        api->Log(self, OptiLog_Info, "OptiScaler is too old to register providers");
        return;
    }

    api->RegisterProvider(self, "Sample FSR", OptiProvider_Ffx, GetProc<std::size(ffxExports)>, (void*) &ffxExports);
    api->RegisterProvider(self, "Sample DLSS", OptiProvider_Dlss, GetProc<std::size(ngxExports)>, (void*) &ngxExports);
}
} // namespace

extern "C" __declspec(dllexport) bool OptiPluginInit(const OptiPluginApi* optiApi, OptiPlugin* plugin)
{
    api = optiApi;
    self = plugin;

    LoadSettings();

    api->RegisterTab(self, "Sample", "Settings of the sample plugin", DrawMainTab, nullptr);
    api->RegisterTab(self, "Sample Info", "What the plugin API reports", DrawInfoTab, &clickCount);

    RegisterProviders();

    api->Log(self, OptiLog_Info, "Sample plugin initialized");

    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
