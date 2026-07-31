#include "pch.h"

#include <NVNGX_Parameter.h>
#include "Nvngx_FG.h"

#include "proxies/NVNGX_Proxy.h"
#include "proxies/Ntdll_Proxy.h"

#define DLSSG_MOD_ID_OFFSET 2000000

typedef void (*PFN_RefreshGlobalConfiguration)();
typedef void (*PFN_EnableDebugView)(bool enable);

static decltype(&GetFileAttributesExW) o_GetFileAttributesExW = GetFileAttributesExW;

static BOOL WINAPI hkGetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId,
                                          LPVOID lpFileInformation)
{
    if (lpFileName)
    {
        std::wstring string(lpFileName);

        // Prevent a copy by saying it wasn't found
        if (string.contains(L"nvngx"))
            return false;
    }

    return o_GetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
}

void Nvngx_FG::setSetting(const wchar_t* setting, const wchar_t* value)
{
    if (is120orNewer() && !_mfg)
    {
        SetEnvironmentVariable(setting, value);
        _refreshGlobalConfiguration();
    }
}

HMODULE Nvngx_FG::TryInitMFG()
{
    // set early so the hooks know
    _mfg = true;

    HMODULE dll = nullptr;
    if (o_GetFileAttributesExW)
    {

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_GetFileAttributesExW, hkGetFileAttributesExW);

        DetourTransactionCommit();

        HMODULE memModule = nullptr;
        auto optiPath = Config::Instance()->MainDllPath.value();
        Util::LoadProxyLibrary(L"dlss-enabler-headless.dll", L"", optiPath, &memModule, &dll);

        if (dll == nullptr && memModule != nullptr)
            dll = memModule;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourDetach(&(PVOID&) o_GetFileAttributesExW, hkGetFileAttributesExW);

        DetourTransactionCommit();
    }

    if (!dll)
        _mfg = false;

    return dll;
}

void Nvngx_FG::InitDLSSGMod_Dx12()
{
    LOG_FUNC();

    if (_dx12_inited || (Config::Instance()->FGInput.value_or_default() != FGInput::NvngxFG &&
                         Config::Instance()->FGOutput.value_or_default() != FGOutput::DLSSGWithNvngx))
    {
        return;
    }

    _dll = TryInitMFG();

    if (_dll != nullptr)
    {
        _DLSSG_D3D12_Init = (PFN_D3D12_Init) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_D3D12_Init");
        _DLSSG_D3D12_Init_Ext = (PFN_D3D12_Init_Ext) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_D3D12_Init_Ext");
        _DLSSG_D3D12_Shutdown = (PFN_D3D12_Shutdown) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_D3D12_Shutdown");
        _DLSSG_D3D12_Shutdown1 = (PFN_D3D12_Shutdown1) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_D3D12_Shutdown1");
        _DLSSG_D3D12_GetScratchBufferSize =
            (PFN_D3D12_GetScratchBufferSize) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_D3D12_GetScratchBufferSize");
        _DLSSG_D3D12_CreateFeature =
            (PFN_D3D12_CreateFeature) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_D3D12_CreateFeature");
        _DLSSG_D3D12_ReleaseFeature =
            (PFN_D3D12_ReleaseFeature) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_D3D12_ReleaseFeature");
        _DLSSG_D3D12_GetFeatureRequirements =
            (PFN_D3D12_GetFeatureRequirements) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_D3D12_GetFeatureRequirements");
        _DLSSG_D3D12_EvaluateFeature =
            (PFN_D3D12_EvaluateFeature) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_D3D12_EvaluateFeature");
        _DLSSG_D3D12_PopulateParameters_Impl =
            (PFN_D3D12_PopulateParameters_Impl) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_D3D12_PopulateParameters_Impl");

        _dx12_inited = _DLSSG_D3D12_Init != nullptr;

        LOG_INFO("DLSSG MFG Mod initialized for DX12");

        return;
    }
    else
    {
        HMODULE memModule = nullptr;
        auto optiPath = Config::Instance()->MainDllPath.value();
        Util::LoadProxyLibrary(L"dlssg_to_fsr3_amd_is_better.dll", L"", optiPath, &memModule, &_dll);

        if (_dll == nullptr && memModule != nullptr)
            _dll = memModule;
    }

    if (_dll != nullptr)
    {
        _DLSSG_D3D12_Init = (PFN_D3D12_Init) GetProcAddress(_dll, "NVSDK_NGX_D3D12_Init");
        _DLSSG_D3D12_Init_Ext = (PFN_D3D12_Init_Ext) GetProcAddress(_dll, "NVSDK_NGX_D3D12_Init_Ext");
        _DLSSG_D3D12_Shutdown = (PFN_D3D12_Shutdown) GetProcAddress(_dll, "NVSDK_NGX_D3D12_Shutdown");
        _DLSSG_D3D12_Shutdown1 = (PFN_D3D12_Shutdown1) GetProcAddress(_dll, "NVSDK_NGX_D3D12_Shutdown1");
        _DLSSG_D3D12_GetScratchBufferSize =
            (PFN_D3D12_GetScratchBufferSize) GetProcAddress(_dll, "NVSDK_NGX_D3D12_GetScratchBufferSize");
        _DLSSG_D3D12_CreateFeature = (PFN_D3D12_CreateFeature) GetProcAddress(_dll, "NVSDK_NGX_D3D12_CreateFeature");
        _DLSSG_D3D12_ReleaseFeature = (PFN_D3D12_ReleaseFeature) GetProcAddress(_dll, "NVSDK_NGX_D3D12_ReleaseFeature");
        _DLSSG_D3D12_GetFeatureRequirements =
            (PFN_D3D12_GetFeatureRequirements) GetProcAddress(_dll, "NVSDK_NGX_D3D12_GetFeatureRequirements");
        _DLSSG_D3D12_EvaluateFeature =
            (PFN_D3D12_EvaluateFeature) GetProcAddress(_dll, "NVSDK_NGX_D3D12_EvaluateFeature");
        _DLSSG_D3D12_PopulateParameters_Impl =
            (PFN_D3D12_PopulateParameters_Impl) GetProcAddress(_dll, "NVSDK_NGX_D3D12_PopulateParameters_Impl");
        _refreshGlobalConfiguration =
            (PFN_RefreshGlobalConfiguration) GetProcAddress(_dll, "RefreshGlobalConfiguration");
        _fsrDebugView = (PFN_EnableDebugView) GetProcAddress(_dll, "FSRDebugView");
        _dx12_inited = true;

        LOG_INFO("DLSSG Mod initialized for DX12");
    }
    else
    {
        LOG_INFO("DLSSG Mod enabled but cannot be loaded");
    }
}

void Nvngx_FG::InitDLSSGMod_Vulkan()
{
    LOG_FUNC();

    if (_vulkan_inited || Config::Instance()->FGInput.value_or_default() != FGInput::NvngxFG)
        return;

    // Vulkan support was removed in 4.4; in <=4.3 only the original Nukem's 2x mode is supported
    _dll = TryInitMFG();

    if (_dll != nullptr)
    {
        _DLSSG_VULKAN_Init = (PFN_VULKAN_Init) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_VULKAN_Init");
        _DLSSG_VULKAN_Init_Ext = (PFN_VULKAN_Init_Ext) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_VULKAN_Init_Ext");
        _DLSSG_VULKAN_Init_Ext2 = (PFN_VULKAN_Init_Ext2) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_VULKAN_Init_Ext2");
        _DLSSG_VULKAN_Shutdown = (PFN_VULKAN_Shutdown) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_VULKAN_Shutdown");
        _DLSSG_VULKAN_Shutdown1 = (PFN_VULKAN_Shutdown1) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_VULKAN_Shutdown1");
        _DLSSG_VULKAN_GetScratchBufferSize =
            (PFN_VULKAN_GetScratchBufferSize) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_VULKAN_GetScratchBufferSize");
        _DLSSG_VULKAN_CreateFeature =
            (PFN_VULKAN_CreateFeature) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_VULKAN_CreateFeature");
        _DLSSG_VULKAN_CreateFeature1 =
            (PFN_VULKAN_CreateFeature1) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_VULKAN_CreateFeature1");
        _DLSSG_VULKAN_ReleaseFeature =
            (PFN_VULKAN_ReleaseFeature) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_VULKAN_ReleaseFeature");
        _DLSSG_VULKAN_GetFeatureRequirements =
            (PFN_VULKAN_GetFeatureRequirements) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_VULKAN_GetFeatureRequirements");
        _DLSSG_VULKAN_EvaluateFeature =
            (PFN_VULKAN_EvaluateFeature) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_VULKAN_EvaluateFeature");
        _DLSSG_VULKAN_PopulateParameters_Impl =
            (PFN_VULKAN_PopulateParameters_Impl) GetProcAddress(_dll, "DLSSG_NVSDK_NGX_VULKAN_PopulateParameters_Impl");

        _vulkan_inited = _DLSSG_VULKAN_Init != nullptr;

        LOG_INFO("DLSSG MFG Mod initialized for Vulkan");

        return;
    }
    else
    {
        HMODULE memModule = nullptr;
        auto optiPath = Config::Instance()->MainDllPath.value();
        Util::LoadProxyLibrary(L"dlssg_to_fsr3_amd_is_better.dll", L"", optiPath, &memModule, &_dll);

        if (_dll == nullptr && memModule != nullptr)
            _dll = memModule;
    }

    if (_dll != nullptr)
    {
        _DLSSG_VULKAN_Init = (PFN_VULKAN_Init) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Init");
        _DLSSG_VULKAN_Init_Ext = (PFN_VULKAN_Init_Ext) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Init_Ext");
        _DLSSG_VULKAN_Init_Ext2 = (PFN_VULKAN_Init_Ext2) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Init_Ext2");
        _DLSSG_VULKAN_Shutdown = (PFN_VULKAN_Shutdown) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Shutdown");
        _DLSSG_VULKAN_Shutdown1 = (PFN_VULKAN_Shutdown1) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Shutdown1");
        _DLSSG_VULKAN_GetScratchBufferSize =
            (PFN_VULKAN_GetScratchBufferSize) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_GetScratchBufferSize");
        _DLSSG_VULKAN_CreateFeature = (PFN_VULKAN_CreateFeature) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_CreateFeature");
        _DLSSG_VULKAN_CreateFeature1 =
            (PFN_VULKAN_CreateFeature1) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_CreateFeature1");
        _DLSSG_VULKAN_ReleaseFeature =
            (PFN_VULKAN_ReleaseFeature) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_ReleaseFeature");
        _DLSSG_VULKAN_GetFeatureRequirements =
            (PFN_VULKAN_GetFeatureRequirements) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_GetFeatureRequirements");
        _DLSSG_VULKAN_EvaluateFeature =
            (PFN_VULKAN_EvaluateFeature) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_EvaluateFeature");
        _DLSSG_VULKAN_PopulateParameters_Impl =
            (PFN_VULKAN_PopulateParameters_Impl) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_PopulateParameters_Impl");
        _refreshGlobalConfiguration =
            (PFN_RefreshGlobalConfiguration) GetProcAddress(_dll, "RefreshGlobalConfiguration");
        _fsrDebugView = (PFN_EnableDebugView) GetProcAddress(_dll, "FSRDebugView");
        _vulkan_inited = true;

        LOG_INFO("DLSSG Mod initialized for Vulkan");
    }
    else
    {
        LOG_INFO("DLSSG Mod enabled but cannot be loaded");
    }
}

void Nvngx_FG::setDebugView(bool enabled)
{
    auto setting = L"DLSSGTOFSR3_EnableDebugOverlay";
    auto value = enabled ? L"1" : L"";
    setSetting(setting, value);
}

void Nvngx_FG::setInterpolatedOnly(bool enabled)
{
    auto setting = L"DLSSGTOFSR3_EnableInterpolatedFramesOnly";
    auto value = enabled ? L"1" : L"";
    setSetting(setting, value);
}

NVSDK_NGX_Result Nvngx_FG::D3D12_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                      ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                      NVSDK_NGX_Version InSDKVersion)
{
    if (isDx12Available())
        return _DLSSG_D3D12_Init(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::D3D12_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                          ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                          const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    if (isDx12Available())
        return _DLSSG_D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::D3D12_Shutdown()
{
    if (isDx12Available())
        return _DLSSG_D3D12_Shutdown();
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::D3D12_Shutdown1(ID3D12Device* InDevice)
{
    if (isDx12Available())
        return _DLSSG_D3D12_Shutdown1(InDevice);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                      const NVSDK_NGX_Parameter* InParameters, size_t* OutSizeInBytes)
{
    if (isDx12Available())
        return _DLSSG_D3D12_GetScratchBufferSize(InFeatureId, InParameters, OutSizeInBytes);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                               NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    if (isDx12Available())
    {
        auto result = _DLSSG_D3D12_CreateFeature(InCmdList, InFeatureID, InParameters, OutHandle);
        (*OutHandle)->Id += DLSSG_MOD_ID_OFFSET;
        return result;
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    if (isDx12Available() && InHandle->Id >= DLSSG_MOD_ID_OFFSET)
    {
        NVSDK_NGX_Handle TempHandle = { .Id = InHandle->Id - DLSSG_MOD_ID_OFFSET };

        return _DLSSG_D3D12_ReleaseFeature(&TempHandle);
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::D3D12_GetFeatureRequirements(IDXGIAdapter* Adapter,
                                                        const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                        NVSDK_NGX_FeatureRequirement* OutSupported)
{
    if (isDx12Available())
        return _DLSSG_D3D12_GetFeatureRequirements(Adapter, FeatureDiscoveryInfo, OutSupported);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                 const NVSDK_NGX_Handle* InFeatureHandle,
                                                 NVSDK_NGX_Parameter* InParameters,
                                                 PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    if (isDx12Available() && InFeatureHandle->Id >= DLSSG_MOD_ID_OFFSET)
    {
        if (!is120orNewer())
        {
            // Workaround mostly for final fantasy xvi
            uint32_t depthInverted = 0;
            float cameraNear = 0;
            float cameraFar = 0;
            InParameters->Get("DLSSG.DepthInverted", &depthInverted);
            InParameters->Get("DLSSG.CameraNear", &cameraNear);
            InParameters->Get("DLSSG.CameraFar", &cameraFar);

            if (cameraNear == 0)
            {
                if (depthInverted)
                    cameraNear = 100000.0f;
                else
                    cameraNear = 0.1f;

                InParameters->Set("DLSSG.CameraNear", cameraNear);
            }

            if (cameraFar == 0)
            {
                if (depthInverted)
                    cameraFar = 0.1f;
                else
                    cameraFar = 100000.0f;

                InParameters->Set("DLSSG.CameraFar", cameraFar);
            }
            else if (std::isinf(cameraFar))
            {
                cameraFar = 100000.0f;
                InParameters->Set("DLSSG.CameraFar", cameraFar);
            }

            // Workaround for a bug in Nukem's mod
            // if (uint32_t LowresMvec = 0; InParameters->Get("DLSSG.run_lowres_mvec_pass", &LowresMvec) ==
            // NVSDK_NGX_Result_Success && LowresMvec == 1) {
            InParameters->Set("DLSSG.MVecsSubrectWidth", 0U);
            InParameters->Set("DLSSG.MVecsSubrectHeight", 0U);
            //}
        }

        // Make a copy of the depth going to the frame generator
        // Fixes an issue with the depth being corrupted on AMD under Windows
        ID3D12Resource* dlssgDepth = nullptr;

        if (Config::Instance()->MakeDepthCopy.value_or_default())
            InParameters->Get("DLSSG.Depth", &dlssgDepth);

        if (dlssgDepth)
        {
            D3D12_RESOURCE_DESC desc = dlssgDepth->GetDesc();

            D3D12_HEAP_PROPERTIES heapProperties;
            D3D12_HEAP_FLAGS heapFlags;

            static ID3D12Resource* copiedDlssgDepth = nullptr;
            SAFE_RELEASE(copiedDlssgDepth);

            if (dlssgDepth->GetHeapProperties(&heapProperties, &heapFlags) == S_OK)
            {
                auto result = State::Instance().currentD3D12Device->CreateCommittedResource(
                    &heapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&copiedDlssgDepth));

                if (result == S_OK)
                {
                    InCmdList->CopyResource(copiedDlssgDepth, dlssgDepth);
                    InParameters->Set("DLSSG.Depth",
                                      (void*) copiedDlssgDepth); // cast to make sure it's void*, otherwise dlssg cries
                }
                else
                {
                    LOG_ERROR("Making a new resource for DLSSG Depth has failed");
                }
            }
            else
            {
                LOG_ERROR("Getting heap properties has failed");
            }
        }

        bool applyHudCutoff = Config::Instance()->FGHudCutoff.value_or_default() > 0.0f ||
                              (State::Instance().gameQuirks & GameQuirk::FSRFGHudlessMismatchFixup && !_mfg);

        uint32_t frameIndex = 1;
        InParameters->Get("DLSSG.MultiFrameIndex", &frameIndex);

        if (applyHudCutoff && frameIndex == 1)
        {
            ID3D12Resource* presentWithHud = nullptr;
            InParameters->Get("DLSSG.Backbuffer", &presentWithHud);
            auto presentWithHudState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

            ID3D12Resource* hudlessResource = nullptr;
            InParameters->Get("DLSSG.HUDLess", &hudlessResource);
            auto hudlessState = D3D12_RESOURCE_STATE_COPY_DEST;

            auto device = State::Instance().currentD3D12Device;

            if (presentWithHud && hudlessResource && device)
            {
                if (_hudCopy.get() == nullptr)
                    _hudCopy = std::make_unique<HudCopy_Dx12>("HudCopy", device);

                if (auto hudCopy = _hudCopy.get(); hudCopy && hudCopy->IsInit())
                {
                    // In Cyberprank - DLSSG has noise issues, FSR FG has noise + vignetting
                    // In Death Stranding 2 - DLSSG has wrong colormapping it seems, FSR FG is fine
                    const bool isCyberpunk = State::Instance().gameQuirks[GameQuirk::CyberpunkHudlessState];
                    float hudDetectionThreshold = 0.03f;

                    if (isCyberpunk && State::Instance().activeFgInput != FGInput::FSRFG)
                        hudDetectionThreshold = 0.01f;

                    if (Config::Instance()->FGHudCutoff.value_or_default() > 0.0f)
                        hudDetectionThreshold = Config::Instance()->FGHudCutoff.value_or_default() / 10.0f;

                    hudCopy->Dispatch(InCmdList, hudlessResource, presentWithHud, hudlessState, presentWithHudState,
                                      hudDetectionThreshold);
                }
            }
            else
            {
                LOG_WARN("Couldn't run hudless fixup");
            }
        }

        uint32_t showDebug = Config::Instance()->FGDLSSGShowDebug.value_or_default();
        uint32_t flags = Config::Instance()->FGDLSSGDispatchFlags.value_or_default();
        bool disableHudless = Config::Instance()->FGDLSSGDisableHudless.value_or_default();

        InParameters->Set("DLSSG.ShowDebug", showDebug);
        InParameters->Set("DLSSG.DispatchFlags", flags);

        if (disableHudless)
            InParameters->Set("DLSSG.HUDLess", (void*) nullptr);

        NVSDK_NGX_Handle TempHandle = { .Id = InFeatureHandle->Id - DLSSG_MOD_ID_OFFSET };
        return _DLSSG_D3D12_EvaluateFeature(InCmdList, &TempHandle, InParameters, InCallback);
    }

    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    if (isDx12Available())
        return _DLSSG_D3D12_PopulateParameters_Impl(InParameters);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                       VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                       PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                       const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo, NVSDK_NGX_Version InSDKVersion)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_Init(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice, InGIPA, InGDPA,
                                  InFeatureInfo, InSDKVersion);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                           VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                           NVSDK_NGX_Version InSDKVersion,
                                           const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_Init_Ext(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice, InSDKVersion,
                                      InFeatureInfo);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_Init_Ext2(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                            VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                            PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                            NVSDK_NGX_Version InSDKVersion,
                                            const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_Init_Ext2(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice, InGIPA,
                                       InGDPA, InSDKVersion, InFeatureInfo);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_Shutdown()
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_Shutdown();
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_Shutdown1(VkDevice InDevice)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_Shutdown1(InDevice);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                       const NVSDK_NGX_Parameter* InParameters, size_t* OutSizeInBytes)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_GetScratchBufferSize(InFeatureId, InParameters, OutSizeInBytes);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_CreateFeature(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Feature InFeatureID,
                                                NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    if (isVulkanAvailable())
    {
        auto result = _DLSSG_VULKAN_CreateFeature(InCmdBuffer, InFeatureID, InParameters, OutHandle);
        (*OutHandle)->Id += DLSSG_MOD_ID_OFFSET;
        return result;
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_CreateFeature1(VkDevice InDevice, VkCommandBuffer InCmdList,
                                                 NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter* InParameters,
                                                 NVSDK_NGX_Handle** OutHandle)
{
    if (isVulkanAvailable())
    {
        auto result = _DLSSG_VULKAN_CreateFeature1(InDevice, InCmdList, InFeatureID, InParameters, OutHandle);
        (*OutHandle)->Id += DLSSG_MOD_ID_OFFSET;
        return result;
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    if (isVulkanAvailable() && InHandle->Id >= DLSSG_MOD_ID_OFFSET)
    {
        NVSDK_NGX_Handle TempHandle = { .Id = InHandle->Id - DLSSG_MOD_ID_OFFSET };
        return _DLSSG_VULKAN_ReleaseFeature(&TempHandle);
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_GetFeatureRequirements(const VkInstance Instance,
                                                         const VkPhysicalDevice PhysicalDevice,
                                                         const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                         NVSDK_NGX_FeatureRequirement* OutSupported)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_GetFeatureRequirements(Instance, PhysicalDevice, FeatureDiscoveryInfo, OutSupported);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_EvaluateFeature(VkCommandBuffer InCmdList, const NVSDK_NGX_Handle* InFeatureHandle,
                                                  NVSDK_NGX_Parameter* InParameters,
                                                  PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    if (isVulkanAvailable() && InFeatureHandle->Id >= DLSSG_MOD_ID_OFFSET)
    {
        if (!is120orNewer())
        {
            // Workaround mostly for final fantasy xvi, keeping it from DX12
            uint32_t depthInverted = 0;
            float cameraNear = 0;
            float cameraFar = 0;
            InParameters->Get("DLSSG.DepthInverted", &depthInverted);
            InParameters->Get("DLSSG.CameraNear", &cameraNear);
            InParameters->Get("DLSSG.CameraFar", &cameraFar);

            if (cameraNear == 0)
            {
                if (depthInverted)
                    cameraNear = 100000.0f;
                else
                    cameraNear = 0.1f;

                InParameters->Set("DLSSG.CameraNear", cameraNear);
            }

            if (cameraFar == 0)
            {
                if (depthInverted)
                    cameraFar = 0.1f;
                else
                    cameraFar = 100000.0f;

                InParameters->Set("DLSSG.CameraFar", cameraFar);
            }
            else if (std::isinf(cameraFar))
            {
                cameraFar = 10000;
                InParameters->Set("DLSSG.CameraFar", cameraFar);
            }

            // Workaround for a bug in Nukem's mod, keeping it from DX12
            // if (uint32_t LowresMvec = 0; InParameters->Get("DLSSG.run_lowres_mvec_pass", &LowresMvec) ==
            // NVSDK_NGX_Result_Success && LowresMvec == 1) {
            InParameters->Set("DLSSG.MVecsSubrectWidth", 0U);
            InParameters->Set("DLSSG.MVecsSubrectHeight", 0U);
            //}
        }

        NVSDK_NGX_Handle TempHandle = { .Id = InFeatureHandle->Id - DLSSG_MOD_ID_OFFSET };
        return _DLSSG_VULKAN_EvaluateFeature(InCmdList, &TempHandle, InParameters, InCallback);
    }

    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_FG::VULKAN_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_PopulateParameters_Impl(InParameters);
    return NVSDK_NGX_Result_Fail;
}
