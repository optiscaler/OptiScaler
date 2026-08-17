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
        return _DLSSG_D3D12_CreateFeature(InCmdList, InFeatureID, InParameters, OutHandle);
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    if (isDx12Available())
    {
        return _DLSSG_D3D12_ReleaseFeature(InHandle);
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

static bool CreateBufferResource(ID3D12Device* device, ID3D12Resource* source, D3D12_RESOURCE_STATES initialState,
                                 ID3D12Resource** target)
{
    if (device == nullptr || source == nullptr)
        return false;

    auto inDesc = source->GetDesc();

    if (*target != nullptr)
    {
        auto bufDesc = (*target)->GetDesc();

        if (bufDesc.Width != inDesc.Width || bufDesc.Height != inDesc.Height || bufDesc.Format != inDesc.Format ||
            bufDesc.Flags != inDesc.Flags)
        {
            (*target)->Release();
            (*target) = nullptr;
        }
        else
        {
            return true;
        }
    }

    D3D12_HEAP_PROPERTIES heapProperties;
    D3D12_HEAP_FLAGS heapFlags;

    HRESULT hr = source->GetHeapProperties(&heapProperties, &heapFlags);

    if (hr != S_OK)
    {
        LOG_ERROR("GetHeapProperties result: {:X}", (UINT64) hr);
        return false;
    }

    hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &inDesc, initialState, nullptr,
                                         IID_PPV_ARGS(target));

    if (hr != S_OK)
    {
        LOG_ERROR("CreateCommittedResource result: {:X}", (UINT64) hr);
        return false;
    }

    LOG_DEBUG("Created new one: {}x{}", inDesc.Width, inDesc.Height);

    return true;
}

static void ResourceBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
                            D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    if (beforeState == afterState)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
}

NVSDK_NGX_Result Nvngx_DllProxy::D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                       const NVSDK_NGX_Handle* InFeatureHandle,
                                                       NVSDK_NGX_Parameter* InParameters,
                                                       PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    if (isDx12Available())
    {
        // Make a copy of the depth going to the frame generator
        // Fixes an issue with the depth being corrupted on AMD under Windows
        ID3D12Resource* dlssgDepth = nullptr;

        if (Config::Instance()->NvngxFGMakeDepthCopy.value_or_default())
            InParameters->Get("DLSSG.Depth", &dlssgDepth);

        if (dlssgDepth)
        {
            static size_t count = 0;
            const size_t index = count % 2;

            // Nukem's is expecting D3D12_RESOURCE_STATE_COPY_DEST
            CreateBufferResource(State::Instance().currentD3D12Device, dlssgDepth, D3D12_RESOURCE_STATE_COPY_DEST,
                                 &depthCopy[index]);

            if (depthCopy[index])
            {
                ResourceBarrier(InCmdList, dlssgDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                D3D12_RESOURCE_STATE_COPY_SOURCE);

                InCmdList->CopyResource(depthCopy[index], dlssgDepth);

                ResourceBarrier(InCmdList, dlssgDepth, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                // cast to make sure it's void*, otherwise dlssg cries
                InParameters->Set("DLSSG.Depth", (void*) depthCopy[index]);
            }

            count++;
        }

        bool showDebug = Config::Instance()->NvngxFGShowDebug.value_or_default();
        uint32_t flags = Config::Instance()->NvngxFGDispatchFlags.value_or_default();

        InParameters->Set("DLSSG.ShowDebug", showDebug);
        InParameters->Set("DLSSG.DispatchFlags", flags);

        return _DLSSG_D3D12_EvaluateFeature(InCmdList, InFeatureHandle, InParameters, InCallback);
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
        return _DLSSG_VULKAN_CreateFeature(InCmdBuffer, InFeatureID, InParameters, OutHandle);
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_CreateFeature1(VkDevice InDevice, VkCommandBuffer InCmdList,
                                                       NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter* InParameters,
                                                       NVSDK_NGX_Handle** OutHandle)
{
    if (isVulkanAvailable())
    {
        return _DLSSG_VULKAN_CreateFeature1(InDevice, InCmdList, InFeatureID, InParameters, OutHandle);
    }
    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    if (isVulkanAvailable())
    {
        return _DLSSG_VULKAN_ReleaseFeature(InHandle);
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
    if (isVulkanAvailable())
    {
        return _DLSSG_VULKAN_EvaluateFeature(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    return NVSDK_NGX_Result_Fail;
}

NVSDK_NGX_Result Nvngx_DllProxy::VULKAN_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    if (isVulkanAvailable())
        return _DLSSG_VULKAN_PopulateParameters_Impl(InParameters);
    return NVSDK_NGX_Result_Fail;
}
