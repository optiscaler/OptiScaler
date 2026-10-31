# OptiScaler plugin SDK

ASI plugins loaded by OptiScaler can add their own tabs to the OptiScaler menu and keep their settings in `OptiScaler.ini`.

- The API is a plain C function table defined in [`optiscaler_plugin.h`](optiscaler_plugin.h). This header is all you need: you don't link against OptiScaler or ImGui.
- Plugins don't have to be rebuilt for newer OptiScaler versions. Fields are only ever appended to `OptiPluginApi`, so before you use a field added after version 1, check it with `OPTI_API_HAS(api, Field)`.

## Getting started

1. Export `OptiPluginInit` from your plugin:

   ```cpp
   extern "C" __declspec(dllexport) bool OptiPluginInit(const OptiPluginApi* api, OptiPlugin* plugin)
   {
       api->RegisterTab(plugin, "My Plugin", "Tooltip in the sidebar", DrawTab, nullptr);
       return true;
   }
   ```

   OptiScaler calls it once, right after `InitializeASI`. Return `false` to opt out; any tabs you registered are then removed.
2. Put the `.asi` file in the `plugins` folder and set `LoadAsiPlugins=true` in `OptiScaler.ini`.
3. Your tabs appear under **PLUGINS** in the menu sidebar.

## Drawing

Inside a draw callback you can use `Section`, `Text`, `Checkbox`, `SliderFloat`/`SliderInt`, `Combo`/`ComboEx`, `InputText`, `ColorEdit`, `Button`, `SameLine`, `Disabled`, `ItemTooltip` and `ItemDeactivatedAfterEdit`.

- `Section` starts a card like the ones in OptiScaler's own tabs.
- `ComboEx` is a combo whose items can be greyed out. `ItemDeactivatedAfterEdit` is true on the frame a slider is released, for values that should apply once instead of on every frame of a drag.
- Cards, disabled blocks and IDs are closed by OptiScaler after the callback, so a plugin can't leave ImGui in a broken state.
- Widget functions do nothing when called outside a draw callback or from another thread.
- Draw callbacks run on the render thread while the menu is open. Keep them fast, don't call D3D/Vulkan from them, and protect any data you share with your own threads.
- If a callback crashes, OptiScaler disables that tab and logs it, instead of crashing the game.

## Settings

Settings are stored in the `[Plugin.<name>]` section of `OptiScaler.ini`, where `<name>` is the plugin file name without the extension and without a `-loadlate` suffix.

- `Get*` returns the default when the key is missing or set to `auto`.
- `Set*` only changes the value in memory. `SaveSettings`, or **Save INI** in the menu, writes the file.
- `ResetValue` removes the key.

## Sample

[`sample/`](sample) contains a plugin that uses every function of the API.

To build it:

1. Install Visual Studio 2022 or newer with the **Desktop development with C++** workload. The free Community edition is enough.
2. Open `sample/SamplePlugin.vcxproj` in Visual Studio (File → Open → Project/Solution).
3. Select the `Release` configuration and the `x64` platform in the toolbar.
4. Build → Build Solution.

The plugin is written to `sample/bin/Release/SamplePlugin.asi`. Copy it to the `plugins` folder next to OptiScaler and set `LoadAsiPlugins=true` in `OptiScaler.ini`.
