#include "pch.h"
#include "Nvngx_Arturs.h"

void Nvngx_Arturs::QueryVersions()
{
    if (!_DLSSG_D3D12_PopulateParameters_Impl)
        return;

    // Main version
    HMODULE hModule;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR) _DLSSG_D3D12_PopulateParameters_Impl, &hModule);

    wchar_t dllPath[MAX_PATH] = { 0 };
    GetModuleFileNameW(hModule, dllPath, MAX_PATH);

    version_t tempVersion {};
    Util::GetFileVersion(dllPath, &tempVersion);
    _version = tempVersion;

    // Antighosting version
    QueryReportedParameters();

    if (!Nvngx_DllProxy::supportsDispatchFlags())
        LOG_WARN("Couldn't query version");

    LOG_INFO("DE Ver: {}.{}.{}.{}, GB Ver: {}.{}", _version.major, _version.minor, _version.patch, _version.reserved,
             antighostingVersion.major, antighostingVersion.minor);
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

        _DLSSG_VULKAN_Init = (PFN_VULKAN_Init) GetProcAddress(dll, "DLSSG_NVSDK_NGX_VULKAN_Init");
        _DLSSG_VULKAN_Init_Ext = (PFN_VULKAN_Init_Ext) GetProcAddress(dll, "DLSSG_NVSDK_NGX_VULKAN_Init_Ext");
        _DLSSG_VULKAN_Init_Ext2 = (PFN_VULKAN_Init_Ext2) GetProcAddress(dll, "DLSSG_NVSDK_NGX_VULKAN_Init_Ext2");
        _DLSSG_VULKAN_Shutdown = (PFN_VULKAN_Shutdown) GetProcAddress(dll, "DLSSG_NVSDK_NGX_VULKAN_Shutdown");
        _DLSSG_VULKAN_Shutdown1 = (PFN_VULKAN_Shutdown1) GetProcAddress(dll, "DLSSG_NVSDK_NGX_VULKAN_Shutdown1");
        _DLSSG_VULKAN_GetScratchBufferSize =
            (PFN_VULKAN_GetScratchBufferSize) GetProcAddress(dll, "DLSSG_NVSDK_NGX_VULKAN_GetScratchBufferSize");
        _DLSSG_VULKAN_CreateFeature =
            (PFN_VULKAN_CreateFeature) GetProcAddress(dll, "DLSSG_NVSDK_NGX_VULKAN_CreateFeature");
        _DLSSG_VULKAN_CreateFeature1 =
            (PFN_VULKAN_CreateFeature1) GetProcAddress(dll, "DLSSG_NVSDK_NGX_VULKAN_CreateFeature1");
        _DLSSG_VULKAN_ReleaseFeature =
            (PFN_VULKAN_ReleaseFeature) GetProcAddress(dll, "DLSSG_NVSDK_NGX_VULKAN_ReleaseFeature");
        _DLSSG_VULKAN_GetFeatureRequirements =
            (PFN_VULKAN_GetFeatureRequirements) GetProcAddress(dll, "DLSSG_NVSDK_NGX_VULKAN_GetFeatureRequirements");
        _DLSSG_VULKAN_EvaluateFeature =
            (PFN_VULKAN_EvaluateFeature) GetProcAddress(dll, "DLSSG_NVSDK_NGX_VULKAN_EvaluateFeature");
        _DLSSG_VULKAN_PopulateParameters_Impl =
            (PFN_VULKAN_PopulateParameters_Impl) GetProcAddress(dll, "DLSSG_NVSDK_NGX_VULKAN_PopulateParameters_Impl");

        QueryVersions();

        LOG_INFO("Artur's initialized");
    }
    else
    {
        LOG_WARN("Artur's enabled but cannot be loaded");
    }
}
