#include "pch.h"
#include "input_system_internal.h"

#include <hooks/Kernel_Hooks.h>

#include <detours/detours.h>

#include <cstring>
#include <vector>

namespace OptiInput
{
// One instance per hook slot so each calls its own slot's trampoline, another mod can replace the vtable entry
template <std::size_t Slot>
HRESULT WINAPI hkDirectInputCreateDevice(void* directInput, REFGUID guid, void** device, LPUNKNOWN outer);
template <std::size_t Slot> HRESULT WINAPI hkDirectInputGetDeviceState(void* device, DWORD dataSize, LPVOID data);
template <std::size_t Slot>
HRESULT WINAPI hkDirectInputGetDeviceData(void* device, DWORD objectDataSize, LPDIDEVICEOBJECTDATA data, LPDWORD inOut,
                                          DWORD flags);
template <std::size_t Slot> ULONG WINAPI hkDirectInputDeviceRelease(void* device);

namespace
{
constexpr wchar_t DirectInput8ModuleName[] = L"dinput8.dll";
constexpr wchar_t DirectInputLegacyModuleName[] = L"dinput.dll";

constexpr char DirectInput8CreateExportName[] = "DirectInput8Create";
constexpr char DirectInputCreateAExportName[] = "DirectInputCreateA";
constexpr char DirectInputCreateWExportName[] = "DirectInputCreateW";
constexpr char DirectInputCreateExExportName[] = "DirectInputCreateEx";

constexpr GUID DirectInputSysKeyboardGuid = {
    0x6f1d2b61, 0xd5a0, 0x11cf, { 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 }
};
constexpr GUID DirectInputSysMouseGuid = {
    0x6f1d2b60, 0xd5a0, 0x11cf, { 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 }
};

constexpr std::size_t MaxDirectInputMethodHooks = 8;

template <typename T> struct DirectInputMethodHookSlot
{
    bool InUse = false;
    T Target = nullptr;
    T Trampoline = nullptr;
};

std::array<DirectInputMethodHookSlot<DirectInputCreateDevice_t>, MaxDirectInputMethodHooks>
    DirectInputCreateDeviceHooks {};
std::array<DirectInputMethodHookSlot<DirectInputDeviceRelease_t>, MaxDirectInputMethodHooks> DirectInputReleaseHooks {};
std::array<DirectInputMethodHookSlot<DirectInputGetDeviceState_t>, MaxDirectInputMethodHooks>
    DirectInputGetDeviceStateHooks {};
std::array<DirectInputMethodHookSlot<DirectInputGetDeviceData_t>, MaxDirectInputMethodHooks>
    DirectInputGetDeviceDataHooks {};

template <std::size_t... I> constexpr auto CreateDeviceDetours(std::index_sequence<I...>)
{
    return std::array { &hkDirectInputCreateDevice<I>... };
}
template <std::size_t... I> constexpr auto GetDeviceStateDetours(std::index_sequence<I...>)
{
    return std::array { &hkDirectInputGetDeviceState<I>... };
}
template <std::size_t... I> constexpr auto GetDeviceDataDetours(std::index_sequence<I...>)
{
    return std::array { &hkDirectInputGetDeviceData<I>... };
}
template <std::size_t... I> constexpr auto ReleaseDetours(std::index_sequence<I...>)
{
    return std::array { &hkDirectInputDeviceRelease<I>... };
}

constexpr auto DirectInputCreateDeviceDetours =
    CreateDeviceDetours(std::make_index_sequence<MaxDirectInputMethodHooks> {});
constexpr auto DirectInputGetDeviceStateDetours =
    GetDeviceStateDetours(std::make_index_sequence<MaxDirectInputMethodHooks> {});
constexpr auto DirectInputGetDeviceDataDetours =
    GetDeviceDataDetours(std::make_index_sequence<MaxDirectInputMethodHooks> {});
constexpr auto DirectInputReleaseDetours = ReleaseDetours(std::make_index_sequence<MaxDirectInputMethodHooks> {});

// The slot's position in its table, which picks its hook instance
template <typename T, std::size_t N>
std::size_t SlotIndex(const std::array<DirectInputMethodHookSlot<T>, N>& hooks,
                      const DirectInputMethodHookSlot<T>* slot)
{
    return static_cast<std::size_t>(slot - hooks.data());
}

bool IsDirectInputKeyboardGuid(REFGUID guid) { return IsEqualGUID(guid, DirectInputSysKeyboardGuid) != FALSE; }

bool IsDirectInputMouseGuid(REFGUID guid) { return IsEqualGUID(guid, DirectInputSysMouseGuid) != FALSE; }

DirectInputDeviceKind GetDirectInputDeviceKind(REFGUID guid)
{
    if (IsDirectInputKeyboardGuid(guid))
        return DirectInputDeviceKind::Keyboard;

    if (IsDirectInputMouseGuid(guid))
        return DirectInputDeviceKind::Mouse;

    return DirectInputDeviceKind::Other;
}

bool ShouldBlockDirectInputKeyboardLocked()
{
    return _state.Initialized && _state.Focused && ShouldBlockKeyboardInputLocked();
}

bool ShouldBlockDirectInputMouseLocked()
{
    return _state.Initialized && _state.Focused && ShouldBlockMouseInputLocked();
}

bool ShouldBlockDirectInputOtherLocked()
{
    return _state.Initialized && (ShouldBlockKeyboardInputLocked() || ShouldBlockMouseInputLocked());
}

bool ShouldBlockDirectInputDeviceLocked(DirectInputDeviceKind kind)
{
    switch (kind)
    {
    case DirectInputDeviceKind::Keyboard:
        return ShouldBlockDirectInputKeyboardLocked();

    case DirectInputDeviceKind::Mouse:
        return ShouldBlockDirectInputMouseLocked();

    case DirectInputDeviceKind::Other:
    default:
        return ShouldBlockDirectInputOtherLocked();
    }
}

const char* DirectInputDeviceKindName(DirectInputDeviceKind kind)
{
    switch (kind)
    {
    case DirectInputDeviceKind::Keyboard:
        return "keyboard";

    case DirectInputDeviceKind::Mouse:
        return "mouse";

    case DirectInputDeviceKind::Other:
    default:
        return "other";
    }
}

template <typename T, std::size_t N>
DirectInputMethodHookSlot<T>*
FindDirectInputMethodHookByTargetLocked(std::array<DirectInputMethodHookSlot<T>, N>& hooks, T target)
{
    if (target == nullptr)
        return nullptr;

    for (auto& slot : hooks)
    {
        if (slot.InUse && slot.Target == target)
            return &slot;
    }

    return nullptr;
}

template <typename T, std::size_t N>
DirectInputMethodHookSlot<T>* PrepareDirectInputMethodHookLocked(std::array<DirectInputMethodHookSlot<T>, N>& hooks,
                                                                 T target, bool* needsAttach)
{
    if (needsAttach != nullptr)
        *needsAttach = false;

    if (target == nullptr)
        return nullptr;

    if (auto* existing = FindDirectInputMethodHookByTargetLocked(hooks, target); existing != nullptr)
        return existing;

    for (auto& slot : hooks)
    {
        if (slot.InUse)
            continue;

        slot.InUse = true;
        slot.Target = target;
        slot.Trampoline = target;

        if (needsAttach != nullptr)
            *needsAttach = true;

        return &slot;
    }

    return nullptr;
}

template <typename T, std::size_t N>
bool HasDirectInputMethodHooksLocked(const std::array<DirectInputMethodHookSlot<T>, N>& hooks)
{
    for (const auto& slot : hooks)
    {
        if (slot.InUse)
            return true;
    }

    return false;
}

template <typename T, std::size_t N>
T FirstDirectInputMethodTrampolineLocked(const std::array<DirectInputMethodHookSlot<T>, N>& hooks)
{
    for (const auto& slot : hooks)
    {
        if (slot.InUse && slot.Trampoline != nullptr)
            return slot.Trampoline;
    }

    return nullptr;
}

void RefreshDirectInputDeviceHookStateLocked()
{
    _state.DirectInputDeviceReleaseHookInstalled = HasDirectInputMethodHooksLocked(DirectInputReleaseHooks);
    _state.DirectInputGetDeviceStateHookInstalled = HasDirectInputMethodHooksLocked(DirectInputGetDeviceStateHooks);
    _state.DirectInputGetDeviceDataHookInstalled = HasDirectInputMethodHooksLocked(DirectInputGetDeviceDataHooks);

    // Keep the legacy globals valid for diagnostics/compatibility, but do not use them to identify a target.
    o_DirectInputCreateDeviceA = FirstDirectInputMethodTrampolineLocked(DirectInputCreateDeviceHooks);
    o_DirectInputCreateDeviceW = o_DirectInputCreateDeviceA;
    o_DirectInputDeviceRelease = FirstDirectInputMethodTrampolineLocked(DirectInputReleaseHooks);
    o_DirectInputDeviceGetDeviceState = FirstDirectInputMethodTrampolineLocked(DirectInputGetDeviceStateHooks);
    o_DirectInputDeviceGetDeviceData = FirstDirectInputMethodTrampolineLocked(DirectInputGetDeviceDataHooks);
}

void ClearDirectInputMethodHooksLocked()
{
    DirectInputCreateDeviceHooks = {};
    _state.DirectInputCreateDeviceAHookInstalled = false;
    _state.DirectInputCreateDeviceWHookInstalled = false;
    DirectInputReleaseHooks = {};
    DirectInputGetDeviceStateHooks = {};
    DirectInputGetDeviceDataHooks = {};
    RefreshDirectInputDeviceHookStateLocked();
}

void MarkDirectInputDeviceKindSeenLocked(DirectInputDeviceKind kind)
{
    if (kind == DirectInputDeviceKind::Keyboard)
        _state.DirectInputKeyboardDeviceSeen = true;
    else if (kind == DirectInputDeviceKind::Mouse)
        _state.DirectInputMouseDeviceSeen = true;
    else
        _state.DirectInputOtherDeviceSeen = true;
}

HMODULE FindLoadedDirectInput8Module() { return GetModuleHandleW(DirectInput8ModuleName); }

HMODULE FindLoadedDirectInputLegacyModule() { return GetModuleHandleW(DirectInputLegacyModuleName); }

void ClearDirectInputHookPointersLocked()
{
    o_DirectInput8Create = nullptr;
    o_DirectInputCreateA = nullptr;
    o_DirectInputCreateW = nullptr;
    o_DirectInputCreateEx = nullptr;
    o_DirectInputCreateDeviceA = nullptr;
    o_DirectInputCreateDeviceW = nullptr;

    _state.DirectInput8CreateHookInstalled = false;
    _state.DirectInputCreateAHookInstalled = false;
    _state.DirectInputCreateWHookInstalled = false;
    _state.DirectInputCreateExHookInstalled = false;
    _state.DirectInputCreateDeviceAHookInstalled = false;
    _state.DirectInputCreateDeviceWHookInstalled = false;

    ClearDirectInputMethodHooksLocked();
}

std::size_t FindDirectInputDeviceSlotLocked(void* device)
{
    if (device == nullptr)
        return MaxTrackedDirectInputDevices;

    for (std::size_t i = 0; i < _state.DirectInputDeviceSlots.size(); ++i)
    {
        if (_state.DirectInputDeviceSlots[i].InUse && _state.DirectInputDeviceSlots[i].Device == device)
            return i;
    }

    return MaxTrackedDirectInputDevices;
}

DirectInputDeviceKind GetDirectInputDeviceKindLocked(void* device)
{
    const std::size_t slot = FindDirectInputDeviceSlotLocked(device);

    if (slot >= MaxTrackedDirectInputDevices)
        return DirectInputDeviceKind::Other;

    return _state.DirectInputDeviceSlots[slot].Kind;
}

void ClearDirectInputDeviceSlotLocked(std::size_t slot)
{
    if (slot >= MaxTrackedDirectInputDevices)
        return;

    if (_state.DirectInputDeviceSlots[slot].InUse && _state.DirectInputTrackedDeviceCount > 0)
        _state.DirectInputTrackedDeviceCount--;

    _state.DirectInputDeviceSlots[slot] = {};
}

void ClearAllDirectInputDeviceSlotsLocked()
{
    _state.DirectInputDeviceSlots = {};
    _state.DirectInputTrackedDeviceCount = 0;
}

void TrackDirectInputDeviceLocked(void* device, DirectInputDeviceKind kind)
{
    if (device == nullptr)
        return;

    std::size_t freeSlot = MaxTrackedDirectInputDevices;

    for (std::size_t i = 0; i < _state.DirectInputDeviceSlots.size(); ++i)
    {
        auto& slot = _state.DirectInputDeviceSlots[i];

        if (slot.InUse && slot.Device == device)
        {
            // A later CreateDevice call may use an instance GUID that we cannot classify and
            // therefore reports Other. Never downgrade a known keyboard/mouse classification.
            if (slot.Kind == DirectInputDeviceKind::Other && kind != DirectInputDeviceKind::Other)
            {
                slot.Kind = kind;
                MarkDirectInputDeviceKindSeenLocked(kind);
                LOG_INFO("DirectInput device reclassified device:{} kind:{}", device, DirectInputDeviceKindName(kind));
            }
            else if (slot.Kind != DirectInputDeviceKind::Other && kind != DirectInputDeviceKind::Other &&
                     slot.Kind != kind)
            {
                LOG_WARN("DirectInput device kind mismatch device:{} existing:{} new:{}; preserving existing kind",
                         device, DirectInputDeviceKindName(slot.Kind), DirectInputDeviceKindName(kind));
            }

            return;
        }

        if (!slot.InUse && freeSlot >= MaxTrackedDirectInputDevices)
            freeSlot = i;
    }

    if (freeSlot >= MaxTrackedDirectInputDevices)
    {
        LOG_WARN("DirectInput device tracking table is full device:{} kind:{}", device,
                 DirectInputDeviceKindName(kind));
        return;
    }

    auto& slot = _state.DirectInputDeviceSlots[freeSlot];
    slot.InUse = true;
    slot.Device = device;
    slot.Kind = kind;

    _state.DirectInputTrackedDeviceCount++;

    MarkDirectInputDeviceKindSeenLocked(kind);

    LOG_INFO("DirectInput device captured device:{} kind:{}", device, DirectInputDeviceKindName(kind));
}

bool HookDirectInputDeviceLocked(void* device, DirectInputDeviceKind kind)
{
    if (device == nullptr)
        return false;

    PVOID* vtable = *reinterpret_cast<PVOID**>(device);

    auto release = reinterpret_cast<DirectInputDeviceRelease_t>(vtable[2]);
    auto getDeviceState = reinterpret_cast<DirectInputGetDeviceState_t>(vtable[9]);
    auto getDeviceData = reinterpret_cast<DirectInputGetDeviceData_t>(vtable[10]);

    bool attachRelease = false;
    bool attachGetDeviceState = false;
    bool attachGetDeviceData = false;

    auto* releaseHook = PrepareDirectInputMethodHookLocked(DirectInputReleaseHooks, release, &attachRelease);
    auto* getDeviceStateHook =
        PrepareDirectInputMethodHookLocked(DirectInputGetDeviceStateHooks, getDeviceState, &attachGetDeviceState);
    auto* getDeviceDataHook =
        PrepareDirectInputMethodHookLocked(DirectInputGetDeviceDataHooks, getDeviceData, &attachGetDeviceData);

    bool completeCoverage = true;

    if (release != nullptr && releaseHook == nullptr)
    {
        LOG_WARN("DirectInput Release hook table is full, device:{} target:{}", device,
                 reinterpret_cast<void*>(release));
        completeCoverage = false;
    }

    if (getDeviceState != nullptr && getDeviceStateHook == nullptr)
    {
        LOG_WARN("DirectInput GetDeviceState hook table is full, device:{} target:{}", device,
                 reinterpret_cast<void*>(getDeviceState));
        completeCoverage = false;
    }

    if (getDeviceData != nullptr && getDeviceDataHook == nullptr)
    {
        LOG_WARN("DirectInput GetDeviceData hook table is full, device:{} target:{}", device,
                 reinterpret_cast<void*>(getDeviceData));
        completeCoverage = false;
    }

    if (!attachRelease && !attachGetDeviceState && !attachGetDeviceData)
    {
        TrackDirectInputDeviceLocked(device, kind);
        return completeCoverage;
    }

    LONG result = NO_ERROR;

    {
        std::scoped_lock detourLock(GetDetourTransactionMutex());
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (attachRelease)
            DetourAttach(reinterpret_cast<PVOID*>(&releaseHook->Trampoline),
                         DirectInputReleaseDetours[SlotIndex(DirectInputReleaseHooks, releaseHook)]);

        if (attachGetDeviceState)
            DetourAttach(
                reinterpret_cast<PVOID*>(&getDeviceStateHook->Trampoline),
                DirectInputGetDeviceStateDetours[SlotIndex(DirectInputGetDeviceStateHooks, getDeviceStateHook)]);

        if (attachGetDeviceData)
            DetourAttach(reinterpret_cast<PVOID*>(&getDeviceDataHook->Trampoline),
                         DirectInputGetDeviceDataDetours[SlotIndex(DirectInputGetDeviceDataHooks, getDeviceDataHook)]);

        result = DetourTransactionCommit();
    }

    if (result != NO_ERROR)
    {
        LOG_ERROR("DirectInput device hook installation failed result:{} device:{} kind:{}", result, device,
                  DirectInputDeviceKindName(kind));

        if (attachRelease)
            *releaseHook = {};

        if (attachGetDeviceState)
            *getDeviceStateHook = {};

        if (attachGetDeviceData)
            *getDeviceDataHook = {};

        RefreshDirectInputDeviceHookStateLocked();
        return false;
    }

    RefreshDirectInputDeviceHookStateLocked();

    if (attachRelease)
        LOG_INFO("DirectInput Release target detoured target:{} device:{}", reinterpret_cast<void*>(release), device);

    if (attachGetDeviceState)
        LOG_INFO("DirectInput GetDeviceState target detoured target:{} device:{}",
                 reinterpret_cast<void*>(getDeviceState), device);

    if (attachGetDeviceData)
        LOG_INFO("DirectInput GetDeviceData target detoured target:{} device:{}",
                 reinterpret_cast<void*>(getDeviceData), device);

    TrackDirectInputDeviceLocked(device, kind);
    return completeCoverage;
}

bool HookDirectInputInterfaceLocked(void* directInput, bool wide)
{
    if (directInput == nullptr)
        return false;

    PVOID* vtable = *reinterpret_cast<PVOID**>(directInput);
    auto createDevice = reinterpret_cast<DirectInputCreateDevice_t>(vtable[3]);

    if (createDevice == nullptr)
        return false;

    bool needsAttach = false;
    auto* slot = PrepareDirectInputMethodHookLocked(DirectInputCreateDeviceHooks, createDevice, &needsAttach);

    if (slot == nullptr)
    {
        LOG_ERROR("DirectInput CreateDevice hook table full wide:{} target:{}", wide ? 1 : 0,
                  reinterpret_cast<void*>(createDevice));
        return false;
    }

    if (needsAttach)
    {
        // ANSI and Unicode CreateDevice have the same ABI, a shared A/W implementation is one slot
        LONG result = NO_ERROR;

        {
            std::scoped_lock detourLock(GetDetourTransactionMutex());
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(reinterpret_cast<PVOID*>(&slot->Trampoline),
                         DirectInputCreateDeviceDetours[SlotIndex(DirectInputCreateDeviceHooks, slot)]);
            result = DetourTransactionCommit();
        }

        if (result != NO_ERROR)
        {
            LOG_ERROR("DirectInput CreateDevice hook installation failed result:{} wide:{} target:{}", result,
                      wide ? 1 : 0, reinterpret_cast<void*>(createDevice));
            *slot = {};
            RefreshDirectInputDeviceHookStateLocked();
            return false;
        }

        LOG_INFO("DirectInput CreateDevice target detoured target:{}", reinterpret_cast<void*>(createDevice));
    }

    if (wide)
        _state.DirectInputCreateDeviceWHookInstalled = true;
    else
        _state.DirectInputCreateDeviceAHookInstalled = true;

    RefreshDirectInputDeviceHookStateLocked();
    return true;
}

bool TryGetDirectInputInterfaceWidth(REFIID riid, bool* wide)
{
    if (wide == nullptr)
        return false;

    if (IsEqualGUID(riid, IID_IDirectInput8W) || IsEqualGUID(riid, IID_IDirectInput7W) ||
        IsEqualGUID(riid, IID_IDirectInput2W) || IsEqualGUID(riid, IID_IDirectInputW))
    {
        *wide = true;
        return true;
    }

    if (IsEqualGUID(riid, IID_IDirectInput8A) || IsEqualGUID(riid, IID_IDirectInput7A) ||
        IsEqualGUID(riid, IID_IDirectInput2A) || IsEqualGUID(riid, IID_IDirectInputA))
    {
        *wide = false;
        return true;
    }

    return false;
}

void HandleDirectInputCreatedLocked(REFIID riid, void** out, const char* source)
{
    if (out == nullptr || *out == nullptr)
        return;

    bool wide = false;
    if (TryGetDirectInputInterfaceWidth(riid, &wide))
    {
        HookDirectInputInterfaceLocked(*out, wide);
        return;
    }

    OPTIINPUT_LOG_VERBOSE("{} returned unsupported riid directInput:{} riid:{}",
                          source != nullptr ? source : "DirectInput", *out, static_cast<const void*>(&riid));
}

void HandleLegacyDirectInputCreatedLocked(void** out, bool wide)
{
    if (out == nullptr || *out == nullptr)
        return;

    HookDirectInputInterfaceLocked(*out, wide);
}

// Export hooks that failed to install, they aren't retried every frame
std::vector<void*> FailedDirectInputExportHooks;

bool InstallDirectInputExportHook(HMODULE module, const char* exportName, void** original, void* hook, bool* installed)
{
    if (module == nullptr || exportName == nullptr || original == nullptr || hook == nullptr || installed == nullptr)
        return false;

    {
        std::unique_lock lock(_state.Mutex);

        if (*installed)
            return true;

        if (std::ranges::find(FailedDirectInputExportHooks, hook) != FailedDirectInputExportHooks.end())
            return false;
    }

    void* resolved = reinterpret_cast<void*>(KernelBaseProxy::GetProcAddress_()(module, exportName));

    if (resolved == nullptr)
        return false;

    {
        std::unique_lock lock(_state.Mutex);

        if (*installed)
            return true;

        *original = resolved;
    }

    LONG result = NO_ERROR;

    {
        std::scoped_lock detourLock(GetDetourTransactionMutex());
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(reinterpret_cast<PVOID*>(original), hook);
        result = DetourTransactionCommit();
    }

    std::unique_lock lock(_state.Mutex);

    if (result != NO_ERROR)
    {
        LOG_ERROR("{} hook installation failed result:{}", exportName, result);
        *original = nullptr;
        FailedDirectInputExportHooks.push_back(hook);
        return false;
    }

    *installed = true;
    LOG_INFO("{} hook installed module:{}", exportName, static_cast<void*>(module));
    return true;
}

// A device the game created before the hooks went in is tracked on its first read, its kind from its capabilities
void TrackUntrackedDirectInputDevice(void* device)
{
    {
        std::unique_lock lock(_state.Mutex);

        if (device == nullptr || FindDirectInputDeviceSlotLocked(device) < MaxTrackedDirectInputDevices ||
            _state.DirectInputTrackedDeviceCount >= MaxTrackedDirectInputDevices)
        {
            return;
        }
    }

    using GetCapabilities_t = HRESULT(WINAPI*)(void* device, LPDIDEVCAPS caps);
    auto getCapabilities = reinterpret_cast<GetCapabilities_t>((*reinterpret_cast<PVOID**>(device))[3]);

    DIDEVCAPS caps {};
    caps.dwSize = sizeof(caps);
    HRESULT result = DIERR_GENERIC;

    {
        ScopedHookBypass bypass;
        result = getCapabilities(device, &caps);
    }

    DirectInputDeviceKind kind = DirectInputDeviceKind::Other;

    if (SUCCEEDED(result))
    {
        switch (GET_DIDEVICE_TYPE(caps.dwDevType))
        {
        case DI8DEVTYPE_KEYBOARD:
        case 3: // DIDEVTYPE_KEYBOARD of DirectInput 7 devices
            kind = DirectInputDeviceKind::Keyboard;
            break;

        case DI8DEVTYPE_MOUSE:
        case 2: // DIDEVTYPE_MOUSE
            kind = DirectInputDeviceKind::Mouse;
            break;
        }
    }

    std::unique_lock lock(_state.Mutex);
    TrackDirectInputDeviceLocked(device, kind);
}

// Keyboard keys are their DIK codes, mouse buttons have their own offsets
bool IsDirectInputButton(DirectInputDeviceKind kind, DWORD offset)
{
    if (kind == DirectInputDeviceKind::Keyboard)
        return offset < 256;

    return kind == DirectInputDeviceKind::Mouse && offset >= DIMOFS_BUTTON0 && offset <= DIMOFS_BUTTON7;
}

// What the game was told about its keys and buttons, so their releases can be passed while blocked
void TrackDirectInputButtons(void* device, DirectInputDeviceKind kind, const void* data, DWORD objectDataSize,
                             DWORD count)
{
    if (kind == DirectInputDeviceKind::Other || count == 0)
        return;

    std::unique_lock lock(_state.Mutex);
    const std::size_t slot = FindDirectInputDeviceSlotLocked(device);

    if (slot >= MaxTrackedDirectInputDevices)
        return;

    auto& down = _state.DirectInputDeviceSlots[slot].DownSeenByGame;

    for (DWORD i = 0; i < count; ++i)
    {
        auto* event =
            reinterpret_cast<const DIDEVICEOBJECTDATA_DX3*>(static_cast<const BYTE*>(data) + i * objectDataSize);

        if (IsDirectInputButton(kind, event->dwOfs))
            down[event->dwOfs] = (event->dwData & 0x80) != 0;
    }
}

// A joystick read with c_dfDIJoystick(2) at rest: 0 is full left/up for axes and north for POV hats
void FillNeutralJoystickState(void* device, DIJOYSTATE* state)
{
    std::memset(state->rgdwPOV, 0xFF, sizeof(state->rgdwPOV));

    using GetProperty_t = HRESULT(WINAPI*)(void* device, REFGUID property, LPDIPROPHEADER header);
    auto getProperty = reinterpret_cast<GetProperty_t>((*reinterpret_cast<PVOID**>(device))[5]);

    // lX to lRz and the two sliders are consecutive LONGs, each centered in its range
    for (DWORD offset = 0; offset <= offsetof(DIJOYSTATE, rglSlider[1]); offset += sizeof(LONG))
    {
        DIPROPRANGE range {};
        range.diph = { sizeof(DIPROPRANGE), sizeof(DIPROPHEADER), offset, DIPH_BYOFFSET };

        if (SUCCEEDED(getProperty(device, DIPROP_RANGE, &range.diph)))
            *reinterpret_cast<LONG*>(reinterpret_cast<BYTE*>(state) + offset) =
                range.lMin + (range.lMax - range.lMin) / 2;
    }
}

HRESULT CallDirectInputCreateDeviceOriginal(DirectInputCreateDevice_t original, void* directInput, REFGUID guid,
                                            void** device, LPUNKNOWN outer)
{
    if (original == nullptr)
        return DIERR_GENERIC;

    ScopedHookBypass bypass;
    return original(directInput, guid, device, outer);
}
} // namespace

// The game's DirectInput objects created before the hooks went in share their methods with new ones. Creating
// a device of our own hooks CreateDevice and the device methods for them too.
void HookDirectInput8MethodsThroughOwnDevice()
{
    static bool hooked = false;

    {
        std::unique_lock lock(_state.Mutex);

        if (hooked || !_state.DirectInput8CreateHookInstalled || o_DirectInput8Create == nullptr)
            return;

        hooked = true;
    }

    for (const bool wide : { false, true })
    {
        void* directInput = nullptr;

        {
            ScopedHookBypass bypass;

            if (FAILED(o_DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                                            wide ? IID_IDirectInput8W : IID_IDirectInput8A, &directInput, nullptr)) ||
                directInput == nullptr)
            {
                continue;
            }
        }

        {
            std::unique_lock lock(_state.Mutex);
            HookDirectInputInterfaceLocked(directInput, wide);
        }

        // Through the hooked CreateDevice, which hooks the device's methods
        PVOID* vtable = *reinterpret_cast<PVOID**>(directInput);
        void* device = nullptr;

        if (SUCCEEDED(reinterpret_cast<DirectInputCreateDevice_t>(vtable[3])(directInput, DirectInputSysKeyboardGuid,
                                                                             &device, nullptr)) &&
            device != nullptr)
        {
            reinterpret_cast<ULONG(WINAPI*)(void*)>((*reinterpret_cast<PVOID**>(device))[2])(device);
        }

        reinterpret_cast<ULONG(WINAPI*)(void*)>(vtable[2])(directInput);
    }
}

void UpdateDirectInputIntegration()
{
    // Module discovery can enter the loader. Do it before publishing state or installing hooks.
    HMODULE module8 = FindLoadedDirectInput8Module();
    HMODULE legacyModule = FindLoadedDirectInputLegacyModule();

    {
        std::unique_lock lock(_state.Mutex);
        _state.DirectInputModule = module8;
        _state.DirectInputLegacyModule = legacyModule;
        _state.DirectInputModuleLoaded = module8 != nullptr || legacyModule != nullptr;
        _state.DirectInputLegacyModuleLoaded = legacyModule != nullptr;
    }

    if (module8 != nullptr)
    {
        if (!InstallDirectInputExportHook(module8, DirectInput8CreateExportName,
                                          reinterpret_cast<void**>(&o_DirectInput8Create), hkDirectInput8Create,
                                          &_state.DirectInput8CreateHookInstalled))
        {
            std::unique_lock lock(_state.Mutex);

            if (o_DirectInput8Create == nullptr)
            {
                OPTIINPUT_LOG_VERBOSE("DirectInput8Create export was not found module:{}", static_cast<void*>(module8));
            }
        }
    }

    HookDirectInput8MethodsThroughOwnDevice();

    if (legacyModule != nullptr)
    {
        InstallDirectInputExportHook(legacyModule, DirectInputCreateAExportName,
                                     reinterpret_cast<void**>(&o_DirectInputCreateA), hkDirectInputCreateA,
                                     &_state.DirectInputCreateAHookInstalled);

        InstallDirectInputExportHook(legacyModule, DirectInputCreateWExportName,
                                     reinterpret_cast<void**>(&o_DirectInputCreateW), hkDirectInputCreateW,
                                     &_state.DirectInputCreateWHookInstalled);

        InstallDirectInputExportHook(legacyModule, DirectInputCreateExExportName,
                                     reinterpret_cast<void**>(&o_DirectInputCreateEx), hkDirectInputCreateEx,
                                     &_state.DirectInputCreateExHookInstalled);
    }
}

bool RemoveDirectInputHooksLocked()
{
    if (!_state.DirectInput8CreateHookInstalled && !_state.DirectInputCreateAHookInstalled &&
        !_state.DirectInputCreateWHookInstalled && !_state.DirectInputCreateExHookInstalled &&
        !_state.DirectInputCreateDeviceAHookInstalled && !_state.DirectInputCreateDeviceWHookInstalled &&
        !_state.DirectInputGetDeviceStateHookInstalled && !_state.DirectInputGetDeviceDataHookInstalled &&
        !_state.DirectInputDeviceReleaseHookInstalled)
    {
        ClearDirectInputHookPointersLocked();
        ClearAllDirectInputDeviceSlotsLocked();
        return true;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (_state.DirectInput8CreateHookInstalled && o_DirectInput8Create != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_DirectInput8Create), hkDirectInput8Create);

    if (_state.DirectInputCreateAHookInstalled && o_DirectInputCreateA != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_DirectInputCreateA), hkDirectInputCreateA);

    if (_state.DirectInputCreateWHookInstalled && o_DirectInputCreateW != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_DirectInputCreateW), hkDirectInputCreateW);

    if (_state.DirectInputCreateExHookInstalled && o_DirectInputCreateEx != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_DirectInputCreateEx), hkDirectInputCreateEx);

    for (std::size_t i = 0; i < DirectInputCreateDeviceHooks.size(); ++i)
    {
        auto& slot = DirectInputCreateDeviceHooks[i];
        if (slot.InUse && slot.Trampoline != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&slot.Trampoline), DirectInputCreateDeviceDetours[i]);
    }

    for (std::size_t i = 0; i < DirectInputGetDeviceStateHooks.size(); ++i)
    {
        auto& slot = DirectInputGetDeviceStateHooks[i];
        if (slot.InUse && slot.Trampoline != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&slot.Trampoline), DirectInputGetDeviceStateDetours[i]);
    }

    for (std::size_t i = 0; i < DirectInputGetDeviceDataHooks.size(); ++i)
    {
        auto& slot = DirectInputGetDeviceDataHooks[i];
        if (slot.InUse && slot.Trampoline != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&slot.Trampoline), DirectInputGetDeviceDataDetours[i]);
    }

    for (std::size_t i = 0; i < DirectInputReleaseHooks.size(); ++i)
    {
        auto& slot = DirectInputReleaseHooks[i];
        if (slot.InUse && slot.Trampoline != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&slot.Trampoline), DirectInputReleaseDetours[i]);
    }

    const LONG result = DetourTransactionCommit();

    if (result != NO_ERROR)
    {
        LOG_WARN("DirectInput hook removal failed result:{}; retaining trampoline tables for a safe retry", result);
        return false;
    }

    ClearDirectInputHookPointersLocked();
    ClearAllDirectInputDeviceSlotsLocked();
    return true;
}

void EndDirectInputBlockLocked()
{
    // Events still queued came while the menu had input, GetDeviceData keeps only owed releases from them
    const DWORD now = GetTickCount();

    for (DirectInputDeviceSlot& deviceSlot : _state.DirectInputDeviceSlots)
    {
        if (deviceSlot.InUse)
            deviceSlot.BlockedUntilTick = now;
    }
}

HRESULT WINAPI hkDirectInput8Create(HINSTANCE instance, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer)
{
    HRESULT result = DIERR_GENERIC;

    if (o_DirectInput8Create != nullptr)
    {
        ScopedHookBypass bypass;
        result = o_DirectInput8Create(instance, version, riid, out, outer);
    }

    {
        std::unique_lock lock(_state.Mutex);
        _state.DirectInputCreateCallCount++;

        if (SUCCEEDED(result))
        {
            _state.DirectInputCreateSucceededCount++;
            HandleDirectInputCreatedLocked(riid, reinterpret_cast<void**>(out), "DirectInput8Create");
        }
        else
        {
            _state.DirectInputCreateFailedCount++;
        }
    }

    return result;
}

HRESULT WINAPI hkDirectInputCreateA(HINSTANCE instance, DWORD version, void** out, LPUNKNOWN outer)
{
    HRESULT result = DIERR_GENERIC;

    if (o_DirectInputCreateA != nullptr)
    {
        ScopedHookBypass bypass;
        result = o_DirectInputCreateA(instance, version, out, outer);
    }

    {
        std::unique_lock lock(_state.Mutex);
        _state.DirectInputCreateCallCount++;

        if (SUCCEEDED(result))
        {
            _state.DirectInputCreateSucceededCount++;
            HandleLegacyDirectInputCreatedLocked(out, false);
        }
        else
        {
            _state.DirectInputCreateFailedCount++;
        }
    }

    return result;
}

HRESULT WINAPI hkDirectInputCreateW(HINSTANCE instance, DWORD version, void** out, LPUNKNOWN outer)
{
    HRESULT result = DIERR_GENERIC;

    if (o_DirectInputCreateW != nullptr)
    {
        ScopedHookBypass bypass;
        result = o_DirectInputCreateW(instance, version, out, outer);
    }

    {
        std::unique_lock lock(_state.Mutex);
        _state.DirectInputCreateCallCount++;

        if (SUCCEEDED(result))
        {
            _state.DirectInputCreateSucceededCount++;
            HandleLegacyDirectInputCreatedLocked(out, true);
        }
        else
        {
            _state.DirectInputCreateFailedCount++;
        }
    }

    return result;
}

HRESULT WINAPI hkDirectInputCreateEx(HINSTANCE instance, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer)
{
    HRESULT result = DIERR_GENERIC;

    if (o_DirectInputCreateEx != nullptr)
    {
        ScopedHookBypass bypass;
        result = o_DirectInputCreateEx(instance, version, riid, out, outer);
    }

    {
        std::unique_lock lock(_state.Mutex);
        _state.DirectInputCreateCallCount++;

        if (SUCCEEDED(result))
        {
            _state.DirectInputCreateSucceededCount++;
            HandleDirectInputCreatedLocked(riid, reinterpret_cast<void**>(out), "DirectInputCreateEx");
        }
        else
        {
            _state.DirectInputCreateFailedCount++;
        }
    }

    return result;
}

template <std::size_t Slot>
HRESULT WINAPI hkDirectInputCreateDevice(void* directInput, REFGUID guid, void** device, LPUNKNOWN outer)
{
    DirectInputCreateDevice_t original = nullptr;

    {
        std::unique_lock lock(_state.Mutex);
        original = DirectInputCreateDeviceHooks[Slot].Trampoline;
    }

    HRESULT result = CallDirectInputCreateDeviceOriginal(original, directInput, guid, device, outer);

    {
        std::unique_lock lock(_state.Mutex);
        _state.DirectInputCreateDeviceCallCount++;

        if (SUCCEEDED(result) && device != nullptr && *device != nullptr)
        {
            _state.DirectInputCreateDeviceSucceededCount++;
            HookDirectInputDeviceLocked(*device, GetDirectInputDeviceKind(guid));
        }
        else if (FAILED(result))
        {
            _state.DirectInputCreateDeviceFailedCount++;
        }
    }

    return result;
}

template <std::size_t Slot> HRESULT WINAPI hkDirectInputGetDeviceState(void* device, DWORD dataSize, LPVOID data)
{
    TrackUntrackedDirectInputDevice(device);

    DirectInputGetDeviceState_t original = nullptr;
    DirectInputDeviceKind kind = DirectInputDeviceKind::Other;
    bool shouldBlock = false;

    {
        std::unique_lock lock(_state.Mutex);
        kind = GetDirectInputDeviceKindLocked(device);
        _state.DirectInputGetDeviceStateCallCount++;
        shouldBlock = ShouldBlockDirectInputDeviceLocked(kind);

        if (shouldBlock)
            _state.DirectInputGetDeviceStateBlockedCount++;
        else
            _state.DirectInputGetDeviceStatePassedCount++;

        original = DirectInputGetDeviceStateHooks[Slot].Trampoline;
    }

    if (shouldBlock)
    {
        if (data != nullptr && dataSize > 0)
            std::memset(data, 0, dataSize);

        // Outside the lock, it asks the device for its axis ranges
        if (data != nullptr && kind == DirectInputDeviceKind::Other &&
            (dataSize == sizeof(DIJOYSTATE) || dataSize == sizeof(DIJOYSTATE2)))
        {
            ScopedHookBypass bypass;
            FillNeutralJoystickState(device, static_cast<DIJOYSTATE*>(data));
        }

        OPTIINPUT_LOG_VERBOSE("blocking DirectInput GetDeviceState device:{} kind:{} size:{}", device,
                              DirectInputDeviceKindName(kind), dataSize);
        return DI_OK;
    }

    if (original == nullptr)
        return DIERR_GENERIC;

    ScopedHookBypass bypass;
    return original(device, dataSize, data);
}

template <std::size_t Slot>
HRESULT WINAPI hkDirectInputGetDeviceData(void* device, DWORD objectDataSize, LPDIDEVICEOBJECTDATA data, LPDWORD inOut,
                                          DWORD flags)
{
    TrackUntrackedDirectInputDevice(device);

    DirectInputGetDeviceData_t original = nullptr;
    DirectInputDeviceKind kind = DirectInputDeviceKind::Other;
    bool shouldBlock = false;
    std::optional<DWORD> blockedUntil;
    std::bitset<256> downSeenByGame;

    {
        std::unique_lock lock(_state.Mutex);
        kind = GetDirectInputDeviceKindLocked(device);
        _state.DirectInputGetDeviceDataCallCount++;
        shouldBlock = ShouldBlockDirectInputDeviceLocked(kind);

        if (shouldBlock)
            _state.DirectInputGetDeviceDataBlockedCount++;
        else
            _state.DirectInputGetDeviceDataPassedCount++;

        if (const std::size_t slot = FindDirectInputDeviceSlotLocked(device); slot < MaxTrackedDirectInputDevices)
        {
            blockedUntil = _state.DirectInputDeviceSlots[slot].BlockedUntilTick;
            downSeenByGame = _state.DirectInputDeviceSlots[slot].DownSeenByGame;
        }

        original = DirectInputGetDeviceDataHooks[Slot].Trampoline;
    }

    if (original == nullptr)
        return DIERR_GENERIC;

    // A flush or discard without data has nothing to filter
    const bool readable = data != nullptr && inOut != nullptr && objectDataSize >= sizeof(DIDEVICEOBJECTDATA_DX3);
    const DWORD requested = inOut != nullptr ? *inOut : 0;
    HRESULT result = DI_OK;

    {
        ScopedHookBypass bypass;

        // An object size we can't read is flushed while blocked, like before
        if (!readable && shouldBlock && data != nullptr && inOut != nullptr)
        {
            DWORD flushCount = INFINITE;
            original(device, objectDataSize, nullptr, &flushCount, 0);
            *inOut = 0;
            return DI_OK;
        }

        result = original(device, objectDataSize, data, inOut, flags);
    }

    if (!readable || FAILED(result) || (!shouldBlock && !blockedUntil.has_value()))
    {
        if (readable && SUCCEEDED(result) && (flags & DIGDD_PEEK) == 0)
            TrackDirectInputButtons(device, kind, data, objectDataSize, *inOut);

        return result;
    }

    // Events the menu had are dropped, releases of keys and buttons the game saw pressed are kept so nothing
    // stays held after it closes
    const bool peek = (flags & DIGDD_PEEK) != 0;
    DWORD count = *inOut;
    DWORD kept = 0;

    // Games read until a read comes back empty, a read the filter emptied while more is queued reads again
    constexpr int MaxReads = 64;

    for (int reads = 1;; ++reads)
    {
        for (DWORD i = 0; i < count; ++i)
        {
            auto* event = reinterpret_cast<DIDEVICEOBJECTDATA_DX3*>(reinterpret_cast<BYTE*>(data) + i * objectDataSize);
            const bool isButton = IsDirectInputButton(kind, event->dwOfs);
            const bool isDown = (event->dwData & 0x80) != 0;

            // The menu's own tick counts as menu time, it has the key that closed it
            const bool blocked =
                shouldBlock || (blockedUntil.has_value() && static_cast<LONG>(event->dwTimeStamp - *blockedUntil) <= 0);

            if (blocked && !(isButton && !isDown && downSeenByGame[event->dwOfs]))
                continue;

            if (isButton && !peek)
                downSeenByGame[event->dwOfs] = isDown;

            if (kept != i)
                std::memmove(reinterpret_cast<BYTE*>(data) + kept * objectDataSize, event, objectDataSize);

            kept++;
        }

        if (kept != 0 || count == 0 || count < requested || peek || reads == MaxReads)
            break;

        *inOut = requested;

        {
            ScopedHookBypass bypass;
            result = original(device, objectDataSize, data, inOut, flags);
        }

        if (FAILED(result))
        {
            *inOut = 0;
            return result;
        }

        count = *inOut;
    }

    *inOut = kept;

    if (!peek)
    {
        std::unique_lock lock(_state.Mutex);

        if (const std::size_t slot = FindDirectInputDeviceSlotLocked(device); slot < MaxTrackedDirectInputDevices)
        {
            auto& tracked = _state.DirectInputDeviceSlots[slot];
            tracked.DownSeenByGame = downSeenByGame;

            // Read empty, everything queued after this is newer. Unless the menu closed again since.
            if (!shouldBlock && count < requested && tracked.BlockedUntilTick == blockedUntil)
                tracked.BlockedUntilTick.reset();
        }
    }

    if (shouldBlock)
        OPTIINPUT_LOG_VERBOSE("blocking DirectInput GetDeviceData device:{} kind:{} read:{} kept:{}", device,
                              DirectInputDeviceKindName(kind), count, kept);

    return result;
}

template <std::size_t Slot> ULONG WINAPI hkDirectInputDeviceRelease(void* device)
{
    DirectInputDeviceRelease_t original = nullptr;

    {
        std::unique_lock lock(_state.Mutex);
        original = DirectInputReleaseHooks[Slot].Trampoline;
    }

    ULONG result = 0;

    if (original != nullptr)
    {
        ScopedHookBypass bypass;
        result = original(device);
    }

    if (result == 0)
    {
        std::unique_lock lock(_state.Mutex);
        const std::size_t slot = FindDirectInputDeviceSlotLocked(device);

        if (slot < MaxTrackedDirectInputDevices)
            ClearDirectInputDeviceSlotLocked(slot);
    }

    return result;
}

} // namespace OptiInput
