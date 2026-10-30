#pragma once

#include <NVNGX_Parameter.h>

#include "proxies/NVNGX_Proxy.h"
#include "proxies/Ntdll_Proxy.h"

#include <misc/DeferredRelease.h>

#define DLSSG_MOD_ID_OFFSET 2000000

typedef void (*PFN_RefreshGlobalConfiguration)();
typedef void (*PFN_EnableDebugView)(bool enable);

class DLSSGMod
{
  private:
    // Depth formats need depth stencil usage, the typeless one of the same family can be copied to
    static DXGI_FORMAT DepthCopyFormat(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_D32_FLOAT:
            return DXGI_FORMAT_R32_TYPELESS;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            return DXGI_FORMAT_R24G8_TYPELESS;
        case DXGI_FORMAT_D16_UNORM:
            return DXGI_FORMAT_R16_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return DXGI_FORMAT_R32G8X24_TYPELESS;
        default:
            return format;
        }
    }

    inline static HMODULE _dll = nullptr;

    inline static PFN_RefreshGlobalConfiguration _refreshGlobalConfiguration = nullptr;
    inline static PFN_EnableDebugView _fsrDebugView = nullptr; // for now keep compatibility with the patched 0.110

    inline static PFN_D3D12_Init _DLSSG_D3D12_Init = nullptr;
    inline static PFN_D3D12_Init_Ext _DLSSG_D3D12_Init_Ext = nullptr;
    inline static PFN_D3D12_Shutdown _DLSSG_D3D12_Shutdown = nullptr;
    inline static PFN_D3D12_Shutdown1 _DLSSG_D3D12_Shutdown1 = nullptr;
    inline static PFN_D3D12_GetScratchBufferSize _DLSSG_D3D12_GetScratchBufferSize = nullptr;
    inline static PFN_D3D12_CreateFeature _DLSSG_D3D12_CreateFeature = nullptr;
    inline static PFN_D3D12_ReleaseFeature _DLSSG_D3D12_ReleaseFeature = nullptr;
    inline static PFN_D3D12_GetFeatureRequirements _DLSSG_D3D12_GetFeatureRequirements = nullptr; // unused
    inline static PFN_D3D12_EvaluateFeature _DLSSG_D3D12_EvaluateFeature = nullptr;
    inline static PFN_D3D12_PopulateParameters_Impl _DLSSG_D3D12_PopulateParameters_Impl = nullptr;

    inline static PFN_VULKAN_Init _DLSSG_VULKAN_Init = nullptr;
    inline static PFN_VULKAN_Init_Ext _DLSSG_VULKAN_Init_Ext = nullptr;
    inline static PFN_VULKAN_Init_Ext2 _DLSSG_VULKAN_Init_Ext2 = nullptr;
    inline static PFN_VULKAN_Shutdown _DLSSG_VULKAN_Shutdown = nullptr;
    inline static PFN_VULKAN_Shutdown1 _DLSSG_VULKAN_Shutdown1 = nullptr;
    inline static PFN_VULKAN_GetScratchBufferSize _DLSSG_VULKAN_GetScratchBufferSize = nullptr;
    inline static PFN_VULKAN_CreateFeature _DLSSG_VULKAN_CreateFeature = nullptr;
    inline static PFN_VULKAN_CreateFeature1 _DLSSG_VULKAN_CreateFeature1 = nullptr;
    inline static PFN_VULKAN_ReleaseFeature _DLSSG_VULKAN_ReleaseFeature = nullptr;
    inline static PFN_VULKAN_GetFeatureRequirements _DLSSG_VULKAN_GetFeatureRequirements = nullptr; // unused
    inline static PFN_VULKAN_EvaluateFeature _DLSSG_VULKAN_EvaluateFeature = nullptr;
    inline static PFN_VULKAN_PopulateParameters_Impl _DLSSG_VULKAN_PopulateParameters_Impl = nullptr;

    inline static bool _dx12_inited = false;
    inline static bool _vulkan_inited = false;

    // Envvars that can be set:
    // DLSSGTOFSR3_EnableDebugOverlay
    // DLSSGTOFSR3_EnableDebugTearLines
    // DLSSGTOFSR3_EnableInterpolatedFramesOnly
    static inline void setSetting(const wchar_t* setting, const wchar_t* value)
    {
        if (is120orNewer())
        {
            SetEnvironmentVariable(setting, value);
            _refreshGlobalConfiguration();
        }
    }

  public:
    static void InitDLSSGMod_Dx12()
    {
        LOG_FUNC();

        if (_dx12_inited || Config::Instance()->FGInput.value_or_default() != FGInput::Nukems)
            return;

        if (_dll == nullptr)
        {
            HMODULE memModule = nullptr;
            auto optiPath = Config::Instance()->MainDllPath.value();
            Util::LoadProxyLibrary(L"dlssg_to_fsr3_amd_is_better.dll", L"", optiPath, &memModule, &_dll);

            if (_dll == nullptr && memModule != nullptr)
                _dll = memModule;
        }

        if (_dll != nullptr)
        {
            _DLSSG_D3D12_Init = (PFN_D3D12_Init) GetProcAddress(_dll, "NVSDK_NGX_D3D12_Init");
            _DLSSG_D3D12_Init_Ext = (PFN_D3D12_Init_Ext) GetProcAddress(_dll, "NVSDK_NGX_D3D12_Init_Ext");
            _DLSSG_D3D12_Shutdown = (PFN_D3D12_Shutdown) GetProcAddress(_dll, "NVSDK_NGX_D3D12_Shutdown");
            _DLSSG_D3D12_Shutdown1 = (PFN_D3D12_Shutdown1) GetProcAddress(_dll, "NVSDK_NGX_D3D12_Shutdown1");
            _DLSSG_D3D12_GetScratchBufferSize =
                (PFN_D3D12_GetScratchBufferSize) GetProcAddress(_dll, "NVSDK_NGX_D3D12_GetScratchBufferSize");
            _DLSSG_D3D12_CreateFeature =
                (PFN_D3D12_CreateFeature) GetProcAddress(_dll, "NVSDK_NGX_D3D12_CreateFeature");
            _DLSSG_D3D12_ReleaseFeature =
                (PFN_D3D12_ReleaseFeature) GetProcAddress(_dll, "NVSDK_NGX_D3D12_ReleaseFeature");
            _DLSSG_D3D12_GetFeatureRequirements =
                (PFN_D3D12_GetFeatureRequirements) GetProcAddress(_dll, "NVSDK_NGX_D3D12_GetFeatureRequirements");
            _DLSSG_D3D12_EvaluateFeature =
                (PFN_D3D12_EvaluateFeature) GetProcAddress(_dll, "NVSDK_NGX_D3D12_EvaluateFeature");
            _DLSSG_D3D12_PopulateParameters_Impl =
                (PFN_D3D12_PopulateParameters_Impl) GetProcAddress(_dll, "NVSDK_NGX_D3D12_PopulateParameters_Impl");
            _refreshGlobalConfiguration =
                (PFN_RefreshGlobalConfiguration) GetProcAddress(_dll, "RefreshGlobalConfiguration");
            _fsrDebugView = (PFN_EnableDebugView) GetProcAddress(_dll, "FSRDebugView");
            _dx12_inited = true;

            LOG_INFO("DLSSG Mod initialized for DX12");
        }
        else
        {
            LOG_INFO("DLSSG Mod enabled but cannot be loaded");
        }
    }

    static void InitDLSSGMod_Vulkan()
    {
        LOG_FUNC();

        if (_vulkan_inited || Config::Instance()->FGInput.value_or_default() != FGInput::Nukems)
            return;

        if (_dll == nullptr)
        {
            HMODULE memModule = nullptr;
            auto optiPath = Config::Instance()->MainDllPath.value();
            Util::LoadProxyLibrary(L"dlssg_to_fsr3_amd_is_better.dll", L"", optiPath, &memModule, &_dll);

            if (_dll == nullptr && memModule != nullptr)
                _dll = memModule;
        }

        if (_dll != nullptr)
        {
            _DLSSG_VULKAN_Init = (PFN_VULKAN_Init) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Init");
            _DLSSG_VULKAN_Init_Ext = (PFN_VULKAN_Init_Ext) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Init_Ext");
            _DLSSG_VULKAN_Init_Ext2 = (PFN_VULKAN_Init_Ext2) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Init_Ext2");
            _DLSSG_VULKAN_Shutdown = (PFN_VULKAN_Shutdown) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Shutdown");
            _DLSSG_VULKAN_Shutdown1 = (PFN_VULKAN_Shutdown1) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_Shutdown1");
            _DLSSG_VULKAN_GetScratchBufferSize =
                (PFN_VULKAN_GetScratchBufferSize) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_GetScratchBufferSize");
            _DLSSG_VULKAN_CreateFeature =
                (PFN_VULKAN_CreateFeature) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_CreateFeature");
            _DLSSG_VULKAN_CreateFeature1 =
                (PFN_VULKAN_CreateFeature1) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_CreateFeature1");
            _DLSSG_VULKAN_ReleaseFeature =
                (PFN_VULKAN_ReleaseFeature) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_ReleaseFeature");
            _DLSSG_VULKAN_GetFeatureRequirements =
                (PFN_VULKAN_GetFeatureRequirements) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_GetFeatureRequirements");
            _DLSSG_VULKAN_EvaluateFeature =
                (PFN_VULKAN_EvaluateFeature) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_EvaluateFeature");
            _DLSSG_VULKAN_PopulateParameters_Impl =
                (PFN_VULKAN_PopulateParameters_Impl) GetProcAddress(_dll, "NVSDK_NGX_VULKAN_PopulateParameters_Impl");
            _refreshGlobalConfiguration =
                (PFN_RefreshGlobalConfiguration) GetProcAddress(_dll, "RefreshGlobalConfiguration");
            _fsrDebugView = (PFN_EnableDebugView) GetProcAddress(_dll, "FSRDebugView");
            _vulkan_inited = true;

            LOG_INFO("DLSSG Mod initialized for Vulkan");
        }
        else
        {
            LOG_INFO("DLSSG Mod enabled but cannot be loaded");
        }
    }

    static inline bool isLoaded() { return _dll != nullptr; }

    static inline bool isDx12Available() { return isLoaded() && _dx12_inited; }

    static inline void setDebugView(bool enabled)
    {
        auto setting = L"DLSSGTOFSR3_EnableDebugOverlay";
        auto value = enabled ? L"1" : L"";
        setSetting(setting, value);
    }

    static inline void setInterpolatedOnly(bool enabled)
    {
        auto setting = L"DLSSGTOFSR3_EnableInterpolatedFramesOnly";
        auto value = enabled ? L"1" : L"";
        setSetting(setting, value);
    }

    static inline bool is120orNewer() { return _refreshGlobalConfiguration != nullptr; }

    static inline PFN_EnableDebugView FSRDebugView() { return _fsrDebugView; }

    static inline NVSDK_NGX_Result D3D12_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                              ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                              NVSDK_NGX_Version InSDKVersion)
    {
        if (isDx12Available())
            return _DLSSG_D3D12_Init(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result D3D12_Init_Ext(unsigned long long InApplicationId,
                                                  const wchar_t* InApplicationDataPath, ID3D12Device* InDevice,
                                                  NVSDK_NGX_Version InSDKVersion,
                                                  const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
    {
        if (isDx12Available())
            return _DLSSG_D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result D3D12_Shutdown()
    {
        if (isDx12Available())
            return _DLSSG_D3D12_Shutdown();
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result D3D12_Shutdown1(ID3D12Device* InDevice)
    {
        if (isDx12Available())
            return _DLSSG_D3D12_Shutdown1(InDevice);
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                              const NVSDK_NGX_Parameter* InParameters,
                                                              size_t* OutSizeInBytes)
    {
        if (isDx12Available())
            return _DLSSG_D3D12_GetScratchBufferSize(InFeatureId, InParameters, OutSizeInBytes);
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                       NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter* InParameters,
                                                       NVSDK_NGX_Handle** OutHandle)
    {
        if (isDx12Available())
        {
            auto result = _DLSSG_D3D12_CreateFeature(InCmdList, InFeatureID, InParameters, OutHandle);
            (*OutHandle)->Id += DLSSG_MOD_ID_OFFSET;
            return result;
        }
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
    {
        if (isDx12Available() && InHandle->Id >= DLSSG_MOD_ID_OFFSET)
        {
            NVSDK_NGX_Handle TempHandle = { .Id = InHandle->Id - DLSSG_MOD_ID_OFFSET };
            return _DLSSG_D3D12_ReleaseFeature(&TempHandle);
        }
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result
    D3D12_GetFeatureRequirements(IDXGIAdapter* Adapter, const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                 NVSDK_NGX_FeatureRequirement* OutSupported)
    {
        if (isDx12Available())
            return _DLSSG_D3D12_GetFeatureRequirements(Adapter, FeatureDiscoveryInfo, OutSupported);
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                         const NVSDK_NGX_Handle* InFeatureHandle,
                                                         NVSDK_NGX_Parameter* InParameters,
                                                         PFN_NVSDK_NGX_ProgressCallback InCallback)
    {
        if (isDx12Available() && InFeatureHandle->Id >= DLSSG_MOD_ID_OFFSET)
        {
            if (!is120orNewer())
            {
                // Workaround mostly for final fantasy xvi
                uint32_t depthInverted = 0;
                float cameraNear = 0;
                float cameraFar = 0;
                InParameters->Get("DLSSG.DepthInverted", &depthInverted);
                InParameters->Get("DLSSG.CameraNear", &cameraNear);
                InParameters->Get("DLSSG.CameraFar", &cameraFar);

                if (cameraNear == 0)
                {
                    if (depthInverted)
                        cameraNear = 100000.0f;
                    else
                        cameraNear = 0.1f;

                    InParameters->Set("DLSSG.CameraNear", cameraNear);
                }

                if (cameraFar == 0)
                {
                    if (depthInverted)
                        cameraFar = 0.1f;
                    else
                        cameraFar = 100000.0f;

                    InParameters->Set("DLSSG.CameraFar", cameraFar);
                }
                else if (std::isinf(cameraFar))
                {
                    cameraFar = 100000.0f;
                    InParameters->Set("DLSSG.CameraFar", cameraFar);
                }

                // Workaround for a bug in Nukem's mod
                // if (uint32_t LowresMvec = 0; InParameters->Get("DLSSG.run_lowres_mvec_pass", &LowresMvec) ==
                // NVSDK_NGX_Result_Success && LowresMvec == 1) {
                InParameters->Set("DLSSG.MVecsSubrectWidth", 0U);
                InParameters->Set("DLSSG.MVecsSubrectHeight", 0U);
                //}
            }

            // Make a copy of the depth going to the frame generator
            // Fixes an issue with the depth being corrupted on AMD under Windows
            ID3D12Resource* dlssgDepth = nullptr;

            if (Config::Instance()->MakeDepthCopy.value_or_default())
                InParameters->Get("DLSSG.Depth", &dlssgDepth);

            if (dlssgDepth)
            {
                // Previous frame's copy can still be read by FG
                static ID3D12Resource* copiedDlssgDepth[2] = {};
                static uint32_t copyIndex = 0;
                copyIndex = (copyIndex + 1) % 2;
                auto& copy = copiedDlssgDepth[copyIndex];

                // Without depth stencil usage common state promotes to copy dest and decays back after execution
                D3D12_RESOURCE_DESC desc = dlssgDepth->GetDesc();
                desc.Flags &= ~(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
                desc.Format = DepthCopyFormat(desc.Format);

                if (copy != nullptr)
                {
                    auto copyDesc = copy->GetDesc();
                    if (copyDesc.Width != desc.Width || copyDesc.Height != desc.Height ||
                        copyDesc.Format != desc.Format)
                        DeferredRelease::Release(copy);
                }

                ID3D12Device* device = nullptr;
                if (copy == nullptr && dlssgDepth->GetDevice(IID_PPV_ARGS(&device)) == S_OK)
                {
                    D3D12_HEAP_PROPERTIES heapProperties {};
                    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

                    if (device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                        IID_PPV_ARGS(&copy)) != S_OK)
                    {
                        LOG_ERROR("Making a new resource for DLSSG Depth has failed");
                        copy = nullptr;
                    }

                    device->Release();
                }

                if (copy != nullptr)
                {
                    InCmdList->CopyResource(copy, dlssgDepth);
                    InParameters->Set("DLSSG.Depth",
                                      (void*) copy); // cast to make sure it's void*, otherwise dlssg cries
                }
            }

            NVSDK_NGX_Handle TempHandle = { .Id = InFeatureHandle->Id - DLSSG_MOD_ID_OFFSET };
            return _DLSSG_D3D12_EvaluateFeature(InCmdList, &TempHandle, InParameters, InCallback);
        }

        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
    {
        if (isDx12Available())
            return _DLSSG_D3D12_PopulateParameters_Impl(InParameters);
        return NVSDK_NGX_Result_Fail;
    }

    static inline bool isVulkanAvailable() { return isLoaded() && _vulkan_inited; }

    static inline NVSDK_NGX_Result VULKAN_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                               VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                               PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                               const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                               NVSDK_NGX_Version InSDKVersion)
    {
        if (isVulkanAvailable())
            return _DLSSG_VULKAN_Init(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice, InGIPA,
                                      InGDPA, InFeatureInfo, InSDKVersion);
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result VULKAN_Init_Ext(unsigned long long InApplicationId,
                                                   const wchar_t* InApplicationDataPath, VkInstance InInstance,
                                                   VkPhysicalDevice InPD, VkDevice InDevice,
                                                   NVSDK_NGX_Version InSDKVersion,
                                                   const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
    {
        if (isVulkanAvailable())
            return _DLSSG_VULKAN_Init_Ext(InApplicationId, InApplicationDataPath, InInstance, InPD, InDevice,
                                          InSDKVersion, InFeatureInfo);
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result VULKAN_Init_Ext2(unsigned long long InApplicationId,
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

    static inline NVSDK_NGX_Result VULKAN_Shutdown()
    {
        if (isVulkanAvailable())
            return _DLSSG_VULKAN_Shutdown();
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result VULKAN_Shutdown1(VkDevice InDevice)
    {
        if (isVulkanAvailable())
            return _DLSSG_VULKAN_Shutdown1(InDevice);
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result VULKAN_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                               const NVSDK_NGX_Parameter* InParameters,
                                                               size_t* OutSizeInBytes)
    {
        if (isVulkanAvailable())
            return _DLSSG_VULKAN_GetScratchBufferSize(InFeatureId, InParameters, OutSizeInBytes);
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result VULKAN_CreateFeature(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Feature InFeatureID,
                                                        NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
    {
        if (isVulkanAvailable())
        {
            auto result = _DLSSG_VULKAN_CreateFeature(InCmdBuffer, InFeatureID, InParameters, OutHandle);
            (*OutHandle)->Id += DLSSG_MOD_ID_OFFSET;
            return result;
        }
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result VULKAN_CreateFeature1(VkDevice InDevice, VkCommandBuffer InCmdList,
                                                         NVSDK_NGX_Feature InFeatureID,
                                                         NVSDK_NGX_Parameter* InParameters,
                                                         NVSDK_NGX_Handle** OutHandle)
    {
        if (isVulkanAvailable())
        {
            auto result = _DLSSG_VULKAN_CreateFeature1(InDevice, InCmdList, InFeatureID, InParameters, OutHandle);
            (*OutHandle)->Id += DLSSG_MOD_ID_OFFSET;
            return result;
        }
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result VULKAN_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
    {
        if (isVulkanAvailable() && InHandle->Id >= DLSSG_MOD_ID_OFFSET)
        {
            NVSDK_NGX_Handle TempHandle = { .Id = InHandle->Id - DLSSG_MOD_ID_OFFSET };
            return _DLSSG_VULKAN_ReleaseFeature(&TempHandle);
        }
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result
    VULKAN_GetFeatureRequirements(const VkInstance Instance, const VkPhysicalDevice PhysicalDevice,
                                  const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                  NVSDK_NGX_FeatureRequirement* OutSupported)
    {
        if (isVulkanAvailable())
            return _DLSSG_VULKAN_GetFeatureRequirements(Instance, PhysicalDevice, FeatureDiscoveryInfo, OutSupported);
        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result VULKAN_EvaluateFeature(VkCommandBuffer InCmdList,
                                                          const NVSDK_NGX_Handle* InFeatureHandle,
                                                          NVSDK_NGX_Parameter* InParameters,
                                                          PFN_NVSDK_NGX_ProgressCallback InCallback)
    {
        if (isVulkanAvailable() && InFeatureHandle->Id >= DLSSG_MOD_ID_OFFSET)
        {
            if (!is120orNewer())
            {
                // Workaround mostly for final fantasy xvi, keeping it from DX12
                uint32_t depthInverted = 0;
                float cameraNear = 0;
                float cameraFar = 0;
                InParameters->Get("DLSSG.DepthInverted", &depthInverted);
                InParameters->Get("DLSSG.CameraNear", &cameraNear);
                InParameters->Get("DLSSG.CameraFar", &cameraFar);

                if (cameraNear == 0)
                {
                    if (depthInverted)
                        cameraNear = 100000.0f;
                    else
                        cameraNear = 0.1f;

                    InParameters->Set("DLSSG.CameraNear", cameraNear);
                }

                if (cameraFar == 0)
                {
                    if (depthInverted)
                        cameraFar = 0.1f;
                    else
                        cameraFar = 100000.0f;

                    InParameters->Set("DLSSG.CameraFar", cameraFar);
                }
                else if (std::isinf(cameraFar))
                {
                    cameraFar = 10000;
                    InParameters->Set("DLSSG.CameraFar", cameraFar);
                }

                // Workaround for a bug in Nukem's mod, keeping it from DX12
                // if (uint32_t LowresMvec = 0; InParameters->Get("DLSSG.run_lowres_mvec_pass", &LowresMvec) ==
                // NVSDK_NGX_Result_Success && LowresMvec == 1) {
                InParameters->Set("DLSSG.MVecsSubrectWidth", 0U);
                InParameters->Set("DLSSG.MVecsSubrectHeight", 0U);
                //}
            }

            NVSDK_NGX_Handle TempHandle = { .Id = InFeatureHandle->Id - DLSSG_MOD_ID_OFFSET };
            return _DLSSG_VULKAN_EvaluateFeature(InCmdList, &TempHandle, InParameters, InCallback);
        }

        return NVSDK_NGX_Result_Fail;
    }

    static inline NVSDK_NGX_Result VULKAN_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
    {
        if (isVulkanAvailable())
            return _DLSSG_VULKAN_PopulateParameters_Impl(InParameters);
        return NVSDK_NGX_Result_Fail;
    }
};
