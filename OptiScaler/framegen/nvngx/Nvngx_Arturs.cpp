#include "pch.h"
#include "Nvngx_Arturs.h"

void Nvngx_Arturs::QueryVersions()
{
    if (!ngx.D3D12_PopulateParameters_Impl)
        return;

    // Main version
    HMODULE hModule;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR) ngx.D3D12_PopulateParameters_Impl, &hModule);

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
    Util::LoadProxyLibrary(L"dlss-enabler-headless.dll", L"", optiPath, &memModule, &ngx.dll);

    if (ngx.dll == nullptr && memModule != nullptr)
        ngx.dll = memModule;

    if (ngx.dll != nullptr)
    {
        ngx.LoadExports("DLSSG_NVSDK_NGX_");

        QueryVersions();

        LOG_INFO("Artur's initialized");
    }
    else
    {
        LOG_WARN("Artur's enabled but cannot be loaded");
    }
}
