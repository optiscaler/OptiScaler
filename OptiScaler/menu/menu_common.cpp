#include "pch.h"
#include "menu_common.h"

#include "font/Hack_Compressed.h"

#include <proxies/XeSS_Proxy.h>
#include <proxies/XeFG_Proxy.h>
#include <proxies/FfxApi_Proxy.h>
#include <proxies/Streamline_Proxy.h>

#include <framegen/nvngx/Nvngx_FG.h>

#include <fsr4/FSR4Upgrade.h>

#include <nvapi/fakenvapi.h>
#include <hooks/Reflex_Hooks.h>

#include <version_check.h>

#include <imgui/imgui_internal.h>
#include <imgui/ImGuiNotify.hpp>

#include <mutex>
#include <cstdarg>

#include <array>
#include <chrono>
#include <misc/IdentifyGpu.h>

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
static bool inputFG = false;
static bool inputFps = false;
static bool inputFpsCycle = false;
static bool hasGamepad = false;
static bool fsr31InitTried = false;
static bool xefgInitTried = false;
static std::string windowTitle;
static std::string selectedUpscalerName = "";
static Upscaler currentBackend = Upscaler::Reset;
static std::string currentBackendName = "";
static int refreshRate = 0;

static ImVec2 splashPosition(-1000.0f, -1000.0f);
static ImVec2 splashSize(0.0f, 0.0f);
static double splashStart = 0.0;
static double splashLimit = 0.0;
static std::vector<std::string> splashText = { "Cope smarter, not harder",
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
                                               "Because native rendering is overrated.",
                                               "The more you upscaler, the more you save",
                                               "It's never too late to buy a better GPU",
                                               "We don't need real pixels where we're going",
                                               "Did you know that Intel released XeFG for everyone?",
                                               "MFG totally works with Nukem's 100%% no scam",
                                               "Some of those pixels might even be real!",
                                               "Just don't look too closely at the image",
                                               "Even supports \"software\" XeSS!",
                                               "It’s too blurry to go alone, take RCAS with you",
                                               "Thanks nitec, back to you nitec",
                                               "Tested and approved by By-U",
                                               "0.8 was an inside job",
                                               "FSR4 DP4a wenETA, AMD plz",
                                               "OptiCopers, assemble!",
                                               "The Way It's Meant To Be Upscaled",
                                               "Your game may not even crash today",
                                               "Expanded and Enhanced",
                                               "It's only my 5th crash today",
                                               "Latency with FG? But I have good internet",
                                               "Console peasants can't do that",
                                               "Hope you don't have a good eyesight",
                                               "Such an aggressive upscaling? A bold move",
                                               "I almost don't feel the input lag",
                                               "And that's how you get to 60 FPS",
                                               "Together We Upscale",
                                               "For upscalers, by upscalers",
                                               "Opti Sports, it's in the sampling",
                                               "Render in your world. Upscale in ours",
                                               "All your pixels are belong to us",
                                               "Upscaling for the masses, not the classes",
                                               "Generating discord since 2023",
                                               "Enabling DLSS since 2023",
                                               "[Reducted] never looked better",
                                               "Free and always free",
                                               "Getting unshackled from green chains in progress...",
                                               "Who's Nukem anyway?",
                                               "Compiling shaders... ETA: 05h:49m",
                                               "Did you really just pay 70 EUR for this game?!",
                                               "Guess who forgot about a nullptr check again",
                                               "AI can't outslop this",
                                               "Guess we're pre-alpha build demos now",
                                               "New app on the block - TH",
                                               "One more stutter and I might lose it",
                                               "Mostly stable, unlike the driver",
                                               "Vul... what? ~AMD",
                                               "My 8 points are floating",
                                               "No floating here - I'm strictly between -128 and 127",
                                               "Fake it til you bake it",
                                               "Worst case just turn it off and on",
                                               "*On a generative damage control mode at geometry level*",
                                               "Deep Learning Slop Sampling 5",
                                               "2D AI filters, now powered by just 2x 5090s",
                                               "Neural Slop Sampling with DLSS5",
                                               "DLSS 5 - the way it's meant to be slopped",
                                               "Like going in the first gear on the highway",
                                               "Nitec's Bizarre Upscaling",
                                               "<Your funny text goes here>" };

static std::string updateNoticeTag;
static std::string updateNoticeUrl;
static float lastMenuScale = 0.0f;

template <typename T, size_t N> struct RingBuffer
{
    std::array<T, N> data {};
    size_t head { 0 };
    size_t count { N };
    double sum { 0.0 };

    RingBuffer() { data.fill(static_cast<T>(0)); }

    void Push(T v)
    {
        if (count == N)
        {
            sum -= data[head];
        }
        else
        {
            ++count;
        }
        data[head] = v;
        sum += v;
        head = (head + 1) % N;
    }

    size_t Size() const { return N; }

    T At(size_t i) const
    {
        size_t start = head;
        return data[(start + i) % N];
    }

    float Average() const { return static_cast<float>(sum / static_cast<double>(N)); }
};

const int plotWidth = 360;
static RingBuffer<float, plotWidth> gFrameTimes;
static RingBuffer<float, plotWidth> gUpscalerTimes;

struct FsExistsCache
{
    std::wstring lastPath;
    bool cached { false };
    std::chrono::steady_clock::time_point nextRefresh { std::chrono::steady_clock::time_point::min() };
    std::chrono::milliseconds interval { 2000 };

    bool Get(const std::wstring& path)
    {
        auto now = std::chrono::steady_clock::now();
        if (path != lastPath || now >= nextRefresh)
        {
            lastPath = path;
            cached = std::filesystem::exists(path);
            nextRefresh = now + interval;
        }
        return cached;
    }
};

static FsExistsCache gExists;

inline std::string StrFmt(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    std::string out(len, '\0');
    va_start(args, fmt);
    std::vsnprintf(out.data(), len + 1, fmt, args);
    va_end(args);
    return out;
}

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
        State::Instance().newBackend = Upscaler::DLSSD;
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

  public:
    Keybind(std::string name, int id) : name(name), id(id) {}

    static std::string KeyNameFromVirtualKeyCode(USHORT virtualKey)
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

        LONG lParam = (scanCode & 0xFF) << 16;
        if (scanCode & 0xE000)
            lParam |= 1 << 24;

        wchar_t buf[64] = {};
        if (GetKeyNameTextW(lParam, buf, static_cast<int>(std::size(buf))) != 0)
            return wstring_to_string(buf);

        return "Unknown";
    }

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

            if (!inputFG)
                inputFG = rawData.data.keyboard.VKey == Config::Instance()->FGShortcutKey.value_or_default();

            if (!inputFpsCycle)
                inputFpsCycle =
                    rawData.data.keyboard.VKey == Config::Instance()->FpsCycleShortcutKey.value_or_default();
        }
    }

    if (!lastKey && msg == WM_KEYUP)
        lastKey = static_cast<int>(wParam);

    if (!inputMenu)
        inputMenu = msg == WM_KEYUP && wParam == Config::Instance()->ShortcutKey.value_or_default();

    if (!inputFps)
        inputFps = msg == WM_KEYUP && wParam == Config::Instance()->FpsShortcutKey.value_or_default();

    if (!inputFG)
        inputFG = msg == WM_KEYUP && wParam == Config::Instance()->FGShortcutKey.value_or_default();

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
    inputFG = vKey == Config::Instance()->FGShortcutKey.value_or_default();
    inputFpsCycle = vKey == Config::Instance()->FpsCycleShortcutKey.value_or_default();
}

bool IsFsr(Upscaler upscaler)
{
    switch (upscaler)
    {
    case Upscaler::FSR21:
    case Upscaler::FSR22:
    case Upscaler::FSR31:
    case Upscaler::FSR21_11on12:
    case Upscaler::FSR22_11on12:
    case Upscaler::FSR31_11on12:
        return true;
    default:
        return false;
    }
}

Upscaler MenuCommon::GetBackendCode(const API api)
{
    Upscaler upscaler;

    if (api == DX11)
        upscaler = Config::Instance()->Dx11Upscaler.value_or_default();
    else if (api == DX12)
        upscaler = Config::Instance()->Dx12Upscaler.value_or_default();
    else
        upscaler = Config::Instance()->VulkanUpscaler.value_or_default();

    return upscaler;
}

void MenuCommon::GetCurrentBackendInfo(const API api, Upscaler& upscaler, std::string* name)
{
    upscaler = GetBackendCode(api);
    *name = UpscalerDisplayName(upscaler, api);
}

void MenuCommon::RenderUpscalerCombo(const API api, Upscaler currentUpscaler, const std::vector<Upscaler>& options)
{
    static auto primaryGpu = IdentifyGpu::getPrimaryGpu();

    // Determine display name
    Upscaler targetBackend = State::Instance().newBackend;
    if (targetBackend == Upscaler::Reset)
        targetBackend = currentUpscaler;

    std::string selectedName = UpscalerDisplayName(targetBackend, api);

    if (ImGui::BeginCombo("##UpscalerCombo", selectedName.c_str()))
    {
        for (auto opt : options)
        {
            // Check if GPU is capable of a given backend
            bool isCapable = (opt == Upscaler::DLSS) ? primaryGpu.dlssCapable : true;
            if (!isCapable)
                continue;

            bool isSelected = (currentUpscaler == opt);
            if (ImGui::Selectable(UpscalerDisplayName(opt, api).c_str(), isSelected))
            {
                State::Instance().newBackend = opt;
            }
        }
        ImGui::EndCombo();
    }
}

void MenuCommon::AddDx11Backends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::DX11, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR22, Upscaler::FSR31, Upscaler::XeSS_11on12,
                          Upscaler::FSR21_11on12, Upscaler::FSR22_11on12, Upscaler::FSR31_11on12, Upscaler::DLSS });
}

void MenuCommon::AddDx12Backends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::DX12, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR21, Upscaler::FSR22, Upscaler::FSR31, Upscaler::DLSS });
}

void MenuCommon::AddVulkanBackends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::Vulkan, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR21, Upscaler::FSR22, Upscaler::FSR31, Upscaler::FSR21_11on12,
                          Upscaler::FSR31_11on12, Upscaler::DLSS });
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

// TODO: disable presets based on the detected DLSS version
template <HasDefaultValue B> void MenuCommon::AddDLSSRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    // clang-format off
    static const std::vector<MenuOption<uint32_t>> presets = {
        { NVSDK_NGX_DLSS_Hint_Render_Preset_Default, "DEFAULT", 
            "Whatever the game uses" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_A, "PRESET A",
            "Intended for Performance/Balanced/Quality modes.\nAn older variant best suited to combat ghosting...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_B, "PRESET B",
            "Intended for Ultra Performance mode.\nSimilar to Preset A...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_C, "PRESET C",
            "Intended for Performance/Balanced/Quality modes.\nGenerally favors current frame information...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_D, "PRESET D",
            "Default preset for Performance/Balanced/Quality modes;\ngenerally favors image stability.\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_E, "PRESET E",
            "DLSS 3.7+, a better D preset\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_F, "PRESET F",
            "Default preset for Ultra Performance and DLAA modes\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_G, "PRESET G",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_H_Reserved, "PRESET H",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_I_Reserved, "PRESET I",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_J, "PRESET J",
            "Similar to preset K. Preset J might exhibit slightly\nless ghosting...\n1st Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_K, "PRESET K",
            "Default preset for DLAA/Balanced/Quality modes...\n1st Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_L, "PRESET L",
            "Default for Ultra Perf mode\n2nd Gen Transformers" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_M, "PRESET M",
            "Default for Perf mode\n2nd Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_N, "PRESET N",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_O, "PRESET O",
            "Unused" },
        { NV_PRESET_LATEST, "Latest",
            "Latest supported by the dll" }
    };
    // clang-format on

    PopulateCombo(name, *value, presets);
}

template <HasDefaultValue B> void MenuCommon::AddDLSSDRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    // We don't have DLSSD definitions so using raw values
    static const std::vector<MenuOption<uint32_t>> presets = {
        { 0, "DEFAULT", "Whatever the game uses" },
        { 1, "PRESET A", "Preset A\nRemoved on recent versions!" },
        { 2, "PRESET B", "Preset B\nRemoved on recent versions!" },
        { 3, "PRESET C", "Preset C\nRemoved on recent versions!" },
        { 4, "PRESET D", "Default model, Transformer" },
        { 5, "PRESET E", "Latest Transformer model\nMust use if DoF guide is needed" },
        { NV_PRESET_LATEST, "Latest", "Latest supported by the dll" }
    };

    PopulateCombo(name, *value, presets);
}

template <typename TStorage, typename T>
void MenuCommon::PopulateCombo(const std::string& name, TStorage& currentValue,
                               const std::vector<MenuOption<T>>& options)
{
    if (options.empty())
        return;

    // Assumes that different types mean that TStorage is std::optional
    T currentVal;
    if constexpr (std::is_same_v<TStorage, T>)
        currentVal = currentValue;
    else
        currentVal = currentValue.value_or(options[0].value);

    // Find the label for the currently selected item
    std::string preview = "Unknown";
    for (const auto& opt : options)
    {
        if (opt.value == currentVal)
        {
            preview = opt.label;
            break;
        }
    }

    if (ImGui::BeginCombo(name.c_str(), preview.c_str()))
    {
        for (const auto& opt : options)
        {
            if (opt.disabled)
                ImGui::BeginDisabled();

            bool isSelected = (currentVal == opt.value);
            if (ImGui::Selectable(opt.label.c_str(), isSelected))
                currentValue = opt.value;

            // Show tooltip for the individual item if it exists
            if (!opt.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", opt.tooltip.c_str());

            if (opt.disabled)
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
        (!Config::Instance()->OverlayMenu.value_or_default() && State::Instance().currentFeature != nullptr &&
         State::Instance().currentFeature->IsHdr()))
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

static float MenuResolutionScale(ImGuiIO io)
{
    if (Config::Instance()->MenuScale.has_value())
        return Config::Instance()->MenuScale.value();

    // Calculate menu scale according to display resolution
    float y = State::Instance().screenHeight;

    if (io.DisplaySize.y != 0)
        y = (float) io.DisplaySize.y;

    // 1000p is minimum for 1.0 menu ratio
    float result = (float) ((int) (y / 108.0f)) / 10.0f;

    result = std::round(result * 10.0f) / 10.0f;

    if (result < 0.5f)
        result = 0.5f;

    if (result > 2.0f)
        result = 2.0f;

    return result;
}

inline static std::string GetSourceString(UINT source)
{
    switch (source)
    {
    case 1:
        return "RTV";
    case 2:
        return "SRV";
    case 4:
        return "UAV";
    case 8:
        return "OM";
    case 16:
        return "Ups";
    case 32:
        return "SCR";
    case 64:
        return "SGR";
    default:
        return std::format("{}", source);
    }
}

inline static std::string GetDispatchString(UINT source)
{
    switch (source)
    {
    case 512:
        return "DI";
    case 1024:
        return "DII";
    case 256:
        return "Disp";
    default:
        return std::format("{}", source);
    }
}

static double lastTime = 0.0;
static UINT64 uwpTargetFrame = 0;

bool MenuCommon::RenderMenu()
{
    if (!_isInited)
        return false;

    auto& state = State::Instance();
    auto config = Config::Instance();

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

    state.frameTimes.pop_front();
    state.frameTimes.push_back(frameTime);

    ImGuiIO& io = ImGui::GetIO();
    (void) io;
    auto currentFeature = state.currentFeature;

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
        if (inputFG)
        {
            inputFG = false;

            if (state.activeFgInput != FGInput::NoFG && state.activeFgOutput != FGOutput::NoFG &&
                (state.currentFGSwapchain != nullptr || state.activeFgInput == FGInput::NvngxFG))
            {
                config->FGEnabled = !config->FGEnabled.value_or_default();
                LOG_DEBUG("FG toggle key pressed, setting FGEnabled to {}", config->FGEnabled.value_or_default());

                if (config->FGEnabled.value_or_default())
                    state.FGchanged = true;
            }
        }

        if (inputFps)
        {
            inputFps = false;
            config->ShowFps = !config->ShowFps.value_or_default();
        }

        if (inputFpsCycle && config->ShowFps.value_or_default())
            config->FpsOverlayType = (FpsOverlay) ((config->FpsOverlayType.value_or_default() + 1) % FpsOverlay_COUNT);

        if (inputMenu)
        {
            inputMenu = false;
            _isVisible = !_isVisible;

            LOG_DEBUG("Menu key pressed, {0}", _isVisible ? "opening ImGui" : "closing ImGui");

            if (_isVisible)
            {
                refreshRate = Util::GetActiveRefreshRate(_handle);

                auto dllPath = Util::DllPath().parent_path() / "dlss-enabler-headless.asi";
                state.NvngxFgFilesAvailable = gExists.Get(dllPath);

                if (!state.NvngxFgFilesAvailable)
                {
                    dllPath = Util::DllPath().parent_path() / "dlssg_to_fsr3_amd_is_better.dll";
                    state.NvngxFgFilesAvailable = gExists.Get(dllPath);
                }

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

    bool frameTimesCalculated = false;
    constexpr double splashTime = 7000.0;
    constexpr double fadeTime = 1000.0;
    constexpr int updateNoticeTime = 60000;
    static std::string splashMessage;

    // Version check
    struct VersionCheckStatus
    {
        bool completed = false;
        bool updateAvailable = false;
        std::string latestTag;
        std::string latestUrl;
        std::string error;
    } versionStatus;

    {
        std::scoped_lock lock(state.versionCheckMutex);
        versionStatus.completed = state.versionCheckCompleted;
        versionStatus.updateAvailable = state.updateAvailable;
        versionStatus.latestTag = state.latestVersionTag;
        versionStatus.latestUrl = state.latestVersionUrl;
        versionStatus.error = state.versionCheckError;
    }

    const auto& currentVersionText = VersionCheck::CurrentVersionString();

    if (versionStatus.completed && versionStatus.updateAvailable && !versionStatus.latestTag.empty())
    {
        if (updateNoticeTag != versionStatus.latestTag)
        {
            updateNoticeTag = versionStatus.latestTag;
            updateNoticeUrl = versionStatus.latestUrl;
            const auto notice = [&]()
            {
                ImGuiToast updateNotification { ImGuiToastType::Error, updateNoticeTime };
                updateNotification.setTitle("OptiScaler Update available");
                updateNotification.setContent(
                    "Press %s for more info",
                    Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str());
                ImGui::InsertNotification(updateNotification);
                return true;
            };
            static auto res = notice();
        }
    }

    if (!state.postDone)
    {
        if (state.postCodes & PostCode::SlPluginsAlreadyInMemory)
        {
            auto filename = Util::DllPath().filename().string();
            to_lower_in_place(filename);

            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            notification.setTitle("Late Streamline hook detected");
            notification.setContent(
                "Consider renaming OptiScaler from %s to other supported name.\nYou may experience issues otherwise.",
                filename.c_str());
            ImGui::InsertNotification(notification);
        }

        state.postDone = true;
    }

    if (splashLimit < 1.0f)
    {
        splashStart = now + 100.0;
        splashLimit = splashStart + splashTime;

        std::srand(static_cast<unsigned>(std::time(nullptr)));
        splashMessage = splashText[std::rand() % splashText.size()];
    }

    // New frame check
    if ((!config->DisableSplash.value_or_default() && now > splashStart && now < splashLimit) ||
        config->ShowFps.value_or_default() || _isVisible || ImGui::notifications.size() > 0)
    {
        if (!_isUWP)
        {
            ImGui_ImplWin32_NewFrame();
        }
        else
        {
            ImVec2 displaySize { state.screenWidth, state.screenHeight };
            ImGui_ImplUwp_NewFrame(displaySize);
        }

        MenuHdrCheck(io);
        ImGui::NewFrame();

        newFrame = true;
    }

    float menuResScale = MenuResolutionScale(io);

    // Splash screen
    if (!config->DisableSplash.value_or_default())
    {
        if (now > splashStart && now < splashLimit)
        {

            ImGui::SetNextWindowSize({ 0.0f, 0.0f });
            ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default());
            ImGui::SetNextWindowPos(splashPosition, ImGuiCond_Always);

            float windowAlpha = 1.0f;
            if (auto diff = now - splashStart; diff < fadeTime)
                windowAlpha = static_cast<float>(diff / fadeTime);
            else if (auto diff = splashLimit - now; diff < fadeTime)
                windowAlpha = static_cast<float>(diff / fadeTime);

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

                if (config->UseHQFont.value_or_default())
                    ImGui::PushFontSize(std::round(splashScale * fontSize));
                else
                    ImGui::SetWindowFontScale(splashScale);

                ImGui::Text("OptiScaler - %s for menu",
                            Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str());
                ImGui::TextColored(toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 0.7f)), splashMessage.c_str());

                splashSize = ImGui::GetWindowSize();

                if (config->UseHQFont.value_or_default())
                    ImGui::PopFontSize();

                ImGui::End();

                splashPosition.x = 0.0f; // io.DisplaySize.x - splashWinSize.x;
                splashPosition.y = io.DisplaySize.y - splashSize.y;
            }

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }
    }

    // Notifications
    bool tonemapRequired = State::Instance().isHdrActive ||
                           (!Config::Instance()->OverlayMenu.value_or_default() &&
                            State::Instance().currentFeature != nullptr && State::Instance().currentFeature->IsHdr());

    float screenHeight = State::Instance().screenHeight;
    if (io.DisplaySize.y != 0)
        screenHeight = io.DisplaySize.y;

    // Map resolution height to scale, 0.5 for 480p, 2.0 for 1440p
    constexpr float slope = (2.0f - 0.5f) / (1440.f - 480.f);
    float notificationScale = 0.5f + slope * (screenHeight - 480.f);
    notificationScale = std::clamp(notificationScale, 0.5f, 2.0f);

    if (config->UseHQFont.value_or_default())
        ImGui::PushFontSize(std::round(notificationScale * fontSize));

    // No fallback font, SetWindowFontScale needs to be called after Begin()

    ImGui::RenderNotifications(ImGuiToastPos::TopCenter, notificationScale, tonemapRequired);

    if (config->UseHQFont.value_or_default())
        ImGui::PopFontSize();

    // FPS Overlay font
    auto fpsScale = config->FpsScale.value_or(menuResScale);

    // Update frame time & upscaler time averages
    float averageFrameTime = 0.0f;
    float averageUpscalerFT = 0.0f;

    if (config->ShowFps.value_or_default() || _isVisible)
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (state.frameTimes[i] > 0.0)
            {
                frameTime += state.frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
        frameTimesCalculated = true;

        float lastFT = static_cast<float>(state.frameTimes.empty() ? 0.0f : state.frameTimes.back());
        float lastUT = static_cast<float>(state.upscaleTimes.empty() ? 0.0f : state.upscaleTimes.back());
        gFrameTimes.Push(lastFT);
        gUpscalerTimes.Push(lastUT);

        averageFrameTime = gFrameTimes.Average();
        averageUpscalerFT = gUpscalerTimes.Average();
    }

    // If Fps overlay is visible
    if (config->ShowFps.value_or_default())
    {
        bool stylePushed = false;

        const static auto defaultStyle = ImGuiStyle();

        // Rescale the fps overlay every frame because it shares style with the main menu
        if (config->FpsScale.has_value() && config->FpsScale.value() != menuResScale)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, defaultStyle.WindowPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, defaultStyle.FramePadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, defaultStyle.CellPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, defaultStyle.SeparatorTextPadding * fpsScale);

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, defaultStyle.ItemSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, defaultStyle.ItemInnerSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, defaultStyle.IndentSpacing * fpsScale);

            stylePushed = true;
        }

        // Set overlay position
        ImGui::SetNextWindowPos(overlayPosition, ImGuiCond_Always);

        // Set overlay window properties
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));            // Transparent border
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));           // Transparent frame background
        ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default()); // Transparent background

        ImVec4 green(0.0f, 1.0f, 0.0f, 1.0f);
        if (state.isHdrActive)
            ImGui::PushStyleColor(ImGuiCol_PlotLines, toneMapColor(green)); // Tone Map plot line color
        else
            ImGui::PushStyleColor(ImGuiCol_PlotLines, green);

        if (ImGui::Begin("Performance Overlay", nullptr,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav))
        {
            std::string api;
            if (IdentifyGpu::getPrimaryGpu().usesDxvk && state.api == DX11)
            {
                api = "DXVK";
            }
            else if (IdentifyGpu::getPrimaryGpu().usesVkd3dProton && state.api == DX12)
            {
                api = "VKD3D";
            }
            else
            {
                switch (state.swapchainApi)
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
                    switch (state.api)
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

            if (config->UseHQFont.value_or_default())
                ImGui::PushFontSize(std::round(fpsScale * fontSize));
            else
                ImGui::SetWindowFontScale(fpsScale);

            std::string firstLine = "";
            std::string secondLine = "";
            std::string thirdLine = "";

            auto fg = state.currentFG;
            auto fgText = (fg != nullptr && fg->IsActive() && !fg->IsPaused()) ? ("(" + std::string(fg->Name()) + ")")
                                                                               : std::string();

            // Prepare Line 1
            if (config->FpsOverlayType.value_or_default() == FpsOverlay_JustFPS)
            {
                firstLine = StrFmt("%s | FPS: %6.1f %s", api.c_str(), frameRate, fgText.c_str());
            }
            else if (config->FpsOverlayType.value_or_default() == FpsOverlay_Simple)
            {
                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    firstLine = StrFmt("%s | FPS: %6.1f, %7.2f ms %s | %s -> %s %u.%u.%u", api.c_str(), frameRate,
                                       frameTime, fgText.c_str(), state.currentInputApiName.c_str(),
                                       currentFeature->Name().c_str(), currentFeature->Version().major,
                                       currentFeature->Version().minor, currentFeature->Version().patch);
                }
                else
                {
                    firstLine =
                        StrFmt("%s | FPS: %6.1f, %7.2f ms %s", api.c_str(), frameRate, frameTime, fgText.c_str());
                }
            }
            else
            {
                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    firstLine = StrFmt("%s | FPS: %6.1f, Avg: %6.1f %s | %s -> %s %u.%u.%u", api.c_str(), frameRate,
                                       1000.0f / averageFrameTime, fgText.c_str(), state.currentInputApiName.c_str(),
                                       currentFeature->Name().c_str(), currentFeature->Version().major,
                                       currentFeature->Version().minor, currentFeature->Version().patch);
                }
                else
                {
                    firstLine = StrFmt("%s | FPS: %6.1f, Avg: %6.1f %s", api.c_str(), frameRate,
                                       1000.0f / averageFrameTime, fgText.c_str());
                }
            }

            // Prepare Line 2
            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Detailed)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                secondLine = StrFmt("Frame Time: %7.2f ms, Avg: %7.2f ms", state.frameTimes.back(), averageFrameTime);
            }

            // Prepare Line 3
            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Full)
            {
                thirdLine =
                    StrFmt("Upscaler Time: %7.2f ms, Avg: %7.2f ms", state.upscaleTimes.back(), averageUpscalerFT);
            }

            ImVec2 plotSize;
            if (config->FpsOverlayHorizontal.value_or_default())
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

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Detailed)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
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

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_DetailedGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of frame times
                ImGui::PlotLines(
                    "##FrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gFrameTimes, plotWidth, 0, nullptr, 0.0f, 66.6f, plotSize);
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Full)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
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

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_FullGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of upscaler times
                ImGui::PlotLines(
                    "##UpscalerFrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gUpscalerTimes, plotWidth, 0, nullptr, 0.0f, 20.0f, plotSize);
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_ReflexTimings)
            {
                constexpr auto delayBetweenPollsMs = 500;
                static auto previousPoll = 0.0;
                static bool gotData = false;
                if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
                {
                    gotData = ReflexHooks::updateTimingData();
                    previousPoll = now;
                }

                auto& timingData = ReflexHooks::timingData;

                if (gotData && timingData[TimingType::TimeRange].has_value())
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    constexpr float offsetForText = 155;

                    const auto& rangeInNs = timingData[TimingType::TimeRange].value().length;

                    UINT64 localFrameCount = 0;

                    if (fg != nullptr)
                        localFrameCount = fg->FrameCount();

                    ImGui::Text("FGId: %llu, RfxId: %llu", localFrameCount, state.reflexFrameId);
                    ImGui::Text("Reflex timings, whole frame: %.1fms", rangeInNs / 1000.0);

                    const auto maxWidth =
                        config->FpsOverlayHorizontal.value_or_default() ? ImGui::GetWindowWidth() : plotSize.x;

                    const auto drawTiming = [&](TimingType type, const char* desc, ImVec4 color)
                    {
                        if (!timingData[type].has_value())
                            return;

                        auto toneMappedColor = State::Instance().isHdrActive ? toneMapColor(color) : color;

                        auto& timing = timingData[type].value();
                        float duration = static_cast<float>(timing.length * rangeInNs / 1000.0);
                        ImGui::TextColored(toneMappedColor, "%-12s %4.1fms", desc, duration);
                        auto leftLimit = ImGui::GetItemRectMin().x + offsetForText * fpsScale;
                        auto start = static_cast<float>(leftLimit + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                        timing.position);
                        auto end = static_cast<float>(start + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                  timing.length);
                        auto pos = ImVec2(start, ImGui::GetItemRectMin().y);
                        auto size = ImVec2(end, ImGui::GetItemRectMax().y);
                        drawList->AddRectFilled(pos, size, ImGui::ColorConvertFloat4ToU32(toneMappedColor));
                    };

                    drawTiming(TimingType::Simulation, "Simulation", ImVec4(0.768f, 0.169f, 0.169f, 1.0f));
                    drawTiming(TimingType::RenderSubmit, "RenderSubmit", ImVec4(0.235f, 0.705f, 0.294f, 1.0f));
                    drawTiming(TimingType::Present, "Present", ImVec4(1.0f, 0.88f, 0.098f, 1.0f));
                    drawTiming(TimingType::Driver, "Driver", ImVec4(0.263f, 0.388f, 0.847f, 1.0f));
                    drawTiming(TimingType::OsRenderQueue, "RenderQueue", ImVec4(0.76f, 0.51f, 0.188f, 1.0f));
                    drawTiming(TimingType::GpuRender, "GpuRender", ImVec4(0.569f, 0.117f, 0.705f, 1.0f));
                }
            }

            ImGui::PopStyleColor(3); // Restore the style
        }

        // Get size for postioning
        overlaySize = ImGui::GetWindowSize();

        if (config->UseHQFont.value_or_default())
            ImGui::PopFontSize();

        ImGui::End();

        if (stylePushed)
            ImGui::PopStyleVar(7);

        // Left / Right
        if (config->FpsOverlayPos.value_or_default() == 0 || config->FpsOverlayPos.value_or_default() == 2)
            overlayPosition.x = 0;
        else
            overlayPosition.x = io.DisplaySize.x - overlaySize.x;

        // Top / Bottom
        if (config->FpsOverlayPos.value_or_default() < 2)
        {
            overlayPosition.y = 0;
        }
        else
        {
            // Prevent overlapping with splash message
            if (!config->DisableSplash.value_or_default() && now > splashStart && now < splashLimit)
                overlayPosition.y = io.DisplaySize.y - overlaySize.y - splashSize.y;
            else
                overlayPosition.y = io.DisplaySize.y - overlaySize.y;
        }
    }

    if (_isVisible)
    {
        // Check for gpu support
        // DXVK might call vulkan device creation which would destroy our objects
        State::Instance().vulkanSkipHooks = true;
        static auto primaryGpu = IdentifyGpu::getPrimaryGpu();
        State::Instance().vulkanSkipHooks = false;

        // Overlay font
        if (config->UseHQFont.value_or_default())
            ImGui::PushFontSize(std::round(menuResScale * fontSize));

        // If overlay is not visible frame needs to be inited
        if (!frameTimesCalculated)
        {
            float frameCnt = 0;
            frameTime = 0;
            for (size_t i = 299; i > 199; i--)
            {
                if (state.frameTimes[i] > 0.0)
                {
                    frameTime += state.frameTimes[i];
                    frameCnt++;
                }
            }

            frameTime /= frameCnt;
            frameRate = 1000.0 / frameTime;
        }

        ImGuiWindowFlags flags = 0;
        flags |= ImGuiWindowFlags_NoSavedSettings;
        flags |= ImGuiWindowFlags_NoCollapse;
        flags |= ImGuiWindowFlags_AlwaysAutoResize;

        if (lastMenuScale != menuResScale)
        {
            lastMenuScale = menuResScale;

            // if UI scale is changed rescale the style
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
            style.ScaleAllSizes(menuResScale);
            style.MouseCursorScale = 1.0f;
            CopyMemory(style.Colors, styleold.Colors, sizeof(style.Colors)); // Restore colors

            ImGui::SetNextWindowSize({ 1.0f, 1.0f });
        }

        // Main menu window
        if (windowTitle.empty())
        {
            windowTitle =
                StrFmt("%s - %s %s %s %s", VER_PRODUCT_NAME, state.GameExe.c_str(),
                       state.GameName.empty() ? "" : StrFmt("- %s", state.GameName.c_str()).c_str(),
                       (state.detectedQuirks.size() > 0) ? "(Q)" : "", state.isOptiPatcherSucceed ? "(OP)" : "");
        }

        if (ImGui::Begin(windowTitle.c_str(), NULL, flags))
        {
            bool rcasEnabled = false;

            if (!_showMipmapCalcWindow && !_showHudlessWindow && !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetWindowFocus();

            if (config->MenuScale.has_value())
            {
                _selectedScale = ((int) (menuResScale * 10.0f)) - 4;
            }
            else
            {
                _selectedScale = 0;
            }

            if (versionStatus.completed)
            {
                if (versionStatus.updateAvailable && !versionStatus.latestTag.empty())
                {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f), "Update available: %s (current %s)",
                                       versionStatus.latestTag.c_str(), currentVersionText.c_str());

                    if (!versionStatus.latestUrl.empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextLinkOpenURL("Open release page", versionStatus.latestUrl.c_str());
                    }

                    ImGui::Spacing();
                }
                else if (!versionStatus.error.empty())
                {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.f, 1.f), "%s", versionStatus.error.c_str());
                    ImGui::Spacing();
                }
            }

            // No active upscaler message
            if (currentFeature == nullptr || !currentFeature->IsInited())
            {
                ImGui::Spacing();

                if (config->UseHQFont.value_or_default())
                    ImGui::PushFontSize(std::round(fontSize * menuResScale * 3.0f));
                else
                    ImGui::SetWindowFontScale(menuResScale * 3.0f);

                if (state.nvngxExists || state.nvngxReplacement.has_value() ||
                    (state.libxessExists || XeSSProxy::Module() != nullptr))
                {
                    ImGui::Spacing();

                    std::vector<std::string> upscalers;

                    if (state.fsrHooks)
                        upscalers.push_back("FSR");

                    if (state.nvngxExists || state.nvngxReplacement.has_value() || primaryGpu.dlssCapable)
                        upscalers.push_back("DLSS");

                    if (state.libxessExists || XeSSProxy::Module() != nullptr)
                        upscalers.push_back("XeSS");

                    auto joined = upscalers | std::views::join_with(std::string { " or " });

                    std::string joinedUpscalers(joined.begin(), joined.end());

                    ImGui::Text("Please select %s as upscaler\nfrom game options and load into the game\nto enable "
                                "upscaler settings.\n",
                                joinedUpscalers.c_str());

                    if (config->UseHQFont.value_or_default())
                        ImGui::PopFontSize();
                    else
                        ImGui::SetWindowFontScale(menuResScale);

                    ImGui::Spacing();

                    if (primaryGpu.dlssCapable)
                    {
                        ImGui::Text("nvngx_dlss : %s", state.NVNGX_DLSS_Path.has_value() ? "Exists" : "Doesn't Exist");
                        ImGui::SameLine(0.0f, 16.0f);
                        ImGui::Text("nvngx_dlssd : %s",
                                    state.NVNGX_DLSSD_Path.has_value() ? "Exists" : "Doesn't Exist");
                    }
                    else
                    {
                        ImGui::Text("nvngx.dll: %s", state.nvngxExists ? "Exists" : "Doesn't Exist");
                        ImGui::SameLine(0.0f, 16.0f);
                        ImGui::Text("nvngx replacement: %s",
                                    state.nvngxReplacement.has_value() ? "Exists" : "Doesn't Exist");
                    }

                    ImGui::Text("libxess: %s",
                                (state.libxessExists || XeSSProxy::Module() != nullptr) ? "Exists" : "Doesn't Exist");

                    ImGui::Text("FSR Hooks: %s", state.fsrHooks ? "Exist" : "Don't Exist");
                    ImGui::SameLine(0.0f, 16.0f);
                    ImGui::Text("FSR 3.1: %s", FfxApiProxy::Dx12Module() != nullptr ? "Exists" : "Doesn't Exist");
                    ImGui::SameLine(0.0f, 16.0f);
                    ImGui::Text("FSR 3.1 SR: %s", FfxApiProxy::Dx12Module_SR() != nullptr ? "Exists" : "Doesn't Exist");
                    ImGui::SameLine(0.0f, 16.0f);
                    ImGui::Text("FSR 3.1 FG: %s", FfxApiProxy::Dx12Module_FG() != nullptr ? "Exists" : "Doesn't Exist");

                    ImGui::Spacing();
                }
                else
                {
                    ImGui::Spacing();
                    ImGui::Text(
                        "Can't find nvngx.dll and libxess.dll and FSR inputs\nUpscaling support will NOT work.");
                    ImGui::Spacing();

                    if (config->UseHQFont.value_or_default())
                        ImGui::PopFont();
                    else
                        ImGui::SetWindowFontScale(menuResScale);
                }
            }
            else if (currentFeature->IsFrozen())
            {
                ImGui::Spacing();

                if (config->UseHQFont.value_or_default())
                    ImGui::PushFontSize(std::round(fontSize * menuResScale * 3.0f));
                else
                    ImGui::SetWindowFontScale(menuResScale * 3.0f);

                ImGui::Text("%s is active, but not currently used by the game\nPlease enter the game",
                            currentFeature->Name().c_str());

                if (config->UseHQFont.value_or_default())
                    ImGui::PopFont();
                else
                    ImGui::SetWindowFontScale(menuResScale);
            }

            if (ImGui::BeginTable("main", 2, ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();

                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    // UPSCALERS -----------------------------
                    ImGui::SeparatorText("Upscalers");
                    ShowTooltip("Which copium do you choose?");

                    GetCurrentBackendInfo(state.api, currentBackend, &currentBackendName);

                    std::string spoofingText;

                    ImGui::PushItemWidth(180.0f * menuResScale);

                    switch (state.api)
                    {
                    case DX11:
                        ImGui::Text(primaryGpu.name.c_str());

                        ImGui::Text("D3D11 %s| %s %d.%d.%d", primaryGpu.usesDxvk ? "(DXVK) " : "",
                                    currentFeature->Name().c_str(), currentFeature->Version().major,
                                    currentFeature->Version().minor, currentFeature->Version().patch);
                        ImGui::SameLine(0.0f, 6.0f);
                        ImGui::Text("| Input: %s", state.currentInputApiName.c_str());

                        ImGui::SameLine(0.0f, 6.0f);
                        spoofingText = config->DxgiSpoofing.value_or_default() ? "On" : "Off";
                        ImGui::Text("| Spoof: %s", spoofingText.c_str());

                        if (currentFeature->Name() != "DLSSD")
                            AddDx11Backends(currentBackend);

                        break;

                    case DX12:
                        ImGui::Text(primaryGpu.name.c_str());

                        ImGui::Text("D3D12 %s| %s %d.%d.%d", primaryGpu.usesDxvk ? "(DXVK) " : "",
                                    currentFeature->Name().c_str(), currentFeature->Version().major,
                                    currentFeature->Version().minor, currentFeature->Version().patch);
                        ImGui::SameLine(0.0f, 6.0f);
                        ImGui::Text("| Input: %s", state.currentInputApiName.c_str());

                        ImGui::SameLine(0.0f, 6.0f);
                        spoofingText = config->DxgiSpoofing.value_or_default() ? "On" : "Off";
                        ImGui::Text("| Spoof: %s", spoofingText.c_str());

                        if (currentFeature->Name() != "DLSSD")
                            AddDx12Backends(currentBackend);

                        break;

                    default:
                        ImGui::Text(primaryGpu.name.c_str());

                        ImGui::Text("Vulkan %s| %s %d.%d.%d", primaryGpu.usesDxvk ? "(DXVK) " : "",
                                    currentFeature->Name().c_str(), currentFeature->Version().major,
                                    currentFeature->Version().minor, currentFeature->Version().patch);
                        ImGui::SameLine(0.0f, 6.0f);
                        ImGui::Text("| Input: %s", state.currentInputApiName.c_str());

                        auto vlkSpoof = config->VulkanSpoofing.value_or_default();
                        auto vlkExtSpoof = config->VulkanExtensionSpoofing.value_or_default();

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

                        if (currentFeature->Name() != "DLSSD")
                            AddVulkanBackends(currentBackend);
                    }

                    ImGui::PopItemWidth();

                    if (currentFeature->Name() != "DLSSD")
                    {
                        ImGui::SameLine(0.0f, 6.0f);

                        if (ImGui::Button("Change Upscaler##2") && state.newBackend != Upscaler::Reset &&
                            state.newBackend != currentBackend)
                        {
                            if (state.newBackend == Upscaler::XeSS)
                            {
                                // Reseting them for xess
                                config->DisableReactiveMask.reset();
                                config->DlssReactiveMaskBias.reset();
                            }

                            MARK_ALL_BACKENDS_CHANGED();
                        }
                    }

                    if (currentFeature->AccessToReactiveMask())
                    {
                        ImGui::BeginDisabled(config->DisableReactiveMask.value_or(false));

                        auto useAsTransparency = config->FsrUseMaskForTransparency.value_or_default();
                        if (ImGui::Checkbox("Use Reactive Mask as Transparency Mask", &useAsTransparency))
                            config->FsrUseMaskForTransparency = useAsTransparency;

                        ImGui::EndDisabled();
                    }

                    if (primaryGpu.dlssCapable && !state.NVNGX_DLSS_Path.has_value())
                    {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f), "nvngx_dlss.dll not found, DLSS disabled!");
                    }
                }

                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    // Dx11 with Dx12
                    if (state.api == DX11 && config->Dx11Upscaler.value_or_default() != Upscaler::FSR22 &&
                        config->Dx11Upscaler.value_or_default() != Upscaler::DLSS &&
                        config->Dx11Upscaler.value_or_default() != Upscaler::FSR31)
                    {
                        ImGui::Spacing();
                        if (auto ch = ScopedCollapsingHeader("Dx11 with Dx12 Settings"); ch.IsHeaderOpen())
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            if (bool dontUseNTShared = config->DontUseNTShared.value_or_default();
                                ImGui::Checkbox("Don't Use NTShared", &dontUseNTShared))
                                config->DontUseNTShared = dontUseNTShared;

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }

                    if (state.api == Vulkan && currentFeature->IsWithDx12())
                    {
                        ImGui::Spacing();
                        if (auto ch = ScopedCollapsingHeader("Vulkan with Dx12 Settings"); ch.IsHeaderOpen())
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            if (bool inputsUseCopy = config->VulkanUseCopyForInputs.value_or_default();
                                ImGui::Checkbox("Use CopyResource for Inputs", &inputsUseCopy))
                                config->VulkanUseCopyForInputs = inputsUseCopy;

                            if (bool outputUseCopy = config->VulkanUseCopyForOutput.value_or_default();
                                ImGui::Checkbox("Use CopyResource for Output", &outputUseCopy))
                                config->VulkanUseCopyForOutput = outputUseCopy;

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }

                    // UPSCALER SPECIFIC -----------------------------

                    // XeSS -----------------------------
                    if (currentBackend == Upscaler::XeSS && currentFeature->Name() != "DLSSD")
                    {
                        ImGui::Spacing();
                        if (auto ch = ScopedCollapsingHeader("XeSS Settings"); ch.IsHeaderOpen())
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            const char* models[] = { "KPSS", "SPLAT", "MODEL_3", "MODEL_4", "MODEL_5", "MODEL_6" };
                            auto configModes = config->NetworkModel.value_or_default();

                            if (configModes < 0 || configModes > 5)
                                configModes = 0;

                            const char* selectedModel = models[configModes];

                            if (ImGui::BeginCombo("Network Models", selectedModel))
                            {
                                for (int n = 0; n < 6; n++)
                                {
                                    if (ImGui::Selectable(models[n], (config->NetworkModel.value_or_default() == n)))
                                    {
                                        config->NetworkModel = n;
                                        state.newBackend = currentBackend;
                                        MARK_ALL_BACKENDS_CHANGED();
                                    }
                                }

                                ImGui::EndCombo();
                            }
                            ShowHelpMarker("Likely doesn't do much");

                            if (bool dbg = state.xessDebug; ImGui::Checkbox("Dump (Shift+Del)", &dbg))
                                state.xessDebug = dbg;

                            ImGui::SameLine(0.0f, 6.0f);
                            int dbgCount = state.xessDebugFrames;

                            ImGui::PushItemWidth(95.0f * menuResScale);
                            if (ImGui::InputInt("frames", &dbgCount))
                            {
                                if (dbgCount < 4)
                                    dbgCount = 4;
                                else if (dbgCount > 999)
                                    dbgCount = 999;

                                state.xessDebugFrames = dbgCount;
                            }

                            ImGui::PopItemWidth();

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }

                    // FFX -----------------
                    if (currentFeature->Name() != "DLSSD" &&
                        (currentBackend == Upscaler::FSR31 || currentBackend == Upscaler::FSR31_11on12))
                    {
                        ImGui::SeparatorText("FFX Settings");

                        if (_ffxUpscalerIndex < 0)
                            _ffxUpscalerIndex = config->FfxUpscalerIndex.value_or_default();

                        if (currentBackend == Upscaler::FSR31 ||
                            currentBackend == Upscaler::FSR31_11on12 && state.ffxUpscalerVersionNames.size() > 0)
                        {
                            ImGui::PushItemWidth(135.0f * menuResScale);

                            auto currentName = StrFmt("FSR %s", state.ffxUpscalerVersionNames[_ffxUpscalerIndex]);
                            if (ImGui::BeginCombo("FFX Upscaler", currentName.c_str()))
                            {
                                for (int n = 0; n < state.ffxUpscalerVersionIds.size(); n++)
                                {
                                    auto name = StrFmt("FSR %s", state.ffxUpscalerVersionNames[n]);
                                    if (ImGui::Selectable(name.c_str(),
                                                          config->FfxUpscalerIndex.value_or_default() == n))
                                        _ffxUpscalerIndex = n;
                                }

                                ImGui::EndCombo();
                            }
                            ImGui::PopItemWidth();

                            ShowHelpMarker("List of upscalers reported by FFX SDK");

                            ImGui::SameLine(0.0f, 6.0f);

                            if (ImGui::Button("Change Upscaler") &&
                                _ffxUpscalerIndex != config->FfxUpscalerIndex.value_or_default())
                            {
                                config->FfxUpscalerIndex = _ffxUpscalerIndex;
                                state.newBackend = currentBackend;
                                MARK_ALL_BACKENDS_CHANGED();
                            }

                            auto majorFsrVersion = currentFeature->Version().major;

                            if (majorFsrVersion >= 4)
                            {
                                ImGui::Spacing();

                                ImGui::BeginDisabled(config->FsrNonLinearSRGB.value_or_default() ||
                                                     config->FsrNonLinearPQ.value_or_default());

                                if (bool nlCS = config->FsrNonLinearColorSpace.value_or_default();
                                    ImGui::Checkbox("Non-Linear Color Space", &nlCS))
                                {
                                    config->FsrNonLinearColorSpace = nlCS;
                                    state.newBackend = currentBackend;
                                    MARK_ALL_BACKENDS_CHANGED();
                                }

                                ImGui::EndDisabled();

                                ShowHelpMarker("Indicates input color resource uses Non-Linear color space\n"
                                               "Might improve upscaling quality of FSR4\n"
                                               "Might increase ghosting");

                                if (ImGui::BeginTable("nonLinear", 2, ImGuiTableFlags_SizingStretchProp))
                                {

                                    ImGui::TableNextColumn();

                                    if (bool nlSRGB = config->FsrNonLinearSRGB.value_or_default();
                                        ImGui::Checkbox("Non-Linear sRGB Input", &nlSRGB))
                                    {
                                        config->FsrNonLinearSRGB = nlSRGB;

                                        if (nlSRGB)
                                        {
                                            config->FsrNonLinearPQ = false;
                                            config->FsrNonLinearColorSpace.set_volatile_value(true);
                                        }
                                        else
                                        {
                                            // If has config value revert back to it, otherwise reset
                                            if (config->FsrNonLinearColorSpace.value_for_config().has_value())
                                            {
                                                config->FsrNonLinearColorSpace =
                                                    config->FsrNonLinearColorSpace.value_for_config();
                                            }
                                            else
                                            {
                                                config->FsrNonLinearColorSpace.reset();
                                            }
                                        }

                                        state.newBackend = currentBackend;
                                        MARK_ALL_BACKENDS_CHANGED();
                                    }
                                    ShowHelpMarker("Indicates input color resource contains perceptual sRGB colors\n"
                                                   "Might improve upscaling quality of FSR4\n"
                                                   "Might increase ghosting");

                                    ImGui::TableNextColumn();

                                    if (bool nlPQ = config->FsrNonLinearPQ.value_or_default();
                                        ImGui::Checkbox("Non-Linear PQ Input", &nlPQ))
                                    {
                                        config->FsrNonLinearPQ = nlPQ;

                                        if (nlPQ)
                                        {
                                            config->FsrNonLinearSRGB = false;
                                            config->FsrNonLinearColorSpace.set_volatile_value(true);
                                        }
                                        else
                                        {
                                            // If has config value revert back to it othervise reset
                                            if (config->FsrNonLinearColorSpace.value_for_config().has_value())
                                            {
                                                config->FsrNonLinearColorSpace =
                                                    config->FsrNonLinearColorSpace.value_for_config();
                                            }
                                            else
                                            {
                                                config->FsrNonLinearColorSpace.reset();
                                            }
                                        }

                                        state.newBackend = currentBackend;
                                        MARK_ALL_BACKENDS_CHANGED();
                                    }
                                    ShowHelpMarker("Indicates input color resource contains perceptual PQ colors\n"
                                                   "Might improve upscaling quality of FSR4\n"
                                                   "Rarest, might increase ghosting and break lights");

                                    ImGui::EndTable();
                                }

                                std::array<const char*, 7> models = { "Default", "Model 0", "Model 1", "Model 2",
                                                                      "Model 3", "Model 4", "Model 5" };

                                // Conversion from 0 -> 6 into nullopt + 0 -> 5 is required
                                uint32_t configModes = 0;

                                if (config->Fsr4Model.has_value())
                                    configModes = config->Fsr4Model.value_or(0) + 1;

                                if (configModes < 0 || configModes >= models.size())
                                    configModes = 0;

                                const char* selectedModel = models[configModes];

                                if (ImGui::BeginTable("nonLinear", 2, ImGuiTableFlags_SizingStretchProp))
                                {

                                    ImGui::TableNextColumn();

                                    if (ImGui::BeginCombo("Models", selectedModel))
                                    {
                                        for (int n = 0; n < models.size(); n++)
                                        {
                                            uint32_t selection = 0;

                                            if (config->Fsr4Model.has_value())
                                                selection = config->Fsr4Model.value_or(0) + 1;

                                            if (ImGui::Selectable(models[n], selection == n))
                                            {
                                                if (n < 1)
                                                    config->Fsr4Model.reset();
                                                else
                                                    config->Fsr4Model = n - 1;

                                                state.newBackend = currentBackend;
                                                MARK_ALL_BACKENDS_CHANGED();
                                            }
                                        }

                                        ImGui::EndCombo();
                                    }
                                    ShowHelpMarker("Each FSR4 preset uses its own model.\n"
                                                   "Selecting a model won't change the upscaler preset!\n\n"
                                                   "Model 0 is meant for FSR Native AA\n"
                                                   "Model 1 is meant for Quality/Ultra Quality\n"
                                                   "Model 2 is meant for Balanced\n"
                                                   "Model 3 is meant for Performance\n"
                                                   "Model 5 is meant for Ultra Performance");

                                    // ImGui::PopItemWidth();

                                    // ImGui::SameLine(0.0f, 6.0f);

                                    ImGui::TableNextColumn();

                                    ImGui::Text("Current model: %d", state.currentFsr4Model);

                                    ImGui::EndTable();
                                }
                            }

                            if (majorFsrVersion >= 3)
                            {
                                if (bool dView = config->FsrDebugView.value_or_default();
                                    ImGui::Checkbox("Upscaler Debug View", &dView))
                                {
                                    config->FsrDebugView = dView;

                                    if (majorFsrVersion > 3)
                                    {
                                        config->Fsr4EnableDebugView = dView;
                                        state.newBackend = currentBackend;
                                        MARK_ALL_BACKENDS_CHANGED();
                                    }
                                }

                                if (majorFsrVersion > 3)
                                {
                                    ShowHelpMarker("Top left: Dilated Motion Vectors\n"
                                                   "Top right: Predicted Blend Factor");
                                }
                                else
                                {
                                    ShowHelpMarker("Top left: Dilated Motion Vectors\n"
                                                   "Top middle: Protected Areas\n"
                                                   "Top right: Dilated Depth\n"
                                                   "Middle: Upscaled frame\n"
                                                   "Bottom left: Disocclusion mask\n"
                                                   "Bottom middle: Reactiveness\n"
                                                   "Bottom right: Detail Protection Takedown");
                                }

                                if (majorFsrVersion > 3)
                                {
                                    ImGui::SameLine(0.0f, 6.0f);

                                    if (bool fsr4wm = config->Fsr4EnableWatermark.value_or_default();
                                        ImGui::Checkbox("Upscaler Watermark", &fsr4wm))
                                    {
                                        LOG_DEBUG("FSR4 Watermark set to {}", fsr4wm);
                                        config->Fsr4EnableWatermark = fsr4wm;
                                    }

                                    ShowHelpMarker("After changing this option, please Save INI\n"
                                                   "It will be applied on next launch.");
                                }
                            }

                            if (currentFeature->Version() >= feature_version { 3, 1, 1 } &&
                                currentFeature->Version() < feature_version { 4, 0, 0 })
                            {
                                if (auto ch = ScopedCollapsingHeader("FSR 3 Upscaler Fine Tuning"); ch.IsHeaderOpen())
                                {
                                    ScopedIndent indent {};
                                    ImGui::Spacing();
                                    ImGui::Spacing();

                                    ImGui::PushItemWidth(220.0f * menuResScale);

                                    float velocity = config->FsrVelocity.value_or_default();
                                    if (ImGui::SliderFloat("Velocity Factor", &velocity, 0.00f, 1.0f, "%.2f"))
                                        config->FsrVelocity = velocity;

                                    ShowHelpMarker("Value of 0.0f can improve temporal stability of bright pixels\n"
                                                   "Lower values are more stable with ghosting\n"
                                                   "Higher values are more pixelly but less ghosting.");

                                    if (currentFeature->Version() >= feature_version { 3, 1, 4 })
                                    {
                                        // Reactive Scale
                                        float reactiveScale = config->FsrReactiveScale.value_or_default();
                                        if (ImGui::SliderFloat("Reactive Scale", &reactiveScale, 0.0f, 100.0f, "%.1f"))
                                            config->FsrReactiveScale = reactiveScale;

                                        ShowHelpMarker("Meant for development purpose to test if\n"
                                                       "writing a larger value to reactive mask, reduces ghosting.");

                                        // Shading Scale
                                        float shadingScale = config->FsrShadingScale.value_or_default();
                                        if (ImGui::SliderFloat("Shading Scale", &shadingScale, 0.0f, 100.0f, "%.1f"))
                                            config->FsrShadingScale = shadingScale;

                                        ShowHelpMarker("Increasing this scales fsr3.1 computed shading\n"
                                                       "change value at read to have higher reactiveness.");

                                        // Accumulation Added Per Frame
                                        float accAddPerFrame = config->FsrAccAddPerFrame.value_or_default();
                                        if (ImGui::SliderFloat("Acc. Added Per Frame", &accAddPerFrame, 0.00f, 1.0f,
                                                               "%.2f"))
                                            config->FsrAccAddPerFrame = accAddPerFrame;

                                        ShowHelpMarker(
                                            "Corresponds to amount of accumulation added per frame\n"
                                            "at pixel coordinate where disocclusion occured or when\n"
                                            "reactive mask value is > 0.0f. Decreasing this and \n"
                                            "drawing the ghosting object (IE no mv) to reactive mask \n"
                                            "with value close to 1.0f can decrease temporal ghosting.\n"
                                            "Decreasing this could result in more thin feature pixels flickering.");

                                        // Min Disocclusion Accumulation
                                        float minDisOccAcc = config->FsrMinDisOccAcc.value_or_default();
                                        if (ImGui::SliderFloat("Min. Disocclusion Acc.", &minDisOccAcc, -1.0f, 1.0f,
                                                               "%.2f"))
                                            config->FsrMinDisOccAcc = minDisOccAcc;

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
                    }

                    // DLSS -----------------
                    if ((config->DLSSEnabled.value_or_default() && currentBackend == Upscaler::DLSS &&
                         currentFeature->Version().major > 2) ||
                        currentFeature->Name() == "DLSSD")
                    {
                        const bool usesDlssd = currentFeature->Name() == "DLSSD";

                        if (usesDlssd)
                            ImGui::SeparatorText("DLSSD Settings");
                        else
                            ImGui::SeparatorText("DLSS Settings");

                        auto overridden =
                            usesDlssd ? state.dlssdPresetsOverriddenExternally : state.dlssPresetsOverriddenExternally;

                        if (overridden)
                        {
                            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f), "Presets are overridden externally");
                            ShowHelpMarker("This usually happens due to using tools\n"
                                           "such as Nvidia App or Nvidia Inspector");
                            ImGui::Text("Selecting setting below will disable that external override\n"
                                        "but you need to Save INI and restart the game");

                            ImGui::Spacing();
                        }

                        if (usesDlssd)
                        {
                            if (bool pOverride = config->DLSSDRenderPresetOverride.value_or_default();
                                ImGui::Checkbox("Render Presets Override", &pOverride))
                                config->DLSSDRenderPresetOverride = pOverride;

                            ShowHelpMarker("Each render preset has it strengths and weaknesses\n"
                                           "Override to potentially improve image quality\n"
                                           "Press apply after enable/disable");

                            ImGui::BeginDisabled(!config->DLSSDRenderPresetOverride.value_or_default() || overridden);
                            ImGui::PushItemWidth(135.0f * menuResScale);

                            AddDLSSDRenderPreset("Override Preset", &config->DLSSDRenderPresetForAll);

                            ImGui::PopItemWidth();
                            ImGui::EndDisabled();
                        }
                        else
                        {
                            if (bool pOverride = config->RenderPresetOverride.value_or_default();
                                ImGui::Checkbox("Render Presets Override", &pOverride))
                                config->RenderPresetOverride = pOverride;

                            ShowHelpMarker("Each render preset has it strengths and weaknesses\n"
                                           "Override to potentially improve image quality\n"
                                           "Press apply after enable/disable");

                            ImGui::BeginDisabled(!config->RenderPresetOverride.value_or_default() || overridden);

                            ImGui::PushItemWidth(135.0f * menuResScale);

                            AddDLSSRenderPreset("Override Preset", &config->RenderPresetForAll);

                            ImGui::PopItemWidth();
                            ImGui::EndDisabled();
                        }

                        ImGui::SameLine(0.0f, 6.0f);

                        if (ImGui::Button("Apply Changes"))
                        {
                            if (usesDlssd)
                                state.newBackend = Upscaler::DLSSD;
                            else
                                state.newBackend = currentBackend;

                            MARK_ALL_BACKENDS_CHANGED();
                        }

                        ImGui::Spacing();

                        if (auto ch = ScopedCollapsingHeader(usesDlssd ? "Advanced DLSSD Settings"
                                                                       : "Advanced DLSS Settings");
                            ch.IsHeaderOpen())
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            bool appIdOverride = config->UseGenericAppIdWithDlss.value_or_default();
                            if (ImGui::Checkbox("Use Generic App Id with DLSS", &appIdOverride))
                                config->UseGenericAppIdWithDlss = appIdOverride;

                            ShowHelpMarker("Use generic appid with NGX\n"
                                           "Fixes OptiScaler preset override not working with certain games\n"
                                           "Requires a game restart.");

                            ImGui::BeginDisabled(!config->RenderPresetOverride.value_or_default() || overridden);
                            ImGui::Spacing();
                            ImGui::PushItemWidth(135.0f * menuResScale);

                            if (usesDlssd)
                            {
                                AddDLSSDRenderPreset("DLAA Preset", &config->DLSSDRenderPresetDLAA);
                                AddDLSSDRenderPreset("UltraQ Preset", &config->DLSSDRenderPresetUltraQuality);
                                AddDLSSDRenderPreset("Quality Preset", &config->DLSSDRenderPresetQuality);
                                AddDLSSDRenderPreset("Balanced Preset", &config->DLSSDRenderPresetBalanced);
                                AddDLSSDRenderPreset("Perf Preset", &config->DLSSDRenderPresetPerformance);
                                AddDLSSDRenderPreset("UltraP Preset", &config->DLSSDRenderPresetUltraPerformance);
                            }
                            else
                            {
                                AddDLSSRenderPreset("DLAA Preset", &config->RenderPresetDLAA);
                                AddDLSSRenderPreset("UltraQ Preset", &config->RenderPresetUltraQuality);
                                AddDLSSRenderPreset("Quality Preset", &config->RenderPresetQuality);
                                AddDLSSRenderPreset("Balanced Preset", &config->RenderPresetBalanced);
                                AddDLSSRenderPreset("Perf Preset", &config->RenderPresetPerformance);
                                AddDLSSRenderPreset("UltraP Preset", &config->RenderPresetUltraPerformance);
                            }
                            ImGui::PopItemWidth();
                            ImGui::EndDisabled();

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }
                }

                /// FG INPUTS

                static std::vector<MenuOption<FGInput>> inputOptions;
                inputOptions.clear();

                // clang-format off

                inputOptions = {
                    { FGInput::NoFG, "No Frame Generation" },
                    { FGInput::NvngxFG, "Nukem's/Artur's DLSSG",
                        "Limited to FSR3-FG\n\nSupports Hudless out of the box\n\nUses Streamline swapchain for pacing" },
                    { FGInput::FSRFG, "FSR 3.1 FG",
                        "Can be used with any FG Output\n\nSupports Hudless out of the box" },
                    { FGInput::DLSSG, "DLSSG via Streamline",
                        "Can be used with any FG Output\n\nSupports Hudless out of the box\n\nLimited to games that use Streamline v2" },
                    { FGInput::XeFG, "XeFG" },
                    { FGInput::Upscaler, "OptiFG (Upscaler)",
                        "Upscaler must be enabled\n\nCan be used with any FG Output, but might be imperfect with some\n\nTo prevent UI glitching, HUDfix required" },
                    { FGInput::FSRFG30, "FSR 3.0 FG",
                        "Can be used with any FG Output\n\nSupports Hudless out of the box" }
                };

                // clang-format on

                auto constexpr nvngxInputIndex = (uint32_t) FGInput::NvngxFG;
                if (state.activeFgInput == FGInput::NvngxFG)
                {
                    if (Nvngx_FG::isMFG())
                        inputOptions[nvngxInputIndex].label = "Artur's DLSSG";
                    else
                        inputOptions[nvngxInputIndex].label = "Nukem's DLSSG";
                }

                // XeFG input requirements
                auto constexpr xefgInputIndex = (uint32_t) FGInput::XeFG;
                inputOptions[xefgInputIndex].set_disabled(true, "Support not implemented");

                // OptiFG requirements
                auto constexpr optiFgIndex = (uint32_t) FGInput::Upscaler;
                inputOptions[optiFgIndex].set_disabled(
                    state.swapchainApi == API::DX11 || state.swapchainApi == API::Vulkan, "Unsupported API");

                if (!inputOptions[optiFgIndex].disabled && state.activeFgOutput == FGOutput::FSRFG &&
                    !FfxApiProxy::IsFGReady() && !fsr31InitTried)
                {
                    fsr31InitTried = true;
                    FfxApiProxy::InitFfxDx12();
                    inputOptions[optiFgIndex].set_disabled(!FfxApiProxy::IsFGReady(),
                                                           "amd_fidelityfx_dx12.dll is missing");
                }
                else if (!inputOptions[optiFgIndex].disabled && state.activeFgOutput == FGOutput::XeFG &&
                         !xefgInitTried && XeFGProxy::Module() == nullptr)
                {
                    xefgInitTried = true;
                    XeFGProxy::InitXeFG();
                    inputOptions[optiFgIndex].set_disabled(XeFGProxy::Module() == nullptr, "libxess_fg.dll is missing");
                }

                // DLSSG inputs requirements
                auto constexpr dlssgInputIndex = (uint32_t) FGInput::DLSSG;
                inputOptions[dlssgInputIndex].set_disabled(state.swapchainApi == API::DX11, "Unsupported API");

                if (!inputOptions[dlssgInputIndex].disabled && state.streamlineVersion.major < 2)
                {
                    inputOptions[dlssgInputIndex].set_disabled(
                        true, std::format("Unsupported Streamline version: {}.{}.{}", state.streamlineVersion.major,
                                          state.streamlineVersion.minor, state.streamlineVersion.patch));
                }

                // FSRFG inputs requirements
                auto constexpr fsrfgInputIndex = (uint32_t) FGInput::FSRFG;
                inputOptions[fsrfgInputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");

                // FSRFG30 inputs requirements
                auto constexpr fsrfg30InputIndex = (uint32_t) FGInput::FSRFG30;
                inputOptions[fsrfg30InputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");

                if (!config->FGInput.has_value())
                    config->FGInput = config->FGInput.value_or_default(); // need to have a value before combo

                /// FG OUTPUTS

                static std::vector<MenuOption<FGOutput>> outputOptions;
                outputOptions.clear();

                // clang-format off

                outputOptions = {
                    { FGOutput::NoFG, "No Frame Generation" },
                    { FGOutput::NvngxFG, "FSR3-FG Nukem/Enabler", "Enable DLSS-FG in-game" },
                    { FGOutput::FSRFG, "FSR FG", "FSR3/4 FG" },
                    { FGOutput::DLSSG, "DLSSG", "For 40xx and above" },
                    { FGOutput::XeFG, "XeFG", "XeFG" },
                    { FGOutput::DLSSGWithNvngx, "DLSSG with Nvngx FG", "Streamline + NvngxFG" }
                };

                // clang-format on

                // DLSSG output requirements
                auto constexpr dlssgOutputIndex = (uint32_t) FGOutput::DLSSG;
                outputOptions[dlssgOutputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");
                outputOptions[dlssgOutputIndex].set_disabled(
                    primaryGpu.nvidiaArchInfo.architecture_id < NV_GPU_ARCHITECTURE_AD100, "Unsupported hardware");

                // Nukem's FG mod requirements
                auto constexpr nvngxOutputIndex = (uint32_t) FGOutput::NvngxFG;
                if (state.activeFgOutput == FGOutput::NvngxFG)
                {
                    if (Nvngx_FG::isMFG())
                        outputOptions[nvngxOutputIndex].label = "FSR3-MFG via DLSS Enabler";
                    else
                        outputOptions[nvngxOutputIndex].label = "FSR3-FG via Nukem's";
                }
                if (!state.NvngxFgFilesAvailable)
                {
                    inputOptions[nvngxInputIndex].set_disabled(
                        true, "Missing dlssg_to_fsr3_amd_is_better.dll\nor dlss-enabler-headless.asi");
                    outputOptions[nvngxOutputIndex].set_disabled(
                        true, "Missing dlssg_to_fsr3_amd_is_better.dll\nor dlss-enabler-headless.asi");
                }

                // For that one case of DX11 DLSSG
                const auto streamlineVersion = state.streamlineVersion;
                const bool nukemsUnsupportedApi =
                    state.swapchainApi == API::DX11 && (streamlineVersion == feature_version { 0, 0, 0 } ||
                                                        streamlineVersion > feature_version { 2, 0, 1 });
                inputOptions[nvngxInputIndex].set_disabled(nukemsUnsupportedApi, "Unsupported API");
                outputOptions[nvngxOutputIndex].set_disabled(nukemsUnsupportedApi, "Unsupported API");

                auto constexpr DLSSGWithNvngxOutputIndex = (uint32_t) FGOutput::DLSSGWithNvngx;
                if (!state.NvngxFgFilesAvailable)
                {
                    outputOptions[DLSSGWithNvngxOutputIndex].set_disabled(
                        true, "Missing the dlssg_to_fsr3_amd_is_better.dll file");
                }
                outputOptions[DLSSGWithNvngxOutputIndex].set_disabled(state.swapchainApi != API::DX12,
                                                                      "Unsupported API");

                // FSR FG output requirements
                auto constexpr fsrfgOutputIndex = (uint32_t) FGOutput::FSRFG;
                outputOptions[fsrfgOutputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");

                // XeFG output requirements
                auto constexpr xefgOutputIndex = (uint32_t) FGOutput::XeFG;
                outputOptions[xefgOutputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");

                // Unsupported FG input selected
                const auto currentInputIndex = (uint32_t) state.activeFgInput;
                if (config->FGInput != FGInput::NoFG && inputOptions.size() > currentInputIndex &&
                    inputOptions[currentInputIndex].disabled && state.activeFgInput == config->FGInput)
                {
                    LOG_WARN("Resetting FGInput to NoFG: {}", inputOptions[currentInputIndex].label);
                    config->FGInput = FGInput::NoFG;

                    // Changing active can be dangerous but we are talking about an unsupported mode
                    // which shouldn't even actually have taken affect
                    state.activeFgInput = FGInput::NoFG;
                }

                // Unsupported FG output selected
                const auto currentOutputIndex = (uint32_t) state.activeFgOutput;
                if (config->FGOutput != FGOutput::NoFG && outputOptions.size() > currentOutputIndex &&
                    outputOptions[currentOutputIndex].disabled && state.activeFgOutput == config->FGOutput)
                {
                    LOG_WARN("Resetting FGOutput to NoFG: {}", outputOptions[currentOutputIndex].label);
                    config->FGOutput = FGOutput::NoFG;
                    state.activeFgOutput = FGOutput::NoFG;
                }

                if (!config->FGOutput.has_value())
                    config->FGOutput = config->FGOutput.value_or_default(); // need to have a value before combo

                if (state.activeFgInput != FGInput::ForceXeLL)
                {
                    ImGui::SeparatorText("Frame Generation");

                    if (ImGui::BeginTable("fgSelection", 2, ImGuiTableFlags_SizingStretchSame))
                    {
                        ImGui::TableNextColumn();

                        PopulateCombo("FG Input", config->FGInput, inputOptions);
                        ShowTooltip("The data source to be used for FG\n"
                                    "The native FG which the game supports");

                        ImGui::TableNextColumn();

                        const bool disableOutputs = config->FGInput.value_or_default() == FGInput::NvngxFG;

                        ImGui::BeginDisabled(disableOutputs);
                        PopulateCombo("FG Output", config->FGOutput, outputOptions);
                        ImGui::EndDisabled();

                        if (disableOutputs)
                            ShowTooltip("Doesn't matter with the selected FG Source");
                        else
                            ShowTooltip("The FG that you will actually be using");

                        ImGui::EndTable();
                    }

                    auto static fgInputOverridden = false;

                    if (config->FGOutput == FGOutput::NvngxFG && !fgInputOverridden)
                    {
                        config->FGInput = FGInput::NvngxFG;
                        fgInputOverridden = true;
                    }
                    else if (config->FGInput != FGInput::NvngxFG && fgInputOverridden)
                    {
                        config->FGOutput = FGOutput::NoFG;
                        fgInputOverridden = false;
                    }

                    state.fgSettingsChanged = state.activeFgOutput != config->FGOutput.value_or_default() ||
                                              state.activeFgInput != config->FGInput.value_or_default();

                    if (state.fgSettingsChanged)
                    {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.0f, 1.f), "Save INI and restart to apply the changes");
                        ImGui::Spacing();
                    }

                    ImGui::BeginDisabled(state.dlssgDMFGSupported &&
                                         config->FGDLSSGOverrideForceDMFG.value_or_default());
                    if (state.dlssgMfgMax.has_value() && state.dlssgMfgMax.value() >= 1)
                    {
                        auto maxInterpolationCount = state.dlssgMfgMax.value();

                        if (maxInterpolationCount > 1)
                        {
                            const char* intModes[] = { "Default", "Off", "2X", "3X", "4X", "5X", "6X" };

                            // Map config value to UI index
                            int currentSet = 0;
                            if (config->FGDLSSGOverrideInterpolationCount.has_value())
                            {
                                currentSet = config->FGDLSSGOverrideInterpolationCount.value() + 1;
                            }

                            const char* currentIntCount = intModes[currentSet];

                            ImGui::PushItemWidth(95.0f * menuResScale);

                            if (ImGui::BeginCombo("Override DLSSG", currentIntCount))
                            {
                                for (int i = 0; i <= maxInterpolationCount + 1; i++)
                                {
                                    if (ImGui::Selectable(intModes[i], (currentSet == i)))
                                    {
                                        if (i == 0)
                                        {
                                            // Default, no override
                                            config->FGDLSSGOverrideInterpolationCount.reset();
                                        }
                                        else
                                        {
                                            // UI index, store value
                                            int framesToGenerate = i - 1;

                                            LOG_DEBUG("DLSSG Interpolation Count set to: {}", framesToGenerate);
                                            config->FGDLSSGOverrideInterpolationCount = framesToGenerate;
                                        }

                                        StreamlineHooks::updateDlssgOptions();
                                    }
                                }

                                ImGui::EndCombo();
                            }

                            ImGui::PopItemWidth();
                        }
                    }

                    ImGui::EndDisabled();

                    if (state.dlssgDMFGSupported)
                    {
                        ImGui::SameLine(0.0f, 16.0f);

                        ImGui::BeginDisabled(config->FGDLSSGFramerateTargetDMFG.value_or_default());

                        if (bool dynamicMFG = config->FGDLSSGOverrideForceDMFG.value_or_default();
                            ImGui::Checkbox("Force Dynamic MFG", &dynamicMFG))
                        {
                            config->FGDLSSGOverrideForceDMFG = dynamicMFG;
                            StreamlineHooks::updateDlssgOptions();
                        }

                        if (config->FGDLSSGFramerateTargetDMFG.value_or_default())
                        {
                            ShowHelpMarker("You have set a custom FPS target in the config\n"
                                           "You cannot disable DMFG while the game is running");
                        }
                        else
                        {
                            ShowHelpMarker("Currently targets your monitors refresh rate\n"
                                           "For custom targets set FramerateTargetDMFG in the config and restart");
                        }

                        ImGui::EndDisabled();
                    }

                    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
                    if (((state.activeFgOutput == FGOutput::FSRFG || state.activeFgOutput == FGOutput::XeFG ||
                          state.activeFgOutput == FGOutput::DLSSG ||
                          state.activeFgOutput == FGOutput::DLSSGWithNvngx) &&
                         state.activeFgInput != FGInput::NoFG && state.activeFgInput != FGInput::NvngxFG) &&
                        fgOutput)
                    {
                        ImGui::Checkbox("Show Detected UI", &state.FGHudlessCompare);
                        ShowHelpMarker("Needs HUDless texture to compare with final image.\n"
                                       "UI elements and ONLY UI elements should have a pink tint!");

                        const auto isUsingUIAny = fgOutput->IsUsingUIAny();

                        ImGui::BeginDisabled(!isUsingUIAny);

                        if (bool drawUIOverFG = config->FGDrawUIOverFG.value_or_default();
                            ImGui::Checkbox("Draw UI over", &drawUIOverFG))
                        {
                            config->FGDrawUIOverFG = drawUIOverFG;
                        }
                        ShowHelpMarker("Draws UI resource over the final image\n"
                                       "If no UI visible, enable this!");

                        ImGui::EndDisabled();

                        ImGui::SameLine(0.0f, 16.0f);

                        ImGui::BeginDisabled(!isUsingUIAny || !config->FGDrawUIOverFG.value_or_default());

                        if (bool uiPremultipliedAlpha = config->FGUIPremultipliedAlpha.value_or_default();
                            ImGui::Checkbox("UI Premult. alpha", &uiPremultipliedAlpha))
                        {
                            config->FGUIPremultipliedAlpha = uiPremultipliedAlpha;
                        }
                        ShowHelpMarker("If UI is too faint, disable this option");

                        ImGui::EndDisabled();
                    }

                    if (state.activeFgInput == FGInput::DLSSG || state.activeFgInput == FGInput::FSRFG ||
                        state.activeFgInput == FGInput::FSRFG30)
                    {
                        ImGui::Spacing();

                        if (auto ch = ScopedCollapsingHeader("Advanced FG Settings"); ch.IsHeaderOpen())
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
                            if (fgOutput)
                            {
                                ImGui::BeginDisabled(!fgOutput->IsActive());

                                const auto isUsingUIAny = fgOutput->IsUsingUIAny();
                                const auto isUsingHudlessAny = fgOutput->IsUsingHudlessAny();

                                bool disableUI = config->FGDisableUI.value_or_default();
                                ImGui::BeginDisabled(!isUsingUIAny && !disableUI);

                                if (ImGui::Checkbox("Disable UI texture", &disableUI))
                                {
                                    config->FGDisableUI = disableUI;
                                    fgOutput->UpdateTarget();
                                }

                                ShowHelpMarker("For when the game sends a UI texture, but you want to disable it");

                                ImGui::EndDisabled();

                                ImGui::SameLine(0.0f, 16.0f);

                                bool disableHudless = config->FGDisableHudless.value_or_default();
                                ImGui::BeginDisabled(!isUsingHudlessAny && !disableHudless);

                                if (ImGui::Checkbox("Disable HUDless", &disableHudless))
                                {
                                    config->FGDisableHudless = disableHudless;
                                }

                                ShowHelpMarker("For when the game sends HUDless, but you want to disable it");

                                ImGui::EndDisabled();

                                bool depthValidNow = config->FGDepthValidNow.value_or_default();
                                if (ImGui::Checkbox("Depth as ValidNow", &depthValidNow))
                                    config->FGDepthValidNow = depthValidNow;

                                ShowHelpMarker("Will use more VRAM, but Uniscaler needs this\n"
                                               "Maybe some other games might need too");

                                ImGui::SameLine(0.0f, 16.0f);

                                bool velocityValidNow = config->FGVelocityValidNow.value_or_default();
                                if (ImGui::Checkbox("Velocity as ValidNow", &velocityValidNow))
                                    config->FGVelocityValidNow = velocityValidNow;

                                ShowHelpMarker("Will use more VRAM, but Uniscaler needs this\n"
                                               "Maybe some other games might need too");

                                bool hudlessValidNow = config->FGHudlessValidNow.value_or_default();
                                if (ImGui::Checkbox("HUDless as ValidNow", &hudlessValidNow))
                                    config->FGHudlessValidNow = hudlessValidNow;

                                ShowHelpMarker("Will use more VRAM, but some games might need this");

                                ImGui::SameLine(0.0f, 16.0f);

                                bool firstHudless = config->FGOnlyAcceptFirstHudless.value_or_default();
                                if (ImGui::Checkbox("Accept First HUDless", &firstHudless))
                                    config->FGOnlyAcceptFirstHudless = firstHudless;

                                ShowHelpMarker("If source tags more than one HUDless, only use the first one");

                                if (bool skipReset = config->FGSkipReset.value_or_default();
                                    ImGui::Checkbox("Skip Reset", &skipReset))
                                {
                                    config->FGSkipReset = skipReset;
                                }

                                ShowHelpMarker("Don't use reset signals from FG Inputs");

                                ImGui::EndDisabled();

                                ImGui::PushItemWidth(80.0f * menuResScale);

                                auto frameAhead = config->FGAllowedFrameAhead.value_or_default();
                                if (ImGui::InputInt("Frame Ahead", &frameAhead, 1, 1) && frameAhead > 0 &&
                                    frameAhead < 4)
                                {
                                    config->FGAllowedFrameAhead = frameAhead;
                                }

                                ShowHelpMarker("Number of frames the FG is allowed to be ahead of the game\n"
                                               "Might prevent FG on/off switching, but also might cause issues");

                                ImGui::PopItemWidth();

                                ImGui::SameLine(0.0f, 16.0f);

                                const char* ftSources[] = { "Input", "Opti", "Zero" };
                                const char* ftSourceInfos[] = { "Uses frametimes provided by\nDLSSG or FSR-FG ",
                                                                "Uses frametimes calculated by Opti",
                                                                "Let XeFG to handle frametimes" };

                                auto currentSet = (int) config->FTInput.value_or_default();
                                auto currentSourceCount = state.activeFgOutput == FGOutput::XeFG ? 3 : 2;

                                ImGui::PushItemWidth(95.0f * menuResScale);

                                if (ImGui::BeginCombo("FT Input", ftSources[currentSet]))
                                {
                                    for (size_t i = 0; i < currentSourceCount; i++)
                                    {

                                        if (ImGui::Selectable(ftSources[i], currentSet == i))
                                        {
                                            LOG_DEBUG("FTInput has changed {} -> {}", ftSources[currentSet],
                                                      ftSources[i]);
                                            config->FTInput = (FrameTimeSource) i;
                                        }

                                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                            ImGui::SetTooltip(ftSourceInfos[i]);
                                    }

                                    ImGui::EndCombo();
                                }

                                ImGui::PopItemWidth();

                                ShowHelpMarker("Select source for frametime\n"
                                               "Might help frame pacing and stutter issues");
                            }
                        }
                    }

                    ImGui::Spacing();
                }

                // FSR FG controls
                if (state.activeFgOutput == FGOutput::FSRFG && state.activeFgInput != FGInput::NoFG &&
                    state.currentFGSwapchain != nullptr)
                {
                    if (state.activeFgInput != FGInput::Upscaler ||
                        (currentFeature != nullptr && !currentFeature->IsFrozen()) && FfxApiProxy::IsFGReady())
                    {
                        ImGui::SeparatorText("Frame Generation (FSR FG)");

                        if (_ffxFGIndex < 0)
                            _ffxFGIndex = config->FfxFGIndex.value_or_default();

                        if (state.ffxFGVersionNames.size() > 0)
                        {
                            ImGui::PushItemWidth(135.0f * menuResScale);

                            auto currentName = StrFmt("FSR %s", state.ffxFGVersionNames[_ffxFGIndex]);
                            if (ImGui::BeginCombo("FFX FG", currentName.c_str()))
                            {
                                for (int n = 0; n < state.ffxFGVersionIds.size(); n++)
                                {
                                    auto name = StrFmt("FSR %s", state.ffxFGVersionNames[n]);
                                    if (ImGui::Selectable(name.c_str(), config->FfxFGIndex.value_or_default() == n))
                                        _ffxFGIndex = n;
                                }

                                ImGui::EndCombo();
                            }
                            ImGui::PopItemWidth();

                            ShowHelpMarker("List of FGs reported by FFX SDK");

                            ImGui::SameLine(0.0f, 6.0f);

                            if (ImGui::Button("Change FG") && _ffxFGIndex != config->FfxFGIndex.value_or_default())
                            {
                                config->FfxFGIndex = _ffxFGIndex;
                                state.FGchanged = true;
                                state.SCchanged = true;
                            }
                        }

                        bool fgActive = config->FGEnabled.value_or_default();
                        if (ImGui::Checkbox("Active##2", &fgActive))
                        {
                            config->FGEnabled = fgActive;
                            LOG_DEBUG("FGEnabled set FGEnabled: {}", fgActive);

                            if (config->FGEnabled.value_or_default())
                                state.FGchanged = true;
                        }
                        ShowHelpMarker("Enable Frame Generation");

                        bool fgAsync = config->FGAsync.value_or_default();
                        if (ImGui::Checkbox("Allow Async", &fgAsync))
                        {
                            config->FGAsync = fgAsync;

                            if (config->FGEnabled.value_or_default())
                            {
                                state.FGchanged = true;
                                state.SCchanged = true;
                                LOG_DEBUG("Async set FGChanged");
                            }
                        }
                        ShowHelpMarker(
                            "Enable Async for better FG performance\nMight cause crashes, especially with HUD Fix!");

                        ImGui::SameLine(0.0f, 16.0f);

                        bool fgDV = config->FGDebugView.value_or_default();
                        if (ImGui::Checkbox("Debug View##2", &fgDV))
                        {
                            config->FGDebugView = fgDV;

                            if (config->FGEnabled.value_or_default())
                            {
                                state.FGchanged = true;
                                LOG_DEBUG("DebugView set FGChanged");
                            }
                        }
                        ShowHelpMarker("Enable FSR3.1-FG Debug view\n\n"
                                       "Top left: Game Motion Vectors\n"
                                       "Top middle: GMV Depth\n"
                                       "Top right: Optical Flow MV\n"
                                       "Middle: Interpolated frame only\n"
                                       "Bottom left: Disocclusion mask\n"
                                       "Bottom middle: Interpolation source (w/o UI)\n"
                                       "Bottom right: HUDless resource");

                        ImGui::SameLine(0.0f, 16.0f);

                        if (state.currentFG->Version().major > 3)
                        {
                            if (bool fgwm = config->FSRFGEnableWatermark.value_or_default();
                                ImGui::Checkbox("Enable Watermark", &fgwm))
                            {
                                LOG_DEBUG("FSRFGEnableWatermark set FGWatermark: {}", fgwm);
                                config->FSRFGEnableWatermark = fgwm;
                            }

                            ShowHelpMarker("After changing this option, please Save INI\n"
                                           "It will be applied on next launch.");
                        }

                        ImGui::Spacing();
                        ImGui::Spacing();
                        if (auto ch = ScopedCollapsingHeader("Advanced FSR FG Settings"); ch.IsHeaderOpen())
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            ImGui::Checkbox("FG Only Generated", &state.FGonlyGenerated);
                            ShowHelpMarker("Display only FSR 3.1 Generated frames");

                            ImGui::SameLine(0.0f, 16.0f);
                            auto debugResetLines = config->FGDebugResetLines.value_or_default();
                            if (ImGui::Checkbox("Debug Reset Lines", &debugResetLines))
                            {
                                config->FGDebugResetLines = debugResetLines;
                                LOG_DEBUG("Enabled set FGDebugLines: {}", debugResetLines);
                            }
                            ShowHelpMarker("Enables drawing of Interpolation skip lines");

                            auto debugTearLines = config->FGDebugTearLines.value_or_default();
                            if (ImGui::Checkbox("Debug Tear Lines", &debugTearLines))
                            {
                                config->FGDebugTearLines = debugTearLines;
                                LOG_DEBUG("Enabled set FGDebugLines: {}", debugTearLines);
                            }
                            ShowHelpMarker("Enables drawing of Tear and Interpolation skip lines");

                            ImGui::SameLine(0.0f, 16.0f);
                            auto debugPacingLines = config->FGDebugPacingLines.value_or_default();
                            if (ImGui::Checkbox("Debug Pacing Lines", &debugPacingLines))
                            {
                                config->FGDebugPacingLines = debugPacingLines;
                                LOG_DEBUG("Enabled set FGDebugLines: {}", debugPacingLines);
                            }
                            ShowHelpMarker("Enables drawing of Pacing lines");

                            ImGui::Spacing();
                            if (ImGui::TreeNode("FG Rectangle Settings"))
                            {
                                ImGui::PushItemWidth(95.0f * menuResScale);
                                int rectLeft = config->FGRectLeft.value_or(0);
                                if (ImGui::InputInt("Rect Left", &rectLeft))
                                    config->FGRectLeft = rectLeft;

                                ImGui::SameLine(0.0f, 16.0f);
                                int rectTop = config->FGRectTop.value_or(0);
                                if (ImGui::InputInt("Rect Top", &rectTop))
                                    config->FGRectTop = rectTop;

                                int rectWidth = config->FGRectWidth.value_or(0);
                                if (ImGui::InputInt("Rect Width", &rectWidth))
                                    config->FGRectWidth = rectWidth;

                                ImGui::SameLine(0.0f, 16.0f);
                                int rectHeight = config->FGRectHeight.value_or(0);
                                if (ImGui::InputInt("Rect Height", &rectHeight))
                                    config->FGRectHeight = rectHeight;

                                ImGui::PopItemWidth();
                                ShowHelpMarker("Frame generation rectangle, adjust for letterboxed content");

                                ImGui::BeginDisabled(
                                    !config->FGRectLeft.has_value() && !config->FGRectTop.has_value() &&
                                    !config->FGRectWidth.has_value() && !config->FGRectHeight.has_value());

                                if (ImGui::Button("Reset FG Rect"))
                                {
                                    config->FGRectLeft.reset();
                                    config->FGRectTop.reset();
                                    config->FGRectWidth.reset();
                                    config->FGRectHeight.reset();
                                }

                                ShowHelpMarker("Resets Frame generation rectangle");

                                ImGui::EndDisabled();
                                ImGui::TreePop();
                            }

                            auto fg = state.currentFG;
                            if (fg != nullptr && strcmp(fg->Name(), "FSR-FG") == 0 &&
                                FfxApiProxy::VersionDx12_FG() >= feature_version { 3, 1, 3 })
                            {
                                ImGui::Spacing();

                                if (ImGui::TreeNode("Frame Pacing Tuning"))
                                {
                                    auto fptEnabled = config->FGFramePacingTuning.value_or_default();
                                    if (ImGui::Checkbox("Enable Tuning", &fptEnabled))
                                    {
                                        config->FGFramePacingTuning = fptEnabled;
                                        state.FSRFGFTPchanged = true;
                                    }

                                    ImGui::BeginDisabled(!config->FGFramePacingTuning.value_or_default());

                                    ImGui::PushItemWidth(115.0f * menuResScale);
                                    auto fptSafetyMargin = config->FGFPTSafetyMarginInMs.value_or_default();
                                    if (ImGui::InputFloat("Safety Margins in ms", &fptSafetyMargin, 0.01f, 0.1f,
                                                          "%.2f"))
                                        config->FGFPTSafetyMarginInMs = fptSafetyMargin;
                                    ShowHelpMarker("Safety margins in millisecons\n"
                                                   "FSR default value: 0.1ms\n"
                                                   "Opti default value: 0.01ms");

                                    auto fptVarianceFactor = config->FGFPTVarianceFactor.value_or_default();
                                    if (ImGui::SliderFloat("Variance Factor", &fptVarianceFactor, 0.0f, 1.0f, "%.2f"))
                                        config->FGFPTVarianceFactor = fptVarianceFactor;
                                    ShowHelpMarker("Variance factor\n"
                                                   "FSR default value: 0.1\n"
                                                   "Opti default value: 0.3");
                                    ImGui::PopItemWidth();

                                    auto fpHybridSpin = config->FGFPTAllowHybridSpin.value_or_default();
                                    if (ImGui::Checkbox("Enable Hybrid Spin", &fpHybridSpin))
                                        config->FGFPTAllowHybridSpin = fpHybridSpin;
                                    ShowHelpMarker("Allows pacing spinlock to sleep, should reduce CPU usage\n"
                                                   "Might cause slow ramp up of FPS");

                                    ImGui::PushItemWidth(115.0f * menuResScale);
                                    auto fptHybridSpinTime = config->FGFPTHybridSpinTime.value_or_default();
                                    if (ImGui::SliderInt("Hybrid Spin Time", &fptHybridSpinTime, 0, 100))
                                        config->FGFPTHybridSpinTime = fptHybridSpinTime;
                                    ShowHelpMarker("How long to spin if FPTHybridSpin is true. Measured in timer "
                                                   "resolution units.\n"
                                                   "Not recommended to go below 2. Will result in frequent overshoots");
                                    ImGui::PopItemWidth();

                                    auto fpWaitForSingleObjectOnFence =
                                        config->FGFPTAllowWaitForSingleObjectOnFence.value_or_default();
                                    if (ImGui::Checkbox("Enable WaitForSingleObjectOnFence",
                                                        &fpWaitForSingleObjectOnFence))
                                    {
                                        config->FGFPTAllowWaitForSingleObjectOnFence = fpWaitForSingleObjectOnFence;
                                    }
                                    ShowHelpMarker("Allows WaitForSingleObject instead of spinning for fence value");

                                    if (ImGui::Button("Apply Timing Changes"))
                                        state.FSRFGFTPchanged = true;

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
                if (state.activeFgOutput == FGOutput::XeFG && state.activeFgInput != FGInput::NoFG &&
                    state.activeFgInput != FGInput::ForceXeLL && state.currentFGSwapchain != nullptr)
                {
                    if (XeFGProxy::InitXeFG() && currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        ImGui::SeparatorText("Frame Generation (XeFG)");

                        bool ignoreChecks = config->FGXeFGIgnoreInitChecks.value_or_default();

                        bool nativeAA = false;
                        if (state.activeFgInput == FGInput::Upscaler && currentFeature != nullptr)
                            nativeAA = currentFeature->RenderWidth() == currentFeature->DisplayWidth();

                        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
                        const bool correctMVs = fgOutput && fgOutput->IsLowResMV() || nativeAA || ignoreChecks;

                        if (!correctMVs || state.realExclusiveFullscreen)
                        {
                            config->FGEnabled.reset();
                            config->FGXeFGDebugView.reset();
                        }

                        const bool restartNeeded =
                            fgOutput &&
                            (config->FGXeFGDepthInverted.value_or_default() != fgOutput->IsInvertedDepth() ||
                             config->FGXeFGJitteredMV.value_or_default() != fgOutput->IsJitteredMVs() ||
                             config->FGXeFGHighResMV.value_or_default() == fgOutput->IsLowResMV());

                        bool cantActivate = false;
                        if (restartNeeded)
                        {
                            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f),
                                               "Restart the game to apply correct XeFG settings!");
                        }
                        else
                        {
                            if (!correctMVs)
                                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f),
                                                   "Requires disabling dilated motion vectors");

                            if (!ignoreChecks && state.realExclusiveFullscreen)
                            {
                                cantActivate = true;
                                ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Borderless display mode required!");
                            }

                            if (!ignoreChecks && state.isHdrActive)
                            {
                                if (state.currentSwapchainDesc.BufferDesc.Format > 0 &&
                                    state.currentSwapchainDesc.BufferDesc.Format < 15)
                                {
                                    cantActivate = true;
                                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.f), "XeFG only supports HDR10");
                                }
                            }
                        }

                        if (!correctMVs || cantActivate || ignoreChecks)
                        {
                            if (ImGui::Checkbox("Ignore Init Checks", &ignoreChecks))
                                config->FGXeFGIgnoreInitChecks = ignoreChecks;

                            ShowHelpMarker("Ignores all prechecks for XeFG\n"
                                           "Don't use this option to skip MV size warning for UE games!\n"
                                           "It might cause crashes and bad IQ!");
                        }

                        ImGui::BeginDisabled(!correctMVs || cantActivate);

                        bool fgActive = config->FGEnabled.value_or_default();
                        if (ImGui::Checkbox("Active##3", &fgActive))
                        {
                            config->FGEnabled = fgActive;
                            LOG_DEBUG("Enabled set FGEnabled: {}", fgActive);

                            if (config->FGEnabled.value_or_default())
                                state.FGchanged = true;
                        }

                        ShowHelpMarker("Enable Frame Generation");

                        auto maxInterpolationCount = state.xefgMaxInterpolationCount;

                        if (maxInterpolationCount > 1)
                        {
                            ImGui::SameLine(0.0f, 16.0f);

                            const char* intModes[] = { "2X", "3X", "4X", "5X", "6X" };
                            auto currentSet = config->FGXeFGInterpolationCount.value_or_default() - 1;
                            auto currentIntCount = intModes[currentSet];

                            ImGui::PushItemWidth(95.0f * menuResScale);

                            if (ImGui::BeginCombo("MFG", currentIntCount))
                            {
                                for (int i = 0; i < maxInterpolationCount; i++)
                                {
                                    if (ImGui::Selectable(intModes[i], (currentSet == i)))
                                    {
                                        LOG_DEBUG("XeFG Interpolation Count set to: {}", i + 1);
                                        config->FGXeFGInterpolationCount = i + 1;
                                    }
                                }

                                ImGui::EndCombo();
                            }

                            ImGui::PopItemWidth();

                            ShowHelpMarker("Set XeFG interpolation count");
                        }

                        ImGui::SameLine(0.0f, 16.0f);
                        ImGui::BeginDisabled(!fgOutput->IsUsingHudlessAny() ||
                                             XeFGProxy::SetUiCompositionState() == nullptr);
                        bool fgCompositeUI = config->FGXeFGUIComposition.value_or_default();
                        if (ImGui::Checkbox("UI Composition", &fgCompositeUI))
                            config->FGXeFGUIComposition = fgCompositeUI;

                        ShowHelpMarker("Disable HUD/UI interpolation\n"
                                       "Reverts back to previous XeFG 2 behaviour\n\n"
                                       "Fixes artifacting transparent HUD/UI");
                        ImGui::EndDisabled();

                        bool fgDV = config->FGXeFGDebugView.value_or_default();
                        if (ImGui::Checkbox("Debug View##2", &fgDV))
                        {
                            config->FGXeFGDebugView = fgDV;

                            if (config->FGXeFGDebugView.value_or_default())
                            {
                                state.FGchanged = true;
                                LOG_DEBUG("DebugView set FGChanged");
                            }
                        }
                        ShowHelpMarker("Enable XeFG Debug view");

                        ImGui::EndDisabled();

                        ImGui::SameLine(0.0f, 16.0f);
                        bool fgBorderless = config->FGXeFGForceBorderless.value_or_default();
                        if (ImGui::Checkbox("Force Borderless", &fgBorderless))
                            config->FGXeFGForceBorderless = fgBorderless;

                        ShowHelpMarker("Forces Borderless display mode\n\n"
                                       "For best results, set fullscreen \n"
                                       "resolution to your display resolution\n"
                                       "Might cause some instability issues.\n\n"
                                       "NEEDS GAME RESTART TO BE ACTIVE!");

                        // Disable this for now
                        // ImGui::SameLine(0.0f, 16.0f);
                        // ImGui::Checkbox("Only Generated##2", &state.FGonlyGenerated);
                        // ShowHelpMarker("Display only XeFG generated frames");

                        ImGui::Spacing();
                        if (auto ch = ScopedCollapsingHeader("Advanced XeFG Settings"); ch.IsHeaderOpen())
                        {
                            ImGui::Spacing();
                            if (ImGui::TreeNode("Rectangle Settings"))
                            {
                                ImGui::PushItemWidth(95.0f * menuResScale);
                                int rectLeft = config->FGRectLeft.value_or(0);
                                if (ImGui::InputInt("Rect Left##2", &rectLeft))
                                    config->FGRectLeft = rectLeft;

                                ImGui::SameLine(0.0f, 16.0f);
                                int rectTop = config->FGRectTop.value_or(0);
                                if (ImGui::InputInt("Rect Top##2", &rectTop))
                                    config->FGRectTop = rectTop;

                                int rectWidth = config->FGRectWidth.value_or(0);
                                if (ImGui::InputInt("Rect Width##2", &rectWidth))
                                    config->FGRectWidth = rectWidth;

                                ImGui::SameLine(0.0f, 16.0f);
                                int rectHeight = config->FGRectHeight.value_or(0);
                                if (ImGui::InputInt("Rect Height##2", &rectHeight))
                                    config->FGRectHeight = rectHeight;

                                ImGui::PopItemWidth();
                                ShowHelpMarker("Frame generation rectangle, adjust for letterboxed content##2");

                                ImGui::BeginDisabled(
                                    !config->FGRectLeft.has_value() && !config->FGRectTop.has_value() &&
                                    !config->FGRectWidth.has_value() && !config->FGRectHeight.has_value());

                                if (ImGui::Button("Reset FG Rect##2"))
                                {
                                    config->FGRectLeft.reset();
                                    config->FGRectTop.reset();
                                    config->FGRectWidth.reset();
                                    config->FGRectHeight.reset();
                                }

                                ShowHelpMarker("Resets Frame generation rectangle##2");

                                ImGui::EndDisabled();
                                ImGui::TreePop();
                            }

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }
                }

                // DLSSG controls
                if ((state.activeFgOutput == FGOutput::DLSSG || state.activeFgOutput == FGOutput::DLSSGWithNvngx) &&
                    state.activeFgInput != FGInput::NoFG && state.currentFGSwapchain != nullptr)
                {
                    if (StreamlineProxy::LoadStreamline() && currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        ImGui::SeparatorText("Frame Generation (DLSSG)");

                        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);

                        bool fgActive = config->FGEnabled.value_or_default();
                        if (ImGui::Checkbox("Active##4", &fgActive))
                        {
                            config->FGEnabled = fgActive;
                            LOG_DEBUG("Enabled set FGEnabled: {}", fgActive);

                            if (config->FGEnabled.value_or_default())
                                state.FGchanged = true;
                        }

                        ShowHelpMarker("Enable Frame Generation");

                        auto maxInterpolationCount = state.dlssgMaxInterpolationCount;

                        if (maxInterpolationCount > 1)
                        {
                            ImGui::SameLine(0.0f, 16.0f);

                            const char* intModes[] = { "2X", "3X", "4X", "5X", "6X" };
                            auto currentSet = config->FGDLSSGInterpolationCount.value_or_default() - 1;
                            auto currentIntCount = intModes[currentSet];

                            ImGui::PushItemWidth(95.0f * menuResScale);

                            if (ImGui::BeginCombo("MFG", currentIntCount))
                            {
                                for (int i = 0; i < maxInterpolationCount; i++)
                                {
                                    if (ImGui::Selectable(intModes[i], (currentSet == i)))
                                    {
                                        LOG_DEBUG("DLSSG Interpolation Count set to: {}", i + 1);
                                        config->FGDLSSGInterpolationCount = i + 1;
                                    }
                                }

                                ImGui::EndCombo();
                            }

                            ImGui::PopItemWidth();

                            ShowHelpMarker("Set DLSSG interpolation count");
                        }

                        bool useGamesMarkers = config->FGDLSSGUseGamesReflexMarkers.value_or_default();
                        ImGui::BeginDisabled(!ReflexHooks::gameIsSendingMarkers());
                        if (ImGui::Checkbox("Use Game's Reflex Markers", &useGamesMarkers))
                        {
                            config->FGDLSSGUseGamesReflexMarkers = useGamesMarkers;
                            LOG_DEBUG("Changed set FGDLSSGUseGamesReflexMarkers: {}", useGamesMarkers);
                        }
                        ImGui::EndDisabled();
                    }
                }

                // OptiFG
                if (state.api == DX12 && state.currentFGSwapchain != nullptr &&
                    state.activeFgInput == FGInput::Upscaler)
                {
                    SeparatorWithHelpMarker("Frame Generation (OptiFG)", "Using upscaler data for FG");

                    if (currentFeature != nullptr && !currentFeature->IsFrozen() &&
                        ((state.activeFgOutput == FGOutput::FSRFG && FfxApiProxy::IsFGReady()) ||
                         (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() != nullptr) ||
                         ((state.activeFgOutput == FGOutput::DLSSG ||
                           state.activeFgOutput == FGOutput::DLSSGWithNvngx) &&
                          StreamlineProxy::Module() != nullptr)))
                    {
                        if (!Config::Instance()->FGDisableHUDFix.value_or_default())
                        {
                            bool fgHudfix = config->FGHUDFix.value_or_default();
                            bool disableHudfix = static_cast<bool>(state.gameQuirks & GameQuirk::DisableHudfix);

                            ImGui::BeginDisabled(disableHudfix);
                            if (ImGui::Checkbox("HUDFix", &fgHudfix))
                            {
                                config->FGHUDFix = fgHudfix;
                                LOG_DEBUG("Enabled set FGHUDFix: {}", fgHudfix);
                                state.ClearCapturedHudlesses = true;
                                state.FGchanged = true;
                            }
                            ImGui::EndDisabled();

                            if (disableHudfix)
                                ShowHelpMarker("HUDfix disabled due to known issues");
                            else
                                ShowHelpMarker("Enable HUD stability fix, might cause crashes!");

                            ImGui::BeginDisabled(!config->FGHUDFix.value_or_default());

                            ImGui::SameLine(0.0f, 16.0f);
                            ImGui::PushItemWidth(95.0f * menuResScale);
                            int hudFixLimit = config->FGHUDLimit.value_or_default();
                            if (ImGui::InputInt("Limit", &hudFixLimit))
                            {
                                if (hudFixLimit < 1)
                                    hudFixLimit = 1;
                                else if (hudFixLimit > 999)
                                    hudFixLimit = 999;

                                config->FGHUDLimit = hudFixLimit;
                                LOG_DEBUG("Enabled set FGHUDLimit: {}", hudFixLimit);
                            }
                            ShowHelpMarker("Delay HUDless capture, high values might cause crash!");

                            ImGui::SameLine(0.0f, 16.0f);
                            if (ImGui::Button("Res##2"))
                                _showHudlessWindow = !_showHudlessWindow;

                            ImGui::EndDisabled();

                            auto hudExtended = config->FGHUDFixExtended.value_or_default();
                            if (ImGui::Checkbox("Extended", &hudExtended))
                            {
                                LOG_DEBUG("Enabled set FGHUDFixExtended: {}", hudExtended);
                                config->FGHUDFixExtended = hudExtended;
                            }
                            ShowHelpMarker(
                                "Extended format checks for possible Hudless\nMight cause crashes and slowdowns!");
                            ImGui::SameLine(0.0f, 16.0f);

                            ImGui::BeginDisabled(!config->FGHUDFix.value_or_default());

                            auto immediate = config->FGImmediateCapture.value_or_default();
                            if (ImGui::Checkbox("Immediate Capture", &immediate))
                            {
                                LOG_DEBUG("Enabled set FGImmediateCapture: {}", immediate);
                                config->FGImmediateCapture = immediate;
                            }
                            ShowHelpMarker("Enables capturing of resources before shader execution.\nIncrease Hudless "
                                           "capture chances, but might cause capturing of unnecessary resources.");

                            ImGui::PopItemWidth();

                            ImGui::EndDisabled();
                        }
                        bool depthScale = config->FGEnableDepthScale.value_or_default();
                        if (ImGui::Checkbox("Scale Depth to fix DLSS RR", &depthScale))
                            config->FGEnableDepthScale = depthScale;
                        ShowHelpMarker("Fix for DLSS-D wrong depth inputs");

                        bool resourceFlip = config->FGResourceFlip.value_or_default();
                        if (ImGui::Checkbox("Flip (Unity)", &resourceFlip))
                            config->FGResourceFlip = resourceFlip;
                        ShowHelpMarker("Flip Velocity & Depth resources of Unity games");

                        ImGui::SameLine(0.0f, 16.0f);

                        bool resourceFlipOffset = config->FGResourceFlipOffset.value_or_default();
                        if (ImGui::Checkbox("Flip Use Offset", &resourceFlipOffset))
                            config->FGResourceFlipOffset = resourceFlipOffset;
                        ShowHelpMarker("Use height difference as offset");

                        ImGui::Spacing();

                        if (auto ch = ScopedCollapsingHeader("Advanced OptiFG Settings"); ch.IsHeaderOpen())
                        {
                            ScopedIndent indent {};

                            if (!Config::Instance()->FGDisableHUDFix.value_or_default())
                            {
                                ImGui::Spacing();

                                auto rb = config->FGResourceBlocking.value_or_default();
                                if (ImGui::Checkbox("Resource Blocking", &rb))
                                {
                                    config->FGResourceBlocking = rb;
                                    LOG_DEBUG("Enabled set FGResourceBlocking: {}", rb);
                                }
                                ShowHelpMarker("Block rarely used resources from using as Hudless \n"
                                               "to prevent flickers and other issues\n\n"
                                               "HUDfix enable/disable will reset the block list!");

                                ImGui::SameLine(0.0f, 16.0f);

                                auto rrc = config->FGRelaxedResolutionCheck.value_or_default();
                                if (ImGui::Checkbox("Relaxed Resource Check", &rrc))
                                {
                                    config->FGRelaxedResolutionCheck = rrc;
                                    LOG_DEBUG("Enabled set FGRelaxedResolutionCheck: {}", rrc);
                                }
                                ShowHelpMarker("Relax resolution checks for Hudless by 32 pixels \n"
                                               "Helps games which use black borders for some \n"
                                               "resolutions and screen ratios (e.g. Witcher 3)");

                                ImGui::BeginDisabled(state.FGresetCapturedResources);
                                ImGui::PushItemWidth(95.0f * menuResScale);
                                if (ImGui::Checkbox("FG Create List", &state.FGcaptureResources))
                                {
                                    if (!state.FGcaptureResources)
                                        config->FGHUDLimit = 1;
                                    else
                                        state.FGonlyUseCapturedResources = false;
                                }

                                ImGui::SameLine(0.0f, 16.0f);
                                if (ImGui::Checkbox("FG Use List", &state.FGonlyUseCapturedResources))
                                {
                                    if (state.FGcaptureResources)
                                    {
                                        state.FGcaptureResources = false;
                                        config->FGHUDLimit = 1;
                                    }
                                }

                                ImGui::SameLine(0.0f, 8.0f);
                                ImGui::Text("(%d)", state.FGcapturedResourceCount);

                                ImGui::PopItemWidth();

                                ImGui::SameLine(0.0f, 16.0f);

                                if (ImGui::Button("Reset List"))
                                {
                                    state.FGresetCapturedResources = true;
                                    state.FGonlyUseCapturedResources = false;
                                    state.FGonlyUseCapturedResources = false;
                                }

                                ImGui::EndDisabled();

                                ImGui::Spacing();
                                ImGui::Spacing();
                                if (ImGui::TreeNode("Tracking Settings"))
                                {
                                    auto ath = config->FGAlwaysTrackHeaps.value_or_default();
                                    if (ImGui::Checkbox("Always Track Heaps", &ath))
                                    {
                                        config->FGAlwaysTrackHeaps = ath;
                                        LOG_DEBUG("Enabled set FGAlwaysTrackHeaps: {}", ath);
                                    }
                                    ShowHelpMarker(
                                        "Always track resources, might cause performance issues\n, but also might "
                                        "fix HUDFix related crashes!");

                                    auto disableRTV = config->FGHudfixDisableRTV.value_or_default();
                                    if (ImGui::Checkbox("Disable RTV Tracking", &disableRTV))
                                        config->FGHudfixDisableRTV = disableRTV;
                                    ShowHelpMarker("Disable tracking of CreateRenderTargetView\n"
                                                   "This might help filtering of wrong hudless resources");

                                    ImGui::SameLine(0.0f, 16.0f);

                                    auto disableSRV = config->FGHudfixDisableSRV.value_or_default();
                                    if (ImGui::Checkbox("Disable SRV Tracking", &disableSRV))
                                        config->FGHudfixDisableSRV = disableSRV;
                                    ShowHelpMarker("Disable tracking of CreateShaderResourceView\n"
                                                   "This might help filtering of wrong Hudless resources");

                                    auto disableUAV = config->FGHudfixDisableUAV.value_or_default();
                                    if (ImGui::Checkbox("Disable UAV Tracking", &disableUAV))
                                        config->FGHudfixDisableUAV = disableUAV;
                                    ShowHelpMarker("Disable tracking of CreateUnorderedAccessView\n"
                                                   "This might help filtering of wrong Hudless resources");

                                    ImGui::SameLine(0.0f, 16.0f);

                                    auto disableOM = config->FGHudfixDisableOM.value_or_default();
                                    if (ImGui::Checkbox("Disable OM Tracking", &disableOM))
                                        config->FGHudfixDisableOM = disableOM;
                                    ShowHelpMarker("Disable tracking of OMSetRenderTargets\n"
                                                   "This might help filtering of wrong Hudless resources");

                                    auto disableSCR = config->FGHudfixDisableSCR.value_or_default();
                                    if (ImGui::Checkbox("Disable SCR Tracking", &disableSCR))
                                        config->FGHudfixDisableSCR = disableSCR;
                                    ShowHelpMarker("Disable tracking of SetComputeRootDescriptorTable\n"
                                                   "This might help filtering of wrong Hudless resources");

                                    ImGui::SameLine(0.0f, 16.0f);

                                    auto disableSGR = config->FGHudfixDisableSGR.value_or_default();
                                    if (ImGui::Checkbox("Disable SGR Tracking", &disableSGR))
                                        config->FGHudfixDisableSGR = disableSGR;
                                    ShowHelpMarker("Disable tracking of SetGraphicsRootDescriptorTable\n"
                                                   "This might help filtering of wrong Hudless resources");

                                    ImGui::Spacing();

                                    auto disableDI = config->FGHudfixDisableDI.value_or_default();
                                    if (ImGui::Checkbox("Disable DI Tracking", &disableDI))
                                        config->FGHudfixDisableDI = disableDI;
                                    ShowHelpMarker("Disable tracking of DrawInstanced\n"
                                                   "This might help filtering of wrong Hudless resources");

                                    ImGui::SameLine(0.0f, 16.0f);

                                    auto disableDII = config->FGHudfixDisableDII.value_or_default();
                                    if (ImGui::Checkbox("Disable DII Tracking", &disableDII))
                                        config->FGHudfixDisableDII = disableDII;
                                    ShowHelpMarker("Disable tracking of DrawIndexedInstanced\n"
                                                   "This might help filtering of wrong Hudless resources");

                                    auto disableDispatch = config->FGHudfixDisableDispatch.value_or_default();
                                    if (ImGui::Checkbox("Disable Dispatch Tracking", &disableDispatch))
                                        config->FGHudfixDisableDispatch = disableDispatch;
                                    ShowHelpMarker("Disable tracking of Dispatch\n"
                                                   "This might help filtering of wrong Hudless resources");

                                    ImGui::TreePop();
                                }
                            }

                            ImGui::Spacing();
                            if (ImGui::TreeNode("Resource Settings"))
                            {
                                bool makeMVCopies = config->FGMakeMVCopy.value_or_default();
                                if (ImGui::Checkbox("FG Make MV Copies", &makeMVCopies))
                                    config->FGMakeMVCopy = makeMVCopies;
                                ShowHelpMarker("Make a copy of motion vectors to use with OptiFG\n"
                                               "For preventing corruptions that might happen");

                                bool makeDepthCopies = config->FGMakeDepthCopy.value_or_default();
                                if (ImGui::Checkbox("FG Make Depth Copies", &makeDepthCopies))
                                    config->FGMakeDepthCopy = makeDepthCopies;
                                ShowHelpMarker("Make a copy of depth to use with OptiFG\n"
                                               "For preventing corruptions that might happen");

                                ImGui::PushItemWidth(115.0f * menuResScale);
                                float depthScaleMax = config->FGDepthScaleMax.value_or_default();
                                if (ImGui::InputFloat("FG Scale Depth Max", &depthScaleMax, 10.0f, 100.0f, "%.1f"))
                                    config->FGDepthScaleMax = depthScaleMax;
                                ShowHelpMarker("Depth values will be divided to this value");
                                ImGui::PopItemWidth();

                                ImGui::TreePop();
                            }

                            ImGui::Spacing();
                            if (ImGui::TreeNode("Syncing Settings"))
                            {
                                bool useMutexForPresent = config->FGUseMutexForSwapchain.value_or_default();
                                if (ImGui::Checkbox("FG Use Mutex for Present", &useMutexForPresent))
                                    config->FGUseMutexForSwapchain = useMutexForPresent;
                                ShowHelpMarker("Use mutex to prevent desync of FG and crashes\n"
                                               "Disabling might improve the perf but decrease stability");

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
                    else if (state.activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::IsFGReady())
                    {
                        ImGui::TextColored({ 1.0f, 0.0f, 0.0f, 1.0f },
                                           "amd_fidelityfx_dx12.dll is missing!"); // Probably never will be visible
                    }
                    else if (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() == nullptr)
                    {
                        ImGui::TextColored({ 1.0f, 0.0f, 0.0f, 1.0f },
                                           "libxess_fg.dll is missing!"); // Probably never will be visible
                    }
                }

                // Nukems Mod
                if (state.activeFgInput == FGInput::NvngxFG && state.activeFgOutput == FGOutput::NvngxFG)
                {
                    if (Nvngx_FG::isMFG())
                    {
                        SeparatorWithHelpMarker("Frame Generation (FSR3-MFG via DLSS Enabler)",
                                                "DLSS Enabler as dlss-enabler-headless.asi\n"
                                                "Select DLSS-FG in-game");
                    }
                    else
                    {
                        SeparatorWithHelpMarker("Frame Generation (FSR3-FG via Nukem's DLSSG)",
                                                "Requires Nukem's dlssg_to_fsr3 dll\n"
                                                "Select DLSS-FG in-game");
                    }

                    if (!state.NvngxFgFilesAvailable)
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Please put dlssg_to_fsr3_amd_is_better.dll or "
                                                                       "dlss-enabler-headless.asi next to OptiScaler");
                    }

                    if (Nvngx_FG::isMFG())
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f), "Using a subset of features from DLSS Enabler");
                    }

                    bool dmfgActive = state.dlssgDMFGSupported && config->FGDLSSGOverrideForceDMFG.value_or_default();

                    if (!ReflexHooks::isReflexHooked())
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Reflex not hooked");
                        ImGui::Text("If you are using an AMD/Intel GPU, then make sure you have Fakenvapi");
                    }
                    else if (ReflexHooks::dlssgFrameCountToGenerate() == 0 && !dmfgActive)
                    {
                        ImGui::Text("Please select DLSS Frame Generation in the game options\n"
                                    "You might need to select DLSS first");
                    }

                    if (state.swapchainApi == DX12)
                    {
                        ImGui::Text("Current DLSSG state:");
                        ImGui::SameLine();
                        if (auto count = state.dlssgDetectedInterpolationCount; count > 0)
                        {
                            ImGui::TextColored(ImVec4(0.f, 1.f, 0.25f, 1.f), std::format("ON {}x", count + 1).c_str());
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "OFF");
                        }

                        // Issue mostly shows up on AMD on Windows on pre-RDNA3 in some non-UE games
                        // Hide to reduce confusion, config is still read
                        bool isUnrealEngine = State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
                                              State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine;
                        if (!primaryGpu.dlssCapable && !primaryGpu.fsr4Capable && !primaryGpu.usesVkd3dProton &&
                            !isUnrealEngine)
                        {
                            if (bool makeDepthCopy = config->MakeDepthCopy.value_or_default();
                                ImGui::Checkbox("Fix broken visuals", &makeDepthCopy))
                            {
                                config->MakeDepthCopy = makeDepthCopy;
                            }
                            ShowHelpMarker(
                                "Makes a copy of the depth buffer\nCan fix broken visuals in some games on AMD "
                                "GPUs under Windows\nCan cause stutters, so best to use only when necessary");
                        }
                    }
                    else if (state.swapchainApi == Vulkan)
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f),
                                           "DLSSG is purposefully disabled when this menu is visible");
                        ImGui::Spacing();
                    }

                    if (Nvngx_FG::isLoaded() && !Nvngx_FG::isMFG())
                    {
                        if (Nvngx_FG::is120orNewer())
                        {
                            if (ImGui::Checkbox("Enable Debug View", &state.DLSSGDebugView))
                            {
                                Nvngx_FG::setDebugView(state.DLSSGDebugView);
                            }
                            if (ImGui::Checkbox("Interpolated frames only", &state.DLSSGInterpolatedOnly))
                            {
                                Nvngx_FG::setInterpolatedOnly(state.DLSSGInterpolatedOnly);
                            }
                        }
                        else if (Nvngx_FG::FSRDebugView() != nullptr)
                        {
                            if (ImGui::Checkbox("Enable Debug View", &state.DLSSGDebugView))
                            {
                                Nvngx_FG::FSRDebugView()(state.DLSSGDebugView);
                            }
                        }
                    }
                }

                // FSR-FG Inputs
                if (state.currentFGSwapchain != nullptr &&
                    (state.activeFgInput == FGInput::FSRFG || state.activeFgInput == FGInput::FSRFG30))
                {
                    SeparatorWithHelpMarker("Frame Generation (FSR-FG Inputs)", "Select FSR-FG in-game");

                    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
                    if (fgOutput != nullptr)
                    {
                        ImGui::Text("Current FSR-FG state:");
                        ImGui::SameLine();
                        if (state.FSRFGInputActive)
                        {
                            if (fgOutput->IsActive())
                                ImGui::TextColored(ImVec4(0.f, 1.f, 0.25f, 1.f), "ON");
                            else
                                ImGui::TextColored(ImVec4(1.0f, 0.647f, 0.0f, 1.f), "ACTIVATE FG");
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "OFF");
                            ImGui::Text("Please select FSR Frame Generation in the game options\n"
                                        "You might need to select FSR first");
                        }
                    }

                    bool skipConfig = config->FSRFGSkipConfigForHudless.value_or_default();
                    if (ImGui::Checkbox("Skip Config for Hudless", &skipConfig))
                        config->FSRFGSkipConfigForHudless = skipConfig;

                    ShowHelpMarker("Do not use Hudless set at ffxConfig");

                    ImGui::SameLine(0.0f, 6.0f);

                    bool skipDispatch = config->FSRFGSkipDispatchForHudless.value_or_default();
                    if (ImGui::Checkbox("Skip Dispatch for Hudless", &skipDispatch))
                        config->FSRFGSkipDispatchForHudless = skipDispatch;

                    ShowHelpMarker("Do not use Hudless set at ffxDispatch");
                }

                // Streamline FG Inputs
                if (state.currentFGSwapchain != nullptr && state.activeFgInput == FGInput::DLSSG)
                {
                    SeparatorWithHelpMarker("Frame Generation (Streamline FG Inputs)", "Select DLSS-FG in-game");

                    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);

                    if (!ReflexHooks::isReflexHooked())
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "Reflex not hooked");
                        ImGui::Text("If you are using an AMD/Intel GPU then make sure you have fakenvapi");
                    }
                    else if (fgOutput != nullptr)
                    {
                        ImGui::Text("Current Streamline FG state:");
                        ImGui::SameLine();
                        if ((state.FGLastFrame - state.DLSSGLastFrame) < 3)
                        {
                            if (fgOutput->IsActive())
                                ImGui::TextColored(ImVec4(0.f, 1.f, 0.25f, 1.f), "ON");
                            else
                                ImGui::TextColored(ImVec4(1.0f, 0.647f, 0.0f, 1.f), "ACTIVATE FG");
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
                        (state.activeFgOutput == FGOutput::FSRFG || IsFsr(currentBackend)))
                    {
                        SeparatorWithHelpMarker("FSR Common Settings", "Affects both FSR-FG & Upscalers");

                        bool useFsrVales = config->FsrUseFsrInputValues.value_or_default();
                        if (ImGui::Checkbox("Use FSR Input Values", &useFsrVales))
                            config->FsrUseFsrInputValues = useFsrVales;

                        ImGui::Spacing();
                        if (auto ch = ScopedCollapsingHeader("FoV & Camera Values"); ch.IsHeaderOpen())
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            bool useVFov = config->FsrVerticalFov.has_value() || !config->FsrHorizontalFov.has_value();

                            float vfov = config->FsrVerticalFov.value_or_default();
                            float hfov = config->FsrHorizontalFov.value_or(90.0f);

                            if (useVFov && !config->FsrVerticalFov.has_value())
                                config->FsrVerticalFov = vfov;
                            else if (!useVFov && !config->FsrHorizontalFov.has_value())
                                config->FsrHorizontalFov = hfov;

                            if (ImGui::RadioButton("Use Vert. Fov", useVFov))
                            {
                                config->FsrHorizontalFov.reset();
                                config->FsrVerticalFov = vfov;
                                useVFov = true;
                            }

                            ImGui::SameLine(0.0f, 6.0f);

                            if (ImGui::RadioButton("Use Horz. Fov", !useVFov))
                            {
                                config->FsrVerticalFov.reset();
                                config->FsrHorizontalFov = hfov;
                                useVFov = false;
                            }

                            if (useVFov)
                            {
                                if (ImGui::SliderFloat("Vert. FOV", &vfov, 0.0f, 180.0f, "%.1f"))
                                    config->FsrVerticalFov = vfov;

                                ShowHelpMarker("Might help achieve better image quality");
                            }
                            else
                            {
                                if (ImGui::SliderFloat("Horz. FOV", &hfov, 0.0f, 180.0f, "%.1f"))
                                    config->FsrHorizontalFov = hfov;

                                ShowHelpMarker("Might help achieve better image quality");
                            }

                            float cameraNear;
                            float cameraFar;

                            cameraNear = config->FsrCameraNear.value_or_default();
                            cameraFar = config->FsrCameraFar.value_or_default();

                            if (ImGui::SliderFloat("Camera Near", &cameraNear, 0.1f, 500000.0f, "%.1f"))
                                config->FsrCameraNear = cameraNear;
                            ShowHelpMarker("Might help achieve better image quality\n"
                                           "And potentially less ghosting");

                            if (ImGui::SliderFloat("Camera Far", &cameraFar, 0.1f, 500000.0f, "%.1f"))
                                config->FsrCameraFar = cameraFar;
                            ShowHelpMarker("Might help achieve better image quality\n"
                                           "And potentially less ghosting");

                            if (ImGui::Button("Reset Camera Values"))
                            {
                                config->FsrVerticalFov.reset();
                                config->FsrHorizontalFov.reset();
                                config->FsrCameraNear.reset();
                                config->FsrCameraFar.reset();
                            }

                            ImGui::SameLine(0.0f, 6.0f);
                            ImGui::Text("Near: %.1f Far: %.1f",
                                        state.lastFsrCameraNear < 500000.0f ? state.lastFsrCameraNear : 500000.0f,
                                        state.lastFsrCameraFar < 500000.0f ? state.lastFsrCameraFar : 500000.0f);

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }
                    }
                }

                // Framerate ---------------------
                if (state.reflexLimitsFps || config->OverlayMenu.value_or_default())
                {
                    SeparatorWithHelpMarker(
                        "Framerate",
                        "Uses Reflex when possible\nOn AMD/Intel cards, you can use Fakenvapi to substitute Reflex");

                    static std::string currentMethod {};
                    LowLatencyMode fakenvapiMode = {};
                    if (state.reflexLimitsFps)
                    {
                        fakenvapiMode = fakenvapi::getCurrentMode();

                        if (fakenvapiMode == LowLatencyMode::AntiLag2)
                            currentMethod = "FSR Latency Reduction 2.0";
                        else if (fakenvapiMode == LowLatencyMode::LatencyFlex)
                            currentMethod = "LatencyFlex";
                        else if (fakenvapiMode == LowLatencyMode::XeLL)
                            currentMethod = "XeLL";
                        else if (fakenvapiMode == LowLatencyMode::AntiLagVk)
                            currentMethod = "Vulkan AntiLag";
                        else if (fakenvapiMode == LowLatencyMode::None)
                        {
                            if (fakenvapi::isUsingAsMainNvapi())
                                currentMethod = "None";
                            else
                                currentMethod = "Reflex";
                        }

                        if (state.rtssReflexInjection && fakenvapiMode == LowLatencyMode::AntiLag2 &&
                            config->FGOutput.value_or_default() == FGOutput::FSRFG)
                            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.f, 1.f),
                                               "Using RTSS Reflex injection with FSR Latency Reduction 2.0 and FSR "
                                               "FG might cause issues");
                    }
                    else
                    {
                        currentMethod = "Fallback";
                    }

                    if (state.rtssReflexInjection)
                        currentMethod.append(" (RTSS)");

                    const bool fakenvapiInactive =
                        (fakenvapi::isUsingAsMainNvapi() || fakenvapiMode == LowLatencyMode::XeLL) &&
                        !fakenvapi::isLowLatencyActive() && state.reflexLimitsFps;

                    if (fakenvapiInactive)
                        currentMethod.append(" (inactive)");

                    ImGui::Text("Current method: %s", currentMethod.c_str());

                    if (fakenvapiMode == LowLatencyMode::AntiLag2)
                        ShowHelpMarker("FSR Latency Reduction 2.0 is the new name for AntiLag 2\nDon't ask me why");

                    if (state.reflexShowWarning)
                    {
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f),
                                           "Using Reflex's limit with FSR FG has performance overhead");

                        ImGui::Spacing();
                    }

                    // set initial value
                    if (std::isinf(_limitFps))
                        _limitFps = config->FramerateLimit.value_or_default();

                    ImGui::SliderFloat("FPS Limit", &_limitFps, 0, 200, "%.0f");

                    if (ImGui::Button("Apply Limit"))
                    {
                        config->FramerateLimit = _limitFps;
                    }

                    ImGui::SameLine(0.0f, 16.0f);

                    if (ImGui::Button("Reset Limit"))
                    {
                        _limitFps = 0.0f;
                        config->FramerateLimit = _limitFps;
                    }

                    ImGui::Spacing();
                    if (auto ch = ScopedCollapsingHeader("VRR Frame Cap Calculator"); ch.IsHeaderOpen())
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();

                        ImGui::PushItemWidth(105.0f * menuResScale);
                        ImGui::InputInt("Refresh Rate", &refreshRate, 1, 1, ImGuiInputTextFlags_None);
                        ImGui::PopItemWidth();

                        float refreshRateF = static_cast<float>(refreshRate);
                        // it's fine to use with real reflex, we only care about antilag
                        auto fpsLimitTech = fakenvapi::getCurrentMode();
                        constexpr float margin = 0.3f; // in ms
                        float frameCap = std::round(10000.f / (1000.f / refreshRateF + margin)) / 10.f;

                        if (fpsLimitTech == LowLatencyMode::AntiLag2 || fpsLimitTech == LowLatencyMode::AntiLagVk)
                            frameCap = std::round(frameCap);

                        ImGui::Text("Calculated Cap: %.1f", frameCap);

                        ImGui::SameLine(0.0f, 16.0f);

                        if (ImGui::Button("Set as FPS Limit"))
                        {
                            _limitFps = frameCap;
                            config->FramerateLimit = _limitFps;
                        }
                    }
                }

                // FAKENVAPI ---------------------------
                if (fakenvapi::isUsingAsMainNvapi() ||
                    (state.activeFgOutput == FGOutput::XeFG && state.reflexLimitsFps))
                {
                    // Using state.reflexLimitsFps as a detection for Reflex being used on Nvidia

                    ImGui::SeparatorText("fakenvapi");

                    ImGui::BeginDisabled(state.activeFgOutput == FGOutput::XeFG ||
                                         state.activeFgInput == FGInput::ForceXeLL);
                    if (bool forceLFX = config->FN_ForceLatencyFlex.value_or_default();
                        ImGui::Checkbox("Force LatencyFlex", &forceLFX))
                    {
                        config->FN_ForceLatencyFlex = forceLFX;
                    }
                    ShowHelpMarker("FSR Latency Reduction 2.0 / XeLL is used when available, this setting lets you "
                                   "force LatencyFlex instead");
                    ImGui::EndDisabled();

                    bool forceXell = config->ForceXeLL.value_or_default();
                    static bool activeForceXeLL = forceXell;

                    if (fakenvapi::isUsingAsMainNvapi())
                    {
                        ImGui::SameLine(0.0f, 16.0f);

                        if (ImGui::Checkbox("Force XeLL", &forceXell))
                        {
                            config->ForceXeLL = forceXell;
                        }
                        ShowHelpMarker("Allows XeLL to work without FG on non-Intel cards.\n\nDisables FG "
                                       "options\n\nRequires a restart");
                    }

                    if (activeForceXeLL != forceXell)
                    {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.f, 0.f, 0.0f, 1.f), "Save INI and restart to apply the changes");
                        ImGui::Spacing();
                    }

                    // clang-format off
                    static const std::vector<MenuOption<LFXMode>> lfx_modes = {
                        { LFXMode::Conservative, "Conservative",
                            "The safest, but might not reduce latency well" },
                        { LFXMode::Aggressive, "Aggressive",
                            "Improves latency, but in some cases will lower FPS more than expected" },
                        { LFXMode::ReflexIDs, "Reflex ID",
                            "Best when can be used, some games are not compatible (i.e. cyberpunk) and will fallback to Aggressive" }
                    };

                    bool usingLFX = fakenvapi::getCurrentMode() == LowLatencyMode::LatencyFlex;

                    ImGui::BeginDisabled(!usingLFX);
                    PopulateCombo("LatencyFlex mode", config->FN_LatencyFlexMode, lfx_modes);
                    ImGui::EndDisabled();

                    static std::vector<MenuOption<ForceReflex>> reflex_modes = { { ForceReflex::InGame, "Follow in-game" },
                                                                            { ForceReflex::ForceDisable, "Force Disable" },
                                                                            { ForceReflex::ForceEnable, "Force Enable" } };

                    PopulateCombo("Force Reflex", config->FN_ForceReflex, reflex_modes);
                    // clang-format on
                }

                // NEXT COLUMN -----------------
                ImGui::TableNextColumn();

                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    // SHARPNESS -----------------------------
                    ImGui::SeparatorText("Sharpness");

                    if (bool overrideSharpness = config->OverrideSharpness.value_or_default();
                        ImGui::Checkbox("Override", &overrideSharpness))
                    {
                        config->OverrideSharpness = overrideSharpness;

                        if (currentBackend == Upscaler::DLSS && currentFeature->Version().major < 3)
                        {
                            state.newBackend = currentBackend;
                            MARK_ALL_BACKENDS_CHANGED();
                        }
                    }
                    ShowHelpMarker("Ignores the value sent by the game\n"
                                   "and uses the value set below");

                    ImGui::BeginDisabled(!config->OverrideSharpness.value_or_default());

                    float sharpness = config->Sharpness.value_or_default();
                    auto justRcasEnabled =
                        config->RcasEnabled.value_or(rcasEnabled) && !config->ContrastEnabled.value_or_default();
                    float sharpnessLimit = justRcasEnabled ? 1.3f : 1.0f;

                    if (ImGui::SliderFloat("Sharpness", &sharpness, 0.0f, sharpnessLimit))
                        config->Sharpness = sharpness;

                    ImGui::EndDisabled();

                    // RCAS
                    // if (state.api == DX12 || state.api == DX11)
                    {
                        // xess or dlss version >= 2.5.1
                        constexpr feature_version requiredDlssVersion = { 2, 5, 1 };
                        rcasEnabled =
                            (currentBackend == Upscaler::XeSS ||
                             (currentBackend == Upscaler::DLSS && currentFeature->Version() >= requiredDlssVersion));

                        if (bool rcas = config->RcasEnabled.value_or(rcasEnabled);
                            ImGui::Checkbox("Enable RCAS", &rcas))
                            config->RcasEnabled = rcas;
                        ShowHelpMarker("A sharpening filter\n"
                                       "By default uses a sharpening value provided by the game\n"
                                       "Select 'Override' under 'Sharpness' and adjust the slider to change it\n\n"
                                       "Some upscalers have their own sharpness filter, so RCAS is not always needed");

                        ImGui::BeginDisabled(!config->RcasEnabled.value_or(rcasEnabled));

                        ImGui::Spacing();
                        if (auto ch = ScopedCollapsingHeader("Depth Adaptive RCAS##2"); ch.IsHeaderOpen())
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            if (bool depthEnabled = config->RcasDepthEnabled.value_or_default();
                                ImGui::Checkbox("Depth Adaptive RCAS", &depthEnabled))
                                config->RcasDepthEnabled = depthEnabled;

                            ShowHelpMarker(
                                "Uses depth to keep nearby detail while reducing shimmer around geometry edges.");

                            ImGui::BeginDisabled(!config->RcasDepthEnabled.value_or_default());

                            float depthSharpness = config->RcasDepthSharpness.value_or_default();
                            if (ImGui::SliderFloat("Near Detail Boost", &depthSharpness, 0.0f, 1.0f, "%.2f"))
                                config->RcasDepthSharpness = depthSharpness;

                            float depthThreshold = config->RcasDepthThreshold.value_or_default();
                            if (ImGui::SliderFloat("Edge Sensitivity", &depthThreshold, 0.0001f, 0.1f, "%.4f",
                                                   ImGuiSliderFlags_Logarithmic))
                                config->RcasDepthThreshold = depthThreshold;

                            ShowHelpMarker("Higher Near Detail Boost sharpens close surfaces more.\n"
                                           "Lower Edge Sensitivity reduces more shimmer but also softens more.\n"
                                           "If the image gets too soft, raise Edge Sensitivity.");

                            ImGui::EndDisabled();

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }

                        if (bool contrastEnabled = config->ContrastEnabled.value_or_default();
                            ImGui::Checkbox("Contrast Enabled", &contrastEnabled))
                            config->ContrastEnabled = contrastEnabled;

                        ShowHelpMarker("Increases sharpness at high contrast areas.");

                        if (config->ContrastEnabled.value_or_default() && config->Sharpness.value_or_default() > 1.0f)
                            config->Sharpness = 1.0f;

                        ImGui::BeginDisabled(!config->ContrastEnabled.value_or_default());

                        float contrast = config->Contrast.value_or_default();
                        if (ImGui::SliderFloat("Contrast", &contrast, 0.0f, 2.0f, "%.2f"))
                            config->Contrast = contrast;

                        ShowHelpMarker("Higher values increases sharpness at high contrast areas.\n"
                                       "High values might cause graphical GLITCHES \n"
                                       "when used with high sharpness values !!!");

                        ImGui::EndDisabled();

                        ImGui::Spacing();
                        if (auto ch = ScopedCollapsingHeader("Motion Adaptive Sharpness##2"); ch.IsHeaderOpen())
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            if (bool overrideMotionSharpness = config->MotionSharpnessEnabled.value_or_default();
                                ImGui::Checkbox("Motion Adaptive Sharpness", &overrideMotionSharpness))
                                config->MotionSharpnessEnabled = overrideMotionSharpness;
                            ShowHelpMarker("Applies more sharpness to things in motion");

                            ImGui::BeginDisabled(!config->MotionSharpnessEnabled.value_or_default());

                            ImGui::SameLine(0.0f, 6.0f);

                            if (bool overrideMSDebug = config->MotionSharpnessDebug.value_or_default();
                                ImGui::Checkbox("MAS Debug", &overrideMSDebug))
                                config->MotionSharpnessDebug = overrideMSDebug;
                            ShowHelpMarker("Areas that are more red will have more sharpness applied\n"
                                           "Green areas will get reduced sharpness");

                            float motionSharpness = config->MotionSharpness.value_or_default();
                            ImGui::SliderFloat("MotionSharpness", &motionSharpness, -1.3f, 1.3f, "%.3f");
                            config->MotionSharpness = motionSharpness;

                            float motionThreshod = config->MotionThreshold.value_or_default();
                            ImGui::SliderFloat("MotionThreshod", &motionThreshod, 0.0f, 100.0f, "%.2f");
                            config->MotionThreshold = motionThreshod;

                            float motionScale = config->MotionScaleLimit.value_or_default();
                            ImGui::SliderFloat("MotionRange", &motionScale, 0.01f, 100.0f, "%.2f");
                            config->MotionScaleLimit = motionScale;

                            ImGui::EndDisabled();

                            ImGui::Spacing();
                            ImGui::Spacing();
                        }

                        ImGui::EndDisabled();
                    }

                    // UPSCALE RATIO OVERRIDE -----------------

                    auto minSliderLimit = config->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
                    auto maxSliderLimit = config->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;

                    ImGui::SeparatorText("Upscale Ratio Override");

                    if (bool upOverride = config->UpscaleRatioOverrideEnabled.value_or_default();
                        ImGui::Checkbox("Override all", &upOverride))
                    {
                        config->UpscaleRatioOverrideEnabled = upOverride;

                        if (upOverride)
                            config->QualityRatioOverrideEnabled = false;
                    }
                    ShowHelpMarker("Lets you override every upscaler preset\n"
                                   "with a value set below\n\n"
                                   "1.5x on a 1080p screen means internal resolution of 720p\n"
                                   "1080 / 1.5 = 720");

                    if (bool qOverride = config->QualityRatioOverrideEnabled.value_or_default();
                        ImGui::Checkbox("Override per quality preset", &qOverride))
                    {
                        config->QualityRatioOverrideEnabled = qOverride;

                        if (qOverride)
                            config->UpscaleRatioOverrideEnabled = false;
                    }

                    ShowHelpMarker("Lets you override each preset's ratio individually\n"
                                   "Note that not every game supports every quality preset\n\n"
                                   "1.5x on a 1080p screen means internal resolution of 720p\n"
                                   "1080 / 1.5 = 720");

                    if (config->UpscaleRatioOverrideEnabled.value_or_default())
                    {
                        float urOverride = config->UpscaleRatioOverrideValue.value_or_default();
                        ImGui::SliderFloat("All Ratios", &urOverride, minSliderLimit, maxSliderLimit, "%.3f");
                        config->UpscaleRatioOverrideValue = urOverride;
                    }

                    if (config->QualityRatioOverrideEnabled.value_or_default())
                    {
                        float qDlaa = config->QualityRatio_DLAA.value_or_default();
                        if (ImGui::SliderFloat("DLAA", &qDlaa, minSliderLimit, maxSliderLimit, "%.3f"))
                            config->QualityRatio_DLAA = qDlaa;

                        float qUq = config->QualityRatio_UltraQuality.value_or_default();
                        if (ImGui::SliderFloat("Ultra Quality", &qUq, minSliderLimit, maxSliderLimit, "%.3f"))
                            config->QualityRatio_UltraQuality = qUq;

                        float qQ = config->QualityRatio_Quality.value_or_default();
                        if (ImGui::SliderFloat("Quality", &qQ, minSliderLimit, maxSliderLimit, "%.3f"))
                            config->QualityRatio_Quality = qQ;

                        float qB = config->QualityRatio_Balanced.value_or_default();
                        if (ImGui::SliderFloat("Balanced", &qB, minSliderLimit, maxSliderLimit, "%.3f"))
                            config->QualityRatio_Balanced = qB;

                        float qP = config->QualityRatio_Performance.value_or_default();
                        if (ImGui::SliderFloat("Performance", &qP, minSliderLimit, maxSliderLimit, "%.3f"))
                            config->QualityRatio_Performance = qP;

                        float qUp = config->QualityRatio_UltraPerformance.value_or_default();
                        if (ImGui::SliderFloat("Ultra Performance", &qUp, minSliderLimit, maxSliderLimit, "%.3f"))
                            config->QualityRatio_UltraPerformance = qUp;
                    }

                    if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        // OUTPUT SCALING -----------------------------
                        // if (state.api == DX12 || state.api == DX11)
                        {
                            // if motion vectors are not display size
                            ImGui::BeginDisabled(!currentFeature->LowResMV() &&
                                                 currentFeature->RenderWidth() != currentFeature->DisplayWidth());

                            ImGui::SeparatorText("Output Scaling");

                            float defaultRatio = 1.5f;

                            if (_ssRatio == 0.0f)
                            {
                                _ssRatio = config->OutputScalingMultiplier.value_or(defaultRatio);
                                _ssEnabled = config->OutputScalingEnabled.value_or_default();
                                _ssDownsampler = config->OutputScalingDownscaler.value_or_default();
                            }

                            ImGui::BeginDisabled(
                                (currentBackend == Upscaler::XeSS || currentBackend == Upscaler::DLSS) &&
                                currentFeature->RenderWidth() > currentFeature->DisplayWidth());
                            ImGui::Checkbox("Enable", &_ssEnabled);
                            ImGui::EndDisabled();

                            ShowHelpMarker("Upscales the image internally to a higher output resolution\n"
                                           "then downscales it back to your display resolution\n\n"
                                           "Values <1.0 make the upscaler cheaper\n"
                                           "Values >1.0 make image sharper at the cost of performance\n\n"
                                           "If greyed out, please check Git Wiki - Unreal Engine tweaks\n\n"
                                           "Target res and total ratio at the bottom (max. total 3.0!)");

                            ImGui::SameLine(0.0f, 6.0f);

                            ImGui::BeginDisabled(!_ssEnabled);
                            {
                                ImGui::PushItemWidth(95.0f * menuResScale);

                                // clang-format off
                                std::vector<MenuOption<Scaler>> ds_options = {
                                    { Scaler::FSR1, "FSR1",
                                        "Default option.\nGood enough image quality and very fast." },
                                    { Scaler::Bicubic, "Bicubic",
                                        "Fastest traditional option.\nProduces a very soft/blurry image, but might be okay for downscaling." },
                                    { Scaler::CatmullRom, "Catmull-Rom",
                                        "Designed primarily for downscaling.\nRetains good contrast with minimal artefacts, but softer than Lanczos." },
                                    { Scaler::Lanczos2, "Lanczos2",
                                        "Lighter and faster than Lanczos3.\nLess prone to ringing artefacts, but slightly blurrier." },
                                    { Scaler::Lanczos3, "Lanczos3",
                                        "Heavier version of Lanczos2.\nOffers the sharpest image, but is the most prone to ringing." },
                                    { Scaler::Kaiser2, "Kaiser2",
                                        "Similar to Lanczos2.\nSmoother and less prone to artefacts than Lanczos, but slightly blurrier." },
                                    { Scaler::Kaiser3, "Kaiser3",
                                        "Similar to Lanczos3.\nFar less prone to artefacting than Lanczos3, but much heavier on the GPU." },
                                    { Scaler::Magic, "MAGIC",
                                        "Specialised to prevent artifacts.\nEliminates harsh halos for a natural look, but can appear slightly soft." }
                                };
                                // clang-format on

                                const bool isUpsampleRatio = _ssRatio < 1.0f;
                                const std::string disabledReason =
                                    "Only FSR1 and Bicubic are supported when Ratio is below 1.0.";

                                for (auto& opt : ds_options)
                                {
                                    if (isUpsampleRatio && opt.value > Scaler::Bicubic)
                                        opt.set_disabled(true, opt.tooltip + "\n\n" + disabledReason);
                                }

                                if (isUpsampleRatio && _ssDownsampler > Scaler::Bicubic)
                                    _ssDownsampler = Scaler::FSR1;

                                PopulateCombo("Downscaler", _ssDownsampler, ds_options);

                                ImGui::PopItemWidth();
                            }
                            ImGui::EndDisabled();

                            bool applyEnabled = _ssEnabled != config->OutputScalingEnabled.value_or_default() ||
                                                _ssRatio != config->OutputScalingMultiplier.value_or(defaultRatio) ||
                                                _ssDownsampler != config->OutputScalingDownscaler.value_or_default();

                            ImGui::BeginDisabled(!applyEnabled);
                            if (ImGui::Button("Apply Change"))
                            {
                                config->OutputScalingEnabled = _ssEnabled;
                                config->OutputScalingMultiplier = _ssRatio;

                                if (_ssRatio < 1.0f && _ssDownsampler > Scaler::Bicubic)
                                    _ssDownsampler = Scaler::FSR1;

                                config->OutputScalingDownscaler = _ssDownsampler;

                                if (currentFeature->Name() == "DLSSD")
                                    state.newBackend = Upscaler::DLSSD;
                                else
                                    state.newBackend = currentBackend;

                                MARK_ALL_BACKENDS_CHANGED();
                            }
                            ImGui::EndDisabled();

                            ImGui::BeginDisabled(!_ssEnabled ||
                                                 currentFeature->RenderWidth() > currentFeature->DisplayWidth());
                            ImGui::SliderFloat("Ratio", &_ssRatio, 0.5f, 3.0f, "%.2f");
                            ImGui::EndDisabled();

                            if (currentFeature != nullptr && !currentFeature->IsFrozen())
                            {
                                ImGui::Text("Output Scaling is %s, Target Res: %dx%d (%.2f)\nJitter Count: %d",
                                            config->OutputScalingEnabled.value_or_default() ? "ENABLED" : "DISABLED",
                                            (uint32_t) (currentFeature->DisplayWidth() * _ssRatio),
                                            (uint32_t) (currentFeature->DisplayHeight() * _ssRatio),
                                            ((float) currentFeature->DisplayWidth() * _ssRatio) /
                                                (float) currentFeature->RenderWidth(),
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
                        bool autoExposureDisabled = state.api == API::DX11 && currentBackend == Upscaler::XeSS;
                        ImGui::BeginDisabled(autoExposureDisabled);

                        if (bool autoExposure = currentFeature->AutoExposure();
                            ImGui::Checkbox("Auto Exposure", &autoExposure))
                        {
                            config->AutoExposure = autoExposure;
                            ReInitUpscaler();
                        }
                        ShowResetButton(&config->AutoExposure, "R");
                        ShowHelpMarker("Some Unreal Engine games need this\n\n"
                                       "Try using if colours flickering or\n"
                                       "objects have ghosting trails");

                        ImGui::EndDisabled();

                        ImGui::TableNextColumn();
                        auto accessToReactiveMask = currentFeature->AccessToReactiveMask();
                        ImGui::BeginDisabled(!accessToReactiveMask);

                        bool canUseReactiveMask = accessToReactiveMask && currentBackend != Upscaler::DLSS &&
                                                  (currentBackend != Upscaler::XeSS ||
                                                   currentFeature->Version() >= feature_version { 2, 0, 1 });

                        bool disableReactiveMask = config->DisableReactiveMask.value_or(!canUseReactiveMask);

                        if (ImGui::Checkbox("Disable Reactive Mask", &disableReactiveMask))
                        {
                            config->DisableReactiveMask = disableReactiveMask;

                            if (currentBackend == Upscaler::XeSS)
                            {
                                state.newBackend = currentBackend;
                                MARK_ALL_BACKENDS_CHANGED();
                            }
                        }

                        ImGui::EndDisabled();

                        if (accessToReactiveMask)
                            ShowHelpMarker("Allows the use of a Reactive mask\n"
                                           "Keep in mind that a Reactive mask sent to DLSS\n"
                                           "will not produce a good image in combination with FSR/XeSS");
                        else
                            ShowHelpMarker("Option disabled because the game doesn't provide a Reactive mask");

                        ImGui::EndTable();

                        ImGui::Spacing();
                        if (auto ch = ScopedCollapsingHeader("Advanced Init Flags"); ch.IsHeaderOpen())
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            if (ImGui::BeginTable("init2", 2, ImGuiTableFlags_SizingStretchProp))
                            {
                                ImGui::TableNextColumn();
                                if (bool depth = currentFeature->DepthInverted();
                                    ImGui::Checkbox("Depth Inverted", &depth))
                                {
                                    config->DepthInverted = depth;
                                    ReInitUpscaler();
                                }
                                ShowResetButton(&config->DepthInverted, "R##2");
                                ShowHelpMarker("You shouldn't need to change it");

                                ImGui::TableNextColumn();
                                if (bool hdr = currentFeature->IsHdr(); ImGui::Checkbox("HDR", &hdr))
                                {
                                    config->HDR = hdr;
                                    ReInitUpscaler();
                                }
                                ShowResetButton(&config->HDR, "R##1");
                                ShowHelpMarker("Might help with purple hue in some games");

                                ImGui::TableNextColumn();
                                if (bool mv = !currentFeature->LowResMV(); ImGui::Checkbox("Display Res. MV", &mv))
                                {
                                    config->DisplayResolution = mv;

                                    // Disable output scaling when
                                    // Display res MV is active
                                    if (mv)
                                    {
                                        config->OutputScalingEnabled = false;
                                        _ssEnabled = false;
                                    }

                                    ReInitUpscaler();
                                }
                                ShowResetButton(&config->DisplayResolution, "R##4");
                                ShowHelpMarker("Mostly a fix for Unreal Engine games\n"
                                               "Top left part of the screen will be blurry");

                                ImGui::TableNextColumn();

                                if (bool jitter = currentFeature->JitteredMV();
                                    ImGui::Checkbox("Jitter Cancellation", &jitter))
                                {
                                    config->JitterCancellation = jitter;
                                    ReInitUpscaler();
                                }
                                ShowResetButton(&config->JitterCancellation, "R##3");
                                ShowHelpMarker("Fix for games that send motion data with preapplied jitter");

                                ImGui::TableNextColumn();
                                ImGui::EndTable();
                            }

                            if (currentFeature->AccessToReactiveMask() && currentBackend != Upscaler::DLSS)
                            {
                                ImGui::BeginDisabled(
                                    config->DisableReactiveMask.value_or(currentBackend == Upscaler::XeSS));

                                bool binaryMask = state.api == Vulkan || currentBackend == Upscaler::XeSS;
                                auto defaultBias = binaryMask ? 0.0f : 0.45f;
                                auto maskBias = config->DlssReactiveMaskBias.value_or(defaultBias);

                                if (!binaryMask)
                                {
                                    if (ImGui::SliderFloat("React. Mask Bias", &maskBias, 0.0f, 0.9f, "%.2f"))
                                        config->DlssReactiveMaskBias = maskBias;

                                    ShowHelpMarker("Values above 0 activate usage of Reactive mask");
                                }
                                else
                                {
                                    bool useRM = maskBias > 0.0f;
                                    if (ImGui::Checkbox("Use Binary Reactive Mask", &useRM))
                                    {
                                        if (useRM)
                                            config->DlssReactiveMaskBias = 0.45f;
                                        else
                                            config->DlssReactiveMaskBias.reset();
                                    }
                                }

                                ImGui::EndDisabled();
                            }
                        }
                    }
                }

                // QUIRKS -----------------------------
                if (state.detectedQuirks.size() > 0)
                {
                    ImGui::Spacing();
                    if (auto ch = ScopedCollapsingHeader("Active Quirks"); ch.IsHeaderOpen())
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();

                        for (const auto& quirk : state.detectedQuirks)
                        {
                            ImGui::TextWrapped("%s", quirk.c_str());
                        }
                    }
                }

                // ADVANCED SETTINGS -----------------------------
                ImGui::Spacing();
                if (auto ch = ScopedCollapsingHeader("Advanced Settings"); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        bool extendedLimits = config->ExtendedLimits.value_or_default();
                        if (ImGui::Checkbox("Enable Extended Limits", &extendedLimits))
                            config->ExtendedLimits = extendedLimits;

                        ShowHelpMarker("Extended sliders limit for quality presets\n\n"
                                       "Using this option changes resolution detection logic\n"
                                       "and might cause issues and crashes!");
                    }

                    bool pcShaders = config->UsePrecompiledShaders.value_or_default();
                    if (ImGui::Checkbox("Use Precompiled Shaders", &pcShaders))
                    {
                        config->UsePrecompiledShaders = pcShaders;
                        state.newBackend = currentBackend;
                        MARK_ALL_BACKENDS_CHANGED();
                    }

                    // DRS
                    ImGui::SeparatorText("DRS (Dynamic Resolution Scaling)");
                    if (ImGui::BeginTable("drs", 2, ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableNextColumn();
                        if (bool drsMin = config->DrsMinOverrideEnabled.value_or_default();
                            ImGui::Checkbox("Override Minimum", &drsMin))
                            config->DrsMinOverrideEnabled = drsMin;
                        ShowHelpMarker("Fix for games ignoring official DRS limits");

                        ImGui::TableNextColumn();
                        if (bool drsMax = config->DrsMaxOverrideEnabled.value_or_default();
                            ImGui::Checkbox("Override Maximum", &drsMax))
                            config->DrsMaxOverrideEnabled = drsMax;
                        ShowHelpMarker("Fix for games ignoring official DRS limits");

                        ImGui::EndTable();
                    }

                    // Non-DLSS hotfixes -----------------------------
                    if (currentFeature != nullptr && !currentFeature->IsFrozen() && currentBackend != Upscaler::DLSS)
                    {
                        // BARRIERS -----------------------------
                        ImGui::Spacing();
                        if (auto ch = ScopedCollapsingHeader("Resource Barriers"); ch.IsHeaderOpen())
                        {
                            ScopedIndent indent {};
                            ImGui::Spacing();

                            AddResourceBarrier("Color", &config->ColorResourceBarrier);
                            AddResourceBarrier("Depth", &config->DepthResourceBarrier);
                            AddResourceBarrier("Motion", &config->MVResourceBarrier);
                            AddResourceBarrier("Exposure", &config->ExposureResourceBarrier);
                            AddResourceBarrier("Mask", &config->MaskResourceBarrier);
                            AddResourceBarrier("Output", &config->OutputResourceBarrier);
                        }

                        // HOTFIXES -----------------------------
                        if (state.api == DX12)
                        {
                            ImGui::Spacing();
                            if (auto ch = ScopedCollapsingHeader("Root Signatures"); ch.IsHeaderOpen())
                            {
                                ScopedIndent indent {};
                                ImGui::Spacing();

                                if (bool crs = config->RestoreComputeSignature.value_or_default();
                                    ImGui::Checkbox("Restore Compute Root Signature", &crs))
                                    config->RestoreComputeSignature = crs;

                                if (bool grs = config->RestoreGraphicSignature.value_or_default();
                                    ImGui::Checkbox("Restore Graphic Root Signature", &grs))
                                    config->RestoreGraphicSignature = grs;
                            }
                        }
                    }
                }

                // LOGGING -----------------------------
                ImGui::Spacing();
                if (auto ch = ScopedCollapsingHeader("Logging"); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (config->LogToConsole.value_or_default() || config->LogToFile.value_or_default() ||
                        config->LogToNGX.value_or_default())
                        spdlog::default_logger()->set_level(
                            (spdlog::level::level_enum) config->LogLevel.value_or_default());
                    else
                        spdlog::default_logger()->set_level(spdlog::level::off);

                    if (bool toFile = config->LogToFile.value_or_default(); ImGui::Checkbox("To File", &toFile))
                    {
                        config->LogToFile = toFile;
                        PrepareLogger();
                    }

                    ImGui::SameLine(0.0f, 6.0f);
                    if (bool toConsole = config->LogToConsole.value_or_default();
                        ImGui::Checkbox("To Console", &toConsole))
                    {
                        config->LogToConsole = toConsole;
                        PrepareLogger();
                    }

                    const char* logLevels[] = { "Trace", "Debug", "Information", "Warning", "Error" };
                    const char* selectedLevel = logLevels[config->LogLevel.value_or_default()];

                    if (ImGui::BeginCombo("Log Level", selectedLevel))
                    {
                        for (int n = 0; n < 5; n++)
                        {
                            if (ImGui::Selectable(logLevels[n], (config->LogLevel.value_or_default() == n)))
                            {
                                config->LogLevel = n;
                                spdlog::default_logger()->set_level(
                                    (spdlog::level::level_enum) config->LogLevel.value_or_default());
                            }
                        }

                        ImGui::EndCombo();
                    }
                }

                // FPS OVERLAY -----------------------------
                ImGui::Spacing();
                if (auto ch = ScopedCollapsingHeader("FPS Overlay"); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    bool fpsEnabled = config->ShowFps.value_or_default();
                    if (ImGui::Checkbox("FPS Overlay Enabled", &fpsEnabled))
                        config->ShowFps = fpsEnabled;

                    ImGui::SameLine(0.0f, 6.0f);

                    bool fpsHorizontal = config->FpsOverlayHorizontal.value_or_default();
                    if (ImGui::Checkbox("Horizontal", &fpsHorizontal))
                        config->FpsOverlayHorizontal = fpsHorizontal;

                    const char* fpsPosition[] = { "Top Left", "Top Right", "Bottom Left", "Bottom Right" };
                    const char* selectedPosition = fpsPosition[config->FpsOverlayPos.value_or_default()];

                    if (ImGui::BeginCombo("Overlay Position", selectedPosition))
                    {
                        for (int n = 0; n < 4; n++)
                        {
                            if (ImGui::Selectable(fpsPosition[n], (config->FpsOverlayPos.value_or_default() == n)))
                                config->FpsOverlayPos = n;
                        }

                        ImGui::EndCombo();
                    }

                    const char* fpsType[] = { "Just FPS", "Simple",       "Detailed",      "Detailed + Graph",
                                              "Full",     "Full + Graph", "Reflex timings" };
                    const char* selectedType = fpsType[config->FpsOverlayType.value_or_default()];

                    if (ImGui::BeginCombo("Overlay Type", selectedType))
                    {
                        for (int n = 0; n < std::size(fpsType); n++)
                        {
                            if (ImGui::Selectable(fpsType[n], (config->FpsOverlayType.value_or_default() == n)))
                                config->FpsOverlayType = (FpsOverlay) n;
                        }

                        ImGui::EndCombo();
                    }

                    float fpsAlpha = config->FpsOverlayAlpha.value_or_default();
                    if (ImGui::SliderFloat("Background Alpha", &fpsAlpha, 0.0f, 1.0f, "%.2f"))
                        config->FpsOverlayAlpha = fpsAlpha;

                    const char* options[] = { "Same as menu", "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1", "1.2",
                                              "1.3",          "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
                    int currentIndex = std::max(((int) (config->FpsScale.value_or(0.0f) * 10.0f)) - 4, 0);
                    float values[] = { 0.0f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f,
                                       1.3f, 1.4f, 1.5f, 1.6f, 1.7f, 1.8f, 1.9f, 2.0f };

                    if (ImGui::SliderInt("Scale", &currentIndex, 0, IM_ARRAYSIZE(options) - 1, options[currentIndex],
                                         ImGuiSliderFlags_ClampOnInput))
                    {
                        if (currentIndex == 0)
                            config->FpsScale.reset();
                        else
                            config->FpsScale = values[currentIndex];
                    }
                }

                // UPSCALER INPUTS -----------------------------
                ImGui::Spacing();
                auto uiStateOpen = currentFeature == nullptr || currentFeature->IsFrozen();
                if (auto ch =
                        ScopedCollapsingHeader("Upscaler Inputs", uiStateOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                    ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (config->EnableFsr2Inputs.value_or_default())
                    {
                        bool fsr2Inputs = config->UseFsr2Inputs.value_or_default();
                        bool fsr2Pattern = config->Fsr2Pattern.value_or_default();

                        if (ImGui::Checkbox("Use Fsr2 Inputs", &fsr2Inputs))
                            config->UseFsr2Inputs = fsr2Inputs;

                        if (ImGui::Checkbox("Use Fsr2 Pattern Matching", &fsr2Pattern))
                            config->Fsr2Pattern = fsr2Pattern;
                        ShowTooltip("This setting will become active on next boot!");
                    }

                    if (config->EnableFsr3Inputs.value_or_default())
                    {
                        bool fsr3Inputs = config->UseFsr3Inputs.value_or_default();
                        bool fsr3Pattern = config->Fsr3Pattern.value_or_default();

                        if (ImGui::Checkbox("Use Fsr3 Inputs", &fsr3Inputs))
                            config->UseFsr3Inputs = fsr3Inputs;

                        if (ImGui::Checkbox("Use Fsr3 Pattern Matching", &fsr3Pattern))
                            config->Fsr3Pattern = fsr3Pattern;
                        ShowTooltip("This setting will become active on next boot!");
                    }

                    if (config->EnableFfxInputs.value_or_default())
                    {
                        bool ffxInputs = config->UseFfxInputs.value_or_default();

                        if (ImGui::Checkbox("Use Ffx Inputs", &ffxInputs))
                            config->UseFfxInputs = ffxInputs;
                    }
                }

                // DX11 & DX12 -----------------------------
                if (state.swapchainApi != Vulkan)
                {
                    // V-SYNC -----------------------------
                    ImGui::Spacing();
                    if (auto ch = ScopedCollapsingHeader("V-Sync Settings"); ch.IsHeaderOpen())
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();

                        auto forceVsyncOn = config->ForceVsync.has_value() && config->ForceVsync.value();
                        auto forceVsyncOff = config->ForceVsync.has_value() && !config->ForceVsync.value();
                        bool vsyncChanged = false;

                        if (ImGui::Checkbox("V-Sync On", &forceVsyncOn))
                        {
                            if (forceVsyncOn)
                            {
                                config->ForceVsync = true;
                                vsyncChanged = true;
                            }
                            else
                            {
                                config->ForceVsync.reset();
                                vsyncChanged = true;
                            }
                        }
                        ImGui::SameLine(0.0f, 16.0f);

                        if (ImGui::Checkbox("V-Sync Off", &forceVsyncOff))
                        {
                            if (forceVsyncOff)
                            {
                                config->ForceVsync = false;
                                vsyncChanged = true;
                            }
                            else
                            {
                                config->ForceVsync.reset();
                                vsyncChanged = true;
                            }
                        }
                        ImGui::SameLine(0.0f, 16.0f);

                        ImGui::BeginDisabled(!forceVsyncOn);

                        ImGui::PushItemWidth(50.0f * menuResScale);

                        auto vsyncBuf = StrFmt("%d", config->VsyncInterval.value_or_default());
                        if (ImGui::BeginCombo("Sync Int.", vsyncBuf.c_str()))
                        {
                            if (ImGui::Selectable("0", config->VsyncInterval.value_or_default() == 0))
                            {
                                config->VsyncInterval = 0;
                                vsyncChanged = true;
                            }

                            if (ImGui::Selectable("1", config->VsyncInterval.value_or_default() == 1))
                            {
                                config->VsyncInterval = 1;
                                vsyncChanged = true;
                            }

                            if (ImGui::Selectable("2", config->VsyncInterval.value_or_default() == 2))
                            {
                                config->VsyncInterval = 2;
                                vsyncChanged = true;
                            }

                            if (ImGui::Selectable("3", config->VsyncInterval.value_or_default() == 3))
                            {
                                config->VsyncInterval = 3;
                                vsyncChanged = true;
                            }

                            ImGui::EndCombo();
                        }
                        ImGui::PopItemWidth();

                        ImGui::EndDisabled();
                        ImGui::SameLine(0.0f, 16.0f);

                        if (ImGui::Button("Reset##10"))
                        {
                            config->ForceVsync.reset();
                            vsyncChanged = true;
                        }

                        ShowHelpMarker("Force V-Sync On/Off & Sync Interval options");

                        if (vsyncChanged && state.activeFgOutput == FGOutput::XeFG && state.currentFG != nullptr)
                        {
                            // To prevent XeLL issues
                            LOG_DEBUG("V-Sync change detected, forcing XeFG reset");
                            state.WAR_xefgRequestFGToggle = true;
                        }
                    }

                    // MIPMAP BIAS & Anisotropy -----------------------------
                    ImGui::Spacing();
                    if (auto ch = ScopedCollapsingHeader("Mipmap Bias",
                                                         (currentFeature == nullptr || currentFeature->IsFrozen())
                                                             ? ImGuiTreeNodeFlags_DefaultOpen
                                                             : 0);
                        ch.IsHeaderOpen())
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();
                        if (config->MipmapBiasOverride.has_value() && _mipBias == 0.0f)
                            _mipBias = config->MipmapBiasOverride.value();

                        ImGui::SliderFloat("Mipmap Bias##2", &_mipBias, -15.0f, 15.0f, "%.6f");
                        ShowHelpMarker("Can help with blurry textures in broken games\n"
                                       "Negative values will make textures sharper\n"
                                       "Positive values will make textures more blurry\n\n"
                                       "Has a small performance impact");

                        ImGui::BeginDisabled(!config->MipmapBiasOverride.has_value());
                        {
                            ImGui::BeginDisabled(config->MipmapBiasScaleOverride.has_value() &&
                                                 config->MipmapBiasScaleOverride.value());
                            {
                                bool mbFixed = config->MipmapBiasFixedOverride.value_or_default();
                                if (ImGui::Checkbox("MB Fixed Override", &mbFixed))
                                {
                                    config->MipmapBiasScaleOverride.reset();
                                    config->MipmapBiasFixedOverride = mbFixed;
                                }

                                ShowHelpMarker("Apply same override value to all textures");
                            }
                            ImGui::EndDisabled();

                            ImGui::SameLine(0.0f, 6.0f);

                            ImGui::BeginDisabled(config->MipmapBiasFixedOverride.has_value() &&
                                                 config->MipmapBiasFixedOverride.value());
                            {
                                bool mbScale = config->MipmapBiasScaleOverride.value_or_default();
                                if (ImGui::Checkbox("MB Scale Override", &mbScale))
                                {
                                    config->MipmapBiasFixedOverride.reset();
                                    config->MipmapBiasScaleOverride = mbScale;
                                }

                                ShowHelpMarker("Apply override value as scale multiplier\n"
                                               "When using scale mode, please use positive\n"
                                               "override values to increase sharpness!");
                            }
                            ImGui::EndDisabled();

                            bool mbAll = config->MipmapBiasOverrideAll.value_or_default();
                            if (ImGui::Checkbox("MB Override All Textures", &mbAll))
                                config->MipmapBiasOverrideAll = mbAll;

                            ShowHelpMarker("Override all textures mipmap values\n"
                                           "Normally OptiScaler only overrides\n"
                                           "below zero mipmap values!");
                        }
                        ImGui::EndDisabled();

                        ImGui::BeginDisabled(config->MipmapBiasOverride.has_value() &&
                                             config->MipmapBiasOverride.value() == _mipBias);
                        {
                            if (ImGui::Button("Set"))
                            {
                                config->MipmapBiasOverride = _mipBias;
                                state.lastMipBias = 100.0f;
                                state.lastMipBiasMax = -100.0f;
                            }
                        }
                        ImGui::EndDisabled();

                        ImGui::SameLine(0.0f, 6.0f);

                        ImGui::BeginDisabled(!config->MipmapBiasOverride.has_value());
                        {
                            if (ImGui::Button("Reset"))
                            {
                                config->MipmapBiasOverride.reset();
                                _mipBias = 0.0f;
                                state.lastMipBias = 100.0f;
                                state.lastMipBiasMax = -100.0f;
                            }
                        }
                        ImGui::EndDisabled();

                        if (currentFeature != nullptr && !currentFeature->IsFrozen())
                        {
                            ImGui::SameLine(0.0f, 6.0f);

                            if (ImGui::Button("Calculate Mipmap Bias"))
                                _showMipmapCalcWindow = true;
                        }

                        if (config->MipmapBiasOverride.has_value())
                        {
                            if (config->MipmapBiasFixedOverride.value_or_default())
                            {
                                ImGui::Text("Current : %.3f / %.3f, Target: %.3f", state.lastMipBias,
                                            state.lastMipBiasMax, config->MipmapBiasOverride.value());
                            }
                            else if (config->MipmapBiasScaleOverride.value_or_default())
                            {
                                ImGui::Text("Current : %.3f / %.3f, Target: Base * %.3f", state.lastMipBias,
                                            state.lastMipBiasMax, config->MipmapBiasOverride.value());
                            }
                            else
                            {
                                ImGui::Text("Current : %.3f / %.3f, Target: Base + %.3f", state.lastMipBias,
                                            state.lastMipBiasMax, config->MipmapBiasOverride.value());
                            }
                        }
                        else
                        {
                            ImGui::Text("Current : %.3f / %.3f", state.lastMipBias, state.lastMipBiasMax);
                        }

                        ImGui::Text("Will be applied after RESOLUTION/PRESET change !!!");
                    }

                    ImGui::Spacing();
                    if (auto ch = ScopedCollapsingHeader("Anisotropic Filtering",
                                                         (currentFeature == nullptr || currentFeature->IsFrozen())
                                                             ? ImGuiTreeNodeFlags_DefaultOpen
                                                             : 0);
                        ch.IsHeaderOpen())
                    {
                        ScopedIndent indent {};
                        ImGui::Spacing();
                        ImGui::PushItemWidth(65.0f * menuResScale);

                        auto selectedAF = config->AnisotropyOverride.has_value()
                                              ? std::to_string(config->AnisotropyOverride.value())
                                              : "Auto";
                        if (ImGui::BeginCombo("Force Anisotropic Filtering", selectedAF.c_str()))
                        {
                            if (ImGui::Selectable("Auto", !config->AnisotropyOverride.has_value()))
                                config->AnisotropyOverride.reset();

                            if (ImGui::Selectable("1", config->AnisotropyOverride.value_or(0) == 1))
                                config->AnisotropyOverride = 1;

                            if (ImGui::Selectable("2", config->AnisotropyOverride.value_or(0) == 2))
                                config->AnisotropyOverride = 2;

                            if (ImGui::Selectable("4", config->AnisotropyOverride.value_or(0) == 4))
                                config->AnisotropyOverride = 4;

                            if (ImGui::Selectable("8", config->AnisotropyOverride.value_or(0) == 8))
                                config->AnisotropyOverride = 8;

                            if (ImGui::Selectable("16", config->AnisotropyOverride.value_or(0) == 16))
                                config->AnisotropyOverride = 16;

                            ImGui::EndCombo();
                        }

                        ImGui::PopItemWidth();

                        bool afComp = config->AnisotropyModifyComp.value_or_default();
                        if (ImGui::Checkbox("Modify Compare", &afComp))
                            config->AnisotropyModifyComp = afComp;

                        ShowHelpMarker("Update comparison filters");

                        ImGui::SameLine(0.0f, 6.0f);

                        bool afMinMax = config->AnisotropyModifyMinMax.value_or_default();
                        if (ImGui::Checkbox("Modify Min/Max", &afMinMax))
                            config->AnisotropyModifyMinMax = afMinMax;

                        ShowHelpMarker("Update min/max filters");

                        bool afSkipPoint = config->AnisotropySkipPointFilter.value_or_default();
                        if (ImGui::Checkbox("Skip Point Filters", &afSkipPoint))
                            config->AnisotropySkipPointFilter = afSkipPoint;

                        ShowHelpMarker("Skip updating of point filters");

                        ImGui::Text("Will might be applied after RESOLUTION/PRESET change !!!");
                    }
                }

                ImGui::Spacing();
                if (auto ch = ScopedCollapsingHeader("Keybinds"); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    ImGui::Text("Key combinations are currently NOT supported!");
                    ImGui::Text("Escape to cancel, Backspace to unbind");
                    ImGui::Spacing();

                    static auto menu = Keybind("Menu", 10);
                    static auto fpsOverlay = Keybind("FPS Overlay", 11);
                    static auto fpsOverlayCycle = Keybind("FPS Overlay Cycle", 12);
                    static auto fgEnable = Keybind("Frame Generation", 13);

                    menu.Render(config->ShortcutKey);
                    fpsOverlay.Render(config->FpsShortcutKey);
                    fpsOverlayCycle.Render(config->FpsCycleShortcutKey);
                    fgEnable.Render(config->FGShortcutKey);
                }

                ImGui::EndTable();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::BeginTable("plots", 2, ImGuiTableFlags_SizingStretchSame))
                {
                    ImGui::TableNextColumn();
                    ImGui::Text("FrameTime");
                    auto ft = StrFmt("%7.2f ms / %6.1f fps", frameTime, frameRate);
                    ImGui::PlotLines(
                        ft.c_str(), [](void* rb, int idx) -> float
                        { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); }, &gFrameTimes, plotWidth);

                    if (currentFeature != nullptr && !currentFeature->IsFrozen())
                    {
                        ImGui::TableNextColumn();
                        ImGui::Text("Upscaler");
                        auto ups = StrFmt("%7.2f ms", state.upscaleTimes.back());
                        ImGui::PlotLines(
                            ups.c_str(), [](void* rb, int idx) -> float
                            { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); }, &gUpscalerTimes,
                            plotWidth);
                    }

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

                    ImGui::Text("%d", currentFeature->FrameCount());

                    ImGui::SameLine(0.0f, 10.0f);
                }

                ImGui::PushItemWidth(100.0f * menuResScale);

                auto autoText = config->MenuScale.has_value() ? "Auto" : StrFmt("Auto (%3.1f)", menuResScale);
                // clang-format off
                const char* uiScales[] = { autoText.c_str(), "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1",
                                           "1.2", "1.3", "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
                // clang-format on

                const char* selectedScaleName = uiScales[_selectedScale];

                if (ImGui::BeginCombo("Menu Scale", selectedScaleName))
                {
                    for (int n = 0; n < std::size(uiScales); n++)
                    {
                        if (ImGui::Selectable(uiScales[n], (_selectedScale == n)))
                        {
                            _selectedScale = n;

                            if (n == 0)
                                config->MenuScale.reset();
                            else
                                config->MenuScale = 0.4f + (float) n / 10.0f;
                        }
                    }

                    ImGui::EndCombo();
                }

                ImGui::PopItemWidth();

                ImGui::SameLine(0.0f, 15.0f);

                if (ImGui::Button("Save INI"))
                    config->SaveIni();

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
                    _showHudlessWindow = false;
                    io.MouseDrawCursor = false;
                    io.WantCaptureKeyboard = false;
                    io.WantCaptureMouse = false;
                }

                auto winSize = ImGui::GetWindowSize();
                auto winPos = ImGui::GetWindowPos();

                ImGui::SameLine();

                auto textSize = ImGui::CalcTextSize("Open Wiki (?)");
                textSize.x += ImGui::GetStyle().FramePadding.x * 3.0f;

                float avail = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - textSize.x);

                // Make button text underline
                if (ImGui::Button("Open Wiki"))
                {
                    auto pIO = &ImGui::GetPlatformIO();
                    auto ctx = ImGui::GetCurrentContext();
                    pIO->Platform_OpenInShellFn(ctx, "https://github.com/optiscaler/OptiScaler/wiki");
                }
                ShowHelpMarker("Click to open the OptiScaler Wiki\npage in your default browser");

                ImGui::Spacing();
                ImGui::Separator();

                if (state.nvngxIniDetected)
                {
                    ImGui::Spacing();
                    ImGui::TextColored(
                        ImVec4(1.f, 0.f, 0.f, 1.f),
                        "nvngx.ini detected, please move over to using OptiScaler.ini and delete the old config");
                    ImGui::Spacing();
                }

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

            // Metrics window (for debug)
            // ImGui::ShowMetricsWindow();

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
                    if (config->OutputScalingEnabled.value_or_default())
                    {
                        _displayWidth = static_cast<uint32_t>(currentFeature->DisplayWidth() *
                                                              config->OutputScalingMultiplier.value_or_default());
                    }
                    else
                    {
                        _displayWidth = currentFeature->DisplayWidth();
                    }

                    _renderWidth = static_cast<uint32_t>(_displayWidth / 3.0f);
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
                            if (config->OutputScalingEnabled.value_or_default())
                            {
                                _displayWidth =
                                    static_cast<uint32_t>(currentFeature->DisplayWidth() *
                                                          config->OutputScalingMultiplier.value_or_default());
                            }
                            else
                            {
                                _displayWidth = currentFeature->DisplayWidth();
                            }
                        }

                        _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                    }

                    const char* q[] = { "Ultra Performance", "Performance",   "Balanced",
                                        "Quality",           "Ultra Quality", "DLAA" };
                    float fr[] = { 3.0f, 2.0f, 1.7f, 1.5f, 1.3f, 1.0f };
                    auto configQ = _mipmapUpscalerQuality;

                    const char* selectedQ = q[configQ];

                    ImGui::BeginDisabled(config->UpscaleRatioOverrideEnabled.value_or_default());

                    if (ImGui::BeginCombo("Upscaler Quality", selectedQ))
                    {
                        for (int n = 0; n < 6; n++)
                        {
                            if (ImGui::Selectable(q[n], (_mipmapUpscalerQuality == n)))
                            {
                                _mipmapUpscalerQuality = n;

                                float ov = -1.0f;

                                if (config->QualityRatioOverrideEnabled.value_or_default())
                                {
                                    switch (n)
                                    {
                                    case 0:
                                        ov = config->QualityRatio_UltraPerformance.value_or(-1.0f);
                                        break;

                                    case 1:
                                        ov = config->QualityRatio_Performance.value_or(-1.0f);
                                        break;

                                    case 2:
                                        ov = config->QualityRatio_Balanced.value_or(-1.0f);
                                        break;

                                    case 3:
                                        ov = config->QualityRatio_Quality.value_or(-1.0f);
                                        break;

                                    case 4:
                                        ov = config->QualityRatio_UltraQuality.value_or(-1.0f);
                                        break;
                                    }
                                }

                                if (ov > 0.0f)
                                    _mipmapUpscalerRatio = ov;
                                else
                                    _mipmapUpscalerRatio = fr[n];

                                _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                            }
                        }

                        ImGui::EndCombo();
                    }

                    ImGui::EndDisabled();

                    auto minLimit = config->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
                    auto maxLimit = config->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;
                    if (ImGui::SliderFloat("Upscaler Ratio", &_mipmapUpscalerRatio, minLimit, maxLimit, "%.2f"))
                    {
                        _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                    }

                    if (ImGui::InputScalar("Render Width", ImGuiDataType_U32, &_renderWidth, NULL, NULL, "%u"))
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);

                    ImGui::SliderFloat("Mipmap Bias", &_mipBiasCalculated, -15.0f, 0.0f, "%.6f");

                    // BOTTOM LINE
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::SameLine();
                    ImGui::Spacing();

                    constexpr float spacing = 6.0f;
                    auto textSize = ImGui::CalcTextSize("Use Value");
                    textSize += ImGui::CalcTextSize("Close");
                    textSize.x += ImGui::GetStyle().FramePadding.x * 5.0f + spacing; // 2 sides * 2 buttons + 1

                    float avail = ImGui::GetContentRegionAvail().x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - textSize.x);

                    if (ImGui::Button("Use Value"))
                    {
                        _mipBias = _mipBiasCalculated;
                        _showMipmapCalcWindow = false;
                    }

                    ImGui::SameLine(0.0f, spacing);

                    if (ImGui::Button("Close"))
                        _showMipmapCalcWindow = false;

                    ImGui::Spacing();
                    ImGui::Separator();

                    ImGui::End();
                }
            }

            auto fg = state.currentFG;
            if (_showHudlessWindow && config->FGHUDFix.value_or_default() && fg != nullptr && fg->IsActive())
            {
                auto posX = (io.DisplaySize.x - 400.0f) / 2.0f;
                auto posY = (io.DisplaySize.y - 300.0f) / 2.0f;

                ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2 { 400.0f, 300.0f });

                if (ImGui::Begin("Hudless Resources", nullptr, flags))
                {
                    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                        ImGui::SetWindowFocus();

                    int btnCount = 100;

                    if (ImGui::BeginTable("HudlessTable", 2, ImGuiTableFlags_SizingFixedFit))
                    {
                        ImGui::TableSetupColumn("##1", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("##2", ImGuiTableColumnFlags_WidthFixed);

                        ankerl::unordered_dense::map<void*, CapturedHudlessInfo>::iterator it;

                        for (it = state.CapturedHudlesses.begin(); it != state.CapturedHudlesses.end(); it++)
                        {
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);

                            ImGui::Text("%08x, %s->%s, Count: %llu, %s", (size_t) it->first,
                                        GetSourceString(it->second.captureInfo & 0xFF).c_str(),
                                        GetDispatchString(it->second.captureInfo & 0xFF00).c_str(),
                                        it->second.usageCount, it->second.enabled ? "Active" : "Passive");

                            ImGui::TableSetColumnIndex(1);

                            btnCount++;
                            std::string text;

                            if (it->second.enabled)
                                text = StrFmt("Disable##%d", btnCount);
                            else
                                text = StrFmt("Enable##%d", btnCount);

                            if (ImGui::Button(text.c_str()))
                                it->second.enabled = !it->second.enabled;
                        }

                        ImGui::EndTable();
                    }

                    if (ImGui::Button("Clear##4"))
                        state.ClearCapturedHudlesses = true;

                    ImGui::SameLine(0.0f, 8.0f);

                    if (ImGui::Button("Close##4"))
                        _showHudlessWindow = false;

                    ImGui::End();
                }
            }
        }

        if (config->UseHQFont.value_or_default())
            ImGui::PopFontSize();
    }

    if (newFrame)
        ImGui::EndFrame();

    return newFrame;
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

    _handle = nullptr;
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
