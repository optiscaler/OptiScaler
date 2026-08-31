#include "pch.h"
#include "input_system_internal.h"

#include <array>
#include <vector>
#include <hidusage.h>

namespace OptiInput
{

namespace
{
constexpr const wchar_t* ExternalRawInputSinkClassName = L"OptiInputExternalRawInputSink";
constexpr UINT MaxExternalRawInputMessagesPerFrame = 256;

HINSTANCE GetCurrentModuleHandle()
{
    HINSTANCE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&_state), &module);
    return module != nullptr ? module : GetModuleHandleW(nullptr);
}

LRESULT CALLBACK ExternalRawInputSinkWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_INPUT)
    {
        {
            std::unique_lock lock(_state.Mutex);
            if (hwnd == _state.ExternalRawInputSinkHwnd)
            {
                HandleRawInputLocked(reinterpret_cast<HRAWINPUT>(lParam));
                _state.ExternalRawInputSinkMessageCount++;
            }
        }

        // WM_INPUT documentation expects DefWindowProc to be called so the
        // system can perform raw-input cleanup. Do this even after consuming it.
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool RegisterExternalRawInputSinkClass()
{
    static ATOM atom = 0;

    if (atom != 0)
        return true;

    WNDCLASSEXW wc {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ExternalRawInputSinkWndProc;
    wc.hInstance = GetCurrentModuleHandle();
    wc.lpszClassName = ExternalRawInputSinkClassName;

    atom = RegisterClassExW(&wc);
    if (atom != 0)
        return true;

    const DWORD error = GetLastError();
    if (error == ERROR_CLASS_ALREADY_EXISTS)
        return true;

    LOG_WARN("external raw input sink RegisterClassExW failed error:{}", error);
    return false;
}

bool ShouldUseExternalRawInputSinkLocked()
{
    return _state.Initialized && _state.ExternalTargetProcess && _state.InputHwnd == nullptr &&
           _state.TargetHwnd != nullptr && IsWindow(_state.TargetHwnd);
}

bool IsExternalRawInputSinkOnCurrentThreadLocked()
{
    return _state.ExternalRawInputSinkHwnd != nullptr && _state.ExternalRawInputSinkThreadId == GetCurrentThreadId();
}
} // namespace

void RemoveExternalRawInputSinkLocked()
{
    if (_state.ExternalRawInputSinkHwnd != nullptr)
    {
        if (_state.ExternalRawInputSinkThreadId == GetCurrentThreadId())
        {
            LOG_INFO("destroying external raw input sink hwnd:{}", static_cast<void*>(_state.ExternalRawInputSinkHwnd));
            DestroyWindow(_state.ExternalRawInputSinkHwnd);
        }
        else
        {
            LOG_WARN("external raw input sink hwnd:{} was created on thread {} and cannot be destroyed from thread {}",
                     static_cast<void*>(_state.ExternalRawInputSinkHwnd), _state.ExternalRawInputSinkThreadId,
                     GetCurrentThreadId());
        }
    }

    _state.ExternalRawInputSinkHwnd = nullptr;
    _state.ExternalRawInputSinkThreadId = 0;
    _state.ExternalRawInputSinkRegistered = false;
    _state.ExternalRawInputSinkPumpUsedThisFrame = false;
}

void EnsureExternalRawInputSinkLocked()
{
    if (!ShouldUseExternalRawInputSinkLocked())
    {
        if (_state.ExternalRawInputSinkHwnd != nullptr || _state.ExternalRawInputSinkRegistered)
            RemoveExternalRawInputSinkLocked();

        return;
    }

    if (_state.ExternalRawInputSinkRegistered && _state.ExternalRawInputSinkHwnd != nullptr)
        return;

    if (!RegisterExternalRawInputSinkClass())
        return;

    const HINSTANCE module = GetCurrentModuleHandle();
    HWND hwnd = CreateWindowExW(0, ExternalRawInputSinkClassName, L"OptiInput External RawInput Sink", WS_POPUP, 0, 0,
                                1, 1, nullptr, nullptr, module, nullptr);

    if (hwnd == nullptr)
    {
        LOG_WARN("external raw input sink CreateWindowExW failed error:{}", GetLastError());
        return;
    }

    RAWINPUTDEVICE devices[2] {};
    devices[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
    devices[0].usUsage = HID_USAGE_GENERIC_MOUSE;
    devices[0].dwFlags = RIDEV_INPUTSINK;
    devices[0].hwndTarget = hwnd;

    devices[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
    devices[1].usUsage = HID_USAGE_GENERIC_KEYBOARD;
    devices[1].dwFlags = RIDEV_INPUTSINK;
    devices[1].hwndTarget = hwnd;

    BOOL registered = FALSE;
    {
        ScopedHookBypass bypass;
        registered = o_RegisterRawInputDevices != nullptr
                         ? o_RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))
                         : RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
    }

    if (!registered)
    {
        const DWORD error = GetLastError();
        LOG_WARN("external raw input sink RegisterRawInputDevices failed hwnd:{} error:{}", static_cast<void*>(hwnd),
                 error);
        DestroyWindow(hwnd);
        return;
    }

    _state.ExternalRawInputSinkHwnd = hwnd;
    _state.ExternalRawInputSinkThreadId = GetCurrentThreadId();
    _state.ExternalRawInputSinkRegistered = true;

    LOG_INFO("external raw input sink registered hwnd:{} threadId:{} target:{} targetPid:{}", static_cast<void*>(hwnd),
             _state.ExternalRawInputSinkThreadId, static_cast<void*>(_state.TargetHwnd), _state.TargetProcessId);
}

void PumpExternalRawInputSinkLocked()
{
    _state.ExternalRawInputSinkPumpUsedThisFrame = false;

    if (!_state.ExternalRawInputSinkRegistered || _state.ExternalRawInputSinkHwnd == nullptr)
        return;

    if (!IsExternalRawInputSinkOnCurrentThreadLocked())
    {
        OPTIINPUT_LOG_VERBOSE("external raw input sink pump skipped hwnd:{} ownerThread:{} currentThread:{}",
                              static_cast<void*>(_state.ExternalRawInputSinkHwnd), _state.ExternalRawInputSinkThreadId,
                              GetCurrentThreadId());
        return;
    }

    MSG msg {};
    UINT pumped = 0;

    for (; pumped < MaxExternalRawInputMessagesPerFrame; pumped++)
    {
        BOOL hasMessage = FALSE;
        {
            ScopedHookBypass bypass;
            hasMessage = o_PeekMessageW != nullptr
                             ? o_PeekMessageW(&msg, _state.ExternalRawInputSinkHwnd, 0, 0, PM_REMOVE)
                             : PeekMessageW(&msg, _state.ExternalRawInputSinkHwnd, 0, 0, PM_REMOVE);
        }

        if (!hasMessage)
            break;

        if (msg.message == WM_INPUT)
        {
            HandleRawInputLocked(reinterpret_cast<HRAWINPUT>(msg.lParam));
            _state.ExternalRawInputSinkMessageCount++;
            DefWindowProcW(msg.hwnd, msg.message, msg.wParam, msg.lParam);
        }
        else
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (pumped == 0)
        return;

    _state.ExternalRawInputSinkPumpUsedThisFrame = true;
    _state.ExternalRawInputSinkPumpFrameCount++;
    OPTIINPUT_LOG_VERBOSE("external raw input sink pumped messages:{}", pumped);
}

void AccumulateExternalRawMouseDeltaLocked(const RAWMOUSE& mouse)
{
    if (!_state.ExternalRawInputSinkRegistered || !_state.ExternalTargetProcess || _state.InputHwnd != nullptr ||
        !_state.Focused)
    {
        return;
    }

    if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
        return;

    if (mouse.lLastX == 0 && mouse.lLastY == 0)
        return;

    _state.ExternalPendingMouseDeltaX += mouse.lLastX;
    _state.ExternalPendingMouseDeltaY += mouse.lLastY;
    _state.ExternalMouseDeltaEventCount++;
    _state.ExternalVirtualMouseActive = true;

    OPTIINPUT_LOG_VERBOSE("external raw mouse delta=({}, {}) accumulated=({}, {})", mouse.lLastX, mouse.lLastY,
                          _state.ExternalPendingMouseDeltaX, _state.ExternalPendingMouseDeltaY);
}

void ResetRawInputBlockStateLocked()
{
    OPTIINPUT_LOG_VERBOSE("reset raw/windows-hook blocked-down state");
    _state.RawKeyboardBlockedDown = {};
    _state.RawMouseBlockedDown = {};
    _state.WindowsHookKeyboardBlockedDown = {};
    _state.WindowsHookMouseBlockedDown = {};
}

void ResetRawInputSanitizeCacheLocked()
{
    _state.RawInputSanitizeCache = {};
    _state.RawInputSanitizeCacheWriteIndex = 0;
}

RawSanitizeAction GetRawKeyboardSanitizeActionLocked(const RAWKEYBOARD& keyboard)
{
    if (!_state.MenuVisible || !_state.BlockKeyboard)
        return RawSanitizeAction::Pass;

    const int vk = NormalizeRawKeyboardVirtualKey(keyboard);

    // Unknown key while menu is open: safest is to hide it from the game.
    if (vk <= 0 || vk >= 256)
        return RawSanitizeAction::SanitizeAll;

    const bool released = (keyboard.Flags & RI_KEY_BREAK) != 0;

    if (!released)
    {
        _state.RawKeyboardBlockedDown[vk] = true;
        return RawSanitizeAction::SanitizeAll;
    }

    const bool wasBlockedDown = _state.RawKeyboardBlockedDown[vk];
    _state.RawKeyboardBlockedDown[vk] = false;

    return wasBlockedDown ? RawSanitizeAction::SanitizeAll : RawSanitizeAction::Pass;
}

void HandleRawMouseButtonPairLocked(USHORT flags, USHORT downFlag, USHORT upFlag, int button, bool& shouldSanitize,
                                    USHORT& allowedButtonUpFlags)
{
    if ((flags & downFlag) != 0)
    {
        _state.RawMouseBlockedDown[button] = true;
        shouldSanitize = true;
    }

    if ((flags & upFlag) != 0)
    {
        const bool wasBlockedDown = _state.RawMouseBlockedDown[button];
        _state.RawMouseBlockedDown[button] = false;

        if (wasBlockedDown)
        {
            shouldSanitize = true;
        }
        else
        {
            allowedButtonUpFlags |= upFlag;
        }
    }
}

RawMouseSanitizeResult GetRawMouseSanitizeActionLocked(const RAWMOUSE& mouse)
{
    RawMouseSanitizeResult result {};

    if (!_state.MenuVisible || !_state.BlockMouse)
        return result;

    const USHORT flags = mouse.usButtonFlags;

    bool shouldSanitize = false;
    USHORT allowedButtonUpFlags = 0;

    HandleRawMouseButtonPairLocked(flags, RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, 0, shouldSanitize,
                                   allowedButtonUpFlags);

    HandleRawMouseButtonPairLocked(flags, RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, 1, shouldSanitize,
                                   allowedButtonUpFlags);

    HandleRawMouseButtonPairLocked(flags, RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, 2, shouldSanitize,
                                   allowedButtonUpFlags);

    HandleRawMouseButtonPairLocked(flags, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, 3, shouldSanitize,
                                   allowedButtonUpFlags);

    HandleRawMouseButtonPairLocked(flags, RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, 4, shouldSanitize,
                                   allowedButtonUpFlags);

    const bool hasMovement = mouse.lLastX != 0 || mouse.lLastY != 0;

    const bool hasWheel = (flags & RI_MOUSE_WHEEL) != 0 || (flags & RI_MOUSE_HWHEEL) != 0;

    if (hasMovement || hasWheel)
        shouldSanitize = true;

    if (!shouldSanitize)
        return result;

    result.AllowedButtonUpFlags = allowedButtonUpFlags;

    if (allowedButtonUpFlags != 0)
        result.Action = RawSanitizeAction::SanitizeMouseKeepAllowedButtonUps;
    else
        result.Action = RawSanitizeAction::SanitizeAll;

    return result;
}

RawSanitizeAction GetRawInputSanitizeActionLocked(const RAWINPUT& input, USHORT* allowedMouseButtonUpFlags)
{
    if (allowedMouseButtonUpFlags != nullptr)
        *allowedMouseButtonUpFlags = 0;

    switch (input.header.dwType)
    {
    case RIM_TYPEMOUSE:
    {
        const RawMouseSanitizeResult mouseResult = GetRawMouseSanitizeActionLocked(input.data.mouse);

        if (allowedMouseButtonUpFlags != nullptr)
            *allowedMouseButtonUpFlags = mouseResult.AllowedButtonUpFlags;

        return mouseResult.Action;
    }

    case RIM_TYPEKEYBOARD:
    {
        return GetRawKeyboardSanitizeActionLocked(input.data.keyboard);
    }

    default:
    {
        return RawSanitizeAction::Pass;
    }
    }
}

RawInputSanitizeDecision GetRawInputSanitizeDecisionLocked(HRAWINPUT rawInput, const RAWINPUT& input)
{
    if (rawInput != nullptr)
    {
        for (const RawInputSanitizeDecision& cachedDecision : _state.RawInputSanitizeCache)
        {
            if (cachedDecision.Handle == rawInput)
                return cachedDecision;
        }
    }

    RawInputSanitizeDecision decision {};
    decision.Handle = rawInput;
    decision.Action = GetRawInputSanitizeActionLocked(input, &decision.AllowedMouseButtonUpFlags);

    if (rawInput != nullptr)
    {
        _state.RawInputSanitizeCache[_state.RawInputSanitizeCacheWriteIndex] = decision;
        _state.RawInputSanitizeCacheWriteIndex =
            (_state.RawInputSanitizeCacheWriteIndex + 1) % _state.RawInputSanitizeCache.size();
    }

    return decision;
}

void RecordRawInputSanitizeCounterLocked(const RAWINPUT& input, RawSanitizeAction action)
{
    switch (input.header.dwType)
    {
    case RIM_TYPEMOUSE:
    {
        if (action == RawSanitizeAction::Pass)
            _state.RawMousePassedCount++;
        else if (action == RawSanitizeAction::SanitizeMouseKeepAllowedButtonUps)
            _state.RawMousePartialPassedCount++;
        else
            _state.RawMouseSanitizedCount++;

        break;
    }

    case RIM_TYPEKEYBOARD:
    {
        if (action == RawSanitizeAction::Pass)
            _state.RawKeyboardPassedCount++;
        else
            _state.RawKeyboardSanitizedCount++;

        break;
    }

    default:
    {
        break;
    }
    }
}

bool IsRawInputPacketReadable(const RAWINPUT& input, UINT availableSize)
{
    const UINT headerSize = static_cast<UINT>(sizeof(RAWINPUTHEADER));

    if (availableSize < headerSize)
        return false;

    const UINT packetSize = input.header.dwSize;

    if (packetSize < headerSize || packetSize > availableSize)
        return false;

    const UINT dataOffset = static_cast<UINT>(offsetof(RAWINPUT, data));

    switch (input.header.dwType)
    {
    case RIM_TYPEMOUSE:
    {
        const UINT mousePacketSize = dataOffset + static_cast<UINT>(sizeof(RAWMOUSE));
        return packetSize >= mousePacketSize;
    }

    case RIM_TYPEKEYBOARD:
    {
        const UINT keyboardPacketSize = dataOffset + static_cast<UINT>(sizeof(RAWKEYBOARD));
        return packetSize >= keyboardPacketSize;
    }

    case RIM_TYPEHID:
    {
        return packetSize >= dataOffset;
    }

    default:
    {
        return true;
    }
    }
}

void TrackRawInputDeviceRegistrationLocked(const RAWINPUTDEVICE& device)
{
    OPTIINPUT_LOG_VERBOSE("raw registration usagePage:{} usage:{} flags:{:#x} hwnd:{}", device.usUsagePage,
                          device.usUsage, static_cast<unsigned>(device.dwFlags), static_cast<void*>(device.hwndTarget));

    if (device.usUsagePage != HID_USAGE_PAGE_GENERIC)
        return;

    const bool remove = (device.dwFlags & RIDEV_REMOVE) != 0;

    if (device.usUsage == HID_USAGE_GENERIC_MOUSE)
    {
        if (remove)
        {
            _state.RawMouseTargetHwnd = nullptr;
            _state.RawMouseFlags = 0;
            _state.RawMouseRegistered = false;
            _state.RawMouseNoLegacy = false;
            _state.RawMouseInputSink = false;
            _state.RawMouseCaptureMouse = false;
            return;
        }

        _state.RawMouseTargetHwnd = device.hwndTarget;
        _state.RawMouseFlags = device.dwFlags;
        _state.RawMouseRegistered = true;
        _state.RawMouseNoLegacy = (device.dwFlags & RIDEV_NOLEGACY) != 0;
        _state.RawMouseInputSink = (device.dwFlags & RIDEV_INPUTSINK) != 0 || (device.dwFlags & RIDEV_EXINPUTSINK) != 0;
        _state.RawMouseCaptureMouse = (device.dwFlags & RIDEV_CAPTUREMOUSE) != 0;

        return;
    }

    if (device.usUsage == HID_USAGE_GENERIC_KEYBOARD)
    {
        if (remove)
        {
            _state.RawKeyboardTargetHwnd = nullptr;
            _state.RawKeyboardFlags = 0;
            _state.RawKeyboardRegistered = false;
            _state.RawKeyboardNoLegacy = false;
            _state.RawKeyboardInputSink = false;
            return;
        }

        _state.RawKeyboardTargetHwnd = device.hwndTarget;
        _state.RawKeyboardFlags = device.dwFlags;
        _state.RawKeyboardRegistered = true;
        _state.RawKeyboardNoLegacy = (device.dwFlags & RIDEV_NOLEGACY) != 0;
        _state.RawKeyboardInputSink =
            (device.dwFlags & RIDEV_INPUTSINK) != 0 || (device.dwFlags & RIDEV_EXINPUTSINK) != 0;

        return;
    }
}

void UpdateStateFromRawMouseLocked(const RAWMOUSE& mouse)
{
    AccumulateExternalRawMouseDeltaLocked(mouse);

    const DWORD time = GetTickCount();

    if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
        SetMouseDown(0, time, _state.BlockMouse);

    if (mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
        SetMouseUpStateOnly(0, time);

    if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
        SetMouseDown(1, time, _state.BlockMouse);

    if (mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
        SetMouseUpStateOnly(1, time);

    if (mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
        SetMouseDown(2, time, _state.BlockMouse);

    if (mouse.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)
        SetMouseUpStateOnly(2, time);

    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN)
        SetMouseDown(3, time, _state.BlockMouse);

    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP)
        SetMouseUpStateOnly(3, time);

    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN)
        SetMouseDown(4, time, _state.BlockMouse);

    if (mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP)
        SetMouseUpStateOnly(4, time);

    if (mouse.usButtonFlags & RI_MOUSE_WHEEL)
    {
        _state.MouseWheel += static_cast<SHORT>(mouse.usButtonData) / static_cast<float>(WHEEL_DELTA);
    }

    if (mouse.usButtonFlags & RI_MOUSE_HWHEEL)
    {
        // Optional later:
        // Add horizontal wheel state if you want to feed ImGui AddMouseWheelEvent(x, y).
    }
}

int NormalizeRawKeyboardVirtualKey(const RAWKEYBOARD& keyboard)
{
    int vk = static_cast<int>(keyboard.VKey);

    if (vk <= 0 || vk >= 256)
        return 0;

    if (vk == VK_SHIFT)
    {
        vk = static_cast<int>(MapVirtualKeyW(keyboard.MakeCode, MAPVK_VSC_TO_VK_EX));
    }
    else if (vk == VK_CONTROL)
    {
        const bool extended = (keyboard.Flags & RI_KEY_E0) != 0;
        vk = extended ? VK_RCONTROL : VK_LCONTROL;
    }
    else if (vk == VK_MENU)
    {
        const bool extended = (keyboard.Flags & RI_KEY_E0) != 0;
        vk = extended ? VK_RMENU : VK_LMENU;
    }

    if (vk <= 0 || vk >= 256)
        return 0;

    return vk;
}

void UpdateStateFromRawKeyboardLocked(const RAWKEYBOARD& keyboard)
{
    const int vk = NormalizeRawKeyboardVirtualKey(keyboard);

    if (vk <= 0 || vk >= 256)
        return;

    const bool released = (keyboard.Flags & RI_KEY_BREAK) != 0;
    const DWORD time = GetTickCount();

    if (released)
        SetKeyUpStateOnly(vk, GetTickCount());
    else
        SetKeyDown(vk, GetTickCount(), _state.BlockKeyboard);
}

void UpdateStateFromRawInputLocked(const RAWINPUT& input)
{
    _state.ReceivedRawInputThisFrame = true;
    _state.ReceivedAnyInputThisFrame = true;

    switch (input.header.dwType)
    {
    case RIM_TYPEMOUSE:
    {
        UpdateStateFromRawMouseLocked(input.data.mouse);
        break;
    }

    case RIM_TYPEKEYBOARD:
    {
        UpdateStateFromRawKeyboardLocked(input.data.keyboard);
        break;
    }

    default:
    {
        break;
    }
    }
}

void HandleRawInputLocked(HRAWINPUT rawInputHandle)
{
    if (rawInputHandle == nullptr)
        return;

    UINT size = 0;

    {
        ScopedHookBypass bypass;

        const UINT queryResult = o_GetRawInputData(rawInputHandle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));

        if (queryResult != 0)
            return;
    }

    if (size == 0)
        return;

    std::vector<BYTE> buffer(size);

    UINT readSize = size;

    {
        ScopedHookBypass bypass;

        const UINT readResult =
            o_GetRawInputData(rawInputHandle, RID_INPUT, buffer.data(), &readSize, sizeof(RAWINPUTHEADER));

        if (readResult == static_cast<UINT>(-1))
            return;

        if (readResult != size)
            return;
    }

    const RAWINPUT* input = reinterpret_cast<const RAWINPUT*>(buffer.data());

    UpdateStateFromRawInputLocked(*input);
}

void ApplyRawInputSanitizeActionLocked(RAWINPUT& input, RawSanitizeAction action, USHORT allowedMouseButtonUpFlags)
{
    switch (action)
    {
    case RawSanitizeAction::Pass:
    {
        return;
    }

    case RawSanitizeAction::SanitizeAll:
    {
        if (input.header.dwType == RIM_TYPEMOUSE)
            SanitizeRawMouseAllLocked(input);
        else if (input.header.dwType == RIM_TYPEKEYBOARD)
            SanitizeRawKeyboardLocked(input);

        return;
    }

    case RawSanitizeAction::SanitizeMouseKeepAllowedButtonUps:
    {
        if (input.header.dwType == RIM_TYPEMOUSE)
        {
            SanitizeRawMouseKeepAllowedButtonUpsLocked(input, allowedMouseButtonUpFlags);
        }

        return;
    }
    }
}

void SanitizeRawMouseAllLocked(RAWINPUT& input)
{
    input.header.hDevice = nullptr;
    input.header.wParam = RIM_INPUTSINK;

    RAWMOUSE& mouse = input.data.mouse;

    mouse.usFlags = 0;
    mouse.ulButtons = 0;
    mouse.usButtonFlags = 0;
    mouse.usButtonData = 0;
    mouse.ulRawButtons = 0;
    mouse.lLastX = 0;
    mouse.lLastY = 0;
    mouse.ulExtraInformation = 0;
}

void SanitizeRawMouseKeepAllowedButtonUpsLocked(RAWINPUT& input, USHORT allowedButtonUpFlags)
{
    RAWMOUSE& mouse = input.data.mouse;

    // Preserve input.header.hDevice and input.header.wParam here.
    // This packet is intentionally still visible to the game as a real raw
    // mouse packet so it can receive the release that prevents stuck state.

    mouse.usFlags = 0;

    // Preserve only button-up flags that are needed to prevent stuck game state.
    mouse.ulButtons = allowedButtonUpFlags;
    mouse.usButtonFlags = allowedButtonUpFlags;

    // Button-up flags do not need wheel data.
    mouse.usButtonData = 0;

    mouse.ulRawButtons = 0;

    // Block camera movement.
    mouse.lLastX = 0;
    mouse.lLastY = 0;

    mouse.ulExtraInformation = 0;
}

void SanitizeRawKeyboardLocked(RAWINPUT& input)
{
    input.header.hDevice = nullptr;
    input.header.wParam = RIM_INPUTSINK;

    RAWKEYBOARD& keyboard = input.data.keyboard;

    keyboard.MakeCode = 0;
    keyboard.Flags = RI_KEY_BREAK;
    keyboard.Reserved = 0;
    keyboard.VKey = 0;
    keyboard.Message = WM_KEYUP;
    keyboard.ExtraInformation = 0;
}

UINT WINAPI hkGetRawInputData(HRAWINPUT rawInput, UINT command, LPVOID data, PUINT size, UINT headerSize)
{
    const UINT result = o_GetRawInputData(rawInput, command, data, size, headerSize);

    if (result == static_cast<UINT>(-1))
        return result;

    if (command != RID_INPUT)
        return result;

    if (data == nullptr)
        return result;

    if (result < static_cast<UINT>(sizeof(RAWINPUTHEADER)))
        return result;

    RAWINPUT* input = reinterpret_cast<RAWINPUT*>(data);

    if (!IsRawInputPacketReadable(*input, result))
        return result;

    {
        std::unique_lock lock(_state.Mutex);

        if (bypassHookDepth == 0)
        {
            // A game may query the same HRAWINPUT more than once. Reuse the
            // first decision so key/button release passthrough stays stable.
            const RawInputSanitizeDecision decision = GetRawInputSanitizeDecisionLocked(rawInput, *input);

            OPTIINPUT_LOG_VERBOSE("GetRawInputData sanitize handle:{} type:{} action:{} allowedUp:{:#x}",
                                  static_cast<void*>(rawInput), input->header.dwType, static_cast<int>(decision.Action),
                                  decision.AllowedMouseButtonUpFlags);
            RecordRawInputSanitizeCounterLocked(*input, decision.Action);
            ApplyRawInputSanitizeActionLocked(*input, decision.Action, decision.AllowedMouseButtonUpFlags);
        }
    }

    return result;
}

UINT WINAPI hkGetRawInputBuffer(PRAWINPUT data, PUINT size, UINT headerSize)
{
    const UINT bufferSize = size != nullptr ? *size : 0;
    const UINT result = o_GetRawInputBuffer(data, size, headerSize);

    if (result == static_cast<UINT>(-1))
        return result;

    if (result == 0)
        return result;

    if (data == nullptr || bufferSize == 0)
        return result;

    BYTE* const bufferBegin = reinterpret_cast<BYTE*>(data);
    BYTE* const bufferEnd = bufferBegin + bufferSize;
    PRAWINPUT current = data;

    {
        std::unique_lock lock(_state.Mutex);

        if (bypassHookDepth != 0)
            return result;

        for (UINT i = 0; i < result; i++)
        {
            BYTE* const currentBytes = reinterpret_cast<BYTE*>(current);

            if (currentBytes < bufferBegin || currentBytes >= bufferEnd)
                break;

            const std::size_t remainingSize = static_cast<std::size_t>(bufferEnd - currentBytes);

            if (remainingSize < sizeof(RAWINPUTHEADER))
                break;

            if (!IsRawInputPacketReadable(*current, static_cast<UINT>(remainingSize)))
                break;

            const DWORD packetSize = current->header.dwSize;
            USHORT allowedMouseButtonUpFlags = 0;
            const RawSanitizeAction action = GetRawInputSanitizeActionLocked(*current, &allowedMouseButtonUpFlags);

            OPTIINPUT_LOG_VERBOSE("GetRawInputBuffer sanitize index:{} type:{} action:{} allowedUp:{:#x} offset:{}", i,
                                  current->header.dwType, static_cast<int>(action), allowedMouseButtonUpFlags,
                                  static_cast<unsigned>(currentBytes - bufferBegin));
            RecordRawInputSanitizeCounterLocked(*current, action);
            ApplyRawInputSanitizeActionLocked(*current, action, allowedMouseButtonUpFlags);

            current = reinterpret_cast<PRAWINPUT>(currentBytes + packetSize);
        }
    }

    return result;
}

BOOL WINAPI hkRegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT deviceCount, UINT size)
{
    const BOOL result = o_RegisterRawInputDevices(devices, deviceCount, size);

    if (!result)
        return result;

    if (devices == nullptr)
        return result;

    if (size != sizeof(RAWINPUTDEVICE))
        return result;

    {
        std::unique_lock lock(_state.Mutex);

        for (UINT i = 0; i < deviceCount; i++)
            TrackRawInputDeviceRegistrationLocked(devices[i]);
    }

    return result;
}

} // namespace OptiInput
