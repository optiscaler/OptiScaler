#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <xell.h>
#include <xell_d3d12.h>

#include <magic_enum.hpp>

#pragma comment(lib, "Version.lib")

// Common
typedef decltype(&xellDestroyContext) PFN_xellDestroyContext;
typedef decltype(&xellSetSleepMode) PFN_xellSetSleepMode;
typedef decltype(&xellGetSleepMode) PFN_xellGetSleepMode;
typedef decltype(&xellSleep) PFN_xellSleep;
typedef decltype(&xellAddMarkerData) PFN_xellAddMarkerData;
typedef decltype(&xellGetVersion) PFN_xellGetVersion;
typedef decltype(&xellSetLoggingCallback) PFN_xellSetLoggingCallback;
typedef decltype(&xellGetFramesReports) PFN_xellGetFramesReports;

// Dx12
typedef decltype(&xellD3D12CreateContext) PFN_xellD3D12CreateContext;

// This callback runs once for every function exported by the Old DLL
static int ExportCallback(PVOID hNewDll, ULONG nOrdinal, LPCSTR pszName, PVOID pOldFunction)
{
    if (pszName == NULL)
        return true;

    auto pNewFunction = GetProcAddress((HMODULE) hNewDll, pszName);

    if (pNewFunction && pNewFunction != pOldFunction)
    {
        // pOldFunction doesn't get stored because we don't plan on calling the old DLL
        if (DetourAttach(&pOldFunction, pNewFunction))
            LOG_TRACE("Failed to detour {}", pszName);
    }

    return true;
}

static void RedirectAllExports(HMODULE hOld, HMODULE hNew)
{
    if (!hOld || !hNew)
    {
        LOG_ERROR("Could not find modules");
        return;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    DetourEnumerateExports(hOld, hNew, ExportCallback);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
        LOG_ERROR("Failed to commit detours: {:X}", detourResult);
}

class XeLLProxy
{
  private:
    inline static HMODULE _dll = nullptr;
    inline static HMODULE _memModule = nullptr;
    inline static std::wstring _dllPath;

    inline static xell_version_t _xellVersion {};

    inline static xell_context_handle_t _xellContext = nullptr;

    static void xellLogCallback(const char* message, xell_logging_level_t loggingLevel)
    {
        switch (loggingLevel)
        {
        case XELL_LOGGING_LEVEL_DEBUG:
            spdlog::debug("XeLL Log: {}", message);
            return;

        case XELL_LOGGING_LEVEL_INFO:
            spdlog::info("XeLL Log: {}", message);
            return;

        case XELL_LOGGING_LEVEL_WARNING:
            spdlog::warn("XeLL Log: {}", message);
            return;

        default:
            spdlog::error("XeLL Log: {}", message);
            return;
        }
    }

    // Common
    inline static PFN_xellDestroyContext _xellDestroyContext = nullptr;
    inline static PFN_xellSetSleepMode _xellSetSleepMode = nullptr;
    inline static PFN_xellGetSleepMode _xellGetSleepMode = nullptr;
    inline static PFN_xellSleep _xellSleep = nullptr;
    inline static PFN_xellAddMarkerData _xellAddMarkerData = nullptr;
    inline static PFN_xellGetVersion _xellGetVersion = nullptr;
    inline static PFN_xellSetLoggingCallback _xellSetLoggingCallback = nullptr;
    inline static PFN_xellGetFramesReports _xellGetFramesReports = nullptr;

    // Dx12
    inline static PFN_xellD3D12CreateContext _xellD3D12CreateContext = nullptr;

    inline static xell_version_t GetDLLVersion(std::wstring dllPath)
    {
        // Step 1: Get the size of the version information
        DWORD handle = 0;
        DWORD versionSize = GetFileVersionInfoSizeW(dllPath.c_str(), &handle);
        xell_version_t version { 0, 0, 0 };

        if (versionSize == 0)
        {
            LOG_ERROR("Failed to get version info size: {0:X}", GetLastError());
            return version;
        }

        // Step 2: Allocate buffer and get the version information
        std::vector<BYTE> versionInfo(versionSize);
        if (handle == 0 && !GetFileVersionInfoW(dllPath.c_str(), handle, versionSize, versionInfo.data()))
        {
            LOG_ERROR("Failed to get version info: {0:X}", GetLastError());
            return version;
        }

        // Step 3: Extract the version information
        VS_FIXEDFILEINFO* fileInfo = nullptr;
        UINT size = 0;
        if (!VerQueryValueW(versionInfo.data(), L"\\", reinterpret_cast<LPVOID*>(&fileInfo), &size))
        {
            LOG_ERROR("Failed to query version value: {0:X}", GetLastError());
            return version;
        }

        if (fileInfo != nullptr)
        {
            // Extract major, minor, build, and revision numbers from version information
            DWORD fileVersionMS = fileInfo->dwFileVersionMS;
            DWORD fileVersionLS = fileInfo->dwFileVersionLS;

            version.major = (fileVersionMS >> 16) & 0xffff;
            version.minor = (fileVersionMS >> 0) & 0xffff;
            version.patch = (fileVersionLS >> 16) & 0xffff;
            version.reserved = (fileVersionLS >> 0) & 0xffff;
        }
        else
        {
            LOG_ERROR("No version information found!");
        }

        return version;
    }

    inline static std::filesystem::path DllPath(HMODULE module)
    {
        static std::filesystem::path dll;

        if (dll.empty())
        {
            wchar_t dllPath[MAX_PATH];
            GetModuleFileNameW(module, dllPath, MAX_PATH);
            dll = std::filesystem::path(dllPath);
        }

        return dll;
    }

  public:
    static HMODULE Module() { return _dll; }
    static HMODULE MemModule() { return _memModule; }
    static bool WasLoaded() { return _memModule != nullptr && _memModule != _dll; }
    static std::wstring Module_Path() { return _dllPath; }

    static bool InitXeLL()
    {
        if (_dll != nullptr)
            return true;

        HMODULE mainModule = nullptr;

        std::vector<std::wstring> dllNames = { L"libxell.dll" };

        auto optiPath = Config::Instance()->MainDllPath.value();

        for (size_t i = 0; i < dllNames.size(); i++)
        {
            LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

            auto overridePath = Config::Instance()->XeLLLibrary.value_or(L"");

            HMODULE memModule = nullptr;
            Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &mainModule);

            if (mainModule != nullptr)
            {
                _memModule = memModule;

                // We don't control which XeLL dll XeFG will pick
                // Detouring GetModuleHandleExA seemingly isn't enough
                if (memModule && mainModule != memModule)
                    RedirectAllExports(memModule, mainModule);

                break;
            }
        }

        if (mainModule != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(mainModule, modulePath, MAX_PATH);
            _dllPath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(_dllPath));
            return HookXeLL(mainModule);
        }

        return false;
    }

    static bool HookXeLL(HMODULE libxellModule)
    {
        // if dll already loaded
        if (_dll != nullptr && _xellDestroyContext != nullptr)
            return true;

        spdlog::info("");

        if (libxellModule == nullptr)
            return false;

        _dll = libxellModule;

        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

            if (_dll != nullptr)
            {
                _xellDestroyContext =
                    (PFN_xellDestroyContext) KernelBaseProxy::GetProcAddress_()(_dll, "xellDestroyContext");
                _xellSetSleepMode = (PFN_xellSetSleepMode) KernelBaseProxy::GetProcAddress_()(_dll, "xellSetSleepMode");
                _xellGetSleepMode = (PFN_xellGetSleepMode) KernelBaseProxy::GetProcAddress_()(_dll, "xellGetSleepMode");
                _xellSleep = (PFN_xellSleep) KernelBaseProxy::GetProcAddress_()(_dll, "xellSleep");
                _xellAddMarkerData =
                    (PFN_xellAddMarkerData) KernelBaseProxy::GetProcAddress_()(_dll, "xellAddMarkerData");
                _xellGetVersion = (PFN_xellGetVersion) KernelBaseProxy::GetProcAddress_()(_dll, "xellGetVersion");
                _xellSetLoggingCallback =
                    (PFN_xellSetLoggingCallback) KernelBaseProxy::GetProcAddress_()(_dll, "xellSetLoggingCallback");
                _xellGetFramesReports =
                    (PFN_xellGetFramesReports) KernelBaseProxy::GetProcAddress_()(_dll, "xellGetFramesReports");

                _xellD3D12CreateContext =
                    (PFN_xellD3D12CreateContext) KernelBaseProxy::GetProcAddress_()(_dll, "xellD3D12CreateContext");
            }
        }

        bool loadResult = _xellDestroyContext != nullptr;
        LOG_INFO("LoadResult: {}", loadResult);
        return loadResult;
    }

    static xell_version_t Version()
    {
        if (_xellVersion.major == 0 && _xellGetVersion != nullptr)
        {
            if (auto result = _xellGetVersion(&_xellVersion); result == XELL_RESULT_SUCCESS)
            {
                LOG_INFO("XeLL Version: v{}.{}.{}", _xellVersion.major, _xellVersion.minor, _xellVersion.patch);
            }
            else
            {
                LOG_ERROR("Can't get XeLL version: {}", (UINT) result);
            }
        }

        if (_xellVersion.major == 0)
        {
            _xellVersion.major = 1;
            _xellVersion.minor = 0;
            _xellVersion.patch = 0;
        }

        return _xellVersion;
    }

    static PFN_xellDestroyContext DestroyContext() { return _xellDestroyContext; }
    static PFN_xellSetSleepMode SetSleepMode() { return _xellSetSleepMode; }
    static PFN_xellGetSleepMode GetSleepMode() { return _xellGetSleepMode; }
    static PFN_xellSleep Sleep() { return _xellSleep; }
    static PFN_xellAddMarkerData AddMarkerData() { return _xellAddMarkerData; }
    static PFN_xellGetVersion GetVersion() { return _xellGetVersion; }
    static PFN_xellSetLoggingCallback SetLoggingCallback() { return _xellSetLoggingCallback; }
    static PFN_xellGetFramesReports GetFramesReports() { return _xellGetFramesReports; }

    static PFN_xellD3D12CreateContext D3D12CreateContext() { return _xellD3D12CreateContext; }

    static bool DestroyXeLLContext()
    {
        LOG_DEBUG("");

        if (_xellContext != nullptr)
        {
            auto context = _xellContext;
            _xellContext = nullptr;
            auto xellResult = _xellDestroyContext(context);

            LOG_INFO("XeLL DestroyContext result: {} ({})", magic_enum::enum_name(xellResult), (UINT) xellResult);

            // Set it back because context is not destroyed
            if (xellResult != XELL_RESULT_SUCCESS)
                _xellContext = context;
        }

        return true;
    }

    static bool CreateContext(ID3D12Device* device)
    {
        if (!InitXeLL())
        {
            LOG_ERROR("XeLL proxy can't find libxell.dll!");
            return false;
        }

        if (_xellContext != nullptr)
            DestroyXeLLContext();

        xell_result_t xellResult;
        {
#ifndef DONT_USE_XMX
            ScopedSkipSpoofing skipSpoofing {};
#endif // !DONT_USE_XMX

            xellResult = _xellD3D12CreateContext(device, &_xellContext);
        }

        if (xellResult != XELL_RESULT_SUCCESS)
        {
            LOG_ERROR("XeLL D3D12CreateContext error: {} ({})", magic_enum::enum_name(xellResult), (UINT) xellResult);
            return false;
        }
        else
        {
            LOG_INFO("XeLL context created");
        }

        xellResult = SetLoggingCallback()(_xellContext, XELL_LOGGING_LEVEL_DEBUG, xellLogCallback);
        if (xellResult != XELL_RESULT_SUCCESS)
        {
            LOG_ERROR("XeLL SetLoggingCallback error: {} ({})", magic_enum::enum_name(xellResult), (UINT) xellResult);
        }

        return true;
    }

    static xell_context_handle_t Context() { return _xellContext; }
};
