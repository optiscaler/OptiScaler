#include "pch.h"
#include "Nvngx_RRDllProxy.h"

#include <misc/CustomProviders.h>

Nvngx_RRDllProxy::Nvngx_RRDllProxy(const std::string& name)
{
    auto exports = CustomProviders::Load(name, CustomProviders::Kind::Dlssd);

    if (!exports)
        return;

    ngx.SetSource(exports);
    ngx.LoadExports();

    LOG_INFO("Custom DLSSD {} DX12: {}, Vulkan: {}", name, IsDx12Available(), IsVulkanAvailable());
}

bool Nvngx_RRDllProxy::InitDx12(ID3D12Device* InDevice)
{
    if (_dx12Inited)
        return true;

    if (!IsDx12Available())
        return false;

    auto& state = State::Instance();
    NVSDK_NGX_FeatureCommonInfo fcInfo {};
    NVNGXProxy::GetFeatureCommonInfo(&fcInfo);

    NVSDK_NGX_Result result;

    if (ngx.D3D12_Init_Ext != nullptr)
        result = ngx.D3D12_Init_Ext(state.NVNGX_ApplicationId, state.NVNGX_ApplicationDataPath.c_str(), InDevice,
                                    state.NVNGX_Version, &fcInfo);
    else
        result = ngx.D3D12_Init(state.NVNGX_ApplicationId, state.NVNGX_ApplicationDataPath.c_str(), InDevice, &fcInfo,
                                state.NVNGX_Version);

    LOG_INFO("Result: {:X}", (UINT) result);

    _dx12Inited = result == NVSDK_NGX_Result_Success;
    return _dx12Inited;
}

bool Nvngx_RRDllProxy::InitVulkan(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                  PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA)
{
    if (_vulkanInited)
        return true;

    if (!IsVulkanAvailable())
        return false;

    auto& state = State::Instance();
    NVSDK_NGX_FeatureCommonInfo fcInfo {};
    NVNGXProxy::GetFeatureCommonInfo(&fcInfo);

    const auto appId = state.NVNGX_ApplicationId;
    const auto dataPath = state.NVNGX_ApplicationDataPath.c_str();
    NVSDK_NGX_Result result;

    if (ngx.VULKAN_Init_Ext2 != nullptr)
        result = ngx.VULKAN_Init_Ext2(appId, dataPath, InInstance, InPD, InDevice, InGIPA, InGDPA, state.NVNGX_Version,
                                      &fcInfo);
    else if (ngx.VULKAN_Init != nullptr)
        result =
            ngx.VULKAN_Init(appId, dataPath, InInstance, InPD, InDevice, InGIPA, InGDPA, &fcInfo, state.NVNGX_Version);
    else
        result = ngx.VULKAN_Init_Ext(appId, dataPath, InInstance, InPD, InDevice, state.NVNGX_Version, &fcInfo);

    LOG_INFO("Result: {:X}", (UINT) result);

    _vulkanInited = result == NVSDK_NGX_Result_Success;
    return _vulkanInited;
}

void Nvngx_RRDllProxy::ShutdownDx12(ID3D12Device* InDevice)
{
    if (!_dx12Inited)
        return;

    _dx12Inited = false;

    // Like NGX, the one without a device for the device-less shutdown
    if (ngx.D3D12_Shutdown1 != nullptr && (InDevice != nullptr || ngx.D3D12_Shutdown == nullptr))
        ngx.D3D12_Shutdown1(InDevice);
    else if (ngx.D3D12_Shutdown != nullptr)
        ngx.D3D12_Shutdown();
}

void Nvngx_RRDllProxy::ShutdownVulkan(VkDevice InDevice)
{
    if (!_vulkanInited)
        return;

    _vulkanInited = false;

    if (ngx.VULKAN_Shutdown1 != nullptr && (InDevice != VK_NULL_HANDLE || ngx.VULKAN_Shutdown == nullptr))
        ngx.VULKAN_Shutdown1(InDevice);
    else if (ngx.VULKAN_Shutdown != nullptr)
        ngx.VULKAN_Shutdown();
}
