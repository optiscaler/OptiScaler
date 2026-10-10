#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>
#include <hooks/Xell_Hooks.h>

#include <xell.h>
#include <xell_d3d12.h>

#include <magic_enum.hpp>
#include <low_latency/input/input_xell.h>

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

// Extra
typedef decltype(&xellD3D12SetAppQueue) PFN_xellD3D12SetAppQueue;
typedef decltype(&xellSetDisplayInfo) PFN_xellSetDisplayInfo;
typedef decltype(&xellSetFgEnabled) PFN_xellSetFgEnabled;
typedef decltype(&xellSetGeneratedFramesCount) PFN_xellSetGeneratedFramesCount;
typedef decltype(&xellGetLastPresentStartFrameId) PFN_xellGetLastPresentStartFrameId;

// This callback runs once for every function exported by the Old DLL
static int ExportCallback(PVOID hNewDll, ULONG nOrdinal, LPCSTR pszName, PVOID pOldFunction)
{
    if (pszName == NULL)
        return true;

    auto pNewFunction = KernelBaseProxy::GetProcAddress_()((HMODULE) hNewDll, pszName);

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
    inline static HMODULE _memoryDll = nullptr;
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

    // Extra
    inline static PFN_xellD3D12SetAppQueue _xellD3D12SetAppQueue = nullptr;
    inline static PFN_xellSetDisplayInfo _xellSetDisplayInfo = nullptr;
    inline static PFN_xellSetFgEnabled _xellSetFgEnabled = nullptr;
    inline static PFN_xellSetGeneratedFramesCount _xellSetGeneratedFramesCount = nullptr;
    inline static PFN_xellGetLastPresentStartFrameId _xellGetLastPresentStartFrameId = nullptr;

    inline static xell_version_t GetDLLVersion(std::wstring dllPath)
    {
        xell_version_t xellVersion {};
        version_t tempVersion {};
        auto result = Util::GetFileVersion(dllPath, &tempVersion);

        // Don't assume that the structs are identical
        if (result)
        {
            xellVersion.major = tempVersion.major;
            xellVersion.minor = tempVersion.minor;
            xellVersion.patch = tempVersion.patch;
            xellVersion.reserved = tempVersion.reserved;
        }

        return xellVersion;
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

    static bool InitXeLLProper()
    {
        if (_dll != nullptr)
            return true;

        HMODULE mainModule = nullptr;

        std::vector<std::wstring> dllNames = { L"libxell.dll" };

        auto& optiPath = Config::Instance()->MainDllPath.value();

        for (size_t i = 0; i < dllNames.size(); i++)
        {
            LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

            auto overridePath = Config::Instance()->XeLLLibrary.value_or(L"");

            Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &_memoryDll, &mainModule);

            if (mainModule != nullptr)
            {
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

    static bool InitXeLLInput()
    {
#ifdef LOW_LATENCY_INPUTS
        RedirectToInput(_dll);

        if (_memoryDll != nullptr && _memoryDll != _dll)
            RedirectToInput(_memoryDll);
#endif

        return true;
    }

#ifdef LOW_LATENCY_INPUTS
    // Only called on the real XeLL by the low latency inputs
    inline static PVOID _xellQueryInterface = nullptr;
    inline static PVOID _xellGetContextParameterP = nullptr;
    inline static PVOID _xellSetContextParameterP = nullptr;

    struct RealEntry
    {
        const char* name;
        PVOID* real;
    };

    // The real XeLL's calls that take a context, they go to OptiScaler's exports and these keep the trampolines
    inline static RealEntry _realEntries[] = {
        { "xellDestroyContext", (PVOID*) &_xellDestroyContext },
        { "xellSetSleepMode", (PVOID*) &_xellSetSleepMode },
        { "xellGetSleepMode", (PVOID*) &_xellGetSleepMode },
        { "xellSleep", (PVOID*) &_xellSleep },
        { "xellAddMarkerData", (PVOID*) &_xellAddMarkerData },
        { "xellSetLoggingCallback", (PVOID*) &_xellSetLoggingCallback },
        { "xellGetFramesReports", (PVOID*) &_xellGetFramesReports },
        { "xellD3D12CreateContext", (PVOID*) &_xellD3D12CreateContext },
        { "xellD3D12SetAppQueue", (PVOID*) &_xellD3D12SetAppQueue },
        { "xellSetDisplayInfo", (PVOID*) &_xellSetDisplayInfo },
        { "xellSetFgEnabled", (PVOID*) &_xellSetFgEnabled },
        { "xellSetGeneratedFramesCount", (PVOID*) &_xellSetGeneratedFramesCount },
        { "xellGetLastPresentStartFrameId", (PVOID*) &_xellGetLastPresentStartFrameId },
        { "xellQueryInterface", &_xellQueryInterface },
        { "xellGetContextParameterP", &_xellGetContextParameterP },
        { "xellSetContextParameterP", &_xellSetContextParameterP },
    };

    inline static std::recursive_mutex _redirectMutex;
    inline static std::vector<HMODULE> _redirected;
#endif

  public:
#ifdef LOW_LATENCY_INPUTS
    // Every libxell's calls go to OptiScaler's XeLL exports, also the ones from libraries that import it or got it
    // before OptiScaler, so the game's XeLL is the low latency input. The first one becomes the real XeLL, reached
    // through the trampolines, any other one goes to OptiScaler's entirely.
    static void RedirectToInput(HMODULE module)
    {
        if (module == nullptr || module == dllModule)
            return;

        std::lock_guard lock(_redirectMutex);

        if (std::ranges::find(_redirected, module) != _redirected.end())
            return;

        _redirected.push_back(module);

        // The game loaded the only libxell
        if (_dll == nullptr && HookXeLL(module))
            _dllPath = DllPath(module).wstring();

        auto path = wstring_to_string(DllPath(module).wstring());

        if (module != _dll)
        {
            RedirectAllExports(module, dllModule);
            LOG_INFO("{} goes to the XeLL input", path);
            return;
        }

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        for (auto& entry : _realEntries)
        {
            auto ours = (PVOID) KernelBaseProxy::GetProcAddress_()(dllModule, entry.name);

            if (*entry.real != nullptr && ours != nullptr)
                DetourAttach(entry.real, ours);
        }

        auto result = DetourTransactionCommit();

        if (result == NO_ERROR)
            LOG_INFO("{} is the real XeLL, its calls go to the XeLL input", path);
        else
            LOG_ERROR("Couldn't redirect {} to the XeLL input: {}", path, result);
    }

    // The real XeLL's function, the trampoline when it was redirected
    static PVOID RealFunction(const char* name)
    {
        for (auto& entry : _realEntries)
        {
            if (strcmp(entry.name, name) == 0)
                return *entry.real;
        }

        return _dll != nullptr && _dll != dllModule ? (PVOID) KernelBaseProxy::GetProcAddress_()(_dll, name) : nullptr;
    }
#endif

    static HMODULE Module() { return _dll; }
    static std::wstring Module_Path() { return _dllPath; }

    static bool InitXeLL() { return InitXeLLProper() && InitXeLLInput(); }

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

                _xellD3D12SetAppQueue =
                    (PFN_xellD3D12SetAppQueue) KernelBaseProxy::GetProcAddress_()(_dll, "xellD3D12SetAppQueue");
                _xellSetDisplayInfo =
                    (PFN_xellSetDisplayInfo) KernelBaseProxy::GetProcAddress_()(_dll, "xellSetDisplayInfo");
                _xellSetFgEnabled = (PFN_xellSetFgEnabled) KernelBaseProxy::GetProcAddress_()(_dll, "xellSetFgEnabled");
                _xellSetGeneratedFramesCount = (PFN_xellSetGeneratedFramesCount) KernelBaseProxy::GetProcAddress_()(
                    _dll, "xellSetGeneratedFramesCount");
                _xellGetLastPresentStartFrameId =
                    (PFN_xellGetLastPresentStartFrameId) KernelBaseProxy::GetProcAddress_()(
                        _dll, "xellGetLastPresentStartFrameId");

#ifdef LOW_LATENCY_INPUTS
                _xellQueryInterface = (PVOID) KernelBaseProxy::GetProcAddress_()(_dll, "xellQueryInterface");
                _xellGetContextParameterP =
                    (PVOID) KernelBaseProxy::GetProcAddress_()(_dll, "xellGetContextParameterP");
                _xellSetContextParameterP =
                    (PVOID) KernelBaseProxy::GetProcAddress_()(_dll, "xellSetContextParameterP");
#endif
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

#ifdef LOW_LATENCY_INPUTS
    // We export / implement those functions
    static PFN_xellDestroyContext DestroyContext() { return xellDestroyContext; }
    static PFN_xellSetSleepMode SetSleepMode() { return xellSetSleepMode; }
    static PFN_xellGetSleepMode GetSleepMode() { return xellGetSleepMode; }
    static PFN_xellSleep Sleep() { return xellSleep; }
    static PFN_xellAddMarkerData AddMarkerData() { return xellAddMarkerData; }
    static PFN_xellGetVersion GetVersion() { return xellGetVersion; }
    static PFN_xellSetLoggingCallback SetLoggingCallback() { return xellSetLoggingCallback; }
    static PFN_xellGetFramesReports GetFramesReports() { return xellGetFramesReports; }
    static PFN_xellD3D12CreateContext D3D12CreateContext() { return xellD3D12CreateContext; }

    // Pointing to the actual DLL
    static PFN_xellDestroyContext RealDestroyContext() { return _xellDestroyContext; }
    static PFN_xellSetSleepMode RealSetSleepMode() { return _xellSetSleepMode; }
    static PFN_xellGetSleepMode RealGetSleepMode() { return _xellGetSleepMode; }
    static PFN_xellSleep RealSleep() { return _xellSleep; }
    static PFN_xellAddMarkerData RealAddMarkerData() { return _xellAddMarkerData; }
    static PFN_xellGetVersion RealGetVersion() { return _xellGetVersion; }
    static PFN_xellSetLoggingCallback RealSetLoggingCallback() { return _xellSetLoggingCallback; }
    static PFN_xellGetFramesReports RealGetFramesReports() { return _xellGetFramesReports; }
    static PFN_xellD3D12CreateContext RealD3D12CreateContext() { return _xellD3D12CreateContext; }
    static PFN_xellD3D12SetAppQueue RealD3D12SetAppQueue() { return _xellD3D12SetAppQueue; }
    static PFN_xellSetDisplayInfo RealSetDisplayInfo() { return _xellSetDisplayInfo; }
    static PFN_xellSetFgEnabled RealSetFgEnabled() { return _xellSetFgEnabled; }
    static PFN_xellSetGeneratedFramesCount RealSetGeneratedFramesCount() { return _xellSetGeneratedFramesCount; }
    static PFN_xellGetLastPresentStartFrameId RealGetLastPresentStartFrameId()
    {
        return _xellGetLastPresentStartFrameId;
    }
#else
    static PFN_xellDestroyContext DestroyContext() { return _xellDestroyContext; }
    static PFN_xellSetSleepMode SetSleepMode() { return _xellSetSleepMode; }
    static PFN_xellGetSleepMode GetSleepMode() { return _xellGetSleepMode; }
    static PFN_xellSleep Sleep() { return _xellSleep; }
    static PFN_xellAddMarkerData AddMarkerData() { return _xellAddMarkerData; }
    static PFN_xellGetVersion GetVersion() { return _xellGetVersion; }
    static PFN_xellSetLoggingCallback SetLoggingCallback() { return _xellSetLoggingCallback; }
    static PFN_xellGetFramesReports GetFramesReports() { return _xellGetFramesReports; }
    static PFN_xellD3D12CreateContext D3D12CreateContext() { return _xellD3D12CreateContext; }
#endif
};
