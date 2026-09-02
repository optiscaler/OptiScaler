#include "pch.h"
#include "input_system_internal.h"

#include <algorithm>
#include <cwctype>
#include <string>

#include <winioctl.h>
#include <hidusage.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <hidclass.h>

#ifndef HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER
#define HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER ((USHORT) 0x08)
#endif

namespace OptiInput
{
namespace
{
using HidD_GetPreparsedData_t = BOOLEAN(WINAPI*)(HANDLE, PHIDP_PREPARSED_DATA*);
using HidD_FreePreparsedData_t = BOOLEAN(WINAPI*)(PHIDP_PREPARSED_DATA);
using HidP_GetCaps_t = NTSTATUS(WINAPI*)(PHIDP_PREPARSED_DATA, PHIDP_CAPS);

HidD_GetPreparsedData_t hidGetPreparsedData = nullptr;
HidD_FreePreparsedData_t hidFreePreparsedData = nullptr;
HidP_GetCaps_t hidGetCaps = nullptr;
bool hidApiLoaded = false;

void LoadHidApi()
{
    if (hidApiLoaded)
        return;

    hidApiLoaded = true;

    HMODULE hid = GetModuleHandleW(L"hid.dll");

    if (hid == nullptr)
        hid = LoadLibraryW(L"hid.dll");

    if (hid == nullptr)
        return;

    hidGetPreparsedData = reinterpret_cast<HidD_GetPreparsedData_t>(GetProcAddress(hid, "HidD_GetPreparsedData"));
    hidFreePreparsedData = reinterpret_cast<HidD_FreePreparsedData_t>(GetProcAddress(hid, "HidD_FreePreparsedData"));
    hidGetCaps = reinterpret_cast<HidP_GetCaps_t>(GetProcAddress(hid, "HidP_GetCaps"));
}

std::wstring ToLowerCopy(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });

    return text;
}

bool LooksLikeHidPath(const std::wstring& path)
{
    const std::wstring lower = ToLowerCopy(path);
    return lower.find(L"hid#") != std::wstring::npos || lower.find(L"\\\\.\\hid") != std::wstring::npos;
}

std::wstring AnsiToWide(LPCSTR text)
{
    if (text == nullptr)
        return {};

    const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);

    if (length <= 1)
        return {};

    // MultiByteToWideChar includes the terminating NUL when cbMultiByte == -1.
    // Allocate room for it, then remove it from the returned std::wstring.
    std::wstring result(static_cast<std::size_t>(length), L'\0');

    if (MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), length) != length)
        return {};

    result.resize(static_cast<std::size_t>(length - 1));
    return result;
}

HidDeviceKind HidKindFromUsage(USHORT usagePage, USHORT usage)
{
    if (usagePage != HID_USAGE_PAGE_GENERIC)
        return HidDeviceKind::Other;

    switch (usage)
    {
    case HID_USAGE_GENERIC_MOUSE:
        return HidDeviceKind::Mouse;

    case HID_USAGE_GENERIC_KEYBOARD:
        return HidDeviceKind::Keyboard;

    case HID_USAGE_GENERIC_JOYSTICK:
    case HID_USAGE_GENERIC_GAMEPAD:
    case HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER:
        return HidDeviceKind::Gamepad;

    default:
        return HidDeviceKind::Other;
    }
}

bool TryQueryHidUsage(HANDLE handle, USHORT* usagePage, USHORT* usage)
{
    if (usagePage != nullptr)
        *usagePage = 0;

    if (usage != nullptr)
        *usage = 0;

    LoadHidApi();

    if (hidGetPreparsedData == nullptr || hidFreePreparsedData == nullptr || hidGetCaps == nullptr)
        return false;

    PHIDP_PREPARSED_DATA preparsed = nullptr;

    {
        ScopedHookBypass bypass;

        if (!hidGetPreparsedData(handle, &preparsed) || preparsed == nullptr)
            return false;
    }

    HIDP_CAPS caps {};
    NTSTATUS status = 0;

    {
        ScopedHookBypass bypass;
        status = hidGetCaps(preparsed, &caps);
        hidFreePreparsedData(preparsed);
    }

    if (status < 0)
        return false;

    if (usagePage != nullptr)
        *usagePage = caps.UsagePage;

    if (usage != nullptr)
        *usage = caps.Usage;

    return true;
}

HidHandleSlot* FindHidHandleSlotLocked(HANDLE handle)
{
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        return nullptr;

    for (HidHandleSlot& slot : _state.HidHandleSlots)
    {
        if (slot.InUse && slot.Handle == handle)
            return &slot;
    }

    return nullptr;
}

HidHandleSlot* AllocateHidHandleSlotLocked(HANDLE handle)
{
    HidHandleSlot* slot = FindHidHandleSlotLocked(handle);

    if (slot != nullptr)
        return slot;

    for (HidHandleSlot& candidate : _state.HidHandleSlots)
    {
        if (!candidate.InUse)
        {
            candidate = {};
            candidate.InUse = true;
            candidate.Handle = handle;
            _state.HidTrackedHandleCount++;
            return &candidate;
        }
    }

    return nullptr;
}

void UpdateHidSeenFlagsLocked(HidDeviceKind kind)
{
    switch (kind)
    {
    case HidDeviceKind::Mouse:
        _state.HidMouseHandleSeen = true;
        break;

    case HidDeviceKind::Keyboard:
        _state.HidKeyboardHandleSeen = true;
        break;

    case HidDeviceKind::Gamepad:
        _state.HidGamepadHandleSeen = true;
        break;

    case HidDeviceKind::Other:
    default:
        _state.HidOtherHandleSeen = true;
        break;
    }
}

void TrackHidHandleLocked(HANDLE handle, const std::wstring& path)
{
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        return;

    if (!LooksLikeHidPath(path))
        return;

    _state.HidCreateFileCallCount++;

    USHORT usagePage = 0;
    USHORT usage = 0;
    const bool hasUsage = TryQueryHidUsage(handle, &usagePage, &usage);

    HidHandleSlot* slot = AllocateHidHandleSlotLocked(handle);

    if (slot == nullptr)
    {
        LOG_WARN("HID handle tracking table is full handle:{}", static_cast<void*>(handle));
        return;
    }

    slot->UsagePage = usagePage;
    slot->Usage = usage;
    slot->Kind = hasUsage ? HidKindFromUsage(usagePage, usage) : HidDeviceKind::Other;

    UpdateHidSeenFlagsLocked(slot->Kind);

    OPTIINPUT_LOG_VERBOSE("HID handle tracked handle:{} kind:{} usagePage:{:#x} usage:{:#x}",
                          static_cast<void*>(handle), static_cast<int>(slot->Kind),
                          static_cast<unsigned>(slot->UsagePage), static_cast<unsigned>(slot->Usage));
}

void ClearHidHandleLocked(HANDLE handle)
{
    HidHandleSlot* slot = FindHidHandleSlotLocked(handle);

    if (slot == nullptr)
        return;

    *slot = {};

    if (_state.HidTrackedHandleCount > 0)
        _state.HidTrackedHandleCount--;
}

bool IsTrackedHidMouseLocked(HANDLE handle)
{
    HidHandleSlot* slot = FindHidHandleSlotLocked(handle);
    return slot != nullptr && slot->Kind == HidDeviceKind::Mouse;
}

bool ShouldBlockHidMouseReadLocked(HANDLE handle)
{
    return IsTrackedHidMouseLocked(handle) && ShouldBlockMouseInputLocked();
}

void ZeroReadBuffer(LPVOID buffer, DWORD bufferSize, LPDWORD bytesTransferred)
{
    if (buffer != nullptr && bufferSize > 0)
        SecureZeroMemory(buffer, bufferSize);

    if (bytesTransferred != nullptr)
        *bytesTransferred = 0;
}

bool IsHidInputReportIoctl(DWORD controlCode)
{
#ifdef IOCTL_HID_GET_INPUT_REPORT
    if (controlCode == IOCTL_HID_GET_INPUT_REPORT)
        return true;
#endif

#ifdef IOCTL_HID_READ_REPORT
    if (controlCode == IOCTL_HID_READ_REPORT)
        return true;
#endif

    return false;
}
} // namespace

HANDLE WINAPI hkCreateFileW(LPCWSTR fileName, DWORD desiredAccess, DWORD shareMode,
                            LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition,
                            DWORD flagsAndAttributes, HANDLE templateFile)
{
    HANDLE handle = o_CreateFileW(fileName, desiredAccess, shareMode, securityAttributes, creationDisposition,
                                  flagsAndAttributes, templateFile);

    if (handle != nullptr && handle != INVALID_HANDLE_VALUE && fileName != nullptr)
    {
        std::unique_lock lock(_state.Mutex);
        TrackHidHandleLocked(handle, fileName);
    }

    return handle;
}

HANDLE WINAPI hkCreateFileA(LPCSTR fileName, DWORD desiredAccess, DWORD shareMode,
                            LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition,
                            DWORD flagsAndAttributes, HANDLE templateFile)
{
    HANDLE handle = o_CreateFileA(fileName, desiredAccess, shareMode, securityAttributes, creationDisposition,
                                  flagsAndAttributes, templateFile);

    if (handle != nullptr && handle != INVALID_HANDLE_VALUE && fileName != nullptr)
    {
        const std::wstring widePath = AnsiToWide(fileName);

        std::unique_lock lock(_state.Mutex);
        TrackHidHandleLocked(handle, widePath);
    }

    return handle;
}

BOOL WINAPI hkReadFile(HANDLE file, LPVOID buffer, DWORD bytesToRead, LPDWORD bytesRead, LPOVERLAPPED overlapped)
{
    const BOOL result = o_ReadFile(file, buffer, bytesToRead, bytesRead, overlapped);

    if (!result)
        return result;

    bool shouldZero = false;
    DWORD bytesToZero = bytesRead != nullptr ? *bytesRead : bytesToRead;

    {
        std::unique_lock lock(_state.Mutex);

        if (IsTrackedHidMouseLocked(file))
        {
            _state.HidReadFileCallCount++;

            if (ShouldBlockHidMouseReadLocked(file))
            {
                _state.HidReadFileBlockedCount++;
                shouldZero = true;
            }
            else
            {
                _state.HidReadFilePassedCount++;
            }
        }
    }

    if (shouldZero)
        ZeroReadBuffer(buffer, bytesToZero, bytesRead);

    return result;
}

BOOL WINAPI hkDeviceIoControl(HANDLE device, DWORD controlCode, LPVOID inBuffer, DWORD inBufferSize, LPVOID outBuffer,
                              DWORD outBufferSize, LPDWORD bytesReturned, LPOVERLAPPED overlapped)
{
    const BOOL result = o_DeviceIoControl(device, controlCode, inBuffer, inBufferSize, outBuffer, outBufferSize,
                                          bytesReturned, overlapped);

    if (!result)
        return result;

    bool shouldZero = false;
    DWORD bytesToZero = bytesReturned != nullptr ? *bytesReturned : outBufferSize;

    {
        std::unique_lock lock(_state.Mutex);

        if (IsTrackedHidMouseLocked(device))
        {
            _state.HidDeviceIoControlCallCount++;

            if (ShouldBlockHidMouseReadLocked(device) && IsHidInputReportIoctl(controlCode))
            {
                _state.HidDeviceIoControlBlockedCount++;
                shouldZero = true;
            }
            else
            {
                _state.HidDeviceIoControlPassedCount++;
            }
        }
    }

    if (shouldZero)
        ZeroReadBuffer(outBuffer, bytesToZero, bytesReturned);

    return result;
}

BOOL WINAPI hkCloseHandle(HANDLE handle)
{
    const BOOL result = o_CloseHandle(handle);

    if (result)
    {
        std::unique_lock lock(_state.Mutex);
        ClearHidHandleLocked(handle);
    }

    return result;
}

} // namespace OptiInput
