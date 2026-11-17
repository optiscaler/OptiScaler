#include "pch.h"
#include "CrashDumps.h"

#include <Config.h>
#include <Util.h>
#include <proxies/KernelBase_Proxy.h>
#include <proxies/Kernel32_Proxy.h>
#include <proxies/Ntdll_Proxy.h>

#include <detours/detours.h>

// Stacks and the memory they point to, small enough to be written quickly
static constexpr auto DumpType =
    (MINIDUMP_TYPE) (MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithUnloadedModules |
                     MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo);

// Without the CRT, it can allocate and the heap can be what broke
static size_t Append(wchar_t* buffer, size_t size, size_t pos, const wchar_t* text)
{
    while (*text != L'\0' && pos + 1 < size)
        buffer[pos++] = *text++;

    buffer[pos] = L'\0';
    return pos;
}

static size_t AppendNumber(wchar_t* buffer, size_t size, size_t pos, DWORD value, DWORD base, int digits)
{
    wchar_t text[16] {};

    for (int i = digits - 1; i >= 0; i--)
    {
        text[i] = L"0123456789ABCDEF"[value % base];
        value /= base;
    }

    return Append(buffer, size, pos, text);
}

DWORD WINAPI CrashDumps::DumpThread(LPVOID param)
{
    SYSTEMTIME time {};
    GetLocalTime(&time);

    constexpr auto size = std::size(_path);
    auto pos = Append(_path, size, 0, _folder);
    pos = Append(_path, size, pos, L"\\OptiScaler_");
    pos = Append(_path, size, pos, _request.kind);
    pos = Append(_path, size, pos, L"_");
    pos = AppendNumber(_path, size, pos, _request.code, 16, 8);
    pos = Append(_path, size, pos, L"_");
    pos = AppendNumber(_path, size, pos, time.wYear, 10, 4);
    pos = AppendNumber(_path, size, pos, time.wMonth, 10, 2);
    pos = AppendNumber(_path, size, pos, time.wDay, 10, 2);
    pos = Append(_path, size, pos, L"_");
    pos = AppendNumber(_path, size, pos, time.wHour, 10, 2);
    pos = AppendNumber(_path, size, pos, time.wMinute, 10, 2);
    pos = AppendNumber(_path, size, pos, time.wSecond, 10, 2);
    Append(_path, size, pos, L".dmp");

    // Not through Opti's hook, it allocates
    auto createFile = Kernel32Proxy::CreateFileW_() != nullptr ? Kernel32Proxy::CreateFileW_() : CreateFileW;
    auto file = createFile(_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return 1;

    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo { _request.threadId, _request.exception, FALSE };

    _request.written = o_MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, DumpType,
                                           _request.exception != nullptr ? &exceptionInfo : nullptr, nullptr, nullptr);

    CloseHandle(file);
    return 0;
}

// A fault in dbghelp ends the dump, not the state it holds
DWORD WINAPI CrashDumps::GuardedDumpThread(LPVOID param)
{
    __try
    {
        return DumpThread(param);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 1;
    }
}

// Waits for a dump another thread is writing, false when it never finished
bool CrashDumps::Acquire()
{
    // The thread writing a dump ending the process or faulting again
    if (GetCurrentThreadId() == _dumperThreadId)
        return false;

    const auto start = GetTickCount64();

    while (!_timedOut && GetTickCount64() - start < WaitMs)
    {
        bool expected = false;

        if (_writing.compare_exchange_strong(expected, true))
            return true;

        Sleep(10);
    }

    return false;
}

// On the calling thread, a new one would wait for the locks it holds while the process ends
void CrashDumps::Write(const wchar_t* kind, DWORD code, EXCEPTION_POINTERS* exception)
{
    _request = { kind, code, GetCurrentThreadId(), exception, FALSE };

    if (code != EXCEPTION_STACK_OVERFLOW)
    {
        _dumperThreadId = GetCurrentThreadId();
        GuardedDumpThread(nullptr);
        _dumperThreadId = 0;
        _writing = false;
        return;
    }

    // Too little stack left here for dbghelp
    DWORD threadId = 0;
    auto thread = CreateThread(nullptr, 0, GuardedDumpThread, nullptr, CREATE_SUSPENDED, &threadId);

    if (thread == nullptr)
    {
        _writing = false;
        return;
    }

    _dumperThreadId = threadId;
    ResumeThread(thread);

    auto waitResult = WaitForSingleObject(thread, WaitMs);
    CloseHandle(thread);

    // A dumper still running keeps the request, no more dumps then
    if (waitResult != WAIT_OBJECT_0)
    {
        _timedOut = true;
        return;
    }

    _dumperThreadId = 0;
    _writing = false;
}

bool CrashDumps::IsFatal(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case STATUS_HEAP_CORRUPTION:
        return true;

    default:
        return false;
    }
}

bool CrashDumps::CanWrite(const std::wstring& folder)
{
    auto testFile = std::filesystem::path(folder) / L"OptiScaler_dump_test.tmp";
    auto file = CreateFileW(testFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return false;

    CloseHandle(file);
    return true;
}

LONG CALLBACK CrashDumps::ExceptionHandler(EXCEPTION_POINTERS* info)
{
    auto code = info->ExceptionRecord->ExceptionCode;

    // dbghelp reading unreadable memory while dumping
    if (GetCurrentThreadId() == _dumperThreadId || !IsFatal(code))
        return EXCEPTION_CONTINUE_SEARCH;

    // The exit after it is only skipped for a crash that got dumped
    if (_crashDumps.fetch_add(1) >= MaxCrashDumps || !Acquire())
    {
        _lastCrashCode = 0;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    _lastCrashCode = code;
    Write(L"crash", code, info);

    return EXCEPTION_CONTINUE_SEARCH;
}

// ExitProcess, TerminateProcess and abort end up here, a null handle ends the other threads first
NTSTATUS NTAPI CrashDumps::hkNtTerminateProcess(HANDLE ProcessHandle, NTSTATUS ExitStatus)
{
    const bool self = ProcessHandle == nullptr || ProcessHandle == GetCurrentProcess() ||
                      GetProcessId(ProcessHandle) == GetCurrentProcessId();

    // A dump being written would be cut by ending the process
    if (self && ExitStatus != 0 && !_exitDumped.exchange(true) && Acquire())
    {
        // Ending the process after a crash already dumped
        if ((DWORD) ExitStatus == _lastCrashCode)
        {
            _writing = false;
        }
        else
        {
            // The writing thread's own stack can't be walked in the dump, so this one is its context
            CONTEXT context {};
            RtlCaptureContext(&context);

            EXCEPTION_RECORD record {};
            record.ExceptionCode = (DWORD) ExitStatus;
            record.ExceptionAddress = (PVOID) context.Rip;

            EXCEPTION_POINTERS pointers { &record, &context };
            Write(L"exit", (DWORD) ExitStatus, &pointers);
        }
    }

    return o_NtTerminateProcess(ProcessHandle, ExitStatus);
}

void CrashDumps::Init()
{
    if (!Config::Instance()->CreateDumps.value_or_default() || o_NtTerminateProcess != nullptr)
        return;

    std::filesystem::path folder =
        std::filesystem::path(Config::Instance()->LogFileName.value_or_default()).parent_path();

    if (folder.empty())
        folder = Util::DllPath().parent_path();

    if (!CanWrite(folder.wstring()))
    {
        wchar_t tempDir[MAX_PATH + 1] {};
        auto length = GetTempPathW(MAX_PATH + 1, tempDir);

        if (length == 0 || length > MAX_PATH || !CanWrite(tempDir))
        {
            LOG_ERROR("Can't write to {} or the temp folder, dumps are disabled", wstring_to_string(folder.wstring()));
            return;
        }

        LOG_WARN("Can't write to {}, dumps go to {}", wstring_to_string(folder.wstring()), wstring_to_string(tempDir));

        folder = std::filesystem::path(tempDir).parent_path();
    }

    wcsncpy_s(_folder, folder.c_str(), _TRUNCATE);

    // By the full path, Opti can be the dbghelp.dll
    wchar_t systemDir[MAX_PATH] {};
    GetSystemDirectoryW(systemDir, MAX_PATH);
    auto dbghelp = NtdllProxy::LoadLibraryExW_Ldr((std::filesystem::path(systemDir) / L"dbghelp.dll").c_str(), NULL, 0);

    auto ntdll = KernelBaseProxy::GetModuleHandleW_()(L"ntdll.dll");

    if (dbghelp != nullptr)
        o_MiniDumpWriteDump = (PFN_MiniDumpWriteDump) KernelBaseProxy::GetProcAddress_()(dbghelp, "MiniDumpWriteDump");

    o_NtTerminateProcess = (PFN_NtTerminateProcess) KernelBaseProxy::GetProcAddress_()(ntdll, "NtTerminateProcess");

    if (o_MiniDumpWriteDump == nullptr || o_NtTerminateProcess == nullptr)
    {
        LOG_ERROR("Can't get the functions to write dumps");
        o_NtTerminateProcess = nullptr;
        return;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&) o_NtTerminateProcess, hkNtTerminateProcess);

    if (DetourTransactionCommit() != NO_ERROR)
        LOG_ERROR("Can't hook NtTerminateProcess, exit dumps are disabled");

    AddVectoredExceptionHandler(1, ExceptionHandler);

    LOG_INFO("Dumps will be written to {}", wstring_to_string(_folder));
}
