#include "pch.h"
#include "Kernel_Hooks.h"

#include "Gdi32_Hooks.h"
#include "Streamline_Hooks.h"
#include "LibraryLoad_Hooks.h"

#include <fsr4/FSR4ModelSelection.h>

#include <Util.h>
#include <State.h>
#include <Config.h>

#include <cwctype>
#include <misc/IdentifyGpu.h>

#include "Hook_Utils.h"
#include <proxies/XeLL_Proxy.h>

#include "Amdxc64_Hooks.h"
#pragma intrinsic(_ReturnAddress)

static inline void NormalizePath(std::string& path)
{
    while (!path.empty() && (path.back() == '\\' || path.back() == '/'))
        path.pop_back();
}

static inline bool IsInsideWindowsDirectory(const std::string& path)
{
    char windowsDir[MAX_PATH];
    UINT len = GetWindowsDirectoryA(windowsDir, MAX_PATH);

    if (len == 0 || len >= MAX_PATH)
        return false;

    std::string pathToCheck(path);
    std::string windowsPath(windowsDir);

    NormalizePath(pathToCheck);
    NormalizePath(windowsPath);

    to_lower_in_place(pathToCheck);
    to_lower_in_place(windowsPath);

    // Check if pathToCheck starts with windowsPath, while having a slash after that
    if (pathToCheck.compare(0, windowsPath.size(), windowsPath) == 0 &&
        (pathToCheck.size() == windowsPath.size() || pathToCheck[windowsPath.size()] == '\\' ||
         pathToCheck[windowsPath.size()] == '/'))
        return true;

    return false;
}

static inline HMODULE CheckLoad(const std::wstring& name)
{
    do
    {
        if (State::Instance().isShuttingDown || LibraryLoadHooks::IsApiSetName(name))
            break;

        if (State::SkipDllChecks())
        {
            const std::wstring skip = string_to_wstring(State::SkipDllName());

            if (skip.empty() || LibraryLoadHooks::EndsWithInsensitive(name, std::wstring_view(skip)) ||
                LibraryLoadHooks::EndsWithInsensitive(name, std::wstring(skip + L".dll")))
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(name.data()));
                break;
            }
        }

        auto moduleHandle = LibraryLoadHooks::LoadLibraryCheckW(name.data(), name.data());

        // skip loading of dll
        if (moduleHandle == (HMODULE) 1337)
            break;

        if (moduleHandle != nullptr)
        {
            LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
            return moduleHandle;
        }
    } while (false);

    return nullptr;
}

VALIDATE_HOOK(hk_K32_GetProcAddress, Kernel32Proxy::PFN_GetProcAddress)
FARPROC WINAPI KernelHooks::hk_K32_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{

    if ((size_t) lpProcName < 0x000000000000F000)
    {
        if (hModule == dllModule)
            LOG_TRACE("Ordinal call: {:X}", (size_t) lpProcName);

        return o_K32_GetProcAddress(hModule, lpProcName);
    }

    // if (hModule == dllModule && lpProcName != nullptr)
    //{
    //     LOG_TRACE("Trying to get process address of {}, caller: {}", lpProcName,
    //               Util::WhoIsTheCaller(_ReturnAddress()));
    // }

    // FSR 4 Init in case of missing amdxc64.dll
    // 2nd check is amdxcffx64.dll trying to queue amdxc64 but amdxc64 not being loaded.
    // Also skip the internal call of amdxc64
    if (lpProcName != nullptr && (hModule == amdxc64Mark || hModule == nullptr) &&
        lstrcmpA(lpProcName, "AmdExtD3DCreateInterface") == 0 &&
        IdentifyGpu::getPrimaryGpu().fsr4Support != FSR4Support::None &&
        Util::GetCallerModule(_ReturnAddress()) != KernelBaseProxy::GetModuleHandleW_()(L"amdxc64.dll"))
    {
        LOG_TRACE("Giving hkAmdExtD3DCreateInterface");
        return (FARPROC) &Amdxc64Hooks::hkAmdExtD3DCreateInterface;
    }
    else if (hModule == amdxc64Mark)
    {
        return o_K32_GetProcAddress(KernelBaseProxy::GetModuleHandleW_()(L"amdxc64.dll"), lpProcName);
    }

    return o_K32_GetProcAddress(hModule, lpProcName);
}

VALIDATE_HOOK(hk_K32_GetModuleHandleA, Kernel32Proxy::PFN_GetModuleHandleA)
HMODULE WINAPI KernelHooks::hk_K32_GetModuleHandleA(LPCSTR lpModuleName)
{
    if (lpModuleName != NULL)
    {
        if (strcmp(lpModuleName, "nvngx_dlssg.dll") == 0)
        {
            LOG_TRACE("Trying to get module handle of {}, caller: {}", lpModuleName,
                      Util::WhoIsTheCaller(_ReturnAddress()));
            return dllModule;
        }
        else if (strcmp(lpModuleName, "amdxc64.dll") == 0)
        {
            // Libraries like FFX SDK or AntiLag 2 SDK do not load amdxc64 themselves
            // so most likely amdxc64 is getting loaded by the driver itself.
            // Therefore it should be safe for us to return a custom implementation when it's not loaded
            // This can get removed if Proton starts to ship amdxc64

            // For system with amdxc64 loaded - we should've caught the load and hooked it
            // For systems without - we provide that app with a fake handle and track the usage that way

            auto original = o_K32_GetModuleHandleA(lpModuleName);

            auto primaryGpu = IdentifyGpu::getPrimaryGpu();

            if (original == nullptr && primaryGpu.fsr4Support != FSR4Support::None)
            {
                LOG_INFO("giving a fake HMODULE for amdxc64.dll");
                return amdxc64Mark;
            }

            return original;
        }
    }

    return o_K32_GetModuleHandleA(lpModuleName);
}

VALIDATE_HOOK(hk_K32_GetModuleHandleW, Kernel32Proxy::PFN_GetModuleHandleW)
HMODULE WINAPI KernelHooks::hk_K32_GetModuleHandleW(LPCWSTR lpModuleName)
{
    if (lpModuleName != NULL)
    {
        if (wcscmp(lpModuleName, L"amdxc64.dll") == 0)
        {
            LOG_TRACE("amdxc64.dll call");

            // See comments in hk_K32_GetModuleHandleA

            auto original = o_K32_GetModuleHandleW(lpModuleName);

            auto primaryGpu = IdentifyGpu::getPrimaryGpu();

            if (original == nullptr && primaryGpu.fsr4Support != FSR4Support::None)
            {
                LOG_INFO("giving a fake HMODULE for amdxc64.dll");
                return amdxc64Mark;
            }

            return original;
        }
    }

    return o_K32_GetModuleHandleW(lpModuleName);
}

VALIDATE_HOOK(hk_K32_GetModuleHandleExA, Kernel32Proxy::PFN_GetModuleHandleExA)
BOOL WINAPI KernelHooks::hk_K32_GetModuleHandleExA(DWORD dwFlags, LPCSTR lpModuleName, HMODULE* phModule)
{
    if (lpModuleName && dwFlags == 0 && strcmp("libxell.dll", lpModuleName) == 0 && phModule)
    {
#ifdef LOW_LATENCY_INPUTS
        *phModule = dllModule;
        return true;
#else
        if (const auto module = XeLLProxy::Module())
        {
            return o_K32_GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCSTR>(module),
                                            phModule);
        }
#endif
    }

    return o_K32_GetModuleHandleExA(dwFlags, lpModuleName, phModule);
}

VALIDATE_HOOK(hk_K32_GetModuleHandleExW, Kernel32Proxy::PFN_GetModuleHandleExW)
BOOL WINAPI KernelHooks::hk_K32_GetModuleHandleExW(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE* phModule)
{
    if (lpModuleName && dwFlags == GET_MODULE_HANDLE_EX_FLAG_PIN && lstrcmpW(L"nvapi64.dll", lpModuleName) == 0 &&
        phModule)
    {
        LOG_TRACE("Suspected SpecialK call for nvapi64");
        *phModule = LibraryLoadHooks::LoadNvApi();
        return true;
    }

    return o_K32_GetModuleHandleExW(dwFlags, lpModuleName, phModule);
}

VALIDATE_HOOK(hk_KB_GetProcAddress, KernelBaseProxy::PFN_GetProcAddress)
FARPROC WINAPI KernelHooks::hk_KB_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
    if ((size_t) lpProcName < 0x000000000000F000)
    {
        if (hModule == dllModule)
            LOG_TRACE("Ordinal call: {:X}", (size_t) lpProcName);

        return o_KB_GetProcAddress(hModule, lpProcName);
    }

    // if (hModule == dllModule && lpProcName != nullptr)
    //{
    //     LOG_TRACE("Trying to get process address of {}, caller: {}", lpProcName,
    //               Util::WhoIsTheCaller(_ReturnAddress()));
    // }

    return o_KB_GetProcAddress(hModule, lpProcName);
}

VALIDATE_HOOK(hk_K32_GetFileAttributesW, Kernel32Proxy::PFN_GetFileAttributesW)
DWORD WINAPI KernelHooks::hk_K32_GetFileAttributesW(LPCWSTR lpFileName)
{
    if (!State::Instance().nvngxExists && State::Instance().nvngxReplacement.has_value() &&
        (Config::Instance()->DxgiSpoofing.value_or_default() ||
         Config::Instance()->StreamlineSpoofing.value_or_default()))
    {
        auto path = wstring_to_string(std::wstring(lpFileName));
        to_lower_in_place(path);

        if (path.contains("nvngx.dll") && !path.contains("_nvngx.dll") &&
            !IsInsideWindowsDirectory(path)) // apply the override to just one path
        {
            LOG_DEBUG("Overriding GetFileAttributesW for nvngx");
            return FILE_ATTRIBUTE_ARCHIVE;
        }
    }

    return o_K32_GetFileAttributesW(lpFileName);
}

VALIDATE_HOOK(hk_K32_CreateFileW, Kernel32Proxy::PFN_CreateFileW)
HANDLE WINAPI KernelHooks::hk_K32_CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                              LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                              DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    if (!State::Instance().nvngxExists && State::Instance().nvngxReplacement.has_value() &&
        (Config::Instance()->DxgiSpoofing.value_or_default() ||
         Config::Instance()->StreamlineSpoofing.value_or_default()))
    {
        auto path = wstring_to_string(std::wstring(lpFileName));
        to_lower_in_place(path);

        if (path.contains("nvngx.dll") && !path.contains("_nvngx.dll") && // apply the override to just one path
            !IsInsideWindowsDirectory(path))
        {
            static auto& signedDll = State::Instance().nvngxReplacement;

            if (signedDll.has_value())
            {
                LOG_DEBUG("Overriding CreateFileW for nvngx with a signed dll, original path: {}", path);
                return o_K32_CreateFileW(signedDll.value().c_str(), dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                         dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
            }
        }
    }

    return o_K32_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition,
                             dwFlagsAndAttributes, hTemplateFile);
}

VALIDATE_HOOK(hk_K32_OutputDebugStringW, Kernel32Proxy::PFN_OutputDebugStringW)
VOID WINAPI KernelHooks::hk_K32_OutputDebugStringW(LPCWSTR lpOutputString)
{
    o_K32_OutputDebugStringW(lpOutputString);

    std::wstring result(lpOutputString);

    while (!result.empty() && (result.back() == L'\n' || result.back() == L'\r'))
    {
        result.pop_back();
    }

    LOG_TRACE(L"{}", result);
}

VALIDATE_HOOK(hk_K32_OutputDebugStringA, Kernel32Proxy::PFN_OutputDebugStringA)
VOID WINAPI KernelHooks::hk_K32_OutputDebugStringA(LPCSTR lpOutputString)
{
    o_K32_OutputDebugStringA(lpOutputString);

    std::string result(lpOutputString);

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
    {
        result.pop_back();
    }

    LOG_TRACE("{}", result);
}

// Load Library checks

VALIDATE_HOOK(hk_K32_LoadLibraryW, Kernel32Proxy::PFN_LoadLibraryW)
HMODULE KernelHooks::hk_K32_LoadLibraryW(LPCWSTR lpLibFileName)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::wstring name(lpLibFileName);

#ifdef _DEBUG
    // LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

    auto result = CheckLoad(name);

    if (result != nullptr)
        return result;

    return o_K32_LoadLibraryW(lpLibFileName);
}

VALIDATE_HOOK(hk_K32_LoadLibraryA, Kernel32Proxy::PFN_LoadLibraryA)
HMODULE KernelHooks::hk_K32_LoadLibraryA(LPCSTR lpLibFileName)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::string nameA(lpLibFileName);
    std::wstring name = string_to_wstring(nameA);

#ifdef _DEBUG
    // LOG_TRACE("{}, caller: {}", nameA.data(), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

    auto result = CheckLoad(name);

    if (result != nullptr)
        return result;

    return o_K32_LoadLibraryA(lpLibFileName);
}

VALIDATE_HOOK(hk_K32_LoadLibraryExW, Kernel32Proxy::PFN_LoadLibraryExW)
HMODULE KernelHooks::hk_K32_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::wstring name(lpLibFileName);

#ifdef _DEBUG
    // LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

    auto result = CheckLoad(name);

    if (result != nullptr)
        return result;

    return o_K32_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
}

VALIDATE_HOOK(hk_K32_LoadLibraryExA, Kernel32Proxy::PFN_LoadLibraryExA)
HMODULE KernelHooks::hk_K32_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::string nameA(lpLibFileName);
    std::wstring name = string_to_wstring(nameA);

#ifdef _DEBUG
    // LOG_TRACE("{}, caller: {}", nameA.data(), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

    auto result = CheckLoad(name);

    if (result != nullptr)
        return result;

    return o_K32_LoadLibraryExA(lpLibFileName, hFile, dwFlags);
}

VALIDATE_HOOK(hk_KB_LoadLibraryExW, KernelBaseProxy::PFN_LoadLibraryExW)
HMODULE KernelHooks::hk_KB_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::wstring name(lpLibFileName);

#ifdef _DEBUG
    // LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

    auto result = CheckLoad(name);

    if (result != nullptr)
        return result;

    return o_KB_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
}

VALIDATE_HOOK(hk_K32_FreeLibrary, Kernel32Proxy::PFN_FreeLibrary)
BOOL KernelHooks::hk_K32_FreeLibrary(HMODULE lpLibrary)
{
    if (lpLibrary == nullptr)
        return STATUS_INVALID_PARAMETER;

#ifdef _DEBUG
    // LOG_TRACE("{:X}", (size_t) lpLibrary);
#endif

    if (!State::Instance().isShuttingDown)
    {
        auto result = LibraryLoadHooks::FreeLibrary(lpLibrary);

        if (result.has_value())
            return result.value() == TRUE;
    }

    return o_K32_FreeLibrary(lpLibrary);
}
