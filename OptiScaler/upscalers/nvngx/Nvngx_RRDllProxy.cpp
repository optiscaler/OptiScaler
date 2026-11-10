#include "pch.h"
#include "Nvngx_RRDllProxy.h"

#include <misc/CustomProviders.h>

Nvngx_RRDllProxy::Nvngx_RRDllProxy(const std::string& name)
{
    auto path = CustomProviders::DlssdDllPath(name);
    _dll = CustomProviders::LoadDll(name, path);

    if (_dll == nullptr)
        return;

    filePath = path.wstring();

    D3D12_Init = (PFN_D3D12_Init) GetProcAddress(_dll, "NVSDK_NGX_D3D12_Init");
    D3D12_Init_Ext = (PFN_D3D12_Init_Ext) GetProcAddress(_dll, "NVSDK_NGX_D3D12_Init_Ext");
    D3D12_Shutdown = (PFN_D3D12_Shutdown) GetProcAddress(_dll, "NVSDK_NGX_D3D12_Shutdown");
    D3D12_Shutdown1 = (PFN_D3D12_Shutdown1) GetProcAddress(_dll, "NVSDK_NGX_D3D12_Shutdown1");
    D3D12_CreateFeature = (PFN_D3D12_CreateFeature) GetProcAddress(_dll, "NVSDK_NGX_D3D12_CreateFeature");
    D3D12_ReleaseFeature = (PFN_D3D12_ReleaseFeature) GetProcAddress(_dll, "NVSDK_NGX_D3D12_ReleaseFeature");
    D3D12_GetFeatureRequirements =
        (PFN_D3D12_GetFeatureRequirements) GetProcAddress(_dll, "NVSDK_NGX_D3D12_GetFeatureRequirements");
    D3D12_EvaluateFeature = (PFN_D3D12_EvaluateFeature) GetProcAddress(_dll, "NVSDK_NGX_D3D12_EvaluateFeature");
    D3D12_PopulateParameters_Impl =
        (PFN_D3D12_PopulateParameters_Impl) GetProcAddress(_dll, "NVSDK_NGX_D3D12_PopulateParameters_Impl");

    VULKAN_Init = (PFN_VULKAN_Init) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Init");
    VULKAN_Init_Ext = (PFN_VULKAN_Init_Ext) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Init_Ext");
    VULKAN_Init_Ext2 = (PFN_VULKAN_Init_Ext2) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Init_Ext2");
    VULKAN_Shutdown = (PFN_VULKAN_Shutdown) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Shutdown");
    VULKAN_Shutdown1 = (PFN_VULKAN_Shutdown1) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Shutdown1");
    VULKAN_CreateFeature = (PFN_VULKAN_CreateFeature) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_CreateFeature");
    VULKAN_CreateFeature1 = (PFN_VULKAN_CreateFeature1) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_CreateFeature1");
    VULKAN_ReleaseFeature = (PFN_VULKAN_ReleaseFeature) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_ReleaseFeature");
    VULKAN_GetFeatureRequirements =
        (PFN_VULKAN_GetFeatureRequirements) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_GetFeatureRequirements");
    VULKAN_EvaluateFeature = (PFN_VULKAN_EvaluateFeature) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_EvaluateFeature");
    VULKAN_PopulateParameters_Impl =
        (PFN_VULKAN_PopulateParameters_Impl) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_PopulateParameters_Impl");

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

    if (D3D12_Init_Ext != nullptr)
        result = D3D12_Init_Ext(state.NVNGX_ApplicationId, state.NVNGX_ApplicationDataPath.c_str(), InDevice,
                                state.NVNGX_Version, &fcInfo);
    else
        result = D3D12_Init(state.NVNGX_ApplicationId, state.NVNGX_ApplicationDataPath.c_str(), InDevice, &fcInfo,
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

    if (VULKAN_Init_Ext2 != nullptr)
        result =
            VULKAN_Init_Ext2(appId, dataPath, InInstance, InPD, InDevice, InGIPA, InGDPA, state.NVNGX_Version, &fcInfo);
    else if (VULKAN_Init != nullptr)
        result = VULKAN_Init(appId, dataPath, InInstance, InPD, InDevice, InGIPA, InGDPA, &fcInfo, state.NVNGX_Version);
    else
        result = VULKAN_Init_Ext(appId, dataPath, InInstance, InPD, InDevice, state.NVNGX_Version, &fcInfo);

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
    if (D3D12_Shutdown1 != nullptr && (InDevice != nullptr || D3D12_Shutdown == nullptr))
        D3D12_Shutdown1(InDevice);
    else if (D3D12_Shutdown != nullptr)
        D3D12_Shutdown();
}

void Nvngx_RRDllProxy::ShutdownVulkan(VkDevice InDevice)
{
    if (!_vulkanInited)
        return;

    _vulkanInited = false;

    if (VULKAN_Shutdown1 != nullptr && (InDevice != VK_NULL_HANDLE || VULKAN_Shutdown == nullptr))
        VULKAN_Shutdown1(InDevice);
    else if (VULKAN_Shutdown != nullptr)
        VULKAN_Shutdown();
}
