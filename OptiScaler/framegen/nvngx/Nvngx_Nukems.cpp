#include "pch.h"
#include "Nvngx_Nukems.h"

void Nvngx_Nukems::LoadLibraries()
{
    HMODULE memModule = nullptr;
    auto& optiPath = Config::Instance()->MainDllPath.value();
    Util::LoadProxyLibrary(L"dlssg_to_fsr3_amd_is_better.dll", L"", optiPath, &memModule, &dll);

    if (dll == nullptr && memModule != nullptr)
        dll = memModule;

    if (dll != nullptr)
    {
        _refreshGlobalConfiguration =
            (PFN_RefreshGlobalConfiguration) GetProcAddress(dll, "RefreshGlobalConfiguration");

        _DLSSG_D3D12_Init = (PFN_D3D12_Init) GetProcAddress(dll, "NVSDK_NGX_D3D12_Init");
        _DLSSG_D3D12_Init_Ext = (PFN_D3D12_Init_Ext) GetProcAddress(dll, "NVSDK_NGX_D3D12_Init_Ext");
        _DLSSG_D3D12_Shutdown = (PFN_D3D12_Shutdown) GetProcAddress(dll, "NVSDK_NGX_D3D12_Shutdown");
        _DLSSG_D3D12_Shutdown1 = (PFN_D3D12_Shutdown1) GetProcAddress(dll, "NVSDK_NGX_D3D12_Shutdown1");
        _DLSSG_D3D12_GetScratchBufferSize =
            (PFN_D3D12_GetScratchBufferSize) GetProcAddress(dll, "NVSDK_NGX_D3D12_GetScratchBufferSize");
        _DLSSG_D3D12_CreateFeature = (PFN_D3D12_CreateFeature) GetProcAddress(dll, "NVSDK_NGX_D3D12_CreateFeature");
        _DLSSG_D3D12_ReleaseFeature = (PFN_D3D12_ReleaseFeature) GetProcAddress(dll, "NVSDK_NGX_D3D12_ReleaseFeature");
        _DLSSG_D3D12_GetFeatureRequirements =
            (PFN_D3D12_GetFeatureRequirements) GetProcAddress(dll, "NVSDK_NGX_D3D12_GetFeatureRequirements");
        _DLSSG_D3D12_EvaluateFeature =
            (PFN_D3D12_EvaluateFeature) GetProcAddress(dll, "NVSDK_NGX_D3D12_EvaluateFeature");
        _DLSSG_D3D12_PopulateParameters_Impl =
            (PFN_D3D12_PopulateParameters_Impl) GetProcAddress(dll, "NVSDK_NGX_D3D12_PopulateParameters_Impl");

        _DLSSG_VULKAN_Init = (PFN_VULKAN_Init) GetProcAddress(dll, "NVSDK_NGX_VULKAN_Init");
        _DLSSG_VULKAN_Init_Ext = (PFN_VULKAN_Init_Ext) GetProcAddress(dll, "NVSDK_NGX_VULKAN_Init_Ext");
        _DLSSG_VULKAN_Init_Ext2 = (PFN_VULKAN_Init_Ext2) GetProcAddress(dll, "NVSDK_NGX_VULKAN_Init_Ext2");
        _DLSSG_VULKAN_Shutdown = (PFN_VULKAN_Shutdown) GetProcAddress(dll, "NVSDK_NGX_VULKAN_Shutdown");
        _DLSSG_VULKAN_Shutdown1 = (PFN_VULKAN_Shutdown1) GetProcAddress(dll, "NVSDK_NGX_VULKAN_Shutdown1");
        _DLSSG_VULKAN_GetScratchBufferSize =
            (PFN_VULKAN_GetScratchBufferSize) GetProcAddress(dll, "NVSDK_NGX_VULKAN_GetScratchBufferSize");
        _DLSSG_VULKAN_CreateFeature = (PFN_VULKAN_CreateFeature) GetProcAddress(dll, "NVSDK_NGX_VULKAN_CreateFeature");
        _DLSSG_VULKAN_CreateFeature1 =
            (PFN_VULKAN_CreateFeature1) GetProcAddress(dll, "NVSDK_NGX_VULKAN_CreateFeature1");
        _DLSSG_VULKAN_ReleaseFeature =
            (PFN_VULKAN_ReleaseFeature) GetProcAddress(dll, "NVSDK_NGX_VULKAN_ReleaseFeature");
        _DLSSG_VULKAN_GetFeatureRequirements =
            (PFN_VULKAN_GetFeatureRequirements) GetProcAddress(dll, "NVSDK_NGX_VULKAN_GetFeatureRequirements");
        _DLSSG_VULKAN_EvaluateFeature =
            (PFN_VULKAN_EvaluateFeature) GetProcAddress(dll, "NVSDK_NGX_VULKAN_EvaluateFeature");
        _DLSSG_VULKAN_PopulateParameters_Impl =
            (PFN_VULKAN_PopulateParameters_Impl) GetProcAddress(dll, "NVSDK_NGX_VULKAN_PopulateParameters_Impl");

        LOG_INFO("Nukem's initialized");
    }
    else
    {
        LOG_WARN("Nukem's enabled but cannot be loaded");
    }
}

void Nvngx_Nukems::setSetting(const wchar_t* setting, const wchar_t* value)
{
    if (is120orNewer())
    {
        SetEnvironmentVariable(setting, value);
        _refreshGlobalConfiguration();
    }
}

void Nvngx_Nukems::setDebugView(bool enabled)
{
    auto setting = L"DLSSGTOFSR3_EnableDebugOverlay";
    auto value = enabled ? L"1" : L"";
    setSetting(setting, value);
}

void Nvngx_Nukems::setInterpolatedOnly(bool enabled)
{
    auto setting = L"DLSSGTOFSR3_EnableInterpolatedFramesOnly";
    auto value = enabled ? L"1" : L"";
    setSetting(setting, value);
}
