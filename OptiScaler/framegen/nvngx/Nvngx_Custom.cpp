#include "pch.h"
#include "Nvngx_Custom.h"

#include <misc/CustomProviders.h>

void Nvngx_Custom::LoadLibraries()
{
    dll = CustomProviders::LoadDll(_name, CustomProviders::DlssgDllPath(_name));

    if (dll == nullptr)
        return;

    LoadNgxExports();

    // Max fake frames are reported like DLSSG does, which only reports them once inited
    QueryReportedParameters();

    // A provider that reports FrameGeneration.Available 0 can't run here: fall back instead of creating features
    // that fail. A count of 0 alone keeps the default of 1.
    if (reportedAvailable == 0)
    {
        LOG_WARN("Custom DLSSG {} reports no frame generation (max fake frames {}), not using it", _name,
                 reportedMaxFakeFrames);
        ClearNgxExports();
        return;
    }

    if (!Nvngx_DllProxy::supportsDispatchFlags())
        LOG_INFO("Custom DLSSG {} initialized, max fake frames: {}", _name, getMaxFakeFramesCount());
    else
        LOG_INFO("Custom DLSSG {} initialized, max fake frames: {}, antighosting version: {}.{}", _name,
                 getMaxFakeFramesCount(), antighostingVersion.major, antighostingVersion.minor);
}
