#pragma once

#include "SysUtils.h"
#include <Config.h>

#include <imgui/imgui.h>

class ScopedIndent
{
  public:
    explicit ScopedIndent(float indent = 16.0f) : m_indent(indent) { ImGui::Indent(m_indent); }

    ~ScopedIndent() { ImGui::Unindent(m_indent); }

  private:
    float m_indent;
};

class ScopedID
{
  public:
    explicit ScopedID(const char* id) { ImGui::PushID(id); }

    ~ScopedID() { ImGui::PopID(); }
};

struct ImGuiWindow;

// Draws a rectangle around everything drawn while it is alive. Nothing is drawn when it stays empty
class ScopedCard
{
  public:
    explicit ScopedCard(const char* title = nullptr);
    ~ScopedCard();

    ScopedCard(const ScopedCard&) = delete;
    ScopedCard& operator=(const ScopedCard&) = delete;

    // Turns a separator text into the title of the card when it's the first thing in it
    static bool TryTitle(const char* label);

    // With a filter set, only the box with the same id is drawn
    static bool Shows(const char* id);
    static void SetFilter(const char* id);

  private:
    inline static ScopedCard* _current = nullptr;
    inline static const char* _filter = nullptr;

    ImGuiWindow* _window = nullptr;
    ImDrawListSplitter _splitter;
    ImVec2 _min {};
    ImVec2 _pad {};
    float _width = 0.0f;
    float _contentStartY = 0.0f;
    float _oldWorkRectMaxX = 0.0f;
    float _oldContentRectMaxX = 0.0f;
    bool _active = false;
    bool _hasTitle = false;
};

class ScopedCollapsingHeader
{
  public:
    explicit ScopedCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0)
    {
        ImGui::PushID(label);

        ImGui::BeginChild("##CollapsingHeaderChild", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        _headerOpen = ImGui::CollapsingHeader(label, flags);
        _active = true;
    }

    bool IsHeaderOpen() const { return _headerOpen; }

    ~ScopedCollapsingHeader()
    {
        if (_active)
        {
            ImGui::EndChild();
            ImGui::PopID();
        }
    }

  private:
    bool _active = false;
    bool _headerOpen = false;
};

template <typename T> struct MenuOption
{
    T value;
    std::string label;
    std::string tooltip;
    bool disabled = false;
    bool hidden = false;

    MenuOption& set_disabled(bool condition, const std::string& reason = "")
    {
        if (condition)
        {
            disabled = true;
            if (!reason.empty())
                tooltip = reason;
        }
        return *this;
    }

    MenuOption& set_hidden(bool condition)
    {
        if (condition)
        {
            hidden = true;
        }
        return *this;
    }
};

class MenuCommon
{
  private:
    // internal values
    inline static HWND _handle = nullptr;
    // inline static WNDPROC _oWndProc = nullptr;
    inline static bool _isVisible = false;
    inline static bool _isInited = false;
    inline static bool _isUWP = false;

    // mipmap calculations
    inline static bool _showMipmapCalcWindow = false;
    inline static bool _showHudlessWindow = false;
    inline static float _mipBias = 0.0f;
    inline static float _mipBiasCalculated = 0.0f;
    inline static uint32_t _mipmapUpscalerQuality = 0;
    inline static float _mipmapUpscalerRatio = 0;
    inline static uint32_t _displayWidth = 0;
    inline static uint32_t _renderWidth = 0;

    inline static UINT64 _frameCount = 0;

    // reflex
    inline static float _limitFps = std::numeric_limits<float>::infinity();

    // ffx
    inline static int _ffxUpscalerIndex = -1;
    inline static int _ffxFGIndex = -1;

    // output scaling
    inline static float _ssRatio = 0.0f;
    inline static bool _ssEnabled = false;
    inline static Scaler _ssDownsampler = Scaler::FSR1;

    // ui scale
    inline static int _selectedScale = 0;

    // overlay states
    inline static bool _dx11Ready = false;
    inline static bool _dx12Ready = false;
    inline static bool _vulkanReady = false;

    // Blurred game image provided by the renderer, drawn behind the main menu window
    inline static ImTextureID _bgBlurTexture = ImTextureID_Invalid;
    inline static ImVec2 _bgBlurUVScale { 1.0f, 1.0f };
    inline static bool _bgBlurUsed = false;

    inline static void ShowTooltip(const char* tip);

    inline static void ShowResetButton(CustomOptional<bool, NoDefault>* initFlag, std::string buttonName);
    inline static void ReInitUpscaler();

    inline static void SeparatorWithHelpMarker(const char* label, const char* tip);

    inline static bool SliderUInt(const char* label, uint32_t* v, uint32_t v_min, uint32_t v_max,
                                  const char* format = "%d", ImGuiSliderFlags flags = 0);

    static Upscaler GetBackendCode(const API api);
    static void GetCurrentBackendInfo(const API api, Upscaler& upscaler, std::string* name);
    static void RenderUpscalerCombo(const API api, Upscaler currentUpscaler, const std::vector<Upscaler>& options);
    static void AddDx11Backends(Upscaler upscaler);
    static void AddDx12Backends(Upscaler upscaler);
    static void AddVulkanBackends(Upscaler upscaler);
    template <HasDefaultValue B> static void AddResourceBarrier(std::string name, CustomOptional<int32_t, B>* value);
    template <typename TOption>
    static bool ConfigCheckbox(const char* label, TOption& option, const char* tip = nullptr,
                               bool needsRestart = false);
    template <HasDefaultValue B> static void AddDLSSRenderPreset(std::string name, CustomOptional<uint32_t, B>* value);
    template <HasDefaultValue B> static void AddDLSSDRenderPreset(std::string name, CustomOptional<uint32_t, B>* value);
    template <typename TStorage, typename T>
    static void PopulateCombo(const std::string& name, TStorage& currentValue,
                              const std::vector<MenuOption<T>>& options);

    struct RenderMenuContext;

    // RenderMenu orchestration helpers. These keep the public RenderMenu() flow short
    // while preserving the original ImGui layout and draw order.
    static void UpdateRenderTiming(RenderMenuContext& ctx);
    static void UpdateMenuInputMode(RenderMenuContext& ctx);
    static void HandleMenuShortcuts(RenderMenuContext& ctx);
    static void UpdateVersionAndStartupNotifications(RenderMenuContext& ctx);
    static void BeginMenuFrameIfNeeded(RenderMenuContext& ctx);
    static void RenderSplashWindow(RenderMenuContext& ctx);
    static void RenderNotifications(RenderMenuContext& ctx);
    static void UpdateFrameTimeAverages(RenderMenuContext& ctx);
    static void RenderPerformanceOverlay(RenderMenuContext& ctx);
    static void RenderMainMenuWindow(RenderMenuContext& ctx);

    // RenderMainMenuWindow section helpers. These keep the main window flow readable
    // without changing the existing ImGui layout, labels, or setting side effects.
    static void RenderMainMenuHeaderMessages(RenderMenuContext& ctx);
    static void RenderUpscalerStateMessage(RenderMenuContext& ctx);
    static void RenderMainMenuTabs(RenderMenuContext& ctx);
    static void PlotLinesWithRange(const char* label, float (*values_getter)(void* data, int idx), void* data,
                                   int values_count, int values_offset, const char* overlay_text, ImVec2 graph_size,
                                   float padding);
    static void RenderCard(RenderMenuContext& ctx, void (*render)(RenderMenuContext&));

    // Custom tab
    struct MenuBox;
    static const MenuBox* GetMenuBoxes(size_t& count);
    static void RenderMenuBox(RenderMenuContext& ctx, const MenuBox& box);
    static void RenderCustomTab(RenderMenuContext& ctx);
    static void RenderCustomTabSettings(RenderMenuContext& ctx);
    static void RenderStatusInfo(RenderMenuContext& ctx);
    static void RenderDetectedInfo(RenderMenuContext& ctx);
    static void RenderDetectedCard(RenderMenuContext& ctx);
    static void RenderMenuScaleSettings(RenderMenuContext& ctx);
    static void RenderActiveUpscalerSettings(RenderMenuContext& ctx);
    static void RenderFrameGenerationSelection(RenderMenuContext& ctx);
    static void RenderFrameGenerationRuntimeSettings(RenderMenuContext& ctx);
    static void RenderFsrCommonSettings(RenderMenuContext& ctx);
    static void RenderFramerateSettings(RenderMenuContext& ctx);
    static void RenderVsyncSettings(RenderMenuContext& ctx);
    static void RenderFakenvapiSettings(RenderMenuContext& ctx);
    static void RenderLowLatencySettings(RenderMenuContext& ctx);
    static void RenderActiveImageSettings(RenderMenuContext& ctx);
    static void RenderUpscaleRatioSettings(RenderMenuContext& ctx);
    static void RenderOutputScalingSettings(RenderMenuContext& ctx);
    static void RenderInitFlagsSettings(RenderMenuContext& ctx);
    static void RenderMagnifierSettings(RenderMenuContext& ctx);
    static void RenderQuirksSettings(RenderMenuContext& ctx);
    static void RenderAdvancedSettings(RenderMenuContext& ctx);
    static void RenderLoggingSettings(RenderMenuContext& ctx);
    static void RenderThemeSettings(RenderMenuContext& ctx);
    static void RenderFpsOverlaySettings(RenderMenuContext& ctx);
    static void RenderUpscalerInputsSettings(RenderMenuContext& ctx);
    static void RenderApiAndTextureSettings(RenderMenuContext& ctx);
    static void RenderKeybindSettings(RenderMenuContext& ctx);
    static void RenderMainMenuFooter(RenderMenuContext& ctx);
    static void RenderFrameTimeGraph(RenderMenuContext& ctx);
    static void RenderUpscalerTimeGraph(RenderMenuContext& ctx);
    static void RenderShaderTimes(RenderMenuContext& ctx);
    static void RenderMainMenuBottomBar(RenderMenuContext& ctx);
    static void RenderMipmapBiasWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags);
    static void RenderHudlessResourcesWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags);

    static void UpdateManualInput(HWND targetHwnd);

  public:
    static void Dx11Inited() { _dx11Ready = true; }
    static void Dx12Inited() { _dx12Ready = true; }
    static void VulkanInited() { _vulkanReady = true; }
    static bool IsInited() { return _isInited; }
    static bool IsVisible() { return _isVisible; }
    static HWND Handle() { return _handle; }

    // Texture should cover the whole display, uvScale is the part of it matching the display
    static void SetBackgroundBlur(ImTextureID texture, ImVec2 uvScale = ImVec2(1.0f, 1.0f))
    {
        _bgBlurTexture = texture;
        _bgBlurUVScale = uvScale;
        _bgBlurUsed = false;
    }

    // True if the last RenderMenu call drew the blur texture, so the renderer needs to update it
    static bool BackgroundBlurUsed() { return _bgBlurUsed; }

    static bool RenderMenu();
    static void Init(HWND InHwnd, bool isUWP);
    static void Shutdown();
    static void HideMenu();
    static void Present();
    static void ApplyThemeStyle();
};
