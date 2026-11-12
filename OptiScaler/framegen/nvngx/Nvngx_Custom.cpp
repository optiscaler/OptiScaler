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

    if (!Nvngx_DllProxy::supportsDispatchFlags())
        LOG_INFO("Custom DLSSG {} initialized, max fake frames: {}", _name, getMaxFakeFramesCount());
    else
        LOG_INFO("Custom DLSSG {} initialized, max fake frames: {}, antighosting version: {}.{}", _name,
                 getMaxFakeFramesCount(), antighostingVersion.major, antighostingVersion.minor);
}
