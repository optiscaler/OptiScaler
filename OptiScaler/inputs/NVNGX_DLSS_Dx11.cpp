#include "pch.h"

#include "Config.h"
#include "Util.h"

#include <upscalers/IFeature_Dx11.h>
#include <upscalers/FeatureProvider_Dx11.h>

#include "NVNGX_DLSS.h"
#include "NVNGX_Parameter.h"
#include "proxies/NVNGX_Proxy.h"

#include <upscaler_time/UpscalerTime_Dx11.h>

#include <ankerl/unordered_dense.h>

static ID3D11Device* D3D11Device = nullptr;
static ankerl::unordered_dense::map<unsigned int, ContextData<IFeature_Dx11>> Dx11Contexts;
static int evalCounter = 0;
static bool shutdown = false;
static bool _skipInit = false;
static wchar_t const** paths;

class ScopedInitDx11
{
  private:
    bool previousState;

  public:
    ScopedInitDx11()
    {
        previousState = _skipInit;
        _skipInit = true;
    }
    ~ScopedInitDx11() { _skipInit = previousState; }
};

static void UpdateInitPaths(NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    State::Instance().NVNGX_FeatureInfo_Paths.clear();

    if (InFeatureInfo != nullptr)
    {
        auto exePath = Util::ExePath().remove_filename();

        std::optional<std::filesystem::path> nvngxDlssPath = std::nullopt;
        std::optional<std::filesystem::path> nvngxDlssDPath = std::nullopt;
        std::optional<std::filesystem::path> nvngxDlssGPath = std::nullopt;

        // Check DLSS path
        if (State::Instance().NVNGX_DLSS_Path.has_value())
        {
            nvngxDlssPath = std::filesystem::path(State::Instance().NVNGX_DLSS_Path.value());
        }
        else
        {
            auto path = Util::FindFilePath(exePath, "nvngx_dlss.dll");

            if (path.has_value())
                nvngxDlssPath = path.value();
        }

        // Check DLSS-D path
        if (State::Instance().NVNGX_DLSSD_Path.has_value())
        {
            nvngxDlssDPath = std::filesystem::path(State::Instance().NVNGX_DLSSD_Path.value());
        }
        else
        {
            auto path = Util::FindFilePath(exePath, "nvngx_dlssd.dll");

            if (path.has_value())
                nvngxDlssDPath = path.value();
        }

        // Check DLSS-G path
        if (State::Instance().NVNGX_DLSSG_Path.has_value())
        {
            nvngxDlssGPath = std::filesystem::path(State::Instance().NVNGX_DLSSG_Path.value());
        }
        else
        {
            auto path = Util::FindFilePath(exePath, "nvngx_dlssg.dll");

            if (path.has_value())
                nvngxDlssGPath = path.value();
        }

        // Override locations
        if (Config::Instance()->DLSSFeaturePath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(Config::Instance()->DLSSFeaturePath.value());

        // If DLSS path is overriden
        if (Config::Instance()->NVNGX_DLSS_Library.has_value() && nvngxDlssPath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(nvngxDlssPath.value().parent_path().wstring());

        // OptiDll Path
        State::Instance().NVNGX_FeatureInfo_Paths.push_back(Config::Instance()->MainDllPath.value());

        // Original paths from NVNGX
        for (size_t i = 0; i < InFeatureInfo->PathListInfo.Length; i++)
        {
            const wchar_t* path = InFeatureInfo->PathListInfo.Path[i];
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(std::wstring(path));
        }

        // Exe path
        State::Instance().NVNGX_FeatureInfo_Paths.push_back(exePath.wstring());

        // If DLSS path is not overriden
        if (!Config::Instance()->NVNGX_DLSS_Library.has_value() && nvngxDlssPath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(nvngxDlssPath.value().parent_path().wstring());

        // Add found locations
        if (nvngxDlssDPath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(nvngxDlssDPath.value().parent_path().wstring());

        if (nvngxDlssGPath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(nvngxDlssGPath.value().parent_path().wstring());

        // Build pointer array
        paths = new const wchar_t*[State::Instance().NVNGX_FeatureInfo_Paths.size()];
        for (size_t i = 0; i < State::Instance().NVNGX_FeatureInfo_Paths.size(); ++i)
        {
            paths[i] = State::Instance().NVNGX_FeatureInfo_Paths[i].c_str();
            LOG_DEBUG("Feature Path [{}]: {}", i, wstring_to_string(State::Instance().NVNGX_FeatureInfo_Paths[i]));
        }

        InFeatureInfo->PathListInfo.Path = paths;
        InFeatureInfo->PathListInfo.Length = (int) State::Instance().NVNGX_FeatureInfo_Paths.size();
    }
}

#pragma region NVSDK_NGX_D3D11_Init

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_Init_Ext(unsigned long long InApplicationId,
                                                        const wchar_t* InApplicationDataPath, ID3D11Device* InDevice,
                                                        NVSDK_NGX_Version InSDKVersion,
                                                        const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    NVSDK_NGX_FeatureCommonInfo localFeatureInfo = {};

    if (InFeatureInfo != nullptr)
        std::memcpy(&localFeatureInfo, InFeatureInfo, sizeof(NVSDK_NGX_FeatureCommonInfo));

    if (!_skipInit)
        UpdateInitPaths(&localFeatureInfo);

    State::Instance().NVNGX_ApplicationId = InApplicationId;
    State::Instance().NVNGX_ApplicationDataPath = std::wstring(InApplicationDataPath);
    State::Instance().NVNGX_Version = InSDKVersion;
    State::Instance().NVNGX_FeatureInfo = InFeatureInfo;
    State::Instance().NVNGX_Version = InSDKVersion;

    if (Config::Instance()->DLSSEnabled.value_or_default() && !_skipInit)
    {
        if (Config::Instance()->UseGenericAppIdWithDlss.value_or_default())
            InApplicationId = app_id_override;

        if (NVNGXProxy::NVNGXModule() == nullptr)
            NVNGXProxy::InitNVNGX();

        if (NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D11_Init_Ext() != nullptr)
        {
            LOG_INFO("calling NVNGXProxy::D3D11_Init_Ext");

            auto result = NVNGXProxy::D3D11_Init_Ext()(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion,
                                                       &localFeatureInfo);

            LOG_INFO("calling NVNGXProxy::D3D11_Init_Ext result: {0:X}", (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
                NVNGXProxy::SetDx11Inited(true);
        }
        else
        {
            LOG_WARN("NVNGXProxy::NVNGXModule or NVNGXProxy::D3D11_Init_Ext is nullptr!");
        }
    }

    if (InFeatureInfo != nullptr && InSDKVersion > 0x0000013)
        State::Instance().NVNGX_Logger = InFeatureInfo->LoggingInfo;

    if (State::Instance().NvngxDx11Inited && InDevice == D3D11Device)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    LOG_INFO("AppId: {0}", InApplicationId);
    LOG_INFO("SDK: {0:x}", (int) InSDKVersion);
    std::wstring string(InApplicationDataPath);

    LOG_DEBUG("InApplicationDataPath {0}", wstring_to_string(string));

    if (InDevice)
        D3D11Device = InDevice;

    State::Instance().currentD3D11Device = InDevice;
    State::Instance().NvngxDx11Inited = true;

    UpscalerTimeDx11::Init(InDevice);

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_Init(unsigned long long InApplicationId,
                                                    const wchar_t* InApplicationDataPath, ID3D11Device* InDevice,
                                                    const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                                    NVSDK_NGX_Version InSDKVersion)
{
    NVSDK_NGX_FeatureCommonInfo localFeatureInfo = {};

    if (InFeatureInfo != nullptr)
        std::memcpy(&localFeatureInfo, InFeatureInfo, sizeof(NVSDK_NGX_FeatureCommonInfo));

    if (!_skipInit)
        UpdateInitPaths(&localFeatureInfo);

    if (Config::Instance()->DLSSEnabled.value_or_default() && !_skipInit)
    {
        if (Config::Instance()->UseGenericAppIdWithDlss.value_or_default())
            InApplicationId = app_id_override;

        if (NVNGXProxy::NVNGXModule() == nullptr)
            NVNGXProxy::InitNVNGX();

        if (NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D11_Init() != nullptr)
        {
            LOG_INFO("calling NVNGXProxy::D3D11_Init");

            auto result = NVNGXProxy::D3D11_Init()(InApplicationId, InApplicationDataPath, InDevice, &localFeatureInfo,
                                                   InSDKVersion);

            LOG_INFO("calling NVNGXProxy::D3D11_Init result: {0:X}", (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
                NVNGXProxy::SetDx11Inited(true);
        }
    }

    ScopedInitDx11 scopedInit {};
    auto result = NVSDK_NGX_D3D11_Init_Ext(0x1337, InApplicationDataPath, InDevice, InSDKVersion, &localFeatureInfo);
    LOG_DEBUG("was called NVSDK_NGX_D3D11_Init_Ext");
    return result;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_Init_ProjectID(const char* InProjectId,
                                                              NVSDK_NGX_EngineType InEngineType,
                                                              const char* InEngineVersion,
                                                              const wchar_t* InApplicationDataPath,
                                                              ID3D11Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                                              const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    NVSDK_NGX_FeatureCommonInfo localFeatureInfo = {};

    if (InFeatureInfo != nullptr)
        std::memcpy(&localFeatureInfo, InFeatureInfo, sizeof(NVSDK_NGX_FeatureCommonInfo));

    if (!_skipInit)
        UpdateInitPaths(&localFeatureInfo);

    if (Config::Instance()->DLSSEnabled.value_or_default() && !_skipInit)
    {
        if (Config::Instance()->UseGenericAppIdWithDlss.value_or_default())
            InProjectId = project_id_override;

        if (NVNGXProxy::NVNGXModule() == nullptr)
            NVNGXProxy::InitNVNGX();

        if (NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D11_Init_ProjectID() != nullptr)
        {
            LOG_INFO("calling NVNGXProxy::D3D11_Init_ProjectID");

            auto result =
                NVNGXProxy::D3D11_Init_ProjectID()(InProjectId, InEngineType, InEngineVersion, InApplicationDataPath,
                                                   InDevice, InSDKVersion, &localFeatureInfo);

            LOG_INFO("calling NVNGXProxy::D3D11_Init_ProjectID result: {0:X}", (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
                NVNGXProxy::SetDx11Inited(true);
        }
    }

    ScopedInitDx11 scopedInit {};
    auto result = NVSDK_NGX_D3D11_Init_Ext(0x1337, InApplicationDataPath, InDevice, InSDKVersion, &localFeatureInfo);

    LOG_INFO("InProjectId: {0}", InProjectId);
    LOG_INFO("InEngineType: {0}", (int) InEngineType);
    LOG_INFO("InEngineVersion: {0}", InEngineVersion);

    State::Instance().NVNGX_ProjectId = std::string(InProjectId);
    State::Instance().NVNGX_Engine = InEngineType;
    State::Instance().NVNGX_EngineVersion = std::string(InEngineVersion);

    return result;
}

// Not sure about this one, original nvngx does not export this method
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_Init_with_ProjectID(
    const char* InProjectId, NVSDK_NGX_EngineType InEngineType, const char* InEngineVersion,
    const wchar_t* InApplicationDataPath, ID3D11Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
    NVSDK_NGX_Version InSDKVersion)
{
    auto result = NVSDK_NGX_D3D11_Init_Ext(0x1337, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);

    LOG_INFO("InProjectId: {0}", InProjectId);
    LOG_INFO("InEngineType: {0}", (int) InEngineType);
    LOG_INFO("InEngineVersion: {0}", InEngineVersion);

    State::Instance().NVNGX_ProjectId = std::string(InProjectId);
    State::Instance().NVNGX_Engine = InEngineType;
    State::Instance().NVNGX_EngineVersion = std::string(InEngineVersion);

    return result;
}

#pragma endregion

#pragma region NVSDK_NGX_D3D11_Shutdown

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_Shutdown()
{
    shutdown = true;

    // for (auto const& [key, val] : Dx11Contexts)
    //{
    //     if (val.feature)
    //         NVSDK_NGX_D3D11_ReleaseFeature(val.feature->Handle());
    // }

    // Dx11Contexts.clear();

    D3D11Device = nullptr;
    State::Instance().currentFeature = nullptr;

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::IsDx11Inited() &&
        NVNGXProxy::D3D11_Shutdown() != nullptr)
    {
        auto result = NVNGXProxy::D3D11_Shutdown()();
        NVNGXProxy::SetDx11Inited(false);
    }

    // Unhooking and cleaning stuff causing issues during shutdown.
    // Disabled for now to check if it cause any issues
    // HooksDx::UnHook();

    shutdown = false;
    State::Instance().NvngxDx11Inited = false;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_Shutdown1(ID3D11Device* InDevice)
{
    shutdown = true;

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::IsDx11Inited() &&
        NVNGXProxy::D3D11_Shutdown1() != nullptr)
    {
        auto result = NVNGXProxy::D3D11_Shutdown1()(InDevice);
        NVNGXProxy::SetDx11Inited(false);
    }

    return NVSDK_NGX_D3D11_Shutdown();
}

#pragma endregion

#pragma region NVSDK_NGX_D3D11 Parameters

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_GetParameters(NVSDK_NGX_Parameter** OutParameters)
{
    LOG_FUNC();

    if (OutParameters == nullptr)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::D3D11_GetParameters() != nullptr)
    {
        LOG_INFO("calling NVNGXProxy::D3D11_GetParameters");

        auto result = NVNGXProxy::D3D11_GetParameters()(OutParameters);

        LOG_INFO("calling NVNGXProxy::D3D11_GetParameters result: {0:X}", (UINT) result);

        if (result == NVSDK_NGX_Result_Success)
        {
            InitNGXParameters(*OutParameters);
            return result;
        }
    }

    *OutParameters = GetNGXParameters("OptiDx11");

    LOG_DEBUG("Returning custom Opti parameters");

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_GetCapabilityParameters(NVSDK_NGX_Parameter** OutParameters)
{
    LOG_FUNC();

    if (OutParameters == nullptr)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::IsDx11Inited() && NVNGXProxy::D3D11_GetCapabilityParameters() != nullptr)
    {
        LOG_INFO("calling NVNGXProxy::D3D11_GetCapabilityParameters");

        auto result = NVNGXProxy::D3D11_GetCapabilityParameters()(OutParameters);

        LOG_INFO("calling NVNGXProxy::D3D11_GetCapabilityParameters result: {0:X}", (UINT) result);

        if (result == NVSDK_NGX_Result_Success)
        {
            InitNGXParameters(*OutParameters);
            return result;
        }
    }

    *OutParameters = GetNGXParameters("OptiDx11");

    LOG_DEBUG("Returning custom Opti parameters");

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_AllocateParameters(NVSDK_NGX_Parameter** OutParameters)
{
    LOG_FUNC();

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::D3D11_AllocateParameters() != nullptr)
    {
        LOG_INFO("calling NVNGXProxy::D3D11_AllocateParameters");

        auto result = NVNGXProxy::D3D11_AllocateParameters()(OutParameters);

        LOG_INFO("calling NVNGXProxy::D3D11_AllocateParameters result: {0:X}", (UINT) result);

        if (result == NVSDK_NGX_Result_Success)
            return result;
    }

    auto params = new NVNGX_Parameters();
    params->Name = "OptiDx11";
    *OutParameters = params;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (InParameters == nullptr)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    InitNGXParameters(InParameters);

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_DestroyParameters(NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (InParameters == nullptr)
        return NVSDK_NGX_Result_Fail;

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::D3D11_DestroyParameters() != nullptr)
    {
        LOG_INFO("calling NVNGXProxy::D3D11_DestroyParameters");
        auto result = NVNGXProxy::D3D11_DestroyParameters()(InParameters);
        LOG_INFO("calling NVNGXProxy::D3D11_DestroyParameters result: {0:X}", (UINT) result);

        return result;
    }

    delete InParameters;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                                    const NVSDK_NGX_Parameter* InParameters,
                                                                    size_t* OutSizeInBytes)
{
    LOG_WARN("-> 52428800");
    *OutSizeInBytes = 52428800;
    return NVSDK_NGX_Result_Success;
}

#pragma endregion

#pragma region NVSDK_NGX_D3D11 Feature

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_CreateFeature(ID3D11DeviceContext* InDevCtx,
                                                             NVSDK_NGX_Feature InFeatureID,
                                                             NVSDK_NGX_Parameter* InParameters,
                                                             NVSDK_NGX_Handle** OutHandle)
{
    // FeatureId check
    if (InFeatureID != NVSDK_NGX_Feature_SuperSampling && InFeatureID != NVSDK_NGX_Feature_RayReconstruction)
    {
        if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::InitDx11(D3D11Device) &&
            NVNGXProxy::D3D11_CreateFeature() != nullptr)
        {
            auto result = NVNGXProxy::D3D11_CreateFeature()(InDevCtx, InFeatureID, InParameters, OutHandle);
            LOG_INFO("D3D11_CreateFeature result for ({0}): {1:X}", (int) InFeatureID, (UINT) result);
            return result;
        }
        else
        {
            LOG_ERROR("Can't create this feature ({0})!", (int) InFeatureID);
            return NVSDK_NGX_Result_Fail;
        }
    }

    // CreateFeature
    auto handleId = IFeature::GetNextHandleId();
    LOG_INFO("HandleId: {0}", handleId);

    if (InFeatureID == NVSDK_NGX_Feature_SuperSampling)
    {
        std::string upscalerChoice = "fsr22"; // Default FSR 2.2.1

        // If original NVNGX available use DLSS as base upscaler
        if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::IsDx11Inited())
            upscalerChoice = "dlss";

        if (Config::Instance()->Dx11Upscaler.has_value())
            upscalerChoice = Config::Instance()->Dx11Upscaler.value();

        LOG_INFO("Creating new {} feature", upscalerChoice);

        Dx11Contexts[handleId] = {};

        if (!FeatureProvider_Dx11::GetFeature(upscalerChoice, handleId, InParameters, &Dx11Contexts[handleId].feature))
        {
            LOG_ERROR("Can't create {} feature", upscalerChoice);
            return NVSDK_NGX_Result_Fail;
        }
    }
    else if (InFeatureID == NVSDK_NGX_Feature_RayReconstruction)
    {
        LOG_INFO("Creating new DLSSD feature");

        Dx11Contexts[handleId] = {};

        if (!FeatureProvider_Dx11::GetFeature("dlssd", handleId, InParameters, &Dx11Contexts[handleId].feature))
        {
            LOG_ERROR("Can't create DLSSD feature");
            return NVSDK_NGX_Result_Fail;
        }
    }

    State::Instance().api = DX11;
    auto deviceContext = Dx11Contexts[handleId].feature.get();
    *OutHandle = deviceContext->Handle();

    // Always get device from context to avoid issues with Dx11 w/Dx12
    LOG_DEBUG("Get Dx11Device from InDevCtx!");
    InDevCtx->GetDevice(&D3D11Device);
    evalCounter = 0;

    if (!D3D11Device)
    {
        LOG_ERROR("Can't get Dx11Device from InDevCtx!");
        return NVSDK_NGX_Result_Fail;
    }

    D3D11Device->Release();

    State::Instance().AutoExposure.reset();

    if (deviceContext->ModuleLoaded() && deviceContext->Init(D3D11Device, InDevCtx, InParameters))
    {
        State::Instance().currentFeature = deviceContext;
        return NVSDK_NGX_Result_Success;
    }

    LOG_ERROR("CreateFeature failed");

    State::Instance().newBackend = "fsr22";
    State::Instance().changeBackend[handleId] = true;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    if (!InHandle)
        return NVSDK_NGX_Result_Success;

    auto handleId = InHandle->Id;
    if (handleId < DLSS_MOD_ID_OFFSET)
    {
        if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::D3D11_ReleaseFeature() != nullptr)
        {
            auto result = NVNGXProxy::D3D11_ReleaseFeature()(InHandle);

            if (!shutdown)
                LOG_INFO("D3D11_ReleaseFeature result for ({0}): {1:X}", handleId, (UINT) result);

            return result;
        }
        else
        {
            return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        }
    }

    if (!shutdown)
        LOG_INFO("releasing feature with id {0}", handleId);

    if (auto deviceContext = Dx11Contexts[handleId].feature.get(); deviceContext != nullptr)
    {
        if (!shutdown)
        {
            LOG_TRACE("sleeping for 500ms before reset()!");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (deviceContext == State::Instance().currentFeature)
            State::Instance().currentFeature = nullptr;

        Dx11Contexts[handleId].feature.reset();
        auto it = std::find_if(Dx11Contexts.begin(), Dx11Contexts.end(),
                               [&handleId](const auto& p) { return p.first == handleId; });
        Dx11Contexts.erase(it);

        if (!shutdown && Config::Instance()->Dx11DelayedInit.value_or_default())
        {
            LOG_TRACE("sleeping for 500ms after reset()!");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_GetFeatureRequirements(
    IDXGIAdapter* Adapter, const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
    NVSDK_NGX_FeatureRequirement* OutSupported)
{
    LOG_FUNC();

    if (FeatureDiscoveryInfo->FeatureID == NVSDK_NGX_Feature_SuperSampling)
    {
        if (OutSupported == nullptr)
            OutSupported = new NVSDK_NGX_FeatureRequirement();

        OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_Supported;
        OutSupported->MinHWArchitecture = 0;

        // Some windows 10 os version
        strcpy_s(OutSupported->MinOSVersion, "10.0.10240.16384");
        return NVSDK_NGX_Result_Success;
    }

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() == nullptr)
        NVNGXProxy::InitNVNGX();

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::D3D11_GetFeatureRequirements() != nullptr)
    {
        LOG_DEBUG("D3D11_GetFeatureRequirements for ({0})", (int) FeatureDiscoveryInfo->FeatureID);
        auto result = NVNGXProxy::D3D11_GetFeatureRequirements()(Adapter, FeatureDiscoveryInfo, OutSupported);
        LOG_DEBUG("result for D3D11_GetFeatureRequirements ({0}): {1:X}", (int) FeatureDiscoveryInfo->FeatureID,
                  (UINT) result);
        return result;
    }

    OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_AdapterUnsupported;
    return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D11_EvaluateFeature(ID3D11DeviceContext* InDevCtx,
                                                               const NVSDK_NGX_Handle* InFeatureHandle,
                                                               NVSDK_NGX_Parameter* InParameters,
                                                               PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    if (InFeatureHandle == nullptr)
    {
        LOG_DEBUG("InFeatureHandle is null");
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }
    else
    {
        LOG_DEBUG("Handle: {0}", InFeatureHandle->Id);
    }

    if (InDevCtx == nullptr)
    {
        LOG_ERROR("InDevCtx is null!!!");
        return NVSDK_NGX_Result_Fail;
    }

    auto handleId = InFeatureHandle->Id;
    if (handleId < DLSS_MOD_ID_OFFSET)
    {
        if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::D3D11_EvaluateFeature() != nullptr)
        {
            LOG_DEBUG("D3D11_EvaluateFeature for ({0})", handleId);
            auto result = NVNGXProxy::D3D11_EvaluateFeature()(InDevCtx, InFeatureHandle, InParameters, InCallback);
            LOG_INFO("D3D11_EvaluateFeature result for ({0}): {1:X}", handleId, (UINT) result);
            return result;
        }
        else
        {
            return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        }
    }

    evalCounter++;
    if (Config::Instance()->SkipFirstFrames.has_value() && evalCounter < Config::Instance()->SkipFirstFrames.value())
        return NVSDK_NGX_Result_Success;

    if (InCallback)
        LOG_INFO("callback exist");

    IFeature_Dx11* deviceContext = nullptr;
    auto activeContext = &Dx11Contexts[handleId];

    if (State::Instance().changeBackend[handleId])
    {
        FeatureProvider_Dx11::ChangeFeature(State::Instance().newBackend, D3D11Device, InDevCtx, handleId, InParameters,
                                            activeContext);

        evalCounter = 0;

        return NVSDK_NGX_Result_Success;
    }

    if (activeContext->feature == nullptr) // prevent source api name flicker when dlssg is active
    {
        State::Instance().setInputApiName = State::Instance().currentInputApiName;
    }
    else
    {
        deviceContext = activeContext->feature.get();
        State::Instance().currentFeature = deviceContext;
    }

    if (State::Instance().setInputApiName.length() == 0)
    {
        if (std::strcmp(State::Instance().currentInputApiName.c_str(), "DLSS") != 0)
            State::Instance().currentInputApiName = "DLSS";
    }
    else
    {
        if (std::strcmp(State::Instance().currentInputApiName.c_str(), State::Instance().setInputApiName.c_str()) != 0)
            State::Instance().currentInputApiName = State::Instance().setInputApiName;
    }

    State::Instance().setInputApiName.clear();

    if (deviceContext == nullptr)
    {
        LOG_DEBUG("trying to use released handle, returning NVSDK_NGX_Result_Success");
        return NVSDK_NGX_Result_Success;
    }

    UpscalerTimeDx11::UpscaleStart(InDevCtx);

    if (!deviceContext->Evaluate(InDevCtx, InParameters) && !deviceContext->IsInited() &&
        (deviceContext->Name() == "XeSS" || deviceContext->Name() == "DLSS" || deviceContext->Name() == "FSR3 w/Dx12"))
    {
        State::Instance().newBackend = "fsr22";
        State::Instance().changeBackend[handleId] = true;
    }

    UpscalerTimeDx11::UpscaleEnd(InDevCtx);

    return NVSDK_NGX_Result_Success;
}

#pragma endregion
