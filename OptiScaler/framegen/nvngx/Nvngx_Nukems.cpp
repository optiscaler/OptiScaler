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

        LoadNgxExports();

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
