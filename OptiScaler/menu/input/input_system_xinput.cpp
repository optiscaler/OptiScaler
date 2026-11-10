#include "pch.h"
#include "input_system_internal.h"

#include <algorithm>
#include <intrin.h>
#include <bitset>
#include <vector>

#include <hooks/Kernel_Hooks.h>

#include <detours/detours.h>

namespace OptiInput
{
// One instance per XInput DLL, games and their middleware can load more than one
template <std::size_t Module> DWORD WINAPI hkXInputGetState(DWORD userIndex, XINPUT_STATE* state);
template <std::size_t Module> DWORD WINAPI hkXInputGetStateEx(DWORD userIndex, XINPUT_STATE* state);
template <std::size_t Module>
DWORD WINAPI hkXInputGetKeystroke(DWORD userIndex, DWORD reserved, PXINPUT_KEYSTROKE keystroke);
template <std::size_t Module> DWORD WINAPI hkXInputSetState(DWORD userIndex, XINPUT_VIBRATION* vibration);

namespace
{
constexpr const wchar_t* XInputModuleNames[] = {
    L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll", L"xinput1_2.dll", L"xinput1_1.dll",
};

constexpr std::size_t XInputModuleCount = std::size(XInputModuleNames);

constexpr char XInputGetStateExportName[] = "XInputGetState";
constexpr char XInputGetKeystrokeExportName[] = "XInputGetKeystroke";
constexpr char XInputSetStateExportName[] = "XInputSetState";

struct XInputModuleHooks
{
    HMODULE Module = nullptr;
    bool Attempted = false; // Installed once, a failed install isn't retried every frame

    XInputGetState_t GetState = nullptr;
    XInputGetState_t GetStateEx = nullptr;
    XInputGetKeystroke_t GetKeystroke = nullptr;
    XInputSetState_t SetState = nullptr;
};

std::array<XInputModuleHooks, XInputModuleCount> XInputModules {};

template <std::size_t... I> constexpr auto GetStateDetours(std::index_sequence<I...>)
{
    return std::array { &hkXInputGetState<I>... };
}
template <std::size_t... I> constexpr auto GetStateExDetours(std::index_sequence<I...>)
{
    return std::array { &hkXInputGetStateEx<I>... };
}
template <std::size_t... I> constexpr auto GetKeystrokeDetours(std::index_sequence<I...>)
{
    return std::array { &hkXInputGetKeystroke<I>... };
}
template <std::size_t... I> constexpr auto SetStateDetours(std::index_sequence<I...>)
{
    return std::array { &hkXInputSetState<I>... };
}

constexpr auto XInputGetStateDetours = GetStateDetours(std::make_index_sequence<XInputModuleCount> {});
constexpr auto XInputGetStateExDetours = GetStateExDetours(std::make_index_sequence<XInputModuleCount> {});
constexpr auto XInputGetKeystrokeDetours = GetKeystrokeDetours(std::make_index_sequence<XInputModuleCount> {});
constexpr auto XInputSetStateDetours = SetStateDetours(std::make_index_sequence<XInputModuleCount> {});

// Gamepad keys the game was told are down, per user, their releases stay owed to it while input is blocked
std::array<std::bitset<64>, XUSER_MAX_COUNT> XInputKeysDownSeenByGame {};

// Owed releases read by the drain, handed to the game before anything newer
std::vector<XINPUT_KEYSTROKE> XInputOwedReleases;

bool IsTrackedXInputKey(const XINPUT_KEYSTROKE& keystroke)
{
    return keystroke.UserIndex < XUSER_MAX_COUNT && keystroke.VirtualKey >= VK_PAD_A &&
           keystroke.VirtualKey < VK_PAD_A + XInputKeysDownSeenByGame[0].size();
}

void NoteXInputKeystrokeDeliveredLocked(const XINPUT_KEYSTROKE& keystroke)
{
    if (!IsTrackedXInputKey(keystroke))
        return;

    auto down = XInputKeysDownSeenByGame[keystroke.UserIndex][keystroke.VirtualKey - VK_PAD_A];

    if ((keystroke.Flags & XINPUT_KEYSTROKE_KEYDOWN) != 0)
        down = true;
    else if ((keystroke.Flags & XINPUT_KEYSTROKE_KEYUP) != 0)
        down = false;
}

// A release of a key the game saw pressed, it gets it even while blocked
bool IsOwedXInputReleaseLocked(const XINPUT_KEYSTROKE& keystroke)
{
    return (keystroke.Flags & XINPUT_KEYSTROKE_KEYUP) != 0 && IsTrackedXInputKey(keystroke) &&
           XInputKeysDownSeenByGame[keystroke.UserIndex][keystroke.VirtualKey - VK_PAD_A];
}

bool ShouldBlockXInputLocked()
{
    return _state.Initialized && (ShouldBlockKeyboardInputLocked() || ShouldBlockMouseInputLocked());
}

void RefreshXInputHookStateLocked()
{
    _state.XInputModule = nullptr;
    _state.XInputModuleLoaded = false;
    _state.XInputGetStateHookInstalled = false;
    _state.XInputGetStateExHookInstalled = false;
    _state.XInputGetKeystrokeHookInstalled = false;
    _state.XInputSetStateHookInstalled = false;

    for (const auto& hooks : XInputModules)
    {
        if (hooks.Module == nullptr)
            continue;

        if (_state.XInputModule == nullptr)
            _state.XInputModule = hooks.Module;

        _state.XInputModuleLoaded = true;
        _state.XInputGetStateHookInstalled |= hooks.GetState != nullptr;
        _state.XInputGetStateExHookInstalled |= hooks.GetStateEx != nullptr;
        _state.XInputGetKeystrokeHookInstalled |= hooks.GetKeystroke != nullptr;
        _state.XInputSetStateHookInstalled |= hooks.SetState != nullptr;
    }
}

void InstallXInputModuleHooks(std::size_t index, HMODULE module)
{
    XInputModuleHooks hooks {};
    hooks.Module = module;
    hooks.Attempted = true;
    hooks.GetState =
        reinterpret_cast<XInputGetState_t>(KernelBaseProxy::GetProcAddress_()(module, XInputGetStateExportName));
    hooks.GetStateEx =
        reinterpret_cast<XInputGetState_t>(KernelBaseProxy::GetProcAddress_()(module, MAKEINTRESOURCEA(100)));
    hooks.GetKeystroke = reinterpret_cast<XInputGetKeystroke_t>(
        KernelBaseProxy::GetProcAddress_()(module, XInputGetKeystrokeExportName));
    hooks.SetState =
        reinterpret_cast<XInputSetState_t>(KernelBaseProxy::GetProcAddress_()(module, XInputSetStateExportName));

    if (hooks.GetState == nullptr && hooks.GetStateEx == nullptr && hooks.GetKeystroke == nullptr &&
        hooks.SetState == nullptr)
    {
        LOG_WARN("XInput module loaded but no supported exports found module:{}", static_cast<void*>(module));
        std::unique_lock lock(_state.Mutex);
        XInputModules[index] = { .Attempted = true };
        return;
    }

    {
        std::unique_lock lock(_state.Mutex);
        XInputModules[index] = hooks;
    }

    auto& installed = XInputModules[index];
    LONG result = NO_ERROR;

    {
        std::scoped_lock detourLock(GetDetourTransactionMutex());
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (installed.GetState != nullptr)
            DetourAttach(reinterpret_cast<PVOID*>(&installed.GetState), XInputGetStateDetours[index]);

        if (installed.GetStateEx != nullptr)
            DetourAttach(reinterpret_cast<PVOID*>(&installed.GetStateEx), XInputGetStateExDetours[index]);

        if (installed.GetKeystroke != nullptr)
            DetourAttach(reinterpret_cast<PVOID*>(&installed.GetKeystroke), XInputGetKeystrokeDetours[index]);

        if (installed.SetState != nullptr)
            DetourAttach(reinterpret_cast<PVOID*>(&installed.SetState), XInputSetStateDetours[index]);

        result = DetourTransactionCommit();
    }

    std::unique_lock lock(_state.Mutex);

    if (result != NO_ERROR)
    {
        LOG_ERROR("XInput hook installation failed result:{} module:{}", result, static_cast<void*>(module));
        XInputModules[index] = { .Attempted = true };
        RefreshXInputHookStateLocked();
        return;
    }

    RefreshXInputHookStateLocked();

    LOG_INFO("XInput hooks installed module:{} getState:{} getStateEx:{} getKeystroke:{} setState:{}",
             static_cast<void*>(module), installed.GetState != nullptr ? 1 : 0, installed.GetStateEx != nullptr ? 1 : 0,
             installed.GetKeystroke != nullptr ? 1 : 0, installed.SetState != nullptr ? 1 : 0);
}

DWORD CallXInputGetState(XInputGetState_t original, DWORD userIndex, XINPUT_STATE* state, void* caller)
{
    if (original == nullptr)
        return state == nullptr ? ERROR_BAD_ARGUMENTS : ERROR_DEVICE_NOT_CONNECTED;

    if (state == nullptr)
    {
        ScopedHookBypass bypass;
        return original(userIndex, state);
    }

    bool shouldBlock = false;

    {
        std::unique_lock lock(_state.Mutex);
        _state.XInputGetStateCallCount++;
        shouldBlock = ShouldBlockXInputLocked();

        if (!shouldBlock)
            _state.XInputGetStatePassedCount++;
    }

    // The menu's own gamepad navigation (ImGui) reads it through the hook too
    if (shouldBlock && Util::GetCallerModule(caller) == dllModule)
        shouldBlock = false;

    if (!shouldBlock)
    {
        ScopedHookBypass bypass;
        return original(userIndex, state);
    }

    XINPUT_STATE realState {};
    DWORD result = ERROR_DEVICE_NOT_CONNECTED;

    {
        ScopedHookBypass bypass;
        result = original(userIndex, &realState);
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

} // namespace

void UpdateXInputIntegration()
{
    for (std::size_t i = 0; i < XInputModuleCount; ++i)
    {
        // A mapped XInput DLL can still be inside loader initialization
        HMODULE module = GetModuleHandleW(XInputModuleNames[i]);

        {
            std::unique_lock lock(_state.Mutex);
            auto& hooks = XInputModules[i];

            // Unloaded, its hooks went with it and a reload is hooked again
            if (hooks.Module != nullptr && hooks.Module != module)
            {
                LOG_INFO("XInput module unloaded module:{}", static_cast<void*>(hooks.Module));
                hooks = {};
                RefreshXInputHookStateLocked();
            }

            if (hooks.Attempted || module == nullptr)
                continue;
        }

        InstallXInputModuleHooks(i, module);
    }
}

bool RemoveXInputHooksLocked()
{
    if (std::none_of(XInputModules.begin(), XInputModules.end(),
                     [](const XInputModuleHooks& hooks) { return hooks.Module != nullptr; }))
    {
        XInputModules = {};
        RefreshXInputHookStateLocked();
        return true;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    for (std::size_t i = 0; i < XInputModuleCount; ++i)
    {
        auto& hooks = XInputModules[i];

        if (hooks.GetState != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&hooks.GetState), XInputGetStateDetours[i]);

        if (hooks.GetStateEx != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&hooks.GetStateEx), XInputGetStateExDetours[i]);

        if (hooks.GetKeystroke != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&hooks.GetKeystroke), XInputGetKeystrokeDetours[i]);

        if (hooks.SetState != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&hooks.SetState), XInputSetStateDetours[i]);
    }

    const LONG result = DetourTransactionCommit();

    if (result != NO_ERROR)
    {
        LOG_WARN("XInput hook removal failed result:{}; retaining trampoline pointers for a safe retry", result);
        return false;
    }

    XInputModules = {};
    RefreshXInputHookStateLocked();
    return true;
}

void DrainXInputKeystrokesLocked()
{
    // Menu-time keystrokes are dropped, owed releases wait for the game's next read
    constexpr DWORD MaxDrainPerUser = 256;

    for (std::size_t i = 0; i < XInputModuleCount; ++i)
    {
        const auto& hooks = XInputModules[i];

        // Its trampoline jumps into the DLL, which may be gone since the last frame
        if (hooks.GetKeystroke == nullptr || GetModuleHandleW(XInputModuleNames[i]) != hooks.Module)
            continue;

        for (DWORD userIndex = 0; userIndex < XUSER_MAX_COUNT; ++userIndex)
        {
            DWORD drained = 0;

            for (; drained < MaxDrainPerUser; ++drained)
            {
                XINPUT_KEYSTROKE keystroke {};
                DWORD result = ERROR_EMPTY;

                {
                    ScopedHookBypass bypass;
                    result = hooks.GetKeystroke(userIndex, 0, &keystroke);
                }

                if (result != ERROR_SUCCESS)
                    break;

                if (IsOwedXInputReleaseLocked(keystroke))
                    XInputOwedReleases.push_back(keystroke);
            }

            if (drained == MaxDrainPerUser)
                LOG_WARN("XInput keystroke drain reached safety limit userIndex:{}", userIndex);
        }
    }
}

template <std::size_t Module> DWORD WINAPI hkXInputGetState(DWORD userIndex, XINPUT_STATE* state)
{
    return CallXInputGetState(XInputModules[Module].GetState, userIndex, state, _ReturnAddress());
}

template <std::size_t Module> DWORD WINAPI hkXInputGetStateEx(DWORD userIndex, XINPUT_STATE* state)
{
    return CallXInputGetState(XInputModules[Module].GetStateEx, userIndex, state, _ReturnAddress());
}

template <std::size_t Module>
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

        // Releases the drain kept come first
        if (!shouldBlock && keystroke != nullptr)
        {
            auto owed = std::find_if(XInputOwedReleases.begin(), XInputOwedReleases.end(),
                                     [userIndex](const XINPUT_KEYSTROKE& k)
                                     { return userIndex == XUSER_INDEX_ANY || k.UserIndex == userIndex; });

            if (owed != XInputOwedReleases.end())
            {
                *keystroke = *owed;
                XInputOwedReleases.erase(owed);
                NoteXInputKeystrokeDeliveredLocked(*keystroke);
                return ERROR_SUCCESS;
            }
        }
    }

    const XInputGetKeystroke_t original = XInputModules[Module].GetKeystroke;

    if (original == nullptr)
    {
        if (keystroke != nullptr)
            *keystroke = {};
        return ERROR_EMPTY;
    }

    if (!shouldBlock || keystroke == nullptr)
    {
        DWORD result = ERROR_EMPTY;

        {
            ScopedHookBypass bypass;
            result = original(userIndex, reserved, keystroke);
        }

        if (result == ERROR_SUCCESS && keystroke != nullptr)
        {
            std::unique_lock lock(_state.Mutex);
            NoteXInputKeystrokeDeliveredLocked(*keystroke);
        }

        return result;
    }

    // XInputGetKeystroke is a destructive queue read. Menu-time keystrokes are consumed so they can't
    // replay after the menu closes, but releases of keys the game saw pressed are passed on.
    constexpr DWORD MaxDrain = 256;
    DWORD drained = 0;

    for (; drained < MaxDrain; ++drained)
    {
        XINPUT_KEYSTROKE read {};
        DWORD result = ERROR_EMPTY;

        {
            ScopedHookBypass bypass;
            result = original(userIndex, reserved, &read);
        }

        if (result != ERROR_SUCCESS)
            break;

        std::unique_lock lock(_state.Mutex);

        if (IsOwedXInputReleaseLocked(read))
        {
            NoteXInputKeystrokeDeliveredLocked(read);
            *keystroke = read;
            return ERROR_SUCCESS;
        }
    }

    if (drained == MaxDrain)
        LOG_WARN("XInputGetKeystroke drain reached safety limit userIndex:{}", userIndex);

    *keystroke = {};

    OPTIINPUT_LOG_VERBOSE("blocking XInputGetKeystroke userIndex:{} drained:{}", userIndex, drained);
    return ERROR_EMPTY;
}

template <std::size_t Module> DWORD WINAPI hkXInputSetState(DWORD userIndex, XINPUT_VIBRATION* vibration)
{
    bool shouldBlock = false;

    {
        std::unique_lock lock(_state.Mutex);
        _state.XInputSetStateCallCount++;
        shouldBlock = ShouldBlockXInputLocked();

        if (!shouldBlock)
            _state.XInputSetStatePassedCount++;
    }

    const XInputSetState_t original = XInputModules[Module].SetState;

    if (original == nullptr)
        return ERROR_DEVICE_NOT_CONNECTED;

    if (!shouldBlock)
    {
        ScopedHookBypass bypass;
        return original(userIndex, vibration);
    }

    XINPUT_VIBRATION zeroVibration {};
    DWORD result = ERROR_DEVICE_NOT_CONNECTED;

    {
        ScopedHookBypass bypass;
        result = original(userIndex, &zeroVibration);
    }

    {
        std::unique_lock lock(_state.Mutex);
        _state.XInputSetStateBlockedCount++;
    }

    return result;
}

} // namespace OptiInput
