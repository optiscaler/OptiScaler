#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>

class SLProxy
{
  private:
    inline static HMODULE _dll = nullptr;
    inline static feature_version _slVersion {};

    // Core SL API function pointers
    inline static PFun_slInit* _slInit = nullptr;
    inline static PFun_slShutdown* _slShutdown = nullptr;
    inline static PFun_slIsFeatureSupported* _slIsFeatureSupported = nullptr;
    inline static PFun_slIsFeatureLoaded* _slIsFeatureLoaded = nullptr;
    inline static PFun_slSetFeatureLoaded* _slSetFeatureLoaded = nullptr;
    inline static PFun_slEvaluateFeature* _slEvaluateFeature = nullptr;
    inline static PFun_slAllocateResources* _slAllocateResources = nullptr;
    inline static PFun_slFreeResources* _slFreeResources = nullptr;
    inline static PFun_slSetTag* _slSetTag = nullptr;
    inline static PFun_slSetTagForFrame* _slSetTagForFrame = nullptr;
    inline static PFun_slSetConstants* _slSetConstants = nullptr;
    inline static PFun_slGetFeatureFunction* _slGetFeatureFunction = nullptr;
    inline static PFun_slGetNewFrameToken* _slGetNewFrameToken = nullptr;
    inline static PFun_slSetD3DDevice* _slSetD3DDevice = nullptr;
    inline static PFun_slUpgradeInterface* _slUpgradeInterface = nullptr;
    inline static PFun_slGetNativeInterface* _slGetNativeInterface = nullptr;
    inline static PFun_slGetFeatureVersion* _slGetFeatureVersion = nullptr;
    inline static PFun_slGetFeatureRequirements* _slGetFeatureRequirements = nullptr;

    // DLSS-G feature functions (resolved via slGetFeatureFunction after device is set)
    inline static PFun_slDLSSGSetOptions* _slDLSSGSetOptions = nullptr;
    inline static PFun_slDLSSGGetState* _slDLSSGGetState = nullptr;

    static bool ResolveCoreAPIs()
    {
        if (_dll == nullptr)
            return false;

        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

        auto getProcAddr = KernelBaseProxy::GetProcAddress_();

        _slInit = (PFun_slInit*) getProcAddr(_dll, "slInit");
        _slShutdown = (PFun_slShutdown*) getProcAddr(_dll, "slShutdown");
        _slIsFeatureSupported = (PFun_slIsFeatureSupported*) getProcAddr(_dll, "slIsFeatureSupported");
        _slIsFeatureLoaded = (PFun_slIsFeatureLoaded*) getProcAddr(_dll, "slIsFeatureLoaded");
        _slSetFeatureLoaded = (PFun_slSetFeatureLoaded*) getProcAddr(_dll, "slSetFeatureLoaded");
        _slEvaluateFeature = (PFun_slEvaluateFeature*) getProcAddr(_dll, "slEvaluateFeature");
        _slAllocateResources = (PFun_slAllocateResources*) getProcAddr(_dll, "slAllocateResources");
        _slFreeResources = (PFun_slFreeResources*) getProcAddr(_dll, "slFreeResources");
        _slSetTag = (PFun_slSetTag*) getProcAddr(_dll, "slSetTag");
        _slSetTagForFrame = (PFun_slSetTagForFrame*) getProcAddr(_dll, "slSetTagForFrame");
        _slSetConstants = (PFun_slSetConstants*) getProcAddr(_dll, "slSetConstants");
        _slGetFeatureFunction = (PFun_slGetFeatureFunction*) getProcAddr(_dll, "slGetFeatureFunction");
        _slGetNewFrameToken = (PFun_slGetNewFrameToken*) getProcAddr(_dll, "slGetNewFrameToken");
        _slSetD3DDevice = (PFun_slSetD3DDevice*) getProcAddr(_dll, "slSetD3DDevice");
        _slUpgradeInterface = (PFun_slUpgradeInterface*) getProcAddr(_dll, "slUpgradeInterface");
        _slGetNativeInterface = (PFun_slGetNativeInterface*) getProcAddr(_dll, "slGetNativeInterface");
        _slGetFeatureVersion = (PFun_slGetFeatureVersion*) getProcAddr(_dll, "slGetFeatureVersion");
        _slGetFeatureRequirements = (PFun_slGetFeatureRequirements*) getProcAddr(_dll, "slGetFeatureRequirements");

        bool success = _slInit != nullptr && _slShutdown != nullptr && _slSetD3DDevice != nullptr &&
                       _slEvaluateFeature != nullptr && _slSetTagForFrame != nullptr && _slSetConstants != nullptr &&
                       _slGetFeatureFunction != nullptr && _slGetNewFrameToken != nullptr;

        LOG_INFO("SL Core API resolve result: {}", success);
        return success;
    }

  public:
    static HMODULE Module() { return _dll; }
    static bool IsReady() { return _dll != nullptr && _slInit != nullptr; }
    static bool IsDLSSGReady() { return IsReady() && _slDLSSGSetOptions != nullptr && _slDLSSGGetState != nullptr; }

    static bool InitSL()
    {
        if (_dll != nullptr)
            return true;

        auto dllPath = Util::DllPath();
        std::filesystem::path slInterposerPath = dllPath.parent_path() / L"sl" / L"sl.interposer.dll";

        LOG_INFO(L"Trying to load sl.interposer.dll from: {}", slInterposerPath.wstring());

        {
            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};
            State::DisableChecks(0x534C4F50, "sl.interposer"); // "SLOP" owner ID

            _dll = NtdllProxy::LoadLibraryExW_Ldr(slInterposerPath.c_str(), NULL, 0);

            State::EnableChecks(0x534C4F50);
        }

        if (_dll == nullptr)
        {
            LOG_WARN("Failed to load sl.interposer.dll from sl/ subfolder");
            return false;
        }

        LOG_INFO("sl.interposer.dll loaded successfully");

        if (!ResolveCoreAPIs())
        {
            LOG_ERROR("Failed to resolve SL core API functions");
            FreeLibrary(_dll);
            _dll = nullptr;
            return false;
        }

        State::Instance().SLFilesAvailable = true;
        return true;
    }

    // Resolve DLSS-G feature functions - must be called AFTER slSetD3DDevice
    static bool ResolveDLSSGFunctions()
    {
        if (_slGetFeatureFunction == nullptr)
            return false;

        void* funcPtr = nullptr;

        auto result = _slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", funcPtr);
        if (result == sl::Result::eOk && funcPtr != nullptr)
        {
            _slDLSSGSetOptions = (PFun_slDLSSGSetOptions*) funcPtr;
        }
        else
        {
            LOG_ERROR("Failed to resolve slDLSSGSetOptions: {}", (int) result);
            return false;
        }

        funcPtr = nullptr;
        result = _slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", funcPtr);
        if (result == sl::Result::eOk && funcPtr != nullptr)
        {
            _slDLSSGGetState = (PFun_slDLSSGGetState*) funcPtr;
        }
        else
        {
            LOG_ERROR("Failed to resolve slDLSSGGetState: {}", (int) result);
            return false;
        }

        LOG_INFO("DLSS-G feature functions resolved successfully");
        return true;
    }

    static feature_version Version()
    {
        if (_slVersion.major == 0 && _slGetFeatureVersion != nullptr)
        {
            sl::FeatureVersion ver {};
            if (auto result = _slGetFeatureVersion(sl::kFeatureDLSS_G, ver); result == sl::Result::eOk)
            {
                _slVersion.major = ver.versionSL.major;
                _slVersion.minor = ver.versionSL.minor;
                _slVersion.patch = ver.versionSL.build;
                LOG_INFO("DLSS-G Version: v{}.{}.{}", _slVersion.major, _slVersion.minor, _slVersion.patch);
            }
            else
            {
                LOG_WARN("Can't get DLSS-G version: {}", (int) result);
            }
        }

        if (_slVersion.major == 0)
        {
            _slVersion.major = 1;
            _slVersion.minor = 0;
            _slVersion.patch = 0;
        }

        return _slVersion;
    }

    // Core SL API accessors
    static PFun_slInit* Init() { return _slInit; }
    static PFun_slShutdown* Shutdown() { return _slShutdown; }
    static PFun_slIsFeatureSupported* IsFeatureSupported() { return _slIsFeatureSupported; }
    static PFun_slIsFeatureLoaded* IsFeatureLoaded() { return _slIsFeatureLoaded; }
    static PFun_slSetFeatureLoaded* SetFeatureLoaded() { return _slSetFeatureLoaded; }
    static PFun_slEvaluateFeature* EvaluateFeature() { return _slEvaluateFeature; }
    static PFun_slAllocateResources* AllocateResources() { return _slAllocateResources; }
    static PFun_slFreeResources* FreeResources() { return _slFreeResources; }
    static PFun_slSetTag* SetTag() { return _slSetTag; }
    static PFun_slSetTagForFrame* SetTagForFrame() { return _slSetTagForFrame; }
    static PFun_slSetConstants* SetConstants() { return _slSetConstants; }
    static PFun_slGetFeatureFunction* GetFeatureFunction() { return _slGetFeatureFunction; }
    static PFun_slGetNewFrameToken* GetNewFrameToken() { return _slGetNewFrameToken; }
    static PFun_slSetD3DDevice* SetD3DDevice() { return _slSetD3DDevice; }
    static PFun_slUpgradeInterface* UpgradeInterface() { return _slUpgradeInterface; }
    static PFun_slGetNativeInterface* GetNativeInterface() { return _slGetNativeInterface; }
    static PFun_slGetFeatureVersion* GetFeatureVersion() { return _slGetFeatureVersion; }
    static PFun_slGetFeatureRequirements* GetFeatureRequirements() { return _slGetFeatureRequirements; }

    // DLSS-G feature function accessors
    static PFun_slDLSSGSetOptions* DLSSGSetOptions() { return _slDLSSGSetOptions; }
    static PFun_slDLSSGGetState* DLSSGGetState() { return _slDLSSGGetState; }
};
