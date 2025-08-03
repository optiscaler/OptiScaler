#include "menu_common.h"

#include "font/Hack_Compressed.h"

#include <hooks/HooksDx.h>

#include <proxies/XeSS_Proxy.h>
#include <proxies/FfxApi_Proxy.h>

#include "DLSSG_Mod.h"

#include <framegen/ffx/FSRFG_Dx12.h>

#include <nvapi/fakenvapi.h>
#include <nvapi/ReflexHooks.h>

#include <imgui/imgui_internal.h>

#define MARK_ALL_BACKENDS_CHANGED()                                                                                    \
    for (auto& singleChangeBackend : State::Instance().changeBackend)                                                  \
        singleChangeBackend.second = true;

constexpr float fontSize = 14.0f; // just changing this doesn't make other elements scale ideally
static ImVec2 overlaySize(0.0f, 0.0f);
static ImVec2 overlayPosition(-1000.0f, -1000.0f);
static bool _hdrTonemapApplied = false;
static ImVec4 SdrColors[ImGuiCol_COUNT];
static bool receivingWmInputs = false;
static bool inputMenu = false;
static bool inputFps = false;
static bool inputFpsCycle = false;
static bool hasGamepad = false;
static bool fsr31InitTried = false;
static std::string windowTitle;
static std::string selectedUpscalerName = "";
static std::string currentBackend = "";
static std::string currentBackendName = "";

static ImVec2 splashPosition(-1000.0f, -1000.0f);
static ImVec2 splashSize(0.0f, 0.0f);
static double splashStart = 0.0;
static double splashLimit = 0.0;
static std::vector<std::string> splashText = { "May the coping commence...",
                                               "Coping is strong with this one...",
                                               "This is where the fun begins...",
                                               "Got any more of them scalers?...",
                                               "Fake pixels and even faker frames...",
                                               "Fake frames, get your fake frames...",
                                               "I'm here to kick pixels and chew frames...",
                                               "I find your lack of supersampling disturbing...",
                                               "Frame by frame, I scale-up!",
                                               "Resistance is futile. Your pixels will be upscaled.",
                                               "I've got 99 problems, but low-res ain't one.",
                                               "It's over, DLSS, I have the higher ground!",
                                               "This isn't the resolution you're looking for.",
                                               "To infinity and beyond... with ray tracing off.",
                                               "I have a bad feeling about this frame pacing.",
                                               "It's Dangerous to Go Alone-Take This Upscaler",
                                               "Upscaled beyond recognition.",
                                               "Trust the process. Ignore the shimmer.",
                                               "Real fake frames. Certified.",
                                               "The illusion of performance, perfected.",
                                               "This upscaler belongs in a museum!",
                                               "Because native rendering is overrated." };

void MenuCommon::ShowTooltip(const char* tip)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::BeginTooltip();
        ImGui::Text(tip);
        ImGui::EndTooltip();
    }
}

void MenuCommon::ShowHelpMarker(const char* tip)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ShowTooltip(tip);
}

void MenuCommon::ShowResetButton(CustomOptional<bool, NoDefault>* initFlag, std::string buttonName)
{
    ImGui::SameLine();

    ImGui::BeginDisabled(!initFlag->has_value());

    if (ImGui::Button(buttonName.c_str()))
    {
        initFlag->reset();
        ReInitUpscaler();
    }

    ImGui::EndDisabled();
}

inline void MenuCommon::ReInitUpscaler()
{
    if (State::Instance().currentFeature->Name() == "DLSSD")
        State::Instance().newBackend = "dlssd";
    else
        State::Instance().newBackend = currentBackend;

    MARK_ALL_BACKENDS_CHANGED();
}

void MenuCommon::SeparatorWithHelpMarker(const char* label, const char* tip)
{
    auto marker = "(?) ";
    ImGui::SeparatorTextEx(0, label, ImGui::FindRenderedTextEnd(label),
                           ImGui::CalcTextSize(marker, ImGui::FindRenderedTextEnd(marker)).x);
    ShowHelpMarker(tip);
}

LRESULT MenuCommon::hkSendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (_isVisible && Msg == 0x0020)
        return TRUE;
    else
        return pfn_SendMessageW(hWnd, Msg, wParam, lParam);
}

BOOL MenuCommon::hkSetPhysicalCursorPos(int x, int y)
{
    if (_isVisible)
        return TRUE;
    else
        return pfn_SetPhysicalCursorPos(x, y);
}

BOOL MenuCommon::hkGetPhysicalCursorPos(LPPOINT lpPoint)
{
    if (_isVisible)
    {
        lpPoint->x = _lastPoint.x;
        lpPoint->y = _lastPoint.y;
        return TRUE;
    }
    else
        return pfn_GetCursorPos(lpPoint);
}

BOOL MenuCommon::hkSetCursorPos(int x, int y)
{
    if (_isVisible)
        return TRUE;
    else
        return pfn_SetCursorPos(x, y);
}

BOOL MenuCommon::hkClipCursor(RECT* lpRect)
{
    if (_isVisible)
        return TRUE;
    else
    {
        return pfn_ClipCursor(lpRect);
    }
}

void MenuCommon::hkmouse_event(DWORD dwFlags, DWORD dx, DWORD dy, DWORD dwData, ULONG_PTR dwExtraInfo)
{
    if (_isVisible)
        return;
    else
        pfn_mouse_event(dwFlags, dx, dy, dwData, dwExtraInfo);
}

UINT MenuCommon::hkSendInput(UINT cInputs, LPINPUT pInputs, int cbSize)
{
    if (_isVisible)
        return TRUE;
    else
        return pfn_SendInput(cInputs, pInputs, cbSize);
}

void MenuCommon::AttachHooks()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // Detour the functions
    pfn_SetPhysicalCursorPos =
        reinterpret_cast<PFN_SetCursorPos>(DetourFindFunction("user32.dll", "SetPhysicalCursorPos"));
    pfn_SetCursorPos = reinterpret_cast<PFN_SetCursorPos>(DetourFindFunction("user32.dll", "SetCursorPos"));
    pfn_ClipCursor = reinterpret_cast<PFN_ClipCursor>(DetourFindFunction("user32.dll", "ClipCursor"));
    pfn_mouse_event = reinterpret_cast<PFN_mouse_event>(DetourFindFunction("user32.dll", "mouse_event"));
    pfn_SendInput = reinterpret_cast<PFN_SendInput>(DetourFindFunction("user32.dll", "SendInput"));
    pfn_SendMessageW = reinterpret_cast<PFN_SendMessageW>(DetourFindFunction("user32.dll", "SendMessageW"));

    if (pfn_SetPhysicalCursorPos && (pfn_SetPhysicalCursorPos != pfn_SetCursorPos))
        pfn_SetPhysicalCursorPos_hooked =
            (DetourAttach(&(PVOID&) pfn_SetPhysicalCursorPos, hkSetPhysicalCursorPos) == 0);

    if (pfn_SetCursorPos)
        pfn_SetCursorPos_hooked = (DetourAttach(&(PVOID&) pfn_SetCursorPos, hkSetCursorPos) == 0);

    if (pfn_ClipCursor)
        pfn_ClipCursor_hooked = (DetourAttach(&(PVOID&) pfn_ClipCursor, hkClipCursor) == 0);

    if (pfn_mouse_event)
        pfn_mouse_event_hooked = (DetourAttach(&(PVOID&) pfn_mouse_event, hkmouse_event) == 0);

    if (pfn_SendInput)
        pfn_SendInput_hooked = (DetourAttach(&(PVOID&) pfn_SendInput, hkSendInput) == 0);

    if (pfn_SendMessageW)
        pfn_SendMessageW_hooked = (DetourAttach(&(PVOID&) pfn_SendMessageW, hkSendMessageW) == 0);

    DetourTransactionCommit();
}

void MenuCommon::DetachHooks()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (pfn_SetPhysicalCursorPos_hooked)
        DetourDetach(&(PVOID&) pfn_SetPhysicalCursorPos, hkSetPhysicalCursorPos);

    if (pfn_SetCursorPos_hooked)
        DetourDetach(&(PVOID&) pfn_SetCursorPos, hkSetCursorPos);

    if (pfn_ClipCursor_hooked)
        DetourDetach(&(PVOID&) pfn_ClipCursor, hkClipCursor);

    if (pfn_mouse_event_hooked)
        DetourDetach(&(PVOID&) pfn_mouse_event, hkmouse_event);

    if (pfn_SendInput_hooked)
        DetourDetach(&(PVOID&) pfn_SendInput, hkSendInput);

    if (pfn_SendMessageW_hooked)
        DetourDetach(&(PVOID&) pfn_SendMessageW, hkSendMessageW);

    pfn_SetPhysicalCursorPos_hooked = false;
    pfn_SetCursorPos_hooked = false;
    pfn_mouse_event_hooked = false;
    pfn_SendInput_hooked = false;
    pfn_SendMessageW_hooked = false;

    pfn_SetPhysicalCursorPos = nullptr;
    pfn_SetCursorPos = nullptr;
    pfn_mouse_event = nullptr;
    pfn_SendInput = nullptr;
    pfn_SendMessageW = nullptr;

    DetourTransactionCommit();
}

ImGuiKey MenuCommon::ImGui_ImplWin32_VirtualKeyToImGuiKey(WPARAM wParam)
{
    switch (wParam)
    {
    case VK_TAB:
        return ImGuiKey_Tab;
    case VK_LEFT:
        return ImGuiKey_LeftArrow;
    case VK_RIGHT:
        return ImGuiKey_RightArrow;
    case VK_UP:
        return ImGuiKey_UpArrow;
    case VK_DOWN:
        return ImGuiKey_DownArrow;
    case VK_PRIOR:
        return ImGuiKey_PageUp;
    case VK_NEXT:
        return ImGuiKey_PageDown;
    case VK_HOME:
        return ImGuiKey_Home;
    case VK_END:
        return ImGuiKey_End;
    case VK_INSERT:
        return ImGuiKey_Insert;
    case VK_DELETE:
        return ImGuiKey_Delete;
    case VK_BACK:
        return ImGuiKey_Backspace;
    case VK_SPACE:
        return ImGuiKey_Space;
    case VK_RETURN:
        return ImGuiKey_Enter;
    case VK_ESCAPE:
        return ImGuiKey_Escape;
    case VK_OEM_7:
        return ImGuiKey_Apostrophe;
    case VK_OEM_COMMA:
        return ImGuiKey_Comma;
    case VK_OEM_MINUS:
        return ImGuiKey_Minus;
    case VK_OEM_PERIOD:
        return ImGuiKey_Period;
    case VK_OEM_2:
        return ImGuiKey_Slash;
    case VK_OEM_1:
        return ImGuiKey_Semicolon;
    case VK_OEM_PLUS:
        return ImGuiKey_Equal;
    case VK_OEM_4:
        return ImGuiKey_LeftBracket;
    case VK_OEM_5:
        return ImGuiKey_Backslash;
    case VK_OEM_6:
        return ImGuiKey_RightBracket;
    case VK_OEM_3:
        return ImGuiKey_GraveAccent;
    case VK_CAPITAL:
        return ImGuiKey_CapsLock;
    case VK_SCROLL:
        return ImGuiKey_ScrollLock;
    case VK_NUMLOCK:
        return ImGuiKey_NumLock;
    case VK_SNAPSHOT:
        return ImGuiKey_PrintScreen;
    case VK_PAUSE:
        return ImGuiKey_Pause;
    case VK_NUMPAD0:
        return ImGuiKey_Keypad0;
    case VK_NUMPAD1:
        return ImGuiKey_Keypad1;
    case VK_NUMPAD2:
        return ImGuiKey_Keypad2;
    case VK_NUMPAD3:
        return ImGuiKey_Keypad3;
    case VK_NUMPAD4:
        return ImGuiKey_Keypad4;
    case VK_NUMPAD5:
        return ImGuiKey_Keypad5;
    case VK_NUMPAD6:
        return ImGuiKey_Keypad6;
    case VK_NUMPAD7:
        return ImGuiKey_Keypad7;
    case VK_NUMPAD8:
        return ImGuiKey_Keypad8;
    case VK_NUMPAD9:
        return ImGuiKey_Keypad9;
    case VK_DECIMAL:
        return ImGuiKey_KeypadDecimal;
    case VK_DIVIDE:
        return ImGuiKey_KeypadDivide;
    case VK_MULTIPLY:
        return ImGuiKey_KeypadMultiply;
    case VK_SUBTRACT:
        return ImGuiKey_KeypadSubtract;
    case VK_ADD:
        return ImGuiKey_KeypadAdd;
    case VK_LSHIFT:
        return ImGuiKey_LeftShift;
    case VK_LCONTROL:
        return ImGuiKey_LeftCtrl;
    case VK_LMENU:
        return ImGuiKey_LeftAlt;
    case VK_LWIN:
        return ImGuiKey_LeftSuper;
    case VK_RSHIFT:
        return ImGuiKey_RightShift;
    case VK_RCONTROL:
        return ImGuiKey_RightCtrl;
    case VK_RMENU:
        return ImGuiKey_RightAlt;
    case VK_RWIN:
        return ImGuiKey_RightSuper;
    case VK_APPS:
        return ImGuiKey_Menu;
    case '0':
        return ImGuiKey_0;
    case '1':
        return ImGuiKey_1;
    case '2':
        return ImGuiKey_2;
    case '3':
        return ImGuiKey_3;
    case '4':
        return ImGuiKey_4;
    case '5':
        return ImGuiKey_5;
    case '6':
        return ImGuiKey_6;
    case '7':
        return ImGuiKey_7;
    case '8':
        return ImGuiKey_8;
    case '9':
        return ImGuiKey_9;
    case 'A':
        return ImGuiKey_A;
    case 'B':
        return ImGuiKey_B;
    case 'C':
        return ImGuiKey_C;
    case 'D':
        return ImGuiKey_D;
    case 'E':
        return ImGuiKey_E;
    case 'F':
        return ImGuiKey_F;
    case 'G':
        return ImGuiKey_G;
    case 'H':
        return ImGuiKey_H;
    case 'I':
        return ImGuiKey_I;
    case 'J':
        return ImGuiKey_J;
    case 'K':
        return ImGuiKey_K;
    case 'L':
        return ImGuiKey_L;
    case 'M':
        return ImGuiKey_M;
    case 'N':
        return ImGuiKey_N;
    case 'O':
        return ImGuiKey_O;
    case 'P':
        return ImGuiKey_P;
    case 'Q':
        return ImGuiKey_Q;
    case 'R':
        return ImGuiKey_R;
    case 'S':
        return ImGuiKey_S;
    case 'T':
        return ImGuiKey_T;
    case 'U':
        return ImGuiKey_U;
    case 'V':
        return ImGuiKey_V;
    case 'W':
        return ImGuiKey_W;
    case 'X':
        return ImGuiKey_X;
    case 'Y':
        return ImGuiKey_Y;
    case 'Z':
        return ImGuiKey_Z;
    case VK_F1:
        return ImGuiKey_F1;
    case VK_F2:
        return ImGuiKey_F2;
    case VK_F3:
        return ImGuiKey_F3;
    case VK_F4:
        return ImGuiKey_F4;
    case VK_F5:
        return ImGuiKey_F5;
    case VK_F6:
        return ImGuiKey_F6;
    case VK_F7:
        return ImGuiKey_F7;
    case VK_F8:
        return ImGuiKey_F8;
    case VK_F9:
        return ImGuiKey_F9;
    case VK_F10:
        return ImGuiKey_F10;
    case VK_F11:
        return ImGuiKey_F11;
    case VK_F12:
        return ImGuiKey_F12;
    case VK_F13:
        return ImGuiKey_F13;
    case VK_F14:
        return ImGuiKey_F14;
    case VK_F15:
        return ImGuiKey_F15;
    case VK_F16:
        return ImGuiKey_F16;
    case VK_F17:
        return ImGuiKey_F17;
    case VK_F18:
        return ImGuiKey_F18;
    case VK_F19:
        return ImGuiKey_F19;
    case VK_F20:
        return ImGuiKey_F20;
    case VK_F21:
        return ImGuiKey_F21;
    case VK_F22:
        return ImGuiKey_F22;
    case VK_F23:
        return ImGuiKey_F23;
    case VK_F24:
        return ImGuiKey_F24;
    case VK_BROWSER_BACK:
        return ImGuiKey_AppBack;
    case VK_BROWSER_FORWARD:
        return ImGuiKey_AppForward;
    default:
        return ImGuiKey_None;
    }
}

static int lastKey = 0;

class Keybind
{
    std::string name;
    int id;
    bool waitingForKey = false;

    std::string KeyNameFromVirtualKeyCode(USHORT virtualKey)
    {
        if (virtualKey == (USHORT) UnboundKey)
            return "Unbound";

        UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);

        // Keys like Home would display as Num 0 without this fix
        switch (virtualKey)
        {
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
            scanCode |= 0xE000;
            break;
        }

        LPARAM lParam = (scanCode & 0xFF) << 16;
        if (scanCode & 0xE000)
            lParam |= 1 << 24;

        wchar_t buf[64] = {};
        if (GetKeyNameTextW(lParam, buf, static_cast<int>(std::size(buf))) != 0)
            return wstring_to_string(buf);

        return "Unknown";
    }

  public:
    Keybind(std::string name, int id) : name(name), id(id) {}

    void Render(CustomOptional<int>& configKey)
    {
        ImGui::PushID(id);
        if (ImGui::Button(name.c_str()))
        {
            waitingForKey = true;
            lastKey = 0;
        }
        ImGui::PopID();

        if (waitingForKey)
        {
            ImGui::SameLine();
            ImGui::Text("Press any key...");

            if (lastKey == 0 || lastKey == VK_LBUTTON || lastKey == VK_RBUTTON || lastKey == VK_MBUTTON)
                return;

            if (lastKey == VK_ESCAPE)
            {
                waitingForKey = false;
                return;
            }

            if (lastKey == VK_BACK)
                lastKey = UnboundKey;

            configKey = lastKey;
            waitingForKey = false;
            return;
        }

        ImGui::SameLine();
        ImGui::Text(KeyNameFromVirtualKeyCode(configKey.value_or_default()).c_str());

        ImGui::SameLine();
        ImGui::PushID(id);
        if (ImGui::Button("R"))
        {
            configKey.reset();
        }
        ImGui::PopID();
    }
};

// Win32 message handler
LRESULT MenuCommon::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    // LOG_TRACE("msg: {:X}, wParam: {:X}, lParam: {:X}", msg, wParam, lParam);

    if (!State::Instance().isShuttingDown &&
        (msg == WM_QUIT || msg == WM_CLOSE ||
         msg == WM_DESTROY || /* classic messages but they are a bit late to capture */
         (msg == WM_SYSCOMMAND && wParam == SC_CLOSE /* window close*/)))
    {
        LOG_WARN("IsShuttingDown = true");
        State::Instance().isShuttingDown = true;
        return CallWindowProc(_oWndProc, hWnd, msg, wParam, lParam);
    }

    if (State::Instance().isShuttingDown)
        return CallWindowProc(_oWndProc, hWnd, msg, wParam, lParam);

    if (!_dx11Ready && !_dx12Ready && !_vulkanReady)
    {
        if (_isVisible)
        {
            LOG_INFO("No active features, closing ImGui");

            if (pfn_ClipCursor_hooked)
                pfn_ClipCursor(&_cursorLimit);

            _isVisible = false;
            _showMipmapCalcWindow = false;

            io.MouseDrawCursor = false;
            io.WantCaptureKeyboard = false;
            io.WantCaptureMouse = false;
        }

        return CallWindowProc(_oWndProc, hWnd, msg, wParam, lParam);
    }

    bool rawRead = false;
    ImGuiKey imguiKey;
    RAWINPUT rawData {};
    UINT rawDataSize = sizeof(rawData);

    if (msg == WM_INPUT && GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &rawData, &rawDataSize,
                                           sizeof(rawData.data)) != (UINT) -1)
    {
        auto rawCode = GET_RAWINPUT_CODE_WPARAM(wParam);
        rawRead = true;
        receivingWmInputs = true;
        bool isKeyUp = (rawData.data.keyboard.Flags & RI_KEY_BREAK) != 0;
        if (isKeyUp && rawData.header.dwType == RIM_TYPEKEYBOARD && rawData.data.keyboard.VKey != 0)
        {
            lastKey = rawData.data.keyboard.VKey;

            if (!inputMenu)
                inputMenu = rawData.data.keyboard.VKey == Config::Instance()->ShortcutKey.value_or_default();

            if (!inputFps)
                inputFps = rawData.data.keyboard.VKey == Config::Instance()->FpsShortcutKey.value_or_default();

            if (!inputFpsCycle)
                inputFpsCycle =
                    rawData.data.keyboard.VKey == Config::Instance()->FpsCycleShortcutKey.value_or_default();
        }
    }

    if (!lastKey && msg == WM_KEYUP)
        lastKey = wParam;

    if (!inputMenu)
        inputMenu = msg == WM_KEYUP && wParam == Config::Instance()->ShortcutKey.value_or_default();

    if (!inputFps)
        inputFps = msg == WM_KEYUP && wParam == Config::Instance()->FpsShortcutKey.value_or_default();

    if (!inputFpsCycle)
        inputFpsCycle = msg == WM_KEYUP && wParam == Config::Instance()->FpsCycleShortcutKey.value_or_default();

    // SHIFT + DEL - Debug dump
    if (msg == WM_KEYUP && wParam == VK_DELETE && (GetKeyState(VK_SHIFT) & 0x8000))
    {
        State::Instance().xessDebug = true;
        return CallWindowProc(_oWndProc, hWnd, msg, wParam, lParam);
    }

    // ImGui
    if (_isVisible)
    {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        {

            if (msg == WM_KEYUP || msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP ||
                msg == WM_SYSKEYUP ||
                (msg == WM_INPUT && rawRead && rawData.header.dwType == RIM_TYPEMOUSE &&
                 (rawData.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP ||
                  rawData.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP ||
                  rawData.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)))
            {
                LOG_TRACE("ImGui handled & called original, hWnd:{0:X} msg:{1:X} wParam:{2:X} lParam:{3:X}",
                          (ULONG64) hWnd, msg, (ULONG64) wParam, (ULONG64) lParam);
                return CallWindowProc(_oWndProc, hWnd, msg, wParam, lParam);
            }
            else
            {
                LOG_TRACE("ImGui handled, hWnd:{0:X} msg:{1:X} wParam:{2:X} lParam:{3:X}", (ULONG64) hWnd, msg,
                          (ULONG64) wParam, (ULONG64) lParam);
                return TRUE;
            }
        }

        switch (msg)
        {
        case WM_KEYUP:
            if (wParam != Config::Instance()->ShortcutKey.value_or_default())
                return CallWindowProc(_oWndProc, hWnd, msg, wParam, lParam);

            imguiKey = ImGui_ImplWin32_VirtualKeyToImGuiKey(wParam);
            io.AddKeyEvent(imguiKey, false);

            break;

        case WM_LBUTTONDOWN:
            io.AddMouseButtonEvent(0, true);
            return TRUE;

        case WM_LBUTTONUP:
            io.AddMouseButtonEvent(0, false);
            break;

        case WM_RBUTTONDOWN:
            io.AddMouseButtonEvent(1, true);
            return TRUE;

        case WM_RBUTTONUP:
            io.AddMouseButtonEvent(1, false);
            break;

        case WM_MBUTTONDOWN:
            io.AddMouseButtonEvent(2, true);
            return TRUE;

        case WM_MBUTTONUP:
            io.AddMouseButtonEvent(2, false);
            break;

        case WM_LBUTTONDBLCLK:
            io.AddMouseButtonEvent(0, true);
            return TRUE;

        case WM_RBUTTONDBLCLK:
            io.AddMouseButtonEvent(1, true);
            return TRUE;

        case WM_MBUTTONDBLCLK:
            io.AddMouseButtonEvent(2, true);
            return TRUE;

        case WM_KEYDOWN:
            imguiKey = ImGui_ImplWin32_VirtualKeyToImGuiKey(wParam);
            io.AddKeyEvent(imguiKey, true);
            return TRUE;

        case WM_SYSKEYUP:
            break;

        case WM_SYSKEYDOWN:
        case WM_MOUSEMOVE:
        case WM_SETCURSOR:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
            LOG_TRACE("switch handled, hWnd:{0:X} msg:{1:X} wParam:{2:X} lParam:{3:X}", (ULONG64) hWnd, msg,
                      (ULONG64) wParam, (ULONG64) lParam);
            return TRUE;

        case WM_INPUT:
            if (!rawRead)
                return TRUE;

            if (rawData.header.dwType == RIM_TYPEMOUSE)
            {
                if (rawData.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
                {
                    io.AddMouseButtonEvent(0, true);
                }
                else if (rawData.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
                {
                    io.AddMouseButtonEvent(0, false);
                    break;
                }
                if (rawData.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
                {
                    io.AddMouseButtonEvent(1, true);
                }
                else if (rawData.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
                {
                    io.AddMouseButtonEvent(1, false);
                    break;
                }
                if (rawData.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
                {
                    io.AddMouseButtonEvent(2, true);
                }
                else if (rawData.data.mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
                {
                    io.AddMouseButtonEvent(2, false);
                    break;
                }

                if (rawData.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
                    io.AddMouseWheelEvent(0, static_cast<short>(rawData.data.mouse.usButtonData) / (float) WHEEL_DELTA);
            }
            else
            {
                LOG_TRACE("WM_INPUT hWnd:{0:X} msg:{1:X} wParam:{2:X} lParam:{3:X}", (ULONG64) hWnd, msg,
                          (ULONG64) wParam, (ULONG64) lParam);
            }

            return TRUE;

        default:
            break;
        }
    }

    return CallWindowProc(_oWndProc, hWnd, msg, wParam, lParam);
}

void KeyUp(UINT vKey)
{
    inputMenu = vKey == Config::Instance()->ShortcutKey.value_or_default();
    inputFps = vKey == Config::Instance()->FpsShortcutKey.value_or_default();
    inputFpsCycle = vKey == Config::Instance()->FpsCycleShortcutKey.value_or_default();
}

std::string MenuCommon::GetBackendName(std::string* code)
{
    if (*code == "fsr21")
        return "FSR 2.1.2";

    if (*code == "fsr22")
        return "FSR 2.2.1";

    if (*code == "fsr31")
        return "FSR 3.X";

    if (*code == "fsr21_12")
        return "FSR 2.1.2 w/Dx12";

    if (*code == "fsr22_12")
        return "FSR 2.2.1 w/Dx12";

    if (*code == "fsr31_12")
        return "FSR 3.X w/Dx12";

    if (*code == "xess")
        return "XeSS";

    if (*code == "xess_12")
        return "XeSS w/Dx12";

    if (*code == "dlss")
        return "DLSS";

    return "????";
}

std::string MenuCommon::GetBackendCode(const API api)
{
    std::string code;

    if (api == DX11)
        code = Config::Instance()->Dx11Upscaler.value_or_default();
    else if (api == DX12)
        code = Config::Instance()->Dx12Upscaler.value_or_default();
    else
        code = Config::Instance()->VulkanUpscaler.value_or_default();

    return code;
}

void MenuCommon::GetCurrentBackendInfo(const API api, std::string* code, std::string* name)
{
    *code = GetBackendCode(api);
    *name = GetBackendName(code);
}

void MenuCommon::AddDx11Backends(std::string* code, std::string* name)
{
    std::string selectedUpscalerName = "";
    std::string fsr3xName = Config::Instance()->Fsr4Update.value_or_default() ? "FSR 3.X/4 w/Dx12" : "FSR 3.X w/Dx12";

    if (State::Instance().newBackend == "fsr22" || (State::Instance().newBackend == "" && *code == "fsr22"))
        selectedUpscalerName = "FSR 2.2.1";
    else if (State::Instance().newBackend == "fsr22_12" || (State::Instance().newBackend == "" && *code == "fsr22_12"))
        selectedUpscalerName = "FSR 2.2.1 w/Dx12";
    else if (State::Instance().newBackend == "fsr21_12" || (State::Instance().newBackend == "" && *code == "fsr21_12"))
        selectedUpscalerName = "FSR 2.1.2 w/Dx12";
    else if (State::Instance().newBackend == "fsr31" || (State::Instance().newBackend == "" && *code == "fsr31"))
        selectedUpscalerName = "FSR 3.X";
    else if (State::Instance().newBackend == "fsr31_12" || (State::Instance().newBackend == "" && *code == "fsr31_12"))
        selectedUpscalerName = fsr3xName;
    else if (Config::Instance()->DLSSEnabled.value_or_default() &&
             (State::Instance().newBackend == "dlss" || (State::Instance().newBackend == "" && *code == "dlss")))
        selectedUpscalerName = "DLSS";
    else if (State::Instance().newBackend == "xess" || (State::Instance().newBackend == "" && *code == "xess"))
        selectedUpscalerName = "XeSS";
    else
        selectedUpscalerName = "XeSS w/Dx12";

    if (ImGui::BeginCombo("", selectedUpscalerName.c_str()))
    {
        if (ImGui::Selectable("XeSS", *code == "xess"))
            State::Instance().newBackend = "xess";

        if (ImGui::Selectable("FSR 2.2.1", *code == "fsr22"))
            State::Instance().newBackend = "fsr22";

        if (ImGui::Selectable("FSR 3.X", *code == "fsr31"))
            State::Instance().newBackend = "fsr31";

        if (ImGui::Selectable("XeSS w/Dx12", *code == "xess_12"))
            State::Instance().newBackend = "xess_12";

        if (ImGui::Selectable("FSR 2.1.2 w/Dx12", *code == "fsr21_12"))
            State::Instance().newBackend = "fsr21_12";

        if (ImGui::Selectable("FSR 2.2.1 w/Dx12", *code == "fsr22_12"))
            State::Instance().newBackend = "fsr22_12";

        if (ImGui::Selectable(fsr3xName.c_str(), *code == "fsr31_12"))
            State::Instance().newBackend = "fsr31_12";

        if (Config::Instance()->DLSSEnabled.value_or_default() && ImGui::Selectable("DLSS", *code == "dlss"))
            State::Instance().newBackend = "dlss";

        ImGui::EndCombo();
    }
}

void MenuCommon::AddDx12Backends(std::string* code, std::string* name)
{
    std::string selectedUpscalerName = "";
    std::string fsr3xName = Config::Instance()->Fsr4Update.value_or_default() ? "FSR 3.X/4" : "FSR 3.X";

    if (State::Instance().newBackend == "fsr21" || (State::Instance().newBackend == "" && *code == "fsr21"))
        selectedUpscalerName = "FSR 2.1.2";
    else if (State::Instance().newBackend == "fsr22" || (State::Instance().newBackend == "" && *code == "fsr22"))
        selectedUpscalerName = "FSR 2.2.1";
    else if (State::Instance().newBackend == "fsr31" || (State::Instance().newBackend == "" && *code == "fsr31"))
        selectedUpscalerName = fsr3xName;
    else if (Config::Instance()->DLSSEnabled.value_or_default() &&
             (State::Instance().newBackend == "dlss" || (State::Instance().newBackend == "" && *code == "dlss")))
        selectedUpscalerName = "DLSS";
    else
        selectedUpscalerName = "XeSS";

    if (ImGui::BeginCombo("", selectedUpscalerName.c_str()))
    {
        if (ImGui::Selectable("XeSS", *code == "xess"))
            State::Instance().newBackend = "xess";

        if (ImGui::Selectable("FSR 2.1.2", *code == "fsr21"))
            State::Instance().newBackend = "fsr21";

        if (ImGui::Selectable("FSR 2.2.1", *code == "fsr22"))
            State::Instance().newBackend = "fsr22";

        if (ImGui::Selectable(fsr3xName.c_str(), *code == "fsr31"))
            State::Instance().newBackend = "fsr31";

        if (Config::Instance()->DLSSEnabled.value_or_default() && ImGui::Selectable("DLSS", *code == "dlss"))
            State::Instance().newBackend = "dlss";

        ImGui::EndCombo();
    }
}

void MenuCommon::AddVulkanBackends(std::string* code, std::string* name)
{
    std::string selectedUpscalerName = "";

    if (State::Instance().newBackend == "fsr21" || (State::Instance().newBackend == "" && *code == "fsr21"))
        selectedUpscalerName = "FSR 2.1.2";
    else if (State::Instance().newBackend == "fsr31" || (State::Instance().newBackend == "" && *code == "fsr31"))
        selectedUpscalerName = "FSR 3.X";
    else if (State::Instance().newBackend == "xess" || (State::Instance().newBackend == "" && *code == "xess"))
        selectedUpscalerName = "XeSS";
    else if (Config::Instance()->DLSSEnabled.value_or_default() &&
             (State::Instance().newBackend == "dlss" || (State::Instance().newBackend == "" && *code == "dlss")))
        selectedUpscalerName = "DLSS";
    else
        selectedUpscalerName = "FSR 2.2.1";

    if (ImGui::BeginCombo("", selectedUpscalerName.c_str()))
    {
        if (ImGui::Selectable("XeSS", *code == "xess"))
            State::Instance().newBackend = "xess";

        if (ImGui::Selectable("FSR 2.1.2", *code == "fsr21"))
            State::Instance().newBackend = "fsr21";

        if (ImGui::Selectable("FSR 2.2.1", *code == "fsr22"))
            State::Instance().newBackend = "fsr22";

        if (ImGui::Selectable("FSR 3.X", *code == "fsr31"))
            State::Instance().newBackend = "fsr31";

        if (Config::Instance()->DLSSEnabled.value_or_default() && ImGui::Selectable("DLSS", *code == "dlss"))
            State::Instance().newBackend = "dlss";

        ImGui::EndCombo();
    }
}

template <HasDefaultValue B> void MenuCommon::AddResourceBarrier(std::string name, CustomOptional<int32_t, B>* value)
{
    const char* states[] = { "AUTO",
                             "COMMON",
                             "VERTEX_AND_CONSTANT_BUFFER",
                             "INDEX_BUFFER",
                             "RENDER_TARGET",
                             "UNORDERED_ACCESS",
                             "DEPTH_WRITE",
                             "DEPTH_READ",
                             "NON_PIXEL_SHADER_RESOURCE",
                             "PIXEL_SHADER_RESOURCE",
                             "STREAM_OUT",
                             "INDIRECT_ARGUMENT",
                             "COPY_DEST",
                             "COPY_SOURCE",
                             "RESOLVE_DEST",
                             "RESOLVE_SOURCE",
                             "RAYTRACING_ACCELERATION_STRUCTURE",
                             "SHADING_RATE_SOURCE",
                             "GENERIC_READ",
                             "ALL_SHADER_RESOURCE",
                             "PRESENT",
                             "PREDICATION",
                             "VIDEO_DECODE_READ",
                             "VIDEO_DECODE_WRITE",
                             "VIDEO_PROCESS_READ",
                             "VIDEO_PROCESS_WRITE",
                             "VIDEO_ENCODE_READ",
                             "VIDEO_ENCODE_WRITE" };
    const int values[] = { -1,  0,   1,     2,      4,      8,      16,      32,       64,   128,
                           256, 512, 1024,  2048,   4096,   8192,   4194304, 16777216, 2755, 192,
                           0,   310, 65536, 131072, 262144, 524288, 2097152, 8388608 };

    int selected = value->value_or(-1);

    const char* selectedName = "";

    for (int n = 0; n < 28; n++)
    {
        if (values[n] == selected)
        {
            selectedName = states[n];
            break;
        }
    }

    if (ImGui::BeginCombo(name.c_str(), selectedName))
    {
        if (ImGui::Selectable(states[0], !value->has_value()))
            value->reset();

        for (int n = 1; n < 28; n++)
        {
            if (ImGui::Selectable(states[n], selected == values[n]))
                *value = values[n];
        }

        ImGui::EndCombo();
    }
}

template <HasDefaultValue B> void MenuCommon::AddDLSSRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    const char* presets[] = { "DEFAULT",  "PRESET A", "PRESET B", "PRESET C", "PRESET D", "PRESET E",
                              "PRESET F", "PRESET G", "PRESET H", "PRESET I", "PRESET J", "PRESET K",
                              "PRESET L", "PRESET M", "PRESET N", "PRESET O", "Latest" };
    const std::string presetsDesc[] = {
        "Whatever the game uses",
        "Intended for Performance/Balanced/Quality modes.\nAn older variant best suited to combat ghosting for "
        "elements with missing inputs, such as motion vectors.",
        "Intended for Ultra Performance mode.\nSimilar to Preset A but for Ultra Performance mode.",
        "Intended for Performance/Balanced/Quality modes.\nGenerally favors current frame information;\nwell suited "
        "for fast-paced game content.",
        "Default preset for Performance/Balanced/Quality modes;\ngenerally favors image stability.",
        "DLSS 3.7+, a better D preset",
        "Default preset for Ultra Performance and DLAA modes",
        "Unused",

        "Unused",
        "Unused",
        "Transformers",
        "Transformers 2",
        "Unused",
        "Unused",
        "Unused",
        "Unused",

        "Latest supported by the dll"
    };

    if (value->value_or_default() == 0x00FFFFFF)
        *value = 16;

    PopulateCombo(name, value, presets, presetsDesc, std::size(presets));

    // Value for latest preset
    if (value->value_or_default() == 16)
        *value = 0x00FFFFFF;
}

template <HasDefaultValue B> void MenuCommon::AddDLSSDRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    const char* presets[] = { "DEFAULT",  "PRESET A", "PRESET B", "PRESET C", "PRESET D", "PRESET E",
                              "PRESET F", "PRESET G", "PRESET H", "PRESET I", "PRESET J", "PRESET K",
                              "PRESET L", "PRESET M", "PRESET N", "PRESET O", "Latest" };
    const std::string presetsDesc[] = { "Whatever the game uses",
                                        "CNN 1",
                                        "CNN 2",
                                        "CNN 3",
                                        "Transformers",
                                        "Transformers 2",
                                        "Unused",
                                        "Unused",

                                        "Unused",
                                        "Unused",
                                        "Unused",
                                        "Unused",
                                        "Unused",
                                        "Unused",
                                        "Unused",
                                        "Unused",

                                        "Latest supported by the dll" };

    if (value->value_or_default() == 0x00FFFFFF)
        *value = 16;

    PopulateCombo(name, value, presets, presetsDesc, std::size(presets));

    // Value for latest preset
    if (value->value_or_default() == 16)
        *value = 0x00FFFFFF;
}

template <HasDefaultValue B>
void MenuCommon::PopulateCombo(std::string name, CustomOptional<uint32_t, B>* value, const char* names[],
                               const std::string desc[], int length, const uint8_t disabledMask[], bool firstAsDefault)
{
    int selected = value->value_or(0);

    const char* selectedName = "";

    for (int n = 0; n < length; n++)
    {
        if (n == selected)
        {
            selectedName = names[n];
            break;
        }
    }

    if (ImGui::BeginCombo(name.c_str(), selectedName))
    {
        if (ImGui::Selectable(names[0], !value->has_value()))
        {
            if (firstAsDefault)
                value->reset();
            else
                *value = 0;
        }

        if (!desc[0].empty())
            ShowTooltip(desc[0].c_str());

        for (int n = 1; n < length; n++)
        {
            if (disabledMask && disabledMask[n])
                ImGui::BeginDisabled();

            if (ImGui::Selectable(names[n], selected == n))
            {
                if (n != selected)
                    *value = n;
            }

            if (!desc[n].empty())
                ShowTooltip(desc[n].c_str());

            if (disabledMask && disabledMask[n])
                ImGui::EndDisabled();
        }

        ImGui::EndCombo();
    }
}

static ImVec4 toneMapColor(const ImVec4& color)
{
    // Apply tone mapping (e.g., Reinhard tone mapping)
    float luminance = 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
    float mappedLuminance = luminance / (1.0f + luminance);
    float scale = mappedLuminance / luminance;

    return ImVec4(color.x * scale, color.y * scale, color.z * scale, color.w);
}

static void MenuHdrCheck(ImGuiIO io)
{
    // If game is using HDR, apply tone mapping to the ImGui style
    if (State::Instance().isHdrActive ||
        (!Config::Instance()->OverlayMenu.value_or_default() && State::Instance().currentFeature->IsHdr()))
    {
        if (!_hdrTonemapApplied)
        {
            ImGuiStyle& style = ImGui::GetStyle();

            CopyMemory(SdrColors, style.Colors, sizeof(style.Colors));

            // Apply tone mapping to the ImGui style
            for (int i = 0; i < ImGuiCol_COUNT; ++i)
            {
                ImVec4 color = style.Colors[i];
                style.Colors[i] = toneMapColor(color);
            }

            _hdrTonemapApplied = true;
        }
    }
    else
    {
        if (_hdrTonemapApplied)
        {
            ImGuiStyle& style = ImGui::GetStyle();
            CopyMemory(style.Colors, SdrColors, sizeof(style.Colors));
            _hdrTonemapApplied = false;
        }
    }
}

static void MenuSizeCheck(ImGuiIO io)
{
    // Calculate menu scale according to display resolution
    {
        if (!Config::Instance()->MenuScale.has_value())
        {
            float y = State::Instance().screenHeight;

            if (io.DisplaySize.y != 0)
                y = (float) io.DisplaySize.y;

            // 1000p is minimum for 1.0 menu ratio
            Config::Instance()->MenuScale = (float) ((int) (y / 100.0f)) / 10.0f;

            if (Config::Instance()->MenuScale.value() > 1.0f || Config::Instance()->MenuScale.value() <= 0.0f)
                Config::Instance()->MenuScale.value() = 1.0f;

            ImGuiStyle& style = ImGui::GetStyle();
            style.ScaleAllSizes(Config::Instance()->MenuScale.value());

            if (Config::Instance()->MenuScale.value() < 1.0f)
                style.MouseCursorScale = 1.0f;
        }

        if (Config::Instance()->MenuScale.value() < 0.5f)
            Config::Instance()->MenuScale = 0.5f;

        if (Config::Instance()->MenuScale.value() > 2.0f)
            Config::Instance()->MenuScale = 2.0f;
    }
}

static double lastTime = 0.0;
static UINT64 uwpTargetFrame = 0;

bool MenuCommon::RenderMenu()
{
    if (!_isInited)
        return false;

    _frameCount++;

    // FPS & frame time calculation
    auto now = Util::MillisecondsNow();
    double frameTime = 0.0;
    double frameRate = 0.0;

    if (lastTime > 0.0)
    {
        frameTime = now - lastTime;
        frameRate = 1000.0 / frameTime;
    }

    lastTime = now;

    State::Instance().frameTimes.pop_front();
    State::Instance().frameTimes.push_back(frameTime);

    ImGuiIO& io = ImGui::GetIO();
    (void) io;
    auto currentFeature = State::Instance().currentFeature;

    bool newFrame = false;

    // Moved here to prevent gamepad key replay
    if (_isVisible)
    {
        if (hasGamepad)
            io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

        io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    }
    else
    {
        hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
        io.BackendFlags &= 30;
        io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;
    }

    // Handle Inputs
    {
        if (inputFps)
        {
            inputFps = false;
            Config::Instance()->ShowFps = !Config::Instance()->ShowFps.value_or_default();
        }

        if (inputFpsCycle && Config::Instance()->ShowFps.value_or_default())
            Config::Instance()->FpsOverlayType = (Config::Instance()->FpsOverlayType.value_or_default() + 1) % 6;

        if (inputMenu)
        {
            inputMenu = false;
            _isVisible = !_isVisible;

            LOG_DEBUG("Menu key pressed, {0}", _isVisible ? "opening ImGui" : "closing ImGui");

            if (_isVisible)
            {
                Config::Instance()->ReloadFakenvapi();
                auto dllPath = Util::DllPath().parent_path() / "dlssg_to_fsr3_amd_is_better.dll";
                State::Instance().NukemsFilesAvailable = std::filesystem::exists(dllPath);

                if (pfn_ClipCursor_hooked)
                {
                    _ssRatio = 0;

                    if (GetClipCursor(&_cursorLimit))
                        pfn_ClipCursor(nullptr);

                    GetCursorPos(&_lastPoint);
                }
            }
            else
            {
                if (pfn_ClipCursor_hooked)
                    pfn_ClipCursor(&_cursorLimit);

                _showMipmapCalcWindow = false;
            }

            io.MouseDrawCursor = _isVisible;
            io.WantCaptureKeyboard = _isVisible;
            io.WantCaptureMouse = _isVisible;
        }

        inputFpsCycle = false;
    }

    bool frameStarted = false;
    bool frameTimesCalculated = false;
    const double splashTime = 7000.0;
    const double fadeTime = 1000.0;
    static std::string splashMessage;

    // Splash screen
    if (!Config::Instance()->DisableSplash.value_or_default())
    {
        if (splashLimit < 1.0f)
        {
            splashStart = now + 100.0;
            splashLimit = splashStart + splashTime;

            std::srand(static_cast<unsigned>(std::time(nullptr)));
            splashMessage = splashText[std::rand() % splashText.size()];
        }

        if (now > splashStart && now < splashLimit)
        {
            if (!_isUWP)
            {
                ImGui_ImplWin32_NewFrame();
            }
            else if (!newFrame)
            {
                ImVec2 displaySize { State::Instance().screenWidth, State::Instance().screenHeight };
                ImGui_ImplUwp_NewFrame(displaySize);
            }

            MenuHdrCheck(io);
            MenuSizeCheck(io);
            ImGui::NewFrame();

            ImGui::SetNextWindowSize({ 0.0f, 0.0f });
            ImGui::SetNextWindowBgAlpha(Config::Instance()->FpsOverlayAlpha.value_or_default());
            ImGui::SetNextWindowPos(splashPosition, ImGuiCond_Always);

            float windowAlpha = 1.0f;
            if (auto diff = now - splashStart; diff < fadeTime)
                windowAlpha = diff / fadeTime;
            else if (auto diff = splashLimit - now; diff < fadeTime)
                windowAlpha = diff / fadeTime;

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, windowAlpha);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));

            if (ImGui::Begin("Splash", nullptr,
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav))
            {
                float splashScale = 1.0f;
                float baseScaleHeight = 720.0f;

                if (io.DisplaySize.y > baseScaleHeight)
                    splashScale = io.DisplaySize.y / baseScaleHeight;

                if (Config::Instance()->UseHQFont.value_or_default())
                    ImGui::PushFontSize(std::round(splashScale * fontSize));
                else
                    ImGui::SetWindowFontScale(splashScale);

                ImGui::Text("OptiScaler");
                ImGui::TextColored(toneMapColor(ImVec4(1.0, 1.0, 1.0, 0.7)), splashMessage.c_str());

                splashSize = ImGui::GetWindowSize();

                if (Config::Instance()->UseHQFont.value_or_default())
                    ImGui::PopFontSize();

                ImGui::End();

                splashPosition.x = 0.0f; // io.DisplaySize.x - splashWinSize.x;
                splashPosition.y = io.DisplaySize.y - splashSize.y;
            }

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            frameStarted = true;

            if (!_isVisible && !Config::Instance()->ShowFps.value_or_default())
            {
                ImGui::EndFrame();
                return true;
            }
        }
    }

    // FPS Overlay font
    auto fpsScale = Config::Instance()->FpsScale.value_or(Config::Instance()->MenuScale.value_or_default());

    // If Fps overlay is visible
    if (Config::Instance()->ShowFps.value_or_default())
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (State::Instance().frameTimes[i] > 0.0)
            {
                frameTime += State::Instance().frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
        frameTimesCalculated = true;

        if (!frameStarted)
        {
            if (!_isUWP)
            {
                ImGui_ImplWin32_NewFrame();
            }
            else if (!newFrame)
            {
                ImVec2 displaySize { State::Instance().screenWidth, State::Instance().screenHeight };
                ImGui_ImplUwp_NewFrame(displaySize);
            }

            MenuHdrCheck(io);
            MenuSizeCheck(io);
            ImGui::NewFrame();

            frameStarted = true;
        }

        State::Instance().frameTimeMutex.lock();
        std::vector<float> frameTimeArray(State::Instance().frameTimes.begin(), State::Instance().frameTimes.end());
        std::vector<float> upscalerFrameTimeArray(State::Instance().upscaleTimes.begin(),
                                                  State::Instance().upscaleTimes.end());
        State::Instance().frameTimeMutex.unlock();
        float averageFrameTime = 0.0f;
        float averageUpscalerFT = 0.0f;

        for (size_t i = 0; i < frameTimeArray.size(); i++)
        {
            averageFrameTime += frameTimeArray[i];
            averageUpscalerFT += upscalerFrameTimeArray[i];
        }
        averageFrameTime /= frameTimeArray.size();
        averageUpscalerFT /= frameTimeArray.size();

        // Set overlay position
        ImGui::SetNextWindowPos(overlayPosition, ImGuiCond_Always);

        // Set overlay window properties
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));  // Transparent border
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // Transparent frame background
        ImGui::SetNextWindowBgAlpha(Config::Instance()->FpsOverlayAlpha.value_or_default()); // Transparent background

        ImVec4 green(0.0f, 1.0f, 0.0f, 1.0f);
        if (State::Instance().isHdrActive)
            ImGui::PushStyleColor(ImGuiCol_PlotLines, toneMapColor(green)); // Tone Map plot line color
        else
            ImGui::PushStyleColor(ImGuiCol_PlotLines, green);

        auto size = ImVec2 { 0.0f, 0.0f };
        ImGui::SetNextWindowSize(size);

        if (ImGui::Begin("Performance Overlay", nullptr,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav))
        {
            std::string api;
            if (State::Instance().isRunningOnDXVK || State::Instance().isRunningOnLinux)
            {
                api = "VKD3D";
            }
            else
            {
                switch (State::Instance().swapchainApi)
                {
                case Vulkan:
                    api = "VLK";
                    break;

                case DX11:
                    api = "D3D11";
                    break;

                case DX12:
                    api = "D3D12";
                    break;

                default:
                    switch (State::Instance().api)
                    {
                    case Vulkan:
                        api = "VLK";
                        break;

                    case DX11:
                        api = "D3D11";
                        break;

                    case DX12:
                        api = "D3D12";
                        break;

                    default:
                        api = "???";
                        break;
                    }

                    break;
                }
            }

            if (Config::Instance()->UseHQFont.value_or_default())
                ImGui::PushFontSize(std::round(fpsScale * fontSize));
            else
                ImGui::SetWindowFontScale(fpsScale);

            std::string firstLine = "";
            std::string secondLine = "";
            std::string thirdLine = "";

            // Prepare Line 1
            if (Config::Instance()->FpsOverlayType.value_or_default() == 0)
            {
                firstLine = std::format("{} | FPS: {:5.1f}", api.c_str(), frameRate);
            }
            else if (Config::Instance()->FpsOverlayType.value_or_default() == 1)
            {
                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    firstLine =
                        std::format("{} | FPS: {:5.1f}, {:6.2f} ms | {} -> {} {}.{}.{}", api.c_str(), frameRate,
                                    frameTime, State::Instance().currentInputApiName.c_str(),
                                    currentFeature->Name().c_str(), State::Instance().currentFeature->Version().major,
                                    State::Instance().currentFeature->Version().minor,
                                    State::Instance().currentFeature->Version().patch);
                else
                    firstLine = std::format("{} | FPS: {:5.1f}, {:6.2f} ms", api.c_str(), frameRate, frameTime);
            }
            else
            {
                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    firstLine =
                        std::format("{} | FPS: {:5.1f}, Avg: {:5.1f} | {} -> {} {}.{}.{}", api.c_str(), frameRate,
                                    1000.0f / averageFrameTime, State::Instance().currentInputApiName.c_str(),
                                    currentFeature->Name().c_str(), State::Instance().currentFeature->Version().major,
                                    State::Instance().currentFeature->Version().minor,
                                    State::Instance().currentFeature->Version().patch);
                else
                    firstLine = std::format("{} | FPS: {:5.1f}, Avg: {:5.1f}", api.c_str(), frameRate,
                                            1000.0f / averageFrameTime);
            }

            // Prepare Line 2
            if (Config::Instance()->FpsOverlayType.value_or_default() > 1)
            {
                if (Config::Instance()->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                secondLine = std::format("Frame Time: {:6.2f} ms, Avg: {:6.2f} ms", State::Instance().frameTimes.back(),
                                         averageFrameTime);
            }

            // Prepare Line 3
            if (Config::Instance()->FpsOverlayType.value_or_default() > 3)
            {
                thirdLine = std::format("Upscaler Time: {:6.2f} ms, Avg: {:6.2f} ms",
                                        State::Instance().upscaleTimes.back(), averageUpscalerFT);
            }

            ImVec2 plotSize;
            if (Config::Instance()->FpsOverlayHorizontal.value_or_default())
            {
                plotSize = { fpsScale * 150, fpsScale * 16 };
            }
            else
            {
                // Find the widest text width
                auto firstSize = ImGui::CalcTextSize(firstLine.c_str());
                auto secondSize = ImGui::CalcTextSize(secondLine.c_str());
                auto thirdSize = ImGui::CalcTextSize(thirdLine.c_str());
                auto textWidth = 0.0f;

                if (firstSize.x > secondSize.x)
                    textWidth = firstSize.x > thirdSize.x ? firstSize.x : thirdSize.x;
                else
                    textWidth = secondSize.x > thirdSize.x ? secondSize.x : thirdSize.x;

                auto minWidth = fpsScale * 300.0f;
                auto plotWidth = textWidth < minWidth ? minWidth : textWidth;

                plotSize = { plotWidth, fpsScale * 30 };
            }

            // Draw the overlay
            ImGui::Text(firstLine.c_str());

            if (Config::Instance()->FpsOverlayType.value_or_default() > 1)
            {
                if (Config::Instance()->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(secondLine.c_str());
            }

            if (Config::Instance()->FpsOverlayType.value_or_default() > 2)
            {
                if (Config::Instance()->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of frame times
                ImGui::PlotLines("##FrameTimeGraph", frameTimeArray.data(), static_cast<int>(frameTimeArray.size()), 0,
                                 nullptr, 0.0f, 66.6f, plotSize);
            }

            if (Config::Instance()->FpsOverlayType.value_or_default() > 3)
            {
                if (Config::Instance()->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(thirdLine.c_str());
            }

            if (Config::Instance()->FpsOverlayType.value_or_default() > 4)
            {
                if (Config::Instance()->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of upscaler times
                ImGui::PlotLines("##UpscalerFrameTimeGraph", upscalerFrameTimeArray.data(),
                                 static_cast<int>(upscalerFrameTimeArray.size()), 0, nullptr, 0.0f, 20.0f, plotSize);
            }

            ImGui::PopStyleColor(3); // Restore the style
        }

        // Get size for postioning
        overlaySize = ImGui::GetWindowSize();

        if (Config::Instance()->UseHQFont.value_or_default())
            ImGui::PopFontSize();

        ImGui::End();

        // Left / Right
        if (Config::Instance()->FpsOverlayPos.value_or_default() == 0 ||
            Config::Instance()->FpsOverlayPos.value_or_default() == 2)
            overlayPosition.x = 0;
        else
            overlayPosition.x = io.DisplaySize.x - overlaySize.x;

        // Top / Bottom
        if (Config::Instance()->FpsOverlayPos.value_or_default() < 2)
        {
            overlayPosition.y = 0;
        }
        else
        {
            // Prevent overlapping with splash message
            if (!Config::Instance()->DisableSplash.value_or_default() && now > splashStart && now < splashLimit)
                overlayPosition.y = io.DisplaySize.y - overlaySize.y - splashSize.y;
            else
                overlayPosition.y = io.DisplaySize.y - overlaySize.y;
        }

        if (!_isVisible)
        {
            ImGui::EndFrame();
            return true;
        }
    }

    if (!_isVisible)
        return false;

    {
        // Overlay font
        if (Config::Instance()->UseHQFont.value_or_default())
            ImGui::PushFontSize(std::round(Config::Instance()->MenuScale.value_or_default() * fontSize));

        // If overlay is not visible frame needs to be inited
        if (!frameTimesCalculated)
        {
            float frameCnt = 0;
            frameTime = 0;
            for (size_t i = 299; i > 199; i--)
            {
                if (State::Instance().frameTimes[i] > 0.0)
                {
                    frameTime += State::Instance().frameTimes[i];
                    frameCnt++;
                }
            }

            frameTime /= frameCnt;
            frameRate = 1000.0 / frameTime;
        }

        if (!frameStarted)
        {
            if (!_isUWP)
            {
                ImGui_ImplWin32_NewFrame();
            }
            else if (!newFrame)
            {
                ImVec2 displaySize { State::Instance().screenWidth, State::Instance().screenHeight };
                ImGui_ImplUwp_NewFrame(displaySize);
            }

            MenuHdrCheck(io);
            MenuSizeCheck(io);
            ImGui::NewFrame();
        }

        ImGuiWindowFlags flags = 0;
        flags |= ImGuiWindowFlags_NoSavedSettings;
        flags |= ImGuiWindowFlags_NoCollapse;
        flags |= ImGuiWindowFlags_AlwaysAutoResize;

        // if UI scale is changed rescale the style
        if (_imguiSizeUpdate || Config::Instance()->FpsScale.has_value())
        {
            _imguiSizeUpdate = false;

            ImGuiStyle& style = ImGui::GetStyle();
            ImGuiStyle styleold = style; // Backup colors
            style = ImGuiStyle();        // IMPORTANT: ScaleAllSizes will change the original size,
                                         // so we should reset all style config

            style.WindowBorderSize = 1.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupBorderSize = 1.0f;
            style.FrameBorderSize = 1.0f;
            style.TabBorderSize = 1.0f;
            style.WindowRounding = 0.0f;
            style.ChildRounding = 0.0f;
            style.PopupRounding = 0.0f;
            style.FrameRounding = 0.0f;
            style.ScrollbarRounding = 0.0f;
            style.GrabRounding = 0.0f;
            style.TabRounding = 0.0f;
            style.ScaleAllSizes(Config::Instance()->MenuScale.value_or_default());
            style.MouseCursorScale = 1.0f;
            CopyMemory(style.Colors, styleold.Colors, sizeof(style.Colors)); // Restore colors
        }

        auto size = ImVec2 { 0.0f, 0.0f };
        ImGui::SetNextWindowSize(size);

        // Main menu window
        if (windowTitle.empty())
        {
            windowTitle =
                std::format("{} - {} {} {}", VER_PRODUCT_NAME, State::Instance().GameExe,
                            State::Instance().GameName.empty() ? "" : std::format("- {}", State::Instance().GameName),
                            State::Instance().gameQuirks.count() > 0 ? "(Q)" : "");
        }

        if (ImGui::Begin(windowTitle.c_str(), NULL, flags))
        {
            bool rcasEnabled = false;

            if (!_showMipmapCalcWindow && !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetWindowFocus();

            _selectedScale = ((int) (Config::Instance()->MenuScale.value() * 10.0f)) - 5;

            // No active upscaler message
            if (currentFeature == nullptr || !currentFeature->IsInited())
            {
                ImGui::Spacing();

                if (Config::Instance()->UseHQFont.value_or_default())
                    ImGui::PushFontSize(std::round(fontSize * Config::Instance()->MenuScale.value_or_default() * 3.0));
                else
                    ImGui::SetWindowFontScale(Config::Instance()->MenuScale.value_or_default() * 3.0);

                if (State::Instance().nvngxExists || State::Instance().nvngxReplacement.has_value() ||
                    (State::Instance().libxessExists || XeSSProxy::Module() != nullptr))
                {
                    ImGui::Spacing();

                    std::vector<std::string> upscalers;

                    if (State::Instance().fsrHooks)
                        upscalers.push_back("FSR");

                    if (State::Instance().nvngxExists || State::Instance().nvngxReplacement.has_value() ||
                        State::Instance().isRunningOnNvidia)
                        upscalers.push_back("DLSS");

                    if (State::Instance().libxessExists || XeSSProxy::Module() != nullptr)
                        upscalers.push_back("XeSS");

                    auto joined = upscalers | std::views::join_with(std::string { " or " });

                    std::string joinedUpscalers(joined.begin(), joined.end());

                    ImGui::Text("Please select %s as upscaler\nfrom game options and enter the game\nto enable "
                                "upscaler settings.\n",
                                joinedUpscalers.c_str());

                    if (Config::Instance()->UseHQFont.value_or_default())
                        ImGui::PopFontSize();
                    else
                        ImGui::SetWindowFontScale(Config::Instance()->MenuScale.value_or_default());

                    ImGui::Spacing();
                    ImGui::Text("nvngx.dll: %s", State::Instance().nvngxExists || State::Instance().isRunningOnNvidia
                                                     ? "Exists"
                                                     : "Doesn't Exist");
                    ImGui::Text("nvngx replacement: %s",
                                State::Instance().nvngxReplacement.has_value() ? "Exists" : "Doesn't Exist");
                    ImGui::Text("libxess.dll: %s", (State::Instance().libxessExists || XeSSProxy::Module() != nullptr)
                                                       ? "Exists"
                                                       : "Doesn't Exist");
                    ImGui::Text("fsr: %s", State::Instance().fsrHooks ? "Exists" : "Doesn't Exist");

                    ImGui::Spacing();
                }
                else
                {
                    ImGui::Spacing();
                    ImGui::Text(
                        "Can't find nvngx.dll and libxess.dll and FSR inputs\nUpscaling support will NOT work.");
                    ImGui::Spacing();

                    if (Config::Instance()->UseHQFont.value_or_default())
                        ImGui::PopFont();
                    else
                        ImGui::SetWindowFontScale(Config::Instance()->MenuScale.value_or_default());
                }
            }
            else if (currentFeature->IsFrozen())
            {
                ImGui::Spacing();

                if (Config::Instance()->UseHQFont.value_or_default())
                    ImGui::PushFontSize(std::round(fontSize * Config::Instance()->MenuScale.value_or_default() * 3.0));
                else
                    ImGui::SetWindowFontScale(Config::Instance()->MenuScale.value_or_default() * 3.0);

                ImGui::Text("%s is active but not currently used by the game\nPlease enter the game",
                            currentFeature->Name().c_str());

                if (Config::Instance()->UseHQFont.value_or_default())
                    ImGui::PopFont();
                else
                    ImGui::SetWindowFontScale(Config::Instance()->MenuScale.value_or_default());
            }

            if (ImGui::BeginTable("main", 2, ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();

                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    // UPSCALERS -----------------------------
                    ImGui::SeparatorText("Upscalers");
                    ShowTooltip("Which copium you choose?");

                    GetCurrentBackendInfo(State::Instance().api, &currentBackend, &currentBackendName);

                    std::string spoofingText;

                    ImGui::PushItemWidth(180.0f * Config::Instance()->MenuScale.value_or_default());

                    switch (State::Instance().api)
                    {
                    case DX11:
                        if (State::Instance().DeviceAdapterNames.contains(State::Instance().currentD3D11Device))
                            ImGui::Text(
                                State::Instance().DeviceAdapterNames[State::Instance().currentD3D11Device].c_str());
                        else if (State::Instance().DeviceAdapterNames.contains(State::Instance().currentD3D12Device))
                            ImGui::Text(
                                State::Instance().DeviceAdapterNames[State::Instance().currentD3D12Device].c_str());

                        ImGui::Text("D3D11 %s| %s %d.%d.%d", State::Instance().isRunningOnDXVK ? "(DXVK) " : "",
                                    State::Instance().currentFeature->Name().c_str(),
                                    State::Instance().currentFeature->Version().major,
                                    State::Instance().currentFeature->Version().minor,
                                    State::Instance().currentFeature->Version().patch);

                        ImGui::SameLine(0.0f, 6.0f);
                        spoofingText = Config::Instance()->DxgiSpoofing.value_or_default() ? "On" : "Off";
                        ImGui::Text("| Spoof: %s", spoofingText.c_str());

                        if (State::Instance().currentFeature->Name() != "DLSSD")
                            AddDx11Backends(&currentBackend, &currentBackendName);

                        break;

                    case DX12:
                        if (State::Instance().DeviceAdapterNames.contains(State::Instance().currentD3D12Device))
                            ImGui::Text(
                                State::Instance().DeviceAdapterNames[State::Instance().currentD3D12Device].c_str());

                        ImGui::Text("D3D12 %s| %s %d.%d.%d", State::Instance().isRunningOnDXVK ? "(DXVK) " : "",
                                    State::Instance().currentFeature->Name().c_str(),
                                    State::Instance().currentFeature->Version().major,
                                    State::Instance().currentFeature->Version().minor,
                                    State::Instance().currentFeature->Version().patch);
                        ImGui::SameLine(0.0f, 6.0f);
                        ImGui::Text("| Input: %s", State::Instance().currentInputApiName.c_str());

                        ImGui::SameLine(0.0f, 6.0f);
                        spoofingText = Config::Instance()->DxgiSpoofing.value_or_default() ? "On" : "Off";
                        ImGui::Text("| Spoof: %s", spoofingText.c_str());

                        if (State::Instance().currentFeature->Name() != "DLSSD")
                            AddDx12Backends(&currentBackend, &currentBackendName);

                        break;

                    default:
                        if (State::Instance().DeviceAdapterNames.contains(State::Instance().currentVkDevice))
                            ImGui::Text(
                                State::Instance().DeviceAdapterNames[State::Instance().currentVkDevice].c_str());

                        ImGui::Text("Vulkan %s| %s %d.%d.%d", State::Instance().isRunningOnDXVK ? "(DXVK) " : "",
                                    State::Instance().currentFeature->Name().c_str(),
                                    State::Instance().currentFeature->Version().major,
                                    State::Instance().currentFeature->Version().minor,
                                    State::Instance().currentFeature->Version().patch);
                        ImGui::SameLine(0.0f, 6.0f);
                        ImGui::Text("| Input: %s", State::Instance().currentInputApiName.c_str());

                        auto vlkSpoof = Config::Instance()->VulkanSpoofing.value_or_default();
                        auto vlkExtSpoof = Config::Instance()->VulkanExtensionSpoofing.value_or_default();

                        if (vlkSpoof && vlkExtSpoof)
                            spoofingText = "On + Ext";
                        else if (vlkSpoof)
                            spoofingText = "On";
                        else if (vlkExtSpoof)
                            spoofingText = "Just Ext";
                        else
                            spoofingText = "Off";

                        ImGui::SameLine(0.0f, 6.0f);
                        ImGui::Text("| Spoof: %s", spoofingText.c_str());

                        if (State::Instance().currentFeature->Name() != "DLSSD")
                            AddVulkanBackends(&currentBackend, &currentBackendName);
                    }

                    ImGui::PopItemWidth();

                    if (State::Instance().currentFeature->Name() != "DLSSD")
                    {
                        ImGui::SameLine(0.0f, 6.0f);

                        if (ImGui::Button("Change Upscaler##2") && State::Instance().newBackend != "" &&
                            State::Instance().newBackend != currentBackend)
                        {
                            if (State::Instance().newBackend == "xess")
                            {
                                // Reseting them for xess
                                Config::Instance()->DisableReactiveMask.reset();
                                Config::Instance()->DlssReactiveMaskBias.reset();
                            }

                            MARK_ALL_BACKENDS_CHANGED();
                        }
                    }

                    if (State::Instance().currentFeature->AccessToReactiveMask())
                    {
                        ImGui::BeginDisabled(Config::Instance()->DisableReactiveMask.value_or(false));

                        auto useAsTransparency = Config::Instance()->FsrUseMaskForTransparency.value_or_default();
                        if (ImGui::Checkbox("Use Reactive Mask as Transparency Mask", &useAsTransparency))
                            Config::Instance()->FsrUseMaskForTransparency = useAsTransparency;

                        ImGui::EndDisabled();
                    }
                }

                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    // Dx11 with Dx12
                    if (State::Instance().api == DX11 &&
                        Config::Instance()->Dx11Upscaler.value_or_default() != "fsr22" &&
                        Config::Instance()->Dx11Upscaler.value_or_default() != "dlss" &&
                        Config::Instance()->Dx11Upscaler.value_or_default() != "fsr31")
                    {
                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("Dx11 with Dx12 Settings"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            if (bool dontUseNTShared = Config::Instance()->DontUseNTShared.value_or_default();
                                ImGui::Checkbox("Don't Use NTShared", &dontUseNTShared))
                                Config::Instance()->DontUseNTShared = dontUseNTShared;

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }

                    // UPSCALER SPECIFIC -----------------------------

                    // XeSS -----------------------------
                    if (currentBackend == "xess" && State::Instance().currentFeature->Name() != "DLSSD")
                    {
                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("XeSS Settings"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            const char* models[] = { "KPSS", "SPLAT", "MODEL_3", "MODEL_4", "MODEL_5", "MODEL_6" };
                            auto configModes = Config::Instance()->NetworkModel.value_or_default();

                            if (configModes < 0 || configModes > 5)
                                configModes = 0;

                            const char* selectedModel = models[configModes];

                            if (ImGui::BeginCombo("Network Models", selectedModel))
                            {
                                for (int n = 0; n < 6; n++)
                                {
                                    if (ImGui::Selectable(models[n],
                                                          (Config::Instance()->NetworkModel.value_or_default() == n)))
                                    {
                                        Config::Instance()->NetworkModel = n;
                                        State::Instance().newBackend = currentBackend;
                                        MARK_ALL_BACKENDS_CHANGED();
                                    }
                                }

                                ImGui::EndCombo();
                            }
                            ShowHelpMarker("Likely don't do much");

                            if (bool dbg = State::Instance().xessDebug; ImGui::Checkbox("Dump (Shift+Del)", &dbg))
                                State::Instance().xessDebug = dbg;

                            ImGui::SameLine(0.0f, 6.0f);
                            int dbgCount = State::Instance().xessDebugFrames;

                            ImGui::PushItemWidth(95.0f * Config::Instance()->MenuScale.value_or_default());
                            if (ImGui::InputInt("frames", &dbgCount))
                            {
                                if (dbgCount < 4)
                                    dbgCount = 4;
                                else if (dbgCount > 999)
                                    dbgCount = 999;

                                State::Instance().xessDebugFrames = dbgCount;
                            }

                            ImGui::PopItemWidth();

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }

                    // FFX -----------------
                    if (currentBackend.rfind("fsr", 0) == 0 && State::Instance().currentFeature->Name() != "DLSSD" &&
                        (currentBackend == "fsr31" || currentBackend == "fsr31_12"))
                    {
                        ImGui::SeparatorText("FFX Settings");

                        if (_fsr3xIndex < 0)
                            _fsr3xIndex = Config::Instance()->Fsr3xIndex.value_or_default();

                        if (currentBackend == "fsr31" ||
                            currentBackend == "fsr31_12" && State::Instance().fsr3xVersionNames.size() > 0)
                        {
                            ImGui::PushItemWidth(135.0f * Config::Instance()->MenuScale.value_or_default());

                            auto currentName = std::format("FSR {}", State::Instance().fsr3xVersionNames[_fsr3xIndex]);
                            if (ImGui::BeginCombo("FFX Upscaler", currentName.c_str()))
                            {
                                for (int n = 0; n < State::Instance().fsr3xVersionIds.size(); n++)
                                {
                                    auto name = std::format("FSR {}", State::Instance().fsr3xVersionNames[n]);
                                    if (ImGui::Selectable(name.c_str(),
                                                          Config::Instance()->Fsr3xIndex.value_or_default() == n))
                                        _fsr3xIndex = n;
                                }

                                ImGui::EndCombo();
                            }
                            ImGui::PopItemWidth();

                            ShowHelpMarker("List of upscalers reported by FFX SDK");

                            ImGui::SameLine(0.0f, 6.0f);

                            if (ImGui::Button("Change Upscaler") &&
                                _fsr3xIndex != Config::Instance()->Fsr3xIndex.value_or_default())
                            {
                                Config::Instance()->Fsr3xIndex = _fsr3xIndex;
                                State::Instance().newBackend = currentBackend;
                                MARK_ALL_BACKENDS_CHANGED();
                            }

                            auto majorFsrVersion = currentFeature->Version().major;

                            if (majorFsrVersion >= 4)
                            {
                                ImGui::Spacing();

                                if (ImGui::BeginTable("nonLinear", 2, ImGuiTableFlags_SizingStretchProp))
                                {
                                    ImGui::TableNextColumn();

                                    if (bool nlSRGB = Config::Instance()->FsrNonLinearSRGB.value_or_default();
                                        ImGui::Checkbox("Non-Linear sRGB Input", &nlSRGB))
                                    {
                                        Config::Instance()->FsrNonLinearSRGB = nlSRGB;

                                        if (nlSRGB)
                                            Config::Instance()->FsrNonLinearPQ = false;

                                        State::Instance().newBackend = currentBackend;
                                        MARK_ALL_BACKENDS_CHANGED();
                                    }
                                    ShowHelpMarker("Indicates input color resource contains perceptual sRGB colors\n"
                                                   "Might improve upscaling quality of FSR4");

                                    ImGui::TableNextColumn();

                                    if (bool nlPQ = Config::Instance()->FsrNonLinearPQ.value_or_default();
                                        ImGui::Checkbox("Non-Linear PQ Input", &nlPQ))
                                    {
                                        Config::Instance()->FsrNonLinearPQ = nlPQ;

                                        if (nlPQ)
                                            Config::Instance()->FsrNonLinearSRGB = false;

                                        State::Instance().newBackend = currentBackend;
                                        MARK_ALL_BACKENDS_CHANGED();
                                    }
                                    ShowHelpMarker("Indicates input color resource contains perceptual PQ colors\n"
                                                   "Might improve upscaling quality of FSR4");

                                    ImGui::EndTable();
                                }

                                std::array<const char*, 7> models = { "Default", "Model 0", "Model 1", "Model 2",
                                                                      "Model 3", "Model 4", "Model 5" };

                                // Conversion from 0 -> 6 into nullopt + 0 -> 5 is required
                                uint32_t configModes = 0;

                                if (Config::Instance()->Fsr4Model.has_value())
                                    configModes = Config::Instance()->Fsr4Model.value_or(0) + 1;

                                if (configModes < 0 || configModes >= models.size())
                                    configModes = 0;

                                const char* selectedModel = models[configModes];

                                if (ImGui::BeginCombo("Models", selectedModel))
                                {
                                    for (int n = 0; n < models.size(); n++)
                                    {
                                        uint32_t selection = 0;

                                        if (Config::Instance()->Fsr4Model.has_value())
                                            selection = Config::Instance()->Fsr4Model.value_or(0) + 1;

                                        if (ImGui::Selectable(models[n], selection == n))
                                        {
                                            if (n < 1)
                                                Config::Instance()->Fsr4Model.reset();
                                            else
                                                Config::Instance()->Fsr4Model = n - 1;

                                            State::Instance().newBackend = currentBackend;
                                            MARK_ALL_BACKENDS_CHANGED();
                                        }
                                    }

                                    ImGui::EndCombo();
                                }
                                ShowHelpMarker("Model 0 is meant for FSR AA/Ultra Quality\n"
                                               "Model 1 is meant for Quality\n"
                                               "Model 2 is meant for Balanced\n"
                                               "Model 3 is meant for Performance\n"
                                               "Model 5 is meant for Ultra Performance");

                                ImGui::Spacing();
                                ImGui::Text("Current model: %d", State::Instance().currentFsr4Model);
                                ImGui::Spacing();
                            }

                            if (majorFsrVersion == 3)
                            {
                                if (bool dView = Config::Instance()->FsrDebugView.value_or_default();
                                    ImGui::Checkbox("FSR Upscaling Debug View", &dView))
                                    Config::Instance()->FsrDebugView = dView;
                                ShowHelpMarker("Top left: Dilated Motion Vectors\n"
                                               "Top middle: Protected Areas\n"
                                               "Top right: Dilated Depth\n"
                                               "Middle: Upscaled frame\n"
                                               "Bottom left: Disocclusion mask\n"
                                               "Bottom middle: Reactiveness\n"
                                               "Bottom right: Detail Protection Takedown");
                            }

                            ImGui::Spacing();

                            if (currentFeature->Version() >= feature_version { 3, 1, 1 } &&
                                currentFeature->Version() < feature_version { 4, 0, 0 } &&
                                ImGui::CollapsingHeader("FSR 3 Upscaler Fine Tuning"))
                            {
                                ScopedIndent indent {};
                                ImGui::Spacing();

                                ImGui::PushItemWidth(220.0f * Config::Instance()->MenuScale.value_or_default());

                                float velocity = Config::Instance()->FsrVelocity.value_or_default();
                                if (ImGui::SliderFloat("Velocity Factor", &velocity, 0.00f, 1.0f, "%.2f"))
                                    Config::Instance()->FsrVelocity = velocity;

                                ShowHelpMarker("Value of 0.0f can improve temporal stability of bright pixels\n"
                                               "Lower values are more stable with ghosting\n"
                                               "Higher values are more pixelly but less ghosting.");

                                if (currentFeature->Version() >= feature_version { 3, 1, 4 })
                                {
                                    // Reactive Scale
                                    float reactiveScale = Config::Instance()->FsrReactiveScale.value_or_default();
                                    if (ImGui::SliderFloat("Reactive Scale", &reactiveScale, 0.0f, 100.0f, "%.1f"))
                                        Config::Instance()->FsrReactiveScale = reactiveScale;

                                    ShowHelpMarker("Meant for development purpose to test if\n"
                                                   "writing a larger value to reactive mask, reduces ghosting.");

                                    // Shading Scale
                                    float shadingScale = Config::Instance()->FsrShadingScale.value_or_default();
                                    if (ImGui::SliderFloat("Shading Scale", &shadingScale, 0.0f, 100.0f, "%.1f"))
                                        Config::Instance()->FsrShadingScale = shadingScale;

                                    ShowHelpMarker("Increasing this scales fsr3.1 computed shading\n"
                                                   "change value at read to have higher reactiveness.");

                                    // Accumulation Added Per Frame
                                    float accAddPerFrame = Config::Instance()->FsrAccAddPerFrame.value_or_default();
                                    if (ImGui::SliderFloat("Acc. Added Per Frame", &accAddPerFrame, 0.00f, 1.0f,
                                                           "%.2f"))
                                        Config::Instance()->FsrAccAddPerFrame = accAddPerFrame;

                                    ShowHelpMarker(
                                        "Corresponds to amount of accumulation added per frame\n"
                                        "at pixel coordinate where disocclusion occured or when\n"
                                        "reactive mask value is > 0.0f. Decreasing this and \n"
                                        "drawing the ghosting object (IE no mv) to reactive mask \n"
                                        "with value close to 1.0f can decrease temporal ghosting.\n"
                                        "Decreasing this could result in more thin feature pixels flickering.");

                                    // Min Disocclusion Accumulation
                                    float minDisOccAcc = Config::Instance()->FsrMinDisOccAcc.value_or_default();
                                    if (ImGui::SliderFloat("Min. Disocclusion Acc.", &minDisOccAcc, -1.0f, 1.0f,
                                                           "%.2f"))
                                        Config::Instance()->FsrMinDisOccAcc = minDisOccAcc;

                                    ShowHelpMarker("Increasing this value may reduce white pixel temporal\n"
                                                   "flickering around swaying thin objects that are disoccluding \n"
                                                   "one another often. Too high value may increase ghosting.");
                                }

                                ImGui::PopItemWidth();

                                ImGui::Spacing();
                                ImGui::Spacing();
                            }
                        }
                    }

                    // DLSS -----------------
                    if ((Config::Instance()->DLSSEnabled.value_or_default() && currentBackend == "dlss" &&
                         State::Instance().currentFeature->Version().major > 2) ||
                        State::Instance().currentFeature->Name() == "DLSSD")
                    {
                        const bool usesDlssd = State::Instance().currentFeature->Name() == "DLSSD";

                        if (usesDlssd)
                            ImGui::SeparatorText("DLSSD Settings");
                        else
                            ImGui::SeparatorText("DLSS Settings");

                        auto overridden = usesDlssd ? State::Instance().dlssdPresetsOverriddenExternally
                                                    : State::Instance().dlssPresetsOverriddenExternally;

                        if (overridden)
                        {
                            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Presets are overridden externally");
                            ShowHelpMarker("This usually happens due to using tools\n"
                                           "such as Nvidia App or Nvidia Inspector");
                            ImGui::Text("Selecting setting below will disable that external override\n"
                                        "but you need to Save INI and restart the game");

                            ImGui::Spacing();
                        }

                        if (bool pOverride = Config::Instance()->RenderPresetOverride.value_or_default();
                            ImGui::Checkbox("Render Presets Override", &pOverride))
                            Config::Instance()->RenderPresetOverride = pOverride;
                        ShowHelpMarker("Each render preset has it strengths and weaknesses\n"
                                       "Override to potentially improve image quality");

                        ImGui::BeginDisabled(!Config::Instance()->RenderPresetOverride.value_or_default() ||
                                             overridden);

                        ImGui::PushItemWidth(135.0f * Config::Instance()->MenuScale.value_or_default());
                        if (usesDlssd)
                            AddDLSSDRenderPreset("Override Preset", &Config::Instance()->RenderPresetForAll);
                        else
                            AddDLSSRenderPreset("Override Preset", &Config::Instance()->RenderPresetForAll);

                        ImGui::PopItemWidth();

                        ImGui::SameLine(0.0f, 6.0f);

                        if (ImGui::Button("Apply Changes"))
                        {
                            if (usesDlssd)
                                State::Instance().newBackend = "dlssd";
                            else
                                State::Instance().newBackend = currentBackend;

                            MARK_ALL_BACKENDS_CHANGED();
                        }

                        ImGui::EndDisabled();

                        ImGui::Spacing();

                        if (ImGui::CollapsingHeader(usesDlssd ? "Advanced DLSSD Settings" : "Advanced DLSS Settings"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            bool appIdOverride = Config::Instance()->UseGenericAppIdWithDlss.value_or_default();
                            if (ImGui::Checkbox("Use Generic App Id with DLSS", &appIdOverride))
                                Config::Instance()->UseGenericAppIdWithDlss = appIdOverride;

                            ShowHelpMarker("Use generic appid with NGX\n"
                                           "Fixes OptiScaler preset override not working with certain games\n"
                                           "Requires a game restart.");

                            ImGui::BeginDisabled(!Config::Instance()->RenderPresetOverride.value_or_default() ||
                                                 overridden);
                            ImGui::Spacing();
                            ImGui::PushItemWidth(135.0f * Config::Instance()->MenuScale.value_or_default());

                            if (usesDlssd)
                            {
                                AddDLSSDRenderPreset("DLAA Preset", &Config::Instance()->RenderPresetDLAA);
                                AddDLSSDRenderPreset("UltraQ Preset", &Config::Instance()->RenderPresetUltraQuality);
                                AddDLSSDRenderPreset("Quality Preset", &Config::Instance()->RenderPresetQuality);
                                AddDLSSDRenderPreset("Balanced Preset", &Config::Instance()->RenderPresetBalanced);
                                AddDLSSDRenderPreset("Perf Preset", &Config::Instance()->RenderPresetPerformance);
                                AddDLSSDRenderPreset("UltraP Preset",
                                                     &Config::Instance()->RenderPresetUltraPerformance);
                            }
                            else
                            {
                                AddDLSSRenderPreset("DLAA Preset", &Config::Instance()->RenderPresetDLAA);
                                AddDLSSRenderPreset("UltraQ Preset", &Config::Instance()->RenderPresetUltraQuality);
                                AddDLSSRenderPreset("Quality Preset", &Config::Instance()->RenderPresetQuality);
                                AddDLSSRenderPreset("Balanced Preset", &Config::Instance()->RenderPresetBalanced);
                                AddDLSSRenderPreset("Perf Preset", &Config::Instance()->RenderPresetPerformance);
                                AddDLSSRenderPreset("UltraP Preset", &Config::Instance()->RenderPresetUltraPerformance);
                            }
                            ImGui::PopItemWidth();
                            ImGui::EndDisabled();

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }
                }

                const char* fgOptions[] = { "No Frame Generation", "OptiFG", "FSR-FG via Nukem's DLSSG" };
                std::vector<std::string> fgDesc = { "", "", "Select DLSS FG in-game" };
                std::vector<uint8_t> disabledMask = { false, false, false };

                // OptiFG requirements
                if (!Config::Instance()->OverlayMenu.value_or_default())
                {
                    disabledMask[1] = true;
                    fgDesc[1] = "Old overlay menu is unsupported";
                }
                else if (State::Instance().api != DX12)
                {
                    disabledMask[1] = true;
                    fgDesc[1] = "Unsupported API";
                }
                else if (State::Instance().isWorkingAsNvngx)
                {
                    disabledMask[1] = true;
                    fgDesc[1] = "Unsupported Opti working mode";
                }
                else if ((fsr31InitTried && FfxApiProxy::Dx12Module() == nullptr) ||
                         (!fsr31InitTried && !FfxApiProxy::InitFfxDx12()))
                {
                    fsr31InitTried = true;
                    disabledMask[1] = true;
                    fgDesc[1] = "amd_fidelityfx_dx12.dll is missing";
                }

                // Nukem's FG mod requirements
                if (State::Instance().api == DX11)
                {
                    disabledMask[2] = true;
                    fgDesc[2] = "Unsupported API";
                }
                else if (State::Instance().isWorkingAsNvngx)
                {
                    disabledMask[2] = true;
                    fgDesc[2] = "Unsupported Opti working mode";
                }
                else if (!State::Instance().NukemsFilesAvailable)
                {
                    disabledMask[2] = true;
                    fgDesc[2] = "Missing the dlssg_to_fsr3_amd_is_better.dll file";
                }

                constexpr auto fgOptionsCount = sizeof(fgOptions) / sizeof(char*);

                if (!Config::Instance()->FGPreset.has_value())
                    Config::Instance()->FGPreset =
                        Config::Instance()->FGPreset.value_or_default(); // need to have a value before combo

                ImGui::SeparatorText("Frame Generation");

                PopulateCombo("FG Type", reinterpret_cast<CustomOptional<uint32_t>*>(&Config::Instance()->FGPreset),
                              fgOptions, fgDesc.data(), fgOptionsCount, disabledMask.data(), false);

                if (State::Instance().showRestartWarning)
                {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.f, 0.f, 0.0f, 1.f), "Save INI and restart to apply the changes");
                    ImGui::Spacing();
                }

                State::Instance().showRestartWarning =
                    State::Instance().currentFgPreset != Config::Instance()->FGPreset.value_or_default();

                // FSR FG controls
                if (State::Instance().activeFgOutput == FGOutput::FSRFG &&
                    State::Instance().activeFgInput != FGInput::NoFG &&
                    Config::Instance()->OverlayMenu.value_or_default() && !State::Instance().isWorkingAsNvngx &&
                    State::Instance().api == DX12)
                {
                    if (State::Instance().activeFgInput != FGInput::Upscaler ||
                        (currentFeature != nullptr && !currentFeature->IsFrozen()) && FfxApiProxy::InitFfxDx12())
                    {
                        ImGui::SeparatorText("Frame Generation (FSR FG)");

                        bool fgActive = Config::Instance()->FGEnabled.value_or_default();
                        if (ImGui::Checkbox("Active##2", &fgActive))
                        {
                            Config::Instance()->FGEnabled = fgActive;
                            LOG_DEBUG("Enabled set FGEnabled: {}", fgActive);

                            if (Config::Instance()->FGEnabled.value_or_default())
                                State::Instance().FGchanged = true;
                        }

                        ShowHelpMarker("Enable frame generation");

                        bool fgAsync = Config::Instance()->FGAsync.value_or_default();
                        if (ImGui::Checkbox("Allow Async", &fgAsync))
                        {
                            Config::Instance()->FGAsync = fgAsync;

                            if (Config::Instance()->FGEnabled.value_or_default())
                            {
                                State::Instance().FGchanged = true;
                                State::Instance().SCchanged = true;
                                LOG_DEBUG("Async set FGChanged");
                            }
                        }
                        ShowHelpMarker(
                            "Enable Async for better FG performance\nMight cause crashes, especially with HUD Fix!");

                        ImGui::SameLine(0.0f, 16.0f);

                        bool fgDV = Config::Instance()->FGDebugView.value_or_default();
                        if (ImGui::Checkbox("Debug View##2", &fgDV))
                        {
                            Config::Instance()->FGDebugView = fgDV;

                            if (Config::Instance()->FGEnabled.value_or_default())
                            {
                                State::Instance().FGchanged = true;
                                LOG_DEBUG("DebugView set FGChanged");
                            }
                        }
                        ShowHelpMarker("Enable FSR 3.1 frame generation debug view");

                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("Advanced FSR FG Settings"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            ImGui::Checkbox("FG Only Generated", &State::Instance().FGonlyGenerated);
                            ShowHelpMarker("Display only FSR 3.1 generated frames");

                            ImGui::SameLine(0.0f, 16.0f);
                            auto debugResetLines = Config::Instance()->FGDebugResetLines.value_or_default();
                            if (ImGui::Checkbox("Debug Reset Lines", &debugResetLines))
                            {
                                Config::Instance()->FGDebugResetLines = debugResetLines;
                                LOG_DEBUG("Enabled set FGDebugLines: {}", debugResetLines);
                            }
                            ShowHelpMarker("Enables drawing of interpolation skip lines");

                            auto debugTearLines = Config::Instance()->FGDebugTearLines.value_or_default();
                            if (ImGui::Checkbox("Debug Tear Lines", &debugTearLines))
                            {
                                Config::Instance()->FGDebugTearLines = debugTearLines;
                                LOG_DEBUG("Enabled set FGDebugLines: {}", debugTearLines);
                            }
                            ShowHelpMarker("Enables drawing of tear and interpolation skip lines");

                            ImGui::SameLine(0.0f, 16.0f);
                            auto debugPacingLines = Config::Instance()->FGDebugPacingLines.value_or_default();
                            if (ImGui::Checkbox("Debug Pacing Lines", &debugPacingLines))
                            {
                                Config::Instance()->FGDebugPacingLines = debugPacingLines;
                                LOG_DEBUG("Enabled set FGDebugLines: {}", debugPacingLines);
                            }
                            ShowHelpMarker("Enables drawing of pacing lines");

                            ImGui::Spacing();
                            if (ImGui::TreeNode("FG Rectangle Settings"))
                            {
                                ImGui::PushItemWidth(95.0f * Config::Instance()->MenuScale.value_or_default());
                                int rectLeft = Config::Instance()->FGRectLeft.value_or(0);
                                if (ImGui::InputInt("Rect Left", &rectLeft))
                                    Config::Instance()->FGRectLeft = rectLeft;

                                ImGui::SameLine(0.0f, 16.0f);
                                int rectTop = Config::Instance()->FGRectTop.value_or(0);
                                if (ImGui::InputInt("Rect Top", &rectTop))
                                    Config::Instance()->FGRectTop = rectTop;

                                int rectWidth = Config::Instance()->FGRectWidth.value_or(0);
                                if (ImGui::InputInt("Rect Width", &rectWidth))
                                    Config::Instance()->FGRectWidth = rectWidth;

                                ImGui::SameLine(0.0f, 16.0f);
                                int rectHeight = Config::Instance()->FGRectHeight.value_or(0);
                                if (ImGui::InputInt("Rect Height", &rectHeight))
                                    Config::Instance()->FGRectHeight = rectHeight;

                                ImGui::PopItemWidth();
                                ShowHelpMarker("Frame generation rectangle, adjust for letterboxed content");

                                ImGui::BeginDisabled(!Config::Instance()->FGRectLeft.has_value() &&
                                                     !Config::Instance()->FGRectTop.has_value() &&
                                                     !Config::Instance()->FGRectWidth.has_value() &&
                                                     !Config::Instance()->FGRectHeight.has_value());

                                if (ImGui::Button("Reset FG Rect"))
                                {
                                    Config::Instance()->FGRectLeft.reset();
                                    Config::Instance()->FGRectTop.reset();
                                    Config::Instance()->FGRectWidth.reset();
                                    Config::Instance()->FGRectHeight.reset();
                                }

                                ShowHelpMarker("Resets frame generation rectangle");

                                ImGui::EndDisabled();
                                ImGui::TreePop();
                            }

                            FSRFG_Dx12* fsrFG = nullptr;
                            if (State::Instance().currentFG != nullptr)
                                fsrFG = reinterpret_cast<FSRFG_Dx12*>(State::Instance().currentFG);

                            if (fsrFG != nullptr && FfxApiProxy::VersionDx12() >= feature_version { 3, 1, 3 })
                            {
                                ImGui::Spacing();
                                if (ImGui::TreeNode("Frame Pacing Tuning"))
                                {
                                    auto fptEnabled = Config::Instance()->FGFramePacingTuning.value_or_default();
                                    if (ImGui::Checkbox("Enable Tuning", &fptEnabled))
                                    {
                                        Config::Instance()->FGFramePacingTuning = fptEnabled;
                                        State::Instance().FSRFGFTPchanged = true;
                                    }

                                    ImGui::BeginDisabled(!Config::Instance()->FGFramePacingTuning.value_or_default());

                                    ImGui::PushItemWidth(115.0f * Config::Instance()->MenuScale.value_or_default());
                                    auto fptSafetyMargin = Config::Instance()->FGFPTSafetyMarginInMs.value_or_default();
                                    if (ImGui::InputFloat("Safety Margins in ms", &fptSafetyMargin, 0.01, 0.1, "%.2f"))
                                        Config::Instance()->FGFPTSafetyMarginInMs = fptSafetyMargin;
                                    ShowHelpMarker("Safety margins in millisecons\n"
                                                   "FSR default value: 0.1ms\n"
                                                   "Opti default value: 0.01ms");

                                    auto fptVarianceFactor = Config::Instance()->FGFPTVarianceFactor.value_or_default();
                                    if (ImGui::SliderFloat("Variance Factor", &fptVarianceFactor, 0.0f, 1.0f, "%.2f"))
                                        Config::Instance()->FGFPTVarianceFactor = fptVarianceFactor;
                                    ShowHelpMarker("Variance factor\n"
                                                   "FSR default value: 0.1\n"
                                                   "Opti default value: 0.3");
                                    ImGui::PopItemWidth();

                                    auto fpHybridSpin = Config::Instance()->FGFPTAllowHybridSpin.value_or_default();
                                    if (ImGui::Checkbox("Enable Hybrid Spin", &fpHybridSpin))
                                        Config::Instance()->FGFPTAllowHybridSpin = fpHybridSpin;
                                    ShowHelpMarker("Allows pacing spinlock to sleep, should reduce CPU usage\n"
                                                   "Might cause slow ramp up of FPS");

                                    ImGui::PushItemWidth(115.0f * Config::Instance()->MenuScale.value_or_default());
                                    auto fptHybridSpinTime = Config::Instance()->FGFPTHybridSpinTime.value_or_default();
                                    if (ImGui::SliderInt("Hybrid Spin Time", &fptHybridSpinTime, 0, 100))
                                        Config::Instance()->FGFPTHybridSpinTime = fptHybridSpinTime;
                                    ShowHelpMarker("How long to spin if FPTHybridSpin is true. Measured in timer "
                                                   "resolution units.\n"
                                                   "Not recommended to go below 2. Will result in frequent overshoots");
                                    ImGui::PopItemWidth();

                                    auto fpWaitForSingleObjectOnFence =
                                        Config::Instance()->FGFPTAllowWaitForSingleObjectOnFence.value_or_default();
                                    if (ImGui::Checkbox("Enable WaitForSingleObjectOnFence",
                                                        &fpWaitForSingleObjectOnFence))
                                        Config::Instance()->FGFPTAllowWaitForSingleObjectOnFence =
                                            fpWaitForSingleObjectOnFence;
                                    ShowHelpMarker("Allows WaitForSingleObject instead of spinning for fence value");

                                    if (ImGui::Button("Apply Timing Changes"))
                                        State::Instance().FSRFGFTPchanged = true;

                                    ImGui::EndDisabled();
                                    ImGui::TreePop();
                                }
                            }

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }
                }

                // OptiFG
                if (Config::Instance()->OverlayMenu.value_or_default() && State::Instance().api == DX12 &&
                    !State::Instance().isWorkingAsNvngx && State::Instance().activeFgInput == FGInput::Upscaler)
                {
                    SeparatorWithHelpMarker("Frame Generation (OptiFG)", "Using upscaler data for FG");

                    if (currentFeature != nullptr && !currentFeature->IsFrozen() && FfxApiProxy::InitFfxDx12())
                    {
                        bool fgHudfix = Config::Instance()->FGHUDFix.value_or_default();
                        if (ImGui::Checkbox("HUDFix", &fgHudfix))
                        {
                            Config::Instance()->FGHUDFix = fgHudfix;
                            LOG_DEBUG("Enabled set FGHUDFix: {}", fgHudfix);
                            State::Instance().FGchanged = true;
                        }
                        ShowHelpMarker("Enable HUD stability fix, might cause crashes!");

                        ImGui::BeginDisabled(!Config::Instance()->FGHUDFix.value_or_default());

                        ImGui::SameLine(0.0f, 16.0f);
                        ImGui::PushItemWidth(95.0f * Config::Instance()->MenuScale.value_or_default());
                        int hudFixLimit = Config::Instance()->FGHUDLimit.value_or_default();
                        if (ImGui::InputInt("Limit", &hudFixLimit))
                        {
                            if (hudFixLimit < 1)
                                hudFixLimit = 1;
                            else if (hudFixLimit > 999)
                                hudFixLimit = 999;

                            Config::Instance()->FGHUDLimit = hudFixLimit;
                            LOG_DEBUG("Enabled set FGHUDLimit: {}", hudFixLimit);
                        }
                        ShowHelpMarker("Delay HUDless capture, high values might cause crash!");

                        ImGui::EndDisabled();

                        auto hudExtended = Config::Instance()->FGHUDFixExtended.value_or_default();
                        if (ImGui::Checkbox("Extended", &hudExtended))
                        {
                            LOG_DEBUG("Enabled set FGHUDFixExtended: {}", hudExtended);
                            Config::Instance()->FGHUDFixExtended = hudExtended;
                        }
                        ShowHelpMarker("Extended format checks for possible hudless\nMight cause crash and slowdowns!");
                        ImGui::SameLine(0.0f, 16.0f);

                        ImGui::BeginDisabled(!Config::Instance()->FGHUDFix.value_or_default());

                        auto immediate = Config::Instance()->FGImmediateCapture.value_or_default();
                        if (ImGui::Checkbox("Immediate Capture", &immediate))
                        {
                            LOG_DEBUG("Enabled set FGImmediateCapture: {}", immediate);
                            Config::Instance()->FGImmediateCapture = immediate;
                        }
                        ShowHelpMarker("Enables capturing of resources before shader execution.\nIncrease hudless "
                                       "capture chances but might cause capturing of unnecessary resources.");

                        ImGui::PopItemWidth();

                        ImGui::EndDisabled();

                        bool depthScale = Config::Instance()->FGEnableDepthScale.value_or_default();
                        if (ImGui::Checkbox("Scale Depth to fix DLSS RR", &depthScale))
                            Config::Instance()->FGEnableDepthScale = depthScale;
                        ShowHelpMarker("Fix for DLSS-D wrong depth inputs");

                        bool resourceFlip = Config::Instance()->FGResourceFlip.value_or_default();
                        if (ImGui::Checkbox("Flip (Unity)", &resourceFlip))
                            Config::Instance()->FGResourceFlip = resourceFlip;
                        ShowHelpMarker("Flip Velocity & Depth resources of Unity games");

                        ImGui::SameLine(0.0f, 16.0f);

                        bool resourceFlipOffset = Config::Instance()->FGResourceFlipOffset.value_or_default();
                        if (ImGui::Checkbox("Flip Use Offset", &resourceFlipOffset))
                            Config::Instance()->FGResourceFlipOffset = resourceFlipOffset;
                        ShowHelpMarker("Use height difference as offset");

                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("Advanced OptiFG Settings"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            auto rb = Config::Instance()->FGResourceBlocking.value_or_default();
                            if (ImGui::Checkbox("Resource Blocking", &rb))
                            {
                                Config::Instance()->FGResourceBlocking = rb;
                                LOG_DEBUG("Enabled set FGAlwaysTrackHeaps: {}", rb);
                            }
                            ShowHelpMarker("Block rarely used resources from using as hudless \n"
                                           "to prevent flickers and other issues\n\n"
                                           "Hudfix enable/disable will reset the block list!");

                            auto rrc = Config::Instance()->FGRelaxedResolutionCheck.value_or_default();
                            if (ImGui::Checkbox("Relaxed Resource Check", &rrc))
                            {
                                Config::Instance()->FGRelaxedResolutionCheck = rrc;
                                LOG_DEBUG("Enabled set FGRelaxedResolutionCheck: {}", rrc);
                            }
                            ShowHelpMarker("Relax resolution checks for Hudless by 32 pixels \n"
                                           "Helps games which use black borders for some \n"
                                           "resolutions and screen ratios (e.g. Witcher 3)");

                            ImGui::BeginDisabled(State::Instance().FGresetCapturedResources);
                            ImGui::PushItemWidth(95.0f * Config::Instance()->MenuScale.value_or_default());
                            if (ImGui::Checkbox("FG Create List", &State::Instance().FGcaptureResources))
                            {
                                if (!State::Instance().FGcaptureResources)
                                    Config::Instance()->FGHUDLimit = 1;
                                else
                                    State::Instance().FGonlyUseCapturedResources = false;
                            }

                            ImGui::SameLine(0.0f, 16.0f);
                            if (ImGui::Checkbox("FG Use List", &State::Instance().FGonlyUseCapturedResources))
                            {
                                if (State::Instance().FGcaptureResources)
                                {
                                    State::Instance().FGcaptureResources = false;
                                    Config::Instance()->FGHUDLimit = 1;
                                }
                            }

                            ImGui::SameLine(0.0f, 8.0f);
                            ImGui::Text("(%d)", State::Instance().FGcapturedResourceCount);

                            ImGui::PopItemWidth();

                            ImGui::SameLine(0.0f, 16.0f);

                            if (ImGui::Button("Reset List"))
                            {
                                State::Instance().FGresetCapturedResources = true;
                                State::Instance().FGonlyUseCapturedResources = false;
                                State::Instance().FGonlyUseCapturedResources = false;
                            }

                            ImGui::EndDisabled();

                            ImGui::Spacing();
                            ImGui::Spacing();
                            if (ImGui::TreeNode("Tracking Settings"))
                            {
                                auto ath = Config::Instance()->FGAlwaysTrackHeaps.value_or_default();
                                if (ImGui::Checkbox("Always Track Heaps", &ath))
                                {
                                    Config::Instance()->FGAlwaysTrackHeaps = ath;
                                    LOG_DEBUG("Enabled set FGAlwaysTrackHeaps: {}", ath);
                                }
                                ShowHelpMarker("Always track resources, might cause performace issues\nbut also might "
                                               "fix HudFix related crashes!");

                                ImGui::TreePop();
                            }

                            ImGui::Spacing();
                            if (ImGui::TreeNode("Resource Settings"))
                            {
                                bool makeMVCopies = Config::Instance()->FGMakeMVCopy.value_or_default();
                                if (ImGui::Checkbox("FG Make MV Copies", &makeMVCopies))
                                    Config::Instance()->FGMakeMVCopy = makeMVCopies;
                                ShowHelpMarker("Make a copy of motion vectors to use with OptiFG\n"
                                               "For preventing corruptions that might happen");

                                bool makeDepthCopies = Config::Instance()->FGMakeDepthCopy.value_or_default();
                                if (ImGui::Checkbox("FG Make Depth Copies", &makeDepthCopies))
                                    Config::Instance()->FGMakeDepthCopy = makeDepthCopies;
                                ShowHelpMarker("Make a copy of depth to use with OptiFG\n"
                                               "For preventing corruptions that might happen");

                                ImGui::PushItemWidth(115.0f * Config::Instance()->MenuScale.value_or_default());
                                float depthScaleMax = Config::Instance()->FGDepthScaleMax.value_or_default();
                                if (ImGui::InputFloat("FG Scale Depth Max", &depthScaleMax, 10.0f, 100.0f, "%.1f"))
                                    Config::Instance()->FGDepthScaleMax = depthScaleMax;
                                ShowHelpMarker("Depth values will be divided to this value");
                                ImGui::PopItemWidth();

                                ImGui::TreePop();
                            }

                            ImGui::Spacing();
                            if (ImGui::TreeNode("Syncing Settings"))
                            {
                                bool useMutexForPresent = Config::Instance()->FGUseMutexForSwapchain.value_or_default();
                                if (ImGui::Checkbox("FG Use Mutex for Present", &useMutexForPresent))
                                    Config::Instance()->FGUseMutexForSwapchain = useMutexForPresent;
                                ShowHelpMarker("Use mutex to prevent desync of FG and crashes\n"
                                               "Disabling might improve the perf but decrase stability");

                                bool halfSync = Config::Instance()->FGHudfixHalfSync.value_or_default();
                                if (ImGui::Checkbox("FG Mutex Half Sync", &halfSync))
                                {
                                    Config::Instance()->FGHudfixHalfSync = halfSync;

                                    if (halfSync)
                                        Config::Instance()->FGHudfixFullSync = false;

                                    State::Instance().FGchanged = true;
                                }
                                ShowHelpMarker("Release present sync mutex after presenting 1 frame");

                                bool fullSync = Config::Instance()->FGHudfixFullSync.value_or_default();
                                if (ImGui::Checkbox("FG Mutex Full Sync", &fullSync))
                                {
                                    Config::Instance()->FGHudfixFullSync = fullSync;

                                    if (fullSync)
                                        Config::Instance()->FGHudfixHalfSync = false;

                                    State::Instance().FGchanged = true;
                                }
                                ShowHelpMarker("Release present sync mutex after presenting 2 frames");

                                ImGui::TreePop();
                            }

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }
                    else if (currentFeature == nullptr || currentFeature->IsFrozen())
                    {
                        ImGui::Text("Upscaler is not active"); // Probably never will be visible
                    }
                    else if (!FfxApiProxy::InitFfxDx12())
                    {
                        ImGui::TextColored({ 1.0f, 0.0f, 0.0f, 1.0f },
                                           "amd_fidelityfx_dx12.dll is missing!"); // Probably never will be visible
                    }
                }

                // DLSSG Mod
                if (State::Instance().api != DX11 && !State::Instance().isWorkingAsNvngx &&
                    State::Instance().activeFgInput == FGInput::Nukems &&
                    State::Instance().activeFgOutput == FGOutput::Nukems)
                {
                    SeparatorWithHelpMarker("Frame Generation (FSR-FG via Nukem's DLSSG)",
                                            "Requires Nukem's dlssg_to_fsr3 dll\nSelect DLSS FG in-game");

                    if (!State::Instance().NukemsFilesAvailable)
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f),
                                           "Please put dlssg_to_fsr3_amd_is_better.dll next to OptiScaler");

                    if (!ReflexHooks::isReflexHooked())
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Reflex not hooked");
                        ImGui::Text("If you are using an AMD/Intel GPU then make sure you have fakenvapi");
                    }
                    else if (!ReflexHooks::isDlssgDetected())
                    {
                        ImGui::Text("Please select DLSS Frame Generation in the game options\nYou might need to select "
                                    "DLSS first");
                    }

                    if (State::Instance().api == DX12)
                    {
                        ImGui::Text("Current DLSSG state:");
                        ImGui::SameLine();
                        if (ReflexHooks::isDlssgDetected())
                            ImGui::TextColored(ImVec4(0.f, 1.f, 0.25f, 1.f), "ON");
                        else
                            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "OFF");

                        if (bool makeDepthCopy = Config::Instance()->MakeDepthCopy.value_or_default();
                            ImGui::Checkbox("Fix broken visuals", &makeDepthCopy))
                            Config::Instance()->MakeDepthCopy = makeDepthCopy;
                        ShowHelpMarker("Makes a copy of the depth buffer\nCan fix broken visuals in some games on AMD "
                                       "GPUs under Windows\nCan cause stutters so best to use only when necessary");
                    }
                    else if (State::Instance().api == Vulkan)
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f),
                                           "DLSSG is purposefully disabled when this menu is visible");
                        ImGui::Spacing();
                    }

                    if (DLSSGMod::isLoaded())
                    {
                        if (DLSSGMod::is120orNewer())
                        {
                            if (ImGui::Checkbox("Enable Debug View", &State::Instance().DLSSGDebugView))
                            {
                                DLSSGMod::setDebugView(State::Instance().DLSSGDebugView);
                            }
                            if (ImGui::Checkbox("Interpolated frames only", &State::Instance().DLSSGInterpolatedOnly))
                            {
                                DLSSGMod::setInterpolatedOnly(State::Instance().DLSSGInterpolatedOnly);
                            }
                        }
                        else if (DLSSGMod::FSRDebugView() != nullptr)
                        {
                            if (ImGui::Checkbox("Enable Debug View", &State::Instance().DLSSGDebugView))
                            {
                                DLSSGMod::FSRDebugView()(State::Instance().DLSSGDebugView);
                            }
                        }
                    }
                }

                // Streamline FG Inputs
                if (State::Instance().api == DX12 && !State::Instance().isWorkingAsNvngx &&
                    State::Instance().activeFgInput == FGInput::DLSSG)
                {
                    SeparatorWithHelpMarker("Frame Generation (Streamline FG Inputs)", "Select DLSS FG in-game");

                    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

                    if (!ReflexHooks::isReflexHooked())
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Reflex not hooked");
                        ImGui::Text("If you are using an AMD/Intel GPU then make sure you have fakenvapi");
                    }
                    else if (fgOutput != nullptr)
                    {
                        ImGui::Text("Current Streamline FG state:");
                        ImGui::SameLine();
                        if (fgOutput->IsActive())
                        {
                            ImGui::TextColored(ImVec4(0.f, 1.f, 0.25f, 1.f), "ON");
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "OFF");
                            ImGui::Text("Please select DLSS Frame Generation in the game options\n"
                                        "You might need to select DLSS first");
                        }
                    }
                }

                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    // FSR Common -----------------
                    if (currentFeature != nullptr && !currentFeature->IsFrozen() &&
                        (State::Instance().activeFgOutput == FGOutput::FSRFG || currentBackend.rfind("fsr", 0) == 0))
                    {
                        SeparatorWithHelpMarker("FSR Common Settings", "Affects both FSR-FG & Upscalers");

                        bool useFsrVales = Config::Instance()->FsrUseFsrInputValues.value_or_default();
                        if (ImGui::Checkbox("Use FSR Input Values", &useFsrVales))
                            Config::Instance()->FsrUseFsrInputValues = useFsrVales;

                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("FoV & Camera Values"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            bool useVFov = Config::Instance()->FsrVerticalFov.has_value() ||
                                           !Config::Instance()->FsrHorizontalFov.has_value();

                            float vfov = Config::Instance()->FsrVerticalFov.value_or_default();
                            float hfov = Config::Instance()->FsrHorizontalFov.value_or(90.0f);

                            if (useVFov && !Config::Instance()->FsrVerticalFov.has_value())
                                Config::Instance()->FsrVerticalFov = vfov;
                            else if (!useVFov && !Config::Instance()->FsrHorizontalFov.has_value())
                                Config::Instance()->FsrHorizontalFov = hfov;

                            if (ImGui::RadioButton("Use Vert. Fov", useVFov))
                            {
                                Config::Instance()->FsrHorizontalFov.reset();
                                Config::Instance()->FsrVerticalFov = vfov;
                                useVFov = true;
                            }

                            ImGui::SameLine(0.0f, 6.0f);

                            if (ImGui::RadioButton("Use Horz. Fov", !useVFov))
                            {
                                Config::Instance()->FsrVerticalFov.reset();
                                Config::Instance()->FsrHorizontalFov = hfov;
                                useVFov = false;
                            }

                            if (useVFov)
                            {
                                if (ImGui::SliderFloat("Vert. FOV", &vfov, 0.0f, 180.0f, "%.1f"))
                                    Config::Instance()->FsrVerticalFov = vfov;

                                ShowHelpMarker("Might help achieve better image quality");
                            }
                            else
                            {
                                if (ImGui::SliderFloat("Horz. FOV", &hfov, 0.0f, 180.0f, "%.1f"))
                                    Config::Instance()->FsrHorizontalFov = hfov;

                                ShowHelpMarker("Might help achieve better image quality");
                            }

                            float cameraNear;
                            float cameraFar;

                            cameraNear = Config::Instance()->FsrCameraNear.value_or_default();
                            cameraFar = Config::Instance()->FsrCameraFar.value_or_default();

                            if (ImGui::SliderFloat("Camera Near", &cameraNear, 0.1f, 500000.0f, "%.1f"))
                                Config::Instance()->FsrCameraNear = cameraNear;
                            ShowHelpMarker("Might help achieve better image quality\n"
                                           "And potentially less ghosting");

                            if (ImGui::SliderFloat("Camera Far", &cameraFar, 0.1f, 500000.0f, "%.1f"))
                                Config::Instance()->FsrCameraFar = cameraFar;
                            ShowHelpMarker("Might help achieve better image quality\n"
                                           "And potentially less ghosting");

                            if (ImGui::Button("Reset Camera Values"))
                            {
                                Config::Instance()->FsrVerticalFov.reset();
                                Config::Instance()->FsrHorizontalFov.reset();
                                Config::Instance()->FsrCameraNear.reset();
                                Config::Instance()->FsrCameraFar.reset();
                            }

                            ImGui::SameLine(0.0f, 6.0f);
                            ImGui::Text(
                                "Near: %.1f Far: %.1f",
                                State::Instance().lastFsrCameraNear < 500000.0f ? State::Instance().lastFsrCameraNear
                                                                                : 500000.0f,
                                State::Instance().lastFsrCameraFar < 500000.0f ? State::Instance().lastFsrCameraFar
                                                                               : 500000.0f);

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }

                    // DLSS Enabler -----------------
                    if (State::Instance().enablerAvailable)
                    {
                        ImGui::SeparatorText("DLSS Enabler");

                        ImGui::BeginDisabled(Config::Instance()->DE_FramerateLimitVsync.value_or(false));

                        // set inital value
                        if (Config::Instance()->DE_FramerateLimit.has_value() && _deLimitFps > 200)
                            _deLimitFps = Config::Instance()->DE_FramerateLimit.value();

                        ImGui::SliderInt("FPS Limit", &_deLimitFps, 0, 200);

                        if (ImGui::Button("Apply Limit"))
                            Config::Instance()->DE_FramerateLimit = _deLimitFps;

                        ImGui::EndDisabled();

                        ImGui::SameLine(0.0f, 6.0f);

                        bool fpsLimitVsync = Config::Instance()->DE_FramerateLimitVsync.value_or(false);
                        if (ImGui::Checkbox("VSync", &fpsLimitVsync))
                            Config::Instance()->DE_FramerateLimitVsync = fpsLimitVsync;
                        ShowHelpMarker("Limit FPS to your monitor's refresh rate");

                        if (Config::Instance()->DE_DynamicLimitAvailable.has_value() &&
                            Config::Instance()->DE_DynamicLimitAvailable.value() > 0)
                        {
                            ImGui::SameLine(0.0f, 6.0f);

                            bool dfgEnabled = Config::Instance()->DE_DynamicLimitEnabled.value_or(false);
                            if (ImGui::Checkbox("Dynamic Frame Gen.", &dfgEnabled))
                                Config::Instance()->DE_DynamicLimitEnabled = dfgEnabled;
                            ShowHelpMarker("Enables FG only when FPS would go below FPS Limit.\n"
                                           "This will result in higher input latency in situation of low fps\n"
                                           "but motion smoothness will be preserved.");
                        }

                        if (ImGui::CollapsingHeader("Advanced Enabler Settings"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            std::string selected;

                            if (Config::Instance()->DE_Generator.value_or("auto") == "auto")
                                selected = "Auto";
                            else if (Config::Instance()->DE_Generator.value_or("auto") == "fsr30")
                                selected = "FSR3.0";
                            else if (Config::Instance()->DE_Generator.value_or("auto") == "fsr31")
                                selected = "FSR3.1";
                            else if (Config::Instance()->DE_Generator.value_or("auto") == "dlssg")
                                selected = "DLSS-G";

                            if (ImGui::BeginCombo("Generator", selected.c_str()))
                            {
                                if (ImGui::Selectable("Auto",
                                                      Config::Instance()->DE_Generator.value_or("auto") == "auto"))
                                    Config::Instance()->DE_Generator = "auto";

                                if (ImGui::Selectable("FSR3.0",
                                                      Config::Instance()->DE_Generator.value_or("auto") == "fsr30"))
                                    Config::Instance()->DE_Generator = "fsr30";

                                if (ImGui::Selectable("FSR3.1",
                                                      Config::Instance()->DE_Generator.value_or("auto") == "fsr31"))
                                    Config::Instance()->DE_Generator = "fsr31";

                                if (ImGui::Selectable("DLSS-G",
                                                      Config::Instance()->DE_Generator.value_or("auto") == "dlssg"))
                                    Config::Instance()->DE_Generator = "dlssg";

                                ImGui::EndCombo();
                            }
                            ShowHelpMarker("Frame Generation algorithm to be used");

                            if (Config::Instance()->DE_Reflex.value_or("on") == "on")
                                selected = "On";
                            else if (Config::Instance()->DE_Reflex.value_or("on") == "boost")
                                selected = "Boost";
                            else if (Config::Instance()->DE_Reflex.value_or("on") == "off")
                                selected = "Off";

                            if (ImGui::BeginCombo("Reflex", selected.c_str()))
                            {
                                if (ImGui::Selectable("On", Config::Instance()->DE_Reflex.value_or("on") == "on"))
                                    Config::Instance()->DE_Reflex = "on";

                                if (ImGui::Selectable("Boost", Config::Instance()->DE_Reflex.value_or("on") == "boost"))
                                    Config::Instance()->DE_Reflex = "boost";

                                if (ImGui::Selectable("Off", Config::Instance()->DE_Reflex.value_or("on") == "off"))
                                    Config::Instance()->DE_Reflex = "off";

                                ImGui::EndCombo();
                            }

                            if (Config::Instance()->DE_ReflexEmu.value_or("auto") == "auto")
                                selected = "Auto";
                            else if (Config::Instance()->DE_ReflexEmu.value_or("auto") == "on")
                                selected = "On";
                            else if (Config::Instance()->DE_ReflexEmu.value_or("auto") == "off")
                                selected = "Off";

                            if (ImGui::BeginCombo("Reflex Emu", selected.c_str()))
                            {
                                if (ImGui::Selectable("Auto",
                                                      Config::Instance()->DE_ReflexEmu.value_or("auto") == "auto"))
                                    Config::Instance()->DE_ReflexEmu = "auto";

                                if (ImGui::Selectable("On", Config::Instance()->DE_ReflexEmu.value_or("auto") == "on"))
                                    Config::Instance()->DE_ReflexEmu = "on";

                                if (ImGui::Selectable("Off",
                                                      Config::Instance()->DE_ReflexEmu.value_or("auto") == "off"))
                                    Config::Instance()->DE_ReflexEmu = "off";

                                ImGui::EndCombo();
                            }
                            ShowHelpMarker("Doesn't reduce the input latency\nbut may stabilize the frame rate");
                        }
                    }
                }

                // Framerate ---------------------
                if (!State::Instance().enablerAvailable &&
                    (State::Instance().reflexLimitsFps || Config::Instance()->OverlayMenu))
                {
                    SeparatorWithHelpMarker(
                        "Framerate",
                        "Uses Reflex when possible\non AMD/Intel cards you can use fakenvapi to substitute Reflex");

                    static std::string currentMethod {};
                    if (State::Instance().reflexLimitsFps)
                    {
                        if (fakenvapi::updateModeAndContext())
                        {
                            auto mode = fakenvapi::getCurrentMode();

                            if (mode == Mode::AntiLag2)
                                currentMethod = "AntiLag 2";
                            else if (mode == Mode::LatencyFlex)
                                currentMethod = "LatencyFlex";
                            else if (mode == Mode::XeLL)
                                currentMethod = "XeLL";
                            else if (mode == Mode::AntiLagVk)
                                currentMethod = "Vulkan AntiLag";

                            if (State::Instance().rtssReflexInjection && mode == Mode::AntiLag2 &&
                                Config::Instance()->FGInput == FGInput::Upscaler)
                                ImGui::TextColored(
                                    ImVec4(1.f, 0.8f, 0.f, 1.f),
                                    "Using RTSS Reflex injection with AntiLag 2 and OptiFG might cause issues");
                        }
                        else
                        {
                            currentMethod = "Reflex";
                        }
                    }
                    else
                    {
                        currentMethod = "Fallback";
                    }

                    if (State::Instance().rtssReflexInjection)
                        currentMethod.append(" (RTSS)");

                    ImGui::Text(std::format("Current method: {}", currentMethod).c_str());

                    if (State::Instance().reflexShowWarning)
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f),
                                           "Using Reflex's limit with OptiFG has performance overhead");

                        ImGui::Spacing();
                    }

                    // set initial value
                    if (_limitFps == INFINITY)
                        _limitFps = Config::Instance()->FramerateLimit.value_or_default();

                    ImGui::SliderFloat("FPS Limit", &_limitFps, 0, 200, "%.0f");

                    if (ImGui::Button("Apply Limit"))
                    {
                        Config::Instance()->FramerateLimit = _limitFps;
                    }
                }

                // FAKENVAPI ---------------------------
                if (fakenvapi::isUsingFakenvapi())
                {
                    ImGui::SeparatorText("fakenvapi");

                    if (bool logs = Config::Instance()->FN_EnableLogs.value_or_default();
                        ImGui::Checkbox("Enable Logging To File", &logs))
                        Config::Instance()->FN_EnableLogs = logs;

                    ImGui::BeginDisabled(!Config::Instance()->FN_EnableLogs.value_or_default());

                    ImGui::SameLine(0.0f, 6.0f);
                    if (bool traceLogs = Config::Instance()->FN_EnableTraceLogs.value_or_default();
                        ImGui::Checkbox("Enable Trace Logs", &traceLogs))
                        Config::Instance()->FN_EnableTraceLogs = traceLogs;

                    ImGui::EndDisabled();

                    if (bool forceLFX = Config::Instance()->FN_ForceLatencyFlex.value_or_default();
                        ImGui::Checkbox("Force LatencyFlex", &forceLFX))
                        Config::Instance()->FN_ForceLatencyFlex = forceLFX;
                    ShowHelpMarker(
                        "AntiLag 2 / XeLL is used when available, this setting let's you force LatencyFlex instead");

                    const char* lfx_modes[] = { "Conservative", "Aggressive", "Reflex ID" };
                    const std::string lfx_modesDesc[] = {
                        "The safest but might not reduce latency well",
                        "Improves latency but in some cases will lower fps more than expected",
                        "Best when can be used, some games are not compatible (i.e. cyberpunk) and will fallback to "
                        "aggressive"
                    };

                    PopulateCombo("LatencyFlex mode", &Config::Instance()->FN_LatencyFlexMode, lfx_modes, lfx_modesDesc,
                                  3);

                    const char* rfx_modes[] = { "Follow in-game", "Force Disable", "Force Enable" };
                    const std::string rfx_modesDesc[] = { "", "", "" };

                    PopulateCombo("Force Reflex", &Config::Instance()->FN_ForceReflex, rfx_modes, rfx_modesDesc, 3);

                    if (ImGui::Button("Apply##2"))
                    {
                        Config::Instance()->SaveFakenvapiIni();
                    }
                }

                // NEXT COLUMN -----------------
                ImGui::TableNextColumn();

                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    // SHARPNESS -----------------------------
                    ImGui::SeparatorText("Sharpness");

                    if (bool overrideSharpness = Config::Instance()->OverrideSharpness.value_or_default();
                        ImGui::Checkbox("Override", &overrideSharpness))
                    {
                        Config::Instance()->OverrideSharpness = overrideSharpness;

                        if (currentBackend == "dlss" && State::Instance().currentFeature->Version().major < 3)
                        {
                            State::Instance().newBackend = currentBackend;
                            MARK_ALL_BACKENDS_CHANGED();
                        }
                    }
                    ShowHelpMarker("Ignores the value sent by the game\n"
                                   "and uses the value set below");

                    ImGui::BeginDisabled(!Config::Instance()->OverrideSharpness.value_or_default());

                    float sharpness = Config::Instance()->Sharpness.value_or_default();
                    auto justRcasEnabled = Config::Instance()->RcasEnabled.value_or(rcasEnabled) &&
                                           !Config::Instance()->ContrastEnabled.value_or_default();
                    float sharpnessLimit = justRcasEnabled ? 1.3f : 1.0f;

                    if (ImGui::SliderFloat("Sharpness", &sharpness, 0.0f, sharpnessLimit))
                        Config::Instance()->Sharpness = sharpness;

                    ImGui::EndDisabled();

                    // RCAS
                    if (State::Instance().api == DX12 || State::Instance().api == DX11)
                    {
                        // xess or dlss version >= 2.5.1
                        constexpr feature_version requiredDlssVersion = { 2, 5, 1 };
                        rcasEnabled = (currentBackend == "xess" ||
                                       (currentBackend == "dlss" &&
                                        State::Instance().currentFeature->Version() >= requiredDlssVersion));

                        if (bool rcas = Config::Instance()->RcasEnabled.value_or(rcasEnabled);
                            ImGui::Checkbox("Enable RCAS", &rcas))
                            Config::Instance()->RcasEnabled = rcas;
                        ShowHelpMarker("A sharpening filter\n"
                                       "By default uses a sharpening value provided by the game\n"
                                       "Select 'Override' under 'Sharpness' and adjust the slider to change it\n\n"
                                       "Some upscalers have it's own sharpness filter so RCAS is not always needed");

                        ImGui::BeginDisabled(!Config::Instance()->RcasEnabled.value_or(rcasEnabled));

                        if (bool contrastEnabled = Config::Instance()->ContrastEnabled.value_or_default();
                            ImGui::Checkbox("Contrast Enabled", &contrastEnabled))
                            Config::Instance()->ContrastEnabled = contrastEnabled;

                        ShowHelpMarker("Increases sharpness at high contrast areas.");

                        if (Config::Instance()->ContrastEnabled.value_or_default() &&
                            Config::Instance()->Sharpness.value_or_default() > 1.0f)
                            Config::Instance()->Sharpness = 1.0f;

                        ImGui::BeginDisabled(!Config::Instance()->ContrastEnabled.value_or_default());

                        float contrast = Config::Instance()->Contrast.value_or_default();
                        if (ImGui::SliderFloat("Contrast", &contrast, 0.0f, 2.0f, "%.2f"))
                            Config::Instance()->Contrast = contrast;

                        ShowHelpMarker("Higher values increases sharpness at high contrast areas.\n"
                                       "High values might cause graphical GLITCHES \n"
                                       "when used with high sharpness values !!!");

                        ImGui::EndDisabled();

                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("Motion Adaptive Sharpness##2"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            if (bool overrideMotionSharpness =
                                    Config::Instance()->MotionSharpnessEnabled.value_or_default();
                                ImGui::Checkbox("Motion Adaptive Sharpness", &overrideMotionSharpness))
                                Config::Instance()->MotionSharpnessEnabled = overrideMotionSharpness;
                            ShowHelpMarker("Applies more sharpness to things in motion");

                            ImGui::BeginDisabled(!Config::Instance()->MotionSharpnessEnabled.value_or_default());

                            ImGui::SameLine(0.0f, 6.0f);

                            if (bool overrideMSDebug = Config::Instance()->MotionSharpnessDebug.value_or_default();
                                ImGui::Checkbox("MAS Debug", &overrideMSDebug))
                                Config::Instance()->MotionSharpnessDebug = overrideMSDebug;
                            ShowHelpMarker("Areas that are more red will have more sharpness applied\n"
                                           "Green areas will get reduced sharpness");

                            float motionSharpness = Config::Instance()->MotionSharpness.value_or_default();
                            ImGui::SliderFloat("MotionSharpness", &motionSharpness, -1.3f, 1.3f, "%.3f");
                            Config::Instance()->MotionSharpness = motionSharpness;

                            float motionThreshod = Config::Instance()->MotionThreshold.value_or_default();
                            ImGui::SliderFloat("MotionThreshod", &motionThreshod, 0.0f, 100.0f, "%.2f");
                            Config::Instance()->MotionThreshold = motionThreshod;

                            float motionScale = Config::Instance()->MotionScaleLimit.value_or_default();
                            ImGui::SliderFloat("MotionRange", &motionScale, 0.01f, 100.0f, "%.2f");
                            Config::Instance()->MotionScaleLimit = motionScale;

                            ImGui::EndDisabled();

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }

                        ImGui::EndDisabled();
                    }

                    // UPSCALE RATIO OVERRIDE -----------------

                    auto minSliderLimit = Config::Instance()->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
                    auto maxSliderLimit = Config::Instance()->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;

                    ImGui::SeparatorText("Upscale Ratio Override");

                    if (bool upOverride = Config::Instance()->UpscaleRatioOverrideEnabled.value_or_default();
                        ImGui::Checkbox("Override all", &upOverride))
                    {
                        Config::Instance()->UpscaleRatioOverrideEnabled = upOverride;

                        if (upOverride)
                            Config::Instance()->QualityRatioOverrideEnabled = false;
                    }
                    ShowHelpMarker("Let's you override every upscaler preset\n"
                                   "with a value set below\n\n"
                                   "1.5x on a 1080p screen means internal resolution of 720p\n"
                                   "1080 / 1.5 = 720");

                    if (bool qOverride = Config::Instance()->QualityRatioOverrideEnabled.value_or_default();
                        ImGui::Checkbox("Override per quality preset", &qOverride))
                    {
                        Config::Instance()->QualityRatioOverrideEnabled = qOverride;

                        if (qOverride)
                            Config::Instance()->UpscaleRatioOverrideEnabled = false;
                    }

                    ShowHelpMarker("Let's you override each preset's ratio individually\n"
                                   "Note that not every game supports every quality preset\n\n"
                                   "1.5x on a 1080p screen means internal resolution of 720p\n"
                                   "1080 / 1.5 = 720");

                    if (Config::Instance()->UpscaleRatioOverrideEnabled.value_or_default())
                    {
                        float urOverride = Config::Instance()->UpscaleRatioOverrideValue.value_or_default();
                        ImGui::SliderFloat("All Ratios", &urOverride, minSliderLimit, maxSliderLimit, "%.3f");
                        Config::Instance()->UpscaleRatioOverrideValue = urOverride;
                    }

                    if (Config::Instance()->QualityRatioOverrideEnabled.value_or_default())
                    {
                        float qDlaa = Config::Instance()->QualityRatio_DLAA.value_or_default();
                        if (ImGui::SliderFloat("DLAA", &qDlaa, minSliderLimit, maxSliderLimit, "%.3f"))
                            Config::Instance()->QualityRatio_DLAA = qDlaa;

                        float qUq = Config::Instance()->QualityRatio_UltraQuality.value_or_default();
                        if (ImGui::SliderFloat("Ultra Quality", &qUq, minSliderLimit, maxSliderLimit, "%.3f"))
                            Config::Instance()->QualityRatio_UltraQuality = qUq;

                        float qQ = Config::Instance()->QualityRatio_Quality.value_or_default();
                        if (ImGui::SliderFloat("Quality", &qQ, minSliderLimit, maxSliderLimit, "%.3f"))
                            Config::Instance()->QualityRatio_Quality = qQ;

                        float qB = Config::Instance()->QualityRatio_Balanced.value_or_default();
                        if (ImGui::SliderFloat("Balanced", &qB, minSliderLimit, maxSliderLimit, "%.3f"))
                            Config::Instance()->QualityRatio_Balanced = qB;

                        float qP = Config::Instance()->QualityRatio_Performance.value_or_default();
                        if (ImGui::SliderFloat("Performance", &qP, minSliderLimit, maxSliderLimit, "%.3f"))
                            Config::Instance()->QualityRatio_Performance = qP;

                        float qUp = Config::Instance()->QualityRatio_UltraPerformance.value_or_default();
                        if (ImGui::SliderFloat("Ultra Performance", &qUp, minSliderLimit, maxSliderLimit, "%.3f"))
                            Config::Instance()->QualityRatio_UltraPerformance = qUp;
                    }

                    if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        // OUTPUT SCALING -----------------------------
                        if (State::Instance().api == DX12 || State::Instance().api == DX11)
                        {
                            // if motion vectors are not display size
                            ImGui::BeginDisabled(!currentFeature->LowResMV());

                            ImGui::SeparatorText("Output Scaling");

                            float defaultRatio = 1.5f;

                            if (_ssRatio == 0.0f)
                            {
                                _ssRatio = Config::Instance()->OutputScalingMultiplier.value_or(defaultRatio);
                                _ssEnabled = Config::Instance()->OutputScalingEnabled.value_or_default();
                                _ssUseFsr = Config::Instance()->OutputScalingUseFsr.value_or_default();
                                _ssDownsampler = Config::Instance()->OutputScalingDownscaler.value_or_default();
                            }

                            ImGui::BeginDisabled((currentBackend == "xess" || currentBackend == "dlss") &&
                                                 State::Instance().currentFeature->RenderWidth() >
                                                     State::Instance().currentFeature->DisplayWidth());
                            ImGui::Checkbox("Enable", &_ssEnabled);
                            ImGui::EndDisabled();

                            ShowHelpMarker("Upscales the image internally to a selected resolution\n"
                                           "Then scales it to your resolution\n\n"
                                           "Values <1.0 might make the upscaler cheaper\n"
                                           "Values >1.0 might make image sharper at the cost of performance\n\n"
                                           "You can see each step at the bottom of this menu");

                            ImGui::SameLine(0.0f, 6.0f);

                            ImGui::BeginDisabled(!_ssEnabled);
                            {
                                ImGui::Checkbox("Use FSR 1", &_ssUseFsr);
                                ShowHelpMarker("Use FSR 1 for scaling");

                                ImGui::SameLine(0.0f, 6.0f);

                                ImGui::BeginDisabled(_ssUseFsr || _ssRatio < 1.0f);
                                {
                                    const char* ds_modes[] = { "Bicubic", "Lanczos", "Catmull-Rom", "MAGC" };
                                    const std::string ds_modesDesc[] = { "", "", "", "" };

                                    ImGui::PushItemWidth(75.0f * Config::Instance()->MenuScale.value());
                                    PopulateCombo("Downscaler", &Config::Instance()->OutputScalingDownscaler, ds_modes,
                                                  ds_modesDesc, 4);
                                    ImGui::PopItemWidth();
                                }
                                ImGui::EndDisabled();
                            }
                            ImGui::EndDisabled();

                            bool applyEnabled =
                                _ssEnabled != Config::Instance()->OutputScalingEnabled.value_or_default() ||
                                _ssRatio != Config::Instance()->OutputScalingMultiplier.value_or(defaultRatio) ||
                                _ssUseFsr != Config::Instance()->OutputScalingUseFsr.value_or_default() ||
                                (_ssRatio > 1.0f &&
                                 _ssDownsampler != Config::Instance()->OutputScalingDownscaler.value_or_default());

                            ImGui::BeginDisabled(!applyEnabled);
                            if (ImGui::Button("Apply Change"))
                            {
                                Config::Instance()->OutputScalingEnabled = _ssEnabled;
                                Config::Instance()->OutputScalingMultiplier = _ssRatio;
                                Config::Instance()->OutputScalingUseFsr = _ssUseFsr;
                                _ssDownsampler = Config::Instance()->OutputScalingDownscaler.value_or_default();

                                if (State::Instance().currentFeature->Name() == "DLSSD")
                                    State::Instance().newBackend = "dlssd";
                                else
                                    State::Instance().newBackend = currentBackend;

                                MARK_ALL_BACKENDS_CHANGED();
                            }
                            ImGui::EndDisabled();

                            ImGui::BeginDisabled(!_ssEnabled || State::Instance().currentFeature->RenderWidth() >
                                                                    State::Instance().currentFeature->DisplayWidth());
                            ImGui::SliderFloat("Ratio", &_ssRatio, 0.5f, 3.0f, "%.2f");
                            ImGui::EndDisabled();

                            if (currentFeature != nullptr && !currentFeature->IsFrozen())
                            {
                                ImGui::Text("Output Scaling is %s, Target Res: %dx%d\nJitter Count: %d",
                                            Config::Instance()->OutputScalingEnabled.value_or_default() ? "ENABLED"
                                                                                                        : "DISABLED",
                                            (uint32_t) (currentFeature->DisplayWidth() * _ssRatio),
                                            (uint32_t) (currentFeature->DisplayHeight() * _ssRatio),
                                            currentFeature->JitterCount());
                            }

                            ImGui::EndDisabled();
                        }
                    }

                    // INIT -----------------------------
                    ImGui::SeparatorText("Init Flags");
                    if (ImGui::BeginTable("init", 2, ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableNextColumn();

                        // AutoExposure is always enabled for XeSS with native Dx11
                        bool autoExposureDisabled = State::Instance().api == API::DX11 && currentBackend == "xess";
                        ImGui::BeginDisabled(autoExposureDisabled);

                        if (bool autoExposure = currentFeature->AutoExposure();
                            ImGui::Checkbox("Auto Exposure", &autoExposure))
                        {
                            Config::Instance()->AutoExposure = autoExposure;
                            ReInitUpscaler();
                        }
                        ShowResetButton(&Config::Instance()->AutoExposure, "R");
                        ShowHelpMarker("Some Unreal Engine games need this\n"
                                       "Might fix colors, especially in dark areas");

                        ImGui::EndDisabled();

                        ImGui::TableNextColumn();
                        auto accessToReactiveMask = State::Instance().currentFeature->AccessToReactiveMask();
                        ImGui::BeginDisabled(!accessToReactiveMask);

                        bool canUseReactiveMask =
                            accessToReactiveMask && currentBackend != "dlss" &&
                            (currentBackend != "xess" || currentFeature->Version() >= feature_version { 2, 0, 1 });

                        bool disableReactiveMask =
                            Config::Instance()->DisableReactiveMask.value_or(!canUseReactiveMask);

                        if (ImGui::Checkbox("Disable Reactive Mask", &disableReactiveMask))
                        {
                            Config::Instance()->DisableReactiveMask = disableReactiveMask;

                            if (currentBackend == "xess")
                            {
                                State::Instance().newBackend = currentBackend;
                                MARK_ALL_BACKENDS_CHANGED();
                            }
                        }

                        ImGui::EndDisabled();

                        if (accessToReactiveMask)
                            ShowHelpMarker("Allows the use of a reactive mask\n"
                                           "Keep in mind that a reactive mask sent to DLSS\n"
                                           "will not produce a good image in combination with FSR/XeSS");
                        else
                            ShowHelpMarker("Option disabled because tha game doesn't provide a reactive mask");

                        ImGui::EndTable();

                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("Advanced Init Flags"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            if (ImGui::BeginTable("init2", 2, ImGuiTableFlags_SizingStretchProp))
                            {
                                ImGui::TableNextColumn();
                                if (bool depth = currentFeature->DepthInverted();
                                    ImGui::Checkbox("Depth Inverted", &depth))
                                {
                                    Config::Instance()->DepthInverted = depth;
                                    ReInitUpscaler();
                                }
                                ShowResetButton(&Config::Instance()->DepthInverted, "R##2");
                                ShowHelpMarker("You shouldn't need to change it");

                                ImGui::TableNextColumn();
                                if (bool hdr = currentFeature->IsHdr(); ImGui::Checkbox("HDR", &hdr))
                                {
                                    Config::Instance()->HDR = hdr;
                                    ReInitUpscaler();
                                }
                                ShowResetButton(&Config::Instance()->HDR, "R##1");
                                ShowHelpMarker("Might help with purple hue in some games");

                                ImGui::TableNextColumn();
                                if (bool mv = !currentFeature->LowResMV(); ImGui::Checkbox("Display Res. MV", &mv))
                                {
                                    Config::Instance()->DisplayResolution = mv;

                                    // Disable output scaling when
                                    // Display res MV is active
                                    if (mv)
                                    {
                                        Config::Instance()->OutputScalingEnabled = false;
                                        _ssEnabled = false;
                                    }

                                    ReInitUpscaler();
                                }
                                ShowResetButton(&Config::Instance()->DisplayResolution, "R##4");
                                ShowHelpMarker("Mostly a fix for Unreal Engine games\n"
                                               "Top left part of the screen will be blurry");

                                ImGui::TableNextColumn();

                                if (bool jitter = currentFeature->JitteredMV();
                                    ImGui::Checkbox("Jitter Cancellation", &jitter))
                                {
                                    Config::Instance()->JitterCancellation = jitter;
                                    ReInitUpscaler();
                                }
                                ShowResetButton(&Config::Instance()->JitterCancellation, "R##3");
                                ShowHelpMarker("Fix for games that send motion data with preapplied jitter");

                                ImGui::TableNextColumn();
                                ImGui::EndTable();
                            }

                            if (State::Instance().currentFeature->AccessToReactiveMask() && currentBackend != "dlss")
                            {
                                ImGui::BeginDisabled(
                                    Config::Instance()->DisableReactiveMask.value_or(currentBackend == "xess"));

                                bool binaryMask = State::Instance().api == Vulkan || currentBackend == "xess";
                                auto defaultBias = binaryMask ? 0.0f : 0.45f;
                                auto maskBias = Config::Instance()->DlssReactiveMaskBias.value_or(defaultBias);

                                if (!binaryMask)
                                {
                                    if (ImGui::SliderFloat("React. Mask Bias", &maskBias, 0.0f, 0.9f, "%.2f"))
                                        Config::Instance()->DlssReactiveMaskBias = maskBias;

                                    ShowHelpMarker("Values above 0 activates usage of reactive mask");
                                }
                                else
                                {
                                    bool useRM = maskBias > 0.0f;
                                    if (ImGui::Checkbox("Use Binary Reactive Mask", &useRM))
                                    {
                                        if (useRM)
                                            Config::Instance()->DlssReactiveMaskBias = 0.45f;
                                        else
                                            Config::Instance()->DlssReactiveMaskBias.reset();
                                    }
                                }

                                ImGui::EndDisabled();
                            }
                        }
                    }
                }

                // ADVANCED SETTINGS -----------------------------
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Advanced Settings"))
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        bool extendedLimits = Config::Instance()->ExtendedLimits.value_or_default();
                        if (ImGui::Checkbox("Enable Extended Limits", &extendedLimits))
                            Config::Instance()->ExtendedLimits = extendedLimits;

                        ShowHelpMarker("Extended sliders limit for quality presets\n\n"
                                       "Using this option changes resolution detection logic\n"
                                       "and might cause issues and crashes!");
                    }

                    bool pcShaders = Config::Instance()->UsePrecompiledShaders.value_or_default();
                    if (ImGui::Checkbox("Use Precompiled Shaders", &pcShaders))
                    {
                        Config::Instance()->UsePrecompiledShaders = pcShaders;
                        State::Instance().newBackend = currentBackend;
                        MARK_ALL_BACKENDS_CHANGED();
                    }

                    // DRS
                    ImGui::SeparatorText("DRS (Dynamic Resolution Scaling)");
                    if (ImGui::BeginTable("drs", 2, ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableNextColumn();
                        if (bool drsMin = Config::Instance()->DrsMinOverrideEnabled.value_or_default();
                            ImGui::Checkbox("Override Minimum", &drsMin))
                            Config::Instance()->DrsMinOverrideEnabled = drsMin;
                        ShowHelpMarker("Fix for games ignoring official DRS limits");

                        ImGui::TableNextColumn();
                        if (bool drsMax = Config::Instance()->DrsMaxOverrideEnabled.value_or_default();
                            ImGui::Checkbox("Override Maximum", &drsMax))
                            Config::Instance()->DrsMaxOverrideEnabled = drsMax;
                        ShowHelpMarker("Fix for games ignoring official DRS limits");

                        ImGui::EndTable();
                    }

                    // Non-DLSS hotfixes -----------------------------
                    if (currentFeature != nullptr && !currentFeature->IsFrozen() && currentBackend != "dlss")
                    {
                        // BARRIERS -----------------------------
                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("Resource Barriers"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            AddResourceBarrier("Color", &Config::Instance()->ColorResourceBarrier);
                            AddResourceBarrier("Depth", &Config::Instance()->DepthResourceBarrier);
                            AddResourceBarrier("Motion", &Config::Instance()->MVResourceBarrier);
                            AddResourceBarrier("Exposure", &Config::Instance()->ExposureResourceBarrier);
                            AddResourceBarrier("Mask", &Config::Instance()->MaskResourceBarrier);
                            AddResourceBarrier("Output", &Config::Instance()->OutputResourceBarrier);
                        }

                        // HOTFIXES -----------------------------
                        if (State::Instance().api == DX12)
                        {
                            ImGui::Spacing();
                            if (ImGui::CollapsingHeader("Root Signatures"))
                            {
                                ScopedIndent indent {};
                                ImGui::Spacing();

                                if (bool crs = Config::Instance()->RestoreComputeSignature.value_or_default();
                                    ImGui::Checkbox("Restore Compute Root Signature", &crs))
                                    Config::Instance()->RestoreComputeSignature = crs;

                                if (bool grs = Config::Instance()->RestoreGraphicSignature.value_or_default();
                                    ImGui::Checkbox("Restore Graphic Root Signature", &grs))
                                    Config::Instance()->RestoreGraphicSignature = grs;
                            }
                        }
                    }
                }

                // LOGGING -----------------------------
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Logging"))
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (Config::Instance()->LogToConsole.value_or_default() ||
                        Config::Instance()->LogToFile.value_or_default() ||
                        Config::Instance()->LogToNGX.value_or_default())
                        spdlog::default_logger()->set_level(
                            (spdlog::level::level_enum) Config::Instance()->LogLevel.value_or_default());
                    else
                        spdlog::default_logger()->set_level(spdlog::level::off);

                    if (bool toFile = Config::Instance()->LogToFile.value_or_default();
                        ImGui::Checkbox("To File", &toFile))
                    {
                        Config::Instance()->LogToFile = toFile;
                        PrepareLogger();
                    }

                    ImGui::SameLine(0.0f, 6.0f);
                    if (bool toConsole = Config::Instance()->LogToConsole.value_or_default();
                        ImGui::Checkbox("To Console", &toConsole))
                    {
                        Config::Instance()->LogToConsole = toConsole;
                        PrepareLogger();
                    }

                    const char* logLevels[] = { "Trace", "Debug", "Information", "Warning", "Error" };
                    const char* selectedLevel = logLevels[Config::Instance()->LogLevel.value_or_default()];

                    if (ImGui::BeginCombo("Log Level", selectedLevel))
                    {
                        for (int n = 0; n < 5; n++)
                        {
                            if (ImGui::Selectable(logLevels[n], (Config::Instance()->LogLevel.value_or_default() == n)))
                            {
                                Config::Instance()->LogLevel = n;
                                spdlog::default_logger()->set_level(
                                    (spdlog::level::level_enum) Config::Instance()->LogLevel.value_or_default());
                            }
                        }

                        ImGui::EndCombo();
                    }
                }

                // FPS OVERLAY -----------------------------
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("FPS Overlay"))
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    bool fpsEnabled = Config::Instance()->ShowFps.value_or_default();
                    if (ImGui::Checkbox("FPS Overlay Enabled", &fpsEnabled))
                        Config::Instance()->ShowFps = fpsEnabled;

                    ImGui::SameLine(0.0f, 6.0f);

                    bool fpsHorizontal = Config::Instance()->FpsOverlayHorizontal.value_or_default();
                    if (ImGui::Checkbox("Horizontal", &fpsHorizontal))
                        Config::Instance()->FpsOverlayHorizontal = fpsHorizontal;

                    const char* fpsPosition[] = { "Top Left", "Top Right", "Bottom Left", "Bottom Right" };
                    const char* selectedPosition = fpsPosition[Config::Instance()->FpsOverlayPos.value_or_default()];

                    if (ImGui::BeginCombo("Overlay Position", selectedPosition))
                    {
                        for (int n = 0; n < 4; n++)
                        {
                            if (ImGui::Selectable(fpsPosition[n],
                                                  (Config::Instance()->FpsOverlayPos.value_or_default() == n)))
                                Config::Instance()->FpsOverlayPos = n;
                        }

                        ImGui::EndCombo();
                    }

                    const char* fpsType[] = { "Just FPS",         "Simple", "Detailed",
                                              "Detailed + Graph", "Full",   "Full + Graph" };
                    const char* selectedType = fpsType[Config::Instance()->FpsOverlayType.value_or_default()];

                    if (ImGui::BeginCombo("Overlay Type", selectedType))
                    {
                        for (int n = 0; n < 6; n++)
                        {
                            if (ImGui::Selectable(fpsType[n],
                                                  (Config::Instance()->FpsOverlayType.value_or_default() == n)))
                                Config::Instance()->FpsOverlayType = n;
                        }

                        ImGui::EndCombo();
                    }

                    float fpsAlpha = Config::Instance()->FpsOverlayAlpha.value_or_default();
                    if (ImGui::SliderFloat("Background Alpha", &fpsAlpha, 0.0f, 1.0f, "%.2f"))
                        Config::Instance()->FpsOverlayAlpha = fpsAlpha;

                    const char* options[] = { "Same as menu", "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1", "1.2",
                                              "1.3",          "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
                    int currentIndex = std::max(((int) (Config::Instance()->FpsScale.value_or(0.0f) * 10.0f)) - 4, 0);
                    float values[] = { 0.0f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f,
                                       1.3f, 1.4f, 1.5f, 1.6f, 1.7f, 1.8f, 1.9f, 2.0f };

                    if (ImGui::SliderInt("Scale", &currentIndex, 0, IM_ARRAYSIZE(options) - 1, options[currentIndex],
                                         ImGuiSliderFlags_ClampOnInput))
                    {
                        if (currentIndex == 0)
                        {
                            Config::Instance()->FpsScale.reset();
                        }
                        else
                        {
                            Config::Instance()->FpsScale = values[currentIndex];
                        }
                    }
                }

                // UPSCALER INPUTS -----------------------------
                ImGui::Spacing();
                auto uiStateOpen = currentFeature == nullptr || currentFeature->IsFrozen();
                if (ImGui::CollapsingHeader("Upscaler Inputs", uiStateOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0))
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (Config::Instance()->EnableFsr2Inputs.value_or_default())
                    {
                        bool fsr2Inputs = Config::Instance()->UseFsr2Inputs.value_or_default();
                        bool fsr2Pattern = Config::Instance()->Fsr2Pattern.value_or_default();

                        if (ImGui::Checkbox("Use Fsr2 Inputs", &fsr2Inputs))
                            Config::Instance()->UseFsr2Inputs = fsr2Inputs;

                        if (ImGui::Checkbox("Use Fsr2 Pattern Matching", &fsr2Pattern))
                            Config::Instance()->Fsr2Pattern = fsr2Pattern;
                        ShowTooltip("This setting will become active on next boot!");
                    }

                    if (Config::Instance()->EnableFsr3Inputs.value_or_default())
                    {
                        bool fsr3Inputs = Config::Instance()->UseFsr3Inputs.value_or_default();
                        bool fsr3Pattern = Config::Instance()->Fsr3Pattern.value_or_default();

                        if (ImGui::Checkbox("Use Fsr3 Inputs", &fsr3Inputs))
                            Config::Instance()->UseFsr3Inputs = fsr3Inputs;

                        if (ImGui::Checkbox("Use Fsr3 Pattern Matching", &fsr3Pattern))
                            Config::Instance()->Fsr3Pattern = fsr3Pattern;
                        ShowTooltip("This setting will become active on next boot!");
                    }

                    if (Config::Instance()->EnableFfxInputs.value_or_default())
                    {
                        bool ffxInputs = Config::Instance()->UseFfxInputs.value_or_default();

                        if (ImGui::Checkbox("Use Ffx Inputs", &ffxInputs))
                            Config::Instance()->UseFfxInputs = ffxInputs;
                    }
                }

                // DX11 & DX12 -----------------------------
                if (State::Instance().api != Vulkan)
                {
                    // MIPMAP BIAS & Anisotropy -----------------------------
                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("Mipmap Bias", (currentFeature == nullptr || currentFeature->IsFrozen())
                                                                   ? ImGuiTreeNodeFlags_DefaultOpen
                                                                   : 0))
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();
                        if (Config::Instance()->MipmapBiasOverride.has_value() && _mipBias == 0.0f)
                            _mipBias = Config::Instance()->MipmapBiasOverride.value();

                        ImGui::SliderFloat("Mipmap Bias##2", &_mipBias, -15.0f, 15.0f, "%.6f");
                        ShowHelpMarker("Can help with blurry textures in broken games\n"
                                       "Negative values will make textures sharper\n"
                                       "Positive values will make textures more blurry\n\n"
                                       "Has a small performance impact");

                        ImGui::BeginDisabled(!Config::Instance()->MipmapBiasOverride.has_value());
                        {
                            ImGui::BeginDisabled(Config::Instance()->MipmapBiasScaleOverride.has_value() &&
                                                 Config::Instance()->MipmapBiasScaleOverride.value());
                            {
                                bool mbFixed = Config::Instance()->MipmapBiasFixedOverride.value_or_default();
                                if (ImGui::Checkbox("MB Fixed Override", &mbFixed))
                                {
                                    Config::Instance()->MipmapBiasScaleOverride.reset();
                                    Config::Instance()->MipmapBiasFixedOverride = mbFixed;
                                }

                                ShowHelpMarker("Apply same override value to all textures");
                            }
                            ImGui::EndDisabled();

                            ImGui::SameLine(0.0f, 6.0f);

                            ImGui::BeginDisabled(Config::Instance()->MipmapBiasFixedOverride.has_value() &&
                                                 Config::Instance()->MipmapBiasFixedOverride.value());
                            {
                                bool mbScale = Config::Instance()->MipmapBiasScaleOverride.value_or_default();
                                if (ImGui::Checkbox("MB Scale Override", &mbScale))
                                {
                                    Config::Instance()->MipmapBiasFixedOverride.reset();
                                    Config::Instance()->MipmapBiasScaleOverride = mbScale;
                                }

                                ShowHelpMarker("Apply override value as scale multiplier\n"
                                               "When using scale mode please use positive\n"
                                               "override values to increase sharpness!");
                            }
                            ImGui::EndDisabled();

                            bool mbAll = Config::Instance()->MipmapBiasOverrideAll.value_or_default();
                            if (ImGui::Checkbox("MB Override All Textures", &mbAll))
                                Config::Instance()->MipmapBiasOverrideAll = mbAll;

                            ShowHelpMarker("Override all textures mipmap values\n"
                                           "Normally OptiScaler only overrides\n"
                                           "below zero mipmap values!");
                        }
                        ImGui::EndDisabled();

                        ImGui::BeginDisabled(Config::Instance()->MipmapBiasOverride.has_value() &&
                                             Config::Instance()->MipmapBiasOverride.value() == _mipBias);
                        {
                            if (ImGui::Button("Set"))
                            {
                                Config::Instance()->MipmapBiasOverride = _mipBias;
                                State::Instance().lastMipBias = 100.0f;
                                State::Instance().lastMipBiasMax = -100.0f;
                            }
                        }
                        ImGui::EndDisabled();

                        ImGui::SameLine(0.0f, 6.0f);

                        ImGui::BeginDisabled(!Config::Instance()->MipmapBiasOverride.has_value());
                        {
                            if (ImGui::Button("Reset"))
                            {
                                Config::Instance()->MipmapBiasOverride.reset();
                                _mipBias = 0.0f;
                                State::Instance().lastMipBias = 100.0f;
                                State::Instance().lastMipBiasMax = -100.0f;
                            }
                        }
                        ImGui::EndDisabled();

                        if (currentFeature != nullptr && !currentFeature->IsFrozen())
                        {
                            ImGui::SameLine(0.0f, 6.0f);

                            if (ImGui::Button("Calculate Mipmap Bias"))
                                _showMipmapCalcWindow = true;
                        }

                        if (Config::Instance()->MipmapBiasOverride.has_value())
                        {
                            if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                            {
                                ImGui::Text("Current : %.3f / %.3f, Target: %.3f", State::Instance().lastMipBias,
                                            State::Instance().lastMipBiasMax,
                                            Config::Instance()->MipmapBiasOverride.value());
                            }
                            else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                            {
                                ImGui::Text("Current : %.3f / %.3f, Target: Base * %.3f", State::Instance().lastMipBias,
                                            State::Instance().lastMipBiasMax,
                                            Config::Instance()->MipmapBiasOverride.value());
                            }
                            else
                            {
                                ImGui::Text("Current : %.3f / %.3f, Target: Base + %.3f", State::Instance().lastMipBias,
                                            State::Instance().lastMipBiasMax,
                                            Config::Instance()->MipmapBiasOverride.value());
                            }
                        }
                        else
                        {
                            ImGui::Text("Current : %.3f / %.3f", State::Instance().lastMipBias,
                                        State::Instance().lastMipBiasMax);
                        }

                        ImGui::Text("Will be applied after RESOLUTION/PRESET change !!!");
                    }

                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("Anisotropic Filtering",
                                                (currentFeature == nullptr || currentFeature->IsFrozen())
                                                    ? ImGuiTreeNodeFlags_DefaultOpen
                                                    : 0))
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();
                        ImGui::PushItemWidth(65.0f * Config::Instance()->MenuScale.value());

                        auto selectedAF = Config::Instance()->AnisotropyOverride.has_value()
                                              ? std::to_string(Config::Instance()->AnisotropyOverride.value())
                                              : "Auto";
                        if (ImGui::BeginCombo("Force Anisotropic Filtering", selectedAF.c_str()))
                        {
                            if (ImGui::Selectable("Auto", !Config::Instance()->AnisotropyOverride.has_value()))
                                Config::Instance()->AnisotropyOverride.reset();

                            if (ImGui::Selectable("1", Config::Instance()->AnisotropyOverride.value_or(0) == 1))
                                Config::Instance()->AnisotropyOverride = 1;

                            if (ImGui::Selectable("2", Config::Instance()->AnisotropyOverride.value_or(0) == 2))
                                Config::Instance()->AnisotropyOverride = 2;

                            if (ImGui::Selectable("4", Config::Instance()->AnisotropyOverride.value_or(0) == 4))
                                Config::Instance()->AnisotropyOverride = 4;

                            if (ImGui::Selectable("8", Config::Instance()->AnisotropyOverride.value_or(0) == 8))
                                Config::Instance()->AnisotropyOverride = 8;

                            if (ImGui::Selectable("16", Config::Instance()->AnisotropyOverride.value_or(0) == 16))
                                Config::Instance()->AnisotropyOverride = 16;

                            ImGui::EndCombo();
                        }

                        ImGui::PopItemWidth();

                        ImGui::Text("Will be applied after RESOLUTION/PRESET change !!!");
                    }
                }

                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Keybinds"))
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    ImGui::Text("Key combinations are currently NOT supported!");
                    ImGui::Text("Escape to cancel, Backspace to unbind");
                    ImGui::Spacing();

                    static auto menu = Keybind("Menu", 10);
                    static auto fpsOverlay = Keybind("FPS Overlay", 11);
                    static auto fpsOverlayCycle = Keybind("FPS Overlay Cycle", 12);

                    menu.Render(Config::Instance()->ShortcutKey);
                    fpsOverlay.Render(Config::Instance()->FpsShortcutKey);
                    fpsOverlayCycle.Render(Config::Instance()->FpsCycleShortcutKey);
                }

                ImGui::EndTable();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::BeginTable("plots", 2, ImGuiTableFlags_SizingStretchSame))
                {
                    ImGui::TableNextColumn();
                    ImGui::Text("FrameTime");
                    State::Instance().frameTimeMutex.lock();
                    auto ft = std::format("{:6.2f} ms / {:5.1f} fps", State::Instance().frameTimes.back(), frameRate);
                    std::vector<float> frameTimeArray(State::Instance().frameTimes.begin(),
                                                      State::Instance().frameTimes.end());
                    ImGui::PlotLines(ft.c_str(), frameTimeArray.data(), (int) frameTimeArray.size());

                    if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        ImGui::TableNextColumn();
                        ImGui::Text("Upscaler");
                        auto ups = std::format("{:7.4f} ms", State::Instance().upscaleTimes.back());
                        std::vector<float> upscaleTimeArray(State::Instance().upscaleTimes.begin(),
                                                            State::Instance().upscaleTimes.end());
                        ImGui::PlotLines(ups.c_str(), upscaleTimeArray.data(), (int) upscaleTimeArray.size());
                    }
                    State::Instance().frameTimeMutex.unlock();

                    ImGui::EndTable();
                }

                // BOTTOM LINE ---------------
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    ImGui::Text("%dx%d -> %dx%d (%.1f) [%dx%d (%.1f)]", currentFeature->RenderWidth(),
                                currentFeature->RenderHeight(), currentFeature->TargetWidth(),
                                currentFeature->TargetHeight(),
                                (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth(),
                                currentFeature->DisplayWidth(), currentFeature->DisplayHeight(),
                                (float) currentFeature->DisplayWidth() / (float) currentFeature->RenderWidth());

                    ImGui::SameLine(0.0f, 4.0f);

                    ImGui::Text("%d", State::Instance().currentFeature->FrameCount());

                    ImGui::SameLine(0.0f, 10.0f);
                }

                ImGui::PushItemWidth(55.0f * Config::Instance()->MenuScale.value());

                const char* uiScales[] = { "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1", "1.2",
                                           "1.3", "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
                const char* selectedScaleName = uiScales[_selectedScale];

                if (ImGui::BeginCombo("Menu UI Scale", selectedScaleName))
                {
                    for (int n = 0; n < 16; n++)
                    {
                        if (ImGui::Selectable(uiScales[n], (_selectedScale == n)))
                        {
                            _selectedScale = n;
                            Config::Instance()->MenuScale = 0.5f + (float) n / 10.0f;

                            ImGuiStyle& style = ImGui::GetStyle();
                            style.ScaleAllSizes(Config::Instance()->MenuScale.value());

                            if (Config::Instance()->MenuScale.value() < 1.0f)
                                style.MouseCursorScale = 1.0f;

                            _imguiSizeUpdate = true;
                        }
                    }

                    ImGui::EndCombo();
                }

                ImGui::PopItemWidth();

                ImGui::SameLine(0.0f, 15.0f);

                if (ImGui::Button("Save INI"))
                    Config::Instance()->SaveIni();

                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::Button("Close"))
                {
                    _isVisible = false;
                    hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
                    io.BackendFlags &= 30;
                    io.ConfigFlags =
                        ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

                    if (pfn_ClipCursor_hooked)
                        pfn_ClipCursor(&_cursorLimit);

                    _showMipmapCalcWindow = false;
                    io.MouseDrawCursor = false;
                    io.WantCaptureKeyboard = false;
                    io.WantCaptureMouse = false;
                }

                ImGui::Spacing();
                ImGui::Separator();

                if (State::Instance().nvngxIniDetected)
                {
                    ImGui::Spacing();
                    ImGui::TextColored(
                        ImVec4(1.f, 0.f, 0.f, 1.f),
                        "nvngx.ini detected, please move over to using OptiScaler.ini and delete the old config");
                    ImGui::Spacing();
                }

                auto winSize = ImGui::GetWindowSize();
                auto winPos = ImGui::GetWindowPos();

                if (winPos.x == 60.0 && winSize.x > 100)
                {
                    float posX;
                    float posY;

                    if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        posX = ((float) State::Instance().currentFeature->DisplayWidth() - winSize.x) / 2.0f;
                        posY = ((float) State::Instance().currentFeature->DisplayHeight() - winSize.y) / 2.0f;
                    }
                    else
                    {
                        posX = ((float) State::Instance().screenWidth - winSize.x) / 2.0f;
                        posY = ((float) State::Instance().screenHeight - winSize.x) / 2.0f;
                    }

                    // don't position menu outside of screen
                    if (posX < 0.0 || posY < 0.0)
                    {
                        posX = 50;
                        posY = 50;
                    }

                    ImGui::SetWindowPos(ImVec2 { posX, posY });
                }

                ImGui::End();
            }

            // Mipmap calculation window
            if (_showMipmapCalcWindow && currentFeature != nullptr && !currentFeature->IsFrozen() &&
                currentFeature->IsInited())
            {
                auto posX = (State::Instance().currentFeature->DisplayWidth() - 450.0f) / 2.0f;
                auto posY = (State::Instance().currentFeature->DisplayHeight() - 200.0f) / 2.0f;

                ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2 { 450.0f, 200.0f }, ImGuiCond_FirstUseEver);

                if (_displayWidth == 0)
                {
                    if (Config::Instance()->OutputScalingEnabled.value_or_default())
                        _displayWidth = State::Instance().currentFeature->DisplayWidth() *
                                        Config::Instance()->OutputScalingMultiplier.value_or_default();
                    else
                        _displayWidth = State::Instance().currentFeature->DisplayWidth();

                    _renderWidth = _displayWidth / 3.0f;
                    _mipmapUpscalerQuality = 0;
                    _mipmapUpscalerRatio = 3.0f;
                    _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                }

                if (ImGui::Begin("Mipmap Bias", nullptr, flags))
                {
                    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                        ImGui::SetWindowFocus();

                    if (ImGui::InputScalar("Display Width", ImGuiDataType_U32, &_displayWidth, NULL, NULL, "%u"))
                    {
                        if (_displayWidth <= 0)
                        {
                            if (Config::Instance()->OutputScalingEnabled.value_or_default())
                                _displayWidth = State::Instance().currentFeature->DisplayWidth() *
                                                Config::Instance()->OutputScalingMultiplier.value_or_default();
                            else
                                _displayWidth = State::Instance().currentFeature->DisplayWidth();
                        }

                        _renderWidth = _displayWidth / _mipmapUpscalerRatio;
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                    }

                    const char* q[] = { "Ultra Performance", "Performance",   "Balanced",
                                        "Quality",           "Ultra Quality", "DLAA" };
                    float fr[] = { 3.0f, 2.0f, 1.7f, 1.5f, 1.3f, 1.0f };
                    auto configQ = _mipmapUpscalerQuality;

                    const char* selectedQ = q[configQ];

                    ImGui::BeginDisabled(Config::Instance()->UpscaleRatioOverrideEnabled.value_or_default());

                    if (ImGui::BeginCombo("Upscaler Quality", selectedQ))
                    {
                        for (int n = 0; n < 6; n++)
                        {
                            if (ImGui::Selectable(q[n], (_mipmapUpscalerQuality == n)))
                            {
                                _mipmapUpscalerQuality = n;

                                float ov = -1.0f;

                                if (Config::Instance()->QualityRatioOverrideEnabled.value_or_default())
                                {
                                    switch (n)
                                    {
                                    case 0:
                                        ov = Config::Instance()->QualityRatio_UltraPerformance.value_or(-1.0f);
                                        break;

                                    case 1:
                                        ov = Config::Instance()->QualityRatio_Performance.value_or(-1.0f);
                                        break;

                                    case 2:
                                        ov = Config::Instance()->QualityRatio_Balanced.value_or(-1.0f);
                                        break;

                                    case 3:
                                        ov = Config::Instance()->QualityRatio_Quality.value_or(-1.0f);
                                        break;

                                    case 4:
                                        ov = Config::Instance()->QualityRatio_UltraQuality.value_or(-1.0f);
                                        break;
                                    }
                                }

                                if (ov > 0.0f)
                                    _mipmapUpscalerRatio = ov;
                                else
                                    _mipmapUpscalerRatio = fr[n];

                                _renderWidth = _displayWidth / _mipmapUpscalerRatio;
                                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                            }
                        }

                        ImGui::EndCombo();
                    }

                    ImGui::EndDisabled();

                    auto minLimit = Config::Instance()->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
                    auto maxLimit = Config::Instance()->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;
                    if (ImGui::SliderFloat("Upscaler Ratio", &_mipmapUpscalerRatio, minLimit, maxLimit, "%.2f"))
                    {
                        _renderWidth = _displayWidth / _mipmapUpscalerRatio;
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                    }

                    if (ImGui::InputScalar("Render Width", ImGuiDataType_U32, &_renderWidth, NULL, NULL, "%u"))
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);

                    ImGui::SliderFloat("Mipmap Bias", &_mipBiasCalculated, -15.0f, 0.0f, "%.6f");

                    // BOTTOM LINE
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::SameLine(ImGui::GetWindowWidth() - 130.0f);

                    if (ImGui::Button("Use Value"))
                    {
                        _mipBias = _mipBiasCalculated;
                        _showMipmapCalcWindow = false;
                    }

                    ImGui::SameLine(0.0f, 6.0f);

                    if (ImGui::Button("Close"))
                        _showMipmapCalcWindow = false;

                    ImGui::Spacing();
                    ImGui::Separator();

                    ImGui::End();
                }
            }
        }

        if (Config::Instance()->UseHQFont.value_or_default())
            ImGui::PopFontSize();

        return true;
    }
}

void MenuCommon::Init(HWND InHwnd, bool isUWP)
{
    _handle = InHwnd;
    _isVisible = false;
    _isUWP = isUWP;

    LOG_DEBUG("Handle: {0:X}", (size_t) _handle);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
    io.BackendFlags &= 30;
    io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
    io.WantSetMousePos = _isVisible;

    io.IniFilename = io.LogFilename = nullptr;

    bool initResult = false;

    if (io.BackendPlatformUserData == nullptr)
    {
        if (!isUWP)
        {
            initResult = ImGui_ImplWin32_Init(InHwnd);
            LOG_DEBUG("ImGui_ImplWin32_Init result: {0}", initResult);
        }
        else
        {
            initResult = ImGui_ImplUwp_Init(InHwnd);
            ImGui_BindUwpKeyUp(KeyUp);
            LOG_DEBUG("ImGui_ImplUwp_Init result: {0}", initResult);
        }
    }

    if (io.Fonts->Fonts.empty() && Config::Instance()->UseHQFont.value_or_default())
    {
        ImFontAtlas* atlas = io.Fonts;
        atlas->Clear();

        // This automatically becomes the next default font
        ImFontConfig fontConfig;

        if (Config::Instance()->TTFFontPath.has_value())
        {
            io.FontDefault =
                atlas->AddFontFromFileTTF(wstring_to_string(Config::Instance()->TTFFontPath.value()).c_str(), fontSize,
                                          &fontConfig, io.Fonts->GetGlyphRangesDefault());
        }
        else
        {
            io.FontDefault = atlas->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,
                                                                         fontSize, &fontConfig);
        }
    }

    if (!Config::Instance()->OverlayMenu.value_or_default())
    {
        _imguiSizeUpdate = true;
        _hdrTonemapApplied = false;
    }

    if (_oWndProc == nullptr && !isUWP)
        _oWndProc = (WNDPROC) SetWindowLongPtr(InHwnd, GWLP_WNDPROC, (LONG_PTR) WndProc);

    LOG_DEBUG("_oWndProc: {0:X}", (ULONG64) _oWndProc);

    if (!pfn_SetCursorPos_hooked)
        AttachHooks();

    _isInited = true;
}

void MenuCommon::Shutdown()
{
    if (!MenuCommon::_isInited)
        return;

    if (_oWndProc != nullptr)
    {
        SetWindowLongPtr((HWND) ImGui::GetMainViewport()->PlatformHandleRaw, GWLP_WNDPROC, (LONG_PTR) _oWndProc);
        _oWndProc = nullptr;
    }

    if (pfn_SetCursorPos_hooked)
        DetachHooks();

    if (!_isUWP)
        ImGui_ImplWin32_Shutdown();
    else
        ImGui_ImplUwp_Shutdown();

    ImGui::DestroyContext();

    _isInited = false;
    _isVisible = false;
}

void MenuCommon::HideMenu()
{
    if (!_isVisible)
        return;

    _isVisible = false;

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    if (pfn_ClipCursor_hooked)
        pfn_ClipCursor(&_cursorLimit);

    _showMipmapCalcWindow = false;

    RECT windowRect = {};

    if (!_isUWP && GetWindowRect(_handle, &windowRect))
    {
        auto x = windowRect.left + (windowRect.right - windowRect.left) / 2;
        auto y = windowRect.top + (windowRect.bottom - windowRect.top) / 2;

        if (pfn_SetCursorPos != nullptr)
            pfn_SetCursorPos(x, y);
        else
            SetCursorPos(x, y);
    }

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
}
