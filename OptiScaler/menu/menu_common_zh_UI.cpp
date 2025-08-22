#include "menu_common.h"

#include "font/Hack_Compressed.h"

#include <hooks/HooksDx.h>

#include <proxies/XeSS_Proxy.h>
#include <proxies/XeFG_Proxy.h>
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
static std::vector<std::string> splashText = { "愿糊弄与你同在...",
                                               "这位的糊弄学功力很强...",
                                               "欢乐时光就要开始啦（捧读）...",
                                               "超分神器还有吗？给多点！...",
                                               "量子像素·薛定谔的帧数...",
                                               "假帧！热乎的假帧！现编现卖！...",
                                               "老子今天来就干俩件事：脚踢像素、狂炫帧数...",
                                               "超分不足，纯属摆烂！...",
                                               "帧·命·挽·尊！给我缩！",
                                               "抵抗无效！尔等像素终将归化超分神教！",
                                               "毛病千千万，低画质？不存在的！",
                                               "DLSS，朕的渲染江山更高！",
                                               "这真不是你要的原生画质（挥手）",
                                               "光追一关，画质直冲天际！",
                                               "这帧生成...有种不祥预感",
                                               "独行闯关易扑街，超分法宝随身带",
                                               "超分到亲妈都认不出来",
                                               "玄学流程，鬼影勿视",
                                               "如假包换·假帧认证™",
                                               "性能催眠术，专业造梦三十年",
                                               "这古董算法该送进博物馆！",
                                               "原生渲染？老顽固才用的！",
                                               "超分超得好，显卡用到老",
                                               "卡吧加钱，永不为奴！",
                                               "前往的次元，不需要真实像素！",
                                               "英特尔XeFG大派送！装机就白嫖！",
                                               "MFG×Nukem联名，100%%纯正黑魔法！",
                                               "这像素保真！至少部分保真...吧？",
                                               "别细看，细看就露馅了！",
                                               "甚至附赠‘脑补版’XeSS！",
                                               "RCAS护体，百邪不侵！",
                                               "感谢nitec，接下来请继续听nitec鬼扯",
                                               "By-U认证，翻车包赔（才怪）",
                                               "0.8版本？都是套路！",
                                               "<骚话随机生成中...>" };

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
            return "未绑定按键";

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

        return "未识别按键";
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
            ImGui::Text("请输入任意按键...");

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
        if (ImGui::Button("重置"))
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
            _showHudlessWindow = false;

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
        return "FSR 2.1.2（支持DX12）";

    if (*code == "fsr22_12")
        return "FSR 2.2.1（支持DX12）";

    if (*code == "fsr31_12")
        return "FSR 3.X（支持DX12）";

    if (*code == "xess")
        return "XeSS";

    if (*code == "xess_12")
        return "XeSS（支持DX12）";

    if (*code == "dlss")
        return "DLSS";

    return "未知";
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
    std::string fsr3xName = Config::Instance()->Fsr4Update.value_or_default() ? "FSR 3.X/4（支持DX12）" : "FSR 3.X（支持DX12）";

    if (State::Instance().newBackend == "fsr22" || (State::Instance().newBackend == "" && *code == "fsr22"))
        selectedUpscalerName = "FSR 2.2.1";
    else if (State::Instance().newBackend == "fsr22_12" || (State::Instance().newBackend == "" && *code == "fsr22_12"))
        selectedUpscalerName = "FSR 2.2.1（支持DX12）";
    else if (State::Instance().newBackend == "fsr21_12" || (State::Instance().newBackend == "" && *code == "fsr21_12"))
        selectedUpscalerName = "FSR 2.1.2（支持DX12）";
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
        selectedUpscalerName = "XeSS（支持DX12）";

    if (ImGui::BeginCombo("", selectedUpscalerName.c_str()))
    {
        if (ImGui::Selectable("XeSS", *code == "xess"))
            State::Instance().newBackend = "xess";

        if (ImGui::Selectable("FSR 2.2.1", *code == "fsr22"))
            State::Instance().newBackend = "fsr22";

        if (ImGui::Selectable("FSR 3.X", *code == "fsr31"))
            State::Instance().newBackend = "fsr31";

        if (ImGui::Selectable("XeSS（支持DX12）", *code == "xess_12"))
            State::Instance().newBackend = "xess_12";

        if (ImGui::Selectable("FSR 2.1.2（支持DX12）", *code == "fsr21_12"))
            State::Instance().newBackend = "fsr21_12";

        if (ImGui::Selectable("FSR 2.2.1（支持DX12）", *code == "fsr22_12"))
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
    const char* states[] = { "自动",
                             "通用",
                             "顶点和常量缓冲区",
                             "索引缓冲区",
                             "渲染目标",
                             "无序访问",
                             "深层写入",
                             "深层读取",
                             "非像素着色器资源",
                             "像素着色器资源",
                             "流输出",
                             "间接参数",
                             "复制目标",
                             "复制源",
                             "解析目标",
                             "解析源",
                             "光线追踪加速结构",
                             "着色率源",
                             "通用读取",
                             "所有着色器资源",
                             "呈现",
                             "预测",
                             "视频解码读取",
                             "视频解码写入",
                             "视频处理读取",
                             "视频处理写入",
                             "视频编码读取",
                             "视频编码写入" };
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
    const char* presets[] = { "默认设置",  "预设 A", "预设 B", "预设 C", "预设 D", "预设 E",
                              "预设 F", "预设 G", "预设 H", "预设 I", "预设 J", "预设 K",
                              "预设 L", "预设 M", "预设 N", "预设 O", "最新预设" };
    const std::string presetsDesc[] = {
        "使用游戏默认设置",
        "适用于性能/平衡/画质模式。\n适合处理缺失输入的元素（如运动矢量）"
        "以减少残影的旧版本。",
        "适用于极致性能模式。\n类似于预设 A，但针对极致性能模式优化。",
        "适用于性能/平衡/画质模式。\n一般偏向当前帧信息；\n非常适合"
        "快节奏游戏内容。",
        "性能/平衡/画质模式的默认预设；\n通常偏向画面稳定性。",
        "DLSS 3.7+，比 D 预设更好",
        "极致性能和 DLAA 模式的默认预设",
        "未使用",

        "未使用",
        "未使用",
        "Transformers（偏平衡/画质优化）",
        "Transformers 2（更高画质/极致模式）",
        "未使用",
        "未使用",
        "未使用",
        "未使用",

        "DLL支持的最新预设"
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
    const char* presets[] = { "默认设置",  "预设 A", "预设 B", "预设 C", "预设 D", "预设 E",
                              "预设 F", "预设 G", "预设 H", "预设 I", "预设 J", "预设 K",
                              "预设 L", "预设 M", "预设 N", "预设 O", "最新预设" };
    const std::string presetsDesc[] = { "使用游戏默认设置",
                                        "CNN 1（性能/质量平衡）",
                                        "CNN 2（更偏重画质）",
                                        "CNN 3（极致画质）",
                                        "Transformers（偏平衡/画质优化）",
                                        "Transformers 2（更高画质/极致模式）",
                                        "未使用",
                                        "未使用",

                                        "未使用",
                                        "未使用",
                                        "未使用",
                                        "未使用",
                                        "未使用",
                                        "未使用",
                                        "未使用",
                                        "未使用",

                                        "DLL支持的最新预设" };

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
                _showHudlessWindow = false;
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

            if (ImGui::Begin("启动画面", nullptr,
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

                ImGui::Text("OptiScaler_[AI]联合汉化");
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

        if (ImGui::Begin("性能监控", nullptr,
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
                firstLine = std::format("{} | 帧率: {:5.1f}", api.c_str(), frameRate);
            }
            else if (Config::Instance()->FpsOverlayType.value_or_default() == 1)
            {
                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    firstLine =
                        std::format("{} | 帧率: {:5.1f}, {:6.2f} 毫秒 | {} -> {} {}.{}.{}", api.c_str(), frameRate,
                                    frameTime, State::Instance().currentInputApiName.c_str(),
                                    currentFeature->Name().c_str(), State::Instance().currentFeature->Version().major,
                                    State::Instance().currentFeature->Version().minor,
                                    State::Instance().currentFeature->Version().patch);
                else
                    firstLine = std::format("{} | 帧率: {:5.1f}, {:6.2f} 毫秒", api.c_str(), frameRate, frameTime);
            }
            else
            {
                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    firstLine =
                        std::format("{} | 帧率: {:5.1f}, 平均帧: {:5.1f} | {} -> {} {}.{}.{}", api.c_str(), frameRate,
                                    1000.0f / averageFrameTime, State::Instance().currentInputApiName.c_str(),
                                    currentFeature->Name().c_str(), State::Instance().currentFeature->Version().major,
                                    State::Instance().currentFeature->Version().minor,
                                    State::Instance().currentFeature->Version().patch);
                else
                    firstLine = std::format("{} | 帧率: {:5.1f}, 平均帧: {:5.1f}", api.c_str(), frameRate,
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

                secondLine = std::format("帧生成时间: {:6.2f} 毫秒, 平均: {:6.2f} 毫秒", State::Instance().frameTimes.back(),
                                         averageFrameTime);
            }

            // Prepare Line 3
            if (Config::Instance()->FpsOverlayType.value_or_default() > 3)
            {
                thirdLine = std::format("超采样时间: {:6.2f} 毫秒, 平均: {:6.2f} 毫秒",
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

            if (!_showMipmapCalcWindow && !_showHudlessWindow && !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
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

                    ImGui::Text("请在游戏选项中选择 %s 作为超采样模式，\n"
                                "保存并进入游戏以启用超采样设置。\n",
                                joinedUpscalers.c_str());

                    if (Config::Instance()->UseHQFont.value_or_default())
                        ImGui::PopFontSize();
                    else
                        ImGui::SetWindowFontScale(Config::Instance()->MenuScale.value_or_default());

                    ImGui::Spacing();
                    ImGui::Text("nvngx.dll: %s", State::Instance().nvngxExists || State::Instance().isRunningOnNvidia
                                                     ? "存在"
                                                     : "不存在");
                    ImGui::Text("nvngx 替代: %s",
                                State::Instance().nvngxReplacement.has_value() ? "存在" : "不存在");
                    ImGui::Text("libxess.dll: %s", (State::Instance().libxessExists || XeSSProxy::Module() != nullptr)
                                                       ? "存在"
                                                       : "不存在");
                    ImGui::Text("fsr: %s", State::Instance().fsrHooks ? "存在" : "不存在");

                    ImGui::Spacing();
                }
                else
                {
                    ImGui::Spacing();
                    ImGui::Text(
                        "未找到 nvngx.dll、libxess.dll 或 FSR 输入文件，\n超采样功能将无法使用。");
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

                ImGui::Text("%s 已激活，当前未被使用,\n请进入游戏",
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
                    ImGui::SeparatorText("超采样方案");
                    ShowTooltip("请选择要使用的算法");

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
                        spoofingText = Config::Instance()->DxgiSpoofing.value_or_default() ? "开启" : "禁用";
                        ImGui::Text("| 伪装: %s", spoofingText.c_str());

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
                        ImGui::Text("| 输入: %s", State::Instance().currentInputApiName.c_str());

                        ImGui::SameLine(0.0f, 6.0f);
                        spoofingText = Config::Instance()->DxgiSpoofing.value_or_default() ? "开启" : "禁用";
                        ImGui::Text("| 伪装: %s", spoofingText.c_str());

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
                        ImGui::Text("| 输入: %s", State::Instance().currentInputApiName.c_str());

                        auto vlkSpoof = Config::Instance()->VulkanSpoofing.value_or_default();
                        auto vlkExtSpoof = Config::Instance()->VulkanExtensionSpoofing.value_or_default();

                        if (vlkSpoof && vlkExtSpoof)
                            spoofingText = "开启+拓展";
                        else if (vlkSpoof)
                            spoofingText = "开启";
                        else if (vlkExtSpoof)
                            spoofingText = "仅拓展";
                        else
                            spoofingText = "禁用";

                        ImGui::SameLine(0.0f, 6.0f);
                        ImGui::Text("| 伪装: %s", spoofingText.c_str());

                        if (State::Instance().currentFeature->Name() != "DLSSD")
                            AddVulkanBackends(&currentBackend, &currentBackendName);
                    }

                    ImGui::PopItemWidth();

                    if (State::Instance().currentFeature->Name() != "DLSSD")
                    {
                        ImGui::SameLine(0.0f, 6.0f);

                        if (ImGui::Button("切换采样方案##2") && State::Instance().newBackend != "" &&
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
                        if (ImGui::Checkbox("使用反应掩码作为透明掩码", &useAsTransparency))
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
                        if (ImGui::CollapsingHeader("Dx11 及 Dx12 设置"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            if (bool dontUseNTShared = Config::Instance()->DontUseNTShared.value_or_default();
                                ImGui::Checkbox("不使用 NTShared（安全但性能略低）", &dontUseNTShared))
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
                        if (ImGui::CollapsingHeader("XeSS 设置"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            const char* models[] = { "KPSS", "SPLAT", "MODEL_3", "MODEL_4", "MODEL_5", "MODEL_6" };
                            auto configModes = Config::Instance()->NetworkModel.value_or_default();

                            if (configModes < 0 || configModes > 5)
                                configModes = 0;

                            const char* selectedModel = models[configModes];

                            if (ImGui::BeginCombo("网络模型", selectedModel))
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
                            ShowHelpMarker("可能作用不大");

                            if (bool dbg = State::Instance().xessDebug; ImGui::Checkbox("导出数据 (Shift+Del)", &dbg))
                                State::Instance().xessDebug = dbg;

                            ImGui::SameLine(0.0f, 6.0f);
                            int dbgCount = State::Instance().xessDebugFrames;

                            ImGui::PushItemWidth(95.0f * Config::Instance()->MenuScale.value_or_default());
                            if (ImGui::InputInt("帧率", &dbgCount))
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
                        ImGui::SeparatorText("FFX 设置");

                        if (_fsr3xIndex < 0)
                            _fsr3xIndex = Config::Instance()->Fsr3xIndex.value_or_default();

                        if (currentBackend == "fsr31" ||
                            currentBackend == "fsr31_12" && State::Instance().fsr3xVersionNames.size() > 0)
                        {
                            ImGui::PushItemWidth(135.0f * Config::Instance()->MenuScale.value_or_default());

                            auto currentName = std::format("FSR {}", State::Instance().fsr3xVersionNames[_fsr3xIndex]);
                            if (ImGui::BeginCombo("FFX 超采样", currentName.c_str()))
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

                            ShowHelpMarker("由 FFX SDK 报告的超分列表");

                            ImGui::SameLine(0.0f, 6.0f);

                            if (ImGui::Button("更改采样方案") &&
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
                                        ImGui::Checkbox("非线性 sRGB 输入", &nlSRGB))
                                    {
                                        Config::Instance()->FsrNonLinearSRGB = nlSRGB;

                                        if (nlSRGB)
                                            Config::Instance()->FsrNonLinearPQ = false;

                                        State::Instance().newBackend = currentBackend;
                                        MARK_ALL_BACKENDS_CHANGED();
                                    }
                                    ShowHelpMarker("表示输入色彩资源包含感知 sRGB 颜色，\n"
                                                   "可能提高 FSR4 超分质量。");

                                    ImGui::TableNextColumn();

                                    if (bool nlPQ = Config::Instance()->FsrNonLinearPQ.value_or_default();
                                        ImGui::Checkbox("非线性 PQ 输入", &nlPQ))
                                    {
                                        Config::Instance()->FsrNonLinearPQ = nlPQ;

                                        if (nlPQ)
                                            Config::Instance()->FsrNonLinearSRGB = false;

                                        State::Instance().newBackend = currentBackend;
                                        MARK_ALL_BACKENDS_CHANGED();
                                    }
                                    ShowHelpMarker("表示输入色彩资源包含感知 PQ 颜色，\n"
                                                   "可能提高 FSR4 超分质量。");

                                    ImGui::EndTable();
                                }

                                std::array<const char*, 7> models = { "默认", "模型 0", "模型 1", "模型 2",
                                                                      "模型 3", "模型 4", "模型 5" };

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
                                ShowHelpMarker("模型 0: FSR 抗锯齿/极高质量\n"
                                               "模型 1: FSR 高质量\n"
                                               "模型 2: FSR 平衡\n"
                                               "模型 3: FSR 性能\n"
                                               "模型 5: FSR 极致性能");

                                ImGui::Spacing();
                                ImGui::Text("当前模型: %d", State::Instance().currentFsr4Model);
                                ImGui::Spacing();
                            }

                            if (majorFsrVersion == 3)
                            {
                                if (bool dView = Config::Instance()->FsrDebugView.value_or_default();
                                    ImGui::Checkbox("FSR 超采样调试视图", &dView))
                                    Config::Instance()->FsrDebugView = dView;
                                ShowHelpMarker("左上：膨胀运动向量\n"
                                               "中上：保护区域\n"
                                               "右上：膨胀深度\n"
                                               "中间：放大帧\n"
                                               "左下：去遮挡遮罩\n"
                                               "中下：反应度\n"
                                               "右下：细节保护移除");
                            }

                            ImGui::Spacing();

                            if (currentFeature->Version() >= feature_version { 3, 1, 1 } &&
                                currentFeature->Version() < feature_version { 4, 0, 0 } &&
                                ImGui::CollapsingHeader("FSR 3 超采样微调"))
                            {
                                ScopedIndent indent {};
                                ImGui::Spacing();

                                ImGui::PushItemWidth(220.0f * Config::Instance()->MenuScale.value_or_default());

                                float velocity = Config::Instance()->FsrVelocity.value_or_default();
                                if (ImGui::SliderFloat("速度因子", &velocity, 0.00f, 1.0f, "%.2f"))
                                    Config::Instance()->FsrVelocity = velocity;

                                ShowHelpMarker("0.0f可提高时间抗锯齿的稳定性，\n"
                                               "较低的数值视觉上更平滑，拖影明显，\n"
                                               "较高的数值锯齿感更明显，拖影较少。");

                                if (currentFeature->Version() >= feature_version { 3, 1, 4 })
                                {
                                    // Reactive Scale
                                    float reactiveScale = Config::Instance()->FsrReactiveScale.value_or_default();
                                    if (ImGui::SliderFloat("反应尺度", &reactiveScale, 0.0f, 100.0f, "%.1f"))
                                        Config::Instance()->FsrReactiveScale = reactiveScale;

                                    ShowHelpMarker("用于开发测试：\n"
                                                   "写入较大反应遮罩值可以减少拖影。");

                                    // Shading Scale
                                    float shadingScale = Config::Instance()->FsrShadingScale.value_or_default();
                                    if (ImGui::SliderFloat("着色尺度", &shadingScale, 0.0f, 100.0f, "%.1f"))
                                        Config::Instance()->FsrShadingScale = shadingScale;

                                    ShowHelpMarker("增加此值会放大FSR3.1计算的着色变化值，\n"
                                                   "以提高反应性。");

                                    // Accumulation Added Per Frame
                                    float accAddPerFrame = Config::Instance()->FsrAccAddPerFrame.value_or_default();
                                    if (ImGui::SliderFloat("每帧增加累积量", &accAddPerFrame, 0.00f, 1.0f,
                                                           "%.2f"))
                                        Config::Instance()->FsrAccAddPerFrame = accAddPerFrame;

                                    ShowHelpMarker(
                                        "对应每帧增加的累积量。\n"
                                        "在发生去遮挡的位置或反应遮罩值 > 0 时\n"
                                        "降低此值并将鬼影对象（即无运动向量）\n"
                                        "绘制到反应遮罩中， \n"
                                        "接近 1.0 可减少时间拖影，\n"
                                        "减小可能导致细特征像素闪烁增多。");

                                    // Min Disocclusion Accumulation
                                    float minDisOccAcc = Config::Instance()->FsrMinDisOccAcc.value_or_default();
                                    if (ImGui::SliderFloat("最小遮挡累积.", &minDisOccAcc, -1.0f, 1.0f,
                                                           "%.2f"))
                                        Config::Instance()->FsrMinDisOccAcc = minDisOccAcc;

                                    ShowHelpMarker("增加此值可减少细长物体\n"
                                                   "摆动时的白色像素时间闪烁，\n"
                                                   "过高可能增加鬼影。");
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
                            ImGui::SeparatorText("DLSSD 设置");
                        else
                            ImGui::SeparatorText("DLSS 设置");

                        auto overridden = usesDlssd ? State::Instance().dlssdPresetsOverriddenExternally
                                                    : State::Instance().dlssPresetsOverriddenExternally;

                        if (overridden)
                        {
                            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "预设已被外部覆盖");
                            ShowHelpMarker("通常是因为使用了如 Nvidia App \n"
                                           "或 Nvidia Inspector 等工具。");
                            ImGui::Text("选择下面的设置将禁用外部覆盖，\n"
                                        "但需要保存 INI 并重启游戏。");

                            ImGui::Spacing();
                        }

                        if (bool pOverride = Config::Instance()->RenderPresetOverride.value_or_default();
                            ImGui::Checkbox("渲染预设覆盖", &pOverride))
                            Config::Instance()->RenderPresetOverride = pOverride;
                        ShowHelpMarker("每个渲染预设都有其优缺点,\n"
                                       "覆盖可能提升图像质量。");

                        ImGui::BeginDisabled(!Config::Instance()->RenderPresetOverride.value_or_default() ||
                                             overridden);

                        ImGui::PushItemWidth(135.0f * Config::Instance()->MenuScale.value_or_default());
                        if (usesDlssd)
                            AddDLSSDRenderPreset("预设覆盖", &Config::Instance()->RenderPresetForAll);
                        else
                            AddDLSSRenderPreset("预设覆盖", &Config::Instance()->RenderPresetForAll);

                        ImGui::PopItemWidth();

                        ImGui::SameLine(0.0f, 6.0f);

                        if (ImGui::Button("应用更改"))
                        {
                            if (usesDlssd)
                                State::Instance().newBackend = "dlssd";
                            else
                                State::Instance().newBackend = currentBackend;

                            MARK_ALL_BACKENDS_CHANGED();
                        }

                        ImGui::EndDisabled();

                        ImGui::Spacing();

                        if (ImGui::CollapsingHeader(usesDlssd ? "高级 DLSSD 设置" : "高级 DLSS 设置"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            bool appIdOverride = Config::Instance()->UseGenericAppIdWithDlss.value_or_default();
                            if (ImGui::Checkbox("DLSS 使用通用 App Id", &appIdOverride))
                                Config::Instance()->UseGenericAppIdWithDlss = appIdOverride;

                            ShowHelpMarker("使用 NGX 通用 AppId\n"
                                           "修复某些游戏中 OptiScaler 预设覆盖无效的问题，\n"
                                           "需要重启游戏。");

                            ImGui::BeginDisabled(!Config::Instance()->RenderPresetOverride.value_or_default() ||
                                                 overridden);
                            ImGui::Spacing();
                            ImGui::PushItemWidth(135.0f * Config::Instance()->MenuScale.value_or_default());

                            if (usesDlssd)
                            {
                                AddDLSSDRenderPreset("DLAA 预设", &Config::Instance()->RenderPresetDLAA);
                                AddDLSSDRenderPreset("超高质量预设", &Config::Instance()->RenderPresetUltraQuality);
                                AddDLSSDRenderPreset("质量预设", &Config::Instance()->RenderPresetQuality);
                                AddDLSSDRenderPreset("均衡预设", &Config::Instance()->RenderPresetBalanced);
                                AddDLSSDRenderPreset("性能预设", &Config::Instance()->RenderPresetPerformance);
                                AddDLSSDRenderPreset("极致性能预设",
                                                     &Config::Instance()->RenderPresetUltraPerformance);
                            }
                            else
                            {
                                AddDLSSRenderPreset("DLAA 预设", &Config::Instance()->RenderPresetDLAA);
                                AddDLSSRenderPreset("超高质量预设", &Config::Instance()->RenderPresetUltraQuality);
                                AddDLSSRenderPreset("质量预设", &Config::Instance()->RenderPresetQuality);
                                AddDLSSRenderPreset("均衡预设", &Config::Instance()->RenderPresetBalanced);
                                AddDLSSRenderPreset("性能预设", &Config::Instance()->RenderPresetPerformance);
                                AddDLSSRenderPreset("极致性能预设", &Config::Instance()->RenderPresetUltraPerformance);
                            }
                            ImGui::PopItemWidth();
                            ImGui::EndDisabled();

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }
                }

                // clang-format off
                const char* fgInputOptions[] = {
                    "无帧生成",
                    "DLSS帧生成（Nukem版）",
                    "FSR 帧生成",
                    "DLSS帧生成（通过Streamline）",
                    "XeSS帧生成（Intel）",
                    "Opti帧生成（自带）",
                };
                std::vector<std::string> fgInputDesc = {
                    "",
                    "仅限FSR 3帧生成\n\n开箱即用，支持无 HUD\n\n使用 Streamline 交换链进行帧间距控制", 
                    "可用于任何帧生成类型\n\n开箱即用，支持无 HUD\n\n当前仅支持 FSR3.1帧生成", 
                    "可用于任何帧生成类型\n\n开箱即用，支持无 HUD\n\n仅限使用 Streamline v2 的游戏", 
                    "尚未实现支持", 
                    "必须启用超采样器\n\n可用于任何帧生成类型，但部分场景不够完美\n\n为防止UI出现问题，可开启Hud修复",
                };
                std::vector<uint8_t> disabledMaskInput = { 
                    false, 
                    false, 
                    false, 
                    false, // TODO: Disable DLSSG inputs in games that can't support it
                    true, 
                    false 
                };
                // clang-format on

                // OptiFG requirements
                auto constexpr optiFgIndex = (uint32_t) FGInput::Upscaler;
                if (!Config::Instance()->OverlayMenu.value_or_default())
                {
                    disabledMaskInput[optiFgIndex] = true;
                    fgInputDesc[optiFgIndex] = "不支持旧版叠加菜单";
                }
                else if (State::Instance().api != DX12)
                {
                    disabledMaskInput[optiFgIndex] = true;
                    fgInputDesc[optiFgIndex] = "不支持的 API";
                }
                else if (State::Instance().isWorkingAsNvngx)
                {
                    disabledMaskInput[optiFgIndex] = true;
                    fgInputDesc[optiFgIndex] = "不支持的 Opti 工作模式";
                }
                else if ((fsr31InitTried && FfxApiProxy::Dx12Module() == nullptr) ||
                         (!fsr31InitTried && !FfxApiProxy::InitFfxDx12()))
                {
                    fsr31InitTried = true;
                    disabledMaskInput[optiFgIndex] = true;
                    fgInputDesc[optiFgIndex] = "缺少 amd_fidelityfx_dx12.dll 文件！";
                }

                // DLSSG inputs requirements
                auto constexpr dlssgInputIndex = (uint32_t) FGInput::DLSSG;
                if (State::Instance().streamlineVersion.major < 2)
                {
                    disabledMaskInput[dlssgInputIndex] = true;
                    fgInputDesc[dlssgInputIndex] = "不支持的 Streamline 版本";

                    if (Config::Instance()->FGInput.value_or_default() == FGInput::DLSSG)
                        Config::Instance()->FGInput.reset();
                }
                else if (State::Instance().api != DX12)
                {
                    disabledMaskInput[dlssgInputIndex] = true;
                    fgInputDesc[dlssgInputIndex] = "不支持的 API";
                }

                // FSRFG inputs requirements
                auto constexpr fsrfgInputIndex = (uint32_t) FGInput::FSRFG;
                if (State::Instance().api != DX12)
                {
                    disabledMaskInput[fsrfgInputIndex] = true;
                    fgInputDesc[fsrfgInputIndex] = "不支持的 API";
                }

                constexpr auto fgInputOptionsCount = sizeof(fgInputOptions) / sizeof(char*);

                if (!Config::Instance()->FGInput.has_value())
                    Config::Instance()->FGInput =
                        Config::Instance()->FGInput.value_or_default(); // need to have a value before combo

                // clang-format off
                const char* fgOutputOptions[] = {
                    "无帧生成",
                    "FSR 帧生成（Nukem版）",
                    "FSR 帧生成",
                    "DLSS帧生成",
                    "XeSS帧生成"
                };
                std::vector<std::string> fgOutputDesc = {
                    "",
                    "在游戏中选择 DLSS 帧生成", 
                    "FSR 帧生成", 
                    "尚未实现支持", 
                    "XeSS帧生成",
                };
                std::vector<uint8_t> disabledMaskOutput = { 
                    false, 
                    false, 
                    false, 
                    true, 
                    false,
                };
                // clang-format on

                // Nukem's FG mod requirements
                auto constexpr nukemsInputIndex = (uint32_t) FGInput::Nukems;
                auto constexpr nukemsOutputIndex = (uint32_t) FGOutput::Nukems;
                if (State::Instance().api == DX11)
                {
                    disabledMaskInput[nukemsInputIndex] = true;
                    fgInputDesc[nukemsInputIndex] = "不支持的 API";
                    disabledMaskOutput[nukemsOutputIndex] = true;
                    fgOutputDesc[nukemsOutputIndex] = "不支持的 API";
                }
                else if (State::Instance().isWorkingAsNvngx)
                {
                    disabledMaskInput[nukemsInputIndex] = true;
                    fgInputDesc[nukemsInputIndex] = "不支持的 Opti 工作模式";
                    disabledMaskOutput[nukemsOutputIndex] = true;
                    fgOutputDesc[nukemsOutputIndex] = "不支持的 Opti 工作模式";
                }
                else if (!State::Instance().NukemsFilesAvailable)
                {
                    disabledMaskInput[nukemsInputIndex] = true;
                    fgInputDesc[nukemsInputIndex] = "缺少 dlssg_to_fsr3_amd_is_better.dll 文件！";
                    disabledMaskOutput[nukemsOutputIndex] = true;
                    fgOutputDesc[nukemsOutputIndex] = "缺少 dlssg_to_fsr3_amd_is_better.dll 文件！";
                }

                // FSR FG / XeFG output requirements
                auto constexpr fsrfgOutputIndex = (uint32_t) FGOutput::FSRFG;
                auto constexpr xefgOutputIndex = (uint32_t) FGOutput::XeFG;
                if (State::Instance().api != DX12)
                {
                    disabledMaskOutput[fsrfgOutputIndex] = true;
                    fgOutputDesc[fsrfgOutputIndex] = "不支持的 API";
                    disabledMaskOutput[xefgOutputIndex] = true;
                    fgOutputDesc[xefgOutputIndex] = "不支持的 API";
                }

                constexpr auto fgOutputOptionsCount = std::size(fgOutputOptions);

                if (!Config::Instance()->FGOutput.has_value())
                    Config::Instance()->FGOutput =
                        Config::Instance()->FGOutput.value_or_default(); // need to have a value before combo

                ImGui::SeparatorText("帧生成");

                if (ImGui::BeginTable("fgSelection", 2, ImGuiTableFlags_SizingStretchSame))
                {
                    ImGui::TableNextColumn();

                    PopulateCombo(
                        "输入类型", reinterpret_cast<CustomOptional<uint32_t>*>(&Config::Instance()->FGInput),
                        fgInputOptions, fgInputDesc.data(), fgInputOptionsCount, disabledMaskInput.data(), false);
                    ShowTooltip("用于帧生成的数据来源");

                    ImGui::TableNextColumn();

                    const bool disableOutputs = Config::Instance()->FGInput.value_or_default() == FGInput::Nukems;

                    ImGui::BeginDisabled(disableOutputs);
                    PopulateCombo(
                        "输出类型", reinterpret_cast<CustomOptional<uint32_t>*>(&Config::Instance()->FGOutput),
                        fgOutputOptions, fgOutputDesc.data(), fgOutputOptionsCount, disabledMaskOutput.data(), false);
                    ImGui::EndDisabled();

                    if (disableOutputs)
                        ShowTooltip("对于当前选择的 FG 来源无影响");

                    ImGui::EndTable();
                }

                auto static fgInputOverridden = false;

                if (Config::Instance()->FGOutput == FGOutput::Nukems && !fgInputOverridden)
                {
                    Config::Instance()->FGInput = FGInput::Nukems;
                    fgInputOverridden = true;
                }
                else if (Config::Instance()->FGInput != FGInput::Nukems && fgInputOverridden)
                {
                    Config::Instance()->FGOutput = FGOutput::NoFG;
                    fgInputOverridden = false;
                }

                if (State::Instance().activeFgOutput == FGOutput::FSRFG ||
                    State::Instance().activeFgOutput == FGOutput::XeFG)
                {
                    ImGui::Checkbox("显示检测界面", &State::Instance().FGHudlessCompare);
                    ShowHelpMarker("需要无 HUD 的纹理来与最终图像比较。\n"
                                   "UI元素且仅UI元素，应显示粉红色标记！");
                }

                // if (State::Instance().activeFgInput != Config::Instance()->FGInput.value_or_default())
                //{
                //     State::Instance().activeFgInput = Config::Instance()->FGInput.value_or_default();
                //     State::Instance().FGchanged = true; // Formats might be different so reconfigure
                // }

                State::Instance().fgSettingsChanged =
                    State::Instance().activeFgOutput != Config::Instance()->FGOutput.value_or_default() ||
                    State::Instance().activeFgInput != Config::Instance()->FGInput.value_or_default();

                if (State::Instance().fgSettingsChanged)
                {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.f, 0.f, 0.0f, 1.f), "保存 INI 文件并重启以应用更改");
                    ImGui::Spacing();
                }

                // FSR FG controls
                if (State::Instance().activeFgOutput == FGOutput::FSRFG &&
                    State::Instance().activeFgInput != FGInput::NoFG &&
                    Config::Instance()->OverlayMenu.value_or_default() && !State::Instance().isWorkingAsNvngx &&
                    State::Instance().api == DX12)
                {
                    if (State::Instance().activeFgInput != FGInput::Upscaler ||
                        (currentFeature != nullptr && !currentFeature->IsFrozen()) && FfxApiProxy::InitFfxDx12())
                    {
                        ImGui::SeparatorText("帧生成 (FSR 帧生成)");

                        bool fgActive = Config::Instance()->FGEnabled.value_or_default();
                        if (ImGui::Checkbox("启用##2", &fgActive))
                        {
                            Config::Instance()->FGEnabled = fgActive;
                            LOG_DEBUG("FGEnabled set FGEnabled: {}", fgActive);

                            if (Config::Instance()->FGEnabled.value_or_default())
                                State::Instance().FGchanged = true;
                        }

                        ShowHelpMarker("启用帧生成");

                        bool fgAsync = Config::Instance()->FGAsync.value_or_default();
                        if (ImGui::Checkbox("允许异步", &fgAsync))
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
                            "启用异步以提升帧生成性能\n可能导致崩溃，尤其在使用 HUD 修复时！");

                        ImGui::SameLine(0.0f, 16.0f);

                        bool fgDV = Config::Instance()->FGDebugView.value_or_default();
                        if (ImGui::Checkbox("调试视图##2", &fgDV))
                        {
                            Config::Instance()->FGDebugView = fgDV;

                            if (Config::Instance()->FGEnabled.value_or_default())
                            {
                                State::Instance().FGchanged = true;
                                LOG_DEBUG("DebugView set FGChanged");
                            }
                        }
                        ShowHelpMarker("启用 FSR 3.1 帧生成调试视图");

                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("FSR 帧生成高级设置"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            ImGui::Checkbox("仅显示生成帧", &State::Instance().FGonlyGenerated);
                            ShowHelpMarker("仅显示 FSR 3.1 生成的帧");

                            ImGui::SameLine(0.0f, 16.0f);
                            auto debugResetLines = Config::Instance()->FGDebugResetLines.value_or_default();
                            if (ImGui::Checkbox("调试重置线", &debugResetLines))
                            {
                                Config::Instance()->FGDebugResetLines = debugResetLines;
                                LOG_DEBUG("Enabled set FGDebugLines: {}", debugResetLines);
                            }
                            ShowHelpMarker("启用插值跳过线绘制");

                            auto debugTearLines = Config::Instance()->FGDebugTearLines.value_or_default();
                            if (ImGui::Checkbox("调试撕裂线", &debugTearLines))
                            {
                                Config::Instance()->FGDebugTearLines = debugTearLines;
                                LOG_DEBUG("Enabled set FGDebugLines: {}", debugTearLines);
                            }
                            ShowHelpMarker("启用撕裂线与插值跳过线绘制");

                            ImGui::SameLine(0.0f, 16.0f);
                            auto debugPacingLines = Config::Instance()->FGDebugPacingLines.value_or_default();
                            if (ImGui::Checkbox("调试节奏线", &debugPacingLines))
                            {
                                Config::Instance()->FGDebugPacingLines = debugPacingLines;
                                LOG_DEBUG("Enabled set FGDebugLines: {}", debugPacingLines);
                            }
                            ShowHelpMarker("启用节奏线绘制");

                            ImGui::Spacing();
                            if (ImGui::TreeNode("帧生成矩形设置"))
                            {
                                ImGui::PushItemWidth(95.0f * Config::Instance()->MenuScale.value_or_default());
                                int rectLeft = Config::Instance()->FGRectLeft.value_or(0);
                                if (ImGui::InputInt("矩形左", &rectLeft))
                                    Config::Instance()->FGRectLeft = rectLeft;

                                ImGui::SameLine(0.0f, 16.0f);
                                int rectTop = Config::Instance()->FGRectTop.value_or(0);
                                if (ImGui::InputInt("矩形上", &rectTop))
                                    Config::Instance()->FGRectTop = rectTop;

                                int rectWidth = Config::Instance()->FGRectWidth.value_or(0);
                                if (ImGui::InputInt("矩形宽", &rectWidth))
                                    Config::Instance()->FGRectWidth = rectWidth;

                                ImGui::SameLine(0.0f, 16.0f);
                                int rectHeight = Config::Instance()->FGRectHeight.value_or(0);
                                if (ImGui::InputInt("矩形高", &rectHeight))
                                    Config::Instance()->FGRectHeight = rectHeight;

                                ImGui::PopItemWidth();
                                ShowHelpMarker("帧生成矩形，适用于信箱式画面调整");

                                ImGui::BeginDisabled(!Config::Instance()->FGRectLeft.has_value() &&
                                                     !Config::Instance()->FGRectTop.has_value() &&
                                                     !Config::Instance()->FGRectWidth.has_value() &&
                                                     !Config::Instance()->FGRectHeight.has_value());

                                if (ImGui::Button("重置 FG 矩形"))
                                {
                                    Config::Instance()->FGRectLeft.reset();
                                    Config::Instance()->FGRectTop.reset();
                                    Config::Instance()->FGRectWidth.reset();
                                    Config::Instance()->FGRectHeight.reset();
                                }

                                ShowHelpMarker("重置帧生成矩形");

                                ImGui::EndDisabled();
                                ImGui::TreePop();
                            }

                            FSRFG_Dx12* fsrFG = nullptr;
                            if (State::Instance().currentFG != nullptr)
                                fsrFG = reinterpret_cast<FSRFG_Dx12*>(State::Instance().currentFG);

                            if (fsrFG != nullptr && FfxApiProxy::VersionDx12() >= feature_version { 3, 1, 3 })
                            {
                                ImGui::Spacing();
                                if (ImGui::TreeNode("帧节奏调节"))
                                {
                                    auto fptEnabled = Config::Instance()->FGFramePacingTuning.value_or_default();
                                    if (ImGui::Checkbox("启用调节", &fptEnabled))
                                    {
                                        Config::Instance()->FGFramePacingTuning = fptEnabled;
                                        State::Instance().FSRFGFTPchanged = true;
                                    }

                                    ImGui::BeginDisabled(!Config::Instance()->FGFramePacingTuning.value_or_default());

                                    ImGui::PushItemWidth(115.0f * Config::Instance()->MenuScale.value_or_default());
                                    auto fptSafetyMargin = Config::Instance()->FGFPTSafetyMarginInMs.value_or_default();
                                    if (ImGui::InputFloat("安全边距 (毫秒)", &fptSafetyMargin, 0.01, 0.1, "%.2f"))
                                        Config::Instance()->FGFPTSafetyMarginInMs = fptSafetyMargin;
                                    ShowHelpMarker("安全边距 (毫秒)\n"
                                                   "FSR 默认值: 0.1毫秒\n"
                                                   "Opti 默认值: 0.01毫秒");

                                    auto fptVarianceFactor = Config::Instance()->FGFPTVarianceFactor.value_or_default();
                                    if (ImGui::SliderFloat("方差系数", &fptVarianceFactor, 0.0f, 1.0f, "%.2f"))
                                        Config::Instance()->FGFPTVarianceFactor = fptVarianceFactor;
                                    ShowHelpMarker("方差系数\n"
                                                   "FSR 默认值: 0.1\n"
                                                   "Opti 默认值: 0.3");
                                    ImGui::PopItemWidth();

                                    auto fpHybridSpin = Config::Instance()->FGFPTAllowHybridSpin.value_or_default();
                                    if (ImGui::Checkbox("启用混合自旋", &fpHybridSpin))
                                        Config::Instance()->FGFPTAllowHybridSpin = fpHybridSpin;
                                    ShowHelpMarker("Allows pacing spinlock to sleep, should reduce CPU usage\n"
                                                   "Might cause slow ramp up of FPS");

                                    ImGui::PushItemWidth(115.0f * Config::Instance()->MenuScale.value_or_default());
                                    auto fptHybridSpinTime = Config::Instance()->FGFPTHybridSpinTime.value_or_default();
                                    if (ImGui::SliderInt("帧间距自旋锁休眠", &fptHybridSpinTime, 0, 100))
                                        Config::Instance()->FGFPTHybridSpinTime = fptHybridSpinTime;
                                    ShowHelpMarker("允许帧间距自旋锁休眠，"
                                                   "可降低CPU使用率 \n"
                                                   "但可能导致FPS上升缓慢");
                                    ImGui::PopItemWidth();

                                    auto fpWaitForSingleObjectOnFence =
                                        Config::Instance()->FGFPTAllowWaitForSingleObjectOnFence.value_or_default();
                                    if (ImGui::Checkbox("启用单对象等待",
                                                        &fpWaitForSingleObjectOnFence))
                                        Config::Instance()->FGFPTAllowWaitForSingleObjectOnFence =
                                            fpWaitForSingleObjectOnFence;
                                    ShowHelpMarker("允许启用单对象等待代替自旋等待栅栏值");

                                    if (ImGui::Button("应用时序更改"))
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

                // XeFG controls
                if (State::Instance().activeFgOutput == FGOutput::XeFG &&
                    State::Instance().activeFgInput != FGInput::NoFG &&
                    Config::Instance()->OverlayMenu.value_or_default() && !State::Instance().isWorkingAsNvngx &&
                    State::Instance().api == DX12)
                {
                    if (State::Instance().activeFgInput != FGInput::Upscaler ||
                        (currentFeature != nullptr && !currentFeature->IsFrozen()) && XeFGProxy::InitXeFG())
                    {
                        ImGui::SeparatorText("帧生成 (XeSS帧生成)");

                        if (!State::Instance().currentFG->IsLowResMV())
                        {
                            Config::Instance()->FGEnabled.reset();
                            Config::Instance()->FGXeFGDebugView.reset();
                        }

                        ImGui::BeginDisabled(!State::Instance().currentFG->IsLowResMV());

                        bool fgActive = Config::Instance()->FGEnabled.value_or_default();
                        if (ImGui::Checkbox("启用##3", &fgActive))
                        {
                            Config::Instance()->FGEnabled = fgActive;
                            LOG_DEBUG("Enabled set FGEnabled: {}", fgActive);

                            if (Config::Instance()->FGEnabled.value_or_default())
                                State::Instance().FGchanged = true;
                        }

                        if (State::Instance().currentFG->IsLowResMV())
                            ShowHelpMarker("启用帧生成");
                        else
                            ShowHelpMarker("无法启用帧生成\n\n当前显示尺寸 MV 已激活！");

                        bool fgDV = Config::Instance()->FGXeFGDebugView.value_or_default();
                        if (ImGui::Checkbox("调试视图##2", &fgDV))
                        {
                            Config::Instance()->FGXeFGDebugView = fgDV;

                            if (Config::Instance()->FGXeFGDebugView.value_or_default())
                            {
                                State::Instance().FGchanged = true;
                                LOG_DEBUG("DebugView set FGChanged");
                            }
                        }
                        ShowHelpMarker("启用XeSS帧生成调试视图");

                        // 此功能暂时禁用
                        // ImGui::SameLine(0.0f, 16.0f);
                        // ImGui::Checkbox("仅显示生成帧##2", &State::Instance().FGonlyGenerated);
                        // ShowHelpMarker("仅显示 Xess 生成的帧");

                        ImGui::EndDisabled();

                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("XeSS帧生成高级设置"))
                        {
                            ImGui::Spacing();
                            if (ImGui::TreeNode("帧生成矩形设置"))
                            {
                                ImGui::PushItemWidth(95.0f * Config::Instance()->MenuScale.value_or_default());
                                int rectLeft = Config::Instance()->FGRectLeft.value_or(0);
                                if (ImGui::InputInt("矩形左##2", &rectLeft))
                                    Config::Instance()->FGRectLeft = rectLeft;

                                ImGui::SameLine(0.0f, 16.0f);
                                int rectTop = Config::Instance()->FGRectTop.value_or(0);
                                if (ImGui::InputInt("矩形上##2", &rectTop))
                                    Config::Instance()->FGRectTop = rectTop;

                                int rectWidth = Config::Instance()->FGRectWidth.value_or(0);
                                if (ImGui::InputInt("矩形宽##2", &rectWidth))
                                    Config::Instance()->FGRectWidth = rectWidth;

                                ImGui::SameLine(0.0f, 16.0f);
                                int rectHeight = Config::Instance()->FGRectHeight.value_or(0);
                                if (ImGui::InputInt("矩形高##2", &rectHeight))
                                    Config::Instance()->FGRectHeight = rectHeight;

                                ImGui::PopItemWidth();
                                ShowHelpMarker("帧生成矩形，适用于信箱式画面调整##2");

                                ImGui::BeginDisabled(!Config::Instance()->FGRectLeft.has_value() &&
                                                     !Config::Instance()->FGRectTop.has_value() &&
                                                     !Config::Instance()->FGRectWidth.has_value() &&
                                                     !Config::Instance()->FGRectHeight.has_value());

                                if (ImGui::Button("重置帧生成矩形##2"))
                                {
                                    Config::Instance()->FGRectLeft.reset();
                                    Config::Instance()->FGRectTop.reset();
                                    Config::Instance()->FGRectWidth.reset();
                                    Config::Instance()->FGRectHeight.reset();
                                }

                                ShowHelpMarker("重置帧生成矩形##2");

                                ImGui::EndDisabled();
                                ImGui::TreePop();
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
                    SeparatorWithHelpMarker("帧生成 (Opti帧生成)", "使用超采样数据进行帧生成");

                    if (currentFeature != nullptr && !currentFeature->IsFrozen() && FfxApiProxy::InitFfxDx12())
                    {
                        bool fgHudfix = Config::Instance()->FGHUDFix.value_or_default();
                        bool disableHudfix = static_cast<bool>(State::Instance().gameQuirks & GameQuirk::DisableHudfix);

                        ImGui::BeginDisabled(disableHudfix);
                        if (ImGui::Checkbox("HUD修复", &fgHudfix))
                        {
                            Config::Instance()->FGHUDFix = fgHudfix;
                            LOG_DEBUG("Enabled set FGHUDFix: {}", fgHudfix);
                            State::Instance().ClearCapturedHudlesses = true;
                            State::Instance().FGchanged = true;
                        }
                        ImGui::EndDisabled();

                        if (disableHudfix)
                            ShowHelpMarker("已禁用 HUD修复，存在已知问题");
                        else
                            ShowHelpMarker("启用 HUD 稳定性修复，可能导致崩溃！");

                        ImGui::BeginDisabled(!Config::Instance()->FGHUDFix.value_or_default());

                        ImGui::SameLine(0.0f, 16.0f);
                        ImGui::PushItemWidth(95.0f * Config::Instance()->MenuScale.value_or_default());
                        int hudFixLimit = Config::Instance()->FGHUDLimit.value_or_default();
                        if (ImGui::InputInt("限制帧", &hudFixLimit))
                        {
                            if (hudFixLimit < 1)
                                hudFixLimit = 1;
                            else if (hudFixLimit > 999)
                                hudFixLimit = 999;

                            Config::Instance()->FGHUDLimit = hudFixLimit;
                            LOG_DEBUG("Enabled set FGHUDLimit: {}", hudFixLimit);
                        }
                        ShowHelpMarker("延迟无 HUD 捕获，值过高可能导致崩溃！");

                        ImGui::SameLine(0.0f, 16.0f);
                        if (ImGui::Button("重置##2"))
                            _showHudlessWindow = !_showHudlessWindow;

                        ImGui::EndDisabled();

                        auto hudExtended = Config::Instance()->FGHUDFixExtended.value_or_default();
                        if (ImGui::Checkbox("拓展", &hudExtended))
                        {
                            LOG_DEBUG("Enabled set FGHUDFixExtended: {}", hudExtended);
                            Config::Instance()->FGHUDFixExtended = hudExtended;
                        }
                        ShowHelpMarker("扩展格式检查以支持无 HUD模式\n可能导致崩溃或性能下降！");
                        ImGui::SameLine(0.0f, 16.0f);

                        ImGui::BeginDisabled(!Config::Instance()->FGHUDFix.value_or_default());

                        auto immediate = Config::Instance()->FGImmediateCapture.value_or_default();
                        if (ImGui::Checkbox("立即捕获", &immediate))
                        {
                            LOG_DEBUG("Enabled set FGImmediateCapture: {}", immediate);
                            Config::Instance()->FGImmediateCapture = immediate;
                        }
                        ShowHelpMarker("启用在着色器执行前捕获资源以增加无 HUD 捕获机会，\n "
                                       "但可能捕获到不必要的资源！");

                        ImGui::PopItemWidth();

                        ImGui::EndDisabled();

                        bool depthScale = Config::Instance()->FGEnableDepthScale.value_or_default();
                        if (ImGui::Checkbox("深度缩放修复 DLSS RR[光线重建]", &depthScale))
                            Config::Instance()->FGEnableDepthScale = depthScale;
                        ShowHelpMarker("修复DLSS-D的错误深度输入");

                        bool resourceFlip = Config::Instance()->FGResourceFlip.value_or_default();
                        if (ImGui::Checkbox("翻转（Unity）", &resourceFlip))
                            Config::Instance()->FGResourceFlip = resourceFlip;
                        ShowHelpMarker("翻转Unity游戏的运动向量和深度资源");

                        ImGui::SameLine(0.0f, 16.0f);

                        bool resourceFlipOffset = Config::Instance()->FGResourceFlipOffset.value_or_default();
                        if (ImGui::Checkbox("翻转使用偏移", &resourceFlipOffset))
                            Config::Instance()->FGResourceFlipOffset = resourceFlipOffset;
                        ShowHelpMarker("使用高度差作为偏移");

                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("Opti帧生成高级设置"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            auto rb = Config::Instance()->FGResourceBlocking.value_or_default();
                            if (ImGui::Checkbox("资源阻止", &rb))
                            {
                                Config::Instance()->FGResourceBlocking = rb;
                                LOG_DEBUG("Enabled set FGAlwaysTrackHeaps: {}", rb);
                            }
                            ShowHelpMarker("阻止空闲资源作为无 HUD 使用， \n"
                                           "防止闪烁和其他问题，\n\n"
                                           "启用/禁用 HUD修复 会重置阻止列表。");

                            auto rrc = Config::Instance()->FGRelaxedResolutionCheck.value_or_default();
                            if (ImGui::Checkbox("放宽资源检查", &rrc))
                            {
                                Config::Instance()->FGRelaxedResolutionCheck = rrc;
                                LOG_DEBUG("Enabled set FGRelaxedResolutionCheck: {}", rrc);
                            }
                            ShowHelpMarker("将无 HUD 分辨率检查放宽至 32 像素\n"
                                           "以适应使用黑边的游戏，\n"
                                           "（如《巫师3》的分辨率和屏幕比例）");

                            ImGui::BeginDisabled(State::Instance().FGresetCapturedResources);
                            ImGui::PushItemWidth(95.0f * Config::Instance()->MenuScale.value_or_default());
                            if (ImGui::Checkbox("帧生成创建列表", &State::Instance().FGcaptureResources))
                            {
                                if (!State::Instance().FGcaptureResources)
                                    Config::Instance()->FGHUDLimit = 1;
                                else
                                    State::Instance().FGonlyUseCapturedResources = false;
                            }

                            ImGui::SameLine(0.0f, 16.0f);
                            if (ImGui::Checkbox("帧生成使用列表", &State::Instance().FGonlyUseCapturedResources))
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

                            if (ImGui::Button("重置列表"))
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
                                if (ImGui::Checkbox("始终追踪堆", &ath))
                                {
                                    Config::Instance()->FGAlwaysTrackHeaps = ath;
                                    LOG_DEBUG("Enabled set FGAlwaysTrackHeaps: {}", ath);
                                }
                                ShowHelpMarker("始终追踪资源，可能影响性能，\n "
                                               "但也可能修复 HUD修复 相关崩溃。");

                                ImGui::TreePop();
                            }

                            ImGui::Spacing();
                            if (ImGui::TreeNode("Resource Settings"))
                            {
                                bool makeMVCopies = Config::Instance()->FGMakeMVCopy.value_or_default();
                                if (ImGui::Checkbox("帧生成创建运动向量副本", &makeMVCopies))
                                    Config::Instance()->FGMakeMVCopy = makeMVCopies;
                                ShowHelpMarker("制作运动向量副本以供 Opti帧生成使用，\n"
                                               "防止可能发生的数据损坏。");

                                bool makeDepthCopies = Config::Instance()->FGMakeDepthCopy.value_or_default();
                                if (ImGui::Checkbox("帧生成创建深度副本", &makeDepthCopies))
                                    Config::Instance()->FGMakeDepthCopy = makeDepthCopies;
                                ShowHelpMarker("制作深度副本以供 Opti帧生成使用，\n"
                                               "防止可能发生的数据损坏。");

                                ImGui::PushItemWidth(115.0f * Config::Instance()->MenuScale.value_or_default());
                                float depthScaleMax = Config::Instance()->FGDepthScaleMax.value_or_default();
                                if (ImGui::InputFloat("帧生成深度缩放最大值", &depthScaleMax, 10.0f, 100.0f, "%.1f"))
                                    Config::Instance()->FGDepthScaleMax = depthScaleMax;
                                ShowHelpMarker("深度值将除以此值。");
                                ImGui::PopItemWidth();

                                ImGui::TreePop();
                            }

                            ImGui::Spacing();
                            if (ImGui::TreeNode("同步设置"))
                            {
                                bool useMutexForPresent = Config::Instance()->FGUseMutexForSwapchain.value_or_default();
                                if (ImGui::Checkbox("FG Use Mutex for Present", &useMutexForPresent))
                                    Config::Instance()->FGUseMutexForSwapchain = useMutexForPresent;
                                ShowHelpMarker("使用互斥锁防止帧生成不同步和崩溃，\n"
                                               "禁用可能提升性能，但降低稳定性。");

                                ImGui::TreePop();
                            }

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }
                    else if (currentFeature == nullptr || currentFeature->IsFrozen())
                    {
                        ImGui::Text("超采样器未激活"); // Probably never will be visible
                    }
                    else if (!FfxApiProxy::InitFfxDx12())
                    {
                        ImGui::TextColored({ 1.0f, 0.0f, 0.0f, 1.0f },
                                           "缺少 amd_fidelityfx_dx12.dll 文件！"); // Probably never will be visible
                    }
                }

                // DLSSG Mod
                if (State::Instance().api != DX11 && !State::Instance().isWorkingAsNvngx &&
                    State::Instance().activeFgInput == FGInput::Nukems &&
                    State::Instance().activeFgOutput == FGOutput::Nukems)
                {
                    SeparatorWithHelpMarker("FSR帧生成 (Nukem版DLSSG)",
                                            "需要 Nukem版 dlssg_to_fsr3 dll文件，\n游戏内选择 DLSS 帧生成。");

                    if (!State::Instance().NukemsFilesAvailable)
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f),
                                           "请将 dlssg_to_fsr3_amd_is_better.dll 放到 OptiScaler 同目录下");

                    if (!ReflexHooks::isReflexHooked())
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Reflex 未挂钩");
                        ImGui::Text("如果使用 AMD/Intel GPU，请确保已安装 Fakenvapi");
                    }
                    else if (!ReflexHooks::isDlssgDetected())
                    {
                        ImGui::Text("请在游戏菜单中选择 DLSS 帧生成，\n"
                                    "可能需要先选择 DLSS。");
                    }

                    if (State::Instance().api == DX12)
                    {
                        ImGui::Text("当前DLSS帧生成状态:");
                        ImGui::SameLine();
                        if (ReflexHooks::isDlssgDetected())
                            ImGui::TextColored(ImVec4(0.f, 1.f, 0.25f, 1.f), "开启");
                        else
                            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "关闭");

                        if (bool makeDepthCopy = Config::Instance()->MakeDepthCopy.value_or_default();
                            ImGui::Checkbox("修复破损画面", &makeDepthCopy))
                            Config::Instance()->MakeDepthCopy = makeDepthCopy;
                        ShowHelpMarker("复制深度缓冲区\n在 Windows 下的部分 AMD 显卡游戏中可以修复画面异常，"
                                       "\n可能导致卡顿，建议仅在必要时使用。");
                    }
                    else if (State::Instance().api == Vulkan)
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f),
                                           "当此菜单可见时，DLSS帧生成将被禁用");
                        ImGui::Spacing();
                    }

                    if (DLSSGMod::isLoaded())
                    {
                        if (DLSSGMod::is120orNewer())
                        {
                            if (ImGui::Checkbox("启用调试视图", &State::Instance().DLSSGDebugView))
                            {
                                DLSSGMod::setDebugView(State::Instance().DLSSGDebugView);
                            }
                            if (ImGui::Checkbox("仅插帧", &State::Instance().DLSSGInterpolatedOnly))
                            {
                                DLSSGMod::setInterpolatedOnly(State::Instance().DLSSGInterpolatedOnly);
                            }
                        }
                        else if (DLSSGMod::FSRDebugView() != nullptr)
                        {
                            if (ImGui::Checkbox("启用调试视图", &State::Instance().DLSSGDebugView))
                            {
                                DLSSGMod::FSRDebugView()(State::Instance().DLSSGDebugView);
                            }
                        }
                    }
                }

                // FSR-FG Inputs
                if (State::Instance().api == DX12 && !State::Instance().isWorkingAsNvngx &&
                    State::Instance().activeFgInput == FGInput::FSRFG)
                {
                    SeparatorWithHelpMarker("帧生成 (使用FSR帧生成输入)", "在游戏内选择 FSR帧生成");

                    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);
                    if (fgOutput != nullptr)
                    {
                        ImGui::Text("当前FSR帧生成状态:");
                        ImGui::SameLine();
                        if (State::Instance().FSRFGInputActive)
                        {
                            if (fgOutput->IsActive())
                            {
                                ImGui::TextColored(ImVec4(0.f, 1.f, 0.25f, 1.f), "开启");

                                // TODO: doesn't check if the UI is available, and doesn't save to config
                                if (bool drawUIOverFG = Config::Instance()->DrawUIOverFG.value_or_default();
                                    ImGui::Checkbox("绘制帧生成的UI界面", &drawUIOverFG))
                                    Config::Instance()->DrawUIOverFG = drawUIOverFG;

                                ImGui::SameLine();

                                if (bool uiPremultipliedAlpha =
                                        Config::Instance()->UIPremultipliedAlpha.value_or_default();
                                    ImGui::Checkbox("UI 预乘 Alpha", &uiPremultipliedAlpha))
                                    Config::Instance()->UIPremultipliedAlpha = uiPremultipliedAlpha;
                            }
                            else
                            {
                                ImGui::TextColored(ImVec4(1.0f, 0.647f, 0.0f, 1.f), "帧生成激活");
                            }
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "关闭");
                            ImGui::Text("请在游戏选项中选择 FSR 帧生成，\n"
                                        "可能需要先选择 FSR。");
                        }
                    }
                }

                // Streamline FG Inputs
                if (State::Instance().api == DX12 && !State::Instance().isWorkingAsNvngx &&
                    State::Instance().activeFgInput == FGInput::DLSSG)
                {
                    SeparatorWithHelpMarker("帧生成（Streamline帧生成输入）", "在游戏内选择DLSS帧生成");

                    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

                    if (!ReflexHooks::isReflexHooked())
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Reflex未挂钩");
                        ImGui::Text("如果使用 AMD/Intel GPU，请确保已安装 Fakenvapi");
                    }
                    else if (fgOutput != nullptr)
                    {
                        ImGui::Text("当前Streamline帧生成状态:");
                        ImGui::SameLine();
                        if (fgOutput->IsActive())
                        {
                            ImGui::TextColored(ImVec4(0.f, 1.f, 0.25f, 1.f), "开启");

                            // TODO: doesn't check if the UI is available, and doesn't save to config
                            if (bool drawUIOverFG = Config::Instance()->DrawUIOverFG.value_or_default();
                                ImGui::Checkbox("绘制帧生成的UI界面", &drawUIOverFG))
                                Config::Instance()->DrawUIOverFG = drawUIOverFG;

                            ImGui::SameLine();

                            if (bool uiPremultipliedAlpha = Config::Instance()->UIPremultipliedAlpha.value_or_default();
                                ImGui::Checkbox("UI 预乘 Alpha", &uiPremultipliedAlpha))
                                Config::Instance()->UIPremultipliedAlpha = uiPremultipliedAlpha;
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "关闭");
                            ImGui::Text("请在游戏选项中选择 DLSS 帧生成，\n"
                                        "可能需要先选择 DLSS。");
                        }
                    }
                }

                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    // FSR Common -----------------
                    if (currentFeature != nullptr && !currentFeature->IsFrozen() &&
                        (State::Instance().activeFgOutput == FGOutput::FSRFG || currentBackend.rfind("fsr", 0) == 0))
                    {
                        SeparatorWithHelpMarker("FSR通用设置", "影响FSR帧生成和超采样器");

                        bool useFsrVales = Config::Instance()->FsrUseFsrInputValues.value_or_default();
                        if (ImGui::Checkbox("使用FSR输入值", &useFsrVales))
                            Config::Instance()->FsrUseFsrInputValues = useFsrVales;

                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("视场角（FOV）与相机参数"))
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

                            if (ImGui::RadioButton("使用垂直视场", useVFov))
                            {
                                Config::Instance()->FsrHorizontalFov.reset();
                                Config::Instance()->FsrVerticalFov = vfov;
                                useVFov = true;
                            }

                            ImGui::SameLine(0.0f, 6.0f);

                            if (ImGui::RadioButton("使用水平视场", !useVFov))
                            {
                                Config::Instance()->FsrVerticalFov.reset();
                                Config::Instance()->FsrHorizontalFov = hfov;
                                useVFov = false;
                            }

                            if (useVFov)
                            {
                                if (ImGui::SliderFloat("垂直视场", &vfov, 0.0f, 180.0f, "%.1f"))
                                    Config::Instance()->FsrVerticalFov = vfov;

                                ShowHelpMarker("可能帮助改善图像质量");
                            }
                            else
                            {
                                if (ImGui::SliderFloat("水平视场", &hfov, 0.0f, 180.0f, "%.1f"))
                                    Config::Instance()->FsrHorizontalFov = hfov;

                                ShowHelpMarker("可能帮助改善图像质量");
                            }

                            float cameraNear;
                            float cameraFar;

                            cameraNear = Config::Instance()->FsrCameraNear.value_or_default();
                            cameraFar = Config::Instance()->FsrCameraFar.value_or_default();

                            if (ImGui::SliderFloat("相机近裁剪面", &cameraNear, 0.1f, 500000.0f, "%.1f"))
                                Config::Instance()->FsrCameraNear = cameraNear;
                            ShowHelpMarker("可能改善图像质量，\n"
                                           "并可能减少拖影。");

                            if (ImGui::SliderFloat("相机远裁剪面", &cameraFar, 0.1f, 500000.0f, "%.1f"))
                                Config::Instance()->FsrCameraFar = cameraFar;
                            ShowHelpMarker("可能改善图像质量，\n"
                                           "并可能减少拖影。");

                            if (ImGui::Button("重置相机参数"))
                            {
                                Config::Instance()->FsrVerticalFov.reset();
                                Config::Instance()->FsrHorizontalFov.reset();
                                Config::Instance()->FsrCameraNear.reset();
                                Config::Instance()->FsrCameraFar.reset();
                            }

                            ImGui::SameLine(0.0f, 6.0f);
                            ImGui::Text(
                                "近裁剪面: %.1f 远裁剪面: %.1f",
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
                        ImGui::SeparatorText("DLSS 启用器");

                        ImGui::BeginDisabled(Config::Instance()->DE_FramerateLimitVsync.value_or(false));

                        // set inital value
                        if (Config::Instance()->DE_FramerateLimit.has_value() && _deLimitFps > 200)
                            _deLimitFps = Config::Instance()->DE_FramerateLimit.value();

                        ImGui::SliderInt("帧率限制", &_deLimitFps, 0, 200);

                        if (ImGui::Button("应用限制"))
                            Config::Instance()->DE_FramerateLimit = _deLimitFps;

                        ImGui::EndDisabled();

                        ImGui::SameLine(0.0f, 6.0f);

                        bool fpsLimitVsync = Config::Instance()->DE_FramerateLimitVsync.value_or(false);
                        if (ImGui::Checkbox("垂直同步", &fpsLimitVsync))
                            Config::Instance()->DE_FramerateLimitVsync = fpsLimitVsync;
                        ShowHelpMarker("将帧率限制为显示器刷新率");

                        if (Config::Instance()->DE_DynamicLimitAvailable.has_value() &&
                            Config::Instance()->DE_DynamicLimitAvailable.value() > 0)
                        {
                            ImGui::SameLine(0.0f, 6.0f);

                            bool dfgEnabled = Config::Instance()->DE_DynamicLimitEnabled.value_or(false);
                            if (ImGui::Checkbox("动态帧生成", &dfgEnabled))
                                Config::Instance()->DE_DynamicLimitEnabled = dfgEnabled;
                            ShowHelpMarker("仅当帧率低于限制时启用帧生成。\n"
                                           "低帧率情况下会导致输入延迟增加，\n"
                                           "但能保持运动的流畅性。");
                        }

                        if (ImGui::CollapsingHeader("高级启用器设置"))
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

                            if (ImGui::BeginCombo("生成器", selected.c_str()))
                            {
                                if (ImGui::Selectable("自动",
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
                            ShowHelpMarker("使用的帧生成算法");

                            if (Config::Instance()->DE_Reflex.value_or("on") == "on")
                                selected = "On";
                            else if (Config::Instance()->DE_Reflex.value_or("on") == "boost")
                                selected = "Boost";
                            else if (Config::Instance()->DE_Reflex.value_or("on") == "off")
                                selected = "Off";

                            if (ImGui::BeginCombo("Reflex", selected.c_str()))
                            {
                                if (ImGui::Selectable("开启", Config::Instance()->DE_Reflex.value_or("on") == "on"))
                                    Config::Instance()->DE_Reflex = "on";

                                if (ImGui::Selectable("增强", Config::Instance()->DE_Reflex.value_or("on") == "boost"))
                                    Config::Instance()->DE_Reflex = "boost";

                                if (ImGui::Selectable("关闭", Config::Instance()->DE_Reflex.value_or("on") == "off"))
                                    Config::Instance()->DE_Reflex = "off";

                                ImGui::EndCombo();
                            }

                            if (Config::Instance()->DE_ReflexEmu.value_or("auto") == "auto")
                                selected = "Auto";
                            else if (Config::Instance()->DE_ReflexEmu.value_or("auto") == "on")
                                selected = "On";
                            else if (Config::Instance()->DE_ReflexEmu.value_or("auto") == "off")
                                selected = "Off";

                            if (ImGui::BeginCombo("Reflex模拟", selected.c_str()))
                            {
                                if (ImGui::Selectable("自动",
                                                      Config::Instance()->DE_ReflexEmu.value_or("auto") == "auto"))
                                    Config::Instance()->DE_ReflexEmu = "auto";

                                if (ImGui::Selectable("开启", Config::Instance()->DE_ReflexEmu.value_or("auto") == "on"))
                                    Config::Instance()->DE_ReflexEmu = "on";

                                if (ImGui::Selectable("关闭",
                                                      Config::Instance()->DE_ReflexEmu.value_or("auto") == "off"))
                                    Config::Instance()->DE_ReflexEmu = "off";

                                ImGui::EndCombo();
                            }
                            ShowHelpMarker("不会降低输入延迟，\n但可稳定帧率。");
                        }
                    }
                }

                // Framerate ---------------------
                if (!State::Instance().enablerAvailable &&
                    (State::Instance().reflexLimitsFps || Config::Instance()->OverlayMenu))
                {
                    SeparatorWithHelpMarker(
                        "帧率",
                        "尽可能使用Reflex，\n在AMD/Intel GPU上可使用Fakenvapi替代Reflex");

                    static std::string currentMethod {};
                    if (State::Instance().reflexLimitsFps)
                    {
                        if (fakenvapi::updateModeAndContext())
                        {
                            auto mode = fakenvapi::getCurrentMode();

                            if (mode == Mode::AntiLag2)
                                currentMethod = "AntiLag 2 低延迟";
                            else if (mode == Mode::LatencyFlex)
                                currentMethod = "LatencyFlex低延迟";
                            else if (mode == Mode::XeLL)
                                currentMethod = "XeLL技术";
                            else if (mode == Mode::AntiLagVk)
                                currentMethod = "Vulkan AntiLag低延迟";

                            if (State::Instance().rtssReflexInjection && mode == Mode::AntiLag2 &&
                                Config::Instance()->FGInput == FGInput::Upscaler)
                                ImGui::TextColored(
                                    ImVec4(1.f, 0.8f, 0.f, 1.f),
                                    "结合AntiLag 2和Opti帧生成使用RTSS Reflex注入可能导致问题");
                        }
                        else
                        {
                            currentMethod = "Reflex低延迟";
                        }
                    }
                    else
                    {
                        currentMethod = "回退模式";
                    }

                    if (State::Instance().rtssReflexInjection)
                        currentMethod.append(" (RTSS)");

                    ImGui::Text(std::format("当前方法: {}", currentMethod).c_str());

                    if (State::Instance().reflexShowWarning)
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f),
                                           "结合Opti帧生成使用Reflex的限制会带来额外的性能开销");

                        ImGui::Spacing();
                    }

                    // set initial value
                    if (_limitFps == INFINITY)
                        _limitFps = Config::Instance()->FramerateLimit.value_or_default();

                    ImGui::SliderFloat("帧率限制", &_limitFps, 0, 200, "%.0f");

                    if (ImGui::Button("应用限制"))
                    {
                        Config::Instance()->FramerateLimit = _limitFps;
                    }
                }

                // FAKENVAPI ---------------------------
                if (fakenvapi::isUsingFakenvapi())
                {
                    ImGui::SeparatorText("Fakenvapi设置");

                    if (bool logs = Config::Instance()->FN_EnableLogs.value_or_default();
                        ImGui::Checkbox("启用文件日志记录", &logs))
                        Config::Instance()->FN_EnableLogs = logs;

                    ImGui::BeginDisabled(!Config::Instance()->FN_EnableLogs.value_or_default());

                    ImGui::SameLine(0.0f, 6.0f);
                    if (bool traceLogs = Config::Instance()->FN_EnableTraceLogs.value_or_default();
                        ImGui::Checkbox("启用跟踪日志", &traceLogs))
                        Config::Instance()->FN_EnableTraceLogs = traceLogs;

                    ImGui::EndDisabled();

                    if (bool forceLFX = Config::Instance()->FN_ForceLatencyFlex.value_or_default();
                        ImGui::Checkbox("强制使用LatencyFlex", &forceLFX))
                        Config::Instance()->FN_ForceLatencyFlex = forceLFX;
                    ShowHelpMarker(
                        "AntiLag 2 / XeLL在可用时使用，此设置允许强制使用LatencyFlex。");

                    const char* lfx_modes[] = { "保守模式", "激进模式", "Reflex ID" };
                    const std::string lfx_modesDesc[] = {
                        "最安全，但可能无法有效降低延迟",
                        "改善延迟，但在某些情况下可能比预期帧率更低",
                        "最佳选择，某些游戏不兼容（如赛博朋克）将回退到"
                        "激进模式。"
                    };

                    PopulateCombo("LatencyFlex 模式", &Config::Instance()->FN_LatencyFlexMode, lfx_modes, lfx_modesDesc,
                                  3);

                    const char* rfx_modes[] = { "跟随游戏设置", "强制关闭", "强制开启" };
                    const std::string rfx_modesDesc[] = { "", "", "" };

                    PopulateCombo("强制启用 Reflex", &Config::Instance()->FN_ForceReflex, rfx_modes, rfx_modesDesc, 3);

                    if (ImGui::Button("应用##2"))
                    {
                        Config::Instance()->SaveFakenvapiIni();
                    }
                }

                // NEXT COLUMN -----------------
                ImGui::TableNextColumn();

                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    // SHARPNESS -----------------------------
                    ImGui::SeparatorText("锐化");

                    if (bool overrideSharpness = Config::Instance()->OverrideSharpness.value_or_default();
                        ImGui::Checkbox("覆盖默认值", &overrideSharpness))
                    {
                        Config::Instance()->OverrideSharpness = overrideSharpness;

                        if (currentBackend == "dlss" && State::Instance().currentFeature->Version().major < 3)
                        {
                            State::Instance().newBackend = currentBackend;
                            MARK_ALL_BACKENDS_CHANGED();
                        }
                    }
                    ShowHelpMarker("忽略游戏设置的默认值，\n"
                                   "使用下方设置的值。");

                    ImGui::BeginDisabled(!Config::Instance()->OverrideSharpness.value_or_default());

                    float sharpness = Config::Instance()->Sharpness.value_or_default();
                    auto justRcasEnabled = Config::Instance()->RcasEnabled.value_or(rcasEnabled) &&
                                           !Config::Instance()->ContrastEnabled.value_or_default();
                    float sharpnessLimit = justRcasEnabled ? 1.3f : 1.0f;

                    if (ImGui::SliderFloat("锐化强度", &sharpness, 0.0f, sharpnessLimit))
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
                            ImGui::Checkbox("启用 RCAS", &rcas))
                            Config::Instance()->RcasEnabled = rcas;
                        ShowHelpMarker("锐化滤镜\n"
                                       "默认使用游戏提供的锐化值\n"
                                       "在“锐化”中勾选“覆盖”并调整滑块即可修改\n\n"
                                       "部分超采样器自带锐化功能，因此并非总是需要 RCAS");

                        ImGui::BeginDisabled(!Config::Instance()->RcasEnabled.value_or(rcasEnabled));

                        if (bool contrastEnabled = Config::Instance()->ContrastEnabled.value_or_default();
                            ImGui::Checkbox("启用对比度增强", &contrastEnabled))
                            Config::Instance()->ContrastEnabled = contrastEnabled;

                        ShowHelpMarker("增强高对比度区域的锐化。");

                        if (Config::Instance()->ContrastEnabled.value_or_default() &&
                            Config::Instance()->Sharpness.value_or_default() > 1.0f)
                            Config::Instance()->Sharpness = 1.0f;

                        ImGui::BeginDisabled(!Config::Instance()->ContrastEnabled.value_or_default());

                        float contrast = Config::Instance()->Contrast.value_or_default();
                        if (ImGui::SliderFloat("对比度", &contrast, 0.0f, 2.0f, "%.2f"))
                            Config::Instance()->Contrast = contrast;

                        ShowHelpMarker("较高值会增强高对比度区域的锐化。\n"
                                       "数值过高时可能导致画面异常，\n"
                                       "尤其是在高锐化强度下!");

                        ImGui::EndDisabled();

                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("运动自适应锐化##2"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            if (bool overrideMotionSharpness =
                                    Config::Instance()->MotionSharpnessEnabled.value_or_default();
                                ImGui::Checkbox("运动自适应锐化", &overrideMotionSharpness))
                                Config::Instance()->MotionSharpnessEnabled = overrideMotionSharpness;
                            ShowHelpMarker("对运动物体应用更多锐化");

                            ImGui::BeginDisabled(!Config::Instance()->MotionSharpnessEnabled.value_or_default());

                            ImGui::SameLine(0.0f, 6.0f);

                            if (bool overrideMSDebug = Config::Instance()->MotionSharpnessDebug.value_or_default();
                                ImGui::Checkbox("运动自适应锐化调试", &overrideMSDebug))
                                Config::Instance()->MotionSharpnessDebug = overrideMSDebug;
                            ShowHelpMarker("红色区域将应用更多锐化\n"
                                           "绿色区域锐化减少");

                            float motionSharpness = Config::Instance()->MotionSharpness.value_or_default();
                            ImGui::SliderFloat("运动锐化", &motionSharpness, -1.3f, 1.3f, "%.3f");
                            Config::Instance()->MotionSharpness = motionSharpness;

                            float motionThreshod = Config::Instance()->MotionThreshold.value_or_default();
                            ImGui::SliderFloat("运动阈值", &motionThreshod, 0.0f, 100.0f, "%.2f");
                            Config::Instance()->MotionThreshold = motionThreshod;

                            float motionScale = Config::Instance()->MotionScaleLimit.value_or_default();
                            ImGui::SliderFloat("运动范围", &motionScale, 0.01f, 100.0f, "%.2f");
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

                    ImGui::SeparatorText("超采样比例覆盖");

                    if (bool upOverride = Config::Instance()->UpscaleRatioOverrideEnabled.value_or_default();
                        ImGui::Checkbox("全局覆盖", &upOverride))
                    {
                        Config::Instance()->UpscaleRatioOverrideEnabled = upOverride;

                        if (upOverride)
                            Config::Instance()->QualityRatioOverrideEnabled = false;
                    }
                    ShowHelpMarker("允许覆盖所有超采样器预设\n"
                                   "使用下方设置的值\n\n"
                                   "在1080p屏幕上1.5x表示内部分辨率为720p\n"
                                   "1080 / 1.5 = 720");

                    if (bool qOverride = Config::Instance()->QualityRatioOverrideEnabled.value_or_default();
                        ImGui::Checkbox("按画质预设分别覆盖", &qOverride))
                    {
                        Config::Instance()->QualityRatioOverrideEnabled = qOverride;

                        if (qOverride)
                            Config::Instance()->UpscaleRatioOverrideEnabled = false;
                    }

                    ShowHelpMarker("允许单独覆盖每个画质预设的比率\n"
                                   "注意并非所有游戏支持每种画质预设\n\n"
                                   "在1080p屏幕上1.5x表示内部分辨率为720p\n"
                                   "1080 / 1.5 = 720");

                    if (Config::Instance()->UpscaleRatioOverrideEnabled.value_or_default())
                    {
                        float urOverride = Config::Instance()->UpscaleRatioOverrideValue.value_or_default();
                        ImGui::SliderFloat("所有比率", &urOverride, minSliderLimit, maxSliderLimit, "%.3f");
                        Config::Instance()->UpscaleRatioOverrideValue = urOverride;
                    }

                    if (Config::Instance()->QualityRatioOverrideEnabled.value_or_default())
                    {
                        float qDlaa = Config::Instance()->QualityRatio_DLAA.value_or_default();
                        if (ImGui::SliderFloat("DLAA模式", &qDlaa, minSliderLimit, maxSliderLimit, "%.3f"))
                            Config::Instance()->QualityRatio_DLAA = qDlaa;

                        float qUq = Config::Instance()->QualityRatio_UltraQuality.value_or_default();
                        if (ImGui::SliderFloat("超高质量模式", &qUq, minSliderLimit, maxSliderLimit, "%.3f"))
                            Config::Instance()->QualityRatio_UltraQuality = qUq;

                        float qQ = Config::Instance()->QualityRatio_Quality.value_or_default();
                        if (ImGui::SliderFloat("质量模式", &qQ, minSliderLimit, maxSliderLimit, "%.3f"))
                            Config::Instance()->QualityRatio_Quality = qQ;

                        float qB = Config::Instance()->QualityRatio_Balanced.value_or_default();
                        if (ImGui::SliderFloat("平衡模式", &qB, minSliderLimit, maxSliderLimit, "%.3f"))
                            Config::Instance()->QualityRatio_Balanced = qB;

                        float qP = Config::Instance()->QualityRatio_Performance.value_or_default();
                        if (ImGui::SliderFloat("性能模式", &qP, minSliderLimit, maxSliderLimit, "%.3f"))
                            Config::Instance()->QualityRatio_Performance = qP;

                        float qUp = Config::Instance()->QualityRatio_UltraPerformance.value_or_default();
                        if (ImGui::SliderFloat("极致性能模式", &qUp, minSliderLimit, maxSliderLimit, "%.3f"))
                            Config::Instance()->QualityRatio_UltraPerformance = qUp;
                    }

                    if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        // OUTPUT SCALING -----------------------------
                        if (State::Instance().api == DX12 || State::Instance().api == DX11)
                        {
                            // if motion vectors are not display size
                            ImGui::BeginDisabled(!currentFeature->LowResMV());

                            ImGui::SeparatorText("输出缩放");

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
                            ImGui::Checkbox("启用", &_ssEnabled);
                            ImGui::EndDisabled();

                            ShowHelpMarker("将图像内部放大到指定分辨率\n"
                                           "之后再缩放到显示分辨率\n\n"
                                           "值<1.0可降低超采样器性能开销\n"
                                           "值>1.0可提高图像锐度但牺牲性能\n\n"
                                           "可以在此菜单底部查看每一步的效果");

                            ImGui::SameLine(0.0f, 6.0f);

                            ImGui::BeginDisabled(!_ssEnabled);
                            {
                                ImGui::Checkbox("使用 FSR 1", &_ssUseFsr);
                                ShowHelpMarker("使用 FSR 1 进行缩放");

                                ImGui::SameLine(0.0f, 6.0f);

                                ImGui::BeginDisabled(_ssUseFsr || _ssRatio < 1.0f);
                                {
                                    const char* ds_modes[] = { "Bicubic", "Lanczos", "Catmull-Rom", "MAGC" };
                                    const std::string ds_modesDesc[] = { "", "", "", "" };

                                    ImGui::PushItemWidth(75.0f * Config::Instance()->MenuScale.value());
                                    PopulateCombo("重采样算法", &Config::Instance()->OutputScalingDownscaler, ds_modes,
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
                            if (ImGui::Button("应用更改"))
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
                            ImGui::SliderFloat("缩放比例", &_ssRatio, 0.5f, 3.0f, "%.2f");
                            ImGui::EndDisabled();

                            if (currentFeature != nullptr && !currentFeature->IsFrozen())
                            {
                                ImGui::Text("输出缩放状态: %s, 目标分辨率: %dx%d\n抖动次数: %d",
                                            Config::Instance()->OutputScalingEnabled.value_or_default() ? "启用"
                                                                                                        : "禁用",
                                            (uint32_t) (currentFeature->DisplayWidth() * _ssRatio),
                                            (uint32_t) (currentFeature->DisplayHeight() * _ssRatio),
                                            currentFeature->JitterCount());
                            }

                            ImGui::EndDisabled();
                        }
                    }

                    // INIT -----------------------------
                    ImGui::SeparatorText("初始化标志");
                    if (ImGui::BeginTable("init", 2, ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableNextColumn();

                        // AutoExposure is always enabled for XeSS with native Dx11
                        bool autoExposureDisabled = State::Instance().api == API::DX11 && currentBackend == "xess";
                        ImGui::BeginDisabled(autoExposureDisabled);

                        if (bool autoExposure = currentFeature->AutoExposure();
                            ImGui::Checkbox("自动曝光", &autoExposure))
                        {
                            Config::Instance()->AutoExposure = autoExposure;
                            ReInitUpscaler();
                        }
                        ShowResetButton(&Config::Instance()->AutoExposure, "重置");
                        ShowHelpMarker("某些虚幻引擎游戏需要此功能\n"
                                       "可能修复暗部颜色问题");

                        ImGui::EndDisabled();

                        ImGui::TableNextColumn();
                        auto accessToReactiveMask = State::Instance().currentFeature->AccessToReactiveMask();
                        ImGui::BeginDisabled(!accessToReactiveMask);

                        bool canUseReactiveMask =
                            accessToReactiveMask && currentBackend != "dlss" &&
                            (currentBackend != "xess" || currentFeature->Version() >= feature_version { 2, 0, 1 });

                        bool disableReactiveMask =
                            Config::Instance()->DisableReactiveMask.value_or(!canUseReactiveMask);

                        if (ImGui::Checkbox("禁用反应遮罩", &disableReactiveMask))
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
                            ShowHelpMarker("允许使用反应式遮罩\n"
                                           "请注意，将反应式遮罩发送到 DLSS 时\n"
                                           "与 FSR/XeSS 结合可能导致画质不佳");
                        else
                            ShowHelpMarker("选项已禁用，因为游戏未提供反应式遮罩");

                        ImGui::EndTable();

                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("高级初始化标志"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            if (ImGui::BeginTable("init2", 2, ImGuiTableFlags_SizingStretchProp))
                            {
                                ImGui::TableNextColumn();
                                if (bool depth = currentFeature->DepthInverted();
                                    ImGui::Checkbox("深度反转", &depth))
                                {
                                    Config::Instance()->DepthInverted = depth;
                                    ReInitUpscaler();
                                }
                                ShowResetButton(&Config::Instance()->DepthInverted, "重置##2");
                                ShowHelpMarker("通常无需更改此设置");

                                ImGui::TableNextColumn();
                                if (bool hdr = currentFeature->IsHdr(); ImGui::Checkbox("启用HDR", &hdr))
                                {
                                    Config::Instance()->HDR = hdr;
                                    ReInitUpscaler();
                                }
                                ShowResetButton(&Config::Instance()->HDR, "重置##1");
                                ShowHelpMarker("可能有助于修复某些游戏中的紫色偏色");

                                ImGui::TableNextColumn();
                                if (bool mv = !currentFeature->LowResMV(); ImGui::Checkbox("显示分辨率运动向量", &mv))
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
                                ShowResetButton(&Config::Instance()->DisplayResolution, "重置##4");
                                ShowHelpMarker("主要用于修复虚幻引擎游戏\n"
                                               "屏幕左上角区域可能会变模糊");

                                ImGui::TableNextColumn();

                                if (bool jitter = currentFeature->JitteredMV();
                                    ImGui::Checkbox("抖动消除", &jitter))
                                {
                                    Config::Instance()->JitterCancellation = jitter;
                                    ReInitUpscaler();
                                }
                                ShowResetButton(&Config::Instance()->JitterCancellation, "重置##3");
                                ShowHelpMarker("用于修复某些游戏在发送运动数据时已预先应用抖动的问题");

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
                                    if (ImGui::SliderFloat("反应式遮罩偏移", &maskBias, 0.0f, 0.9f, "%.2f"))
                                        Config::Instance()->DlssReactiveMaskBias = maskBias;

                                    ShowHelpMarker("数值大于 0 时会启用反应式遮罩");
                                }
                                else
                                {
                                    bool useRM = maskBias > 0.0f;
                                    if (ImGui::Checkbox("使用二进制反应式遮罩", &useRM))
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
                if (ImGui::CollapsingHeader("高级设置"))
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        bool extendedLimits = Config::Instance()->ExtendedLimits.value_or_default();
                        if (ImGui::Checkbox("启用扩展限制", &extendedLimits))
                            Config::Instance()->ExtendedLimits = extendedLimits;

                        ShowHelpMarker("为画质预设扩展滑块限制\n\n"
                                       "启用此选项会改变分辨率检测逻辑\n"
                                       "可能导致问题或崩溃！");
                    }

                    bool pcShaders = Config::Instance()->UsePrecompiledShaders.value_or_default();
                    if (ImGui::Checkbox("使用预编译着色器", &pcShaders))
                    {
                        Config::Instance()->UsePrecompiledShaders = pcShaders;
                        State::Instance().newBackend = currentBackend;
                        MARK_ALL_BACKENDS_CHANGED();
                    }

                    // DRS
                    ImGui::SeparatorText("DRS（动态分辨率缩放）");
                    if (ImGui::BeginTable("drs", 2, ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableNextColumn();
                        if (bool drsMin = Config::Instance()->DrsMinOverrideEnabled.value_or_default();
                            ImGui::Checkbox("覆盖最小值", &drsMin))
                            Config::Instance()->DrsMinOverrideEnabled = drsMin;
                        ShowHelpMarker("修复某些游戏忽略官方 DRS 限制的问题");

                        ImGui::TableNextColumn();
                        if (bool drsMax = Config::Instance()->DrsMaxOverrideEnabled.value_or_default();
                            ImGui::Checkbox("覆盖最大值", &drsMax))
                            Config::Instance()->DrsMaxOverrideEnabled = drsMax;
                        ShowHelpMarker("修复某些游戏忽略官方 DRS 限制的问题");

                        ImGui::EndTable();
                    }

                    // Non-DLSS hotfixes -----------------------------
                    if (currentFeature != nullptr && !currentFeature->IsFrozen() && currentBackend != "dlss")
                    {
                        // BARRIERS -----------------------------
                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("资源屏障"))
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            AddResourceBarrier("颜色", &Config::Instance()->ColorResourceBarrier);
                            AddResourceBarrier("深度", &Config::Instance()->DepthResourceBarrier);
                            AddResourceBarrier("运动", &Config::Instance()->MVResourceBarrier);
                            AddResourceBarrier("曝光", &Config::Instance()->ExposureResourceBarrier);
                            AddResourceBarrier("遮罩", &Config::Instance()->MaskResourceBarrier);
                            AddResourceBarrier("输出", &Config::Instance()->OutputResourceBarrier);
                        }

                        // HOTFIXES -----------------------------
                        if (State::Instance().api == DX12)
                        {
                            ImGui::Spacing();
                            if (ImGui::CollapsingHeader("根签名"))
                            {
                                ScopedIndent indent {};
                                ImGui::Spacing();

                                if (bool crs = Config::Instance()->RestoreComputeSignature.value_or_default();
                                    ImGui::Checkbox("恢复计算根签名", &crs))
                                    Config::Instance()->RestoreComputeSignature = crs;

                                if (bool grs = Config::Instance()->RestoreGraphicSignature.value_or_default();
                                    ImGui::Checkbox("恢复图形根签名", &grs))
                                    Config::Instance()->RestoreGraphicSignature = grs;
                            }
                        }
                    }
                }

                // LOGGING -----------------------------
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("日志记录"))
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
                        ImGui::Checkbox("输出到文件", &toFile))
                    {
                        Config::Instance()->LogToFile = toFile;
                        PrepareLogger();
                    }

                    ImGui::SameLine(0.0f, 6.0f);
                    if (bool toConsole = Config::Instance()->LogToConsole.value_or_default();
                        ImGui::Checkbox("输出到控制台", &toConsole))
                    {
                        Config::Instance()->LogToConsole = toConsole;
                        PrepareLogger();
                    }

                    const char* logLevels[] = { "跟踪", "调试", "信息", "警告", "错误" };
                    const char* selectedLevel = logLevels[Config::Instance()->LogLevel.value_or_default()];

                    if (ImGui::BeginCombo("日志级别", selectedLevel))
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
                if (ImGui::CollapsingHeader("帧率叠加层"))
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    bool fpsEnabled = Config::Instance()->ShowFps.value_or_default();
                    if (ImGui::Checkbox("启用帧率叠加显示", &fpsEnabled))
                        Config::Instance()->ShowFps = fpsEnabled;

                    ImGui::SameLine(0.0f, 6.0f);

                    bool fpsHorizontal = Config::Instance()->FpsOverlayHorizontal.value_or_default();
                    if (ImGui::Checkbox("水平显示", &fpsHorizontal))
                        Config::Instance()->FpsOverlayHorizontal = fpsHorizontal;

                    const char* fpsPosition[] = { "左上角", "右上角", "左下角", "右下角" };
                    const char* selectedPosition = fpsPosition[Config::Instance()->FpsOverlayPos.value_or_default()];

                    if (ImGui::BeginCombo("叠加显示位置", selectedPosition))
                    {
                        for (int n = 0; n < 4; n++)
                        {
                            if (ImGui::Selectable(fpsPosition[n],
                                                  (Config::Instance()->FpsOverlayPos.value_or_default() == n)))
                                Config::Instance()->FpsOverlayPos = n;
                        }

                        ImGui::EndCombo();
                    }

                    const char* fpsType[] = { "仅显示FPS",         "简单模式", "详细模式",
                                              "详细 + 图表", "完整模式",   "完整 + 图表" };
                    const char* selectedType = fpsType[Config::Instance()->FpsOverlayType.value_or_default()];

                    if (ImGui::BeginCombo("叠加显示类型", selectedType))
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
                    if (ImGui::SliderFloat("背景透明度", &fpsAlpha, 0.0f, 1.0f, "%.2f"))
                        Config::Instance()->FpsOverlayAlpha = fpsAlpha;

                    const char* options[] = { "与菜单相同", "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1", "1.2",
                                              "1.3",          "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
                    int currentIndex = std::max(((int) (Config::Instance()->FpsScale.value_or(0.0f) * 10.0f)) - 4, 0);
                    float values[] = { 0.0f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f,
                                       1.3f, 1.4f, 1.5f, 1.6f, 1.7f, 1.8f, 1.9f, 2.0f };

                    if (ImGui::SliderInt("缩放比例", &currentIndex, 0, IM_ARRAYSIZE(options) - 1, options[currentIndex],
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
                if (ImGui::CollapsingHeader("超采样输入设置", uiStateOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0))
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (Config::Instance()->EnableFsr2Inputs.value_or_default())
                    {
                        bool fsr2Inputs = Config::Instance()->UseFsr2Inputs.value_or_default();
                        bool fsr2Pattern = Config::Instance()->Fsr2Pattern.value_or_default();

                        if (ImGui::Checkbox("使用 FSR2 输入", &fsr2Inputs))
                            Config::Instance()->UseFsr2Inputs = fsr2Inputs;

                        if (ImGui::Checkbox("使用 FSR2 模式匹配", &fsr2Pattern))
                            Config::Instance()->Fsr2Pattern = fsr2Pattern;
                        ShowTooltip("此设置将在下次启动时生效！");
                    }

                    if (Config::Instance()->EnableFsr3Inputs.value_or_default())
                    {
                        bool fsr3Inputs = Config::Instance()->UseFsr3Inputs.value_or_default();
                        bool fsr3Pattern = Config::Instance()->Fsr3Pattern.value_or_default();

                        if (ImGui::Checkbox("使用 FSR3 输入", &fsr3Inputs))
                            Config::Instance()->UseFsr3Inputs = fsr3Inputs;

                        if (ImGui::Checkbox("使用 FSR3 模式匹配", &fsr3Pattern))
                            Config::Instance()->Fsr3Pattern = fsr3Pattern;
                        ShowTooltip("此设置将在下次启动时生效！");
                    }

                    if (Config::Instance()->EnableFfxInputs.value_or_default())
                    {
                        bool ffxInputs = Config::Instance()->UseFfxInputs.value_or_default();

                        if (ImGui::Checkbox("使用 FFX 输入", &ffxInputs))
                            Config::Instance()->UseFfxInputs = ffxInputs;
                    }
                }

                // DX11 & DX12 -----------------------------
                if (State::Instance().api != Vulkan)
                {
                    // V-SYNC -----------------------------
                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("垂直同步设置"))
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();

                        auto forceVsyncOn =
                            Config::Instance()->ForceVsync.has_value() && Config::Instance()->ForceVsync.value();
                        auto forceVsyncOff =
                            Config::Instance()->ForceVsync.has_value() && !Config::Instance()->ForceVsync.value();

                        if (ImGui::Checkbox("启用垂直同步", &forceVsyncOn))
                        {
                            if (forceVsyncOn)
                                Config::Instance()->ForceVsync = true;
                            else
                                Config::Instance()->ForceVsync.reset();
                        }
                        ImGui::SameLine(0.0f, 16.0f);

                        if (ImGui::Checkbox("禁用垂直同步", &forceVsyncOff))
                        {
                            if (forceVsyncOff)
                                Config::Instance()->ForceVsync = false;
                            else
                                Config::Instance()->ForceVsync.reset();
                        }
                        ImGui::SameLine(0.0f, 16.0f);

                        ImGui::BeginDisabled(!forceVsyncOn);

                        ImGui::PushItemWidth(50.0f * Config::Instance()->MenuScale.value_or_default());
                        if (ImGui::BeginCombo(
                                "同步间隔",
                                std::format("{}", Config::Instance()->VsyncInterval.value_or_default()).c_str()))
                        {
                            if (ImGui::Selectable("0", Config::Instance()->VsyncInterval.value_or_default() == 0))
                                Config::Instance()->VsyncInterval = 0;

                            if (ImGui::Selectable("1", Config::Instance()->VsyncInterval.value_or_default() == 1))
                                Config::Instance()->VsyncInterval = 1;

                            if (ImGui::Selectable("2", Config::Instance()->VsyncInterval.value_or_default() == 2))
                                Config::Instance()->VsyncInterval = 2;

                            if (ImGui::Selectable("3", Config::Instance()->VsyncInterval.value_or_default() == 3))
                                Config::Instance()->VsyncInterval = 3;

                            ImGui::EndCombo();
                        }
                        ImGui::PopItemWidth();

                        ImGui::EndDisabled();
                        ImGui::SameLine(0.0f, 16.0f);

                        if (ImGui::Button("重置##10"))
                            Config::Instance()->ForceVsync.reset();

                        ShowHelpMarker("强制启用/禁用垂直同步及同步间隔选项");
                    }

                    // MIPMAP BIAS & Anisotropy -----------------------------
                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("多级纹理偏差（Mipmap Bias）", (currentFeature == nullptr || currentFeature->IsFrozen())
                                                                   ? ImGuiTreeNodeFlags_DefaultOpen
                                                                   : 0))
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();
                        if (Config::Instance()->MipmapBiasOverride.has_value() && _mipBias == 0.0f)
                            _mipBias = Config::Instance()->MipmapBiasOverride.value();

                        ImGui::SliderFloat("多级纹理偏差（MB）##2", &_mipBias, -15.0f, 15.0f, "%.6f");
                        ShowHelpMarker("可帮助修复游戏中模糊的纹理\n"
                                       "负值可使纹理更锐利\n"
                                       "正值会让纹理更模糊\n\n"
                                       "对性能影响较小");

                        ImGui::BeginDisabled(!Config::Instance()->MipmapBiasOverride.has_value());
                        {
                            ImGui::BeginDisabled(Config::Instance()->MipmapBiasScaleOverride.has_value() &&
                                                 Config::Instance()->MipmapBiasScaleOverride.value());
                            {
                                bool mbFixed = Config::Instance()->MipmapBiasFixedOverride.value_or_default();
                                if (ImGui::Checkbox("固定偏差覆盖", &mbFixed))
                                {
                                    Config::Instance()->MipmapBiasScaleOverride.reset();
                                    Config::Instance()->MipmapBiasFixedOverride = mbFixed;
                                }

                                ShowHelpMarker("对所有纹理应用相同的覆盖值");
                            }
                            ImGui::EndDisabled();

                            ImGui::SameLine(0.0f, 6.0f);

                            ImGui::BeginDisabled(Config::Instance()->MipmapBiasFixedOverride.has_value() &&
                                                 Config::Instance()->MipmapBiasFixedOverride.value());
                            {
                                bool mbScale = Config::Instance()->MipmapBiasScaleOverride.value_or_default();
                                if (ImGui::Checkbox("缩放偏差覆盖", &mbScale))
                                {
                                    Config::Instance()->MipmapBiasFixedOverride.reset();
                                    Config::Instance()->MipmapBiasScaleOverride = mbScale;
                                }

                                ShowHelpMarker("将覆盖值作为缩放倍数应用\n"
                                               "在缩放模式下请使用正值\n"
                                               "以增加清晰度！");
                            }
                            ImGui::EndDisabled();

                            bool mbAll = Config::Instance()->MipmapBiasOverrideAll.value_or_default();
                            if (ImGui::Checkbox("覆盖所有纹理", &mbAll))
                                Config::Instance()->MipmapBiasOverrideAll = mbAll;

                            ShowHelpMarker("覆盖所有纹理的多级纹理值\n"
                                           "对于OptiScaler 来讲\n"
                                           "一般只覆盖小于零的多级纹理值");
                        }
                        ImGui::EndDisabled();

                        ImGui::BeginDisabled(Config::Instance()->MipmapBiasOverride.has_value() &&
                                             Config::Instance()->MipmapBiasOverride.value() == _mipBias);
                        {
                            if (ImGui::Button("设置"))
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
                            if (ImGui::Button("重置"))
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

                            if (ImGui::Button("计算多级纹理偏差"))
                                _showMipmapCalcWindow = true;
                        }

                        if (Config::Instance()->MipmapBiasOverride.has_value())
                        {
                            if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                            {
                                ImGui::Text("当前值 : %.3f / %.3f, 目标值: %.3f", State::Instance().lastMipBias,
                                            State::Instance().lastMipBiasMax,
                                            Config::Instance()->MipmapBiasOverride.value());
                            }
                            else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                            {
                                ImGui::Text("当前值 : %.3f / %.3f, 目标值: 基础 * %.3f", State::Instance().lastMipBias,
                                            State::Instance().lastMipBiasMax,
                                            Config::Instance()->MipmapBiasOverride.value());
                            }
                            else
                            {
                                ImGui::Text("当前值 : %.3f / %.3f, 目标值: 基础 + %.3f", State::Instance().lastMipBias,
                                            State::Instance().lastMipBiasMax,
                                            Config::Instance()->MipmapBiasOverride.value());
                            }
                        }
                        else
                        {
                            ImGui::Text("当前值 : %.3f / %.3f", State::Instance().lastMipBias,
                                        State::Instance().lastMipBiasMax);
                        }

                        ImGui::Text("将在分辨率/预设更改后应用!!!");
                    }

                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("各向异性过滤",
                                                (currentFeature == nullptr || currentFeature->IsFrozen())
                                                    ? ImGuiTreeNodeFlags_DefaultOpen
                                                    : 0))
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();
                        ImGui::PushItemWidth(65.0f * Config::Instance()->MenuScale.value());

                        auto selectedAF = Config::Instance()->AnisotropyOverride.has_value()
                                              ? std::to_string(Config::Instance()->AnisotropyOverride.value())
                                              : "自动";
                        if (ImGui::BeginCombo("强制各向异性过滤", selectedAF.c_str()))
                        {
                            if (ImGui::Selectable("自动", !Config::Instance()->AnisotropyOverride.has_value()))
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

                        ImGui::Text("将在分辨率/预设更改后应用!!!");
                    }
                }

                ImGui::Spacing();
                if (ImGui::CollapsingHeader("快捷键"))
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    ImGui::Text("当前不支持组合键！");
                    ImGui::Text("按 Esc 取消，按 Backspace 解除绑定");
                    ImGui::Spacing();

                    static auto menu = Keybind("菜单", 10);
                    static auto fpsOverlay = Keybind("帧率叠加显示", 11);
                    static auto fpsOverlayCycle = Keybind("帧率叠加循环", 12);

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
                    ImGui::Text("帧时间");
                    State::Instance().frameTimeMutex.lock();
                    auto ft = std::format("{:6.2f} ms / {:5.1f} fps", State::Instance().frameTimes.back(), frameRate);
                    std::vector<float> frameTimeArray(State::Instance().frameTimes.begin(),
                                                      State::Instance().frameTimes.end());
                    ImGui::PlotLines(ft.c_str(), frameTimeArray.data(), (int) frameTimeArray.size());

                    if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        ImGui::TableNextColumn();
                        ImGui::Text("超采样器");
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

                if (ImGui::BeginCombo("菜单界面缩放", selectedScaleName))
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

                if (ImGui::Button("保存设置"))
                    Config::Instance()->SaveIni();

                ImGui::SameLine(0.0f, 6.0f);

                if (ImGui::Button("关闭"))
                {
                    _isVisible = false;
                    hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
                    io.BackendFlags &= 30;
                    io.ConfigFlags =
                        ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

                    if (pfn_ClipCursor_hooked)
                        pfn_ClipCursor(&_cursorLimit);

                    _showMipmapCalcWindow = false;
                    _showHudlessWindow = false;
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
                        "检测到 nvngx.ini，请迁移到 OptiScaler.ini 并删除旧配置");
                    ImGui::Spacing();
                }

                auto winSize = ImGui::GetWindowSize();
                auto winPos = ImGui::GetWindowPos();

                if (winPos.x == 60.0 && winSize.x > 100)
                {
                    float posX;
                    float posY;

                    posX = ((float) io.DisplaySize.x - winSize.x) / 2.0f;
                    posY = ((float) io.DisplaySize.y - winSize.y) / 2.0f;

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
                auto posX = (io.DisplaySize.x - 450.0f) / 2.0f;
                auto posY = (io.DisplaySize.y - 200.0f) / 2.0f;

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

                if (ImGui::Begin("多级纹理偏差", nullptr, flags))
                {
                    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                        ImGui::SetWindowFocus();

                    if (ImGui::InputScalar("显示宽度", ImGuiDataType_U32, &_displayWidth, NULL, NULL, "%u"))
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

                    const char* q[] = { "极致性能", "性能",   "平衡",
                                        "质量",           "超高质量", "DLAA" };
                    float fr[] = { 3.0f, 2.0f, 1.7f, 1.5f, 1.3f, 1.0f };
                    auto configQ = _mipmapUpscalerQuality;

                    const char* selectedQ = q[configQ];

                    ImGui::BeginDisabled(Config::Instance()->UpscaleRatioOverrideEnabled.value_or_default());

                    if (ImGui::BeginCombo("超采样质量", selectedQ))
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
                    if (ImGui::SliderFloat("超采样比率", &_mipmapUpscalerRatio, minLimit, maxLimit, "%.2f"))
                    {
                        _renderWidth = _displayWidth / _mipmapUpscalerRatio;
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                    }

                    if (ImGui::InputScalar("渲染宽度", ImGuiDataType_U32, &_renderWidth, NULL, NULL, "%u"))
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);

                    ImGui::SliderFloat("多级纹理偏差", &_mipBiasCalculated, -15.0f, 0.0f, "%.6f");

                    // BOTTOM LINE
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::SameLine(ImGui::GetWindowWidth() - 130.0f);

                    if (ImGui::Button("应用此值"))
                    {
                        _mipBias = _mipBiasCalculated;
                        _showMipmapCalcWindow = false;
                    }

                    ImGui::SameLine(0.0f, 6.0f);

                    if (ImGui::Button("关闭"))
                        _showMipmapCalcWindow = false;

                    ImGui::Spacing();
                    ImGui::Separator();

                    ImGui::End();
                }
            }

            auto fg = State::Instance().currentFG;
            if (_showHudlessWindow && Config::Instance()->FGHUDFix.value_or_default() && fg != nullptr &&
                fg->IsActive())
            {
                auto posX = (io.DisplaySize.x - 320.0f) / 2.0f;
                auto posY = (io.DisplaySize.y - 400.0f) / 2.0f;

                ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2 { 320.0f, 400.0f });

                if (ImGui::Begin("无HUD资源", nullptr, flags))
                {
                    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                        ImGui::SetWindowFocus();

                    int btnCount = 100;

                    if (ImGui::BeginTable("HudlessTable", 2, ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("##1", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("##2", ImGuiTableColumnFlags_WidthFixed);

                        ankerl::unordered_dense::map<void*, CapturedHudlessInfo>::iterator it;

                        for (it = State::Instance().CapturedHudlesses.begin();
                             it != State::Instance().CapturedHudlesses.end(); it++)
                        {
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);

                            ImGui::Text(std::format("{:X}, 数量: {}, {}", (size_t) it->first, it->second.usageCount,
                                                    it->second.enabled ? "激活" : "未激活")
                                            .c_str());

                            ImGui::TableSetColumnIndex(1);

                            btnCount++;
                            std::string text;

                            if (it->second.enabled)
                                text = std::format("禁用##{}", btnCount);
                            else
                                text = std::format("启用##{}", btnCount);

                            if (ImGui::Button(text.c_str()))
                                it->second.enabled = !it->second.enabled;
                        }

                        ImGui::EndTable();
                    }

                    if (ImGui::Button("清空##4"))
                        State::Instance().ClearCapturedHudlesses = true;

                    ImGui::SameLine(0.0f, 8.0f);

                    if (ImGui::Button("关闭##4"))
                        _showHudlessWindow = false;

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
    _showHudlessWindow = false;

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
