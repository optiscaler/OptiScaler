#pragma once

#include "input_system.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <winerror.h>
#include <Xinput.h>

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>

#ifndef OPTIINPUT_VERBOSE_LOGGING
#define OPTIINPUT_VERBOSE_LOGGING 0
#endif

#if OPTIINPUT_VERBOSE_LOGGING
#define OPTIINPUT_LOG_VERBOSE(...) LOG_TRACE(__VA_ARGS__)
#else
#define OPTIINPUT_LOG_VERBOSE(...) ((void) 0)
#endif

namespace OptiInput
{
enum class InputMessageSource
{
    WndProc,
    MessageQueue,
};

enum class RawSanitizeAction
{
    Pass,
    SanitizeAll,
    SanitizeMouseKeepAllowedButtonUps,
};

struct RawMouseSanitizeResult
{
    RawSanitizeAction Action = RawSanitizeAction::Pass;
    USHORT AllowedButtonUpFlags = 0;
};

struct RawInputSanitizeDecision
{
    HRAWINPUT Handle = nullptr;
    RawSanitizeAction Action = RawSanitizeAction::Pass;
    USHORT AllowedMouseButtonUpFlags = 0;
};

struct WindowsHookSlot
{
    bool InUse = false;
    HHOOK Hook = nullptr;
    int HookType = 0;
    HOOKPROC OriginalProc = nullptr;
    DWORD ThreadId = 0;
    HINSTANCE Module = nullptr;
};

enum class DirectInputDeviceKind
{
    Other,
    Keyboard,
    Mouse,
};

struct DirectInputDeviceSlot
{
    bool InUse = false;
    void* Device = nullptr;
    DirectInputDeviceKind Kind = DirectInputDeviceKind::Other;
};

enum class HidDeviceKind
{
    Other,
    Keyboard,
    Mouse,
    Gamepad,
};

struct HidHandleSlot
{
    bool InUse = false;
    HANDLE Handle = nullptr;
    HidDeviceKind Kind = HidDeviceKind::Other;
    USHORT UsagePage = 0;
    USHORT Usage = 0;
};

constexpr std::size_t MaxTrackedWindowsHooks = 32;
constexpr std::size_t MaxTrackedDirectInputDevices = 32;
constexpr std::size_t MaxTrackedHidHandles = 64;
constexpr std::size_t MaxRawInputSanitizeCacheEntries = 128;

struct ButtonState
{
    bool Down = false;
    bool Pressed = false;
    bool Released = false;

    // Keep blocked down/up pairs symmetrical.
    bool BlockedDown = false;

    DWORD LastMessageTime = 0;
};

struct InputState
{
    // Target/game window and optional local input window.
    HWND TargetHwnd = nullptr;
    HWND TargetRootHwnd = nullptr;
    HWND InputHwnd = nullptr;
    HWND InputRootHwnd = nullptr;

    WNDPROC OriginalWndProc = nullptr;

    bool Initialized = false;
    bool HooksInstalled = false;
    bool Focused = false;

    bool MenuVisible = false;
    bool BlockMouse = false;
    bool BlockKeyboard = false;
    bool BlockCursor = false;

    bool IsUwp = false;
    bool UseWndProcSubclass = true;
    bool WndProcSubclassed = false;
    bool ExternalTargetProcess = false;
    bool HasExplicitInputHwnd = false;

    bool PolledInputActive = false;
    bool PolledInputUsedThisFrame = false;
    bool PolledMouseUsedThisFrame = false;
    bool PolledKeyboardUsedThisFrame = false;

    InputAcquisitionMode AcquisitionMode = InputAcquisitionMode::None;

    bool ExternalVirtualMouseActive = false;
    bool ExternalVirtualMouseUsedThisFrame = false;
    bool ExternalVirtualMouseRelativeUsedThisFrame = false;
    bool ExternalVirtualMouseAuthoritative = false;
    bool ExternalGetCursorPosVirtualizedThisFrame = false;
    bool ExternalCursorRecenteringDetected = false;
    bool ExternalVirtualMouseInitialized = false;
    bool ExternalLowLevelMouseHookInstalled = false;
    bool ExternalRawInputSinkRegistered = false;
    bool ExternalRawInputSinkPumpUsedThisFrame = false;

    bool ImGuiMouseDrawCursorForced = false;
    bool SavedImGuiMouseDrawCursor = false;
    bool ImGuiNoMouseCursorChangeForced = false;
    bool SavedImGuiNoMouseCursorChange = false;

    bool ReceivedWindowMessageThisFrame = false;
    bool ReceivedQueueMessageThisFrame = false;
    bool ReceivedAnyInputThisFrame = false;
    bool ReceivedRawInputThisFrame = false;

    HWND RawMouseTargetHwnd = nullptr;
    HWND RawKeyboardTargetHwnd = nullptr;

    DWORD RawMouseFlags = 0;
    DWORD RawKeyboardFlags = 0;

    bool RawMouseRegistered = false;
    bool RawKeyboardRegistered = false;

    bool RawMouseNoLegacy = false;
    bool RawKeyboardNoLegacy = false;

    bool RawMouseInputSink = false;
    bool RawKeyboardInputSink = false;

    bool RawMouseCaptureMouse = false;

    DWORD TargetProcessId = 0;
    DWORD TargetThreadId = 0;
    DWORD InputProcessId = 0;
    DWORD InputThreadId = 0;
    DWORD CurrentProcessId = 0;

    int LastPressedKey = 0;

    std::array<ButtonState, 256> Keys {};
    std::array<ButtonState, 5> MouseButtons {};

    std::array<bool, 256> RawKeyboardBlockedDown {};
    std::array<bool, 5> RawMouseBlockedDown {};

    std::array<RawInputSanitizeDecision, MaxRawInputSanitizeCacheEntries> RawInputSanitizeCache {};
    std::size_t RawInputSanitizeCacheWriteIndex = 0;

    std::array<WindowsHookSlot, MaxTrackedWindowsHooks> WindowsHookSlots {};
    std::array<bool, 256> WindowsHookKeyboardBlockedDown {};
    std::array<bool, 5> WindowsHookMouseBlockedDown {};

    std::uint64_t WindowMessageBlockedCount = 0;
    std::uint64_t WindowMessagePassedCount = 0;
    std::uint64_t QueueMessageBlockedCount = 0;
    std::uint64_t QueueMessagePassedCount = 0;

    std::uint64_t GetAsyncKeyStateBlockedCount = 0;
    std::uint64_t GetKeyStateBlockedCount = 0;
    std::uint64_t GetKeyboardStateFilteredCount = 0;

    std::uint64_t GetCursorPosBlockedCount = 0;
    std::uint64_t GetPhysicalCursorPosBlockedCount = 0;
    std::uint64_t GetMessagePosBlockedCount = 0;
    std::uint64_t SetCursorPosBlockedCount = 0;
    std::uint64_t SetPhysicalCursorPosBlockedCount = 0;
    std::uint64_t ClipCursorBlockedCount = 0;
    std::uint64_t GetClipCursorVirtualizedCount = 0;

    std::uint64_t SendInputMouseBlockedCount = 0;
    std::uint64_t SendInputKeyboardBlockedCount = 0;
    std::uint64_t MouseEventBlockedCount = 0;
    std::uint64_t PostMouseMessageBlockedCount = 0;
    std::uint64_t PostMouseMessagePassedCount = 0;
    std::uint64_t SendMouseMessageBlockedCount = 0;
    std::uint64_t SendMouseMessagePassedCount = 0;

    std::uint64_t RawKeyboardSanitizedCount = 0;
    std::uint64_t RawKeyboardPassedCount = 0;
    std::uint64_t RawMouseSanitizedCount = 0;
    std::uint64_t RawMousePartialPassedCount = 0;
    std::uint64_t RawMousePassedCount = 0;

    std::uint64_t WindowsHookKeyboardBlockedCount = 0;
    std::uint64_t WindowsHookKeyboardPassedCount = 0;
    std::uint64_t WindowsHookMouseBlockedCount = 0;
    std::uint64_t WindowsHookMousePassedCount = 0;

    std::uint64_t PolledInputFrameCount = 0;
    std::uint64_t PolledMouseFrameCount = 0;
    std::uint64_t PolledKeyboardFrameCount = 0;

    std::uint64_t ExternalVirtualMouseFrameCount = 0;
    std::uint64_t ExternalVirtualMouseAuthoritativeFrameCount = 0;
    std::uint64_t ExternalGetCursorPosVirtualizedCount = 0;
    std::uint64_t ExternalVirtualMouseAbsoluteSuppressedCount = 0;
    std::uint64_t ExternalMouseDeltaEventCount = 0;
    std::uint64_t ExternalCursorRecenteringEventCount = 0;
    std::uint64_t ExternalRawInputSinkMessageCount = 0;
    std::uint64_t ExternalRawInputSinkPumpFrameCount = 0;

    HMODULE GameInputModule = nullptr;
    HMODULE WindowsGamingInputModule = nullptr;
    HMODULE XInputModule = nullptr;
    HMODULE DirectInputModule = nullptr;
    HMODULE DirectInputLegacyModule = nullptr;

    bool GameInputModuleLoaded = false;
    bool GameInputCreateExportFound = false;
    bool GameInputCreateHookInstalled = false;
    bool GameInputCreateHookAttempted = false;
    bool GameInputInterfaceSeen = false;
    bool WindowsGamingInputModuleLoaded = false;

    bool XInputModuleLoaded = false;
    bool XInputGetStateHookInstalled = false;
    bool XInputGetStateExHookInstalled = false;
    bool XInputGetKeystrokeHookInstalled = false;
    bool XInputSetStateHookInstalled = false;

    bool DirectInputModuleLoaded = false;
    bool DirectInputLegacyModuleLoaded = false;
    bool DirectInput8CreateHookInstalled = false;
    bool DirectInputCreateAHookInstalled = false;
    bool DirectInputCreateWHookInstalled = false;
    bool DirectInputCreateExHookInstalled = false;
    bool DirectInputCreateDeviceAHookInstalled = false;
    bool DirectInputCreateDeviceWHookInstalled = false;
    bool DirectInputGetDeviceStateHookInstalled = false;
    bool DirectInputGetDeviceDataHookInstalled = false;
    bool DirectInputDeviceReleaseHookInstalled = false;
    bool DirectInputKeyboardDeviceSeen = false;
    bool DirectInputMouseDeviceSeen = false;
    bool DirectInputOtherDeviceSeen = false;

    HRESULT GameInputLastCreateResult = S_OK;

    std::uint64_t GameInputCreateCallCount = 0;
    std::uint64_t GameInputCreateSucceededCount = 0;
    std::uint64_t GameInputCreateFailedCount = 0;

    std::uint64_t XInputGetStateCallCount = 0;
    std::uint64_t XInputGetStateBlockedCount = 0;
    std::uint64_t XInputGetStatePassedCount = 0;
    std::uint64_t XInputGetKeystrokeCallCount = 0;
    std::uint64_t XInputGetKeystrokeBlockedCount = 0;
    std::uint64_t XInputGetKeystrokePassedCount = 0;
    std::uint64_t XInputSetStateCallCount = 0;
    std::uint64_t XInputSetStateBlockedCount = 0;
    std::uint64_t XInputSetStatePassedCount = 0;

    std::array<DirectInputDeviceSlot, MaxTrackedDirectInputDevices> DirectInputDeviceSlots {};
    std::uint64_t DirectInputCreateCallCount = 0;
    std::uint64_t DirectInputCreateSucceededCount = 0;
    std::uint64_t DirectInputCreateFailedCount = 0;
    std::uint64_t DirectInputCreateDeviceCallCount = 0;
    std::uint64_t DirectInputCreateDeviceSucceededCount = 0;
    std::uint64_t DirectInputCreateDeviceFailedCount = 0;
    std::uint64_t DirectInputTrackedDeviceCount = 0;
    std::uint64_t DirectInputGetDeviceStateCallCount = 0;
    std::uint64_t DirectInputGetDeviceStateBlockedCount = 0;
    std::uint64_t DirectInputGetDeviceStatePassedCount = 0;
    std::uint64_t DirectInputGetDeviceDataCallCount = 0;
    std::uint64_t DirectInputGetDeviceDataBlockedCount = 0;
    std::uint64_t DirectInputGetDeviceDataPassedCount = 0;

    std::array<HidHandleSlot, MaxTrackedHidHandles> HidHandleSlots {};
    bool HidMouseHandleSeen = false;
    bool HidKeyboardHandleSeen = false;
    bool HidGamepadHandleSeen = false;
    bool HidOtherHandleSeen = false;
    std::uint64_t HidCreateFileCallCount = 0;
    std::uint64_t HidTrackedHandleCount = 0;
    std::uint64_t HidReadFileCallCount = 0;
    std::uint64_t HidReadFileBlockedCount = 0;
    std::uint64_t HidReadFilePassedCount = 0;
    std::uint64_t HidDeviceIoControlCallCount = 0;
    std::uint64_t HidDeviceIoControlBlockedCount = 0;
    std::uint64_t HidDeviceIoControlPassedCount = 0;
    std::uint64_t MouseMovePointsBlockedCount = 0;

    POINT MouseClientPos {};
    POINT MouseScreenPos {};
    POINT LastMouseClientPos {};

    // Cursor position returned to the game while the menu owns cursor input.
    // MouseScreenPos may still move because the menu uses the real cursor.
    bool HasBlockedCursorScreenPos = false;
    POINT BlockedCursorScreenPos {};

    POINT ExternalVirtualMouseClient {};
    POINT ExternalLastMouseHookScreen {};
    bool ExternalLastMouseHookScreenValid = false;
    LONG ExternalPendingMouseDeltaX = 0;
    LONG ExternalPendingMouseDeltaY = 0;
    HHOOK ExternalLowLevelMouseHook = nullptr;
    HWND ExternalRawInputSinkHwnd = nullptr;
    DWORD ExternalRawInputSinkThreadId = 0;

    float MouseWheel = 0.0f;

    RECT SavedClipRect {};
    bool HasSavedClipRect = false;
    bool SavedClipWasActive = false;

    RECT DeferredClipRect {};
    bool HasDeferredClipRect = false;
    bool DeferredClipIsNull = false;

    bool CursorClipReleasedForMenu = false;

    std::wstring TextInput;

    std::recursive_mutex Mutex;
};

using GetAsyncKeyState_t = SHORT(WINAPI*)(int);
using GetKeyState_t = SHORT(WINAPI*)(int);
using GetKeyboardState_t = BOOL(WINAPI*)(PBYTE);
using GetCursorPos_t = BOOL(WINAPI*)(LPPOINT);
using SetCursorPos_t = BOOL(WINAPI*)(int, int);
using GetPhysicalCursorPos_t = BOOL(WINAPI*)(LPPOINT);
using SetPhysicalCursorPos_t = BOOL(WINAPI*)(int, int);
using GetMessagePos_t = DWORD(WINAPI*)();
using GetMouseMovePointsEx_t = int(WINAPI*)(UINT, LPMOUSEMOVEPOINT, LPMOUSEMOVEPOINT, int, DWORD);
using ClipCursor_t = BOOL(WINAPI*)(const RECT*);
using SendInput_t = UINT(WINAPI*)(UINT, LPINPUT, int);
using MouseEvent_t = void(WINAPI*)(DWORD, DWORD, DWORD, DWORD, ULONG_PTR);
using PostMessageA_t = BOOL(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using PostMessageW_t = BOOL(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using SendMessageA_t = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using SendMessageW_t = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);
using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using ReadFile_t = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using DeviceIoControl_t = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using CloseHandle_t = BOOL(WINAPI*)(HANDLE);
using PeekMessageA_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using PeekMessageW_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using GetMessageA_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
using GetMessageW_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);
using GetRawInputData_t = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using GetRawInputBuffer_t = UINT(WINAPI*)(PRAWINPUT, PUINT, UINT);
using RegisterRawInputDevices_t = BOOL(WINAPI*)(PCRAWINPUTDEVICE, UINT, UINT);
using GetClipCursor_t = BOOL(WINAPI*)(LPRECT);
using SetWindowsHookExA_t = HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD);
using SetWindowsHookExW_t = HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD);
using UnhookWindowsHookEx_t = BOOL(WINAPI*)(HHOOK);
using GameInputCreate_t = HRESULT(WINAPI*)(void**);
using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using XInputGetKeystroke_t = DWORD(WINAPI*)(DWORD, DWORD, PXINPUT_KEYSTROKE);
using XInputSetState_t = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DirectInputCreateA_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, void**, LPUNKNOWN);
using DirectInputCreateW_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, void**, LPUNKNOWN);
using DirectInputCreateEx_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DirectInputCreateDevice_t = HRESULT(WINAPI*)(void*, REFGUID, void**, LPUNKNOWN);
using DirectInputGetDeviceState_t = HRESULT(WINAPI*)(void*, DWORD, LPVOID);
using DirectInputGetDeviceData_t = HRESULT(WINAPI*)(void*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
using DirectInputDeviceRelease_t = ULONG(WINAPI*)(void*);

extern InputState _state;

extern GetAsyncKeyState_t o_GetAsyncKeyState;
extern GetKeyState_t o_GetKeyState;
extern GetKeyboardState_t o_GetKeyboardState;
extern GetCursorPos_t o_GetCursorPos;
extern SetCursorPos_t o_SetCursorPos;
extern GetPhysicalCursorPos_t o_GetPhysicalCursorPos;
extern SetPhysicalCursorPos_t o_SetPhysicalCursorPos;
extern GetMessagePos_t o_GetMessagePos;
extern GetMouseMovePointsEx_t o_GetMouseMovePointsEx;
extern ClipCursor_t o_ClipCursor;
extern SendInput_t o_SendInput;
extern MouseEvent_t o_mouse_event;
extern PostMessageA_t o_PostMessageA;
extern PostMessageW_t o_PostMessageW;
extern SendMessageA_t o_SendMessageA;
extern SendMessageW_t o_SendMessageW;
extern CreateFileA_t o_CreateFileA;
extern CreateFileW_t o_CreateFileW;
extern ReadFile_t o_ReadFile;
extern DeviceIoControl_t o_DeviceIoControl;
extern CloseHandle_t o_CloseHandle;
extern PeekMessageA_t o_PeekMessageA;
extern PeekMessageW_t o_PeekMessageW;
extern GetMessageA_t o_GetMessageA;
extern GetMessageW_t o_GetMessageW;
extern GetRawInputData_t o_GetRawInputData;
extern GetRawInputBuffer_t o_GetRawInputBuffer;
extern RegisterRawInputDevices_t o_RegisterRawInputDevices;
extern GetClipCursor_t o_GetClipCursor;
extern SetWindowsHookExA_t o_SetWindowsHookExA;
extern SetWindowsHookExW_t o_SetWindowsHookExW;
extern UnhookWindowsHookEx_t o_UnhookWindowsHookEx;
extern GameInputCreate_t o_GameInputCreate;
extern XInputGetState_t o_XInputGetState;
extern XInputGetState_t o_XInputGetStateEx;
extern XInputGetKeystroke_t o_XInputGetKeystroke;
extern XInputSetState_t o_XInputSetState;
extern DirectInput8Create_t o_DirectInput8Create;
extern DirectInputCreateA_t o_DirectInputCreateA;
extern DirectInputCreateW_t o_DirectInputCreateW;
extern DirectInputCreateEx_t o_DirectInputCreateEx;
extern DirectInputCreateDevice_t o_DirectInputCreateDeviceA;
extern DirectInputCreateDevice_t o_DirectInputCreateDeviceW;
extern DirectInputGetDeviceState_t o_DirectInputDeviceGetDeviceState;
extern DirectInputGetDeviceData_t o_DirectInputDeviceGetDeviceData;
extern DirectInputDeviceRelease_t o_DirectInputDeviceRelease;

extern thread_local int bypassHookDepth;

class ScopedHookBypass
{
  public:
    ScopedHookBypass() { bypassHookDepth++; }
    ~ScopedHookBypass() { bypassHookDepth--; }

    ScopedHookBypass(const ScopedHookBypass&) = delete;
    ScopedHookBypass& operator=(const ScopedHookBypass&) = delete;
    ScopedHookBypass(ScopedHookBypass&&) = delete;
    ScopedHookBypass& operator=(ScopedHookBypass&&) = delete;
};

// Lifecycle
bool InstallHooks();
void RemoveHooks();
void ReleaseTrackedWindowsHooksLocked();

// GameInput / Windows.Gaming.Input
void UpdateGameInputIntegrationLocked();
void RemoveGameInputHooksLocked();
HRESULT WINAPI hkGameInputCreate(void** gameInput);

// XInput
void UpdateXInputIntegrationLocked();
void RemoveXInputHooksLocked();
DWORD WINAPI hkXInputGetState(DWORD userIndex, XINPUT_STATE* state);
DWORD WINAPI hkXInputGetStateEx(DWORD userIndex, XINPUT_STATE* state);
DWORD WINAPI hkXInputGetKeystroke(DWORD userIndex, DWORD reserved, PXINPUT_KEYSTROKE keystroke);
DWORD WINAPI hkXInputSetState(DWORD userIndex, XINPUT_VIBRATION* vibration);

// DirectInput
void UpdateDirectInputIntegrationLocked();
void RemoveDirectInputHooksLocked();
HRESULT WINAPI hkDirectInput8Create(HINSTANCE instance, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer);
HRESULT WINAPI hkDirectInputCreateA(HINSTANCE instance, DWORD version, void** out, LPUNKNOWN outer);
HRESULT WINAPI hkDirectInputCreateW(HINSTANCE instance, DWORD version, void** out, LPUNKNOWN outer);
HRESULT WINAPI hkDirectInputCreateEx(HINSTANCE instance, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer);
HRESULT WINAPI hkDirectInputCreateDeviceA(void* directInput, REFGUID guid, void** device, LPUNKNOWN outer);
HRESULT WINAPI hkDirectInputCreateDeviceW(void* directInput, REFGUID guid, void** device, LPUNKNOWN outer);
HRESULT WINAPI hkDirectInputGetDeviceState(void* device, DWORD dataSize, LPVOID data);
HRESULT WINAPI hkDirectInputGetDeviceData(void* device, DWORD objectDataSize, LPDIDEVICEOBJECTDATA data, LPDWORD inOut,
                                          DWORD flags);
ULONG WINAPI hkDirectInputDeviceRelease(void* device);

// Target/input window
void SetTargetWindow(HWND hwnd, bool isUwp, bool useWndProcSubclass);
void SetInputWindow(HWND hwnd, bool useWndProcSubclass, bool explicitInputHwnd);
bool InstallWindowSubclass(HWND hwnd);
void RemoveWindowSubclass();
bool TryGetWindowProc(HWND hwnd, WNDPROC* wndProc);
void ClearTargetWindowLocked();
void ClearInputWindowLocked();
bool ValidateTargetWindowLocked();
bool ValidateInputWindowLocked();
void ValidateWindowSubclassLocked();
bool IsTargetWindow(HWND hwnd);
bool IsInputWindow(HWND hwnd);
void UpdateFocusState(HWND targetHwnd);
LRESULT CALLBACK OptiInputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Frame/menu policy
void ApplyMenuVisibilityChangeLocked(bool visible);
void ResetRawInputBlockStateLocked();
void ResetRawInputSanitizeCacheLocked();
bool ShouldApplyBlockingPolicyLocked();
bool ShouldBlockKeyboardInputLocked();
bool ShouldBlockMouseInputLocked();
bool ShouldBlockCursorInputLocked();
void LogInputHealthSnapshotLocked(const char* origin);
void PollInputFallbackLocked();

// Cursor clipping
void BeginCursorClipBlockLocked();
void EndCursorClipBlockLocked();
void StoreDeferredClipCursorLocked(const RECT* rect);
bool IsVirtualScreenRect(const RECT& rect);
RECT GetVirtualScreenRect();
bool RectEquals(const RECT& a, const RECT& b);

// Message / state handling
bool IsMouseMessage(UINT msg);
bool IsKeyboardMessage(UINT msg);
bool IsMouseVirtualKey(int vk);
bool ShouldBlockMouseWindowMessageLocked(HWND hwnd, UINT msg);
int MouseMessageToButton(UINT msg, WPARAM wParam);
void SetKeyDown(int vk, DWORD messageTime, bool blocked);
bool SetKeyUp(int vk, DWORD messageTime);
void SetMouseDown(int button, DWORD messageTime, bool blocked);
bool SetMouseUp(int button, DWORD messageTime);
void ClearTransientState();
void UpdateMousePositionFromClient(HWND hwnd, LPARAM lParam);
bool ProcessRemovedMessage(MSG* msg);
void ConsumeMessage(MSG* msg);
SHORT RealGetAsyncKeyStateSafe(int vk);
SHORT RealGetKeyStateSafe(int vk);
BOOL RealGetCursorPosSafe(LPPOINT point);
void SetMouseDownFromRawState(int button, DWORD messageTime, bool blocked);
void SetMouseUpFromRawState(int button, DWORD messageTime);
void ResetButtonBlockedStateLocked();
void SetKeyUpStateOnly(int vk, DWORD messageTime);
void SetMouseUpStateOnly(int button, DWORD messageTime);

// Raw input
RawSanitizeAction GetRawInputSanitizeActionLocked(const RAWINPUT& input, USHORT* allowedMouseButtonUpFlags);
RawSanitizeAction GetRawKeyboardSanitizeActionLocked(const RAWKEYBOARD& keyboard);
RawMouseSanitizeResult GetRawMouseSanitizeActionLocked(const RAWMOUSE& mouse);
RawInputSanitizeDecision GetRawInputSanitizeDecisionLocked(HRAWINPUT rawInput, const RAWINPUT& input);
void ApplyRawInputSanitizeActionLocked(RAWINPUT& input, RawSanitizeAction action, USHORT allowedMouseButtonUpFlags);
void RecordRawInputSanitizeCounterLocked(const RAWINPUT& input, RawSanitizeAction action);
bool IsRawInputPacketReadable(const RAWINPUT& input, UINT availableSize);
void SanitizeRawMouseAllLocked(RAWINPUT& input);
void SanitizeRawMouseKeepAllowedButtonUpsLocked(RAWINPUT& input, USHORT allowedButtonUpFlags);
void SanitizeRawKeyboardLocked(RAWINPUT& input);
int NormalizeRawKeyboardVirtualKey(const RAWKEYBOARD& keyboard);
void HandleRawInputLocked(HRAWINPUT rawInputHandle);

// Win32 hook tracking
bool IsTrackedWindowsHookType(int hookType);
bool IsKeyboardWindowsHookType(int hookType);
bool IsMouseWindowsHookType(int hookType);
bool ShouldInterceptWindowsHookInstall(int hookType, HOOKPROC proc, DWORD threadId);
HINSTANCE GetWindowsHookInstallModule(int hookType, DWORD threadId, HINSTANCE originalModule);
int AllocateWindowsHookSlotLocked(int hookType, HOOKPROC proc, HINSTANCE module, DWORD threadId);
void ClearWindowsHookSlotLocked(std::size_t slotIndex);
void ClearWindowsHookSlotByHandleLocked(HHOOK hook);
std::uint32_t CountTrackedWindowsHooksLocked();
bool ShouldBlockWindowsHookCallbackLocked(WindowsHookSlot& slot, int code, WPARAM wParam, LPARAM lParam);
int WindowsHookMouseMessageToButton(int hookType, WPARAM wParam, LPARAM lParam);
HOOKPROC GetWindowsHookProxyProc(std::size_t slotIndex);
LRESULT CALLBACK InvokeWindowsHookProxy(std::size_t slotIndex, int code, WPARAM wParam, LPARAM lParam);
void UpdateExternalMouseHookLocked();
void RemoveExternalMouseHookLocked();
void EnsureExternalRawInputSinkLocked();
void PumpExternalRawInputSinkLocked();
void RemoveExternalRawInputSinkLocked();
void AccumulateExternalRawMouseDeltaLocked(const RAWMOUSE& mouse);
void SyncAggregateModifierStateLocked();

// Detoured Win32 entry points
BOOL WINAPI hkPeekMessageA(LPMSG msg, HWND hwnd, UINT messageFilterMin, UINT messageFilterMax, UINT removeMsg);
BOOL WINAPI hkPeekMessageW(LPMSG msg, HWND hwnd, UINT messageFilterMin, UINT messageFilterMax, UINT removeMsg);
BOOL WINAPI hkGetMessageA(LPMSG msg, HWND hwnd, UINT messageFilterMin, UINT messageFilterMax);
BOOL WINAPI hkGetMessageW(LPMSG msg, HWND hwnd, UINT messageFilterMin, UINT messageFilterMax);
SHORT WINAPI hkGetAsyncKeyState(int vk);
SHORT WINAPI hkGetKeyState(int vk);
BOOL WINAPI hkGetKeyboardState(PBYTE keyState);
BOOL WINAPI hkGetCursorPos(LPPOINT point);
BOOL WINAPI hkSetCursorPos(int x, int y);
BOOL WINAPI hkGetPhysicalCursorPos(LPPOINT point);
BOOL WINAPI hkSetPhysicalCursorPos(int x, int y);
DWORD WINAPI hkGetMessagePos();
int WINAPI hkGetMouseMovePointsEx(UINT pointSize, LPMOUSEMOVEPOINT point, LPMOUSEMOVEPOINT buffer, int bufferPoints,
                                  DWORD resolution);
BOOL WINAPI hkClipCursor(const RECT* rect);
UINT WINAPI hkSendInput(UINT inputCount, LPINPUT inputs, int size);
void WINAPI hkmouse_event(DWORD flags, DWORD dx, DWORD dy, DWORD data, ULONG_PTR extraInfo);
BOOL WINAPI hkPostMessageA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI hkPostMessageW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI hkSendMessageA(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI hkSendMessageW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
HANDLE WINAPI hkCreateFileA(LPCSTR fileName, DWORD desiredAccess, DWORD shareMode,
                            LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition,
                            DWORD flagsAndAttributes, HANDLE templateFile);
HANDLE WINAPI hkCreateFileW(LPCWSTR fileName, DWORD desiredAccess, DWORD shareMode,
                            LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition,
                            DWORD flagsAndAttributes, HANDLE templateFile);
BOOL WINAPI hkReadFile(HANDLE file, LPVOID buffer, DWORD bytesToRead, LPDWORD bytesRead, LPOVERLAPPED overlapped);
BOOL WINAPI hkDeviceIoControl(HANDLE device, DWORD controlCode, LPVOID inBuffer, DWORD inBufferSize, LPVOID outBuffer,
                              DWORD outBufferSize, LPDWORD bytesReturned, LPOVERLAPPED overlapped);
BOOL WINAPI hkCloseHandle(HANDLE handle);
UINT WINAPI hkGetRawInputData(HRAWINPUT rawInput, UINT command, LPVOID data, PUINT size, UINT headerSize);
UINT WINAPI hkGetRawInputBuffer(PRAWINPUT data, PUINT size, UINT headerSize);
BOOL WINAPI hkRegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT deviceCount, UINT size);
BOOL WINAPI hkGetClipCursor(LPRECT rect);
HHOOK WINAPI hkSetWindowsHookExA(int hookType, HOOKPROC proc, HINSTANCE module, DWORD threadId);
HHOOK WINAPI hkSetWindowsHookExW(int hookType, HOOKPROC proc, HINSTANCE module, DWORD threadId);
BOOL WINAPI hkUnhookWindowsHookEx(HHOOK hook);

} // namespace OptiInput
