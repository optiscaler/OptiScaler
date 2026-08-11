#include "pch.h"
#include "Nvngx_Arturs.h"

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

        LOG_INFO("Artur's initialized");
    }
    else
    {
        LOG_WARN("Artur's enabled but cannot be loaded");
    }
}
