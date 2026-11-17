<!-- SPDX-License-Identifier: MIT -->
# OptiScaler plugin SDK

Dlls loaded by OptiScaler can add their own tabs to the OptiScaler menu and keep their settings in `OptiScaler.ini`. Two kinds of dlls get the API:

- **ASI plugins** in the `plugins` folder, loaded when `LoadAsiPlugins=true`.
- **Custom upscaler and FG dlls** in `custom/<name>/<kind>` (`ffx`, `ffx_vk`, `ffx_fg`, `ffx_fg_vk`, `dlss`, `dlssg` or `dlssd`).

Both use the API the same way. Below, "plugin" means either kind.

- The API is a plain C function table defined in [`optiscaler_plugin.h`](optiscaler_plugin.h). This header is all you need: you don't link against OptiScaler or ImGui.
- Plugins don't have to be rebuilt for newer OptiScaler versions. Fields are only ever appended to `OptiPluginApi`, so before you use a field added after version 1, check it with `OPTI_API_HAS(api, Field)`.

## Getting started

1. Export `OptiPluginInit` from your dll:

   ```cpp
   extern "C" __declspec(dllexport) bool OptiPluginInit(const OptiPluginApi* api, OptiPlugin* plugin)
   {
       api->RegisterTab(plugin, "My Plugin", "Tooltip in the sidebar", DrawTab, nullptr);
       return true;
   }
   ```

   OptiScaler calls it once per dll:
   - ASI plugins: right after `InitializeASI`, when the game starts (or after `LateAsiPluginsDelay` for `-loadlate` plugins).
   - Custom upscaler and FG dlls: when OptiScaler loads the dll, the first time that upscaler or FG is used. This can be well after the game started.

   Tabs registered here show up once it returns `true`. Return `false` to opt out; the tabs you registered are then dropped and `RegisterTab` fails from then on.
2. Install the dll:
   - ASI plugin: put the `.asi` file in the `plugins` folder and set `LoadAsiPlugins=true` in `OptiScaler.ini`.
   - Custom upscaler, FG or ray reconstruction: put the dll in `custom/<name>/<kind>` and select `<name>` as the upscaler, FG or ray reconstruction upscaler in the menu.
3. Your tabs appear under **PLUGINS** in the menu sidebar.

## Drawing

Inside a draw callback you can use `Section`, `Text`, `Checkbox`, `SliderFloat`/`SliderInt`, `Combo`, `InputText`, `ColorEdit`, `Button`, `SameLine`, `Disabled`, `ItemTooltip` and `ItemDeactivatedAfterEdit`.

- `Section` starts a card like the ones in OptiScaler's own tabs.
- `Combo` takes an optional array of bools that greys out items. `ItemDeactivatedAfterEdit` is true on the frame a slider is released, for values that should apply once instead of on every frame of a drag.
- Cards, disabled blocks and IDs are closed by OptiScaler after the callback, so a plugin can't leave ImGui in a broken state.
- Widget functions do nothing when called outside a draw callback or from another thread.
- Draw callbacks run on the render thread while the menu is open. Keep them fast, don't call D3D/Vulkan from them, and protect any data you share with your own threads.
- If a callback crashes, OptiScaler disables that tab and logs it, instead of crashing the game.

## Settings

Settings are stored in the `[Plugin.<name>]` section of `OptiScaler.ini`, where `<name>` is:

- for an ASI plugin, the file name without the extension and without a `-loadlate` suffix;
- for a custom upscaler or FG dll, the `<name>` folder in `custom`, not the dll's file name.

- `Get*` returns the default when the key is missing or set to `auto`.
- `Set*` only changes the value in memory. `SaveSettings`, or **Save INI** in the menu, writes the file.
- `ResetValue` removes the key.

## Providers

An ASI plugin can be an upscaler or FG itself, without a dll in `custom`. Register it from `OptiPluginInit`:

```cpp
static void* GetProc(void* userData, const char* exportName)
{
    if (strcmp(exportName, "ffxCreateContext") == 0)
        return (void*) MyCreateContext;
    // ffxDestroyContext, ffxConfigure, ffxQuery, ffxDispatch ...
    return nullptr;
}

if (OPTI_API_HAS(api, RegisterProvider))
    api->RegisterProvider(plugin, "My Upscaler", OptiProvider_Ffx, GetProc, nullptr);
```

- `GetProc` returns the functions a dll of that kind would export, under the same names. OptiScaler calls them exactly like the dll's.
- The name shows up in the upscaler or FG lists like a `custom/<name>` folder and can be put in `OptiScaler.ini` (`Dx12Upscaler=My Upscaler`). A registered name replaces a folder with the same name.
- `OptiProvider_Ffx` and `OptiProvider_Dlss` also run in DX11 games through DX12, `OptiProvider_Ffx` in Vulkan games too.
- `OptiProvider_FfxFg` and `OptiProvider_FfxFgVk` are picked as the FSR FG Provider. They create the frame generation swapchain context as well, so the plugin runs its own swapchain.
- `OptiProvider_Dlssd` runs the game's ray reconstruction, it's picked as the RR upscaler (`RRUpscaler=<name>`).
- Vulkan FFX providers take the FFX SDK 2.x backend type, `FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2`.

## Sample

[`sample/`](sample) contains a plugin that uses every function of the API. It registers two upscaler providers, "Sample FSR" and "Sample DLSS", whose functions always fail, to show the registration without a real upscaler.

To build it:

1. Install Visual Studio 2022 or newer with the **Desktop development with C++** workload. The free Community edition is enough.
2. Open `sample/SamplePlugin.vcxproj` in Visual Studio (File → Open → Project/Solution).
3. Select the `Release` configuration and the `x64` platform in the toolbar.
4. Build → Build Solution.

The plugin is written to `sample/bin/Release/SamplePlugin.asi`. Copy it to the `plugins` folder next to OptiScaler and set `LoadAsiPlugins=true` in `OptiScaler.ini`.

## License

The SDK files that carry an `SPDX-License-Identifier: MIT` tag are licensed under the [MIT License](LICENSE), so plugins can use them under any license. OptiScaler itself is licensed under the GNU GPL v3.
