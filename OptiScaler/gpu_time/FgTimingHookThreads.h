#pragma once

#include <Windows.h>
#include <TlHelp32.h>
#include <detours/detours.h>
#include <vector>

// Prepare handles before starting a Detours transaction. Enlist only after trampoline allocation,
// then keep every handle alive until commit/abort resumes the affected threads.
class FgTimingHookThreads
{
    std::vector<HANDLE> _threads;

  public:
    FgTimingHookThreads() = default;
    FgTimingHookThreads(const FgTimingHookThreads&) = delete;
    FgTimingHookThreads& operator=(const FgTimingHookThreads&) = delete;

    ~FgTimingHookThreads()
    {
        for (auto thread : _threads)
            CloseHandle(thread);
    }

    bool Prepare()
    {
        const auto processId = GetCurrentProcessId();
        const auto currentThreadId = GetCurrentThreadId();
        auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        THREADENTRY32 entry {};
        entry.dwSize = sizeof(entry);
        bool success = Thread32First(snapshot, &entry) != FALSE;
        while (success)
        {
            if (entry.th32OwnerProcessID == processId && entry.th32ThreadID != currentThreadId)
            {
                auto thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                                             THREAD_QUERY_INFORMATION,
                                         FALSE, entry.th32ThreadID);
                if (thread)
                {
                    const auto owner = GetProcessIdOfThread(thread);
                    if (owner == 0)
                    {
                        CloseHandle(thread);
                        success = false;
                        break;
                    }
                    if (owner == processId)
                        _threads.push_back(thread);
                    else
                        CloseHandle(thread);
                }
                else if (GetLastError() != ERROR_INVALID_PARAMETER)
                {
                    success = false;
                    break;
                }
                if (_threads.size() > 4096)
                {
                    success = false;
                    break;
                }
            }
            entry.dwSize = sizeof(entry);
            if (!Thread32Next(snapshot, &entry))
            {
                success = GetLastError() == ERROR_NO_MORE_FILES;
                break;
            }
        }
        CloseHandle(snapshot);
        return success;
    }

    LONG Enlist() const
    {
        auto result = DetourUpdateThread(GetCurrentThread());
        if (result != NO_ERROR)
            return result;
        for (auto thread : _threads)
        {
            DWORD exitCode = 0;
            if (GetExitCodeThread(thread, &exitCode) && exitCode != STILL_ACTIVE)
                continue;
            result = DetourUpdateThread(thread);
            if (result != NO_ERROR)
                return result;
        }
        return NO_ERROR;
    }
};
