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
    auto populate = _DLSSG_D3D12_PopulateParameters_Impl != nullptr
                        ? _DLSSG_D3D12_PopulateParameters_Impl
                        : (PFN_D3D12_PopulateParameters_Impl) _DLSSG_VULKAN_PopulateParameters_Impl;
    NVNGX_Parameters params(API::DX12, false);
    int countMax = 0;

    if (populate != nullptr && populate(&params) == NVSDK_NGX_Result_Success &&
        params.Get("DLSSG.MultiFrameCountMax", &countMax) == NVSDK_NGX_Result_Success && countMax > 0)
    {
        _maxFakeFrames = countMax;
    }

    LOG_INFO("Custom DLSSG {} initialized, max fake frames: {}", _name, _maxFakeFrames);
}
