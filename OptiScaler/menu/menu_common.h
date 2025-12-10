#pragma once

#include <pch.h>
#include <Util.h>
#include <Config.h>
#include <resource.h>
#include <Logger.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_uwp.h>

#include <detours/detours.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class ScopedIndent
{
  public:
    explicit ScopedIndent(float indent = 16.0f) : m_indent(indent) { ImGui::Indent(m_indent); }

    ~ScopedIndent() { ImGui::Unindent(m_indent); }

  private:
    float m_indent;
};

class MenuCommon
{
  private:
    // internal values
    inline static HWND _handle = nullptr;
    inline static WNDPROC _oWndProc = nullptr;
    inline static bool _isVisible = false;
    inline static bool _isInited = false;
    inline static bool _isUWP = false;
    // inline static bool _isResetRequested = false;

    // mipmap calculations
    inline static bool _showMipmapCalcWindow = false;
    inline static bool _showHudlessWindow = false;
    inline static float _mipBias = 0.0f;
    inline static float _mipBiasCalculated = 0.0f;
    inline static uint32_t _mipmapUpscalerQuality = 0;
    inline static float _mipmapUpscalerRatio = 0;
    inline static uint32_t _displayWidth = 0;
    inline static uint32_t _renderWidth = 0;

    // dlss enabler
    inline static int _deLimitFps = 500;

    inline static UINT64 _frameCount = 0;

    // reflex
    inline static float _limitFps = std::numeric_limits<float>::infinity();

    // ffx
    inline static int _ffxUpscalerIndex = -1;
    inline static int _ffxFGIndex = -1;

    // output scaling
    inline static float _ssRatio = 0.0f;
    inline static bool _ssEnabled = false;
    inline static bool _ssUseFsr = false;
    inline static uint32_t _ssDownsampler = 0;

    // ui scale
    inline static int _selectedScale = 5;
    inline static bool _imguiSizeUpdate = true;

    // overlay states
    inline static bool _dx11Ready = false;
    inline static bool _dx12Ready = false;
    inline static bool _vulkanReady = false;

    inline static void ShowTooltip(const char* tip);

    inline static void ShowHelpMarker(const char* tip);
    inline static void ShowResetButton(CustomOptional<bool, NoDefault>* initFlag, std::string buttonName);
    inline static void ReInitUpscaler();

    inline static void SeparatorWithHelpMarker(const char* label, const char* tip);

#pragma region "Hooks & WndProc"

    // for hooking
    typedef decltype(&SetCursorPos) PFN_SetCursorPos;
    typedef decltype(&ClipCursor) PFN_ClipCursor;
    typedef decltype(&SendInput) PFN_SendInput;
    typedef decltype(&mouse_event) PFN_mouse_event;
    typedef decltype(&GetCursorPos) PFN_GetCursorPos;
    typedef decltype(&SendMessageW) PFN_SendMessageW;

    inline static PFN_SetCursorPos pfn_SetPhysicalCursorPos = nullptr;
    inline static PFN_SetCursorPos pfn_SetCursorPos = nullptr;
    inline static PFN_ClipCursor pfn_ClipCursor = nullptr;
    inline static PFN_mouse_event pfn_mouse_event = nullptr;
    inline static PFN_SendInput pfn_SendInput = nullptr;
    inline static PFN_SendMessageW pfn_SendMessageW = nullptr;
    inline static PFN_GetCursorPos pfn_GetCursorPos = nullptr;

    inline static bool pfn_SetPhysicalCursorPos_hooked = false;
    inline static bool pfn_SetCursorPos_hooked = false;
    inline static bool pfn_ClipCursor_hooked = false;
    inline static bool pfn_mouse_event_hooked = false;
    inline static bool pfn_SendInput_hooked = false;
    inline static bool pfn_SendMessageW_hooked = false;

    inline static RECT _cursorLimit = {};
    inline static POINT _lastPoint = {};

    static LRESULT hkSendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

    static BOOL hkSetPhysicalCursorPos(int x, int y);
    static BOOL hkSetCursorPos(int x, int y);
    static BOOL hkClipCursor(RECT* lpRect);
    static void hkmouse_event(DWORD dwFlags, DWORD dx, DWORD dy, DWORD dwData, ULONG_PTR dwExtraInfo);
    static UINT hkSendInput(UINT cInputs, LPINPUT pInputs, int cbSize);
    static BOOL hkGetPhysicalCursorPos(LPPOINT lpPoint);

    static void AttachHooks();
    static void DetachHooks();

    inline static ImGuiKey ImGui_ImplWin32_VirtualKeyToImGuiKey(WPARAM wParam);

    // Win32 message handler
    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#pragma endregion

    static std::string GetBackendName(std::string* code);
    static std::string GetBackendCode(const API api);
    static void GetCurrentBackendInfo(const API api, std::string* code, std::string* name);
    static void AddDx11Backends(std::string* code, std::string* name);
    static void AddDx12Backends(std::string* code, std::string* name);
    static void AddVulkanBackends(std::string* code, std::string* name);
    template <HasDefaultValue B> static void AddResourceBarrier(std::string name, CustomOptional<int32_t, B>* value);
    template <HasDefaultValue B> static void AddDLSSRenderPreset(std::string name, CustomOptional<uint32_t, B>* value);
    template <HasDefaultValue B> static void AddDLSSDRenderPreset(std::string name, CustomOptional<uint32_t, B>* value);
    template <HasDefaultValue B>
    static void PopulateCombo(std::string name, CustomOptional<uint32_t, B>* value, const char* names[],
                              const std::string desc[], int length, const uint8_t disabledMask[] = nullptr,
                              bool firstAsDefault = true);

  public:
    static void Dx11Inited() { _dx11Ready = true; }
    static void Dx12Inited() { _dx12Ready = true; }
    static void VulkanInited() { _vulkanReady = true; }
    static bool IsInited() { return _isInited; }
    static bool IsVisible() { return _isVisible; }
    static HWND Handle() { return _handle; }

    static bool RenderMenu();
    static void Init(HWND InHwnd, bool isUWP);
    static void Shutdown();
    static void HideMenu();
};
