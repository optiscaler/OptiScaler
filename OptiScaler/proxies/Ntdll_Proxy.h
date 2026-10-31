#pragma once

#include "SysUtils.h"

#include <detours/detours.h>

#include <winternl.h>

class NtdllProxy
{
  public:
    typedef NTSTATUS(NTAPI* PFN_LdrLoadDll)(PWSTR PathToFile OPTIONAL, PULONG Flags OPTIONAL,
                                            PUNICODE_STRING ModuleFileName, PHANDLE ModuleHandle);

    typedef NTSTATUS(NTAPI* PFN_NtLoadDll)(PUNICODE_STRING PathToFile OPTIONAL, PULONG Flags OPTIONAL,
                                           PUNICODE_STRING ModuleFileName, PHANDLE ModuleHandle);

    typedef NTSTATUS(NTAPI* PFN_LdrUnloadDll)(PVOID ModuleHandle);

    static HMODULE LoadLibraryExW_Ldr(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        // Search flags aren't passed to LdrLoadDll, a bare name is loaded from system32 by full path
        std::wstring systemPath;
        if ((dwFlags & LOAD_LIBRARY_SEARCH_SYSTEM32) && lpLibFileName != nullptr && !wcschr(lpLibFileName, L'\\') &&
            !wcschr(lpLibFileName, L'/'))
        {
            wchar_t systemDir[MAX_PATH] {};
            auto length = GetSystemDirectoryW(systemDir, MAX_PATH);

            if (length > 0 && length < MAX_PATH)
            {
                systemPath = std::wstring(systemDir) + L"\\" + lpLibFileName;
                lpLibFileName = systemPath.c_str();
            }
        }

        UNICODE_STRING uName;
        o_RtlInitUnicodeString(&uName, lpLibFileName);

        HANDLE hModule = nullptr;
        NTSTATUS status = o_LdrLoadDll(nullptr, nullptr, &uName, &hModule);

        if (NT_SUCCESS(status))
            return static_cast<HMODULE>(hModule);

        SetLastError(o_RtlNtStatusToDosError(status));
        return nullptr;
    }

    static NTSTATUS FreeLibrary_Ldr(PVOID handle) { return o_LdrUnloadDll(handle); }

    static void Init()
    {
        if (o_RtlInitUnicodeString != nullptr)
            return;

        _dll = GetModuleHandleW(L"ntdll.dll");

        if (_dll == nullptr)
            return;

        o_RtlInitUnicodeString = (PFN_RtlInitUnicodeString) GetProcAddress(_dll, "RtlInitUnicodeString");
        o_RtlNtStatusToDosError = (PFN_RtlNtStatusToDosError) GetProcAddress(_dll, "RtlNtStatusToDosError");
        o_LdrLoadDll = (PFN_LdrLoadDll) GetProcAddress(_dll, "LdrLoadDll");
        o_LdrUnloadDll = (PFN_LdrUnloadDll) GetProcAddress(_dll, "LdrUnloadDll");
        o_NtLoadDll = (PFN_NtLoadDll) GetProcAddress(_dll, "NtLoadDll");
    }

    static HMODULE Module() { return _dll; }

    static PFN_LdrLoadDll Hook_LdrLoadDll(PVOID method)
    {
        auto addr = o_LdrLoadDll;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        o_LdrLoadDll = addr;
        return addr;
    }

    static PFN_LdrUnloadDll Hook_LdrUnloadDll(PVOID method)
    {
        auto addr = o_LdrUnloadDll;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        o_LdrUnloadDll = addr;
        return addr;
    }

    static PFN_NtLoadDll Hook_NtLoadDll(PVOID method)
    {
        auto addr = o_NtLoadDll;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook: {:X}", detourResult);
            return nullptr;
        }

        o_NtLoadDll = addr;
        return addr;
    }

  private:
    typedef decltype(&RtlInitUnicodeString) PFN_RtlInitUnicodeString;
    typedef decltype(&RtlNtStatusToDosError) PFN_RtlNtStatusToDosError;

    inline static HMODULE _dll = nullptr;

    inline static PFN_LdrLoadDll o_LdrLoadDll = nullptr;
    inline static PFN_LdrUnloadDll o_LdrUnloadDll = nullptr;
    inline static PFN_NtLoadDll o_NtLoadDll = nullptr;
    inline static PFN_RtlInitUnicodeString o_RtlInitUnicodeString = nullptr;
    inline static PFN_RtlNtStatusToDosError o_RtlNtStatusToDosError = nullptr;
};
