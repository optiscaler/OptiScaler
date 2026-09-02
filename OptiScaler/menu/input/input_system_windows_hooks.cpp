#include "pch.h"
#include "input_system_internal.h"

#include <array>

namespace OptiInput
{
#define OPTI_WINDOWS_HOOK_PROXY(index)                                                                                 \
    static LRESULT CALLBACK WindowsHookProxy##index(int code, WPARAM wParam, LPARAM lParam)                            \
    {                                                                                                                  \
        return InvokeWindowsHookProxy(index, code, wParam, lParam);                                                    \
    }

OPTI_WINDOWS_HOOK_PROXY(0)
OPTI_WINDOWS_HOOK_PROXY(1)
OPTI_WINDOWS_HOOK_PROXY(2)
OPTI_WINDOWS_HOOK_PROXY(3)
OPTI_WINDOWS_HOOK_PROXY(4)
OPTI_WINDOWS_HOOK_PROXY(5)
OPTI_WINDOWS_HOOK_PROXY(6)
OPTI_WINDOWS_HOOK_PROXY(7)
OPTI_WINDOWS_HOOK_PROXY(8)
OPTI_WINDOWS_HOOK_PROXY(9)
OPTI_WINDOWS_HOOK_PROXY(10)
OPTI_WINDOWS_HOOK_PROXY(11)
OPTI_WINDOWS_HOOK_PROXY(12)
OPTI_WINDOWS_HOOK_PROXY(13)
OPTI_WINDOWS_HOOK_PROXY(14)
OPTI_WINDOWS_HOOK_PROXY(15)
OPTI_WINDOWS_HOOK_PROXY(16)
OPTI_WINDOWS_HOOK_PROXY(17)
OPTI_WINDOWS_HOOK_PROXY(18)
OPTI_WINDOWS_HOOK_PROXY(19)
OPTI_WINDOWS_HOOK_PROXY(20)
OPTI_WINDOWS_HOOK_PROXY(21)
OPTI_WINDOWS_HOOK_PROXY(22)
OPTI_WINDOWS_HOOK_PROXY(23)
OPTI_WINDOWS_HOOK_PROXY(24)
OPTI_WINDOWS_HOOK_PROXY(25)
OPTI_WINDOWS_HOOK_PROXY(26)
OPTI_WINDOWS_HOOK_PROXY(27)
OPTI_WINDOWS_HOOK_PROXY(28)
OPTI_WINDOWS_HOOK_PROXY(29)
OPTI_WINDOWS_HOOK_PROXY(30)
OPTI_WINDOWS_HOOK_PROXY(31)

#undef OPTI_WINDOWS_HOOK_PROXY

static const std::array<HOOKPROC, MaxTrackedWindowsHooks> windowsHookProxies = {
    WindowsHookProxy0,  WindowsHookProxy1,  WindowsHookProxy2,  WindowsHookProxy3,  WindowsHookProxy4,
    WindowsHookProxy5,  WindowsHookProxy6,  WindowsHookProxy7,  WindowsHookProxy8,  WindowsHookProxy9,
    WindowsHookProxy10, WindowsHookProxy11, WindowsHookProxy12, WindowsHookProxy13, WindowsHookProxy14,
    WindowsHookProxy15, WindowsHookProxy16, WindowsHookProxy17, WindowsHookProxy18, WindowsHookProxy19,
    WindowsHookProxy20, WindowsHookProxy21, WindowsHookProxy22, WindowsHookProxy23, WindowsHookProxy24,
    WindowsHookProxy25, WindowsHookProxy26, WindowsHookProxy27, WindowsHookProxy28, WindowsHookProxy29,
    WindowsHookProxy30, WindowsHookProxy31
};

HOOKPROC GetWindowsHookProxyProc(std::size_t slotIndex)
{
    if (slotIndex >= windowsHookProxies.size())
        return nullptr;

    return windowsHookProxies[slotIndex];
}

bool IsKeyboardWindowsHookType(int hookType) { return hookType == WH_KEYBOARD || hookType == WH_KEYBOARD_LL; }

bool IsMouseWindowsHookType(int hookType) { return hookType == WH_MOUSE || hookType == WH_MOUSE_LL; }

bool IsTrackedWindowsHookType(int hookType)
{
    return IsKeyboardWindowsHookType(hookType) || IsMouseWindowsHookType(hookType);
}

bool IsThreadInCurrentProcess(DWORD threadId)
{
    if (threadId == 0 || threadId == GetCurrentThreadId())
        return true;

    HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, threadId);

    if (thread == nullptr)
        return false;

    const DWORD processId = GetProcessIdOfThread(thread);
    CloseHandle(thread);

    return processId == GetCurrentProcessId();
}

bool ShouldInterceptWindowsHookInstall(int hookType, HOOKPROC proc, DWORD threadId)
{
    if (proc == nullptr)
    {
        OPTIINPUT_LOG_VERBOSE("skip SetWindowsHookEx hookType:{} null proc threadId:{}", hookType, threadId);
        return false;
    }

    if (!IsTrackedWindowsHookType(hookType))
        return false;

    // Classic WH_KEYBOARD / WH_MOUSE hooks can be global. Do not proxy global
    // classic hooks because their callbacks may be injected into other processes.
    if ((hookType == WH_KEYBOARD || hookType == WH_MOUSE) && (threadId == 0 || !IsThreadInCurrentProcess(threadId)))
    {
        LOG_DEBUG("skip classic global/foreign hook proxy hookType:{} threadId:{}", hookType, threadId);
        return false;
    }

    return true;
}

HINSTANCE GetWindowsHookProxyModule()
{
    static HINSTANCE module = []() -> HINSTANCE
    {
        HINSTANCE resolvedModule = nullptr;

        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&_state), &resolvedModule);

        return resolvedModule;
    }();

    return module;
}

HINSTANCE GetWindowsHookInstallModule(int hookType, DWORD threadId, HINSTANCE originalModule)
{
    if (hookType == WH_KEYBOARD_LL || hookType == WH_MOUSE_LL)
    {
        HINSTANCE proxyModule = GetWindowsHookProxyModule();
        return proxyModule != nullptr ? proxyModule : originalModule;
    }

    // Thread hooks installed inside the current process can use a null module.
    if ((hookType == WH_KEYBOARD || hookType == WH_MOUSE) && threadId != 0 && IsThreadInCurrentProcess(threadId))
        return nullptr;

    return originalModule;
}

int AllocateWindowsHookSlotLocked(int hookType, HOOKPROC proc, HINSTANCE module, DWORD threadId)
{
    for (std::size_t i = 0; i < _state.WindowsHookSlots.size(); i++)
    {
        WindowsHookSlot& slot = _state.WindowsHookSlots[i];

        if (slot.InUse)
            continue;

        slot = {};
        slot.InUse = true;
        slot.HookType = hookType;
        slot.OriginalProc = proc;
        slot.ThreadId = threadId;
        slot.Module = module;
        LOG_DEBUG("allocated windows hook slot:{} type:{} proc:{} module:{} threadId:{}", static_cast<unsigned>(i),
                  hookType, reinterpret_cast<std::uintptr_t>(proc), static_cast<void*>(module), threadId);
        return static_cast<int>(i);
    }

    return -1;
}

void ClearWindowsHookSlotLocked(std::size_t slotIndex)
{
    if (slotIndex >= _state.WindowsHookSlots.size())
        return;

    _state.WindowsHookSlots[slotIndex] = {};
}

void ClearWindowsHookSlotByHandleLocked(HHOOK hook)
{
    if (hook == nullptr)
        return;

    for (std::size_t i = 0; i < _state.WindowsHookSlots.size(); i++)
    {
        if (_state.WindowsHookSlots[i].InUse && _state.WindowsHookSlots[i].Hook == hook)
        {
            LOG_DEBUG("clearing windows hook slot by handle slot:{} hook:{}", static_cast<unsigned>(i),
                      static_cast<void*>(hook));
            ClearWindowsHookSlotLocked(i);
            return;
        }
    }
}

std::uint32_t CountTrackedWindowsHooksLocked()
{
    std::uint32_t count = 0;

    for (const WindowsHookSlot& slot : _state.WindowsHookSlots)
    {
        if (slot.InUse && slot.Hook != nullptr)
            count++;
    }

    return count;
}

bool IsWindowsHookMouseDownMessage(UINT message)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDBLCLK:
        return true;

    default:
        return false;
    }
}

bool IsWindowsHookMouseUpMessage(UINT message)
{
    switch (message)
    {
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
        return true;

    default:
        return false;
    }
}

int WindowsHookMouseMessageToButton(int hookType, WPARAM wParam, LPARAM lParam)
{
    const UINT message = static_cast<UINT>(wParam);

    switch (message)
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
        DWORD mouseData = 0;

        if (hookType == WH_MOUSE_LL)
        {
            const MSLLHOOKSTRUCT* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);

            if (mouse != nullptr)
                mouseData = mouse->mouseData;
        }
        else if (hookType == WH_MOUSE)
        {
            const MOUSEHOOKSTRUCTEX* mouse = reinterpret_cast<const MOUSEHOOKSTRUCTEX*>(lParam);

            if (mouse != nullptr)
                mouseData = mouse->mouseData;
        }

        const WORD xButton = HIWORD(mouseData);

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

bool ShouldBlockWindowsKeyboardHookCallbackLocked(WindowsHookSlot& slot, int code, WPARAM wParam, LPARAM lParam)
{
    if (code < 0 || !ShouldBlockKeyboardInputLocked())
        return false;

    int vk = 0;
    bool released = false;

    if (slot.HookType == WH_KEYBOARD_LL)
    {
        const KBDLLHOOKSTRUCT* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);

        if (keyboard == nullptr)
            return true;

        vk = static_cast<int>(keyboard->vkCode);
        released = (keyboard->flags & LLKHF_UP) != 0;
    }
    else
    {
        vk = static_cast<int>(wParam);
        released = (static_cast<ULONG_PTR>(lParam) & 0x80000000UL) != 0;
    }

    if (vk <= 0 || vk >= 256)
        return true;

    if (!released)
    {
        // Do not turn an auto-repeat from a key held before menu-open into a blocked press.
        // The game hook already saw that original down and is therefore owed the matching up.
        if (!_state.Keys[vk].Down)
            _state.WindowsHookKeyboardBlockedDown[vk] = true;

        return true;
    }

    const bool wasBlockedDown = _state.WindowsHookKeyboardBlockedDown[vk];
    _state.WindowsHookKeyboardBlockedDown[vk] = false;

    // If the game hook saw the key-down before the menu opened, let it see the
    // matching key-up so its own state cannot get stuck.
    return wasBlockedDown;
}

bool ShouldBlockWindowsMouseHookCallbackLocked(WindowsHookSlot& slot, int code, WPARAM wParam, LPARAM lParam)
{
    if (code < 0 || !ShouldBlockMouseInputLocked())
        return false;

    const UINT message = static_cast<UINT>(wParam);

    if (message == WM_MOUSEMOVE || message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
        return true;

    if (IsWindowsHookMouseDownMessage(message))
    {
        const int button = WindowsHookMouseMessageToButton(slot.HookType, wParam, lParam);

        if (button >= 0 && button < static_cast<int>(_state.WindowsHookMouseBlockedDown.size()))
            _state.WindowsHookMouseBlockedDown[button] = true;

        return true;
    }

    if (IsWindowsHookMouseUpMessage(message))
    {
        const int button = WindowsHookMouseMessageToButton(slot.HookType, wParam, lParam);

        if (button < 0 || button >= static_cast<int>(_state.WindowsHookMouseBlockedDown.size()))
            return true;

        const bool wasBlockedDown = _state.WindowsHookMouseBlockedDown[button];
        _state.WindowsHookMouseBlockedDown[button] = false;

        // If the game hook saw the button-down before the menu opened, let it
        // see the matching button-up so its own state cannot get stuck.
        return wasBlockedDown;
    }

    // Conservative default for mouse hook notifications while the overlay owns input.
    return true;
}

bool ShouldBlockWindowsHookCallbackLocked(WindowsHookSlot& slot, int code, WPARAM wParam, LPARAM lParam)
{
    if (IsKeyboardWindowsHookType(slot.HookType))
        return ShouldBlockWindowsKeyboardHookCallbackLocked(slot, code, wParam, lParam);

    if (IsMouseWindowsHookType(slot.HookType))
        return ShouldBlockWindowsMouseHookCallbackLocked(slot, code, wParam, lParam);

    return false;
}

LRESULT CALLBACK InvokeWindowsHookProxy(std::size_t slotIndex, int code, WPARAM wParam, LPARAM lParam)
{
    HHOOK hook = nullptr;
    HOOKPROC originalProc = nullptr;
    int hookType = 0;
    bool shouldBlock = false;
    bool keyboardHook = false;
    bool mouseHook = false;

    {
        std::unique_lock lock(_state.Mutex);

        if (slotIndex >= _state.WindowsHookSlots.size())
            return CallNextHookEx(nullptr, code, wParam, lParam);

        WindowsHookSlot& slot = _state.WindowsHookSlots[slotIndex];

        if (!slot.InUse)
            return CallNextHookEx(nullptr, code, wParam, lParam);

        hook = slot.Hook;
        originalProc = slot.OriginalProc;
        hookType = slot.HookType;
        keyboardHook = IsKeyboardWindowsHookType(slot.HookType);
        mouseHook = IsMouseWindowsHookType(slot.HookType);
        shouldBlock = ShouldBlockWindowsHookCallbackLocked(slot, code, wParam, lParam);

        if (code >= 0)
        {
            OPTIINPUT_LOG_VERBOSE("windows hook callback slot:{} type:{} code:{} wParam:{:#x} block:{} menu:{}",
                                  static_cast<unsigned>(slotIndex), slot.HookType, code, static_cast<UINT64>(wParam),
                                  shouldBlock ? 1 : 0, _state.MenuVisible ? 1 : 0);
            if (keyboardHook)
            {
                if (shouldBlock)
                    _state.WindowsHookKeyboardBlockedCount++;
                else
                    _state.WindowsHookKeyboardPassedCount++;
            }
            else if (mouseHook)
            {
                if (shouldBlock)
                    _state.WindowsHookMouseBlockedCount++;
                else
                    _state.WindowsHookMousePassedCount++;
            }
        }
    }

    if (shouldBlock)
    {
        // Low-level hooks run before Windows processes the input. Returning non-zero from
        // WH_MOUSE_LL/WH_KEYBOARD_LL would discard the event system-wide, including the
        // physical cursor/keyboard state used by our own polling fallback. Keep the event
        // moving through the system hook chain while still bypassing the intercepted proc.
        if (hookType == WH_MOUSE_LL || hookType == WH_KEYBOARD_LL)
            return CallNextHookEx(hook, code, wParam, lParam);

        return 1;
    }

    if (originalProc == nullptr)
        return CallNextHookEx(hook, code, wParam, lParam);

    return originalProc(code, wParam, lParam);
}

namespace
{
bool ShouldUseExternalMouseHookLocked()
{
    return _state.Initialized && _state.Focused && _state.ExternalTargetProcess && _state.InputHwnd == nullptr &&
           !_state.ExternalRawInputSinkRegistered && _state.TargetHwnd != nullptr && IsWindow(_state.TargetHwnd);
}

bool GetTargetCenterScreenLocked(POINT* centerScreen)
{
    if (centerScreen == nullptr || _state.TargetHwnd == nullptr || !IsWindow(_state.TargetHwnd))
        return false;

    RECT clientRect {};
    if (!GetClientRect(_state.TargetHwnd, &clientRect))
        return false;

    POINT center {};
    center.x = (clientRect.left + clientRect.right) / 2;
    center.y = (clientRect.top + clientRect.bottom) / 2;

    if (!ClientToScreen(_state.TargetHwnd, &center))
        return false;

    *centerScreen = center;
    return true;
}

LONG AbsLong(LONG value) { return value < 0 ? -value : value; }

bool IsNearPoint(const POINT& a, const POINT& b, LONG threshold)
{
    return AbsLong(a.x - b.x) <= threshold && AbsLong(a.y - b.y) <= threshold;
}

void RecordExternalMouseMoveLocked(const POINT& point, DWORD flags)
{
    if (!ShouldUseExternalMouseHookLocked())
    {
        _state.ExternalLastMouseHookScreenValid = false;
        return;
    }

    POINT centerScreen {};
    const bool haveCenter = GetTargetCenterScreenLocked(&centerScreen);
    const bool injected = (flags & LLMHF_INJECTED) != 0;
    const bool currentNearCenter = haveCenter && IsNearPoint(point, centerScreen, 3);

    if (!_state.ExternalLastMouseHookScreenValid)
    {
        _state.ExternalLastMouseHookScreen = point;
        _state.ExternalLastMouseHookScreenValid = true;
        return;
    }

    const POINT previous = _state.ExternalLastMouseHookScreen;
    const LONG deltaX = point.x - previous.x;
    const LONG deltaY = point.y - previous.y;
    const bool previousNearCenter = haveCenter && IsNearPoint(previous, centerScreen, 3);

    const bool looksLikeRecenter =
        currentNearCenter && (injected || (!previousNearCenter && (AbsLong(deltaX) > 3 || AbsLong(deltaY) > 3)));

    _state.ExternalLastMouseHookScreen = point;

    if (looksLikeRecenter)
    {
        _state.ExternalCursorRecenteringDetected = true;
        _state.ExternalCursorRecenteringEventCount++;
        OPTIINPUT_LOG_VERBOSE("external virtual mouse ignored recenter point=({}, {}) center=({}, {}) injected:{}",
                              point.x, point.y, centerScreen.x, centerScreen.y, injected ? 1 : 0);
        return;
    }

    if (deltaX == 0 && deltaY == 0)
        return;

    _state.ExternalPendingMouseDeltaX += deltaX;
    _state.ExternalPendingMouseDeltaY += deltaY;
    _state.ExternalMouseDeltaEventCount++;
    _state.ExternalVirtualMouseActive = true;
}
} // namespace

LRESULT CALLBACK ExternalLowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && lParam != 0)
    {
        const MSLLHOOKSTRUCT* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);

        if (mouse != nullptr && wParam == WM_MOUSEMOVE)
        {
            std::unique_lock lock(_state.Mutex);
            RecordExternalMouseMoveLocked(mouse->pt, mouse->flags);
        }
    }

    return CallNextHookEx(_state.ExternalLowLevelMouseHook, code, wParam, lParam);
}

void UpdateExternalMouseHookLocked()
{
    const bool shouldInstall = ShouldUseExternalMouseHookLocked();

    if (!shouldInstall)
    {
        RemoveExternalMouseHookLocked();
        return;
    }

    if (_state.ExternalLowLevelMouseHook != nullptr)
    {
        _state.ExternalLowLevelMouseHookInstalled = true;
        return;
    }

    if (o_SetWindowsHookExW == nullptr)
        return;

    HINSTANCE module = GetWindowsHookProxyModule();
    if (module == nullptr)
        module = GetModuleHandleW(nullptr);

    ScopedHookBypass bypass;
    HHOOK hook = o_SetWindowsHookExW(WH_MOUSE_LL, ExternalLowLevelMouseProc, module, 0);

    if (hook == nullptr)
    {
        LOG_WARN("external low-level mouse hook install failed error:{}", GetLastError());
        _state.ExternalLowLevelMouseHookInstalled = false;
        return;
    }

    _state.ExternalLowLevelMouseHook = hook;
    _state.ExternalLowLevelMouseHookInstalled = true;
    _state.ExternalLastMouseHookScreenValid = false;

    LOG_INFO("external low-level mouse hook installed hook:{} target:{} targetPid:{}", static_cast<void*>(hook),
             static_cast<void*>(_state.TargetHwnd), _state.TargetProcessId);
}

bool RemoveExternalMouseHookLocked()
{
    HHOOK hook = _state.ExternalLowLevelMouseHook;

    if (hook == nullptr)
    {
        _state.ExternalLowLevelMouseHookInstalled = false;
        _state.ExternalLastMouseHookScreenValid = false;
        _state.ExternalPendingMouseDeltaX = 0;
        _state.ExternalPendingMouseDeltaY = 0;
        return true;
    }

    if (o_UnhookWindowsHookEx == nullptr)
    {
        LOG_WARN("external low-level mouse hook removal deferred because UnhookWindowsHookEx is unavailable hook:{}",
                 static_cast<void*>(hook));
        return false;
    }

    {
        ScopedHookBypass bypass;
        if (!o_UnhookWindowsHookEx(hook))
        {
            LOG_WARN("external low-level mouse hook removal failed hook:{} error:{}", static_cast<void*>(hook),
                     GetLastError());
            return false;
        }
    }

    _state.ExternalLowLevelMouseHook = nullptr;
    _state.ExternalLowLevelMouseHookInstalled = false;
    _state.ExternalLastMouseHookScreenValid = false;
    _state.ExternalPendingMouseDeltaX = 0;
    _state.ExternalPendingMouseDeltaY = 0;

    LOG_INFO("external low-level mouse hook removed hook:{}", static_cast<void*>(hook));
    return true;
}

bool ReleaseTrackedWindowsHooksLocked()
{
    bool allRemoved = true;

    for (WindowsHookSlot& slot : _state.WindowsHookSlots)
    {
        if (!slot.InUse || slot.Hook == nullptr)
        {
            slot = {};
            continue;
        }

        if (o_UnhookWindowsHookEx == nullptr)
        {
            allRemoved = false;
            continue;
        }

        const HHOOK hook = slot.Hook;
        BOOL removed = FALSE;

        {
            ScopedHookBypass bypass;
            removed = o_UnhookWindowsHookEx(hook);
        }

        if (!removed)
        {
            LOG_WARN("tracked windows hook removal failed hook:{} type:{} error:{}", static_cast<void*>(hook),
                     slot.HookType, GetLastError());
            allRemoved = false;
            continue;
        }

        slot = {};
    }

    _state.WindowsHookKeyboardBlockedDown = {};
    _state.WindowsHookMouseBlockedDown = {};
    return allRemoved;
}

HHOOK WINAPI hkSetWindowsHookExA(int hookType, HOOKPROC proc, HINSTANCE module, DWORD threadId)
{
    if (bypassHookDepth > 0 || !ShouldInterceptWindowsHookInstall(hookType, proc, threadId))
        return o_SetWindowsHookExA(hookType, proc, module, threadId);

    LOG_DEBUG("intercept SetWindowsHookExA type:{} proc:{} module:{} threadId:{}", hookType,
              reinterpret_cast<std::uintptr_t>(proc), static_cast<void*>(module), threadId);

    int slotIndex = -1;
    HOOKPROC proxyProc = nullptr;
    const HINSTANCE installModule = GetWindowsHookInstallModule(hookType, threadId, module);

    {
        std::unique_lock lock(_state.Mutex);

        slotIndex = AllocateWindowsHookSlotLocked(hookType, proc, module, threadId);

        if (slotIndex >= 0)
            proxyProc = windowsHookProxies[static_cast<std::size_t>(slotIndex)];
    }

    if (slotIndex < 0 || proxyProc == nullptr)
    {
        LOG_WARN("no windows hook slot available for SetWindowsHookExA type:{} threadId:{}", hookType, threadId);
        return o_SetWindowsHookExA(hookType, proc, module, threadId);
    }

    const HHOOK hook = o_SetWindowsHookExA(hookType, proxyProc, installModule, threadId);
    LOG_DEBUG("SetWindowsHookExA proxy result type:{} slot:{} hook:{} proxyProc:{} installModule:{}", hookType,
              slotIndex, static_cast<void*>(hook), reinterpret_cast<std::uintptr_t>(proxyProc),
              static_cast<void*>(installModule));

    {
        std::unique_lock lock(_state.Mutex);

        if (hook == nullptr)
        {
            ClearWindowsHookSlotLocked(static_cast<std::size_t>(slotIndex));
        }
        else
        {
            WindowsHookSlot& slot = _state.WindowsHookSlots[static_cast<std::size_t>(slotIndex)];
            slot.Hook = hook;
        }
    }

    return hook;
}

HHOOK WINAPI hkSetWindowsHookExW(int hookType, HOOKPROC proc, HINSTANCE module, DWORD threadId)
{
    if (bypassHookDepth > 0 || !ShouldInterceptWindowsHookInstall(hookType, proc, threadId))
        return o_SetWindowsHookExW(hookType, proc, module, threadId);

    LOG_DEBUG("intercept SetWindowsHookExW type:{} proc:{} module:{} threadId:{}", hookType,
              reinterpret_cast<std::uintptr_t>(proc), static_cast<void*>(module), threadId);

    int slotIndex = -1;
    HOOKPROC proxyProc = nullptr;
    const HINSTANCE installModule = GetWindowsHookInstallModule(hookType, threadId, module);

    {
        std::unique_lock lock(_state.Mutex);

        slotIndex = AllocateWindowsHookSlotLocked(hookType, proc, module, threadId);

        if (slotIndex >= 0)
            proxyProc = windowsHookProxies[static_cast<std::size_t>(slotIndex)];
    }

    if (slotIndex < 0 || proxyProc == nullptr)
    {
        LOG_WARN("no windows hook slot available for SetWindowsHookExW type:{} threadId:{}", hookType, threadId);
        return o_SetWindowsHookExW(hookType, proc, module, threadId);
    }

    const HHOOK hook = o_SetWindowsHookExW(hookType, proxyProc, installModule, threadId);
    LOG_DEBUG("SetWindowsHookExW proxy result type:{} slot:{} hook:{} proxyProc:{} installModule:{}", hookType,
              slotIndex, static_cast<void*>(hook), reinterpret_cast<std::uintptr_t>(proxyProc),
              static_cast<void*>(installModule));

    {
        std::unique_lock lock(_state.Mutex);

        if (hook == nullptr)
        {
            ClearWindowsHookSlotLocked(static_cast<std::size_t>(slotIndex));
        }
        else
        {
            WindowsHookSlot& slot = _state.WindowsHookSlots[static_cast<std::size_t>(slotIndex)];
            slot.Hook = hook;
        }
    }

    return hook;
}

BOOL WINAPI hkUnhookWindowsHookEx(HHOOK hook)
{
    const BOOL result = o_UnhookWindowsHookEx(hook);

    if (!result)
    {
        LOG_WARN("UnhookWindowsHookEx failed hook:{}", static_cast<void*>(hook));
        return result;
    }

    LOG_DEBUG("UnhookWindowsHookEx succeeded hook:{}", static_cast<void*>(hook));
    std::unique_lock lock(_state.Mutex);
    ClearWindowsHookSlotByHandleLocked(hook);

    return result;
}

} // namespace OptiInput
