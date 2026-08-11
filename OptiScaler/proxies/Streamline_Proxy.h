#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>
#include <hooks/Streamline_Hooks.h>

#include <sl.h>
#include <sl_pcl.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>

#pragma comment(lib, "Version.lib")

class StreamlineProxy
{
  public:
    // Interposer
    typedef decltype(&slInit) PFN_slInit;
    typedef decltype(&slShutdown) PFN_slShutdown;
    typedef decltype(&slIsFeatureSupported) PFN_slIsFeatureSupported;
    typedef decltype(&slIsFeatureLoaded) PFN_slIsFeatureLoaded;
    typedef decltype(&slSetFeatureLoaded) PFN_slSetFeatureLoaded;
    typedef decltype(&slSetTagForFrame) PFN_slSetTagForFrame;
    typedef decltype(&slSetTag) PFN_slSetTag;
    typedef decltype(&slSetConstants) PFN_slSetConstants;
    typedef decltype(&slGetFeatureRequirements) PFN_slGetFeatureRequirements;
    typedef decltype(&slGetFeatureVersion) PFN_slGetFeatureVersion;
    typedef decltype(&slAllocateResources) PFN_slAllocateResources;
    typedef decltype(&slFreeResources) PFN_slFreeResources;
    typedef decltype(&slEvaluateFeature) PFN_slEvaluateFeature;
    typedef decltype(&slUpgradeInterface) PFN_slUpgradeInterface;
    typedef decltype(&slGetNativeInterface) PFN_slGetNativeInterface;
    typedef decltype(&slGetFeatureFunction) PFN_slGetFeatureFunction;
    typedef decltype(&slGetNewFrameToken) PFN_slGetNewFrameToken;
    typedef decltype(&slSetD3DDevice) PFN_slSetD3DDevice;

    typedef HRESULT (*PFN_CreateDxgiFactory)(REFIID riid, IDXGIFactory** ppFactory);
    typedef HRESULT (*PFN_CreateDxgiFactory1)(REFIID riid, IDXGIFactory1** ppFactory);
    typedef HRESULT (*PFN_CreateDxgiFactory2)(UINT Flags, REFIID riid, IDXGIFactory2** ppFactory);

    // Plugin
    typedef void* (*PFN_slGetPluginFunction)(const char* functionName);

    // DLSSG
    typedef decltype(&slDLSSGSetOptions) PFN_slDLSSGSetOptions;
    typedef decltype(&slDLSSGGetState) PFN_slDLSSGGetState;

    // Reflex
    typedef decltype(&slReflexGetState) PFN_slReflexGetState;
    typedef decltype(&slReflexSleep) PFN_slReflexSleep;
    typedef decltype(&slReflexSetOptions) PFN_slReflexSetOptions;
    typedef decltype(&slReflexSetCameraData) PFN_slReflexSetCameraData;
    typedef decltype(&slReflexGetPredictedCameraData) PFN_slReflexGetPredictedCameraData;

    // PCL
    typedef decltype(&slPCLGetState) PFN_slPCLGetState;
    typedef decltype(&slPCLSetMarker) PFN_slPCLSetMarker;
    typedef decltype(&slPCLSetOptions) PFN_slPCLSetOptions;

    static HMODULE Module() { return _dll; }

    static bool LoadStreamline()
    {
        if (_dll != nullptr)
            return true;

        auto owner = State::GetOwner();
        if (State::Instance().activeFgOutput == FGOutput::DLSSG &&
            State::Instance().activeFgNvngx != FGNvngxReplacement::None)
        {
            State::DisableChecks(owner, "sl.");
        }
        else
        {
            State::DisableChecks(owner);
        }

        std::filesystem::path localSlPath(Config::Instance()->MainDllPath.value());
        localSlPath = localSlPath / L"streamline"; // Hardcoded streamline folder

        std::filesystem::path slInterposerPath = localSlPath / L"sl.interposer.dll";
        LOG_INFO(L"Trying to load sl.interposer.dll from dll path: {}", slInterposerPath.wstring());
        _dll = NtdllProxy::LoadLibraryExW_Ldr(slInterposerPath.c_str(), NULL, NULL);

        State::EnableChecks(owner);

        if (_dll != nullptr)
        {
            State::Instance().optiSlInterposer = _dll;
            auto slCommonPath = localSlPath / L"sl.common.dll";
            State::Instance().optiSlCommon = NtdllProxy::LoadLibraryExW_Ldr(slCommonPath.c_str(), NULL, NULL);
            auto dlssgPath = localSlPath / L"nvngx_dlssg.dll"; // TODO: maybe some search?
            State::Instance().optiDLSSG = NtdllProxy::LoadLibraryExW_Ldr(dlssgPath.c_str(), NULL, NULL);

            return HookStreamline(_dll);
        }

        return false;
    }

    static bool HookStreamline(HMODULE slModule)
    {
        // if already hooked
        if (_dll != nullptr && _slInit != nullptr)
            return true;

        spdlog::info("");

        if (slModule == nullptr)
            return false;

        _dll = slModule;

        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

            if (_dll != nullptr)
            {
                // Common
                _slInit = (PFN_slInit) KernelBaseProxy::GetProcAddress_()(_dll, "slInit");
                _slShutdown = (PFN_slShutdown) KernelBaseProxy::GetProcAddress_()(_dll, "slShutdown");
                _slIsFeatureSupported =
                    (PFN_slIsFeatureSupported) KernelBaseProxy::GetProcAddress_()(_dll, "slIsFeatureSupported");
                _slIsFeatureLoaded =
                    (PFN_slIsFeatureLoaded) KernelBaseProxy::GetProcAddress_()(_dll, "slIsFeatureLoaded");
                _slSetFeatureLoaded =
                    (PFN_slSetFeatureLoaded) KernelBaseProxy::GetProcAddress_()(_dll, "slSetFeatureLoaded");
                _slSetTagForFrame = (PFN_slSetTagForFrame) KernelBaseProxy::GetProcAddress_()(_dll, "slSetTagForFrame");
                _slSetTag = (PFN_slSetTag) KernelBaseProxy::GetProcAddress_()(_dll, "slSetTag");
                _slSetConstants = (PFN_slSetConstants) KernelBaseProxy::GetProcAddress_()(_dll, "slSetConstants");
                _slGetFeatureRequirements =
                    (PFN_slGetFeatureRequirements) KernelBaseProxy::GetProcAddress_()(_dll, "slGetFeatureRequirements");
                _slGetFeatureVersion =
                    (PFN_slGetFeatureVersion) KernelBaseProxy::GetProcAddress_()(_dll, "slGetFeatureVersion");
                _slAllocateResources =
                    (PFN_slAllocateResources) KernelBaseProxy::GetProcAddress_()(_dll, "slAllocateResources");
                _slFreeResources = (PFN_slFreeResources) KernelBaseProxy::GetProcAddress_()(_dll, "slFreeResources");
                _slEvaluateFeature =
                    (PFN_slEvaluateFeature) KernelBaseProxy::GetProcAddress_()(_dll, "slEvaluateFeature");
                _slUpgradeInterface =
                    (PFN_slUpgradeInterface) KernelBaseProxy::GetProcAddress_()(_dll, "slUpgradeInterface");
                _slGetNativeInterface =
                    (PFN_slGetNativeInterface) KernelBaseProxy::GetProcAddress_()(_dll, "slGetNativeInterface");
                _slGetFeatureFunction =
                    (PFN_slGetFeatureFunction) KernelBaseProxy::GetProcAddress_()(_dll, "slGetFeatureFunction");
                _slGetNewFrameToken =
                    (PFN_slGetNewFrameToken) KernelBaseProxy::GetProcAddress_()(_dll, "slGetNewFrameToken");
                _slSetD3DDevice = (PFN_slSetD3DDevice) KernelBaseProxy::GetProcAddress_()(_dll, "slSetD3DDevice");
                _slCreateDxgiFactory =
                    (PFN_CreateDxgiFactory) KernelBaseProxy::GetProcAddress_()(_dll, "CreateDXGIFactory");
                _slCreateDxgiFactory1 =
                    (PFN_CreateDxgiFactory1) KernelBaseProxy::GetProcAddress_()(_dll, "CreateDXGIFactory1");
                _slCreateDxgiFactory2 =
                    (PFN_CreateDxgiFactory2) KernelBaseProxy::GetProcAddress_()(_dll, "CreateDXGIFactory2");
            }
        }

        bool result = _slInit != nullptr;
        LOG_INFO("Result: {}", result);
        return result;
    }

    static HMODULE HookStreamlineDLSSG()
    {
        spdlog::info("");

        std::filesystem::path localSlPath(Config::Instance()->MainDllPath.value());
        localSlPath = localSlPath / L"streamline" / L"sl.dlss_g.dll";
        auto dlssg = NtdllProxy::LoadLibraryExW_Ldr(localSlPath.c_str(), NULL, NULL);

        // if already hooked
        if (_slDLSSGSetOptions != nullptr)
            return dlssg;

        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

            if (dlssg != nullptr)
            {
                // Common
                _slDLSSGGetPluginFunction =
                    (PFN_slGetPluginFunction) KernelBaseProxy::GetProcAddress_()(dlssg, "slGetPluginFunction");

                if (_slDLSSGGetPluginFunction != nullptr)
                {
                    _slDLSSGSetOptions = (PFN_slDLSSGSetOptions) _slDLSSGGetPluginFunction("slDLSSGSetOptions");
                    _slDLSSGGetState = (PFN_slDLSSGGetState) _slDLSSGGetPluginFunction("slDLSSGGetState");
                }
            }
        }

        bool result = _slDLSSGSetOptions != nullptr;
        LOG_INFO("Result: {}", result);
        return dlssg;
    }

    static HMODULE HookStreamlineReflex()
    {
        spdlog::info("");

        std::filesystem::path localSlPath(Config::Instance()->MainDllPath.value());
        localSlPath = localSlPath / L"streamline" / L"sl.reflex.dll";
        auto reflex = NtdllProxy::LoadLibraryExW_Ldr(localSlPath.c_str(), NULL, NULL);

        // if already hooked
        if (_slReflexGetState != nullptr)
            return reflex;

        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

            if (reflex != nullptr)
            {
                // Common
                _slReflexGetPluginFunction =
                    (PFN_slGetPluginFunction) KernelBaseProxy::GetProcAddress_()(reflex, "slGetPluginFunction");

                if (_slReflexGetPluginFunction != nullptr)
                {
                    _slReflexGetState = (PFN_slReflexGetState) _slReflexGetPluginFunction("slReflexGetState");
                    _slReflexSleep = (PFN_slReflexSleep) _slReflexGetPluginFunction("slReflexSleep");
                    _slReflexSetOptions = (PFN_slReflexSetOptions) _slReflexGetPluginFunction("slReflexSetOptions");
                    _slReflexSetCameraData =
                        (PFN_slReflexSetCameraData) _slReflexGetPluginFunction("slReflexSetCameraData");
                    _slReflexGetPredictedCameraData = (PFN_slReflexGetPredictedCameraData) _slReflexGetPluginFunction(
                        "slReflexGetPredictedCameraData");
                }
            }
        }

        bool result = _slReflexGetState != nullptr;
        LOG_INFO("Result: {}", result);
        return reflex;
    }

    static HMODULE HookStreamlinePCL()
    {
        spdlog::info("");

        std::filesystem::path localSlPath(Config::Instance()->MainDllPath.value());
        localSlPath = localSlPath / L"streamline" / L"sl.pcl.dll";
        auto pcl = NtdllProxy::LoadLibraryExW_Ldr(localSlPath.c_str(), NULL, NULL);

        // if already hooked
        if (_slPCLSetMarker != nullptr)
            return pcl;

        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

            if (pcl != nullptr)
            {
                // Common
                _slPCLGetPluginFunction =
                    (PFN_slGetPluginFunction) KernelBaseProxy::GetProcAddress_()(pcl, "slGetPluginFunction");

                if (_slPCLGetPluginFunction != nullptr)
                {
                    _slPCLGetState = (PFN_slPCLGetState) _slPCLGetPluginFunction("slPCLGetState");
                    _slPCLSetMarker = (PFN_slPCLSetMarker) _slPCLGetPluginFunction("slPCLSetMarker");
                    _slPCLSetOptions = (PFN_slPCLSetOptions) _slPCLGetPluginFunction("slPCLSetOptions");
                }
            }
        }

        bool result = _slReflexGetState != nullptr;
        LOG_INFO("Result: {}", result);
        return pcl;
    }

    static feature_version Version()
    {
        if (_slVersion.major == 0)
        {
            _slVersion.major = 1;
            _slVersion.minor = 0;
            _slVersion.patch = 0;
        }

        return _slVersion;
    }

    static bool InitWithD3D12(ID3D12Device* device)
    {
        if (_isD3D12Inited)
            return true;

        if (!StreamlineProxy::LoadStreamline())
            return false;

        sl::Preferences pref;

#if _DEBUG
        pref.showConsole = true;
#endif
        pref.logMessageCallback = &slLogCallback;
        pref.logLevel = sl::LogLevel::eVerbose;

        sl::Feature featuresToLoad[] = { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };

        pref.applicationId = 0x0F71CA1E;
        pref.featuresToLoad = featuresToLoad;
        pref.numFeaturesToLoad = static_cast<uint32_t>(std::size(featuresToLoad));
        pref.renderAPI = sl::RenderAPI::eD3D12;

        pref.flags |= sl::PreferenceFlags::eUseManualHooking;
        pref.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;
        pref.flags |= sl::PreferenceFlags::eUseDXGIFactoryProxy;
        pref.flags &= ~sl::PreferenceFlags::eAllowOTA;
        pref.flags &= ~sl::PreferenceFlags::eLoadDownloadedPlugins;

        auto exePath = Util::ExePath().remove_filename();
        auto nvngxDlssPath = Util::FindFilePath(exePath, "nvngx_dlss.dll");
        auto nvngxDlssDPath = Util::FindFilePath(exePath, "nvngx_dlssd.dll");
        auto nvngxDlssGPath = Util::FindFilePath(exePath, "nvngx_dlssg.dll");

        std::vector<std::wstring> pathStorage;

        std::filesystem::path mainDllPath(Config::Instance()->MainDllPath.value());
        mainDllPath = mainDllPath / L"streamline";
        pathStorage.push_back(mainDllPath.wstring());

        if (nvngxDlssGPath.has_value())
            pathStorage.push_back(nvngxDlssGPath.value().parent_path().wstring());

        if (nvngxDlssPath.has_value())
            pathStorage.push_back(nvngxDlssPath.value().parent_path().wstring());

        if (nvngxDlssDPath.has_value())
            pathStorage.push_back(nvngxDlssDPath.value().parent_path().wstring());

        if (Config::Instance()->DLSSFeaturePath.has_value())
            pathStorage.push_back(Config::Instance()->DLSSFeaturePath.value());

        // Streamline makes a copy of those
        std::vector<const wchar_t*> paths;
        paths.reserve(pathStorage.size());

        for (const auto& path : pathStorage)
            paths.push_back(path.c_str());

        pref.pathsToPlugins = paths.data();
        pref.numPathsToPlugins = (uint32_t) paths.size();

        auto owner = State::GetOwner();
        if (State::Instance().activeFgOutput == FGOutput::DLSSG &&
            State::Instance().activeFgNvngx != FGNvngxReplacement::None)
        {
            State::DisableChecks(owner, "sl.");
        }
        else
        {
            State::DisableChecks(owner);
        }

        auto initResult = StreamlineProxy::Init()(pref, sl::kSDKVersion);

        State::EnableChecks(owner);

        if (initResult == sl::Result::eOk)
        {
            State::Instance().optiSlDLSSG = StreamlineProxy::HookStreamlineDLSSG();
            State::Instance().optiSlReflex = StreamlineProxy::HookStreamlineReflex();
            State::Instance().optiSlPCL = StreamlineProxy::HookStreamlinePCL();

            if (State::Instance().gameQuirks & GameQuirk::CreateSLOnThe2ndDevice)
            {
                // slSetD3DDevice moved to hkD3D12CreateDevice
                _isD3D12Inited = true;
            }
            else
            {
                auto result = _slSetD3DDevice(device);
                if (result == sl::Result::eOk)
                {
                    auto reflexConst = sl::ReflexOptions {};
                    reflexConst.mode = sl::ReflexMode::eOff;
                    reflexConst.useMarkersToOptimize = false;

                    result = _slReflexSetOptions(reflexConst);
                    _isD3D12Inited = result == sl::Result::eOk;
                }
            }
        }

        return _isD3D12Inited;
    }

    static bool InitWithD3D11(ID3D11Device* device)
    {
        if (_isD3D11Inited)
            return true;

        if (!StreamlineProxy::LoadStreamline())
            return false;

        sl::Preferences pref;

        pref.applicationId = 0x1337AFF;

#if _DEBUG
        pref.showConsole = true;
#endif
        pref.logMessageCallback = &slLogCallback;
        pref.logLevel = sl::LogLevel::eVerbose;

        sl::Feature featuresToLoad[] = { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };

        pref.featuresToLoad = featuresToLoad;
        pref.numFeaturesToLoad = static_cast<uint32_t>(std::size(featuresToLoad));
        pref.renderAPI = sl::RenderAPI::eD3D12;

        pref.flags |= sl::PreferenceFlags::eUseManualHooking;
        pref.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;

        auto initResult = StreamlineProxy::Init()(pref, sl::kSDKVersion);

        if (initResult == sl::Result::eOk)
        {
            StreamlineProxy::HookStreamlineDLSSG();
            StreamlineProxy::SetD3DDevice()(device);
            _isD3D11Inited = true;
        }

        return _isD3D11Inited;
    }

    static bool IsD3D11Inited() { return _isD3D11Inited; }

    static bool IsD3D12Inited() { return _isD3D12Inited; }

    static PFN_slInit Init() { return _slInit; }
    static PFN_slShutdown Shutdown() { return _slShutdown; }
    static PFN_slIsFeatureSupported IsFeatureSupported() { return _slIsFeatureSupported; }
    static PFN_slIsFeatureLoaded IsFeatureLoaded() { return _slIsFeatureLoaded; }
    static PFN_slSetFeatureLoaded SetFeatureLoaded() { return _slSetFeatureLoaded; }
    static PFN_slSetTagForFrame SetTagForFrame() { return _slSetTagForFrame; }
    static PFN_slSetTag SetTag() { return _slSetTag; }
    static PFN_slSetConstants SetConstants() { return _slSetConstants; }
    static PFN_slGetFeatureRequirements GetFeatureRequirements() { return _slGetFeatureRequirements; }
    static PFN_slGetFeatureVersion GetFeatureVersion() { return _slGetFeatureVersion; }
    static PFN_slAllocateResources AllocateResources() { return _slAllocateResources; }
    static PFN_slFreeResources FreeResources() { return _slFreeResources; }
    static PFN_slEvaluateFeature EvaluateFeature() { return _slEvaluateFeature; }
    static PFN_slUpgradeInterface UpgradeInterface() { return _slUpgradeInterface; }
    static PFN_slGetNativeInterface GetNativeInterface() { return _slGetNativeInterface; }
    static PFN_slGetFeatureFunction GetFeatureFunction() { return _slGetFeatureFunction; }
    static PFN_slGetNewFrameToken GetNewFrameToken() { return _slGetNewFrameToken; }
    static PFN_slSetD3DDevice SetD3DDevice() { return _slSetD3DDevice; }
    static PFN_CreateDxgiFactory CreateDxgiFactory() { return _slCreateDxgiFactory; }
    static PFN_CreateDxgiFactory1 CreateDxgiFactory1() { return _slCreateDxgiFactory1; }
    static PFN_CreateDxgiFactory2 CreateDxgiFactory2() { return _slCreateDxgiFactory2; }

    static PFN_slDLSSGSetOptions DLSSGSetOptions() { return _slDLSSGSetOptions; }
    static PFN_slDLSSGGetState DLSSGGetState() { return _slDLSSGGetState; }

    static PFN_slReflexGetState ReflexGetState() { return _slReflexGetState; }
    static PFN_slReflexSleep ReflexSleep() { return _slReflexSleep; }
    static PFN_slReflexSetOptions ReflexSetOptions() { return _slReflexSetOptions; }
    static PFN_slReflexSetCameraData ReflexSetCameraData() { return _slReflexSetCameraData; }
    static PFN_slReflexGetPredictedCameraData ReflexGetPredictedCameraData() { return _slReflexGetPredictedCameraData; }

    static PFN_slPCLGetState PCLGetState() { return _slPCLGetState; }
    static PFN_slPCLSetMarker PCLSetMarker() { return _slPCLSetMarker; }
    static PFN_slPCLSetOptions PCLSetOptions() { return _slPCLSetOptions; }

  private:
    inline static HMODULE _dll = nullptr;
    inline static feature_version _slVersion {};
    inline static bool _isInited = false;
    inline static bool _isD3D11Inited = false;
    inline static bool _isD3D12Inited = false;

    // Interposer
    inline static PFN_slInit _slInit = nullptr;
    inline static PFN_slShutdown _slShutdown = nullptr;
    inline static PFN_slIsFeatureSupported _slIsFeatureSupported = nullptr;
    inline static PFN_slIsFeatureLoaded _slIsFeatureLoaded = nullptr;
    inline static PFN_slSetFeatureLoaded _slSetFeatureLoaded = nullptr;
    inline static PFN_slSetTagForFrame _slSetTagForFrame = nullptr;
    inline static PFN_slSetTag _slSetTag = nullptr;
    inline static PFN_slSetConstants _slSetConstants = nullptr;
    inline static PFN_slGetFeatureRequirements _slGetFeatureRequirements = nullptr;
    inline static PFN_slGetFeatureVersion _slGetFeatureVersion = nullptr;
    inline static PFN_slAllocateResources _slAllocateResources = nullptr;
    inline static PFN_slFreeResources _slFreeResources = nullptr;
    inline static PFN_slEvaluateFeature _slEvaluateFeature = nullptr;
    inline static PFN_slUpgradeInterface _slUpgradeInterface = nullptr;
    inline static PFN_slGetNativeInterface _slGetNativeInterface = nullptr;
    inline static PFN_slGetFeatureFunction _slGetFeatureFunction = nullptr;
    inline static PFN_slGetNewFrameToken _slGetNewFrameToken = nullptr;
    inline static PFN_slSetD3DDevice _slSetD3DDevice = nullptr;
    inline static PFN_CreateDxgiFactory _slCreateDxgiFactory = nullptr;
    inline static PFN_CreateDxgiFactory1 _slCreateDxgiFactory1 = nullptr;
    inline static PFN_CreateDxgiFactory2 _slCreateDxgiFactory2 = nullptr;

    // DLSSG
    inline static PFN_slGetPluginFunction _slDLSSGGetPluginFunction = nullptr;
    inline static PFN_slDLSSGSetOptions _slDLSSGSetOptions = nullptr;
    inline static PFN_slDLSSGGetState _slDLSSGGetState = nullptr;

    // Reflex
    inline static PFN_slGetPluginFunction _slReflexGetPluginFunction = nullptr;
    inline static PFN_slReflexGetState _slReflexGetState = nullptr;
    inline static PFN_slReflexSleep _slReflexSleep = nullptr;
    inline static PFN_slReflexSetOptions _slReflexSetOptions = nullptr;
    inline static PFN_slReflexSetCameraData _slReflexSetCameraData = nullptr;
    inline static PFN_slReflexGetPredictedCameraData _slReflexGetPredictedCameraData = nullptr;

    // PCL
    inline static PFN_slGetPluginFunction _slPCLGetPluginFunction = nullptr;
    inline static PFN_slPCLGetState _slPCLGetState = nullptr;
    inline static PFN_slPCLSetMarker _slPCLSetMarker = nullptr;
    inline static PFN_slPCLSetOptions _slPCLSetOptions = nullptr;

    inline static void slLogCallback(sl::LogType type, const char* msg)
    {
        size_t len = std::strlen(msg) - 1;

        switch (type)
        {
        case sl::LogType::eInfo:
            spdlog::info("SL Log: {:.{}s}", msg, len);
            return;

        case sl::LogType::eWarn:
            spdlog::warn("SL Log: {:.{}s}", msg, len);
            return;

        default:
            spdlog::error("SL Log: {:.{}s}", msg, len);
            return;
        }
    }
};
