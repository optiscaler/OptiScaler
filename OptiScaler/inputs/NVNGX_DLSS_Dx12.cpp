#include "pch.h"
#include "Util.h"
#include "Config.h"

#include "NVNGX_DLSS.h"
#include "NVNGX_Parameter.h"
#include "proxies/NVNGX_Proxy.h"

#include <upscalers/FeatureProvider_Dx12.h>
#include "upscalers/dlss/DLSSFeature_Dx12.h"

#include "FG/DLSSG_Mod.h"
#include "FG/FSR3_Dx12_FG.h"
#include "FG/Upscaler_Inputs_Dx12.h"

#include <upscaler_time/UpscalerTime_Dx12.h>

#include <hooks/D3D12_Hooks.h>

#include <dxgi1_4.h>
#include <shared_mutex>
#include "detours/detours.h"
#include <ankerl/unordered_dense.h>

static ankerl::unordered_dense::map<unsigned int, ContextData<IFeature_Dx12>> Dx12Contexts;

static ID3D12Device* D3D12Device = nullptr;
static int evalCounter = 0;
static std::wstring appDataPath = L".";
static bool shutdown = false;
static bool _skipInit = false;
static wchar_t const** paths;

class ScopedInitDx12
{
  private:
    bool previousState;

  public:
    ScopedInitDx12()
    {
        previousState = _skipInit;
        _skipInit = true;
    }

    ~ScopedInitDx12() { _skipInit = previousState; }
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

#pragma region DLSS Init Calls

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Init_Ext(unsigned long long InApplicationId,
                                                        const wchar_t* InApplicationDataPath, ID3D12Device* InDevice,
                                                        NVSDK_NGX_Version InSDKVersion,
                                                        const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    LOG_FUNC();

    NVSDK_NGX_FeatureCommonInfo localFeatureInfo = {};

    if (InFeatureInfo != nullptr)
        std::memcpy(&localFeatureInfo, InFeatureInfo, sizeof(NVSDK_NGX_FeatureCommonInfo));

    if (!_skipInit)
        UpdateInitPaths(&localFeatureInfo);

    State::Instance().NVNGX_ApplicationId = InApplicationId;
    State::Instance().NVNGX_ApplicationDataPath = std::wstring(InApplicationDataPath);
    State::Instance().NVNGX_Version = InSDKVersion;
    State::Instance().NVNGX_FeatureInfo = &localFeatureInfo;
    State::Instance().NVNGX_Version = InSDKVersion;

    if (Config::Instance()->DLSSEnabled.value_or_default() && !_skipInit)
    {
        if (Config::Instance()->UseGenericAppIdWithDlss.value_or_default())
            InApplicationId = app_id_override;

        if (NVNGXProxy::NVNGXModule() == nullptr)
            NVNGXProxy::InitNVNGX();

        if (NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D12_Init_Ext() != nullptr)
        {
            LOG_INFO("calling NVNGXProxy::D3D12_Init_Ext");

            auto result = NVNGXProxy::D3D12_Init_Ext()(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion,
                                                       &localFeatureInfo);
            LOG_INFO("calling NVNGXProxy::D3D12_Init_Ext result: {0:X}", (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
                NVNGXProxy::SetDx12Inited(true);
        }
        else
        {
            LOG_WARN("NVNGXProxy::NVNGXModule or NVNGXProxy::D3D12_Init_Ext is nullptr!");
        }
    }

    if (InFeatureInfo != nullptr && InSDKVersion > 0x0000013)
        State::Instance().NVNGX_Logger = InFeatureInfo->LoggingInfo;

    if (State::Instance().NvngxDx12Inited && InDevice == D3D12Device)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    if (State::Instance().activeFgInput == FGInput::Nukems)
    {
        DLSSGMod::InitDLSSGMod_Dx12();
        DLSSGMod::D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, &localFeatureInfo);
    }

    LOG_INFO("AppId: {0}", InApplicationId);
    LOG_INFO("SDK: {0:x}", (unsigned int) InSDKVersion);
    appDataPath = std::wstring(InApplicationDataPath);

    LOG_INFO("InApplicationDataPath {0}", wstring_to_string(appDataPath));

    D3D12Device = InDevice;
    State::Instance().currentD3D12Device = InDevice;
    D3D12Hooks::HookDevice(InDevice);

    if (State::Instance().workingMode != WorkingMode::Nvngx)
    {
        UpscalerTimeDx12::Init(InDevice);
    }

    State::Instance().NvngxDx12Inited = true;

    UpscalerInputsDx12::Init(InDevice);

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Init(unsigned long long InApplicationId,
                                                    const wchar_t* InApplicationDataPath, ID3D12Device* InDevice,
                                                    const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                                    NVSDK_NGX_Version InSDKVersion)
{
    LOG_FUNC();

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

        if (NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D12_Init() != nullptr)
        {
            LOG_INFO("calling NVNGXProxy::D3D12_Init");

            auto result = NVNGXProxy::D3D12_Init()(InApplicationId, InApplicationDataPath, InDevice, &localFeatureInfo,
                                                   InSDKVersion);

            LOG_INFO("calling NVNGXProxy::D3D12_Init result: {0:X}", (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
                NVNGXProxy::SetDx12Inited(true);
        }
    }

    if (State::Instance().NvngxDx12Inited && InDevice == D3D12Device)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    // if (State::Instance().activeFgInput == FGInput::Nukems)
    //{
    //     DLSSGMod::InitDLSSGMod_Dx12();
    //     DLSSGMod::D3D12_Init(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
    // }

    ScopedInitDx12 scopedInit {};
    auto result =
        NVSDK_NGX_D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, &localFeatureInfo);
    LOG_DEBUG("was called NVSDK_NGX_D3D12_Init_Ext");
    return result;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Init_ProjectID(const char* InProjectId,
                                                              NVSDK_NGX_EngineType InEngineType,
                                                              const char* InEngineVersion,
                                                              const wchar_t* InApplicationDataPath,
                                                              ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                                              const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    LOG_FUNC();

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

        if (NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D12_Init_ProjectID() != nullptr)
        {
            LOG_INFO("calling NVNGXProxy::D3D12_Init_ProjectID");

            auto result =
                NVNGXProxy::D3D12_Init_ProjectID()(InProjectId, InEngineType, InEngineVersion, InApplicationDataPath,
                                                   InDevice, InSDKVersion, &localFeatureInfo);

            LOG_INFO("calling NVNGXProxy::D3D12_Init_ProjectID result: {0:X}", (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
                NVNGXProxy::SetDx12Inited(true);
        }
    }

    LOG_INFO("InProjectId: {0}", InProjectId);
    LOG_INFO("InEngineType: {0}", (int) InEngineType);
    LOG_INFO("InEngineVersion: {0}", InEngineVersion);

    State::Instance().NVNGX_ProjectId = std::string(InProjectId);
    State::Instance().NVNGX_Engine = InEngineType;
    State::Instance().NVNGX_EngineVersion = std::string(InEngineVersion);

    if (State::Instance().NvngxDx12Inited && InDevice == D3D12Device)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    ScopedInitDx12 scopedInit {};
    auto result = NVSDK_NGX_D3D12_Init_Ext(0x1337, InApplicationDataPath, InDevice, InSDKVersion, &localFeatureInfo);
    return result;
}

// Not sure about this one, original nvngx does not export this method
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Init_with_ProjectID(
    const char* InProjectId, NVSDK_NGX_EngineType InEngineType, const char* InEngineVersion,
    const wchar_t* InApplicationDataPath, ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
    NVSDK_NGX_Version InSDKVersion)
{
    LOG_FUNC();

    LOG_INFO("InProjectId: {0}", InProjectId);
    LOG_INFO("InEngineType: {0}", (int) InEngineType);
    LOG_INFO("InEngineVersion: {0}", InEngineVersion);

    State::Instance().NVNGX_ProjectId = std::string(InProjectId);
    State::Instance().NVNGX_Engine = InEngineType;
    State::Instance().NVNGX_EngineVersion = std::string(InEngineVersion);

    if (State::Instance().NvngxDx12Inited && InDevice == D3D12Device)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    auto result = NVSDK_NGX_D3D12_Init_Ext(0x1337, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);

    return result;
}

#pragma endregion

#pragma region DLSS Shutdown Calls

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Shutdown(void)
{
    shutdown = true;
    State::Instance().NvngxDx12Inited = false;

    D3D12Device = nullptr;

    State::Instance().currentFeature = nullptr;

    // Unhooking and cleaning stuff causing issues during shutdown.
    // Disabled for now to check if it cause any issues
    // UnhookAll();
    DLSSFeatureDx12::Shutdown(D3D12Device);

    // Added `&& !State::Instance().isShuttingDown` hack for crash on exit
    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::IsDx12Inited() &&
        NVNGXProxy::D3D12_Shutdown() != nullptr && !State::Instance().isShuttingDown)
    {
        auto result = NVNGXProxy::D3D12_Shutdown()();
        NVNGXProxy::SetDx12Inited(false);
    }

    // Unhooking and cleaning stuff causing issues during shutdown.
    // Disabled for now to check if it cause any issues
    // HooksDx::UnHook();

    // Disabled to prevent crash
    if (State::Instance().currentFG != nullptr && State::Instance().activeFgInput == FGInput::Upscaler)
    {
        if (State::Instance().isShuttingDown)
            State::Instance().currentFG->Shutdown();
        else
            State::Instance().currentFG->DestroyFGContext();

        State::Instance().ClearCapturedHudlesses = true;
    }

    shutdown = false;

    if (State::Instance().activeFgInput == FGInput::Nukems)
        DLSSGMod::D3D12_Shutdown();

    State::Instance().NvngxDx12Inited = false;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Shutdown1(ID3D12Device* InDevice)
{
    shutdown = true;
    State::Instance().NvngxDx12Inited = false;

    if (State::Instance().activeFgInput == FGInput::Nukems)
        DLSSGMod::D3D12_Shutdown1(InDevice);

    // Added `&& !State::Instance().isShuttingDown` hack for crash on exit
    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::IsDx12Inited() &&
        NVNGXProxy::D3D12_Shutdown1() != nullptr && !State::Instance().isShuttingDown)
    {
        auto result = NVNGXProxy::D3D12_Shutdown1()(InDevice);
        NVNGXProxy::SetDx12Inited(false);
    }

    return NVSDK_NGX_D3D12_Shutdown();
}

#pragma endregion

#pragma region DLSS Parameter Calls

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetParameters(NVSDK_NGX_Parameter** OutParameters)
{
    LOG_FUNC();

    if (OutParameters == nullptr)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::D3D12_GetParameters() != nullptr)
    {
        LOG_INFO("calling NVNGXProxy::D3D12_GetParameters");
        auto result = NVNGXProxy::D3D12_GetParameters()(OutParameters);
        LOG_INFO("calling NVNGXProxy::D3D12_GetParameters result: {0:X}, ptr: {1:X}", (UINT) result,
                 (UINT64) *OutParameters);

        if (result == NVSDK_NGX_Result_Success)
        {
            InitNGXParameters(*OutParameters);
            return NVSDK_NGX_Result_Success;
        }
    }

    *OutParameters = GetNGXParameters("OptiDx12");

    LOG_DEBUG("Returning custom Opti parameters");

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetCapabilityParameters(NVSDK_NGX_Parameter** OutParameters)
{
    LOG_FUNC();

    if (OutParameters == nullptr)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::IsDx12Inited() && NVNGXProxy::D3D12_GetCapabilityParameters() != nullptr)
    {
        LOG_INFO("calling NVNGXProxy::D3D12_GetCapabilityParameters");
        auto result = NVNGXProxy::D3D12_GetCapabilityParameters()(OutParameters);
        LOG_INFO("calling NVNGXProxy::D3D12_GetCapabilityParameters result: {0:X}, ptr: {1:X}", (UINT) result,
                 (UINT64) *OutParameters);

        if (result == NVSDK_NGX_Result_Success)
        {
            InitNGXParameters(*OutParameters);
            return NVSDK_NGX_Result_Success;
        }
    }

    *OutParameters = GetNGXParameters("OptiDx12");

    LOG_DEBUG("Returning custom Opti parameters");

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_AllocateParameters(NVSDK_NGX_Parameter** OutParameters)
{
    LOG_FUNC();

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::D3D12_AllocateParameters() != nullptr)
    {
        LOG_INFO("calling NVNGXProxy::D3D12_AllocateParameters");
        auto result = NVNGXProxy::D3D12_AllocateParameters()(OutParameters);
        LOG_INFO("calling NVNGXProxy::D3D12_AllocateParameters result: {0:X}, ptr: {1:X}", (UINT) result,
                 (UINT64) *OutParameters);

        if (result == NVSDK_NGX_Result_Success)
            return result;
    }

    auto params = new NVNGX_Parameters();
    params->Name = "OptiDx12";
    *OutParameters = params;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (InParameters == nullptr)
        return NVSDK_NGX_Result_Fail;

    InitNGXParameters(InParameters);

    if (State::Instance().activeFgInput == FGInput::Nukems)
        DLSSGMod::D3D12_PopulateParameters_Impl(InParameters);

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_DestroyParameters(NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (InParameters == nullptr)
        return NVSDK_NGX_Result_Fail;

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::D3D12_DestroyParameters() != nullptr)
    {
        LOG_INFO("calling NVNGXProxy::D3D12_DestroyParameters");
        auto result = NVNGXProxy::D3D12_DestroyParameters()(InParameters);
        LOG_INFO("calling NVNGXProxy::D3D12_DestroyParameters result: {0:X}", (UINT) result);
        UpscalerInputsDx12::Reset();

        return NVSDK_NGX_Result_Success;
    }

    delete InParameters;
    return NVSDK_NGX_Result_Success;
}

#pragma endregion

#pragma region DLSS Feature Calls

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                             NVSDK_NGX_Feature InFeatureID,
                                                             NVSDK_NGX_Parameter* InParameters,
                                                             NVSDK_NGX_Handle** OutHandle)
{
    LOG_FUNC();

    if (State::Instance().activeFgInput == FGInput::Nukems && DLSSGMod::isDx12Available() &&
        InFeatureID == NVSDK_NGX_Feature_FrameGeneration)
    {
        auto result = DLSSGMod::D3D12_CreateFeature(InCmdList, InFeatureID, InParameters, OutHandle);
        LOG_INFO("Creating new modded DLSSG feature with HandleId: {0}", (*OutHandle)->Id);
        return result;
    }
    else if (InFeatureID != NVSDK_NGX_Feature_SuperSampling && InFeatureID != NVSDK_NGX_Feature_RayReconstruction)
    {
        if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::InitDx12(D3D12Device) &&
            NVNGXProxy::D3D12_CreateFeature() != nullptr)
        {
            LOG_INFO("calling D3D12_CreateFeature for ({0})", (int) InFeatureID);
            auto result = NVNGXProxy::D3D12_CreateFeature()(InCmdList, InFeatureID, InParameters, OutHandle);

            if (result == NVSDK_NGX_Result_Success)
            {
                LOG_INFO("D3D12_CreateFeature HandleId for ({0}): {1:X}", (int) InFeatureID, (*OutHandle)->Id);
            }
            else
            {
                LOG_INFO("D3D12_CreateFeature result for ({0}): {1:X}", (int) InFeatureID, (UINT) result);
            }

            return result;
        }
        else
        {
            LOG_ERROR("Can't create this feature ({0})!", (int) InFeatureID);
            return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
        }
    }

    // Create feature
    State::Instance().api = DX12;
    auto handleId = IFeature::GetNextHandleId();
    LOG_INFO("HandleId: {0}", handleId);

    // Root signature restore
    if (Config::Instance()->RestoreComputeSignature.value_or_default() ||
        Config::Instance()->RestoreGraphicSignature.value_or_default())
    {
        D3D12Hooks::SetRootSignatureTracking(false);
        D3D12Hooks::HookToCommandListLate(InCmdList);
    }

    if (InFeatureID == NVSDK_NGX_Feature_SuperSampling)
    {
        std::string upscalerChoice = "xess"; // Default XeSS

        // If original NVNGX available use DLSS as base upscaler
        if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::IsDx12Inited())
            upscalerChoice = "dlss";

        if (Config::Instance()->Dx12Upscaler.has_value())
            upscalerChoice = Config::Instance()->Dx12Upscaler.value();

        LOG_INFO("Creating new {} upscaler", upscalerChoice);

        Dx12Contexts[handleId] = {};

        if (!FeatureProvider_Dx12::GetFeature(upscalerChoice, handleId, InParameters, &Dx12Contexts[handleId].feature))
        {
            LOG_ERROR("Upscaler can't created");
            return NVSDK_NGX_Result_Fail;
        }
    }
    else if (InFeatureID == NVSDK_NGX_Feature_RayReconstruction)
    {
        LOG_INFO("creating new DLSSD feature");

        Dx12Contexts[handleId] = {};

        if (!FeatureProvider_Dx12::GetFeature("dlssd", handleId, InParameters, &Dx12Contexts[handleId].feature))
        {
            LOG_ERROR("DLSSD can't created");
            return NVSDK_NGX_Result_Fail;
        }
    }

    auto deviceContext = Dx12Contexts[handleId].feature.get();

    if (*OutHandle == nullptr)
        *OutHandle = new NVSDK_NGX_Handle { handleId };
    else
        (*OutHandle)->Id = handleId;

#pragma region Check for Dx12Device Device

    if (!D3D12Device)
    {
        LOG_DEBUG("Get D3d12 device from InCmdList!");
        auto deviceResult = InCmdList->GetDevice(IID_PPV_ARGS(&D3D12Device));

        if (deviceResult != S_OK || !D3D12Device)
        {
            LOG_ERROR("Can't get Dx12Device from InCmdList!");
            return NVSDK_NGX_Result_Fail;
        }
    }

#pragma endregion

    State::Instance().AutoExposure.reset();

    if (deviceContext->Init(D3D12Device, InCmdList, InParameters))
    {
        State::Instance().currentFeature = deviceContext;
        evalCounter = 0;

        UpscalerInputsDx12::Reset();
    }
    else
    {
        LOG_ERROR("CreateFeature failed, returning to FSR 2.1.2 upscaler");
        State::Instance().newBackend = "fsr21";
        State::Instance().changeBackend[handleId] = true;
    }

    // Root signature restore
    D3D12Hooks::RestoreRoot(InCmdList);
    D3D12Hooks::SetRootSignatureTracking(true);

    State::Instance().FGchanged = true;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    LOG_FUNC();

    if (!InHandle)
        return NVSDK_NGX_Result_Success;

    auto handleId = InHandle->Id;

    State::Instance().FGchanged = true;
    if (State::Instance().currentFG != nullptr && State::Instance().activeFgInput == FGInput::Upscaler)
    {
        State::Instance().currentFG->DestroyFGContext();
        State::Instance().ClearCapturedHudlesses = true;
        UpscalerInputsDx12::Reset();
    }

    if (!shutdown)
        LOG_INFO("releasing feature with id {0}", handleId);

    if (handleId < DLSS_MOD_ID_OFFSET)
    {
        if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::D3D12_ReleaseFeature() != nullptr)
        {
            if (!shutdown)
                LOG_INFO("calling D3D12_ReleaseFeature for ({0})", handleId);

            auto result = NVNGXProxy::D3D12_ReleaseFeature()(InHandle);

            if (!shutdown)
                LOG_INFO("D3D12_ReleaseFeature result for ({0}): {1:X}", handleId, (UINT) result);

            return result;
        }
        else
        {
            if (!shutdown)
                LOG_INFO("D3D12_ReleaseFeature not available for ({0})", handleId);

            return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        }
    }
    else if (State::Instance().activeFgInput == FGInput::Nukems && handleId >= DLSSG_MOD_ID_OFFSET)
    {
        LOG_INFO("D3D12_ReleaseFeature modded DLSSG with HandleId: {0}", handleId);
        return DLSSGMod::D3D12_ReleaseFeature(InHandle);
    }

    if (auto deviceContext = Dx12Contexts[handleId].feature.get(); deviceContext != nullptr)
    {
        if (deviceContext == State::Instance().currentFeature)
            State::Instance().currentFeature = nullptr;

        Dx12Contexts[handleId].feature.reset();
        auto it = std::find_if(Dx12Contexts.begin(), Dx12Contexts.end(),
                               [&handleId](const auto& p) { return p.first == handleId; });
        Dx12Contexts.erase(it);
    }
    else
    {
        if (!shutdown)
            LOG_ERROR("can't release feature with id {0}!", handleId);
    }

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetFeatureRequirements(
    IDXGIAdapter* Adapter, const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
    NVSDK_NGX_FeatureRequirement* OutSupported)
{
    LOG_DEBUG("for ({0})", (int) FeatureDiscoveryInfo->FeatureID);

    if (State::Instance().activeFgInput == FGInput::Nukems)
        DLSSGMod::InitDLSSGMod_Dx12();

    if (FeatureDiscoveryInfo->FeatureID == NVSDK_NGX_Feature_SuperSampling ||
        (FeatureDiscoveryInfo->FeatureID == NVSDK_NGX_Feature_FrameGeneration &&
         ((DLSSGMod::isDx12Available() && Config::Instance()->FGInput == FGInput::Nukems) ||
          Config::Instance()->FGInput == FGInput::DLSSG)))
    {
        if (OutSupported == nullptr)
            OutSupported = new NVSDK_NGX_FeatureRequirement();

        OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_Supported;
        OutSupported->MinHWArchitecture = 0;

        // Some old windows 10 os version
        strcpy_s(OutSupported->MinOSVersion, "10.0.10240.16384");
        return NVSDK_NGX_Result_Success;
    }

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() == nullptr)
        NVNGXProxy::InitNVNGX();

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::D3D12_GetFeatureRequirements() != nullptr)
    {
        LOG_DEBUG("D3D12_GetFeatureRequirements for ({0})", (int) FeatureDiscoveryInfo->FeatureID);
        auto result = NVNGXProxy::D3D12_GetFeatureRequirements()(Adapter, FeatureDiscoveryInfo, OutSupported);
        LOG_DEBUG("D3D12_GetFeatureRequirements result for ({0}): {1:X}", (int) FeatureDiscoveryInfo->FeatureID,
                  (UINT) result);

        return result;
    }
    else
    {
        LOG_DEBUG("D3D12_GetFeatureRequirements not available for ({0})", (int) FeatureDiscoveryInfo->FeatureID);
    }

    OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_AdapterUnsupported;
    return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                               const NVSDK_NGX_Handle* InFeatureHandle,
                                                               NVSDK_NGX_Parameter* InParameters,
                                                               PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    if (InFeatureHandle == nullptr)
    {
        LOG_DEBUG("InFeatureHandle is null");
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    if (InCmdList == nullptr)
    {
        LOG_ERROR("InCmdList is null!!!");
        return NVSDK_NGX_Result_Fail;
    }

    LOG_DEBUG("Handle: {}, CmdList: {:X}", InFeatureHandle->Id, (size_t) InCmdList);
    auto handleId = InFeatureHandle->Id;

    if (handleId < DLSS_MOD_ID_OFFSET)
    {
        if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::D3D12_EvaluateFeature() != nullptr)
        {
            LOG_DEBUG("D3D12_EvaluateFeature for ({0})", handleId);
            auto result = NVNGXProxy::D3D12_EvaluateFeature()(InCmdList, InFeatureHandle, InParameters, InCallback);
            LOG_DEBUG("D3D12_EvaluateFeature result for ({0}): {1:X}", handleId, (UINT) result);
            return result;
        }
        else
        {
            LOG_DEBUG("D3D12_EvaluateFeature not avaliable for ({0})", handleId);
            return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        }
    }
    else if (State::Instance().activeFgInput == FGInput::Nukems && handleId >= DLSSG_MOD_ID_OFFSET)
    {
        return DLSSGMod::D3D12_EvaluateFeature(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    if (!Dx12Contexts.contains(handleId))
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;

    auto deviceContext = &Dx12Contexts[handleId];

    if (deviceContext->feature == nullptr) // prevent source api name flicker when dlssg is active
        State::Instance().setInputApiName = State::Instance().currentInputApiName;

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

    evalCounter++;
    if (Config::Instance()->SkipFirstFrames.has_value() && evalCounter < Config::Instance()->SkipFirstFrames.value())
        return NVSDK_NGX_Result_Success;

    if ((Config::Instance()->RestoreComputeSignature.value_or_default() ||
         Config::Instance()->RestoreGraphicSignature.value_or_default()) &&
        !D3D12Hooks::CanRestoreRootSignature(InCmdList))
    {
        LOG_DEBUG("Skipping upscaling because can't restore root signature");
        return NVSDK_NGX_Result_Success;
    }

    if (InCallback)
        LOG_INFO("callback exist");

    if (deviceContext->feature)
    {
        auto* feature = deviceContext->feature.get();

        // FSR 3.1 supports upscaleSize that doesn't need reinit to change output resolution
        if (!(feature->Name().starts_with("FSR") && feature->Version() >= feature_version { 3, 1, 0 }) &&
            feature->UpdateOutputResolution(InParameters))
            State::Instance().changeBackend[handleId] = true;
    }

    // Change backend
    if (State::Instance().changeBackend[handleId])
    {
        UpscalerInputsDx12::Reset();
        D3D12Hooks::SetRootSignatureTracking(true);

        FeatureProvider_Dx12::ChangeFeature(State::Instance().newBackend, D3D12Device, InCmdList, handleId,
                                            InParameters, deviceContext);

        evalCounter = 0;

        return NVSDK_NGX_Result_Success;
    }

    if (!deviceContext->feature->IsInited() && Config::Instance()->Dx12Upscaler.value_or_default() != "fsr21")
    {
        LOG_WARN("InCmdList {0} is not inited, falling back to FSR 2.1.2", deviceContext->feature->Name());
        State::Instance().newBackend = "fsr21";
        State::Instance().changeBackend[handleId] = true;
        return NVSDK_NGX_Result_Success;
    }

    State::Instance().currentFeature = deviceContext->feature.get();

    // Root signature restore
    if (Config::Instance()->RestoreComputeSignature.value_or_default() ||
        Config::Instance()->RestoreGraphicSignature.value_or_default())
    {
        D3D12Hooks::SetRootSignatureTracking(false);
        D3D12Hooks::HookToCommandListLate(InCmdList);
    }

    UpscalerInputsDx12::UpscaleStart(InCmdList, InParameters, deviceContext->feature.get());
    FSR3FG::SetUpscalerInputs(InCmdList, InParameters, deviceContext->feature.get());

    // Record the first timestamp
    if (State::Instance().workingMode != WorkingMode::Nvngx)
        UpscalerTimeDx12::UpscaleStart(InCmdList);

    auto evalResult = false;

    // Run upscaler
    {
        // Resource tracking
        UpscalerInputsDx12::UpscaleEnd(InCmdList, InParameters, deviceContext->feature.get());

        ScopedSkipHeapCapture skipHeapCapture {};
        evalResult = deviceContext->feature->Evaluate(InCmdList, InParameters);
    }

    if (evalResult)
    {
        // Upscaler time calc
        // Record the second timestamp
        if (State::Instance().workingMode != WorkingMode::Nvngx)
            UpscalerTimeDx12::UpscaleEnd(InCmdList);
    }

    NVSDK_NGX_Result methodResult = evalResult ? NVSDK_NGX_Result_Success : NVSDK_NGX_Result_Fail;

    // Root signature restore
    D3D12Hooks::RestoreRoot(InCmdList);
    D3D12Hooks::SetRootSignatureTracking(true);

    LOG_DEBUG("Upscaling done: {}", evalResult);

    return methodResult;
}

#pragma endregion

#pragma region DLSS Buffer Size Call

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                                    const NVSDK_NGX_Parameter* InParameters,
                                                                    size_t* OutSizeInBytes)
{
    if (OutSizeInBytes == nullptr)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (State::Instance().activeFgInput == FGInput::Nukems && DLSSGMod::isDx12Available() &&
        InFeatureId == NVSDK_NGX_Feature_FrameGeneration)
    {
        return DLSSGMod::D3D12_GetScratchBufferSize(InFeatureId, InParameters, OutSizeInBytes);
    }

    LOG_WARN("-> 52428800");
    *OutSizeInBytes = 52428800;
    return NVSDK_NGX_Result_Success;
}

#pragma endregion
