/*
 * OptiScaler plugin API
 *
 * Lets an ASI plugin loaded by OptiScaler (plugins folder, LoadAsiPlugins=true) add its own tabs to the
 * OptiScaler menu and keep its settings in OptiScaler.ini.
 *
 * The API is plain C and does not depend on the ImGui version used by OptiScaler, so a plugin built against
 * this header keeps working with newer OptiScaler builds. No OptiScaler library or ImGui is linked.
 *
 * Usage:
 *   Export OptiPluginInit from the plugin. OptiScaler calls it once, right after InitializeASI (if present).
 *   Keep the api pointer, it stays valid for the lifetime of the process.
 *
 * Compatibility rules:
 *   - Fields are only ever appended to OptiPluginApi, never removed or reordered.
 *   - api->size is sizeof(OptiPluginApi) of the running OptiScaler. Before using a field added after
 *     version 1, check it with OPTI_API_HAS(api, Field).
 *
 * Strings are UTF-8 and copied by OptiScaler when it has to keep them.
 *
 * Threading:
 *   - Widget functions only work inside a tab draw callback, which runs on the render thread while the menu
 *     is open. Outside of it they do nothing and return false.
 *   - Draw callbacks have to be quick and must not call into D3D/Vulkan or present.
 *   - Registration, settings and logging functions can be called from any thread.
 *
 * Settings:
 *   Stored in OptiScaler.ini, in the [Plugin.<name>] section. <name> is the plugin file name without the
 *   extension and without a "-loadlate" suffix. A missing key or the value "auto" returns the default.
 *   Set* only changes the value in memory, SaveSettings (or Save INI in the menu) writes the file.
 */

#ifndef OPTISCALER_PLUGIN_H
#define OPTISCALER_PLUGIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define OPTI_PLUGIN_API_VERSION 1

    // Handle of the calling plugin, given to OptiPluginInit
    typedef struct OptiPlugin OptiPlugin;

    // Called every frame while the tab is selected and the menu is open
    typedef void (*OptiDrawTabFn)(void* userData);

    typedef enum OptiLogLevel
    {
        OptiLog_Debug = 0,
        OptiLog_Info = 1,
        OptiLog_Warn = 2,
        OptiLog_Error = 3,
    } OptiLogLevel;

    typedef struct OptiPluginApi
    {
        uint32_t size;    // sizeof(OptiPluginApi) of the running OptiScaler
        uint32_t version; // OPTI_PLUGIN_API_VERSION of the running OptiScaler

        // Registration -------------------------------------------------------------------------------------

        // Adds a tab under the PLUGINS group of the menu sidebar. A plugin can register several tabs.
        // description is shown as the sidebar tooltip and can be NULL.
        bool (*RegisterTab)(OptiPlugin* plugin, const char* name, const char* description, OptiDrawTabFn draw,
                            void* userData);

        // Layout, only inside a draw callback ---------------------------------------------------------------

        // Starts a new card with a title, the previous one is closed. Cards are closed after the callback.
        void (*Section)(const char* title);
        void (*Separator)(void);
        // Places the next widget on the same line as the previous one
        void (*SameLine)(void);
        void (*Spacing)(void);
        // Widgets after Disabled(true) can't be interacted with, until Disabled(false) or the end of the callback
        void (*Disabled)(bool disabled);
        // Shows a tooltip when the previous widget is hovered
        void (*ItemTooltip)(const char* text);

        // Text, only inside a draw callback -----------------------------------------------------------------

        void (*Text)(const char* text);
        void (*TextDisabled)(const char* text);
        // r, g, b, a in 0.0 - 1.0
        void (*TextColored)(float r, float g, float b, float a, const char* text);

        // Widgets, only inside a draw callback --------------------------------------------------------------
        // Labels have to be unique inside a tab. Text after "##" is not shown, "Scale##2" is drawn as "Scale".
        // format is printf-like for the value shown on the slider, NULL for the default.

        bool (*Button)(const char* label);                // true when clicked
        bool (*Checkbox)(const char* label, bool* value); // true when changed
        bool (*SliderFloat)(const char* label, float* value, float min, float max, const char* format);
        bool (*SliderInt)(const char* label, int32_t* value, int32_t min, int32_t max, const char* format);
        // items is an array of itemCount strings, current is the selected index
        bool (*Combo)(const char* label, int32_t* current, const char* const* items, int32_t itemCount);
        // buffer is a zero terminated UTF-8 string that is edited in place
        bool (*InputText)(const char* label, char* buffer, size_t bufferSize);
        // rgba is float[4] in 0.0 - 1.0
        bool (*ColorEdit)(const char* label, float* rgba, bool withAlpha);

        // Menu state ----------------------------------------------------------------------------------------

        // UI scale of the menu, multiply custom sizes by it
        float (*GetMenuScale)(void);
        bool (*IsMenuOpen)(void);

        // Settings in [Plugin.<name>] of OptiScaler.ini ---------------------------------------------------

        bool (*GetBool)(OptiPlugin* plugin, const char* key, bool defaultValue);
        int32_t (*GetInt)(OptiPlugin* plugin, const char* key, int32_t defaultValue);
        float (*GetFloat)(OptiPlugin* plugin, const char* key, float defaultValue);
        // Copies the value to buffer (always zero terminated, truncated if needed).
        // Returns false and copies defaultValue when the key is missing.
        bool (*GetString)(OptiPlugin* plugin, const char* key, char* buffer, size_t bufferSize,
                          const char* defaultValue);

        void (*SetBool)(OptiPlugin* plugin, const char* key, bool value);
        void (*SetInt)(OptiPlugin* plugin, const char* key, int32_t value);
        void (*SetFloat)(OptiPlugin* plugin, const char* key, float value);
        void (*SetString)(OptiPlugin* plugin, const char* key, const char* value);
        // Removes the key, Get* returns the default afterwards
        void (*ResetValue)(OptiPlugin* plugin, const char* key);

        // Writes OptiScaler.ini to the disk
        bool (*SaveSettings)(OptiPlugin* plugin);

        // Misc ------------------------------------------------------------------------------------------------

        // Writes to the OptiScaler log, prefixed with the plugin name
        void (*Log)(OptiPlugin* plugin, OptiLogLevel level, const char* message);

    } OptiPluginApi;

// True when the running OptiScaler has the given field of OptiPluginApi
#define OPTI_API_HAS(api, field)                                                                                       \
    ((api) != NULL && (api)->size >= offsetof(OptiPluginApi, field) + sizeof((api)->field) && (api)->field != NULL)

    // Exported by the plugin. Return false to not use the API (nothing registered is kept).
    typedef bool (*PFN_OptiPluginInit)(const OptiPluginApi* api, OptiPlugin* plugin);

#define OPTI_PLUGIN_INIT_NAME "OptiPluginInit"

#ifdef __cplusplus
}
#endif

#endif // OPTISCALER_PLUGIN_H
