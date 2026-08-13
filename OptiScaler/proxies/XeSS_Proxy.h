#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <inputs/XeSS_Common.h>
#include <inputs/XeSS_Dx12.h>
#include <inputs/XeSS_Dx11.h>
#include <inputs/XeSS_Vulkan.h>
#include <inputs/XeSS_Dbg.h>

#include "xess_debug.h"
#include "xess_d3d11.h"
#include "xess_d3d12.h"
#include "xess_d3d12_debug.h"
#include "xess_vk.h"
#include "xess_vk_debug.h"

#include "detours/detours.h"

#pragma comment(lib, "Version.lib")

typedef decltype(&xessD3D12CreateContext) PFN_xessD3D12CreateContext;
typedef decltype(&xessD3D12BuildPipelines) PFN_xessD3D12BuildPipelines;
typedef decltype(&xessD3D12Init) PFN_xessD3D12Init;
typedef decltype(&xessD3D12Execute) PFN_xessD3D12Execute;
typedef decltype(&xessSelectNetworkModel) PFN_xessSelectNetworkModel;
typedef decltype(&xessStartDump) PFN_xessStartDump;
typedef decltype(&xessGetVersion) PFN_xessGetVersion;
typedef decltype(&xessIsOptimalDriver) PFN_xessIsOptimalDriver;
typedef decltype(&xessSetLoggingCallback) PFN_xessSetLoggingCallback;
typedef decltype(&xessGetProperties) PFN_xessGetProperties;
typedef decltype(&xessDestroyContext) PFN_xessDestroyContext;
typedef decltype(&xessSetVelocityScale) PFN_xessSetVelocityScale;
typedef decltype(&xessGetVelocityScale) PFN_xessGetVelocityScale;

typedef decltype(&xessD3D12GetInitParams) PFN_xessD3D12GetInitParams;
typedef decltype(&xessForceLegacyScaleFactors) PFN_xessForceLegacyScaleFactors;
typedef decltype(&xessGetExposureMultiplier) PFN_xessGetExposureMultiplier;
typedef decltype(&xessGetInputResolution) PFN_xessGetInputResolution;
typedef decltype(&xessGetIntelXeFXVersion) PFN_xessGetIntelXeFXVersion;
typedef decltype(&xessGetJitterScale) PFN_xessGetJitterScale;
typedef decltype(&xessGetOptimalInputResolution) PFN_xessGetOptimalInputResolution;
typedef decltype(&xessSetExposureMultiplier) PFN_xessSetExposureMultiplier;
typedef decltype(&xessSetJitterScale) PFN_xessSetJitterScale;

typedef decltype(&xessD3D12GetResourcesToDump) PFN_xessD3D12GetResourcesToDump;
typedef decltype(&xessD3D12GetProfilingData) PFN_xessD3D12GetProfilingData;

typedef xess_result_t (*PFN_xessSetContextParameterF)(); // XeSS' headers don't export this
typedef decltype(&xessGetPipelineBuildStatus) PFN_xessGetPipelineBuildStatus;

// Vulkan
typedef decltype(&xessVKGetRequiredInstanceExtensions) PFN_xessVKGetRequiredInstanceExtensions;
typedef decltype(&xessVKGetRequiredDeviceExtensions) PFN_xessVKGetRequiredDeviceExtensions;
typedef decltype(&xessVKGetRequiredDeviceFeatures) PFN_xessVKGetRequiredDeviceFeatures;
typedef decltype(&xessVKCreateContext) PFN_xessVKCreateContext;
typedef decltype(&xessVKBuildPipelines) PFN_xessVKBuildPipelines;
typedef decltype(&xessVKInit) PFN_xessVKInit;
typedef decltype(&xessVKGetInitParams) PFN_xessVKGetInitParams;
typedef decltype(&xessVKExecute) PFN_xessVKExecute;
typedef decltype(&xessVKGetResourcesToDump) PFN_xessVKGetResourcesToDump;

// Dx11
typedef decltype(&xessD3D11CreateContext) PFN_xessD3D11CreateContext;
typedef decltype(&xessD3D11Init) PFN_xessD3D11Init;
typedef decltype(&xessD3D11GetInitParams) PFN_xessD3D11GetInitParams;
typedef decltype(&xessD3D11Execute) PFN_xessD3D11Execute;

struct XeSSModule
{
    HMODULE dll = nullptr;
    std::wstring filePath;
    bool hooked = false;

    PFN_xessD3D12CreateContext xessD3D12CreateContext = nullptr;
    PFN_xessD3D12BuildPipelines xessD3D12BuildPipelines = nullptr;
    PFN_xessD3D12Init xessD3D12Init = nullptr;
    PFN_xessD3D12Execute xessD3D12Execute = nullptr;
    PFN_xessSelectNetworkModel xessSelectNetworkModel = nullptr;
    PFN_xessStartDump xessStartDump = nullptr;
    PFN_xessGetVersion xessGetVersion = nullptr;
    PFN_xessIsOptimalDriver xessIsOptimalDriver = nullptr;
    PFN_xessSetLoggingCallback xessSetLoggingCallback = nullptr;
    PFN_xessGetProperties xessGetProperties = nullptr;
    PFN_xessGetVelocityScale xessGetVelocityScale = nullptr;
    PFN_xessDestroyContext xessDestroyContext = nullptr;
    PFN_xessSetVelocityScale xessSetVelocityScale = nullptr;

    PFN_xessD3D12GetInitParams xessD3D12GetInitParams = nullptr;
    PFN_xessForceLegacyScaleFactors xessForceLegacyScaleFactors = nullptr;
    PFN_xessGetExposureMultiplier xessGetExposureMultiplier = nullptr;
    PFN_xessGetInputResolution xessGetInputResolution = nullptr;
    PFN_xessGetIntelXeFXVersion xessGetIntelXeFXVersion = nullptr;
    PFN_xessGetJitterScale xessGetJitterScale = nullptr;
    PFN_xessGetOptimalInputResolution xessGetOptimalInputResolution = nullptr;
    PFN_xessSetExposureMultiplier xessSetExposureMultiplier = nullptr;
    PFN_xessSetJitterScale xessSetJitterScale = nullptr;

    PFN_xessD3D12GetResourcesToDump xessD3D12GetResourcesToDump = nullptr;
    PFN_xessD3D12GetProfilingData xessD3D12GetProfilingData = nullptr;
    PFN_xessSetContextParameterF xessSetContextParameterF = nullptr;
    PFN_xessGetPipelineBuildStatus xessGetPipelineBuildStatus = nullptr;

    PFN_xessVKGetRequiredInstanceExtensions xessVKGetRequiredInstanceExtensions = nullptr;
    PFN_xessVKGetRequiredDeviceExtensions xessVKGetRequiredDeviceExtensions = nullptr;
    PFN_xessVKGetRequiredDeviceFeatures xessVKGetRequiredDeviceFeatures = nullptr;
    PFN_xessVKCreateContext xessVKCreateContext = nullptr;
    PFN_xessVKBuildPipelines xessVKBuildPipelines = nullptr;
    PFN_xessVKInit xessVKInit = nullptr;
    PFN_xessVKGetInitParams xessVKGetInitParams = nullptr;
    PFN_xessVKExecute xessVKExecute = nullptr;
    PFN_xessVKGetResourcesToDump xessVKGetResourcesToDump = nullptr;
};

struct XeSSDx11Module
{
    HMODULE dll = nullptr;
    std::wstring filePath;
    bool hooked = false;

    PFN_xessD3D11CreateContext xessD3D11CreateContext = nullptr;
    PFN_xessD3D11Init xessD3D11Init = nullptr;
    PFN_xessD3D11GetInitParams xessD3D11GetInitParams = nullptr;
    PFN_xessD3D11Execute xessD3D11Execute = nullptr;

    PFN_xessSelectNetworkModel xessSelectNetworkModelDx11 = nullptr;
    PFN_xessStartDump xessStartDumpDx11 = nullptr;
    PFN_xessGetVersion xessGetVersionDx11 = nullptr;
    PFN_xessIsOptimalDriver xessIsOptimalDriverDx11 = nullptr;
    PFN_xessSetLoggingCallback xessSetLoggingCallbackDx11 = nullptr;
    PFN_xessGetVelocityScale xessGetVelocityScaleDx11 = nullptr;
    PFN_xessGetProperties xessGetPropertiesDx11 = nullptr;
    PFN_xessDestroyContext xessDestroyContextDx11 = nullptr;
    PFN_xessSetVelocityScale xessSetVelocityScaleDx11 = nullptr;
    PFN_xessForceLegacyScaleFactors xessForceLegacyScaleFactorsDx11 = nullptr;
    PFN_xessGetExposureMultiplier xessGetExposureMultiplierDx11 = nullptr;
    PFN_xessGetInputResolution xessGetInputResolutionDx11 = nullptr;
    PFN_xessGetIntelXeFXVersion xessGetIntelXeFXVersionDx11 = nullptr;
    PFN_xessGetJitterScale xessGetJitterScaleDx11 = nullptr;
    PFN_xessGetOptimalInputResolution xessGetOptimalInputResolutionDx11 = nullptr;
    PFN_xessSetExposureMultiplier xessSetExposureMultiplierDx11 = nullptr;
    PFN_xessSetJitterScale xessSetJitterScaleDx11 = nullptr;
    PFN_xessSetContextParameterF xessSetContextParameterFDx11 = nullptr;
    PFN_xessGetPipelineBuildStatus xessGetPipelineBuildStatusDx11 = nullptr;
};

class XeSSProxy
{
  private:
    inline static XeSSModule _module = {};
    inline static XeSSDx11Module _module11 = {};
    inline static XeSSModule _module_hooked = {};
    inline static XeSSDx11Module _module11_hooked = {};

    inline static xess_version_t _xessVersion {};
    inline static xess_version_t _xessVersionDx11 {};

    inline static xess_version_t GetDLLVersion(std::wstring dllPath)
    {
        xess_version_t xessVersion {};
        version_t tempVersion {};
        auto result = Util::GetFileVersion(dllPath, &tempVersion);

        // Don't assume that the structs are identical
        if (result)
        {
            xessVersion.major = tempVersion.major;
            xessVersion.minor = tempVersion.minor;
            xessVersion.patch = tempVersion.patch;
            xessVersion.reserved = tempVersion.reserved;
        }

        return xessVersion;
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
    static HMODULE Module() { return _module.dll; }
    static HMODULE ModuleDx11() { return _module11.dll; }

    static std::wstring Module_Path() { return _module.filePath; }
    static std::wstring ModuleDx11_Path() { return _module11.filePath; }

    static bool InitXeSS(HMODULE module = nullptr)
    {
        if (_module.dll != nullptr)
            return true;

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && _module_hooked.dll == nullptr)
        {
            _module_hooked.dll = module;
        }

        if (_module.dll == nullptr)
        {
            std::vector<std::wstring> dllNames = { L"libxess.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->XeSSLibrary.value_or(L"");

                if (_module_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &_module_hooked.dll, &_module.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &_module.dll);
                }

                if (_module.dll != nullptr)
                {
                    break;
                }
            }
        }

        // Can't find Opti dlls, use just loaded module
        if (_module.dll == nullptr && _module_hooked.dll != nullptr)
        {
            _module = _module_hooked;
        }

        if (_module.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(_module.dll, modulePath, MAX_PATH);
            _module.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(_module.filePath));
        }

        if (_module.dll != nullptr && _module.xessD3D12CreateContext == nullptr)
        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

            _module.xessD3D12CreateContext =
                (PFN_xessD3D12CreateContext) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessD3D12CreateContext");
            _module.xessD3D12BuildPipelines = (PFN_xessD3D12BuildPipelines) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "xessD3D12BuildPipelines");
            _module.xessD3D12Init =
                (PFN_xessD3D12Init) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessD3D12Init");
            _module.xessD3D12Execute =
                (PFN_xessD3D12Execute) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessD3D12Execute");
            _module.xessSelectNetworkModel =
                (PFN_xessSelectNetworkModel) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessSelectNetworkModel");
            _module.xessStartDump =
                (PFN_xessStartDump) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessStartDump");
            _module.xessGetVersion =
                (PFN_xessGetVersion) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessGetVersion");
            _module.xessIsOptimalDriver =
                (PFN_xessIsOptimalDriver) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessIsOptimalDriver");
            _module.xessSetLoggingCallback =
                (PFN_xessSetLoggingCallback) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessSetLoggingCallback");
            _module.xessGetProperties =
                (PFN_xessGetProperties) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessGetProperties");
            _module.xessDestroyContext =
                (PFN_xessDestroyContext) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessDestroyContext");
            _module.xessSetVelocityScale =
                (PFN_xessSetVelocityScale) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessSetVelocityScale");

            _module.xessD3D12GetInitParams =
                (PFN_xessD3D12GetInitParams) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessD3D12GetInitParams");
            _module.xessGetVelocityScale =
                (PFN_xessGetVelocityScale) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessGetVelocityScale");
            _module.xessForceLegacyScaleFactors = (PFN_xessForceLegacyScaleFactors) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "xessForceLegacyScaleFactors");
            _module.xessGetExposureMultiplier = (PFN_xessGetExposureMultiplier) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "xessGetExposureMultiplier");
            _module.xessGetInputResolution =
                (PFN_xessGetInputResolution) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessGetInputResolution");
            _module.xessGetIntelXeFXVersion = (PFN_xessGetIntelXeFXVersion) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "xessGetIntelXeFXVersion");
            _module.xessGetJitterScale =
                (PFN_xessGetJitterScale) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessGetJitterScale");
            _module.xessGetOptimalInputResolution =
                (PFN_xessGetOptimalInputResolution) KernelBaseProxy::GetProcAddress_()(_module.dll,
                                                                                       "xessGetOptimalInputResolution");
            _module.xessSetExposureMultiplier = (PFN_xessSetExposureMultiplier) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "xessSetExposureMultiplier");
            _module.xessSetJitterScale =
                (PFN_xessSetJitterScale) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessSetJitterScale");

            _module.xessD3D12GetResourcesToDump = (PFN_xessD3D12GetResourcesToDump) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "xessD3D12GetResourcesToDump");
            _module.xessD3D12GetProfilingData = (PFN_xessD3D12GetProfilingData) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "xessD3D12GetProfilingData");
            _module.xessSetContextParameterF = (PFN_xessSetContextParameterF) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "xessSetContextParameterF");
            _module.xessGetPipelineBuildStatus = (PFN_xessGetPipelineBuildStatus) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "xessGetPipelineBuildStatus");

            _module.xessVKGetRequiredInstanceExtensions =
                (PFN_xessVKGetRequiredInstanceExtensions) KernelBaseProxy::GetProcAddress_()(
                    _module.dll, "xessVKGetRequiredInstanceExtensions");
            _module.xessVKGetRequiredDeviceExtensions =
                (PFN_xessVKGetRequiredDeviceExtensions) KernelBaseProxy::GetProcAddress_()(
                    _module.dll, "xessVKGetRequiredDeviceExtensions");
            _module.xessVKGetRequiredDeviceFeatures =
                (PFN_xessVKGetRequiredDeviceFeatures) KernelBaseProxy::GetProcAddress_()(
                    _module.dll, "xessVKGetRequiredDeviceFeatures");
            _module.xessVKCreateContext =
                (PFN_xessVKCreateContext) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessVKCreateContext");
            _module.xessVKBuildPipelines =
                (PFN_xessVKBuildPipelines) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessVKBuildPipelines");
            _module.xessVKInit = (PFN_xessVKInit) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessVKInit");
            _module.xessVKGetInitParams =
                (PFN_xessVKGetInitParams) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessVKGetInitParams");
            _module.xessVKExecute =
                (PFN_xessVKExecute) KernelBaseProxy::GetProcAddress_()(_module.dll, "xessVKExecute");
        }

        if (_module_hooked.dll != nullptr && _module_hooked.xessD3D12CreateContext == nullptr)
        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

            _module_hooked.xessD3D12CreateContext = (PFN_xessD3D12CreateContext) KernelBaseProxy::GetProcAddress_()(
                _module_hooked.dll, "xessD3D12CreateContext");
            _module_hooked.xessD3D12BuildPipelines = (PFN_xessD3D12BuildPipelines) KernelBaseProxy::GetProcAddress_()(
                _module_hooked.dll, "xessD3D12BuildPipelines");
            _module_hooked.xessD3D12Init =
                (PFN_xessD3D12Init) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessD3D12Init");
            _module_hooked.xessD3D12Execute =
                (PFN_xessD3D12Execute) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessD3D12Execute");
            _module_hooked.xessSelectNetworkModel = (PFN_xessSelectNetworkModel) KernelBaseProxy::GetProcAddress_()(
                _module_hooked.dll, "xessSelectNetworkModel");
            _module_hooked.xessStartDump =
                (PFN_xessStartDump) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessStartDump");
            _module_hooked.xessGetVersion =
                (PFN_xessGetVersion) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessGetVersion");
            _module_hooked.xessIsOptimalDriver =
                (PFN_xessIsOptimalDriver) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessIsOptimalDriver");
            _module_hooked.xessSetLoggingCallback = (PFN_xessSetLoggingCallback) KernelBaseProxy::GetProcAddress_()(
                _module_hooked.dll, "xessSetLoggingCallback");
            _module_hooked.xessGetProperties =
                (PFN_xessGetProperties) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessGetProperties");
            _module_hooked.xessDestroyContext =
                (PFN_xessDestroyContext) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessDestroyContext");
            _module_hooked.xessSetVelocityScale = (PFN_xessSetVelocityScale) KernelBaseProxy::GetProcAddress_()(
                _module_hooked.dll, "xessSetVelocityScale");

            _module_hooked.xessD3D12GetInitParams = (PFN_xessD3D12GetInitParams) KernelBaseProxy::GetProcAddress_()(
                _module_hooked.dll, "xessD3D12GetInitParams");
            _module_hooked.xessGetVelocityScale = (PFN_xessGetVelocityScale) KernelBaseProxy::GetProcAddress_()(
                _module_hooked.dll, "xessGetVelocityScale");
            _module_hooked.xessForceLegacyScaleFactors =
                (PFN_xessForceLegacyScaleFactors) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll,
                                                                                     "xessForceLegacyScaleFactors");
            _module_hooked.xessGetExposureMultiplier =
                (PFN_xessGetExposureMultiplier) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll,
                                                                                   "xessGetExposureMultiplier");
            _module_hooked.xessGetInputResolution = (PFN_xessGetInputResolution) KernelBaseProxy::GetProcAddress_()(
                _module_hooked.dll, "xessGetInputResolution");
            _module_hooked.xessGetIntelXeFXVersion = (PFN_xessGetIntelXeFXVersion) KernelBaseProxy::GetProcAddress_()(
                _module_hooked.dll, "xessGetIntelXeFXVersion");
            _module_hooked.xessGetJitterScale =
                (PFN_xessGetJitterScale) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessGetJitterScale");
            _module_hooked.xessGetOptimalInputResolution =
                (PFN_xessGetOptimalInputResolution) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll,
                                                                                       "xessGetOptimalInputResolution");
            _module_hooked.xessSetExposureMultiplier =
                (PFN_xessSetExposureMultiplier) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll,
                                                                                   "xessSetExposureMultiplier");
            _module_hooked.xessSetJitterScale =
                (PFN_xessSetJitterScale) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessSetJitterScale");

            _module_hooked.xessD3D12GetResourcesToDump =
                (PFN_xessD3D12GetResourcesToDump) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll,
                                                                                     "xessD3D12GetResourcesToDump");
            _module_hooked.xessD3D12GetProfilingData =
                (PFN_xessD3D12GetProfilingData) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll,
                                                                                   "xessD3D12GetProfilingData");
            _module_hooked.xessSetContextParameterF = (PFN_xessSetContextParameterF) KernelBaseProxy::GetProcAddress_()(
                _module_hooked.dll, "xessSetContextParameterF");
            _module_hooked.xessGetPipelineBuildStatus =
                (PFN_xessGetPipelineBuildStatus) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll,
                                                                                    "xessGetPipelineBuildStatus");

            _module_hooked.xessVKGetRequiredInstanceExtensions =
                (PFN_xessVKGetRequiredInstanceExtensions) KernelBaseProxy::GetProcAddress_()(
                    _module_hooked.dll, "xessVKGetRequiredInstanceExtensions");
            _module_hooked.xessVKGetRequiredDeviceExtensions =
                (PFN_xessVKGetRequiredDeviceExtensions) KernelBaseProxy::GetProcAddress_()(
                    _module_hooked.dll, "xessVKGetRequiredDeviceExtensions");
            _module_hooked.xessVKGetRequiredDeviceFeatures =
                (PFN_xessVKGetRequiredDeviceFeatures) KernelBaseProxy::GetProcAddress_()(
                    _module_hooked.dll, "xessVKGetRequiredDeviceFeatures");
            _module_hooked.xessVKCreateContext =
                (PFN_xessVKCreateContext) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessVKCreateContext");
            _module_hooked.xessVKBuildPipelines = (PFN_xessVKBuildPipelines) KernelBaseProxy::GetProcAddress_()(
                _module_hooked.dll, "xessVKBuildPipelines");
            _module_hooked.xessVKInit =
                (PFN_xessVKInit) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessVKInit");
            _module_hooked.xessVKGetInitParams =
                (PFN_xessVKGetInitParams) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessVKGetInitParams");
            _module_hooked.xessVKExecute =
                (PFN_xessVKExecute) KernelBaseProxy::GetProcAddress_()(_module_hooked.dll, "xessVKExecute");
        }
        else if (_module_hooked.xessD3D12CreateContext == nullptr)
        {
            _module_hooked = _module;
        }

        if (Config::Instance()->EnableXeSSInputs.value_or_default() && _module_hooked.dll != nullptr &&
            _module_hooked.xessD3D12CreateContext != nullptr && !_module_hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (_module_hooked.xessD3D12CreateContext != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessD3D12CreateContext, hk_xessD3D12CreateContext);

            if (_module_hooked.xessD3D12BuildPipelines != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessD3D12BuildPipelines, hk_xessD3D12BuildPipelines);

            if (_module_hooked.xessD3D12Init != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessD3D12Init, hk_xessD3D12Init);

            if (_module_hooked.xessGetVersion != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessGetVersion, hk_xessGetVersion);

            if (_module_hooked.xessD3D12Execute != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessD3D12Execute, hk_xessD3D12Execute);

            if (_module_hooked.xessSelectNetworkModel != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessSelectNetworkModel, hk_xessSelectNetworkModel);

            if (_module_hooked.xessStartDump != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessStartDump, hk_xessStartDump);

            if (_module_hooked.xessIsOptimalDriver != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessIsOptimalDriver, hk_xessIsOptimalDriver);

            if (_module_hooked.xessSetLoggingCallback != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessSetLoggingCallback, hk_xessSetLoggingCallback);

            if (_module_hooked.xessGetProperties != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessGetProperties, hk_xessGetProperties);

            if (_module_hooked.xessDestroyContext != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessDestroyContext, hk_xessDestroyContext);

            if (_module_hooked.xessSetVelocityScale != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessSetVelocityScale, hk_xessSetVelocityScale);

            if (_module_hooked.xessD3D12GetInitParams != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessD3D12GetInitParams, hk_xessD3D12GetInitParams);

            if (_module_hooked.xessForceLegacyScaleFactors != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessForceLegacyScaleFactors, hk_xessForceLegacyScaleFactors);

            if (_module_hooked.xessGetExposureMultiplier != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessGetExposureMultiplier, hk_xessGetExposureMultiplier);

            if (_module_hooked.xessGetInputResolution != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessGetInputResolution, hk_xessGetInputResolution);

            if (_module_hooked.xessGetIntelXeFXVersion != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessGetIntelXeFXVersion, hk_xessGetIntelXeFXVersion);

            if (_module_hooked.xessGetJitterScale != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessGetJitterScale, hk_xessGetJitterScale);

            if (_module_hooked.xessGetVelocityScale != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessGetVelocityScale, hk_xessGetVelocityScale);

            if (_module_hooked.xessGetOptimalInputResolution != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessGetOptimalInputResolution, hk_xessGetOptimalInputResolution);

            if (_module_hooked.xessSetExposureMultiplier != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessSetExposureMultiplier, hk_xessSetExposureMultiplier);

            if (_module_hooked.xessSetJitterScale != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessSetJitterScale, hk_xessSetJitterScale);

            if (_module_hooked.xessD3D12GetResourcesToDump != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessD3D12GetResourcesToDump, hk_xessD3D12GetResourcesToDump);

            if (_module_hooked.xessD3D12GetProfilingData != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessD3D12GetProfilingData, hk_xessD3D12GetProfilingData);

            if (_module_hooked.xessSetContextParameterF != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessSetContextParameterF, hk_xessSetContextParameterF);

            if (_module_hooked.xessVKCreateContext != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessVKCreateContext, hk_xessVKCreateContext);

            if (_module_hooked.xessVKBuildPipelines != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessVKBuildPipelines, hk_xessVKBuildPipelines);

            if (_module_hooked.xessVKInit != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessVKInit, hk_xessVKInit);

            if (_module_hooked.xessVKGetInitParams != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessVKGetInitParams, hk_xessVKGetInitParams);

            if (_module_hooked.xessVKExecute != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessVKExecute, hk_xessVKExecute);

            if (_module_hooked.xessVKGetResourcesToDump != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessVKGetResourcesToDump, hk_xessVKGetResourcesToDump);

            if (_module_hooked.xessVKGetRequiredDeviceExtensions != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessVKGetRequiredDeviceExtensions,
                             hk_xessVKGetRequiredDeviceExtensions);

            if (_module_hooked.xessVKGetRequiredDeviceFeatures != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessVKGetRequiredDeviceFeatures,
                             hk_xessVKGetRequiredDeviceFeatures);

            if (_module_hooked.xessVKGetRequiredInstanceExtensions != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessVKGetRequiredInstanceExtensions,
                             hk_xessVKGetRequiredInstanceExtensions);

            if (_module_hooked.xessGetPipelineBuildStatus != nullptr)
                DetourAttach(&(PVOID&) _module_hooked.xessGetPipelineBuildStatus, hk_xessGetPipelineBuildStatus);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to commit detours: {:X}", detourResult);
                _module_hooked.xessD3D12CreateContext = nullptr;
                _module_hooked.xessD3D12BuildPipelines = nullptr;
                _module_hooked.xessD3D12Init = nullptr;
                _module_hooked.xessD3D12Execute = nullptr;
                _module_hooked.xessSelectNetworkModel = nullptr;
                _module_hooked.xessStartDump = nullptr;
                _module_hooked.xessGetVersion = nullptr;
                _module_hooked.xessIsOptimalDriver = nullptr;
                _module_hooked.xessSetLoggingCallback = nullptr;
                _module_hooked.xessGetProperties = nullptr;
                _module_hooked.xessDestroyContext = nullptr;
                _module_hooked.xessSetVelocityScale = nullptr;
                _module_hooked.xessD3D12GetInitParams = nullptr;
                _module_hooked.xessForceLegacyScaleFactors = nullptr;
                _module_hooked.xessGetExposureMultiplier = nullptr;
                _module_hooked.xessGetInputResolution = nullptr;
                _module_hooked.xessGetIntelXeFXVersion = nullptr;
                _module_hooked.xessGetJitterScale = nullptr;
                _module_hooked.xessGetOptimalInputResolution = nullptr;
                _module_hooked.xessSetExposureMultiplier = nullptr;
                _module_hooked.xessSetJitterScale = nullptr;
                _module_hooked.xessD3D12GetResourcesToDump = nullptr;
                _module_hooked.xessD3D12GetProfilingData = nullptr;
                _module_hooked.xessSetContextParameterF = nullptr;
                _module_hooked.xessVKCreateContext = nullptr;
                _module_hooked.xessVKBuildPipelines = nullptr;
                _module_hooked.xessVKInit = nullptr;
                _module_hooked.xessVKGetInitParams = nullptr;
                _module_hooked.xessVKExecute = nullptr;
                _module_hooked.xessVKGetResourcesToDump = nullptr;
                _module_hooked.xessVKGetRequiredDeviceExtensions = nullptr;
                _module_hooked.xessVKGetRequiredDeviceFeatures = nullptr;
                _module_hooked.xessVKGetRequiredInstanceExtensions = nullptr;
                _module_hooked.xessGetPipelineBuildStatus = nullptr;
            }
            else
            {
                if (_module.dll == _module_hooked.dll)
                    _module = _module_hooked;

                _module_hooked.hooked = true;
            }
        }

        bool loadResult = _module.xessD3D12CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (!loadResult)
            _module.dll = nullptr;

        return loadResult;
    }

    static bool InitXeSSDx11(HMODULE module = nullptr)
    {
        if (_module11.dll != nullptr)
            return true;

        // This is module loaded by game, need to hook it to use inputs
        if (module != nullptr && _module11_hooked.dll == nullptr)
        {
            _module11_hooked.dll = module;
        }

        if (_module11.dll == nullptr)
        {
            std::vector<std::wstring> dllNames = { L"libxess_dx11.dll" };

            auto optiPath = Config::Instance()->MainDllPath.value();

            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                auto overridePath = Config::Instance()->XeSSDx11Library.value_or(L"");

                if (_module11_hooked.dll == nullptr)
                {
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &_module11_hooked.dll, &_module11.dll);
                }
                else
                {
                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &_module11.dll);
                }

                if (_module11.dll != nullptr)
                {
                    break;
                }
            }
        }

        // Can't find Opti dlls, use just loaded module
        if (_module11.dll == nullptr && _module11_hooked.dll != nullptr)
        {
            _module11 = _module11_hooked;
        }

        if (_module11.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(_module11.dll, modulePath, MAX_PATH);
            _module11.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(_module11.filePath));
        }

        if (_module11.dll != nullptr && _module11.xessD3D11CreateContext == nullptr)
        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

            _module11.xessD3D11CreateContext = (PFN_xessD3D11CreateContext) KernelBaseProxy::GetProcAddress_()(
                _module11.dll, "xessD3D11CreateContext");
            _module11.xessD3D11GetInitParams = (PFN_xessD3D11GetInitParams) KernelBaseProxy::GetProcAddress_()(
                _module11.dll, "xessD3D11GetInitParams");
            _module11.xessD3D11Init =
                (PFN_xessD3D11Init) KernelBaseProxy::GetProcAddress_()(_module11.dll, "xessD3D11Init");
            _module11.xessD3D11Execute =
                (PFN_xessD3D11Execute) KernelBaseProxy::GetProcAddress_()(_module11.dll, "xessD3D11Execute");
            _module11.xessDestroyContextDx11 =
                (PFN_xessDestroyContext) KernelBaseProxy::GetProcAddress_()(_module11.dll, "xessDestroyContext");
            _module11.xessForceLegacyScaleFactorsDx11 =
                (PFN_xessForceLegacyScaleFactors) KernelBaseProxy::GetProcAddress_()(_module11.dll,
                                                                                     "xessForceLegacyScaleFactors");
            _module11.xessGetExposureMultiplierDx11 =
                (PFN_xessGetExposureMultiplier) KernelBaseProxy::GetProcAddress_()(_module11.dll,
                                                                                   "xessGetExposureMultiplier");
            _module11.xessGetInputResolutionDx11 = (PFN_xessGetInputResolution) KernelBaseProxy::GetProcAddress_()(
                _module11.dll, "xessGetInputResolution");
            _module11.xessGetIntelXeFXVersionDx11 = (PFN_xessGetIntelXeFXVersion) KernelBaseProxy::GetProcAddress_()(
                _module11.dll, "xessGetIntelXeFXVersion");
            _module11.xessGetJitterScaleDx11 =
                (PFN_xessGetJitterScale) KernelBaseProxy::GetProcAddress_()(_module11.dll, "xessGetJitterScale");
            _module11.xessGetOptimalInputResolutionDx11 =
                (PFN_xessGetOptimalInputResolution) KernelBaseProxy::GetProcAddress_()(_module11.dll,
                                                                                       "xessGetOptimalInputResolution");
            _module11.xessGetPipelineBuildStatusDx11 =
                (PFN_xessGetPipelineBuildStatus) KernelBaseProxy::GetProcAddress_()(_module11.dll,
                                                                                    "xessGetPipelineBuildStatus");
            _module11.xessGetPropertiesDx11 =
                (PFN_xessGetProperties) KernelBaseProxy::GetProcAddress_()(_module11.dll, "xessGetProperties");
            _module11.xessGetVelocityScaleDx11 =
                (PFN_xessGetVelocityScale) KernelBaseProxy::GetProcAddress_()(_module11.dll, "xessGetVelocityScale");
            _module11.xessGetVersionDx11 =
                (PFN_xessGetVersion) KernelBaseProxy::GetProcAddress_()(_module11.dll, "xessGetVersion");
            _module11.xessIsOptimalDriverDx11 =
                (PFN_xessIsOptimalDriver) KernelBaseProxy::GetProcAddress_()(_module11.dll, "xessIsOptimalDriver");
            _module11.xessSelectNetworkModelDx11 = (PFN_xessSelectNetworkModel) KernelBaseProxy::GetProcAddress_()(
                _module11.dll, "xessSelectNetworkModel");
            _module11.xessSetContextParameterFDx11 = (PFN_xessSetContextParameterF) KernelBaseProxy::GetProcAddress_()(
                _module11.dll, "xessSetContextParameterF");
            _module11.xessSetExposureMultiplierDx11 =
                (PFN_xessSetExposureMultiplier) KernelBaseProxy::GetProcAddress_()(_module11.dll,
                                                                                   "xessSetExposureMultiplier");
            _module11.xessSetJitterScaleDx11 =
                (PFN_xessSetJitterScale) KernelBaseProxy::GetProcAddress_()(_module11.dll, "xessSetJitterScale");
            _module11.xessSetLoggingCallbackDx11 = (PFN_xessSetLoggingCallback) KernelBaseProxy::GetProcAddress_()(
                _module11.dll, "xessSetLoggingCallback");
            _module11.xessSetVelocityScaleDx11 =
                (PFN_xessSetVelocityScale) KernelBaseProxy::GetProcAddress_()(_module11.dll, "xessSetVelocityScale");
            _module11.xessStartDumpDx11 =
                (PFN_xessStartDump) KernelBaseProxy::GetProcAddress_()(_module11.dll, "xessStartDump");
        }

        if (_module11_hooked.dll != nullptr && _module11_hooked.xessD3D11CreateContext == nullptr)
        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

            _module11_hooked.xessD3D11CreateContext = (PFN_xessD3D11CreateContext) KernelBaseProxy::GetProcAddress_()(
                _module11_hooked.dll, "xessD3D11CreateContext");
            _module11_hooked.xessD3D11GetInitParams = (PFN_xessD3D11GetInitParams) KernelBaseProxy::GetProcAddress_()(
                _module11_hooked.dll, "xessD3D11GetInitParams");
            _module11_hooked.xessD3D11Init =
                (PFN_xessD3D11Init) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll, "xessD3D11Init");
            _module11_hooked.xessD3D11Execute =
                (PFN_xessD3D11Execute) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll, "xessD3D11Execute");
            _module11_hooked.xessDestroyContextDx11 =
                (PFN_xessDestroyContext) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll, "xessDestroyContext");
            _module11_hooked.xessForceLegacyScaleFactorsDx11 =
                (PFN_xessForceLegacyScaleFactors) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll,
                                                                                     "xessForceLegacyScaleFactors");
            _module11_hooked.xessGetExposureMultiplierDx11 =
                (PFN_xessGetExposureMultiplier) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll,
                                                                                   "xessGetExposureMultiplier");
            _module11_hooked.xessGetInputResolutionDx11 =
                (PFN_xessGetInputResolution) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll,
                                                                                "xessGetInputResolution");
            _module11_hooked.xessGetIntelXeFXVersionDx11 =
                (PFN_xessGetIntelXeFXVersion) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll,
                                                                                 "xessGetIntelXeFXVersion");
            _module11_hooked.xessGetJitterScaleDx11 =
                (PFN_xessGetJitterScale) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll, "xessGetJitterScale");
            _module11_hooked.xessGetOptimalInputResolutionDx11 =
                (PFN_xessGetOptimalInputResolution) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll,
                                                                                       "xessGetOptimalInputResolution");
            _module11_hooked.xessGetPipelineBuildStatusDx11 =
                (PFN_xessGetPipelineBuildStatus) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll,
                                                                                    "xessGetPipelineBuildStatus");
            _module11_hooked.xessGetPropertiesDx11 =
                (PFN_xessGetProperties) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll, "xessGetProperties");
            _module11_hooked.xessGetVelocityScaleDx11 = (PFN_xessGetVelocityScale) KernelBaseProxy::GetProcAddress_()(
                _module11_hooked.dll, "xessGetVelocityScale");
            _module11_hooked.xessGetVersionDx11 =
                (PFN_xessGetVersion) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll, "xessGetVersion");
            _module11_hooked.xessIsOptimalDriverDx11 = (PFN_xessIsOptimalDriver) KernelBaseProxy::GetProcAddress_()(
                _module11_hooked.dll, "xessIsOptimalDriver");
            _module11_hooked.xessSelectNetworkModelDx11 =
                (PFN_xessSelectNetworkModel) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll,
                                                                                "xessSelectNetworkModel");
            _module11_hooked.xessSetContextParameterFDx11 =
                (PFN_xessSetContextParameterF) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll,
                                                                                  "xessSetContextParameterF");
            _module11_hooked.xessSetExposureMultiplierDx11 =
                (PFN_xessSetExposureMultiplier) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll,
                                                                                   "xessSetExposureMultiplier");
            _module11_hooked.xessSetJitterScaleDx11 =
                (PFN_xessSetJitterScale) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll, "xessSetJitterScale");
            _module11_hooked.xessSetLoggingCallbackDx11 =
                (PFN_xessSetLoggingCallback) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll,
                                                                                "xessSetLoggingCallback");
            _module11_hooked.xessSetVelocityScaleDx11 = (PFN_xessSetVelocityScale) KernelBaseProxy::GetProcAddress_()(
                _module11_hooked.dll, "xessSetVelocityScale");
            _module11_hooked.xessStartDumpDx11 =
                (PFN_xessStartDump) KernelBaseProxy::GetProcAddress_()(_module11_hooked.dll, "xessStartDump");
        }
        else if (_module11_hooked.xessD3D11CreateContext == nullptr)
        {
            _module11_hooked = _module11;
        }

        if (Config::Instance()->EnableXeSSInputs.value_or_default() && _module11_hooked.dll != nullptr &&
            _module11_hooked.xessD3D11CreateContext != nullptr && !_module11_hooked.hooked)
        {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (_module11_hooked.xessD3D11CreateContext != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessD3D11CreateContext, hk_xessD3D11CreateContext);

            if (_module11_hooked.xessD3D11Execute != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessD3D11Execute, hk_xessD3D11Execute);

            if (_module11_hooked.xessD3D11GetInitParams != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessD3D11GetInitParams, hk_xessD3D11GetInitParams);

            if (_module11_hooked.xessD3D11Init != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessD3D11Init, hk_xessD3D11Init);

            if (_module11_hooked.xessDestroyContextDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessDestroyContextDx11, hk_xessDestroyContext);

            if (_module11_hooked.xessForceLegacyScaleFactorsDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessForceLegacyScaleFactorsDx11,
                             hk_xessForceLegacyScaleFactors);

            if (_module11_hooked.xessGetExposureMultiplierDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessGetExposureMultiplierDx11, hk_xessGetExposureMultiplier);

            if (_module11_hooked.xessGetInputResolutionDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessGetInputResolutionDx11, hk_xessGetInputResolution);

            if (_module11_hooked.xessGetIntelXeFXVersionDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessGetIntelXeFXVersionDx11, hk_xessGetIntelXeFXVersion);

            if (_module11_hooked.xessGetJitterScaleDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessGetJitterScaleDx11, hk_xessGetJitterScale);

            if (_module11_hooked.xessGetOptimalInputResolutionDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessGetOptimalInputResolutionDx11,
                             hk_xessGetOptimalInputResolution);

            if (_module11_hooked.xessGetPipelineBuildStatusDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessGetPipelineBuildStatusDx11, hk_xessGetPipelineBuildStatus);

            if (_module11_hooked.xessGetPropertiesDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessGetPropertiesDx11, hk_xessGetProperties);

            if (_module11_hooked.xessGetVelocityScaleDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessGetVelocityScaleDx11, hk_xessGetVelocityScale);

            if (_module11_hooked.xessGetVersionDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessGetVersionDx11, hk_xessGetVersionDx11);

            if (_module11_hooked.xessIsOptimalDriverDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessIsOptimalDriverDx11, hk_xessIsOptimalDriver);

            if (_module11_hooked.xessSelectNetworkModelDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessSelectNetworkModelDx11, hk_xessSelectNetworkModel);

            if (_module11_hooked.xessSetContextParameterFDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessSetContextParameterFDx11, hk_xessSetContextParameterF);

            if (_module11_hooked.xessSetExposureMultiplierDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessSetExposureMultiplierDx11, hk_xessSetExposureMultiplier);

            if (_module11_hooked.xessSetJitterScaleDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessSetJitterScaleDx11, hk_xessSetJitterScale);

            if (_module11_hooked.xessSetLoggingCallbackDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessSetLoggingCallbackDx11, hk_xessSetLoggingCallback);

            if (_module11_hooked.xessSetVelocityScaleDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessSetVelocityScaleDx11, hk_xessSetVelocityScale);

            if (_module11_hooked.xessStartDumpDx11 != nullptr)
                DetourAttach(&(PVOID&) _module11_hooked.xessStartDumpDx11, hk_xessStartDump);

            auto detourResult = DetourTransactionCommit();
            if (detourResult != NO_ERROR)
            {
                LOG_ERROR("Failed to commit detours: {:X}", detourResult);
                _module11_hooked.xessD3D11CreateContext = nullptr;
                _module11_hooked.xessD3D11GetInitParams = nullptr;
                _module11_hooked.xessD3D11Init = nullptr;
                _module11_hooked.xessD3D11Execute = nullptr;
                _module11_hooked.xessDestroyContextDx11 = nullptr;
                _module11_hooked.xessForceLegacyScaleFactorsDx11 = nullptr;
                _module11_hooked.xessGetExposureMultiplierDx11 = nullptr;
                _module11_hooked.xessGetInputResolutionDx11 = nullptr;
                _module11_hooked.xessGetIntelXeFXVersionDx11 = nullptr;
                _module11_hooked.xessGetJitterScaleDx11 = nullptr;
                _module11_hooked.xessGetOptimalInputResolutionDx11 = nullptr;
                _module11_hooked.xessGetPipelineBuildStatusDx11 = nullptr;
                _module11_hooked.xessGetPropertiesDx11 = nullptr;
                _module11_hooked.xessGetVelocityScaleDx11 = nullptr;
                _module11_hooked.xessGetVersionDx11 = nullptr;
                _module11_hooked.xessIsOptimalDriverDx11 = nullptr;
                _module11_hooked.xessSelectNetworkModelDx11 = nullptr;
                _module11_hooked.xessSetContextParameterFDx11 = nullptr;
                _module11_hooked.xessSetExposureMultiplierDx11 = nullptr;
                _module11_hooked.xessSetJitterScaleDx11 = nullptr;
                _module11_hooked.xessSetLoggingCallbackDx11 = nullptr;
                _module11_hooked.xessSetVelocityScaleDx11 = nullptr;
                _module11_hooked.xessStartDumpDx11 = nullptr;
            }
            else
            {
                if (_module11.dll == _module11_hooked.dll)
                    _module11 = _module11_hooked;

                _module11_hooked.hooked = true;
            }
        }

        bool loadResult = _module11_hooked.xessD3D11CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (!loadResult)
            _module11.dll = nullptr;

        return loadResult;
    }

    static xess_version_t Version()
    {
        if (_xessVersion.major == 0 && _module.xessGetVersion != nullptr)
        {
            if (auto result = _module.xessGetVersion(&_xessVersion); result == XESS_RESULT_SUCCESS)

                LOG_INFO("XeSS Version: v{}.{}.{}", _xessVersion.major, _xessVersion.minor, _xessVersion.patch);
            else
                LOG_ERROR("Can't get XeSS version: {}", (UINT) result);
        }

        // If dll version cant be read disable 1.3.x specific stuff
        if (_xessVersion.major == 0)
        {
            _xessVersion.major = 1;
            _xessVersion.minor = 2;
            _xessVersion.patch = 0;
        }

        return _xessVersion;
    }

    static xess_version_t VersionDx11()
    {
        if (_xessVersionDx11.major == 0 && _module11.xessGetVersionDx11 != nullptr)
            _module11.xessGetVersionDx11(&_xessVersionDx11);

        // If dll version cant be read disable 1.3.x specific stuff
        if (_xessVersionDx11.major == 0)
        {
            _xessVersionDx11.major = 1;
            _xessVersionDx11.minor = 2;
            _xessVersionDx11.patch = 0;
        }

        return _xessVersionDx11;
    }

    static PFN_xessD3D12CreateContext D3D12CreateContext() { return _module.xessD3D12CreateContext; }
    static PFN_xessD3D12BuildPipelines D3D12BuildPipelines() { return _module.xessD3D12BuildPipelines; }
    static PFN_xessD3D12Init D3D12Init() { return _module.xessD3D12Init; }
    static PFN_xessD3D12Execute D3D12Execute() { return _module.xessD3D12Execute; }
    static PFN_xessSelectNetworkModel SelectNetworkModel() { return _module.xessSelectNetworkModel; }
    static PFN_xessStartDump StartDump() { return _module.xessStartDump; }
    static PFN_xessGetVersion GetVersion() { return _module.xessGetVersion; }
    static PFN_xessIsOptimalDriver IsOptimalDriver() { return _module.xessIsOptimalDriver; }
    static PFN_xessSetLoggingCallback SetLoggingCallback() { return _module.xessSetLoggingCallback; }
    static PFN_xessGetProperties GetProperties() { return _module.xessGetProperties; }
    static PFN_xessDestroyContext DestroyContext() { return _module.xessDestroyContext; }
    static PFN_xessSetVelocityScale SetVelocityScale() { return _module.xessSetVelocityScale; }
    static PFN_xessD3D12GetInitParams D3D12GetInitParams() { return _module.xessD3D12GetInitParams; }
    static PFN_xessForceLegacyScaleFactors ForceLegacyScaleFactors() { return _module.xessForceLegacyScaleFactors; }
    static PFN_xessGetExposureMultiplier GetExposureMultiplier() { return _module.xessGetExposureMultiplier; }
    static PFN_xessGetInputResolution GetInputResolution() { return _module.xessGetInputResolution; }
    static PFN_xessGetIntelXeFXVersion GetIntelXeFXVersion() { return _module.xessGetIntelXeFXVersion; }
    static PFN_xessGetJitterScale GetJitterScale() { return _module.xessGetJitterScale; }
    static PFN_xessGetOptimalInputResolution GetOptimalInputResolution()
    {
        return _module.xessGetOptimalInputResolution;
    }
    static PFN_xessSetExposureMultiplier SetExposureMultiplier() { return _module.xessSetExposureMultiplier; }
    static PFN_xessSetJitterScale SetJitterScale() { return _module.xessSetJitterScale; }
    static PFN_xessD3D12GetResourcesToDump D3D12GetResourcesToDump() { return _module.xessD3D12GetResourcesToDump; }
    static PFN_xessD3D12GetProfilingData D3D12GetProfilingData() { return _module.xessD3D12GetProfilingData; }
    static PFN_xessSetContextParameterF SetContextParameterF() { return _module.xessSetContextParameterF; }

    static PFN_xessVKGetRequiredInstanceExtensions VKGetRequiredInstanceExtensions()
    {
        return _module.xessVKGetRequiredInstanceExtensions;
    }
    static PFN_xessVKGetRequiredDeviceExtensions VKGetRequiredDeviceExtensions()
    {
        return _module.xessVKGetRequiredDeviceExtensions;
    }
    static PFN_xessVKGetRequiredDeviceFeatures VKGetRequiredDeviceFeatures()
    {
        return _module.xessVKGetRequiredDeviceFeatures;
    }
    static PFN_xessVKCreateContext VKCreateContext() { return _module.xessVKCreateContext; }
    static PFN_xessVKBuildPipelines VKBuildPipelines() { return _module.xessVKBuildPipelines; }
    static PFN_xessVKInit VKInit() { return _module.xessVKInit; }
    static PFN_xessVKGetInitParams VKGetInitParams() { return _module.xessVKGetInitParams; }
    static PFN_xessVKExecute VKExecute() { return _module.xessVKExecute; }
    static PFN_xessVKGetResourcesToDump VKGetResourcesToDump() { return _module.xessVKGetResourcesToDump; }

    static PFN_xessD3D11CreateContext D3D11CreateContext() { return _module11.xessD3D11CreateContext; }
    static PFN_xessD3D11Init D3D11Init() { return _module11.xessD3D11Init; }
    static PFN_xessD3D11GetInitParams D3D11GetInitParams() { return _module11.xessD3D11GetInitParams; }
    static PFN_xessD3D11Execute D3D11Execute() { return _module11.xessD3D11Execute; }
    static PFN_xessSelectNetworkModel D3D11SelectNetworkModel() { return _module11.xessSelectNetworkModelDx11; }
    static PFN_xessStartDump D3D11StartDump() { return _module11.xessStartDumpDx11; }
    static PFN_xessGetVersion D3D11GetVersion() { return _module11.xessGetVersionDx11; }
    static PFN_xessIsOptimalDriver D3D11IsOptimalDriver() { return _module11.xessIsOptimalDriverDx11; }
    static PFN_xessSetLoggingCallback D3D11SetLoggingCallback() { return _module11.xessSetLoggingCallbackDx11; }
    static PFN_xessGetProperties D3D11GetProperties() { return _module11.xessGetPropertiesDx11; }
    static PFN_xessDestroyContext D3D11DestroyContext() { return _module11.xessDestroyContextDx11; }
    static PFN_xessSetVelocityScale D3D11SetVelocityScale() { return _module11.xessSetVelocityScaleDx11; }
    static PFN_xessForceLegacyScaleFactors D3D11ForceLegacyScaleFactors()
    {
        return _module11.xessForceLegacyScaleFactorsDx11;
    }
    static PFN_xessGetExposureMultiplier D3D11GetExposureMultiplier()
    {
        return _module11.xessGetExposureMultiplierDx11;
    }
    static PFN_xessGetInputResolution D3D11GetInputResolution() { return _module11.xessGetInputResolutionDx11; }
    static PFN_xessGetIntelXeFXVersion D3D11GetIntelXeFXVersion() { return _module11.xessGetIntelXeFXVersionDx11; }
    static PFN_xessGetJitterScale D3D11GetJitterScale() { return _module11.xessGetJitterScaleDx11; }
    static PFN_xessGetOptimalInputResolution D3D11GetOptimalInputResolution()
    {
        return _module11.xessGetOptimalInputResolutionDx11;
    }
    static PFN_xessSetExposureMultiplier D3D11SetExposureMultiplier()
    {
        return _module11.xessSetExposureMultiplierDx11;
    }
    static PFN_xessSetJitterScale D3D11SetJitterScale() { return _module11.xessSetJitterScaleDx11; }
};
