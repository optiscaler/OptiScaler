#include "pch.h"
#include "Nvngx_DllProxy.h"

NVSDK_NGX_Result Nvngx_DllProxy::D3D12_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                            ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                            NVSDK_NGX_Version InSDKVersion)
{
    if (isDx12Available())
        return _DLSSG_D3D12_Init(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::D3D12_Init_Ext(unsigned long long InApplicationId,
                                                const wchar_t* InApplicationDataPath, ID3D12Device* InDevice,
                                                NVSDK_NGX_Version InSDKVersion,
                                                const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    if (isDx12Available())
        return _DLSSG_D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::D3D12_Shutdown()
{
    if (isDx12Available())
        return _DLSSG_D3D12_Shutdown();
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::D3D12_Shutdown1(ID3D12Device* InDevice)
{
    if (isDx12Available())
        return _DLSSG_D3D12_Shutdown1(InDevice);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                            const NVSDK_NGX_Parameter* InParameters,
                                                            size_t* OutSizeInBytes)
{
    if (isDx12Available())
        return _DLSSG_D3D12_GetScratchBufferSize(InFeatureId, InParameters, OutSizeInBytes);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                     NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter* InParameters,
                                                     NVSDK_NGX_Handle** OutHandle)
{
    if (isDx12Available())
    {
        auto result = _DLSSG_D3D12_CreateFeature(InCmdList, InFeatureID, InParameters, OutHandle);
        (*OutHandle)->Id += NVNGX_PROVIDER_ID_OFFSET;
        return result;
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    if (isDx12Available() && InHandle->Id >= NVNGX_PROVIDER_ID_OFFSET)
    {
        NVSDK_NGX_Handle TempHandle = { .Id = InHandle->Id - NVNGX_PROVIDER_ID_OFFSET };

        return _DLSSG_D3D12_ReleaseFeature(&TempHandle);
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result
Nvngx_DllProxy::D3D12_GetFeatureRequirements(IDXGIAdapter* Adapter,
                                             const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                             NVSDK_NGX_FeatureRequirement* OutSupported)
{
    if (isDx12Available())
        return _DLSSG_D3D12_GetFeatureRequirements(Adapter, FeatureDiscoveryInfo, OutSupported);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                       const NVSDK_NGX_Handle* InFeatureHandle,
                                                       NVSDK_NGX_Parameter* InParameters,
                                                       PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    if (isDx12Available() && InFeatureHandle->Id >= NVNGX_PROVIDER_ID_OFFSET)
    {
        // Make a copy of the depth going to the frame generator
        // Fixes an issue with the depth being corrupted on AMD under Windows
        ID3D12Resource* dlssgDepth = nullptr;

        if (Config::Instance()->NvngxFGMakeDepthCopy.value_or_default())
            InParameters->Get("DLSSG.Depth", &dlssgDepth);

        if (dlssgDepth)
        {
            D3D12_RESOURCE_DESC desc = dlssgDepth->GetDesc();

            D3D12_HEAP_PROPERTIES heapProperties;
            D3D12_HEAP_FLAGS heapFlags;

            static ID3D12Resource* copiedDlssgDepth = nullptr;
            SAFE_RELEASE(copiedDlssgDepth);

            if (dlssgDepth->GetHeapProperties(&heapProperties, &heapFlags) == S_OK)
            {
                auto result = State::Instance().currentD3D12Device->CreateCommittedResource(
                    &heapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(&copiedDlssgDepth));

                if (result == S_OK)
                {
                    InCmdList->CopyResource(copiedDlssgDepth, dlssgDepth);
                    InParameters->Set("DLSSG.Depth",
                                      (void*) copiedDlssgDepth); // cast to make sure it's void*, otherwise dlssg cries
                }
                else
                {
                    LOG_ERROR("Making a new resource for DLSSG Depth has failed");
                }
            }
            else
            {
                LOG_ERROR("Getting heap properties has failed");
            }
        }

        bool showDebug = Config::Instance()->NvngxFGShowDebug.value_or_default();
        uint32_t flags = Config::Instance()->NvngxFGDispatchFlags.value_or_default();

        InParameters->Set("DLSSG.ShowDebug", showDebug);
        InParameters->Set("DLSSG.DispatchFlags", flags);

        NVSDK_NGX_Handle TempHandle = { .Id = InFeatureHandle->Id - NVNGX_PROVIDER_ID_OFFSET };
        return _DLSSG_D3D12_EvaluateFeature(InCmdList, &TempHandle, InParameters, InCallback);
    }

    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    if (isDx12Available())
        return _DLSSG_D3D12_PopulateParameters_Impl(InParameters);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                             VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                             PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                             const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                             NVSDK_NGX_Version InSDKVersion)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_Init(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice, InGIPA, InGDPA,
                                  InFeatureInfo, InSDKVersion);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_Init_Ext(unsigned long long InApplicationId,
                                                 const wchar_t* InApplicationDataPath, VkInstance InInstance,
                                                 VkPhysicalDevice InPD, VkDevice InDevice,
                                                 NVSDK_NGX_Version InSDKVersion,
                                                 const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_Init_Ext(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice, InSDKVersion,
                                      InFeatureInfo);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_Init_Ext2(unsigned long long InApplicationId,
                                                  const wchar_t* InApplicationDataPath, VkInstance InInstance,
                                                  VkPhysicalDevice InPD, VkDevice InDevice,
                                                  PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                                  NVSDK_NGX_Version InSDKVersion,
                                                  const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_Init_Ext2(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice, InGIPA,
                                       InGDPA, InSDKVersion, InFeatureInfo);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_Shutdown()
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_Shutdown();
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_Shutdown1(VkDevice InDevice)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_Shutdown1(InDevice);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                             const NVSDK_NGX_Parameter* InParameters,
                                                             size_t* OutSizeInBytes)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_GetScratchBufferSize(InFeatureId, InParameters, OutSizeInBytes);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_CreateFeature(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Feature InFeatureID,
                                                      NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    if (isVulkanAvailable())
    {
        auto result = _DLSSG_VULKAN_CreateFeature(InCmdBuffer, InFeatureID, InParameters, OutHandle);
        (*OutHandle)->Id += NVNGX_PROVIDER_ID_OFFSET;
        return result;
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_CreateFeature1(VkDevice InDevice, VkCommandBuffer InCmdList,
                                                       NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter* InParameters,
                                                       NVSDK_NGX_Handle** OutHandle)
{
    if (isVulkanAvailable())
    {
        auto result = _DLSSG_VULKAN_CreateFeature1(InDevice, InCmdList, InFeatureID, InParameters, OutHandle);
        (*OutHandle)->Id += NVNGX_PROVIDER_ID_OFFSET;
        return result;
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    if (isVulkanAvailable() && InHandle->Id >= NVNGX_PROVIDER_ID_OFFSET)
    {
        NVSDK_NGX_Handle TempHandle = { .Id = InHandle->Id - NVNGX_PROVIDER_ID_OFFSET };
        return _DLSSG_VULKAN_ReleaseFeature(&TempHandle);
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result
Nvngx_DllProxy::VULKAN_GetFeatureRequirements(const VkInstance Instance, const VkPhysicalDevice PhysicalDevice,
                                              const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                              NVSDK_NGX_FeatureRequirement* OutSupported)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_GetFeatureRequirements(Instance, PhysicalDevice, FeatureDiscoveryInfo, OutSupported);
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_EvaluateFeature(VkCommandBuffer InCmdList,
                                                        const NVSDK_NGX_Handle* InFeatureHandle,
                                                        NVSDK_NGX_Parameter* InParameters,
                                                        PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    if (isVulkanAvailable() && InFeatureHandle->Id >= NVNGX_PROVIDER_ID_OFFSET)
    {
        NVSDK_NGX_Handle TempHandle = { .Id = InFeatureHandle->Id - NVNGX_PROVIDER_ID_OFFSET };
        return _DLSSG_VULKAN_EvaluateFeature(InCmdList, &TempHandle, InParameters, InCallback);
    }

    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_PopulateParameters_Impl(InParameters);
    return NVSDK_NGX_Result_Fail;
}
