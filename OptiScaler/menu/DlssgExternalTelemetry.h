#pragma once
// Optional read-only adapter for an external DLSSG bridge. Does not create an
// OptiScaler FG backend or alter its frame generation, pacing, or input hooks.
#include <Windows.h>
#include <Psapi.h>
#include <cmath>
#include "frame_telemetry_api.h"
namespace DlssgExternalTelemetry
{
static_assert(sizeof(DLSSGFrameTelemetry) == 40, "External DLSSG telemetry ABI layout changed");
using Query = int(__stdcall*)(DLSSGFrameTelemetry*);
inline bool Read(DLSSGFrameTelemetry& sample)
{
    static Query query = nullptr;
    static ULONGLONG nextScan = 0;
    const auto now = GetTickCount64();
    if (!query && now >= nextScan)
    {
        nextScan = now + 1000;
        HMODULE modules[1024];
        DWORD bytes = 0;
        if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes) && bytes <= sizeof(modules))
        {
            FARPROC found = nullptr;
            unsigned count = 0;
            for (unsigned i = 0; i < bytes / sizeof(HMODULE); ++i)
            {
                auto p = GetProcAddress(modules[i], "DLSSG_GetFrameTelemetry");
                if (p)
                {
                    found = p;
                    ++count;
                }
            }
            HMODULE pinned = nullptr;
            if (count == 1 && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                                 reinterpret_cast<LPCWSTR>(found), &pinned))
                query = reinterpret_cast<Query>(found);
        }
    }
    sample = {};
    sample.size = sizeof(sample);
    sample.version = DLSSG_FRAME_TELEMETRY_VERSION;
    return query && query(&sample) && sample.size == sizeof(sample) &&
           sample.version == DLSSG_FRAME_TELEMETRY_VERSION &&
           (sample.flags & (DLSSG_TELEMETRY_ENABLED | DLSSG_TELEMETRY_BASE_VALID)) ==
               (DLSSG_TELEMETRY_ENABLED | DLSSG_TELEMETRY_BASE_VALID) &&
           std::isfinite(sample.base_fps) && sample.base_fps > 0 && sample.age_ms <= 1000;
}
} // namespace DlssgExternalTelemetry
