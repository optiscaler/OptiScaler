#include "pch.h"
#include "input_system_internal.h"

#include <include/imgui/imgui.h>
#include <windowsx.h>

#include <vector>

namespace OptiInput
{
void SetKeyUpStateOnly(int vk, DWORD messageTime)
{
    if (vk < 0 || vk >= 256)
        return;

    ButtonState& key = _state.Keys[vk];

    if (key.Down)
        key.Released = true;

    key.Down = false;
    key.LastMessageTime = messageTime;

    // Do not clear key.BlockedDown here.
    // Polling/raw input are internal state producers. The game-facing
    // WM_KEYUP path must be the one that consumes BlockedDown.
    SyncAggregateModifierStateLocked();
}

void SetMouseUpStateOnly(int button, DWORD messageTime)
{
    if (button < 0 || button >= static_cast<int>(_state.MouseButtons.size()))
        return;

    ButtonState& mouseButton = _state.MouseButtons[button];

    if (mouseButton.Down)
        mouseButton.Released = true;

    mouseButton.Down = false;
    mouseButton.LastMessageTime = messageTime;

    // Do not clear mouseButton.BlockedDown here.
    // Polling/raw input are internal state producers. The game-facing
    // WM_*BUTTONUP path must be the one that consumes BlockedDown.
}

void ResetButtonBlockedStateLocked()
{
    for (ButtonState& key : _state.Keys)
        key.BlockedDown = false;

    for (ButtonState& mouseButton : _state.MouseButtons)
        mouseButton.BlockedDown = false;

    SyncAggregateModifierStateLocked();
}

void SetMouseDownFromRawState(int button, DWORD messageTime, bool blocked)
{
    if (button < 0 || button >= static_cast<int>(_state.MouseButtons.size()))
        return;

    ButtonState& mouseButton = _state.MouseButtons[button];

    if (!mouseButton.Down)
        mouseButton.Pressed = true;

    mouseButton.Down = true;
    mouseButton.LastMessageTime = messageTime;

    if (blocked)
        mouseButton.BlockedDown = true;
}

void SetMouseUpFromRawState(int button, DWORD messageTime)
{
    if (button < 0 || button >= static_cast<int>(_state.MouseButtons.size()))
        return;

    ButtonState& mouseButton = _state.MouseButtons[button];

    if (mouseButton.Down)
        mouseButton.Released = true;

    mouseButton.Down = false;
    mouseButton.LastMessageTime = messageTime;

    // Important:
    // Do not clear mouseButton.BlockedDown here.
    //
    // Raw input and legacy WM mouse messages can arrive for the same physical
    // button release. If raw input clears BlockedDown first, WM_LBUTTONUP sees
    // wasBlockedDown == false and leaks to the game.
}

void SyncAggregateModifierButtonStateLocked(int aggregateVk, int leftVk, int rightVk)
{
    ButtonState& aggregate = _state.Keys[aggregateVk];
    const ButtonState& left = _state.Keys[leftVk];
    const ButtonState& right = _state.Keys[rightVk];

    const bool wasDown = aggregate.Down;
    const bool isDown = left.Down || right.Down;

    aggregate.Down = isDown;
    aggregate.BlockedDown = left.BlockedDown || right.BlockedDown;

    if (!wasDown && isDown)
        aggregate.Pressed = true;

    if (wasDown && !isDown)
        aggregate.Released = true;

    aggregate.LastMessageTime =
        left.LastMessageTime >= right.LastMessageTime ? left.LastMessageTime : right.LastMessageTime;
}

void SyncAggregateModifierStateLocked()
{
    SyncAggregateModifierButtonStateLocked(VK_CONTROL, VK_LCONTROL, VK_RCONTROL);
    SyncAggregateModifierButtonStateLocked(VK_SHIFT, VK_LSHIFT, VK_RSHIFT);
    SyncAggregateModifierButtonStateLocked(VK_MENU, VK_LMENU, VK_RMENU);
}

bool IsMouseMessage(UINT msg)
{
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)
        return true;

    switch (msg)
    {
    case WM_NCMOUSEMOVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCLBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
    case WM_NCRBUTTONDBLCLK:
    case WM_NCMBUTTONDOWN:
    case WM_NCMBUTTONUP:
    case WM_NCMBUTTONDBLCLK:
    case WM_NCXBUTTONDOWN:
    case WM_NCXBUTTONUP:
    case WM_NCXBUTTONDBLCLK:
        return true;

    default:
        return false;
    }
}

bool IsKeyboardMessage(UINT msg) { return msg >= WM_KEYFIRST && msg <= WM_KEYLAST; }

bool IsMouseVirtualKey(int vk)
{
    switch (vk)
    {
    case VK_LBUTTON:
    case VK_RBUTTON:
    case VK_MBUTTON:
    case VK_XBUTTON1:
    case VK_XBUTTON2:
        return true;

    default:
        return false;
    }
}

int MouseMessageToButton(UINT msg, WPARAM wParam)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        return 0;

    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        return 1;

    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        return 2;

    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    {
        const WORD xButton = GET_XBUTTON_WPARAM(wParam);

        if (xButton == XBUTTON1)
            return 3;

        if (xButton == XBUTTON2)
            return 4;

        return -1;
    }

    default:
        return -1;
    }
}

void SetKeyDown(int vk, DWORD messageTime, bool blocked)
{
    if (vk < 0 || vk >= 256)
        return;

    ButtonState& key = _state.Keys[vk];

    if (!key.Down)
    {
        key.Pressed = true;
        _state.LastPressedKey = vk;
    }

    key.Down = true;
    key.LastMessageTime = messageTime;

    if (blocked)
        key.BlockedDown = true;

    SyncAggregateModifierStateLocked();
}

bool SetKeyUp(int vk, DWORD messageTime)
{
    if (vk < 0 || vk >= 256)
        return false;

    ButtonState& key = _state.Keys[vk];

    const bool wasBlockedDown = key.BlockedDown;

    if (key.Down)
        key.Released = true;

    key.Down = false;
    key.BlockedDown = false;
    key.LastMessageTime = messageTime;

    SyncAggregateModifierStateLocked();

    return wasBlockedDown;
}

void SetMouseDown(int button, DWORD messageTime, bool blocked)
{
    if (button < 0 || button >= static_cast<int>(_state.MouseButtons.size()))
        return;

    ButtonState& mouseButton = _state.MouseButtons[button];

    if (!mouseButton.Down)
        mouseButton.Pressed = true;

    mouseButton.Down = true;
    mouseButton.LastMessageTime = messageTime;

    if (blocked)
        mouseButton.BlockedDown = true;
}

bool SetMouseUp(int button, DWORD messageTime)
{
    if (button < 0 || button >= static_cast<int>(_state.MouseButtons.size()))
        return false;

    ButtonState& mouseButton = _state.MouseButtons[button];

    const bool wasBlockedDown = mouseButton.BlockedDown;

    if (mouseButton.Down)
        mouseButton.Released = true;

    mouseButton.Down = false;
    mouseButton.BlockedDown = false;
    mouseButton.LastMessageTime = messageTime;

    return wasBlockedDown;
}

void UpdateMousePositionFromClient(HWND hwnd, LPARAM lParam)
{
    POINT clientPos {};
    clientPos.x = GET_X_LPARAM(lParam);
    clientPos.y = GET_Y_LPARAM(lParam);

    POINT screenPos = clientPos;
    ClientToScreen(hwnd, &screenPos);

    _state.MouseClientPos = clientPos;
    _state.MouseScreenPos = screenPos;
}

bool IsInputMessage(UINT msg)
{
    if (IsMouseMessage(msg))
        return true;

    if (IsKeyboardMessage(msg))
        return true;

    switch (msg)
    {
    case WM_INPUT:
    case WM_CHAR:
    case WM_UNICHAR:
    case WM_IME_CHAR:
    case WM_IME_COMPOSITION:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        return true;

    default:
        return false;
    }
}

int NormalizeModifierVirtualKey(int vk, LPARAM lParam)
{
    switch (vk)
    {
    case VK_SHIFT:
    {
        const UINT scanCode = static_cast<UINT>((lParam >> 16) & 0xFF);
        const UINT mappedVk = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);

        if (mappedVk == VK_RSHIFT)
            return VK_RSHIFT;

        return VK_LSHIFT;
    }

    case VK_CONTROL:
    {
        const bool extended = (lParam & 0x01000000) != 0;
        return extended ? VK_RCONTROL : VK_LCONTROL;
    }

    case VK_MENU:
    {
        const bool extended = (lParam & 0x01000000) != 0;
        return extended ? VK_RMENU : VK_LMENU;
    }

    default:
        return vk;
    }
}

namespace
{
const char* InputMessageSourceName(InputMessageSource source)
{
    return source == InputMessageSource::WndProc ? "WndProc" : "MessageQueue";
}

const char* WindowMessageName(UINT msg)
{
    switch (msg)
    {
    case WM_INPUT:
        return "WM_INPUT";
    case WM_MOUSEMOVE:
        return "WM_MOUSEMOVE";
    case WM_LBUTTONDOWN:
        return "WM_LBUTTONDOWN";
    case WM_LBUTTONUP:
        return "WM_LBUTTONUP";
    case WM_RBUTTONDOWN:
        return "WM_RBUTTONDOWN";
    case WM_RBUTTONUP:
        return "WM_RBUTTONUP";
    case WM_MBUTTONDOWN:
        return "WM_MBUTTONDOWN";
    case WM_MBUTTONUP:
        return "WM_MBUTTONUP";
    case WM_XBUTTONDOWN:
        return "WM_XBUTTONDOWN";
    case WM_XBUTTONUP:
        return "WM_XBUTTONUP";
    case WM_MOUSEWHEEL:
        return "WM_MOUSEWHEEL";
    case WM_MOUSEHWHEEL:
        return "WM_MOUSEHWHEEL";
    case WM_NCMOUSEMOVE:
        return "WM_NCMOUSEMOVE";
    case WM_NCLBUTTONDOWN:
        return "WM_NCLBUTTONDOWN";
    case WM_NCLBUTTONUP:
        return "WM_NCLBUTTONUP";
    case WM_NCRBUTTONDOWN:
        return "WM_NCRBUTTONDOWN";
    case WM_NCRBUTTONUP:
        return "WM_NCRBUTTONUP";
    case WM_NCMBUTTONDOWN:
        return "WM_NCMBUTTONDOWN";
    case WM_NCMBUTTONUP:
        return "WM_NCMBUTTONUP";
    case WM_NCXBUTTONDOWN:
        return "WM_NCXBUTTONDOWN";
    case WM_NCXBUTTONUP:
        return "WM_NCXBUTTONUP";
    case WM_KEYDOWN:
        return "WM_KEYDOWN";
    case WM_KEYUP:
        return "WM_KEYUP";
    case WM_SYSKEYDOWN:
        return "WM_SYSKEYDOWN";
    case WM_SYSKEYUP:
        return "WM_SYSKEYUP";
    case WM_CHAR:
        return "WM_CHAR";
    case WM_UNICHAR:
        return "WM_UNICHAR";
    case WM_SETFOCUS:
        return "WM_SETFOCUS";
    case WM_KILLFOCUS:
        return "WM_KILLFOCUS";
    default:
        return "other";
    }
}

bool ShouldLogInputMessage(UINT msg)
{
    return IsMouseMessage(msg) || IsKeyboardMessage(msg) || msg == WM_INPUT || msg == WM_CHAR || msg == WM_UNICHAR ||
           msg == WM_SETFOCUS || msg == WM_KILLFOCUS;
}
} // namespace

bool ShouldBlockMouseWindowMessageLocked(HWND hwnd, UINT msg)
{
    return ShouldBlockMouseInputLocked() && IsMouseMessage(msg) && IsTargetWindow(hwnd);
}

bool HandleWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, InputMessageSource source)
{
    if (!IsTargetWindow(hwnd))
    {
        if (ShouldLogInputMessage(msg))
        {
            OPTIINPUT_LOG_VERBOSE(
                "ignored input message source:{} msg:{}({:#x}) hwnd:{} input:{} inputRoot:{} target:{} targetRoot:{}",
                InputMessageSourceName(source), WindowMessageName(msg), static_cast<unsigned>(msg),
                static_cast<void*>(hwnd), static_cast<void*>(_state.InputHwnd),
                static_cast<void*>(_state.InputRootHwnd), static_cast<void*>(_state.TargetHwnd),
                static_cast<void*>(_state.TargetRootHwnd));
        }
        return false;
    }

    const bool isInputMessage = IsInputMessage(msg);

    if (isInputMessage)
    {
        if (source == InputMessageSource::WndProc)
            _state.ReceivedWindowMessageThisFrame = true;
        else
            _state.ReceivedQueueMessageThisFrame = true;

        _state.ReceivedAnyInputThisFrame = true;

        OPTIINPUT_LOG_VERBOSE("input message source:{} msg:{}({:#x}) hwnd:{} wParam:{:#x} lParam:{:#x} menu:{} "
                              "blockMouse:{} blockKeyboard:{}",
                              InputMessageSourceName(source), WindowMessageName(msg), static_cast<unsigned>(msg),
                              static_cast<void*>(hwnd), static_cast<UINT64>(wParam), static_cast<UINT64>(lParam),
                              _state.MenuVisible ? 1 : 0, _state.BlockMouse ? 1 : 0, _state.BlockKeyboard ? 1 : 0);
    }

    bool shouldBlock = false;

    switch (msg)
    {
    case WM_SETFOCUS:
    {
        _state.Focused = true;
        break;
    }

    case WM_KILLFOCUS:
    {
        _state.Focused = false;
        break;
    }

    case WM_MOUSEMOVE:
    {
        UpdateMousePositionFromClient(hwnd, lParam);
        shouldBlock = _state.BlockMouse;
        break;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDBLCLK:
    {
        UpdateMousePositionFromClient(hwnd, lParam);

        const int button = MouseMessageToButton(msg, wParam);
        SetMouseDown(button, GetMessageTime(), _state.BlockMouse);
        OPTIINPUT_LOG_VERBOSE("mouse down button:{} blocked:{} pos=({}, {})", button, _state.BlockMouse ? 1 : 0,
                              _state.MouseClientPos.x, _state.MouseClientPos.y);

        shouldBlock = _state.BlockMouse;
        break;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
    {
        UpdateMousePositionFromClient(hwnd, lParam);

        const int button = MouseMessageToButton(msg, wParam);
        const bool wasBlockedDown = SetMouseUp(button, GetMessageTime());
        OPTIINPUT_LOG_VERBOSE("mouse up button:{} wasBlockedDown:{} pos=({}, {})", button, wasBlockedDown ? 1 : 0,
                              _state.MouseClientPos.x, _state.MouseClientPos.y);

        // If the game saw the button down before the menu opened,
        // let it see the matching up event to avoid stuck mouse buttons.
        shouldBlock = wasBlockedDown;
        break;
    }

    case WM_MOUSEWHEEL:
    {
        const SHORT delta = GET_WHEEL_DELTA_WPARAM(wParam);
        _state.MouseWheel += static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA);

        shouldBlock = _state.BlockMouse;
        break;
    }

    case WM_MOUSEHWHEEL:
    {
        shouldBlock = _state.BlockMouse;
        break;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        const int vk = NormalizeModifierVirtualKey(static_cast<int>(wParam), lParam);
        SetKeyDown(vk, GetMessageTime(), _state.BlockKeyboard);
        OPTIINPUT_LOG_VERBOSE("key down vk:{} blocked:{}", vk, _state.BlockKeyboard ? 1 : 0);

        shouldBlock = _state.BlockKeyboard;
        break;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        const int vk = NormalizeModifierVirtualKey(static_cast<int>(wParam), lParam);
        const bool wasBlockedDown = SetKeyUp(vk, GetMessageTime());
        OPTIINPUT_LOG_VERBOSE("key up vk:{} wasBlockedDown:{}", vk, wasBlockedDown ? 1 : 0);

        // If the game saw the key down before the menu opened,
        // let it see the matching key up to avoid stuck movement/actions.
        shouldBlock = wasBlockedDown;
        break;
    }

    case WM_CHAR:
    {
        if (wParam >= 0x20 && wParam <= 0xFFFF)
            _state.TextInput.push_back(static_cast<wchar_t>(wParam));

        shouldBlock = _state.BlockKeyboard;
        break;
    }

    case WM_UNICHAR:
    {
        if (wParam != UNICODE_NOCHAR && wParam >= 0x20 && wParam <= 0xFFFF)
            _state.TextInput.push_back(static_cast<wchar_t>(wParam));

        shouldBlock = wParam != UNICODE_NOCHAR && _state.BlockKeyboard;
        break;
    }

    case WM_INPUT:
    {
        /*
            Avoid double-processing:

            - WndProc source:
                always parse WM_INPUT, because this is the normal DispatchMessage path.

            - MessageQueue source:
                parse only when we are going to block/consume the message.
                Otherwise the same message may later reach WndProc and be parsed twice.
        */

        const bool shouldParseRawInput =
            source == InputMessageSource::WndProc || _state.BlockMouse || _state.BlockKeyboard;

        if (shouldParseRawInput)
            HandleRawInputLocked(reinterpret_cast<HRAWINPUT>(lParam));

        shouldBlock = _state.BlockMouse || _state.BlockKeyboard;
        break;
    }

    default:
    {
        if (IsMouseMessage(msg))
            shouldBlock = _state.BlockMouse;
        else if (IsKeyboardMessage(msg))
            shouldBlock = _state.BlockKeyboard;

        break;
    }
    }

    if (isInputMessage)
    {
        if (source == InputMessageSource::WndProc)
        {
            if (shouldBlock)
                _state.WindowMessageBlockedCount++;
            else
                _state.WindowMessagePassedCount++;
        }
        else
        {
            if (shouldBlock)
                _state.QueueMessageBlockedCount++;
            else
                _state.QueueMessagePassedCount++;
        }

        OPTIINPUT_LOG_VERBOSE(
            "input message result source:{} msg:{}({:#x}) shouldBlock:{} recvWnd:{} recvQueue:{} recvRaw:{} recvAny:{}",
            InputMessageSourceName(source), WindowMessageName(msg), static_cast<unsigned>(msg), shouldBlock ? 1 : 0,
            _state.ReceivedWindowMessageThisFrame ? 1 : 0, _state.ReceivedQueueMessageThisFrame ? 1 : 0,
            _state.ReceivedRawInputThisFrame ? 1 : 0, _state.ReceivedAnyInputThisFrame ? 1 : 0);
    }

    return shouldBlock;
}

void ClearTransientState()
{
    for (ButtonState& key : _state.Keys)
    {
        key.Pressed = false;
        key.Released = false;
    }

    for (ButtonState& button : _state.MouseButtons)
    {
        button.Pressed = false;
        button.Released = false;
    }

    _state.ReceivedWindowMessageThisFrame = false;
    _state.ReceivedQueueMessageThisFrame = false;
    _state.ReceivedRawInputThisFrame = false;
    _state.ReceivedAnyInputThisFrame = false;
    _state.PolledInputUsedThisFrame = false;
    _state.PolledMouseUsedThisFrame = false;
    _state.PolledKeyboardUsedThisFrame = false;
    _state.ExternalGetCursorPosVirtualizedThisFrame = false;
    _state.ExternalRawInputSinkPumpUsedThisFrame = false;
    _state.ExternalCursorRecenteringDetected = false;
    _state.LastPressedKey = 0;

    // Raw-input handles may be reused by callers inside a frame, but sanitize
    // decisions should not leak into the next frame.
    ResetRawInputSanitizeCacheLocked();

    _state.MouseWheel = 0.0f;
    _state.TextInput.clear();
    _state.LastMouseClientPos = _state.MouseClientPos;
}

LRESULT CALLBACK OptiInputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NULL)
    {
        WNDPROC originalWndProc = nullptr;

        {
            std::unique_lock lock(_state.Mutex);
            originalWndProc = _state.OriginalWndProc;
        }

        if (originalWndProc != nullptr)
            return CallWindowProcW(originalWndProc, hwnd, msg, wParam, lParam);

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    WNDPROC originalWndProc = nullptr;
    bool handled = false;
    bool unicodeNoCharProbe = false;

    {
        std::unique_lock lock(_state.Mutex);

        originalWndProc = _state.OriginalWndProc;
        OPTIINPUT_LOG_VERBOSE("WndProc dispatch hwnd:{} msg:{}({:#x}) input:{} original:{} menu:{} focused:{}",
                              static_cast<void*>(hwnd), WindowMessageName(msg), static_cast<unsigned>(msg),
                              static_cast<void*>(_state.InputHwnd), reinterpret_cast<std::uintptr_t>(originalWndProc),
                              _state.MenuVisible ? 1 : 0, _state.Focused ? 1 : 0);
        unicodeNoCharProbe = msg == WM_UNICHAR && wParam == UNICODE_NOCHAR && IsTargetWindow(hwnd);
        handled = HandleWindowMessage(hwnd, msg, wParam, lParam, InputMessageSource::WndProc);
    }

    if (unicodeNoCharProbe)
        return TRUE;

    if (handled)
        return 0;

    if (originalWndProc != nullptr)
        return CallWindowProcW(originalWndProc, hwnd, msg, wParam, lParam);

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ProcessRemovedMessage(MSG* msg)
{
    if (msg == nullptr)
        return false;

    if (msg->message == WM_NULL)
        return false;

    bool handled = false;

    {
        std::unique_lock lock(_state.Mutex);

        if (!_state.Initialized)
            return false;

        if (!IsTargetWindow(msg->hwnd))
        {
            if (ShouldLogInputMessage(msg->message))
            {
                OPTIINPUT_LOG_VERBOSE(
                    "removed queue input msg ignored msg:{}({:#x}) hwnd:{} input:{} inputRoot:{} target:{}",
                    WindowMessageName(msg->message), static_cast<unsigned>(msg->message), static_cast<void*>(msg->hwnd),
                    static_cast<void*>(_state.InputHwnd), static_cast<void*>(_state.InputRootHwnd),
                    static_cast<void*>(_state.TargetHwnd));
            }
            return false;
        }

        handled =
            HandleWindowMessage(msg->hwnd, msg->message, msg->wParam, msg->lParam, InputMessageSource::MessageQueue);
    }

    if (!handled)
        return false;

    // Important:
    // Let TranslateMessage generate WM_CHAR for ImGui text input before
    // neutralizing the original key message.
    TranslateMessage(msg);

    OPTIINPUT_LOG_VERBOSE("consuming removed queue message msg:{}({:#x}) hwnd:{}", WindowMessageName(msg->message),
                          static_cast<unsigned>(msg->message), static_cast<void*>(msg->hwnd));
    ConsumeMessage(msg);
    return true;
}

void ConsumeMessage(MSG* msg)
{
    if (msg == nullptr)
        return;

    msg->message = WM_NULL;
    msg->wParam = 0;
    msg->lParam = 0;
}

BOOL WINAPI hkPeekMessageA(LPMSG msg, HWND hwnd, UINT messageFilterMin, UINT messageFilterMax, UINT removeMsg)
{
    const BOOL result = o_PeekMessageA(msg, hwnd, messageFilterMin, messageFilterMax, removeMsg);

    if (!result || msg == nullptr)
        return result;

    if ((removeMsg & PM_REMOVE) == 0)
        return result;

    if (ShouldLogInputMessage(msg->message))
        OPTIINPUT_LOG_VERBOSE("PeekMessageA removed msg:{}({:#x}) hwnd:{}", WindowMessageName(msg->message),
                              static_cast<unsigned>(msg->message), static_cast<void*>(msg->hwnd));

    ProcessRemovedMessage(msg);

    return result;
}

BOOL WINAPI hkPeekMessageW(LPMSG msg, HWND hwnd, UINT messageFilterMin, UINT messageFilterMax, UINT removeMsg)
{
    const BOOL result = o_PeekMessageW(msg, hwnd, messageFilterMin, messageFilterMax, removeMsg);

    if (!result || msg == nullptr)
        return result;

    if ((removeMsg & PM_REMOVE) == 0)
        return result;

    if (ShouldLogInputMessage(msg->message))
        OPTIINPUT_LOG_VERBOSE("PeekMessageW removed msg:{}({:#x}) hwnd:{}", WindowMessageName(msg->message),
                              static_cast<unsigned>(msg->message), static_cast<void*>(msg->hwnd));

    ProcessRemovedMessage(msg);

    return result;
}

BOOL WINAPI hkGetMessageA(LPMSG msg, HWND hwnd, UINT messageFilterMin, UINT messageFilterMax)
{
    const BOOL result = o_GetMessageA(msg, hwnd, messageFilterMin, messageFilterMax);

    // GetMessage returns:
    // > 0 : message
    //   0 : WM_QUIT
    //  -1 : error
    if (result > 0 && msg != nullptr)
    {
        if (ShouldLogInputMessage(msg->message))
            OPTIINPUT_LOG_VERBOSE("GetMessageA msg:{}({:#x}) hwnd:{}", WindowMessageName(msg->message),
                                  static_cast<unsigned>(msg->message), static_cast<void*>(msg->hwnd));
        ProcessRemovedMessage(msg);
    }

    return result;
}

BOOL WINAPI hkGetMessageW(LPMSG msg, HWND hwnd, UINT messageFilterMin, UINT messageFilterMax)
{
    const BOOL result = o_GetMessageW(msg, hwnd, messageFilterMin, messageFilterMax);

    if (result > 0 && msg != nullptr)
    {
        if (ShouldLogInputMessage(msg->message))
            OPTIINPUT_LOG_VERBOSE("GetMessageW msg:{}({:#x}) hwnd:{}", WindowMessageName(msg->message),
                                  static_cast<unsigned>(msg->message), static_cast<void*>(msg->hwnd));
        ProcessRemovedMessage(msg);
    }

    return result;
}

namespace
{
bool ShouldBlockMouseMessageDispatchLocked(HWND hwnd, UINT msg, std::uint64_t& blockedCount, std::uint64_t& passedCount)
{
    if (!IsMouseMessage(msg) || !IsTargetWindow(hwnd))
        return false;

    if (ShouldBlockMouseInputLocked())
    {
        blockedCount++;
        return true;
    }

    passedCount++;
    return false;
}
} // namespace

BOOL WINAPI hkPostMessageA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    {
        std::unique_lock lock(_state.Mutex);

        if (ShouldBlockMouseMessageDispatchLocked(hwnd, msg, _state.PostMouseMessageBlockedCount,
                                                  _state.PostMouseMessagePassedCount))
        {
            OPTIINPUT_LOG_VERBOSE("blocking PostMessageA mouse msg:{}({:#x}) hwnd:{}", WindowMessageName(msg),
                                  static_cast<unsigned>(msg), static_cast<void*>(hwnd));
            return TRUE;
        }
    }

    return o_PostMessageA(hwnd, msg, wParam, lParam);
}

BOOL WINAPI hkPostMessageW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    {
        std::unique_lock lock(_state.Mutex);

        if (ShouldBlockMouseMessageDispatchLocked(hwnd, msg, _state.PostMouseMessageBlockedCount,
                                                  _state.PostMouseMessagePassedCount))
        {
            OPTIINPUT_LOG_VERBOSE("blocking PostMessageW mouse msg:{}({:#x}) hwnd:{}", WindowMessageName(msg),
                                  static_cast<unsigned>(msg), static_cast<void*>(hwnd));
            return TRUE;
        }
    }

    return o_PostMessageW(hwnd, msg, wParam, lParam);
}

LRESULT WINAPI hkSendMessageA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    {
        std::unique_lock lock(_state.Mutex);

        if (ShouldBlockMouseMessageDispatchLocked(hwnd, msg, _state.SendMouseMessageBlockedCount,
                                                  _state.SendMouseMessagePassedCount))
        {
            OPTIINPUT_LOG_VERBOSE("blocking SendMessageA mouse msg:{}({:#x}) hwnd:{}", WindowMessageName(msg),
                                  static_cast<unsigned>(msg), static_cast<void*>(hwnd));
            return 0;
        }
    }

    return o_SendMessageA(hwnd, msg, wParam, lParam);
}

LRESULT WINAPI hkSendMessageW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    {
        std::unique_lock lock(_state.Mutex);

        if (ShouldBlockMouseMessageDispatchLocked(hwnd, msg, _state.SendMouseMessageBlockedCount,
                                                  _state.SendMouseMessagePassedCount))
        {
            OPTIINPUT_LOG_VERBOSE("blocking SendMessageW mouse msg:{}({:#x}) hwnd:{}", WindowMessageName(msg),
                                  static_cast<unsigned>(msg), static_cast<void*>(hwnd));
            return 0;
        }
    }

    return o_SendMessageW(hwnd, msg, wParam, lParam);
}

UINT WINAPI hkSendInput(UINT inputCount, LPINPUT inputs, int size)
{
    if (inputs == nullptr || inputCount == 0 || size != static_cast<int>(sizeof(INPUT)))
        return o_SendInput(inputCount, inputs, size);

    bool blockMouse = false;
    bool blockKeyboard = false;

    {
        std::unique_lock lock(_state.Mutex);
        blockMouse = ShouldBlockMouseInputLocked();
        blockKeyboard = ShouldBlockKeyboardInputLocked();
    }

    if (!blockMouse && !blockKeyboard)
        return o_SendInput(inputCount, inputs, size);

    std::vector<INPUT> filteredInputs;
    filteredInputs.reserve(inputCount);

    std::uint64_t blockedMouse = 0;
    std::uint64_t blockedKeyboard = 0;

    for (UINT i = 0; i < inputCount; i++)
    {
        if (inputs[i].type == INPUT_MOUSE && blockMouse)
        {
            blockedMouse++;
            continue;
        }

        if (inputs[i].type == INPUT_KEYBOARD && blockKeyboard)
        {
            blockedKeyboard++;
            continue;
        }

        filteredInputs.push_back(inputs[i]);
    }

    if (blockedMouse != 0 || blockedKeyboard != 0)
    {
        std::unique_lock lock(_state.Mutex);
        _state.SendInputMouseBlockedCount += blockedMouse;
        _state.SendInputKeyboardBlockedCount += blockedKeyboard;
    }

    if (filteredInputs.empty())
        return inputCount;

    const UINT sent = o_SendInput(static_cast<UINT>(filteredInputs.size()), filteredInputs.data(), size);
    return sent + static_cast<UINT>(blockedMouse + blockedKeyboard);
}

void WINAPI hkmouse_event(DWORD flags, DWORD dx, DWORD dy, DWORD data, ULONG_PTR extraInfo)
{
    {
        std::unique_lock lock(_state.Mutex);

        if (ShouldBlockMouseInputLocked())
        {
            _state.MouseEventBlockedCount++;
            OPTIINPUT_LOG_VERBOSE("blocking mouse_event flags:{:#x}", static_cast<unsigned>(flags));
            return;
        }
    }

    o_mouse_event(flags, dx, dy, data, extraInfo);
}

SHORT WINAPI hkGetAsyncKeyState(int vk)
{
    if (ShouldBlockVirtualKey(vk))
    {
        {
            std::unique_lock lock(_state.Mutex);
            _state.GetAsyncKeyStateBlockedCount++;
        }

        OPTIINPUT_LOG_VERBOSE("blocking GetAsyncKeyState vk:{}", vk);
        return 0;
    }

    return o_GetAsyncKeyState(vk);
}

SHORT WINAPI hkGetKeyState(int vk)
{
    if (ShouldBlockVirtualKey(vk))
    {
        {
            std::unique_lock lock(_state.Mutex);
            _state.GetKeyStateBlockedCount++;
        }

        OPTIINPUT_LOG_VERBOSE("blocking GetKeyState vk:{}", vk);
        return 0;
    }

    return o_GetKeyState(vk);
}

BOOL WINAPI hkGetKeyboardState(PBYTE keyState)
{
    if (keyState == nullptr)
        return FALSE;

    const BOOL result = o_GetKeyboardState(keyState);

    if (!result)
        return result;

    std::unique_lock lock(_state.Mutex);

    const bool shouldBlockKeyboard = ShouldBlockKeyboardInputLocked();
    const bool shouldBlockMouse = ShouldBlockMouseInputLocked();

    if (!shouldBlockKeyboard && !shouldBlockMouse)
        return result;

    _state.GetKeyboardStateFilteredCount++;

    OPTIINPUT_LOG_VERBOSE("filtering GetKeyboardState blockKeyboard:{} blockMouse:{}", shouldBlockKeyboard ? 1 : 0,
                          shouldBlockMouse ? 1 : 0);

    if (shouldBlockKeyboard)
    {
        for (int vk = 0; vk < 256; vk++)
        {
            if (!IsMouseVirtualKey(vk))
                keyState[vk] = 0;
        }
    }

    if (shouldBlockMouse)
    {
        keyState[VK_LBUTTON] = 0;
        keyState[VK_RBUTTON] = 0;
        keyState[VK_MBUTTON] = 0;
        keyState[VK_XBUTTON1] = 0;
        keyState[VK_XBUTTON2] = 0;
    }

    return result;
}

} // namespace OptiInput
