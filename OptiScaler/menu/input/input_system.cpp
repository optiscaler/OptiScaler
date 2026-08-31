#include "pch.h"
#include "input_system_internal.h"

#include <include/imgui/imgui.h>

namespace OptiInput
{

InputState _state;

GetAsyncKeyState_t o_GetAsyncKeyState = ::GetAsyncKeyState;
GetKeyState_t o_GetKeyState = ::GetKeyState;
GetKeyboardState_t o_GetKeyboardState = ::GetKeyboardState;
GetCursorPos_t o_GetCursorPos = ::GetCursorPos;
SetCursorPos_t o_SetCursorPos = ::SetCursorPos;
GetPhysicalCursorPos_t o_GetPhysicalCursorPos = nullptr;
SetPhysicalCursorPos_t o_SetPhysicalCursorPos = nullptr;
GetMessagePos_t o_GetMessagePos = ::GetMessagePos;
GetMouseMovePointsEx_t o_GetMouseMovePointsEx = ::GetMouseMovePointsEx;
ClipCursor_t o_ClipCursor = ::ClipCursor;
SendInput_t o_SendInput = ::SendInput;
MouseEvent_t o_mouse_event = ::mouse_event;
PostMessageA_t o_PostMessageA = ::PostMessageA;
PostMessageW_t o_PostMessageW = ::PostMessageW;
SendMessageA_t o_SendMessageA = ::SendMessageA;
SendMessageW_t o_SendMessageW = ::SendMessageW;
CreateFileA_t o_CreateFileA = ::CreateFileA;
CreateFileW_t o_CreateFileW = ::CreateFileW;
ReadFile_t o_ReadFile = ::ReadFile;
DeviceIoControl_t o_DeviceIoControl = ::DeviceIoControl;
CloseHandle_t o_CloseHandle = ::CloseHandle;
PeekMessageA_t o_PeekMessageA = ::PeekMessageA;
PeekMessageW_t o_PeekMessageW = ::PeekMessageW;
GetMessageA_t o_GetMessageA = ::GetMessageA;
GetMessageW_t o_GetMessageW = ::GetMessageW;
GetRawInputData_t o_GetRawInputData = ::GetRawInputData;
GetRawInputBuffer_t o_GetRawInputBuffer = ::GetRawInputBuffer;
RegisterRawInputDevices_t o_RegisterRawInputDevices = ::RegisterRawInputDevices;
GetClipCursor_t o_GetClipCursor = ::GetClipCursor;
SetWindowsHookExA_t o_SetWindowsHookExA = ::SetWindowsHookExA;
SetWindowsHookExW_t o_SetWindowsHookExW = ::SetWindowsHookExW;
UnhookWindowsHookEx_t o_UnhookWindowsHookEx = ::UnhookWindowsHookEx;
GameInputCreate_t o_GameInputCreate = nullptr;
XInputGetState_t o_XInputGetState = nullptr;
XInputGetState_t o_XInputGetStateEx = nullptr;
XInputGetKeystroke_t o_XInputGetKeystroke = nullptr;
XInputSetState_t o_XInputSetState = nullptr;
DirectInput8Create_t o_DirectInput8Create = nullptr;
DirectInputCreateA_t o_DirectInputCreateA = nullptr;
DirectInputCreateW_t o_DirectInputCreateW = nullptr;
DirectInputCreateEx_t o_DirectInputCreateEx = nullptr;
DirectInputCreateDevice_t o_DirectInputCreateDeviceA = nullptr;
DirectInputCreateDevice_t o_DirectInputCreateDeviceW = nullptr;
DirectInputGetDeviceState_t o_DirectInputDeviceGetDeviceState = nullptr;
DirectInputGetDeviceData_t o_DirectInputDeviceGetDeviceData = nullptr;
DirectInputDeviceRelease_t o_DirectInputDeviceRelease = nullptr;

thread_local int bypassHookDepth = 0;

bool ShouldApplyBlockingPolicyLocked() { return bypassHookDepth == 0 && _state.MenuVisible; }

bool ShouldBlockKeyboardInputLocked() { return ShouldApplyBlockingPolicyLocked() && _state.BlockKeyboard; }

bool ShouldBlockMouseInputLocked() { return ShouldApplyBlockingPolicyLocked() && _state.BlockMouse; }

bool ShouldBlockCursorInputLocked() { return ShouldApplyBlockingPolicyLocked() && _state.BlockCursor; }

namespace
{
const char* YesNo(bool value) { return value ? "yes" : "no"; }

const char* AcquisitionModeName(InputAcquisitionMode mode)
{
    switch (mode)
    {
    case InputAcquisitionMode::WindowMessages:
        return "window";

    case InputAcquisitionMode::RawInput:
        return "raw";

    case InputAcquisitionMode::PolledAbsolute:
        return "polled-absolute";

    case InputAcquisitionMode::ExternalRawVirtualMouse:
        return "external-raw-virtual-mouse";

    case InputAcquisitionMode::None:
    default:
        return "none";
    }
}

InputAcquisitionMode ResolveInputAcquisitionModeLocked()
{
    if (_state.ExternalVirtualMouseAuthoritative || _state.ExternalVirtualMouseUsedThisFrame ||
        _state.ExternalVirtualMouseRelativeUsedThisFrame)
    {
        return InputAcquisitionMode::ExternalRawVirtualMouse;
    }

    if (_state.ReceivedRawInputThisFrame)
        return InputAcquisitionMode::RawInput;

    if (_state.ReceivedWindowMessageThisFrame || _state.ReceivedQueueMessageThisFrame)
        return InputAcquisitionMode::WindowMessages;

    if (_state.PolledInputUsedThisFrame || _state.PolledInputActive)
        return InputAcquisitionMode::PolledAbsolute;

    return InputAcquisitionMode::None;
}

void RefreshInputAcquisitionModeLocked() { _state.AcquisitionMode = ResolveInputAcquisitionModeLocked(); }

void LogHwndIdentityLocked(const char* label, HWND hwnd)
{
    if (hwnd == nullptr)
    {
        LOG_DEBUG("{}: hwnd:null", label);
        return;
    }

    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(hwnd, &processId);
    const HWND rootHwnd = IsWindow(hwnd) ? GetAncestor(hwnd, GA_ROOT) : nullptr;

    LOG_DEBUG("{}: hwnd:{} isWindow:{} root:{} pid:{} tid:{} currentPid:{}", label, static_cast<void*>(hwnd),
              IsWindow(hwnd) ? 1 : 0, static_cast<void*>(rootHwnd), processId, threadId, _state.CurrentProcessId);
}
} // namespace

void LogInputHealthSnapshotLocked(const char* origin)
{
    static std::uint64_t frameIndex = 0;
    static HWND lastTargetHwnd = nullptr;
    static HWND lastInputHwnd = nullptr;
    static HWND lastForegroundHwnd = nullptr;
    static bool lastFocused = false;
    static bool lastMenuVisible = false;
    static bool lastWndProcSubclassed = false;
    static bool lastExternalTargetProcess = false;
    static bool lastHasExplicitInputHwnd = false;
    static bool lastPolledInputActive = false;
    static bool lastExternalVirtualMouseActive = false;
    static bool lastExternalVirtualMouseAuthoritative = false;
    static bool lastExternalLowLevelMouseHookInstalled = false;
    static bool lastExternalRawInputSinkRegistered = false;
    static InputAcquisitionMode lastAcquisitionMode = InputAcquisitionMode::None;
    static DWORD lastTargetProcessId = 0;
    static DWORD lastInputProcessId = 0;
    static std::uint64_t lastNoInputWarnFrame = 0;
    static std::uint64_t lastNoInputHwndWarnFrame = 0;
    static std::uint64_t lastNoSubclassWarnFrame = 0;

    frameIndex++;

    HWND foregroundHwnd = GetForegroundWindow();
    DWORD foregroundProcessId = 0;
    DWORD foregroundThreadId = 0;

    if (foregroundHwnd != nullptr)
        foregroundThreadId = GetWindowThreadProcessId(foregroundHwnd, &foregroundProcessId);

    RefreshInputAcquisitionModeLocked();

    const bool stateChanged =
        lastTargetHwnd != _state.TargetHwnd || lastInputHwnd != _state.InputHwnd ||
        lastForegroundHwnd != foregroundHwnd || lastFocused != _state.Focused ||
        lastMenuVisible != _state.MenuVisible || lastWndProcSubclassed != _state.WndProcSubclassed ||
        lastExternalTargetProcess != _state.ExternalTargetProcess ||
        lastHasExplicitInputHwnd != _state.HasExplicitInputHwnd || lastPolledInputActive != _state.PolledInputActive ||
        lastExternalVirtualMouseActive != _state.ExternalVirtualMouseActive ||
        lastExternalVirtualMouseAuthoritative != _state.ExternalVirtualMouseAuthoritative ||
        lastExternalLowLevelMouseHookInstalled != _state.ExternalLowLevelMouseHookInstalled ||
        lastExternalRawInputSinkRegistered != _state.ExternalRawInputSinkRegistered ||
        lastAcquisitionMode != _state.AcquisitionMode || lastTargetProcessId != _state.TargetProcessId ||
        lastInputProcessId != _state.InputProcessId;

#if OPTIINPUT_VERBOSE_LOGGING
    const bool shouldLogHealth =
        stateChanged || (frameIndex % 120) == 0 || (_state.MenuVisible && (frameIndex % 30) == 0);
#else
    const bool shouldLogHealth = stateChanged || (frameIndex % 600) == 0;
#endif

    if (shouldLogHealth)
    {
#if OPTIINPUT_VERBOSE_LOGGING
        LOG_DEBUG(
            "{} health frame:{} mode:{} target:{} targetPid:{} input:{} inputPid:{} explicitInput:{} externalTarget:{} "
            "foreground:{} foregroundPid:{} foregroundTid:{} focused:{} menu:{} subclassed:{} hooks:{} recvWnd:{} "
            "recvQueue:{} recvRaw:{} recvAny:{} polledActive:{} polledUsed:{} polledMouse:{} polledKeyboard:{} "
            "externalVirtualMouse:{} externalAuthoritative:{} externalGetCursor:{} externalVirtualUsed:{} "
            "externalRelative:{} recenter:{} llMouseHook:{} rawSink:{} rawSinkPump:{} pendingDelta=({}, {}) "
            "virtualMouse=({}, {}) mouse=({}, {}) wheel:{} rawMouseReg:{} rawKeyboardReg:{} "
            "rawMouseNoLegacy:{} rawMouseSink:{} rawMouseCapture:{} rawKeyboardNoLegacy:{} rawKeyboardSink:{} "
            "trackedHooks:{} "
            "wndBlock:{} wndPass:{} queueBlock:{} queuePass:{} "
            "rawKeySan:{} rawKeyPass:{} rawMouseSan:{} rawMousePart:{} rawMousePass:{} "
            "asyncKeyBlock:{} keyStateBlock:{} keyboardStateFilter:{} cursorGetBlock:{} cursorPhysGetBlock:{} "
            "msgPosBlock:{} cursorSetBlock:{} cursorPhysSetBlock:{} clipBlock:{} getClipBlock:{} "
            "sendInputMouseBlock:{} sendInputKeyboardBlock:{} mouseEventBlock:{} postMouseBlock:{} postMousePass:{} "
            "sendMouseBlock:{} sendMousePass:{} hookKeyBlock:{} hookKeyPass:{} hookMouseBlock:{} hookMousePass:{} "
            "mouseMovePtsBlock:{} hidMouse:{} hidKeyboard:{} hidGamepad:{} hidOther:{} hidHandles:{} "
            "hidCreate:{} hidReadBlock:{} hidReadPass:{} hidIoctlBlock:{} hidIoctlPass:{} "
            "xinput:{} xGetState:{} xGetStateEx:{} xKeystroke:{} xSetState:{} xGetStateBlock:{} xKeyBlock:{} "
            "dinput:{} dinputLegacy:{} di8:{} diCreateA:{} diCreateW:{} diCreateEx:{} diDevA:{} diDevW:{} "
            "diState:{} diData:{} diKeyboard:{} diMouse:{} diOther:{} diStateBlock:{} diDataBlock:{}",
            origin != nullptr ? origin : "?", frameIndex, AcquisitionModeName(_state.AcquisitionMode),
            static_cast<void*>(_state.TargetHwnd), _state.TargetProcessId, static_cast<void*>(_state.InputHwnd),
            _state.InputProcessId, YesNo(_state.HasExplicitInputHwnd), YesNo(_state.ExternalTargetProcess),
            static_cast<void*>(foregroundHwnd), foregroundProcessId, foregroundThreadId, YesNo(_state.Focused),
            YesNo(_state.MenuVisible), YesNo(_state.WndProcSubclassed), YesNo(_state.HooksInstalled),
            YesNo(_state.ReceivedWindowMessageThisFrame), YesNo(_state.ReceivedQueueMessageThisFrame),
            YesNo(_state.ReceivedRawInputThisFrame), YesNo(_state.ReceivedAnyInputThisFrame),
            YesNo(_state.PolledInputActive), YesNo(_state.PolledInputUsedThisFrame),
            YesNo(_state.PolledMouseUsedThisFrame), YesNo(_state.PolledKeyboardUsedThisFrame),
            YesNo(_state.ExternalVirtualMouseActive), YesNo(_state.ExternalVirtualMouseAuthoritative),
            YesNo(_state.ExternalGetCursorPosVirtualizedThisFrame), YesNo(_state.ExternalVirtualMouseUsedThisFrame),
            YesNo(_state.ExternalVirtualMouseRelativeUsedThisFrame), YesNo(_state.ExternalCursorRecenteringDetected),
            YesNo(_state.ExternalLowLevelMouseHookInstalled), YesNo(_state.ExternalRawInputSinkRegistered),
            YesNo(_state.ExternalRawInputSinkPumpUsedThisFrame), _state.ExternalPendingMouseDeltaX,
            _state.ExternalPendingMouseDeltaY, _state.ExternalVirtualMouseClient.x, _state.ExternalVirtualMouseClient.y,
            _state.MouseClientPos.x, _state.MouseClientPos.y, _state.MouseWheel, YesNo(_state.RawMouseRegistered),
            YesNo(_state.RawKeyboardRegistered), YesNo(_state.RawMouseNoLegacy), YesNo(_state.RawMouseInputSink),
            YesNo(_state.RawMouseCaptureMouse), YesNo(_state.RawKeyboardNoLegacy), YesNo(_state.RawKeyboardInputSink),
            CountTrackedWindowsHooksLocked(), _state.WindowMessageBlockedCount, _state.WindowMessagePassedCount,
            _state.QueueMessageBlockedCount, _state.QueueMessagePassedCount, _state.RawKeyboardSanitizedCount,
            _state.RawKeyboardPassedCount, _state.RawMouseSanitizedCount, _state.RawMousePartialPassedCount,
            _state.RawMousePassedCount, _state.GetAsyncKeyStateBlockedCount, _state.GetKeyStateBlockedCount,
            _state.GetKeyboardStateFilteredCount, _state.GetCursorPosBlockedCount,
            _state.GetPhysicalCursorPosBlockedCount, _state.GetMessagePosBlockedCount, _state.SetCursorPosBlockedCount,
            _state.SetPhysicalCursorPosBlockedCount, _state.ClipCursorBlockedCount,
            _state.GetClipCursorVirtualizedCount, _state.SendInputMouseBlockedCount,
            _state.SendInputKeyboardBlockedCount, _state.MouseEventBlockedCount, _state.PostMouseMessageBlockedCount,
            _state.PostMouseMessagePassedCount, _state.SendMouseMessageBlockedCount, _state.SendMouseMessagePassedCount,
            _state.WindowsHookKeyboardBlockedCount, _state.WindowsHookKeyboardPassedCount,
            _state.WindowsHookMouseBlockedCount, _state.WindowsHookMousePassedCount, _state.MouseMovePointsBlockedCount,
            YesNo(_state.HidMouseHandleSeen), YesNo(_state.HidKeyboardHandleSeen), YesNo(_state.HidGamepadHandleSeen),
            YesNo(_state.HidOtherHandleSeen), _state.HidTrackedHandleCount, _state.HidCreateFileCallCount,
            _state.HidReadFileBlockedCount, _state.HidReadFilePassedCount, _state.HidDeviceIoControlBlockedCount,
            _state.HidDeviceIoControlPassedCount, YesNo(_state.XInputModuleLoaded),
            YesNo(_state.XInputGetStateHookInstalled), YesNo(_state.XInputGetStateExHookInstalled),
            YesNo(_state.XInputGetKeystrokeHookInstalled), YesNo(_state.XInputSetStateHookInstalled),
            _state.XInputGetStateBlockedCount, _state.XInputGetKeystrokeBlockedCount,
            YesNo(_state.DirectInputModuleLoaded), YesNo(_state.DirectInputLegacyModuleLoaded),
            YesNo(_state.DirectInput8CreateHookInstalled), YesNo(_state.DirectInputCreateAHookInstalled),
            YesNo(_state.DirectInputCreateWHookInstalled), YesNo(_state.DirectInputCreateExHookInstalled),
            YesNo(_state.DirectInputCreateDeviceAHookInstalled), YesNo(_state.DirectInputCreateDeviceWHookInstalled),
            YesNo(_state.DirectInputGetDeviceStateHookInstalled), YesNo(_state.DirectInputGetDeviceDataHookInstalled),
            YesNo(_state.DirectInputKeyboardDeviceSeen), YesNo(_state.DirectInputMouseDeviceSeen),
            YesNo(_state.DirectInputOtherDeviceSeen), _state.DirectInputGetDeviceStateBlockedCount,
            _state.DirectInputGetDeviceDataBlockedCount);
#else
        LOG_DEBUG("{} health frame:{} mode:{} target:{} input:{} focused:{} menu:{} subclassed:{} hooks:{} "
                  "recvWnd:{} recvQueue:{} recvRaw:{} polled:{} rawMouse:{} rawKeyboard:{} trackedHooks:{} "
                  "xinput:{} dinput:{} hidMouse:{} hidKeyboard:{} hidGamepad:{}",
                  origin != nullptr ? origin : "?", frameIndex, AcquisitionModeName(_state.AcquisitionMode),
                  static_cast<void*>(_state.TargetHwnd), static_cast<void*>(_state.InputHwnd), YesNo(_state.Focused),
                  YesNo(_state.MenuVisible), YesNo(_state.WndProcSubclassed), YesNo(_state.HooksInstalled),
                  YesNo(_state.ReceivedWindowMessageThisFrame), YesNo(_state.ReceivedQueueMessageThisFrame),
                  YesNo(_state.ReceivedRawInputThisFrame), YesNo(_state.PolledInputActive),
                  YesNo(_state.RawMouseRegistered), YesNo(_state.RawKeyboardRegistered),
                  CountTrackedWindowsHooksLocked(), YesNo(_state.XInputModuleLoaded),
                  YesNo(_state.DirectInputModuleLoaded), YesNo(_state.HidMouseHandleSeen),
                  YesNo(_state.HidKeyboardHandleSeen), YesNo(_state.HidGamepadHandleSeen));
#endif
    }

    if (_state.ExternalTargetProcess && _state.InputHwnd == nullptr && frameIndex - lastNoInputHwndWarnFrame >= 600)
    {
        lastNoInputHwndWarnFrame = frameIndex;
        LOG_WARN("external target process is active but InputHwnd is null; using polled/raw-sink/virtual mouse "
                 "fallback for menu input. "
                 "A local InputHwnd can improve menu input, but robust game input blocking still requires an "
                 "in-process input module.");
    }

    if (_state.InputHwnd != nullptr && _state.UseWndProcSubclass && !_state.WndProcSubclassed &&
        frameIndex - lastNoSubclassWarnFrame >= 600)
    {
        lastNoSubclassWarnFrame = frameIndex;
        LOG_WARN(
            "InputHwnd is set but WndProc is not subclassed: input:{} inputPid:{} useSubclass:{} externalTarget:{}",
            static_cast<void*>(_state.InputHwnd), _state.InputProcessId, YesNo(_state.UseWndProcSubclass),
            YesNo(_state.ExternalTargetProcess));
    }

    if (_state.MenuVisible && _state.InputHwnd != nullptr && !_state.ReceivedAnyInputThisFrame &&
        frameIndex - lastNoInputWarnFrame >= 600)
    {
        lastNoInputWarnFrame = frameIndex;
        LOG_WARN("menu is visible but no window/queue/raw input was received this frame. input:{} foreground:{} "
                 "focused:{} subclassed:{}. Check that InputHwnd is the real overlay/menu HWND and that its message "
                 "pump runs.",
                 static_cast<void*>(_state.InputHwnd), static_cast<void*>(foregroundHwnd), YesNo(_state.Focused),
                 YesNo(_state.WndProcSubclassed));
    }

    lastTargetHwnd = _state.TargetHwnd;
    lastInputHwnd = _state.InputHwnd;
    lastForegroundHwnd = foregroundHwnd;
    lastFocused = _state.Focused;
    lastExternalRawInputSinkRegistered = _state.ExternalRawInputSinkRegistered;
    lastMenuVisible = _state.MenuVisible;
    lastWndProcSubclassed = _state.WndProcSubclassed;
    lastExternalTargetProcess = _state.ExternalTargetProcess;
    lastHasExplicitInputHwnd = _state.HasExplicitInputHwnd;
    lastPolledInputActive = _state.PolledInputActive;
    lastExternalVirtualMouseActive = _state.ExternalVirtualMouseActive;
    lastExternalVirtualMouseAuthoritative = _state.ExternalVirtualMouseAuthoritative;
    lastExternalLowLevelMouseHookInstalled = _state.ExternalLowLevelMouseHookInstalled;
    lastTargetProcessId = _state.TargetProcessId;
    lastInputProcessId = _state.InputProcessId;
}

namespace
{
bool ShouldPollVirtualKey(int vk)
{
    if (vk < 0 || vk >= 256)
        return false;

    switch (vk)
    {
    case VK_LBUTTON:
    case VK_RBUTTON:
    case VK_MBUTTON:
    case VK_XBUTTON1:
    case VK_XBUTTON2:
    case VK_SHIFT:
    case VK_CONTROL:
    case VK_MENU:
        return false;

    default:
        return true;
    }
}

void PollVirtualKeyLocked(int vk, DWORD time)
{
    if (!ShouldPollVirtualKey(vk))
        return;

    const bool down = (RealGetAsyncKeyStateSafe(vk) & 0x8000) != 0;

    if (down)
    {
        SetKeyDown(vk, time, ShouldBlockKeyboardInputLocked());
        return;
    }

    if (_state.Keys[vk].Down)
        SetKeyUpStateOnly(vk, time);
}

bool PollMouseButtonLocked(int vk, int button, DWORD time)
{
    if (button < 0 || button >= static_cast<int>(_state.MouseButtons.size()))
        return false;

    const bool wasDown = _state.MouseButtons[button].Down;
    const bool down = (RealGetAsyncKeyStateSafe(vk) & 0x8000) != 0;

    if (down)
    {
        SetMouseDown(button, time, ShouldBlockMouseInputLocked());
        return !wasDown;
    }

    if (wasDown)
    {
        SetMouseUpStateOnly(button, time);
        return true;
    }

    return false;
}

HWND GetPolledCoordinateHwndLocked()
{
    if (_state.InputHwnd != nullptr && IsWindow(_state.InputHwnd))
        return _state.InputHwnd;

    if (_state.TargetHwnd != nullptr && IsWindow(_state.TargetHwnd))
        return _state.TargetHwnd;

    return nullptr;
}

bool ShouldUseExternalVirtualMouseLocked()
{
    return _state.ExternalTargetProcess && _state.InputHwnd == nullptr && _state.TargetHwnd != nullptr &&
           IsWindow(_state.TargetHwnd);
}

bool IsExternalVirtualMouseAuthoritativeLocked()
{
    return ShouldUseExternalVirtualMouseLocked() && _state.ExternalVirtualMouseActive &&
           _state.ExternalVirtualMouseInitialized;
}

void ClampPointToClientLocked(HWND hwnd, POINT& point)
{
    RECT clientRect {};

    if (hwnd == nullptr || !GetClientRect(hwnd, &clientRect))
        return;

    const LONG minX = clientRect.left;
    const LONG minY = clientRect.top;
    const LONG maxX = clientRect.right > clientRect.left ? clientRect.right - 1 : clientRect.left;
    const LONG maxY = clientRect.bottom > clientRect.top ? clientRect.bottom - 1 : clientRect.top;

    if (point.x < minX)
        point.x = minX;
    else if (point.x > maxX)
        point.x = maxX;

    if (point.y < minY)
        point.y = minY;
    else if (point.y > maxY)
        point.y = maxY;
}

POINT GetClientCenterLocked(HWND hwnd)
{
    RECT clientRect {};

    if (hwnd == nullptr || !GetClientRect(hwnd, &clientRect))
        return {};

    POINT center {};
    center.x = (clientRect.left + clientRect.right) / 2;
    center.y = (clientRect.top + clientRect.bottom) / 2;
    return center;
}

bool UpdateExternalVirtualMouseScreenPosLocked(HWND coordinateHwnd)
{
    if (coordinateHwnd == nullptr || !_state.ExternalVirtualMouseInitialized)
        return false;

    POINT screenPos = _state.ExternalVirtualMouseClient;
    if (!ClientToScreen(coordinateHwnd, &screenPos))
        return false;

    _state.MouseScreenPos = screenPos;
    return true;
}

void InitializeExternalVirtualMouseLocked(HWND coordinateHwnd, const POINT& fallbackClientPos)
{
    if (_state.ExternalVirtualMouseInitialized)
        return;

    POINT initialPos = fallbackClientPos;

    if (initialPos.x == 0 && initialPos.y == 0)
        initialPos = GetClientCenterLocked(coordinateHwnd);

    ClampPointToClientLocked(coordinateHwnd, initialPos);

    _state.ExternalVirtualMouseClient = initialPos;
    _state.ExternalVirtualMouseInitialized = true;
    UpdateExternalVirtualMouseScreenPosLocked(coordinateHwnd);

    LOG_INFO("external virtual mouse initialized hwnd:{} pos=({}, {}) screen=({}, {})",
             static_cast<void*>(coordinateHwnd), _state.ExternalVirtualMouseClient.x,
             _state.ExternalVirtualMouseClient.y, _state.MouseScreenPos.x, _state.MouseScreenPos.y);
}

bool ShouldForceImGuiMouseDrawCursorLocked()
{
    return _state.MenuVisible && _state.Focused && _state.ExternalTargetProcess && _state.InputHwnd == nullptr &&
           _state.ExternalVirtualMouseInitialized &&
           (_state.ExternalVirtualMouseActive || _state.ExternalVirtualMouseAuthoritative ||
            _state.ExternalVirtualMouseUsedThisFrame || _state.ExternalVirtualMouseRelativeUsedThisFrame);
}

bool ShouldUseExternalCursorPolicyLocked()
{
    return _state.ExternalTargetProcess && _state.InputHwnd == nullptr && _state.TargetHwnd != nullptr &&
           IsWindow(_state.TargetHwnd);
}

void RestoreImGuiMouseDrawCursorLocked(ImGuiIO& io)
{
    if (_state.ImGuiMouseDrawCursorForced)
    {
        io.MouseDrawCursor = _state.SavedImGuiMouseDrawCursor;
        _state.ImGuiMouseDrawCursorForced = false;
        _state.SavedImGuiMouseDrawCursor = false;
    }

    if (_state.ImGuiNoMouseCursorChangeForced)
    {
        if (_state.SavedImGuiNoMouseCursorChange)
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        else
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;

        _state.ImGuiNoMouseCursorChangeForced = false;
        _state.SavedImGuiNoMouseCursorChange = false;
    }
}

void UpdateImGuiMouseDrawCursorLocked(ImGuiIO& io)
{
    const bool externalCursorPolicy = ShouldUseExternalCursorPolicyLocked();
    const bool shouldDrawSoftwareCursor = ShouldForceImGuiMouseDrawCursorLocked();

    if (externalCursorPolicy)
    {
        if (!_state.ImGuiMouseDrawCursorForced)
        {
            _state.SavedImGuiMouseDrawCursor = io.MouseDrawCursor;
            _state.ImGuiMouseDrawCursorForced = true;
        }

        if (!_state.ImGuiNoMouseCursorChangeForced)
        {
            _state.SavedImGuiNoMouseCursorChange = (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) != 0;
            _state.ImGuiNoMouseCursorChangeForced = true;
        }

        // Portal RTX / Remix owns a foreign HWND and continuously recenters or
        // hides the real cursor. Do not let ImGui_ImplWin32 switch the OS cursor
        // back to an arrow in this process. We draw an ImGui software cursor only
        // while the OptiScaler menu owns the virtual cursor. When the menu is
        // closed, force the software cursor off every frame instead of restoring
        // a possibly-stale true value.
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.MouseDrawCursor = shouldDrawSoftwareCursor;

        if (!shouldDrawSoftwareCursor)
            ::SetCursor(nullptr);

        return;
    }

    RestoreImGuiMouseDrawCursorLocked(io);
}

bool ApplyExternalVirtualMouseLocked(HWND coordinateHwnd, const POINT& absoluteClientPos)
{
    _state.ExternalVirtualMouseUsedThisFrame = false;
    _state.ExternalVirtualMouseRelativeUsedThisFrame = false;

    if (!_state.MenuVisible)
    {
        _state.ExternalPendingMouseDeltaX = 0;
        _state.ExternalPendingMouseDeltaY = 0;
        _state.ExternalVirtualMouseActive = false;
        _state.ExternalVirtualMouseAuthoritative = false;
        return false;
    }

    if (!ShouldUseExternalVirtualMouseLocked() || coordinateHwnd == nullptr)
    {
        _state.ExternalVirtualMouseActive = false;
        _state.ExternalVirtualMouseAuthoritative = false;
        return false;
    }

    const LONG deltaX = _state.ExternalPendingMouseDeltaX;
    const LONG deltaY = _state.ExternalPendingMouseDeltaY;
    _state.ExternalPendingMouseDeltaX = 0;
    _state.ExternalPendingMouseDeltaY = 0;

    if (deltaX != 0 || deltaY != 0)
    {
        InitializeExternalVirtualMouseLocked(coordinateHwnd, absoluteClientPos);

        _state.ExternalVirtualMouseClient.x += deltaX;
        _state.ExternalVirtualMouseClient.y += deltaY;
        ClampPointToClientLocked(coordinateHwnd, _state.ExternalVirtualMouseClient);

        _state.ExternalVirtualMouseActive = true;
        _state.ExternalVirtualMouseUsedThisFrame = true;
        _state.ExternalVirtualMouseRelativeUsedThisFrame = true;
        _state.ExternalVirtualMouseFrameCount++;

        OPTIINPUT_LOG_VERBOSE("external virtual mouse delta=({}, {}) pos=({}, {})", deltaX, deltaY,
                              _state.ExternalVirtualMouseClient.x, _state.ExternalVirtualMouseClient.y);
    }

    if (!_state.ExternalVirtualMouseActive || !_state.ExternalVirtualMouseInitialized)
    {
        _state.ExternalVirtualMouseAuthoritative = false;
        return false;
    }

    _state.ExternalVirtualMouseAuthoritative = true;
    _state.ExternalVirtualMouseAuthoritativeFrameCount++;
    _state.ExternalVirtualMouseAbsoluteSuppressedCount++;
    _state.MouseClientPos = _state.ExternalVirtualMouseClient;

    // Keep the virtual cursor coherent in both coordinate spaces. This matters
    // because legacy/manual menu code may still call GetCursorPos and then
    // ScreenToClient. When the menu is open, hkGetCursorPos returns
    // MouseScreenPos, so MouseScreenPos must represent the virtual cursor, not
    // the real hardware cursor that Portal RTX keeps snapping back to center.
    UpdateExternalVirtualMouseScreenPosLocked(coordinateHwnd);
    return true;
}
} // namespace

void PollInputFallbackLocked()
{
    _state.PolledInputActive = false;
    _state.PolledInputUsedThisFrame = false;
    _state.PolledMouseUsedThisFrame = false;
    _state.PolledKeyboardUsedThisFrame = false;
    _state.ExternalVirtualMouseUsedThisFrame = false;
    _state.ExternalVirtualMouseRelativeUsedThisFrame = false;
    _state.ExternalVirtualMouseAuthoritative = false;

    if (!_state.Initialized || !_state.Focused)
    {
        RefreshInputAcquisitionModeLocked();
        return;
    }

    // Some games never dispatch normal Win32 messages to the render HWND, or they
    // process input via DirectInput / engine polling before our WndProc/message hooks
    // can see anything. Polling here is an input acquisition fallback for Opti's UI;
    // it does not by itself block the game from reading input.
    _state.PolledInputActive = true;

    const DWORD time = GetTickCount();

    POINT screenPos {};
    const bool haveCursor = RealGetCursorPosSafe(&screenPos) != FALSE;
    const HWND coordinateHwnd = GetPolledCoordinateHwndLocked();

    if (haveCursor && coordinateHwnd != nullptr)
    {
        POINT clientPos = screenPos;
        if (ScreenToClient(coordinateHwnd, &clientPos))
        {
            _state.MouseScreenPos = screenPos;

            const bool usedVirtualMouse = ApplyExternalVirtualMouseLocked(coordinateHwnd, clientPos);
            if (!usedVirtualMouse)
            {
                if (clientPos.x != _state.MouseClientPos.x || clientPos.y != _state.MouseClientPos.y)
                    _state.PolledMouseUsedThisFrame = true;

                _state.MouseClientPos = clientPos;
            }
            else if (_state.ExternalVirtualMouseUsedThisFrame)
            {
                _state.PolledMouseUsedThisFrame = true;
            }
        }
    }

    _state.PolledMouseUsedThisFrame |= PollMouseButtonLocked(VK_LBUTTON, 0, time);
    _state.PolledMouseUsedThisFrame |= PollMouseButtonLocked(VK_RBUTTON, 1, time);
    _state.PolledMouseUsedThisFrame |= PollMouseButtonLocked(VK_MBUTTON, 2, time);
    _state.PolledMouseUsedThisFrame |= PollMouseButtonLocked(VK_XBUTTON1, 3, time);
    _state.PolledMouseUsedThisFrame |= PollMouseButtonLocked(VK_XBUTTON2, 4, time);

    if (_state.PolledMouseUsedThisFrame)
        _state.PolledMouseFrameCount++;

    for (int vk = 0; vk < 256; ++vk)
    {
        const bool wasDown = _state.Keys[vk].Down;
        PollVirtualKeyLocked(vk, time);

        if (_state.Keys[vk].Down != wasDown || _state.Keys[vk].Pressed || _state.Keys[vk].Released)
            _state.PolledKeyboardUsedThisFrame = true;
    }

    if (_state.PolledKeyboardUsedThisFrame)
        _state.PolledKeyboardFrameCount++;

    if (_state.PolledMouseUsedThisFrame || _state.PolledKeyboardUsedThisFrame)
    {
        _state.PolledInputUsedThisFrame = true;
        _state.PolledInputFrameCount++;
    }

    RefreshInputAcquisitionModeLocked();
}

void ApplyMenuVisibilityChangeLocked(bool visible)
{
    const bool wasMenuVisible = _state.MenuVisible;

    _state.MenuVisible = visible;
    _state.BlockMouse = visible;
    _state.BlockKeyboard = visible;
    _state.BlockCursor = visible;

    if (wasMenuVisible != visible)
    {
        LOG_INFO("menu visibility changed {} -> {} blockMouse:{} blockKeyboard:{} blockCursor:{} input:{} target:{}",
                 wasMenuVisible ? 1 : 0, visible ? 1 : 0, _state.BlockMouse ? 1 : 0, _state.BlockKeyboard ? 1 : 0,
                 _state.BlockCursor ? 1 : 0, static_cast<void*>(_state.InputHwnd),
                 static_cast<void*>(_state.TargetHwnd));
    }

    if (!visible && _state.ImGuiMouseDrawCursorForced && ImGui::GetCurrentContext() != nullptr)
        UpdateImGuiMouseDrawCursorLocked(ImGui::GetIO());

    if (!wasMenuVisible && visible)
    {
        POINT blockedCursorPos {};
        if (o_GetCursorPos != nullptr && o_GetCursorPos(&blockedCursorPos))
            _state.BlockedCursorScreenPos = blockedCursorPos;
        else
            _state.BlockedCursorScreenPos = _state.MouseScreenPos;

        _state.HasBlockedCursorScreenPos = true;

        LOG_DEBUG("captured blocked cursor position screen:({}, {})", _state.BlockedCursorScreenPos.x,
                  _state.BlockedCursorScreenPos.y);

        BeginCursorClipBlockLocked();
    }
    else if (wasMenuVisible && !visible)
    {
        EndCursorClipBlockLocked();

        _state.HasBlockedCursorScreenPos = false;
        _state.BlockedCursorScreenPos = {};

        // Drop synthetic down/up suppression that only existed while the
        // overlay owned input. New raw handles will rebuild sanitize decisions.
        ResetButtonBlockedStateLocked();
        ResetRawInputBlockStateLocked();
        ResetRawInputSanitizeCacheLocked();
    }
}

bool Initialize(const InitializeOptions& options)
{
    std::unique_lock lock(_state.Mutex);

    if (_state.CurrentProcessId == 0)
        _state.CurrentProcessId = GetCurrentProcessId();

    LOG_INFO("Initialize requested target:{} input:{} isUwp:{} useSubclass:{} currentPid:{}",
             static_cast<void*>(options.TargetHwnd), static_cast<void*>(options.InputHwnd), options.IsUwp ? 1 : 0,
             options.UseWndProcSubclass ? 1 : 0, _state.CurrentProcessId);
    LogHwndIdentityLocked("Initialize target", options.TargetHwnd);
    LogHwndIdentityLocked("Initialize input", options.InputHwnd);

    if (_state.Initialized)
    {
        if (options.TargetHwnd != nullptr &&
            (options.TargetHwnd != _state.TargetHwnd || options.IsUwp != _state.IsUwp ||
             options.UseWndProcSubclass != _state.UseWndProcSubclass))
        {
            SetTargetWindow(options.TargetHwnd, options.IsUwp, options.UseWndProcSubclass);
        }

        if (options.InputHwnd != nullptr && options.InputHwnd != _state.InputHwnd)
            SetInputWindow(options.InputHwnd, options.UseWndProcSubclass, true);

        LOG_DEBUG(
            "Initialize re-entry state target:{} targetPid:{} input:{} inputPid:{} externalTarget:{} subclassed:{}",
            static_cast<void*>(_state.TargetHwnd), _state.TargetProcessId, static_cast<void*>(_state.InputHwnd),
            _state.InputProcessId, _state.ExternalTargetProcess ? 1 : 0, _state.WndProcSubclassed ? 1 : 0);

        return true;
    }

    _state.Initialized = true;
    _state.IsUwp = options.IsUwp;
    _state.UseWndProcSubclass = options.UseWndProcSubclass;
    _state.CurrentProcessId = GetCurrentProcessId();

    if (options.TargetHwnd != nullptr)
        SetTargetWindow(options.TargetHwnd, options.IsUwp, options.UseWndProcSubclass);

    if (options.InputHwnd != nullptr)
        SetInputWindow(options.InputHwnd, options.UseWndProcSubclass, true);

    const bool hooksInstalled = InstallHooks();
    LOG_INFO("Initialize completed hooksInstalled:{} target:{} targetPid:{} input:{} inputPid:{} externalTarget:{} "
             "subclassed:{}",
             hooksInstalled ? 1 : 0, static_cast<void*>(_state.TargetHwnd), _state.TargetProcessId,
             static_cast<void*>(_state.InputHwnd), _state.InputProcessId, _state.ExternalTargetProcess ? 1 : 0,
             _state.WndProcSubclassed ? 1 : 0);

    if (hooksInstalled)
    {
        UpdateGameInputIntegrationLocked();
        UpdateXInputIntegrationLocked();
        UpdateDirectInputIntegrationLocked();
    }

    return hooksInstalled;
}

bool Initialize(HWND targetHwnd, bool isUwp)
{
    InitializeOptions options {};
    options.TargetHwnd = targetHwnd;
    options.IsUwp = isUwp;
    options.UseWndProcSubclass = true;

    return Initialize(options);
}

bool Initialize(HWND targetHwnd, HWND inputHwnd, bool isUwp)
{
    InitializeOptions options {};
    options.TargetHwnd = targetHwnd;
    options.InputHwnd = inputHwnd;
    options.IsUwp = isUwp;
    options.UseWndProcSubclass = true;

    return Initialize(options);
}

void ResetStateAfterShutdown()
{
    if ((_state.ImGuiMouseDrawCursorForced || _state.ImGuiNoMouseCursorChangeForced) &&
        ImGui::GetCurrentContext() != nullptr)
    {
        RestoreImGuiMouseDrawCursorLocked(ImGui::GetIO());
    }

    _state.TargetHwnd = nullptr;
    _state.TargetRootHwnd = nullptr;
    _state.InputHwnd = nullptr;
    _state.InputRootHwnd = nullptr;
    _state.TargetProcessId = 0;
    _state.TargetThreadId = 0;
    _state.InputProcessId = 0;
    _state.InputThreadId = 0;
    _state.CurrentProcessId = 0;

    _state.IsUwp = false;
    _state.UseWndProcSubclass = true;
    _state.WndProcSubclassed = false;
    _state.ExternalTargetProcess = false;
    _state.HasExplicitInputHwnd = false;
    _state.PolledInputActive = false;
    _state.PolledInputUsedThisFrame = false;
    _state.PolledMouseUsedThisFrame = false;
    _state.PolledKeyboardUsedThisFrame = false;
    _state.AcquisitionMode = InputAcquisitionMode::None;
    _state.ExternalVirtualMouseActive = false;
    _state.ExternalVirtualMouseUsedThisFrame = false;
    _state.ExternalVirtualMouseRelativeUsedThisFrame = false;
    _state.ExternalVirtualMouseAuthoritative = false;
    _state.ExternalGetCursorPosVirtualizedThisFrame = false;
    _state.ExternalCursorRecenteringDetected = false;
    _state.ExternalVirtualMouseInitialized = false;
    _state.ExternalLowLevelMouseHookInstalled = false;
    _state.ExternalRawInputSinkRegistered = false;
    _state.ExternalRawInputSinkPumpUsedThisFrame = false;

    _state.OriginalWndProc = nullptr;

    _state.Initialized = false;
    _state.HooksInstalled = false;
    _state.Focused = false;

    _state.MenuVisible = false;
    _state.BlockMouse = false;
    _state.BlockKeyboard = false;
    _state.BlockCursor = false;

    _state.RawMouseTargetHwnd = nullptr;
    _state.RawKeyboardTargetHwnd = nullptr;

    _state.RawMouseFlags = 0;
    _state.RawKeyboardFlags = 0;

    _state.RawMouseRegistered = false;
    _state.RawKeyboardRegistered = false;

    _state.RawMouseNoLegacy = false;
    _state.RawKeyboardNoLegacy = false;

    _state.RawMouseInputSink = false;
    _state.RawKeyboardInputSink = false;
    _state.RawMouseCaptureMouse = false;

    _state.Keys = {};
    _state.MouseButtons = {};

    _state.MouseClientPos = {};
    _state.MouseScreenPos = {};
    _state.LastMouseClientPos = {};
    _state.ExternalVirtualMouseClient = {};
    _state.ExternalLastMouseHookScreen = {};
    _state.ExternalLastMouseHookScreenValid = false;
    _state.ExternalPendingMouseDeltaX = 0;
    _state.ExternalPendingMouseDeltaY = 0;
    _state.ExternalLowLevelMouseHook = nullptr;
    _state.ExternalRawInputSinkHwnd = nullptr;
    _state.ExternalRawInputSinkThreadId = 0;

    ResetRawInputBlockStateLocked();
    ResetRawInputSanitizeCacheLocked();

    _state.WindowsHookSlots = {};

    _state.WindowMessageBlockedCount = 0;
    _state.WindowMessagePassedCount = 0;
    _state.QueueMessageBlockedCount = 0;
    _state.QueueMessagePassedCount = 0;

    _state.GetAsyncKeyStateBlockedCount = 0;
    _state.GetKeyStateBlockedCount = 0;
    _state.GetKeyboardStateFilteredCount = 0;

    _state.GetCursorPosBlockedCount = 0;
    _state.GetPhysicalCursorPosBlockedCount = 0;
    _state.GetMessagePosBlockedCount = 0;
    _state.SetCursorPosBlockedCount = 0;
    _state.SetPhysicalCursorPosBlockedCount = 0;
    _state.ClipCursorBlockedCount = 0;
    _state.GetClipCursorVirtualizedCount = 0;
    _state.SendInputMouseBlockedCount = 0;
    _state.SendInputKeyboardBlockedCount = 0;
    _state.MouseEventBlockedCount = 0;
    _state.PostMouseMessageBlockedCount = 0;
    _state.PostMouseMessagePassedCount = 0;
    _state.SendMouseMessageBlockedCount = 0;
    _state.SendMouseMessagePassedCount = 0;

    _state.RawKeyboardSanitizedCount = 0;
    _state.RawKeyboardPassedCount = 0;
    _state.RawMouseSanitizedCount = 0;
    _state.RawMousePartialPassedCount = 0;
    _state.RawMousePassedCount = 0;

    _state.WindowsHookKeyboardBlockedCount = 0;
    _state.WindowsHookKeyboardPassedCount = 0;
    _state.WindowsHookMouseBlockedCount = 0;
    _state.WindowsHookMousePassedCount = 0;

    _state.PolledInputFrameCount = 0;
    _state.PolledMouseFrameCount = 0;
    _state.PolledKeyboardFrameCount = 0;
    _state.ImGuiMouseDrawCursorForced = false;
    _state.SavedImGuiMouseDrawCursor = false;
    _state.ImGuiNoMouseCursorChangeForced = false;
    _state.SavedImGuiNoMouseCursorChange = false;

    _state.ExternalVirtualMouseFrameCount = 0;
    _state.ExternalVirtualMouseAuthoritativeFrameCount = 0;
    _state.ExternalGetCursorPosVirtualizedCount = 0;
    _state.ExternalVirtualMouseAbsoluteSuppressedCount = 0;
    _state.ExternalMouseDeltaEventCount = 0;
    _state.ExternalCursorRecenteringEventCount = 0;
    _state.ExternalRawInputSinkMessageCount = 0;
    _state.ExternalRawInputSinkPumpFrameCount = 0;

    _state.GameInputModule = nullptr;
    _state.WindowsGamingInputModule = nullptr;
    _state.XInputModule = nullptr;
    _state.DirectInputModule = nullptr;
    _state.DirectInputLegacyModule = nullptr;
    _state.GameInputModuleLoaded = false;
    _state.GameInputCreateExportFound = false;
    _state.GameInputCreateHookInstalled = false;
    _state.GameInputCreateHookAttempted = false;
    _state.GameInputInterfaceSeen = false;
    _state.WindowsGamingInputModuleLoaded = false;
    _state.GameInputLastCreateResult = S_OK;
    _state.GameInputCreateCallCount = 0;
    _state.GameInputCreateSucceededCount = 0;
    _state.GameInputCreateFailedCount = 0;

    _state.XInputModuleLoaded = false;
    _state.XInputGetStateHookInstalled = false;
    _state.XInputGetStateExHookInstalled = false;
    _state.XInputGetKeystrokeHookInstalled = false;
    _state.XInputSetStateHookInstalled = false;
    _state.XInputGetStateCallCount = 0;
    _state.XInputGetStateBlockedCount = 0;
    _state.XInputGetStatePassedCount = 0;
    _state.XInputGetKeystrokeCallCount = 0;
    _state.XInputGetKeystrokeBlockedCount = 0;
    _state.XInputGetKeystrokePassedCount = 0;
    _state.XInputSetStateCallCount = 0;
    _state.XInputSetStateBlockedCount = 0;
    _state.XInputSetStatePassedCount = 0;

    _state.DirectInputModuleLoaded = false;
    _state.DirectInputLegacyModuleLoaded = false;
    _state.DirectInput8CreateHookInstalled = false;
    _state.DirectInputCreateAHookInstalled = false;
    _state.DirectInputCreateWHookInstalled = false;
    _state.DirectInputCreateExHookInstalled = false;
    _state.DirectInputCreateDeviceAHookInstalled = false;
    _state.DirectInputCreateDeviceWHookInstalled = false;
    _state.DirectInputGetDeviceStateHookInstalled = false;
    _state.DirectInputGetDeviceDataHookInstalled = false;
    _state.DirectInputDeviceReleaseHookInstalled = false;
    _state.DirectInputKeyboardDeviceSeen = false;
    _state.DirectInputMouseDeviceSeen = false;
    _state.DirectInputOtherDeviceSeen = false;
    _state.DirectInputDeviceSlots = {};
    _state.HidHandleSlots = {};
    _state.HidMouseHandleSeen = false;
    _state.HidKeyboardHandleSeen = false;
    _state.HidGamepadHandleSeen = false;
    _state.HidOtherHandleSeen = false;
    _state.HidCreateFileCallCount = 0;
    _state.HidTrackedHandleCount = 0;
    _state.HidReadFileCallCount = 0;
    _state.HidReadFileBlockedCount = 0;
    _state.HidReadFilePassedCount = 0;
    _state.HidDeviceIoControlCallCount = 0;
    _state.HidDeviceIoControlBlockedCount = 0;
    _state.HidDeviceIoControlPassedCount = 0;
    _state.MouseMovePointsBlockedCount = 0;
    _state.DirectInputCreateCallCount = 0;
    _state.DirectInputCreateSucceededCount = 0;
    _state.DirectInputCreateFailedCount = 0;
    _state.DirectInputCreateDeviceCallCount = 0;
    _state.DirectInputCreateDeviceSucceededCount = 0;
    _state.DirectInputCreateDeviceFailedCount = 0;
    _state.DirectInputTrackedDeviceCount = 0;
    _state.DirectInputGetDeviceStateCallCount = 0;
    _state.DirectInputGetDeviceStateBlockedCount = 0;
    _state.DirectInputGetDeviceStatePassedCount = 0;
    _state.DirectInputGetDeviceDataCallCount = 0;
    _state.DirectInputGetDeviceDataBlockedCount = 0;
    _state.DirectInputGetDeviceDataPassedCount = 0;

    _state.SavedClipRect = {};
    _state.HasSavedClipRect = false;
    _state.SavedClipWasActive = false;

    _state.DeferredClipRect = {};
    _state.HasDeferredClipRect = false;
    _state.DeferredClipIsNull = false;

    _state.CursorClipReleasedForMenu = false;

    _state.MouseWheel = 0.0f;
    _state.TextInput.clear();
}

void Shutdown()
{
    std::unique_lock lock(_state.Mutex);

    RemoveWindowSubclass();
    ReleaseTrackedWindowsHooksLocked();
    RemoveExternalRawInputSinkLocked();
    RemoveExternalMouseHookLocked();
    RemoveDirectInputHooksLocked();
    RemoveXInputHooksLocked();
    RemoveGameInputHooksLocked();
    RemoveHooks();

    ResetStateAfterShutdown();
}

static void BeginFrameLocked(HWND targetHwnd, HWND inputHwnd, bool hasInputHwnd, bool isUwp)
{
    // Expected frame order:
    //   BeginFrame() validates game/input HWNDs, subclass/focus.
    //   Hooked WndProc/message/raw APIs accumulate state during message processing.
    //   FeedImGui() publishes the accumulated state.
    //   EndFrame() applies the menu block policy for the next frame and clears transients.

    if (targetHwnd != nullptr && (targetHwnd != _state.TargetHwnd || isUwp != _state.IsUwp))
        SetTargetWindow(targetHwnd, isUwp, _state.UseWndProcSubclass);

    if (hasInputHwnd && inputHwnd != _state.InputHwnd)
        SetInputWindow(inputHwnd, _state.UseWndProcSubclass, true);

    ValidateTargetWindowLocked();
    ValidateInputWindowLocked();
    ValidateWindowSubclassLocked();

    // Optional input APIs may be loaded after OptiInput initialization.
    UpdateGameInputIntegrationLocked();
    UpdateXInputIntegrationLocked();
    UpdateDirectInputIntegrationLocked();

    UpdateFocusState(_state.TargetHwnd);
    EnsureExternalRawInputSinkLocked();
    UpdateExternalMouseHookLocked();
    PumpExternalRawInputSinkLocked();
    PollInputFallbackLocked();
}

void BeginFrame(HWND targetHwnd, bool isUwp)
{
    std::unique_lock lock(_state.Mutex);

    if (!_state.Initialized)
        Initialize(targetHwnd, isUwp);

    BeginFrameLocked(targetHwnd, nullptr, false, isUwp);
}

void BeginFrame(HWND targetHwnd, HWND inputHwnd, bool isUwp)
{
    std::unique_lock lock(_state.Mutex);

    if (!_state.Initialized)
        Initialize(targetHwnd, inputHwnd, isUwp);

    BeginFrameLocked(targetHwnd, inputHwnd, inputHwnd != nullptr, isUwp);
}

void FeedImGui(bool menuVisible)
{
    std::unique_lock lock(_state.Mutex);

    ImGuiIO& io = ImGui::GetIO();

    // When not visible skip feeding input to ImGui and clear the event queue
    if (!menuVisible)
    {
        // This is for virtual mouse mode
        UpdateImGuiMouseDrawCursorLocked(io);

        io.ClearEventsQueue();
        io.ClearInputKeys();
        io.ClearInputMouse();

        return;
    }

    io.AddFocusEvent(_state.Focused);

    RefreshInputAcquisitionModeLocked();

    OPTIINPUT_LOG_VERBOSE("FeedImGui focused:{} mode:{} input:{} target:{} mouse=({}, {}) wheel:{} textChars:{} "
                          "keyCtrl:{} keyShift:{} keyAlt:{}",
                          _state.Focused ? 1 : 0, AcquisitionModeName(_state.AcquisitionMode),
                          static_cast<void*>(_state.InputHwnd), static_cast<void*>(_state.TargetHwnd),
                          _state.MouseClientPos.x, _state.MouseClientPos.y, _state.MouseWheel,
                          static_cast<unsigned>(_state.TextInput.size()), _state.Keys[VK_CONTROL].Down ? 1 : 0,
                          _state.Keys[VK_SHIFT].Down ? 1 : 0, _state.Keys[VK_MENU].Down ? 1 : 0);

    if (!_state.Focused)
    {
        UpdateImGuiMouseDrawCursorLocked(io);
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);

        io.KeyCtrl = false;
        io.KeyShift = false;
        io.KeyAlt = false;
        io.KeySuper = false;

        return;
    }

    auto AddKey = [&](ImGuiKey key, int vk)
    {
        if (vk >= 0 && vk < 256)
            io.AddKeyEvent(key, _state.Keys[vk].Down);
    };

    const bool polledCtrlDown = (RealGetAsyncKeyStateSafe(VK_CONTROL) & 0x8000) != 0 ||
                                (RealGetAsyncKeyStateSafe(VK_LCONTROL) & 0x8000) != 0 ||
                                (RealGetAsyncKeyStateSafe(VK_RCONTROL) & 0x8000) != 0;

    const bool polledShiftDown = (RealGetAsyncKeyStateSafe(VK_SHIFT) & 0x8000) != 0 ||
                                 (RealGetAsyncKeyStateSafe(VK_LSHIFT) & 0x8000) != 0 ||
                                 (RealGetAsyncKeyStateSafe(VK_RSHIFT) & 0x8000) != 0;

    const bool polledAltDown = (RealGetAsyncKeyStateSafe(VK_MENU) & 0x8000) != 0 ||
                               (RealGetAsyncKeyStateSafe(VK_LMENU) & 0x8000) != 0 ||
                               (RealGetAsyncKeyStateSafe(VK_RMENU) & 0x8000) != 0;

    const bool ctrlDown = _state.Keys[VK_CONTROL].Down || _state.Keys[VK_LCONTROL].Down ||
                          _state.Keys[VK_RCONTROL].Down || polledCtrlDown;

    const bool shiftDown =
        _state.Keys[VK_SHIFT].Down || _state.Keys[VK_LSHIFT].Down || _state.Keys[VK_RSHIFT].Down || polledShiftDown;

    const bool altDown =
        _state.Keys[VK_MENU].Down || _state.Keys[VK_LMENU].Down || _state.Keys[VK_RMENU].Down || polledAltDown;

    // In external/Remix virtual-mouse mode the real OS cursor may be hidden or
    // snapped to the game center. Ask ImGui to draw its own cursor only while
    // the menu actually owns the virtual mouse. Restore the previous setting
    // as soon as the menu closes, focus is lost, or external virtual mode ends.
    UpdateImGuiMouseDrawCursorLocked(io);

    // Legacy modifier fields for older ImGui versions.
    // Must be set before mouse button events.
    io.KeyCtrl = ctrlDown;
    io.KeyShift = shiftDown;
    io.KeyAlt = altDown;
    io.KeySuper = false;

    // Required for ImGui versions using the event input queue.
    // These must happen before mouse button events.
    io.AddKeyEvent(ImGuiMod_Ctrl, ctrlDown);
    io.AddKeyEvent(ImGuiMod_Shift, shiftDown);
    io.AddKeyEvent(ImGuiMod_Alt, altDown);
    io.AddKeyEvent(ImGuiMod_Super, false);

    // Also feed side-specific modifier keys before mouse buttons.
    AddKey(ImGuiKey_LeftCtrl, VK_LCONTROL);
    AddKey(ImGuiKey_RightCtrl, VK_RCONTROL);
    AddKey(ImGuiKey_LeftShift, VK_LSHIFT);
    AddKey(ImGuiKey_RightShift, VK_RSHIFT);
    AddKey(ImGuiKey_LeftAlt, VK_LMENU);
    AddKey(ImGuiKey_RightAlt, VK_RMENU);

    io.AddMousePosEvent(static_cast<float>(_state.MouseClientPos.x), static_cast<float>(_state.MouseClientPos.y));

    if (_state.MouseWheel != 0.0f)
        io.AddMouseWheelEvent(0.0f, _state.MouseWheel);

    io.AddMouseButtonEvent(0, _state.MouseButtons[0].Down);
    io.AddMouseButtonEvent(1, _state.MouseButtons[1].Down);
    io.AddMouseButtonEvent(2, _state.MouseButtons[2].Down);
    io.AddMouseButtonEvent(3, _state.MouseButtons[3].Down);
    io.AddMouseButtonEvent(4, _state.MouseButtons[4].Down);

    AddKey(ImGuiKey_Tab, VK_TAB);
    AddKey(ImGuiKey_LeftArrow, VK_LEFT);
    AddKey(ImGuiKey_RightArrow, VK_RIGHT);
    AddKey(ImGuiKey_UpArrow, VK_UP);
    AddKey(ImGuiKey_DownArrow, VK_DOWN);
    AddKey(ImGuiKey_PageUp, VK_PRIOR);
    AddKey(ImGuiKey_PageDown, VK_NEXT);
    AddKey(ImGuiKey_Home, VK_HOME);
    AddKey(ImGuiKey_End, VK_END);
    AddKey(ImGuiKey_Insert, VK_INSERT);
    AddKey(ImGuiKey_Delete, VK_DELETE);
    AddKey(ImGuiKey_Backspace, VK_BACK);
    AddKey(ImGuiKey_Space, VK_SPACE);
    AddKey(ImGuiKey_Enter, VK_RETURN);
    AddKey(ImGuiKey_Escape, VK_ESCAPE);

    for (int vk = 'A'; vk <= 'Z'; vk++)
    {
        io.AddKeyEvent(static_cast<ImGuiKey>(ImGuiKey_A + (vk - 'A')), _state.Keys[vk].Down);
    }

    for (int vk = '0'; vk <= '9'; vk++)
    {
        io.AddKeyEvent(static_cast<ImGuiKey>(ImGuiKey_0 + (vk - '0')), _state.Keys[vk].Down);
    }

    for (wchar_t ch : _state.TextInput)
        io.AddInputCharacterUTF16(ch);
}

void EndFrame(bool menuVisible)
{
    std::unique_lock lock(_state.Mutex);

    ApplyMenuVisibilityChangeLocked(menuVisible);
    LogInputHealthSnapshotLocked("EndFrame");
    ClearTransientState();
}

void SetMenuVisible(bool visible)
{
    std::unique_lock lock(_state.Mutex);

    ApplyMenuVisibilityChangeLocked(visible);
}

bool IsFocused()
{
    std::unique_lock lock(_state.Mutex);
    return _state.Focused;
}

bool IsKeyDown(int vk)
{
    std::unique_lock lock(_state.Mutex);

    if (vk < 0 || vk >= 256)
        return false;

    return _state.Keys[vk].Down;
}

bool IsKeyPressed(int vk)
{
    std::unique_lock lock(_state.Mutex);

    if (vk < 0 || vk >= 256)
        return false;

    return _state.Keys[vk].Pressed;
}

bool IsKeyReleased(int vk)
{
    std::unique_lock lock(_state.Mutex);

    if (vk < 0 || vk >= 256)
        return false;

    return _state.Keys[vk].Released;
}

int GetLastPressedKey()
{
    std::unique_lock lock(_state.Mutex);
    return _state.LastPressedKey;
}

bool IsMouseDown(int button)
{
    std::unique_lock lock(_state.Mutex);

    if (button < 0 || button >= static_cast<int>(_state.MouseButtons.size()))
        return false;

    return _state.MouseButtons[button].Down;
}

bool IsMousePressed(int button)
{
    std::unique_lock lock(_state.Mutex);

    if (button < 0 || button >= static_cast<int>(_state.MouseButtons.size()))
        return false;

    return _state.MouseButtons[button].Pressed;
}

bool IsMouseReleased(int button)
{
    std::unique_lock lock(_state.Mutex);

    if (button < 0 || button >= static_cast<int>(_state.MouseButtons.size()))
        return false;

    return _state.MouseButtons[button].Released;
}

float GetMouseWheel()
{
    std::unique_lock lock(_state.Mutex);
    return _state.MouseWheel;
}

POINT GetMouseScreenPos()
{
    std::unique_lock lock(_state.Mutex);
    return _state.MouseScreenPos;
}

bool ShouldBlockMouse()
{
    std::unique_lock lock(_state.Mutex);
    return _state.BlockMouse;
}

bool ShouldBlockKeyboard()
{
    std::unique_lock lock(_state.Mutex);
    return _state.BlockKeyboard;
}

bool ShouldBlockCursor()
{
    std::unique_lock lock(_state.Mutex);
    return _state.BlockCursor;
}

bool ShouldBlockVirtualKey(int vk)
{
    std::unique_lock lock(_state.Mutex);

    if (!ShouldApplyBlockingPolicyLocked())
        return false;

    if (IsMouseVirtualKey(vk))
        return _state.BlockMouse;

    return _state.BlockKeyboard;
}

DebugState GetDebugState()
{
    std::unique_lock lock(_state.Mutex);

    DebugState state {};

    state.TargetHwnd = _state.TargetHwnd;
    state.TargetRootHwnd = _state.TargetRootHwnd;
    state.InputHwnd = _state.InputHwnd;
    state.InputRootHwnd = _state.InputRootHwnd;
    state.RawMouseTargetHwnd = _state.RawMouseTargetHwnd;
    state.RawKeyboardTargetHwnd = _state.RawKeyboardTargetHwnd;

    state.TargetProcessId = _state.TargetProcessId;
    state.TargetThreadId = _state.TargetThreadId;
    state.InputProcessId = _state.InputProcessId;
    state.InputThreadId = _state.InputThreadId;
    state.CurrentProcessId = _state.CurrentProcessId;
    state.RawMouseFlags = _state.RawMouseFlags;
    state.RawKeyboardFlags = _state.RawKeyboardFlags;

    state.Initialized = _state.Initialized;
    state.HooksInstalled = _state.HooksInstalled;
    state.Focused = _state.Focused;

    state.MenuVisible = _state.MenuVisible;
    state.BlockMouse = _state.BlockMouse;
    state.BlockKeyboard = _state.BlockKeyboard;
    state.BlockCursor = _state.BlockCursor;

    state.IsUwp = _state.IsUwp;
    state.UseWndProcSubclass = _state.UseWndProcSubclass;
    state.WndProcSubclassed = _state.WndProcSubclassed;
    state.ExternalTargetProcess = _state.ExternalTargetProcess;
    state.HasExplicitInputHwnd = _state.HasExplicitInputHwnd;
    state.PolledInputActive = _state.PolledInputActive;
    state.PolledInputUsedThisFrame = _state.PolledInputUsedThisFrame;
    state.PolledMouseUsedThisFrame = _state.PolledMouseUsedThisFrame;
    state.PolledKeyboardUsedThisFrame = _state.PolledKeyboardUsedThisFrame;
    state.AcquisitionMode = _state.AcquisitionMode;
    state.ExternalVirtualMouseActive = _state.ExternalVirtualMouseActive;
    state.ExternalVirtualMouseUsedThisFrame = _state.ExternalVirtualMouseUsedThisFrame;
    state.ExternalVirtualMouseRelativeUsedThisFrame = _state.ExternalVirtualMouseRelativeUsedThisFrame;
    state.ExternalVirtualMouseAuthoritative = _state.ExternalVirtualMouseAuthoritative;
    state.ExternalGetCursorPosVirtualizedThisFrame = _state.ExternalGetCursorPosVirtualizedThisFrame;
    state.ExternalCursorRecenteringDetected = _state.ExternalCursorRecenteringDetected;
    state.ExternalLowLevelMouseHookInstalled = _state.ExternalLowLevelMouseHookInstalled;
    state.ExternalRawInputSinkRegistered = _state.ExternalRawInputSinkRegistered;
    state.ExternalRawInputSinkPumpUsedThisFrame = _state.ExternalRawInputSinkPumpUsedThisFrame;

    state.ReceivedWindowMessageThisFrame = _state.ReceivedWindowMessageThisFrame;
    state.ReceivedQueueMessageThisFrame = _state.ReceivedQueueMessageThisFrame;
    state.ReceivedRawInputThisFrame = _state.ReceivedRawInputThisFrame;
    state.ReceivedAnyInputThisFrame = _state.ReceivedAnyInputThisFrame;

    state.RawMouseRegistered = _state.RawMouseRegistered;
    state.RawKeyboardRegistered = _state.RawKeyboardRegistered;
    state.RawMouseNoLegacy = _state.RawMouseNoLegacy;
    state.RawKeyboardNoLegacy = _state.RawKeyboardNoLegacy;
    state.RawMouseInputSink = _state.RawMouseInputSink;
    state.RawKeyboardInputSink = _state.RawKeyboardInputSink;
    state.RawMouseCaptureMouse = _state.RawMouseCaptureMouse;

    state.WindowsHookTrackedCount = CountTrackedWindowsHooksLocked();

    state.GameInputModuleLoaded = _state.GameInputModuleLoaded;
    state.GameInputCreateExportFound = _state.GameInputCreateExportFound;
    state.GameInputCreateHookInstalled = _state.GameInputCreateHookInstalled;
    state.GameInputCreateHookAttempted = _state.GameInputCreateHookAttempted;
    state.GameInputInterfaceSeen = _state.GameInputInterfaceSeen;
    state.WindowsGamingInputModuleLoaded = _state.WindowsGamingInputModuleLoaded;
    state.XInputModuleLoaded = _state.XInputModuleLoaded;
    state.XInputGetStateHookInstalled = _state.XInputGetStateHookInstalled;
    state.XInputGetStateExHookInstalled = _state.XInputGetStateExHookInstalled;
    state.XInputGetKeystrokeHookInstalled = _state.XInputGetKeystrokeHookInstalled;
    state.XInputSetStateHookInstalled = _state.XInputSetStateHookInstalled;
    state.DirectInputModuleLoaded = _state.DirectInputModuleLoaded;
    state.DirectInputLegacyModuleLoaded = _state.DirectInputLegacyModuleLoaded;
    state.DirectInput8CreateHookInstalled = _state.DirectInput8CreateHookInstalled;
    state.DirectInputCreateAHookInstalled = _state.DirectInputCreateAHookInstalled;
    state.DirectInputCreateWHookInstalled = _state.DirectInputCreateWHookInstalled;
    state.DirectInputCreateExHookInstalled = _state.DirectInputCreateExHookInstalled;
    state.DirectInputCreateDeviceAHookInstalled = _state.DirectInputCreateDeviceAHookInstalled;
    state.DirectInputCreateDeviceWHookInstalled = _state.DirectInputCreateDeviceWHookInstalled;
    state.DirectInputGetDeviceStateHookInstalled = _state.DirectInputGetDeviceStateHookInstalled;
    state.DirectInputGetDeviceDataHookInstalled = _state.DirectInputGetDeviceDataHookInstalled;
    state.DirectInputDeviceReleaseHookInstalled = _state.DirectInputDeviceReleaseHookInstalled;
    state.DirectInputKeyboardDeviceSeen = _state.DirectInputKeyboardDeviceSeen;
    state.DirectInputMouseDeviceSeen = _state.DirectInputMouseDeviceSeen;
    state.DirectInputOtherDeviceSeen = _state.DirectInputOtherDeviceSeen;
    state.GameInputLastCreateResult = _state.GameInputLastCreateResult;
    state.GameInputCreateCallCount = _state.GameInputCreateCallCount;
    state.GameInputCreateSucceededCount = _state.GameInputCreateSucceededCount;
    state.GameInputCreateFailedCount = _state.GameInputCreateFailedCount;

    state.XInputGetStateCallCount = _state.XInputGetStateCallCount;
    state.XInputGetStateBlockedCount = _state.XInputGetStateBlockedCount;
    state.XInputGetStatePassedCount = _state.XInputGetStatePassedCount;
    state.XInputGetKeystrokeCallCount = _state.XInputGetKeystrokeCallCount;
    state.XInputGetKeystrokeBlockedCount = _state.XInputGetKeystrokeBlockedCount;
    state.XInputGetKeystrokePassedCount = _state.XInputGetKeystrokePassedCount;
    state.XInputSetStateCallCount = _state.XInputSetStateCallCount;
    state.XInputSetStateBlockedCount = _state.XInputSetStateBlockedCount;
    state.XInputSetStatePassedCount = _state.XInputSetStatePassedCount;

    state.DirectInputCreateCallCount = _state.DirectInputCreateCallCount;
    state.DirectInputCreateSucceededCount = _state.DirectInputCreateSucceededCount;
    state.DirectInputCreateFailedCount = _state.DirectInputCreateFailedCount;
    state.DirectInputCreateDeviceCallCount = _state.DirectInputCreateDeviceCallCount;
    state.DirectInputCreateDeviceSucceededCount = _state.DirectInputCreateDeviceSucceededCount;
    state.DirectInputCreateDeviceFailedCount = _state.DirectInputCreateDeviceFailedCount;
    state.DirectInputTrackedDeviceCount = _state.DirectInputTrackedDeviceCount;
    state.DirectInputGetDeviceStateCallCount = _state.DirectInputGetDeviceStateCallCount;
    state.DirectInputGetDeviceStateBlockedCount = _state.DirectInputGetDeviceStateBlockedCount;
    state.DirectInputGetDeviceStatePassedCount = _state.DirectInputGetDeviceStatePassedCount;
    state.DirectInputGetDeviceDataCallCount = _state.DirectInputGetDeviceDataCallCount;
    state.DirectInputGetDeviceDataBlockedCount = _state.DirectInputGetDeviceDataBlockedCount;
    state.DirectInputGetDeviceDataPassedCount = _state.DirectInputGetDeviceDataPassedCount;

    state.HidMouseHandleSeen = _state.HidMouseHandleSeen;
    state.HidKeyboardHandleSeen = _state.HidKeyboardHandleSeen;
    state.HidGamepadHandleSeen = _state.HidGamepadHandleSeen;
    state.HidOtherHandleSeen = _state.HidOtherHandleSeen;
    state.HidCreateFileCallCount = _state.HidCreateFileCallCount;
    state.HidTrackedHandleCount = _state.HidTrackedHandleCount;
    state.HidReadFileCallCount = _state.HidReadFileCallCount;
    state.HidReadFileBlockedCount = _state.HidReadFileBlockedCount;
    state.HidReadFilePassedCount = _state.HidReadFilePassedCount;
    state.HidDeviceIoControlCallCount = _state.HidDeviceIoControlCallCount;
    state.HidDeviceIoControlBlockedCount = _state.HidDeviceIoControlBlockedCount;
    state.HidDeviceIoControlPassedCount = _state.HidDeviceIoControlPassedCount;
    state.MouseMovePointsBlockedCount = _state.MouseMovePointsBlockedCount;

    state.RawKeyboardSanitizedCount = _state.RawKeyboardSanitizedCount;
    state.RawKeyboardPassedCount = _state.RawKeyboardPassedCount;
    state.RawMouseSanitizedCount = _state.RawMouseSanitizedCount;
    state.RawMousePartialPassedCount = _state.RawMousePartialPassedCount;
    state.RawMousePassedCount = _state.RawMousePassedCount;

    state.WindowsHookKeyboardBlockedCount = _state.WindowsHookKeyboardBlockedCount;
    state.WindowsHookKeyboardPassedCount = _state.WindowsHookKeyboardPassedCount;
    state.WindowsHookMouseBlockedCount = _state.WindowsHookMouseBlockedCount;
    state.WindowsHookMousePassedCount = _state.WindowsHookMousePassedCount;

    state.PolledInputFrameCount = _state.PolledInputFrameCount;
    state.PolledMouseFrameCount = _state.PolledMouseFrameCount;
    state.PolledKeyboardFrameCount = _state.PolledKeyboardFrameCount;
    state.ExternalVirtualMouseFrameCount = _state.ExternalVirtualMouseFrameCount;
    state.ExternalVirtualMouseAuthoritativeFrameCount = _state.ExternalVirtualMouseAuthoritativeFrameCount;
    state.ExternalGetCursorPosVirtualizedCount = _state.ExternalGetCursorPosVirtualizedCount;
    state.ExternalVirtualMouseAbsoluteSuppressedCount = _state.ExternalVirtualMouseAbsoluteSuppressedCount;
    state.ExternalMouseDeltaEventCount = _state.ExternalMouseDeltaEventCount;
    state.ExternalCursorRecenteringEventCount = _state.ExternalCursorRecenteringEventCount;
    state.ExternalRawInputSinkMessageCount = _state.ExternalRawInputSinkMessageCount;
    state.ExternalRawInputSinkPumpFrameCount = _state.ExternalRawInputSinkPumpFrameCount;
    state.ExternalVirtualMouseClient = _state.ExternalVirtualMouseClient;
    state.ExternalPendingMouseDeltaX = _state.ExternalPendingMouseDeltaX;
    state.ExternalPendingMouseDeltaY = _state.ExternalPendingMouseDeltaY;

    return state;
}

bool IsExternalVirtualMouseAuthoritative()
{
    std::unique_lock lock(_state.Mutex);
    return IsExternalVirtualMouseAuthoritativeLocked();
}

InputAcquisitionMode GetInputAcquisitionMode()
{
    std::unique_lock lock(_state.Mutex);
    RefreshInputAcquisitionModeLocked();
    return _state.AcquisitionMode;
}

void ResetMenuInputTransientState()
{
    _state.ExternalPendingMouseDeltaX = 0;
    _state.ExternalPendingMouseDeltaY = 0;
    _state.MouseWheel = 0.0f;
    _state.TextInput.clear();

    for (auto& key : _state.Keys)
    {
        key.Pressed = false;
        key.Released = false;
    }

    for (auto& button : _state.MouseButtons)
    {
        button.Pressed = false;
        button.Released = false;
    }
}

} // namespace OptiInput
