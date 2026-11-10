#include "pch.h"
#include "Nvngx_Custom.h"

#include <misc/CustomProviders.h>

void Nvngx_Custom::LoadLibraries()
{
    ngx.dll = CustomProviders::LoadDll(_name, CustomProviders::DlssgDllPath(_name));

    if (ngx.dll == nullptr)
        return;

    ngx.LoadExports();

    // Max fake frames are reported like DLSSG does, which only reports them once inited
    QueryReportedParameters();

    if (!Nvngx_DllProxy::supportsDispatchFlags())
        LOG_INFO("Custom DLSSG {} initialized, max fake frames: {}", _name, getMaxFakeFramesCount());
    else
        LOG_INFO("Custom DLSSG {} initialized, max fake frames: {}, antighosting version: {}.{}", _name,
                 getMaxFakeFramesCount(), antighostingVersion.major, antighostingVersion.minor);
}
