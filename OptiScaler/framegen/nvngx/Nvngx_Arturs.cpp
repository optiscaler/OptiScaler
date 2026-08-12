#include "pch.h"
#include "Nvngx_Arturs.h"

void Nvngx_Arturs::QueryVersions()
{
    if (!_DLSSG_D3D12_GetCapabilityParameters)
        return;

    // Main version
    HMODULE hModule;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR) _DLSSG_D3D12_GetCapabilityParameters, &hModule);

    wchar_t dllPath[MAX_PATH] = { 0 };
    GetModuleFileNameW(hModule, dllPath, MAX_PATH);

    Util::GetFileVersion(dllPath, &enablerVersion);

    // Antighosting version
    NVSDK_NGX_Parameter* tempParams {};
    _DLSSG_D3D12_GetCapabilityParameters(&tempParams);
    if (tempParams)
    {
        auto result = tempParams->Get("FrameInterpolation.GhostbusterVersionMajor", &ghostbusterVersion.major);
        tempParams->Get("FrameInterpolation.GhostbusterVersionMinor", &ghostbusterVersion.minor);

        if (result != NVSDK_NGX_Result_Success)
            LOG_WARN("Couldn't query version");
    }

    LOG_INFO("DE Ver: {}.{}.{}.{}, GB Ver: {}.{}", enablerVersion.major, enablerVersion.minor, enablerVersion.patch,
             enablerVersion.reserved, ghostbusterVersion.major, ghostbusterVersion.minor);
}

void Nvngx_Arturs::LoadLibraries()
{
    HMODULE memModule = nullptr;
    auto optiPath = Config::Instance()->MainDllPath.value();
    Util::LoadProxyLibrary(L"dlss-enabler-headless.dll", L"", optiPath, &memModule, &dll);

    if (dll == nullptr && memModule != nullptr)
        dll = memModule;

    if (dll != nullptr)
    {
        _DLSSG_D3D12_Init = (PFN_D3D12_Init) GetProcAddress(dll, "DLSSG_NVSDK_NGX_D3D12_Init");
        _DLSSG_D3D12_Init_Ext = (PFN_D3D12_Init_Ext) GetProcAddress(dll, "DLSSG_NVSDK_NGX_D3D12_Init_Ext");
        _DLSSG_D3D12_Shutdown = (PFN_D3D12_Shutdown) GetProcAddress(dll, "DLSSG_NVSDK_NGX_D3D12_Shutdown");
        _DLSSG_D3D12_Shutdown1 = (PFN_D3D12_Shutdown1) GetProcAddress(dll, "DLSSG_NVSDK_NGX_D3D12_Shutdown1");
        _DLSSG_D3D12_GetScratchBufferSize =
            (PFN_D3D12_GetScratchBufferSize) GetProcAddress(dll, "DLSSG_NVSDK_NGX_D3D12_GetScratchBufferSize");
        _DLSSG_D3D12_CreateFeature =
            (PFN_D3D12_CreateFeature) GetProcAddress(dll, "DLSSG_NVSDK_NGX_D3D12_CreateFeature");
        _DLSSG_D3D12_ReleaseFeature =
            (PFN_D3D12_ReleaseFeature) GetProcAddress(dll, "DLSSG_NVSDK_NGX_D3D12_ReleaseFeature");
        _DLSSG_D3D12_GetFeatureRequirements =
            (PFN_D3D12_GetFeatureRequirements) GetProcAddress(dll, "DLSSG_NVSDK_NGX_D3D12_GetFeatureRequirements");
        _DLSSG_D3D12_EvaluateFeature =
            (PFN_D3D12_EvaluateFeature) GetProcAddress(dll, "DLSSG_NVSDK_NGX_D3D12_EvaluateFeature");
        _DLSSG_D3D12_PopulateParameters_Impl =
            (PFN_D3D12_PopulateParameters_Impl) GetProcAddress(dll, "DLSSG_NVSDK_NGX_D3D12_PopulateParameters_Impl");

        _DLSSG_D3D12_GetCapabilityParameters =
            (PFN_D3D12_GetCapabilityParameters) GetProcAddress(dll, "NVSDK_NGX_D3D12_GetCapabilityParameters");

        QueryVersions();

        LOG_INFO("Artur's initialized");
    }
    else
    {
        LOG_WARN("Artur's enabled but cannot be loaded");
    }
}
