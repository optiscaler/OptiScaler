#include "pch.h"
#include "Nvngx_Custom.h"

#include <misc/CustomProviders.h>

void Nvngx_Custom::LoadLibraries()
{
    auto exports = CustomProviders::Load(_name, CustomProviders::Kind::Dlssg);

    if (!exports)
        return;

    ngx.SetSource(exports);
    ngx.LoadExports();

    // Max fake frames are reported like DLSSG does, which only reports them once inited
    QueryReportedParameters();

    // A provider that reports FrameGeneration.Available 0 can't run here: fall back instead of creating features
    // that fail. The dll stays loaded, without its exports isDx12Available and isVulkanAvailable are false.
    if (reportedAvailable == 0)
    {
        LOG_WARN("Custom DLSSG {} reports no frame generation (max fake frames {}), not using it", _name,
                 reportedMaxFakeFrames);

        auto dll = ngx.dll;
        ngx = {};
        ngx.dll = dll;
        return;
    }

    if (!Nvngx_DllProxy::supportsDispatchFlags())
        LOG_INFO("Custom DLSSG {} initialized, max fake frames: {}", _name, getMaxFakeFramesCount());
    else
        LOG_INFO("Custom DLSSG {} initialized, max fake frames: {}, antighosting version: {}.{}", _name,
                 getMaxFakeFramesCount(), antighostingVersion.major, antighostingVersion.minor);
}
