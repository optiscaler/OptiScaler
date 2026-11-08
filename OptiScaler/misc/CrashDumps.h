#pragma once

#include "SysUtils.h"

#include <DbgHelp.h>

#include <atomic>
#include <string>

// Writes a minidump next to the log on a fatal exception or when the process ends itself with an error, Log.CreateDumps
class CrashDumps
{
  private:
    typedef BOOL(WINAPI* PFN_MiniDumpWriteDump)(HANDLE hProcess, DWORD ProcessId, HANDLE hFile, MINIDUMP_TYPE DumpType,
                                                PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
                                                PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
                                                PMINIDUMP_CALLBACK_INFORMATION CallbackParam);
    typedef NTSTATUS(NTAPI* PFN_NtTerminateProcess)(HANDLE ProcessHandle, NTSTATUS ExitStatus);

    // Static, the dumper can outlive the wait of the thread that asked for it
    struct DumpRequest
    {
        const wchar_t* kind = nullptr;
        DWORD code = 0;
        DWORD threadId = 0;
        EXCEPTION_POINTERS* exception = nullptr;
        BOOL written = FALSE;
    };

    // First chance exceptions can be handled by the game, a few dumps are enough
    static constexpr int MaxCrashDumps = 3;
    static constexpr DWORD WaitMs = 120000;

    inline static wchar_t _folder[MAX_PATH * 2] {};
    inline static wchar_t _path[MAX_PATH * 2 + 64] {};
    inline static DumpRequest _request {};

    inline static PFN_MiniDumpWriteDump o_MiniDumpWriteDump = nullptr;
    inline static PFN_NtTerminateProcess o_NtTerminateProcess = nullptr;

    inline static std::atomic<bool> _writing = false;
    inline static std::atomic<DWORD> _dumperThreadId = 0;
    inline static std::atomic<DWORD> _lastCrashCode = 0;
    inline static std::atomic<int> _crashDumps = 0;
    inline static std::atomic<bool> _exitDumped = false;
    inline static std::atomic<bool> _timedOut = false;

    static DWORD WINAPI DumpThread(LPVOID param);
    static DWORD WINAPI GuardedDumpThread(LPVOID param);
    static bool Acquire();
    static void Write(const wchar_t* kind, DWORD code, EXCEPTION_POINTERS* exception);
    static bool IsFatal(DWORD code);
    static bool CanWrite(const std::wstring& folder);

    static LONG CALLBACK ExceptionHandler(EXCEPTION_POINTERS* info);
    static NTSTATUS NTAPI hkNtTerminateProcess(HANDLE ProcessHandle, NTSTATUS ExitStatus);

  public:
    static void Init();
};
