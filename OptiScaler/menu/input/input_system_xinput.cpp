#include "pch.h"
#include "input_system_internal.h"

#include <detours/detours.h>

namespace OptiInput
{
namespace
{
constexpr const wchar_t* XInputModuleNames[] = {
    L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll", L"xinput1_2.dll", L"xinput1_1.dll",
};

constexpr char XInputGetStateExportName[] = "XInputGetState";
constexpr char XInputGetKeystrokeExportName[] = "XInputGetKeystroke";
constexpr char XInputSetStateExportName[] = "XInputSetState";

HMODULE FindLoadedXInputModule()
{
    for (const wchar_t* moduleName : XInputModuleNames)
    {
        HMODULE module = GetModuleHandleW(moduleName);

        if (module != nullptr)
            return module;
    }

    return nullptr;
}

bool ShouldBlockXInputLocked()
{
    return _state.Initialized && (ShouldBlockKeyboardInputLocked() || ShouldBlockMouseInputLocked());
}

void FillNeutralXInputState(XINPUT_STATE* state)
{
    if (state == nullptr)
        return;

    const DWORD packetNumber = state->dwPacketNumber;
    *state = {};
    state->dwPacketNumber = packetNumber;
}

void ClearXInputHookPointersLocked()
{
    o_XInputGetState = nullptr;
    o_XInputGetStateEx = nullptr;
    o_XInputGetKeystroke = nullptr;
    o_XInputSetState = nullptr;

    _state.XInputGetStateHookInstalled = false;
    _state.XInputGetStateExHookInstalled = false;
    _state.XInputGetKeystrokeHookInstalled = false;
    _state.XInputSetStateHookInstalled = false;
}

bool ResolveXInputExportsLocked(HMODULE module)
{
    if (module == nullptr)
        return false;

    if (o_XInputGetState == nullptr)
        o_XInputGetState = reinterpret_cast<XInputGetState_t>(GetProcAddress(module, XInputGetStateExportName));

    if (o_XInputGetStateEx == nullptr)
        o_XInputGetStateEx = reinterpret_cast<XInputGetState_t>(GetProcAddress(module, MAKEINTRESOURCEA(100)));

    if (o_XInputGetKeystroke == nullptr)
        o_XInputGetKeystroke =
            reinterpret_cast<XInputGetKeystroke_t>(GetProcAddress(module, XInputGetKeystrokeExportName));

    if (o_XInputSetState == nullptr)
        o_XInputSetState = reinterpret_cast<XInputSetState_t>(GetProcAddress(module, XInputSetStateExportName));

    return o_XInputGetState != nullptr || o_XInputGetStateEx != nullptr || o_XInputGetKeystroke != nullptr ||
           o_XInputSetState != nullptr;
}
} // namespace

void UpdateXInputIntegrationLocked()
{
    if (_state.XInputGetStateHookInstalled || _state.XInputGetStateExHookInstalled ||
        _state.XInputGetKeystrokeHookInstalled || _state.XInputSetStateHookInstalled)
    {
        return;
    }

    HMODULE module = FindLoadedXInputModule();
    _state.XInputModule = module;
    _state.XInputModuleLoaded = module != nullptr;

    if (module == nullptr)
        return;

    if (!ResolveXInputExportsLocked(module))
    {
        LOG_WARN("XInput module loaded but no supported exports found module:{}", static_cast<void*>(module));
        return;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_XInputGetState != nullptr)
        DetourAttach(reinterpret_cast<PVOID*>(&o_XInputGetState), hkXInputGetState);

    if (o_XInputGetStateEx != nullptr)
        DetourAttach(reinterpret_cast<PVOID*>(&o_XInputGetStateEx), hkXInputGetStateEx);

    if (o_XInputGetKeystroke != nullptr)
        DetourAttach(reinterpret_cast<PVOID*>(&o_XInputGetKeystroke), hkXInputGetKeystroke);

    if (o_XInputSetState != nullptr)
        DetourAttach(reinterpret_cast<PVOID*>(&o_XInputSetState), hkXInputSetState);

    const LONG result = DetourTransactionCommit();

    if (result != NO_ERROR)
    {
        LOG_ERROR("XInput hook installation failed result:{}", result);
        ClearXInputHookPointersLocked();
        return;
    }

    _state.XInputGetStateHookInstalled = o_XInputGetState != nullptr;
    _state.XInputGetStateExHookInstalled = o_XInputGetStateEx != nullptr;
    _state.XInputGetKeystrokeHookInstalled = o_XInputGetKeystroke != nullptr;
    _state.XInputSetStateHookInstalled = o_XInputSetState != nullptr;

    LOG_INFO("XInput hooks installed module:{} getState:{} getStateEx:{} getKeystroke:{} setState:{}",
             static_cast<void*>(module), _state.XInputGetStateHookInstalled ? 1 : 0,
             _state.XInputGetStateExHookInstalled ? 1 : 0, _state.XInputGetKeystrokeHookInstalled ? 1 : 0,
             _state.XInputSetStateHookInstalled ? 1 : 0);
}

bool RemoveXInputHooksLocked()
{
    if (!_state.XInputGetStateHookInstalled && !_state.XInputGetStateExHookInstalled &&
        !_state.XInputGetKeystrokeHookInstalled && !_state.XInputSetStateHookInstalled)
    {
        ClearXInputHookPointersLocked();
        return true;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (_state.XInputGetStateHookInstalled && o_XInputGetState != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_XInputGetState), hkXInputGetState);

    if (_state.XInputGetStateExHookInstalled && o_XInputGetStateEx != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_XInputGetStateEx), hkXInputGetStateEx);

    if (_state.XInputGetKeystrokeHookInstalled && o_XInputGetKeystroke != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_XInputGetKeystroke), hkXInputGetKeystroke);

    if (_state.XInputSetStateHookInstalled && o_XInputSetState != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_XInputSetState), hkXInputSetState);

    const LONG result = DetourTransactionCommit();

    if (result != NO_ERROR)
    {
        LOG_WARN("XInput hook removal failed result:{}; retaining trampoline pointers for a safe retry", result);
        return false;
    }

    ClearXInputHookPointersLocked();
    return true;
}

void DrainXInputKeystrokesLocked()
{
    if (!_state.XInputGetKeystrokeHookInstalled || o_XInputGetKeystroke == nullptr)
        return;

    constexpr DWORD MaxDrainPerUser = 256;

    for (DWORD userIndex = 0; userIndex < XUSER_MAX_COUNT; ++userIndex)
    {
        DWORD drained = 0;

        for (; drained < MaxDrainPerUser; ++drained)
        {
            XINPUT_KEYSTROKE keystroke {};
            DWORD result = ERROR_EMPTY;

            {
                ScopedHookBypass bypass;
                result = o_XInputGetKeystroke(userIndex, 0, &keystroke);
            }

            if (result != ERROR_SUCCESS)
                break;
        }

        if (drained == MaxDrainPerUser)
            LOG_WARN("XInput keystroke drain reached safety limit userIndex:{}", userIndex);
    }
}

DWORD WINAPI hkXInputGetState(DWORD userIndex, XINPUT_STATE* state)
{
    if (state == nullptr)
    {
        if (o_XInputGetState != nullptr)
        {
            ScopedHookBypass bypass;
            return o_XInputGetState(userIndex, state);
        }

        return ERROR_BAD_ARGUMENTS;
    }

    bool shouldBlock = false;

    {
        std::unique_lock lock(_state.Mutex);
        _state.XInputGetStateCallCount++;
        shouldBlock = ShouldBlockXInputLocked();

        if (!shouldBlock)
            _state.XInputGetStatePassedCount++;
    }

    if (o_XInputGetState == nullptr)
        return ERROR_DEVICE_NOT_CONNECTED;

    if (!shouldBlock)
    {
        ScopedHookBypass bypass;
        return o_XInputGetState(userIndex, state);
    }

    XINPUT_STATE realState {};
    DWORD result = ERROR_DEVICE_NOT_CONNECTED;

    {
        ScopedHookBypass bypass;
        result = o_XInputGetState(userIndex, &realState);
    }

    {
        std::unique_lock lock(_state.Mutex);
        _state.XInputGetStateBlockedCount++;
    }

    if (result != ERROR_SUCCESS)
    {
        *state = {};
        return result;
    }

    *state = realState;
    state->Gamepad = {}; // neutral connected controller only if it really exists

    return ERROR_SUCCESS;
}

DWORD WINAPI hkXInputGetStateEx(DWORD userIndex, XINPUT_STATE* state)
{
    if (state == nullptr)
    {
        if (o_XInputGetStateEx != nullptr)
        {
            ScopedHookBypass bypass;
            return o_XInputGetStateEx(userIndex, state);
        }

        return ERROR_BAD_ARGUMENTS;
    }

    bool shouldBlock = false;

    {
        std::unique_lock lock(_state.Mutex);
        _state.XInputGetStateCallCount++;
        shouldBlock = ShouldBlockXInputLocked();

        if (!shouldBlock)
            _state.XInputGetStatePassedCount++;
    }

    if (o_XInputGetStateEx == nullptr)
        return ERROR_DEVICE_NOT_CONNECTED;

    if (!shouldBlock)
    {
        ScopedHookBypass bypass;
        return o_XInputGetStateEx(userIndex, state);
    }

    XINPUT_STATE realState {};
    DWORD result = ERROR_DEVICE_NOT_CONNECTED;

    {
        ScopedHookBypass bypass;
        result = o_XInputGetStateEx(userIndex, &realState);
    }

    {
        std::unique_lock lock(_state.Mutex);
        _state.XInputGetStateBlockedCount++;
    }

    if (result != ERROR_SUCCESS)
    {
        *state = {};
        return result;
    }

    *state = realState;
    state->Gamepad = {}; // neutral connected controller only if it really exists

    return ERROR_SUCCESS;
}

DWORD WINAPI hkXInputGetKeystroke(DWORD userIndex, DWORD reserved, PXINPUT_KEYSTROKE keystroke)
{
    bool shouldBlock = false;

    {
        std::unique_lock lock(_state.Mutex);
        _state.XInputGetKeystrokeCallCount++;
        shouldBlock = ShouldBlockXInputLocked();

        if (shouldBlock)
            _state.XInputGetKeystrokeBlockedCount++;
        else
            _state.XInputGetKeystrokePassedCount++;
    }

    if (o_XInputGetKeystroke == nullptr)
    {
        if (keystroke != nullptr)
            *keystroke = {};
        return ERROR_EMPTY;
    }

    if (!shouldBlock)
    {
        ScopedHookBypass bypass;
        return o_XInputGetKeystroke(userIndex, reserved, keystroke);
    }

    // XInputGetKeystroke is a destructive queue read. Consume all pending
    // events for the requested index while the overlay owns input so they
    // cannot replay after the menu closes.
    constexpr DWORD MaxDrain = 256;
    DWORD drained = 0;

    for (; drained < MaxDrain; ++drained)
    {
        XINPUT_KEYSTROKE discarded {};
        DWORD result = ERROR_EMPTY;

        {
            ScopedHookBypass bypass;
            result = o_XInputGetKeystroke(userIndex, reserved, &discarded);
        }

        if (result != ERROR_SUCCESS)
            break;
    }

    if (drained == MaxDrain)
        LOG_WARN("XInputGetKeystroke drain reached safety limit userIndex:{}", userIndex);

    if (keystroke != nullptr)
        *keystroke = {};

    OPTIINPUT_LOG_VERBOSE("blocking XInputGetKeystroke userIndex:{} drained:{}", userIndex, drained);
    return ERROR_EMPTY;
}

DWORD WINAPI hkXInputSetState(DWORD userIndex, XINPUT_VIBRATION* vibration)
{
    bool shouldBlock = false;

    {
        std::unique_lock lock(_state.Mutex);
        _state.XInputSetStateCallCount++;
        shouldBlock = ShouldBlockXInputLocked();

        if (!shouldBlock)
            _state.XInputSetStatePassedCount++;
    }

    if (o_XInputSetState == nullptr)
        return ERROR_DEVICE_NOT_CONNECTED;

    if (!shouldBlock)
    {
        ScopedHookBypass bypass;
        return o_XInputSetState(userIndex, vibration);
    }

    XINPUT_VIBRATION zeroVibration {};
    DWORD result = ERROR_DEVICE_NOT_CONNECTED;

    {
        ScopedHookBypass bypass;
        result = o_XInputSetState(userIndex, &zeroVibration);
    }

    {
        std::unique_lock lock(_state.Mutex);
        _state.XInputSetStateBlockedCount++;
    }

    return result;
}

} // namespace OptiInput
