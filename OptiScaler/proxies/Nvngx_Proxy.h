#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include "nvapi/NvApiHooks.h"
#include <misc/CustomProviders.h>

#include "detours/detours.h"

#include <filesystem>
#include <list>
#include <vulkan/vulkan.h>

inline const char* project_id_override = "24480451-f00d-face-1304-0308dabad187";
constexpr unsigned long long app_id_override = 0x24480451;

#pragma region spoofing hooks for 16xx

typedef NVSDK_NGX_Result (*PFN_NVSDK_NGX_D3D1X_GetFeatureRequirements)(
    IDXGIAdapter* Adapter, const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
    NVSDK_NGX_FeatureRequirement* OutSupported);
typedef NVSDK_NGX_Result (*PFN_NVSDK_NGX_VULKAN_GetFeatureRequirements)(
    const VkInstance Instance, const VkPhysicalDevice PhysicalDevice,
    const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo, NVSDK_NGX_FeatureRequirement* OutSupported);

inline PFN_NVSDK_NGX_D3D1X_GetFeatureRequirements Original_D3D11_GetFeatureRequirements = nullptr;
inline PFN_NVSDK_NGX_D3D1X_GetFeatureRequirements Original_D3D12_GetFeatureRequirements = nullptr;
inline PFN_NVSDK_NGX_VULKAN_GetFeatureRequirements Original_Vulkan_GetFeatureRequirements = nullptr;

typedef NVSDK_NGX_Result (*PFN_NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements)(
    const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo, uint32_t* OutExtensionCount,
    VkExtensionProperties** OutExtensionProperties);
typedef NVSDK_NGX_Result (*PFN_NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements)(
    VkInstance Instance, VkPhysicalDevice PhysicalDevice, const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
    uint32_t* OutExtensionCount, VkExtensionProperties** OutExtensionProperties);

inline PFN_NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements
    Original_Vulkan_GetFeatureInstanceExtensionRequirements = nullptr;
inline PFN_NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements
    Original_Vulkan_GetFeatureDeviceExtensionRequirements = nullptr;

// NGX returns arrays it owns and frees them when its snippets get reloaded (Streamline's NGX init does that),
// but games keep pointing at the names for vkCreateInstance/vkCreateDevice and their own checks afterwards
inline void CopyNgxExtensionProperties(NVSDK_NGX_Result result, const uint32_t* count,
                                       VkExtensionProperties** properties)
{
    if (result != NVSDK_NGX_Result_Success || count == nullptr || *count == 0 || properties == nullptr ||
        *properties == nullptr)
        return;

    // Never freed, callers don't say when they are done with the names
    static std::list<std::vector<VkExtensionProperties>> copies;
    static std::mutex copiesMutex;

    std::scoped_lock lock(copiesMutex);
    auto& copy = copies.emplace_back(*properties, *properties + *count);
    *properties = copy.data();
}

inline NVSDK_NGX_Result __stdcall
Hooked_Vulkan_GetFeatureInstanceExtensionRequirements(const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                      uint32_t* OutExtensionCount,
                                                      VkExtensionProperties** OutExtensionProperties)
{
    LOG_FUNC();

    auto result = Original_Vulkan_GetFeatureInstanceExtensionRequirements(FeatureDiscoveryInfo, OutExtensionCount,
                                                                          OutExtensionProperties);
    CopyNgxExtensionProperties(result, OutExtensionCount, OutExtensionProperties);
    return result;
}

inline NVSDK_NGX_Result __stdcall Hooked_Vulkan_GetFeatureDeviceExtensionRequirements(
    VkInstance Instance, VkPhysicalDevice PhysicalDevice, const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
    uint32_t* OutExtensionCount, VkExtensionProperties** OutExtensionProperties)
{
    LOG_FUNC();

    auto result = Original_Vulkan_GetFeatureDeviceExtensionRequirements(Instance, PhysicalDevice, FeatureDiscoveryInfo,
                                                                        OutExtensionCount, OutExtensionProperties);
    CopyNgxExtensionProperties(result, OutExtensionCount, OutExtensionProperties);
    return result;
}

inline NVSDK_NGX_Result __stdcall
Hooked_Dx12_GetFeatureRequirements(IDXGIAdapter* Adapter, const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                   NVSDK_NGX_FeatureRequirement* OutSupported)
{
    LOG_FUNC();

    auto result = Original_D3D12_GetFeatureRequirements(Adapter, FeatureDiscoveryInfo, OutSupported);

    if (result == NVSDK_NGX_Result_Success && FeatureDiscoveryInfo->FeatureID == NVSDK_NGX_Feature_SuperSampling)
    {
        LOG_INFO("Spoofing support!");
        OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_Supported;
        OutSupported->MinHWArchitecture = 0;
        strcpy_s(OutSupported->MinOSVersion, "10.0.10240.16384");
    }

    return result;
}

inline NVSDK_NGX_Result __stdcall
Hooked_Dx11_GetFeatureRequirements(IDXGIAdapter* Adapter, const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                   NVSDK_NGX_FeatureRequirement* OutSupported)
{
    LOG_FUNC();

    auto result = Original_D3D11_GetFeatureRequirements(Adapter, FeatureDiscoveryInfo, OutSupported);

    if (result == NVSDK_NGX_Result_Success && FeatureDiscoveryInfo->FeatureID == NVSDK_NGX_Feature_SuperSampling)
    {
        LOG_INFO("Spoofing support!");
        OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_Supported;
        OutSupported->MinHWArchitecture = 0;
        strcpy_s(OutSupported->MinOSVersion, "10.0.10240.16384");
    }

    return result;
}

inline NVSDK_NGX_Result __stdcall
Hooked_Vulkan_GetFeatureRequirements(const VkInstance Instance, const VkPhysicalDevice PhysicalDevice,
                                     const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                     NVSDK_NGX_FeatureRequirement* OutSupported)
{
    LOG_FUNC();

    auto result = Original_Vulkan_GetFeatureRequirements(Instance, PhysicalDevice, FeatureDiscoveryInfo, OutSupported);

    if (result == NVSDK_NGX_Result_Success && FeatureDiscoveryInfo->FeatureID == NVSDK_NGX_Feature_SuperSampling)
    {
        LOG_INFO("Spoofing support!");
        OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_Supported;
        OutSupported->MinHWArchitecture = 0;
        strcpy_s(OutSupported->MinOSVersion, "10.0.10240.16384");
    }

    return result;
}

inline void HookNgxApi(HMODULE nvngx)
{
    if (Original_D3D11_GetFeatureRequirements != nullptr || Original_D3D12_GetFeatureRequirements != nullptr)
        return;

    LOG_DEBUG("Trying to hook NgxApi");

    Original_D3D11_GetFeatureRequirements =
        (PFN_NVSDK_NGX_D3D1X_GetFeatureRequirements) KernelBaseProxy::GetProcAddress_()(
            nvngx, "NVSDK_NGX_D3D11_GetFeatureRequirements");
    Original_D3D12_GetFeatureRequirements =
        (PFN_NVSDK_NGX_D3D1X_GetFeatureRequirements) KernelBaseProxy::GetProcAddress_()(
            nvngx, "NVSDK_NGX_D3D12_GetFeatureRequirements");
    Original_Vulkan_GetFeatureRequirements =
        (PFN_NVSDK_NGX_VULKAN_GetFeatureRequirements) KernelBaseProxy::GetProcAddress_()(
            nvngx, "NVSDK_NGX_VULKAN_GetFeatureRequirements");
    Original_Vulkan_GetFeatureInstanceExtensionRequirements =
        (PFN_NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements) KernelBaseProxy::GetProcAddress_()(
            nvngx, "NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements");
    Original_Vulkan_GetFeatureDeviceExtensionRequirements =
        (PFN_NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements) KernelBaseProxy::GetProcAddress_()(
            nvngx, "NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements");

    if (Original_D3D11_GetFeatureRequirements != nullptr || Original_D3D12_GetFeatureRequirements != nullptr ||
        Original_Vulkan_GetFeatureRequirements != nullptr)
    {
        LOG_INFO("NVSDK_NGX_XXXXXX_GetFeatureRequirements found, hooking!");

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (Original_D3D11_GetFeatureRequirements != nullptr)
            DetourAttach(&(PVOID&) Original_D3D11_GetFeatureRequirements, Hooked_Dx11_GetFeatureRequirements);

        if (Original_D3D12_GetFeatureRequirements != nullptr)
            DetourAttach(&(PVOID&) Original_D3D12_GetFeatureRequirements, Hooked_Dx12_GetFeatureRequirements);

        if (Original_Vulkan_GetFeatureRequirements != nullptr)
            DetourAttach(&(PVOID&) Original_Vulkan_GetFeatureRequirements, Hooked_Vulkan_GetFeatureRequirements);

        if (Original_Vulkan_GetFeatureInstanceExtensionRequirements != nullptr)
            DetourAttach(&(PVOID&) Original_Vulkan_GetFeatureInstanceExtensionRequirements,
                         Hooked_Vulkan_GetFeatureInstanceExtensionRequirements);

        if (Original_Vulkan_GetFeatureDeviceExtensionRequirements != nullptr)
            DetourAttach(&(PVOID&) Original_Vulkan_GetFeatureDeviceExtensionRequirements,
                         Hooked_Vulkan_GetFeatureDeviceExtensionRequirements);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook NVSDK_NGX_XXXXXX_GetFeatureRequirements: {:X}", detourResult);
            Original_D3D11_GetFeatureRequirements = nullptr;
            Original_D3D12_GetFeatureRequirements = nullptr;
            Original_Vulkan_GetFeatureRequirements = nullptr;
            Original_Vulkan_GetFeatureInstanceExtensionRequirements = nullptr;
            Original_Vulkan_GetFeatureDeviceExtensionRequirements = nullptr;
        }
    }
}

inline void UnhookApis()
{
    // NvApi hook stays, it is only installed when nvapi gets loaded
    if (Original_D3D11_GetFeatureRequirements != nullptr || Original_D3D12_GetFeatureRequirements != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (Original_D3D11_GetFeatureRequirements != nullptr)
            DetourDetach(&(PVOID&) Original_D3D11_GetFeatureRequirements, Hooked_Dx11_GetFeatureRequirements);

        if (Original_D3D12_GetFeatureRequirements != nullptr)
            DetourDetach(&(PVOID&) Original_D3D12_GetFeatureRequirements, Hooked_Dx12_GetFeatureRequirements);

        if (Original_Vulkan_GetFeatureRequirements != nullptr)
            DetourDetach(&(PVOID&) Original_Vulkan_GetFeatureRequirements, Hooked_Vulkan_GetFeatureRequirements);

        if (Original_Vulkan_GetFeatureInstanceExtensionRequirements != nullptr)
            DetourDetach(&(PVOID&) Original_Vulkan_GetFeatureInstanceExtensionRequirements,
                         Hooked_Vulkan_GetFeatureInstanceExtensionRequirements);

        if (Original_Vulkan_GetFeatureDeviceExtensionRequirements != nullptr)
            DetourDetach(&(PVOID&) Original_Vulkan_GetFeatureDeviceExtensionRequirements,
                         Hooked_Vulkan_GetFeatureDeviceExtensionRequirements);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to unhook NVSDK_NGX_XXXXXX_GetFeatureRequirements: {:X}", detourResult);
        }
        else
        {
            Original_D3D11_GetFeatureRequirements = nullptr;
            Original_D3D12_GetFeatureRequirements = nullptr;
            Original_Vulkan_GetFeatureRequirements = nullptr;
            Original_Vulkan_GetFeatureInstanceExtensionRequirements = nullptr;
            Original_Vulkan_GetFeatureDeviceExtensionRequirements = nullptr;
        }
    }
}

#pragma endregion

typedef uint32_t (*PFN_NVSDK_NGX_GetSnippetVersion)(void);
static feature_version GetVersionUsingNGXSnippet(const std::vector<std::string>& dlls)
{
    uint32_t highestVersion = 0;
    for (const auto& dll : dlls)
    {
        PFN_NVSDK_NGX_GetSnippetVersion _GetSnippetVersion =
            (PFN_NVSDK_NGX_GetSnippetVersion) DetourFindFunction(dll.c_str(), "NVSDK_NGX_GetSnippetVersion");
        if (_GetSnippetVersion)
        {
            LOG_TRACE("_GetSnippetVersion ptr from {}: {:X}", dll, (ULONG64) _GetSnippetVersion);
            uint32_t version = _GetSnippetVersion();
            highestVersion = std::max(version, highestVersion);
        }
    }

    feature_version version;

    version.major = (highestVersion & 0xFFFF0000) / 0x00010000;
    version.minor = (highestVersion & 0x0000FF00) / 0x00000100;
    version.patch = (highestVersion & 0x000000FF) / 0x00000001;

    return version;
}

typedef NVSDK_NGX_Result (*PFN_D3D11_Init)(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                           ID3D11Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                           NVSDK_NGX_Version InSDKVersion);
typedef NVSDK_NGX_Result (*PFN_D3D11_Init_ProjectID)(const char* InProjectId, NVSDK_NGX_EngineType InEngineType,
                                                     const char* InEngineVersion, const wchar_t* InApplicationDataPath,
                                                     ID3D11Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                                     const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result (*PFN_D3D11_Init_Ext)(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                               ID3D11Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                               const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result (*PFN_D3D11_Shutdown)(void);
typedef NVSDK_NGX_Result (*PFN_D3D11_Shutdown1)(ID3D11Device* InDevice);
typedef NVSDK_NGX_Result (*PFN_D3D11_GetParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_D3D11_AllocateParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_D3D11_GetCapabilityParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_D3D11_DestroyParameters)(NVSDK_NGX_Parameter* InParameters);
typedef NVSDK_NGX_Result (*PFN_D3D11_GetScratchBufferSize)(NVSDK_NGX_Feature InFeatureId,
                                                           const NVSDK_NGX_Parameter* InParameters,
                                                           size_t* OutSizeInBytes);
typedef NVSDK_NGX_Result (*PFN_D3D11_CreateFeature)(ID3D11DeviceContext* InDevCtx, NVSDK_NGX_Feature InFeatureID,
                                                    NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle);
typedef NVSDK_NGX_Result (*PFN_D3D11_ReleaseFeature)(NVSDK_NGX_Handle* InHandle);
typedef NVSDK_NGX_Result (*PFN_D3D11_GetFeatureRequirements)(IDXGIAdapter* Adapter,
                                                             const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                             NVSDK_NGX_FeatureRequirement* OutSupported);
typedef NVSDK_NGX_Result (*PFN_D3D11_EvaluateFeature)(ID3D11DeviceContext* InDevCtx,
                                                      const NVSDK_NGX_Handle* InFeatureHandle,
                                                      const NVSDK_NGX_Parameter* InParameters,
                                                      PFN_NVSDK_NGX_ProgressCallback InCallback);

typedef NVSDK_NGX_Result (*PFN_D3D12_Init)(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                           ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                           NVSDK_NGX_Version InSDKVersion);
typedef NVSDK_NGX_Result (*PFN_D3D12_Init_ProjectID)(const char* InProjectId, NVSDK_NGX_EngineType InEngineType,
                                                     const char* InEngineVersion, const wchar_t* InApplicationDataPath,
                                                     ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                                     const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result (*PFN_D3D12_Init_Ext)(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                               ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                               const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result (*PFN_D3D12_Shutdown)(void);
typedef NVSDK_NGX_Result (*PFN_D3D12_Shutdown1)(ID3D12Device* InDevice);
typedef NVSDK_NGX_Result (*PFN_D3D12_GetParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_D3D12_AllocateParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_D3D12_GetCapabilityParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_D3D12_DestroyParameters)(NVSDK_NGX_Parameter* InParameters);
typedef NVSDK_NGX_Result (*PFN_D3D12_GetScratchBufferSize)(NVSDK_NGX_Feature InFeatureId,
                                                           const NVSDK_NGX_Parameter* InParameters,
                                                           size_t* OutSizeInBytes);
typedef NVSDK_NGX_Result (*PFN_D3D12_CreateFeature)(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                                    NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle);
typedef NVSDK_NGX_Result (*PFN_D3D12_ReleaseFeature)(NVSDK_NGX_Handle* InHandle);
typedef NVSDK_NGX_Result (*PFN_D3D12_GetFeatureRequirements)(IDXGIAdapter* Adapter,
                                                             const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                             NVSDK_NGX_FeatureRequirement* OutSupported);
typedef NVSDK_NGX_Result (*PFN_D3D12_EvaluateFeature)(ID3D12GraphicsCommandList* InCmdList,
                                                      const NVSDK_NGX_Handle* InFeatureHandle,
                                                      const NVSDK_NGX_Parameter* InParameters,
                                                      PFN_NVSDK_NGX_ProgressCallback InCallback);
typedef NVSDK_NGX_Result (*PFN_D3D12_PopulateParameters_Impl)(NVSDK_NGX_Parameter* InParameters);

typedef NVSDK_NGX_Result (*PFN_UpdateFeature)(const NVSDK_NGX_Application_Identifier* ApplicationId,
                                              const NVSDK_NGX_Feature FeatureID);

typedef NVSDK_NGX_Result (*PFN_VULKAN_RequiredExtensions)(unsigned int* OutInstanceExtCount,
                                                          const char*** OutInstanceExts,
                                                          unsigned int* OutDeviceExtCount, const char*** OutDeviceExts);
typedef NVSDK_NGX_Result (*PFN_VULKAN_Init)(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                            VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                            PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                            const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                            NVSDK_NGX_Version InSDKVersion);
typedef NVSDK_NGX_Result (*PFN_VULKAN_Init_Ext)(unsigned long long InApplicationId,
                                                const wchar_t* InApplicationDataPath, VkInstance InInstance,
                                                VkPhysicalDevice InPD, VkDevice InDevice,
                                                NVSDK_NGX_Version InSDKVersion,
                                                const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result (*PFN_VULKAN_Init_Ext2)(unsigned long long InApplicationId,
                                                 const wchar_t* InApplicationDataPath, VkInstance InInstance,
                                                 VkPhysicalDevice InPD, VkDevice InDevice,
                                                 PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                                 NVSDK_NGX_Version InSDKVersion,
                                                 const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result (*PFN_VULKAN_Init_ProjectID_Ext)(
    const char* InProjectId, NVSDK_NGX_EngineType InEngineType, const char* InEngineVersion,
    const wchar_t* InApplicationDataPath, VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
    PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA, NVSDK_NGX_Version InSDKVersion,
    const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result (*PFN_VULKAN_Init_ProjectID)(const char* InProjectId, NVSDK_NGX_EngineType InEngineType,
                                                      const char* InEngineVersion, const wchar_t* InApplicationDataPath,
                                                      VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                                      PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                                                      NVSDK_NGX_Version InSDKVersion,
                                                      const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result (*PFN_VULKAN_Shutdown)(void);
typedef NVSDK_NGX_Result (*PFN_VULKAN_Shutdown1)(VkDevice InDevice);
typedef NVSDK_NGX_Result (*PFN_VULKAN_GetParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_VULKAN_AllocateParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_VULKAN_GetCapabilityParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_VULKAN_DestroyParameters)(NVSDK_NGX_Parameter* InParameters);
typedef NVSDK_NGX_Result (*PFN_VULKAN_GetScratchBufferSize)(NVSDK_NGX_Feature InFeatureId,
                                                            const NVSDK_NGX_Parameter* InParameters,
                                                            size_t* OutSizeInBytes);
typedef NVSDK_NGX_Result (*PFN_VULKAN_CreateFeature)(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Feature InFeatureID,
                                                     NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle);
typedef NVSDK_NGX_Result (*PFN_VULKAN_CreateFeature1)(VkDevice InDevice, VkCommandBuffer InCmdList,
                                                      NVSDK_NGX_Feature InFeatureID, NVSDK_NGX_Parameter* InParameters,
                                                      NVSDK_NGX_Handle** OutHandle);
typedef NVSDK_NGX_Result (*PFN_VULKAN_ReleaseFeature)(NVSDK_NGX_Handle* InHandle);
typedef NVSDK_NGX_Result (*PFN_VULKAN_GetFeatureRequirements)(
    const VkInstance Instance, const VkPhysicalDevice PhysicalDevice,
    const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo, NVSDK_NGX_FeatureRequirement* OutSupported);
typedef NVSDK_NGX_Result (*PFN_VULKAN_GetFeatureInstanceExtensionRequirements)(
    const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo, uint32_t* OutExtensionCount,
    VkExtensionProperties** OutExtensionProperties);
typedef NVSDK_NGX_Result (*PFN_VULKAN_GetFeatureDeviceExtensionRequirements)(
    VkInstance Instance, VkPhysicalDevice PhysicalDevice, const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
    uint32_t* OutExtensionCount, VkExtensionProperties** OutExtensionProperties);
typedef NVSDK_NGX_Result (*PFN_VULKAN_EvaluateFeature)(VkCommandBuffer InCmdList,
                                                       const NVSDK_NGX_Handle* InFeatureHandle,
                                                       const NVSDK_NGX_Parameter* InParameters,
                                                       PFN_NVSDK_NGX_ProgressCallback InCallback);
typedef NVSDK_NGX_Result (*PFN_VULKAN_PopulateParameters_Impl)(NVSDK_NGX_Parameter* InParameters);

// NGX runtime exports, the SDK lib passes a pointer that receives the remaining init count
typedef NVSDK_NGX_Result (*PFN_Nvngx_D3D11_Shutdown1)(ID3D11Device* InDevice, unsigned int* OutRemaining);
typedef NVSDK_NGX_Result (*PFN_Nvngx_D3D12_Shutdown1)(ID3D12Device* InDevice, unsigned int* OutRemaining);
typedef NVSDK_NGX_Result (*PFN_Nvngx_VULKAN_Shutdown1)(VkDevice InDevice, unsigned int* OutRemaining);

// Exports the NGX runtime (nvngx.dll) and the feature dlls (nvngx_dlssg, nvngx_dlssd) share with the same signatures.
// Shutdown1 isn't one of them, the runtime's takes an extra OutRemaining.
struct NgxExports
{
    HMODULE dll = nullptr;
    std::wstring filePath;

    PFN_D3D12_Init D3D12_Init = nullptr;
    PFN_D3D12_Init_Ext D3D12_Init_Ext = nullptr;
    PFN_D3D12_Shutdown D3D12_Shutdown = nullptr;
    PFN_D3D12_GetScratchBufferSize D3D12_GetScratchBufferSize = nullptr;
    PFN_D3D12_CreateFeature D3D12_CreateFeature = nullptr;
    PFN_D3D12_ReleaseFeature D3D12_ReleaseFeature = nullptr;
    PFN_D3D12_GetFeatureRequirements D3D12_GetFeatureRequirements = nullptr;
    PFN_D3D12_EvaluateFeature D3D12_EvaluateFeature = nullptr;

    PFN_VULKAN_Init VULKAN_Init = nullptr;
    PFN_VULKAN_Init_Ext VULKAN_Init_Ext = nullptr;
    PFN_VULKAN_Init_Ext2 VULKAN_Init_Ext2 = nullptr;
    PFN_VULKAN_Shutdown VULKAN_Shutdown = nullptr;
    PFN_VULKAN_GetScratchBufferSize VULKAN_GetScratchBufferSize = nullptr;
    PFN_VULKAN_CreateFeature VULKAN_CreateFeature = nullptr;
    PFN_VULKAN_CreateFeature1 VULKAN_CreateFeature1 = nullptr;
    PFN_VULKAN_ReleaseFeature VULKAN_ReleaseFeature = nullptr;
    PFN_VULKAN_GetFeatureRequirements VULKAN_GetFeatureRequirements = nullptr;
    PFN_VULKAN_EvaluateFeature VULKAN_EvaluateFeature = nullptr;

    // Custom provider's dll, or a plugin's resolver that LoadExports takes the exports from
    void SetSource(const CustomProviders::Exports& exports)
    {
        dll = exports.dll;
        filePath = exports.path.wstring();
        getProc = exports.getProc;
        getProcUserData = exports.userData;
    }

  protected:
    OptiGetProcFn getProc = nullptr;
    void* getProcUserData = nullptr;

    // KernelBase's original GetProcAddress, so no hook can hand out something else
    FARPROC Export(const char* prefix, const char* name) const
    {
        auto fullName = std::string(prefix) + name;

        if (getProc != nullptr)
            return (FARPROC) getProc(getProcUserData, fullName.c_str());

        return KernelBaseProxy::GetProcAddress_()(dll, fullName.c_str());
    }

    void LoadSharedExports(const char* prefix)
    {
        D3D12_Init = (PFN_D3D12_Init) Export(prefix, "D3D12_Init");
        D3D12_Init_Ext = (PFN_D3D12_Init_Ext) Export(prefix, "D3D12_Init_Ext");
        D3D12_Shutdown = (PFN_D3D12_Shutdown) Export(prefix, "D3D12_Shutdown");
        D3D12_GetScratchBufferSize = (PFN_D3D12_GetScratchBufferSize) Export(prefix, "D3D12_GetScratchBufferSize");
        D3D12_CreateFeature = (PFN_D3D12_CreateFeature) Export(prefix, "D3D12_CreateFeature");
        D3D12_ReleaseFeature = (PFN_D3D12_ReleaseFeature) Export(prefix, "D3D12_ReleaseFeature");
        D3D12_GetFeatureRequirements =
            (PFN_D3D12_GetFeatureRequirements) Export(prefix, "D3D12_GetFeatureRequirements");
        D3D12_EvaluateFeature = (PFN_D3D12_EvaluateFeature) Export(prefix, "D3D12_EvaluateFeature");

        VULKAN_Init = (PFN_VULKAN_Init) Export(prefix, "VULKAN_Init");
        VULKAN_Init_Ext = (PFN_VULKAN_Init_Ext) Export(prefix, "VULKAN_Init_Ext");
        VULKAN_Init_Ext2 = (PFN_VULKAN_Init_Ext2) Export(prefix, "VULKAN_Init_Ext2");
        VULKAN_Shutdown = (PFN_VULKAN_Shutdown) Export(prefix, "VULKAN_Shutdown");
        VULKAN_GetScratchBufferSize = (PFN_VULKAN_GetScratchBufferSize) Export(prefix, "VULKAN_GetScratchBufferSize");
        VULKAN_CreateFeature = (PFN_VULKAN_CreateFeature) Export(prefix, "VULKAN_CreateFeature");
        VULKAN_CreateFeature1 = (PFN_VULKAN_CreateFeature1) Export(prefix, "VULKAN_CreateFeature1");
        VULKAN_ReleaseFeature = (PFN_VULKAN_ReleaseFeature) Export(prefix, "VULKAN_ReleaseFeature");
        VULKAN_GetFeatureRequirements =
            (PFN_VULKAN_GetFeatureRequirements) Export(prefix, "VULKAN_GetFeatureRequirements");
        VULKAN_EvaluateFeature = (PFN_VULKAN_EvaluateFeature) Export(prefix, "VULKAN_EvaluateFeature");
    }
};

// nvngx_dlssg and nvngx_dlssd replacements
struct NgxFeatureModule : NgxExports
{
    PFN_D3D12_Shutdown1 D3D12_Shutdown1 = nullptr;
    PFN_D3D12_PopulateParameters_Impl D3D12_PopulateParameters_Impl = nullptr;

    PFN_VULKAN_Shutdown1 VULKAN_Shutdown1 = nullptr;
    PFN_VULKAN_PopulateParameters_Impl VULKAN_PopulateParameters_Impl = nullptr;

    // DLSS Enabler's headless dll has them with a DLSSG_ prefix
    void LoadExports(const char* prefix = "NVSDK_NGX_")
    {
        LoadSharedExports(prefix);

        D3D12_Shutdown1 = (PFN_D3D12_Shutdown1) Export(prefix, "D3D12_Shutdown1");
        D3D12_PopulateParameters_Impl =
            (PFN_D3D12_PopulateParameters_Impl) Export(prefix, "D3D12_PopulateParameters_Impl");

        VULKAN_Shutdown1 = (PFN_VULKAN_Shutdown1) Export(prefix, "VULKAN_Shutdown1");
        VULKAN_PopulateParameters_Impl =
            (PFN_VULKAN_PopulateParameters_Impl) Export(prefix, "VULKAN_PopulateParameters_Impl");
    }
};

// NGX runtime that runs DLSS
struct NvngxModule : NgxExports
{
    PFN_D3D11_Init D3D11_Init = nullptr;
    PFN_D3D11_Init_ProjectID D3D11_Init_ProjectID = nullptr;
    PFN_D3D11_Init_Ext D3D11_Init_Ext = nullptr;
    PFN_D3D11_Shutdown D3D11_Shutdown = nullptr;
    PFN_Nvngx_D3D11_Shutdown1 D3D11_Shutdown1 = nullptr;
    PFN_D3D11_GetParameters D3D11_GetParameters = nullptr;
    PFN_D3D11_AllocateParameters D3D11_AllocateParameters = nullptr;
    PFN_D3D11_GetCapabilityParameters D3D11_GetCapabilityParameters = nullptr;
    PFN_D3D11_DestroyParameters D3D11_DestroyParameters = nullptr;
    PFN_D3D11_GetScratchBufferSize D3D11_GetScratchBufferSize = nullptr;
    PFN_D3D11_CreateFeature D3D11_CreateFeature = nullptr;
    PFN_D3D11_ReleaseFeature D3D11_ReleaseFeature = nullptr;
    PFN_D3D11_GetFeatureRequirements D3D11_GetFeatureRequirements = nullptr;
    PFN_D3D11_EvaluateFeature D3D11_EvaluateFeature = nullptr;

    PFN_D3D12_Init_ProjectID D3D12_Init_ProjectID = nullptr;
    PFN_Nvngx_D3D12_Shutdown1 D3D12_Shutdown1 = nullptr;
    PFN_D3D12_GetParameters D3D12_GetParameters = nullptr;
    PFN_D3D12_AllocateParameters D3D12_AllocateParameters = nullptr;
    PFN_D3D12_GetCapabilityParameters D3D12_GetCapabilityParameters = nullptr;
    PFN_D3D12_DestroyParameters D3D12_DestroyParameters = nullptr;

    PFN_VULKAN_RequiredExtensions VULKAN_RequiredExtensions = nullptr;
    PFN_VULKAN_Init_ProjectID VULKAN_Init_ProjectID = nullptr;
    PFN_VULKAN_Init_ProjectID_Ext VULKAN_Init_ProjectID_Ext = nullptr;
    PFN_Nvngx_VULKAN_Shutdown1 VULKAN_Shutdown1 = nullptr;
    PFN_VULKAN_GetParameters VULKAN_GetParameters = nullptr;
    PFN_VULKAN_AllocateParameters VULKAN_AllocateParameters = nullptr;
    PFN_VULKAN_GetCapabilityParameters VULKAN_GetCapabilityParameters = nullptr;
    PFN_VULKAN_DestroyParameters VULKAN_DestroyParameters = nullptr;
    PFN_VULKAN_GetFeatureInstanceExtensionRequirements VULKAN_GetFeatureInstanceExtensionRequirements = nullptr;
    PFN_VULKAN_GetFeatureDeviceExtensionRequirements VULKAN_GetFeatureDeviceExtensionRequirements = nullptr;

    PFN_UpdateFeature UpdateFeature = nullptr;

    void LoadExports()
    {
        constexpr auto prefix = "NVSDK_NGX_";
        LoadSharedExports(prefix);

        D3D11_Init = (PFN_D3D11_Init) Export(prefix, "D3D11_Init");
        D3D11_Init_ProjectID = (PFN_D3D11_Init_ProjectID) Export(prefix, "D3D11_Init_ProjectID");
        D3D11_Init_Ext = (PFN_D3D11_Init_Ext) Export(prefix, "D3D11_Init_Ext");
        D3D11_Shutdown = (PFN_D3D11_Shutdown) Export(prefix, "D3D11_Shutdown");
        D3D11_Shutdown1 = (PFN_Nvngx_D3D11_Shutdown1) Export(prefix, "D3D11_Shutdown1");
        D3D11_GetParameters = (PFN_D3D11_GetParameters) Export(prefix, "D3D11_GetParameters");
        D3D11_AllocateParameters = (PFN_D3D11_AllocateParameters) Export(prefix, "D3D11_AllocateParameters");
        D3D11_GetCapabilityParameters =
            (PFN_D3D11_GetCapabilityParameters) Export(prefix, "D3D11_GetCapabilityParameters");
        D3D11_DestroyParameters = (PFN_D3D11_DestroyParameters) Export(prefix, "D3D11_DestroyParameters");
        D3D11_GetScratchBufferSize = (PFN_D3D11_GetScratchBufferSize) Export(prefix, "D3D11_GetScratchBufferSize");
        D3D11_CreateFeature = (PFN_D3D11_CreateFeature) Export(prefix, "D3D11_CreateFeature");
        D3D11_ReleaseFeature = (PFN_D3D11_ReleaseFeature) Export(prefix, "D3D11_ReleaseFeature");
        D3D11_GetFeatureRequirements =
            (PFN_D3D11_GetFeatureRequirements) Export(prefix, "D3D11_GetFeatureRequirements");
        D3D11_EvaluateFeature = (PFN_D3D11_EvaluateFeature) Export(prefix, "D3D11_EvaluateFeature");

        D3D12_Init_ProjectID = (PFN_D3D12_Init_ProjectID) Export(prefix, "D3D12_Init_ProjectID");
        D3D12_Shutdown1 = (PFN_Nvngx_D3D12_Shutdown1) Export(prefix, "D3D12_Shutdown1");
        D3D12_GetParameters = (PFN_D3D12_GetParameters) Export(prefix, "D3D12_GetParameters");
        D3D12_AllocateParameters = (PFN_D3D12_AllocateParameters) Export(prefix, "D3D12_AllocateParameters");
        D3D12_GetCapabilityParameters =
            (PFN_D3D12_GetCapabilityParameters) Export(prefix, "D3D12_GetCapabilityParameters");
        D3D12_DestroyParameters = (PFN_D3D12_DestroyParameters) Export(prefix, "D3D12_DestroyParameters");

        VULKAN_RequiredExtensions = (PFN_VULKAN_RequiredExtensions) Export(prefix, "VULKAN_RequiredExtensions");
        VULKAN_Init_ProjectID = (PFN_VULKAN_Init_ProjectID) Export(prefix, "VULKAN_Init_ProjectID");
        VULKAN_Init_ProjectID_Ext = (PFN_VULKAN_Init_ProjectID_Ext) Export(prefix, "VULKAN_Init_ProjectID_Ext");
        VULKAN_Shutdown1 = (PFN_Nvngx_VULKAN_Shutdown1) Export(prefix, "VULKAN_Shutdown1");
        VULKAN_GetParameters = (PFN_VULKAN_GetParameters) Export(prefix, "VULKAN_GetParameters");
        VULKAN_AllocateParameters = (PFN_VULKAN_AllocateParameters) Export(prefix, "VULKAN_AllocateParameters");
        VULKAN_GetCapabilityParameters =
            (PFN_VULKAN_GetCapabilityParameters) Export(prefix, "VULKAN_GetCapabilityParameters");
        VULKAN_DestroyParameters = (PFN_VULKAN_DestroyParameters) Export(prefix, "VULKAN_DestroyParameters");
        VULKAN_GetFeatureInstanceExtensionRequirements = (PFN_VULKAN_GetFeatureInstanceExtensionRequirements) Export(
            prefix, "VULKAN_GetFeatureInstanceExtensionRequirements");
        VULKAN_GetFeatureDeviceExtensionRequirements = (PFN_VULKAN_GetFeatureDeviceExtensionRequirements) Export(
            prefix, "VULKAN_GetFeatureDeviceExtensionRequirements");

        UpdateFeature = (PFN_UpdateFeature) Export(prefix, "UpdateFeature");
    }
};

// NGX runtime from custom/<name>/dlss or a plugin, only used by Opti's DLSS upscaler and never hooked
struct CustomNgxModule
{
    NvngxModule module;
    bool dx12Inited = false;
    bool vulkanInited = false;
};

class NVNGXProxy
{
  private:
    inline static NvngxModule _module;

    // std::map so the pointers handed out stay valid
    inline static std::map<std::string, CustomNgxModule> _customModules;
    inline static std::mutex _customMutex;

    struct FeaturePathList
    {
        std::vector<std::wstring> paths;
        std::vector<const wchar_t*> pointers;
    };

    // NGX might keep the pointers after init, so lists given to it are never freed
    inline static std::list<FeaturePathList> _featurePathLists;
    inline static std::vector<std::wstring> _gamePaths;
    inline static std::mutex _featurePathMutex;

    static void AddFeaturePath(std::vector<std::wstring>& paths, const std::filesystem::path& path)
    {
        auto normalize = [](const std::filesystem::path& p)
        {
            auto result = p.lexically_normal().wstring();

            if (result.size() > 1 && (result.back() == L'\\' || result.back() == L'/'))
                result.pop_back();

            to_lower_in_place(result);
            return result;
        };

        auto normalized = normalize(path);

        for (const auto& existing : paths)
        {
            if (normalize(existing) == normalized)
                return;
        }

        paths.push_back(path.wstring());
    }

    // UseGenericAppIdWithDlss also applies to the runtime started here
    static const char* ProjectIdForDlss()
    {
        return Config::Instance()->UseGenericAppIdWithDlss.value_or_default()
                   ? project_id_override
                   : State::Instance().NVNGX_ProjectId.c_str();
    }

    static unsigned long long AppIdForDlss()
    {
        return Config::Instance()->UseGenericAppIdWithDlss.value_or_default() ? app_id_override
                                                                              : State::Instance().NVNGX_ApplicationId;
    }

    static NVSDK_NGX_Result InitDx12Module(const NvngxModule& module, ID3D12Device* InDevice)
    {
        NVSDK_NGX_FeatureCommonInfo fcInfo {};
        GetFeatureCommonInfo(&fcInfo);
        NVSDK_NGX_Result nvResult = NVSDK_NGX_Result_Fail;

        if (State::Instance().NVNGX_ProjectId != "" && module.D3D12_Init_ProjectID != nullptr)
        {
            LOG_INFO("module.D3D12_Init_ProjectID!");

            nvResult = module.D3D12_Init_ProjectID(ProjectIdForDlss(), State::Instance().NVNGX_Engine,
                                                   State::Instance().NVNGX_EngineVersion.c_str(),
                                                   State::Instance().NVNGX_ApplicationDataPath.c_str(), InDevice,
                                                   State::Instance().NVNGX_Version, &fcInfo);
        }
        else if (module.D3D12_Init_Ext != nullptr)
        {
            LOG_INFO("module.D3D12_Init_Ext!");
            nvResult = module.D3D12_Init_Ext(AppIdForDlss(), State::Instance().NVNGX_ApplicationDataPath.c_str(),
                                             InDevice, State::Instance().NVNGX_Version, &fcInfo);
        }

        LOG_INFO("result: {0:X}", (UINT) nvResult);

        return nvResult;
    }

    static NVSDK_NGX_Result InitVulkanModule(const NvngxModule& module, VkInstance InInstance, VkPhysicalDevice InPD,
                                             VkDevice InDevice, PFN_vkGetInstanceProcAddr InGIPA,
                                             PFN_vkGetDeviceProcAddr InGDPA)
    {
        NVSDK_NGX_FeatureCommonInfo fcInfo {};
        GetFeatureCommonInfo(&fcInfo);
        NVSDK_NGX_Result nvResult = NVSDK_NGX_Result_Fail;

        if (State::Instance().NVNGX_ProjectId != "" && module.VULKAN_Init_ProjectID != nullptr)
        {
            LOG_DEBUG("module.VULKAN_Init_ProjectID!");
            nvResult = module.VULKAN_Init_ProjectID(
                ProjectIdForDlss(), State::Instance().NVNGX_Engine, State::Instance().NVNGX_EngineVersion.c_str(),
                State::Instance().NVNGX_ApplicationDataPath.c_str(), InInstance, InPD, InDevice, InGIPA, InGDPA,
                State::Instance().NVNGX_Version, &fcInfo);
        }
        else if (module.VULKAN_Init_Ext != nullptr)
        {
            LOG_DEBUG("module.VULKAN_Init_Ext!");
            nvResult = module.VULKAN_Init_Ext(AppIdForDlss(), State::Instance().NVNGX_ApplicationDataPath.c_str(),
                                              InInstance, InPD, InDevice, State::Instance().NVNGX_Version, &fcInfo);
        }

        LOG_DEBUG("result: {0:X}", (UINT) nvResult);

        return nvResult;
    }

    inline static bool _cudaInited = false;
    inline static bool _dx11Inited = false;
    inline static bool _dx12Inited = false;
    inline static bool _vulkanInited = false;

    inline static void LogCallback(const char* message, NVSDK_NGX_Logging_Level loggingLevel,
                                   NVSDK_NGX_Feature sourceComponent)
    {
        std::string logMessage(message);
        LOG_DEBUG("NVSDK Feature {}: {}", (UINT) sourceComponent, logMessage);
    }

  public:
    static void InitNVNGX(HMODULE nvngxModule = nullptr)
    {
        // if dll already loaded
        if (_module.dll != nullptr)
            return;

        LOG_INFO("");

        ScopedSkipDxgiLoadChecks scopedSkipDxgiLoadChecks {};

        if (nvngxModule != nullptr && _module.dll == nullptr)
        {
            _module.dll = nvngxModule;
        }

        std::vector<std::wstring> dllNames = { L"_nvngx.dll", L"nvngx.dll" };

        auto optiPath = Config::Instance()->MainDllPath.value();
        auto overridePath = Config::Instance()->NvngxPath.value_or(L"");

        if (_module.dll == nullptr)
        {
            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                HMODULE memModule = nullptr;
                Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &_module.dll);

                if (_module.dll != nullptr)
                {
                    break;
                }
            }
        }

        if (_module.dll == nullptr)
        {
            auto regNGXCorePath = Util::NvngxPath();
            if (regNGXCorePath.has_value())
            {
                overridePath = regNGXCorePath.value();

                for (size_t i = 0; i < dllNames.size(); i++)
                {
                    LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &_module.dll);

                    if (_module.dll != nullptr)
                    {
                        break;
                    }
                }
            }
        }

        if (_module.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(_module.dll, modulePath, MAX_PATH);
            _module.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(_module.filePath));
        }

        if (_module.dll != nullptr && _module.D3D12_Init_ProjectID == nullptr)
        {
            LOG_INFO("Getting nvngx method addresses");

            HookNgxApi(_module.dll);

            _module.LoadExports();
        }
    }

    // gamePaths replaces the stored game paths, they are reused by Opti's own inits
    static void SetFeaturePaths(NVSDK_NGX_FeatureCommonInfo* fcInfo, const NVSDK_NGX_PathListInfo* gamePaths = nullptr)
    {
        auto& state = State::Instance();
        auto config = Config::Instance();
        const bool dlssOverride = config->NVNGX_DLSS_Library.has_value();

        std::scoped_lock lock(_featurePathMutex);

        if (gamePaths != nullptr)
        {
            _gamePaths.clear();

            for (size_t i = 0; i < gamePaths->Length; i++)
                _gamePaths.push_back(gamePaths->Path[i]);
        }

        std::vector<std::wstring> paths;

        auto addDllFolder = [&paths](const std::optional<std::wstring>& dllPath)
        {
            if (dllPath.has_value())
                AddFeaturePath(paths, std::filesystem::path(dllPath.value()).parent_path());
        };

        if (config->DLSSFeaturePath.has_value())
            AddFeaturePath(paths, config->DLSSFeaturePath.value());

        if (dlssOverride)
            addDllFolder(state.NVNGX_DLSS_Path);

        AddFeaturePath(paths, config->MainDllPath.value());

        for (const auto& path : _gamePaths)
            AddFeaturePath(paths, path);

        AddFeaturePath(paths, Util::ExePath().remove_filename());

        if (!dlssOverride)
            addDllFolder(state.NVNGX_DLSS_Path);

        addDllFolder(state.NVNGX_DLSSD_Path);
        addDllFolder(state.NVNGX_DLSSG_Path);

        auto list = std::find_if(_featurePathLists.begin(), _featurePathLists.end(),
                                 [&paths](const FeaturePathList& existing) { return existing.paths == paths; });

        if (list == _featurePathLists.end())
        {
            list = _featurePathLists.insert(_featurePathLists.end(), FeaturePathList { std::move(paths) });

            for (const auto& path : list->paths)
                list->pointers.push_back(path.c_str());
        }

        for (size_t i = 0; i < list->paths.size(); ++i)
            LOG_DEBUG("Feature Path [{}]: {}", i, wstring_to_string(list->paths[i]));

        fcInfo->PathListInfo.Path = list->pointers.data();
        fcInfo->PathListInfo.Length = static_cast<unsigned int>(list->pointers.size());
    }

    static void GetFeatureCommonInfo(NVSDK_NGX_FeatureCommonInfo* fcInfo)
    {
        SetFeaturePaths(fcInfo);

        // Config logging
        fcInfo->LoggingInfo.MinimumLoggingLevel =
            Config::Instance()->LogLevel < 2 ? NVSDK_NGX_LOGGING_LEVEL_VERBOSE : NVSDK_NGX_LOGGING_LEVEL_ON;
        fcInfo->LoggingInfo.LoggingCallback = LogCallback;
        fcInfo->LoggingInfo.DisableOtherLoggingSinks = true;
    }

    static HMODULE NVNGXModule() { return _module.dll; }
    static std::wstring NVNGXModule_Path() { return _module.filePath; }

    static bool IsNVNGXInited()
    {
        return _module.dll != nullptr && (_dx11Inited || _dx12Inited || _vulkanInited) &&
               Config::Instance()->DLSSEnabled.value_or_default();
    }

    // DirectX11
    static bool InitDx11(ID3D11Device* InDevice)
    {
        if (_dx11Inited)
            return true;

        InitNVNGX();

        if (_module.dll == nullptr)
            return false;

        NVSDK_NGX_FeatureCommonInfo fcInfo {};
        GetFeatureCommonInfo(&fcInfo);
        NVSDK_NGX_Result nvResult = NVSDK_NGX_Result_Fail;

        if (State::Instance().NVNGX_ProjectId != "" && _module.D3D11_Init_ProjectID != nullptr)
        {
            LOG_DEBUG("_module.D3D11_Init_ProjectID!");

            nvResult = _module.D3D11_Init_ProjectID(ProjectIdForDlss(), State::Instance().NVNGX_Engine,
                                                    State::Instance().NVNGX_EngineVersion.c_str(),
                                                    State::Instance().NVNGX_ApplicationDataPath.c_str(), InDevice,
                                                    State::Instance().NVNGX_Version, &fcInfo);
        }
        else if (_module.D3D11_Init_Ext != nullptr)
        {
            LOG_DEBUG("_module.D3D11_Init_Ext!");
            nvResult = _module.D3D11_Init_Ext(AppIdForDlss(), State::Instance().NVNGX_ApplicationDataPath.c_str(),
                                              InDevice, State::Instance().NVNGX_Version, &fcInfo);
        }

        LOG_DEBUG("result: {0:X}", (UINT) nvResult);

        _dx11Inited = (nvResult == NVSDK_NGX_Result_Success);
        return _dx11Inited;
    }

    static void SetDx11Inited(bool value) { _dx11Inited = value; }

    static bool IsDx11Inited() { return _dx11Inited; }

    static PFN_D3D11_Init_ProjectID D3D11_Init_ProjectID() { return _module.D3D11_Init_ProjectID; }

    static PFN_D3D11_Init D3D11_Init() { return _module.D3D11_Init; }

    static PFN_D3D11_Init_Ext D3D11_Init_Ext() { return _module.D3D11_Init_Ext; }

    static PFN_D3D11_GetFeatureRequirements D3D11_GetFeatureRequirements()
    {
        return _module.D3D11_GetFeatureRequirements;
    }

    static PFN_D3D11_GetCapabilityParameters D3D11_GetCapabilityParameters()
    {
        return _module.D3D11_GetCapabilityParameters;
    }

    static PFN_D3D11_AllocateParameters D3D11_AllocateParameters() { return _module.D3D11_AllocateParameters; }

    static PFN_D3D11_GetParameters D3D11_GetParameters() { return _module.D3D11_GetParameters; }

    static PFN_D3D11_DestroyParameters D3D11_DestroyParameters()
    {
        if (!_dx11Inited)
            return nullptr;

        return _module.D3D11_DestroyParameters;
    }

    static PFN_D3D11_CreateFeature D3D11_CreateFeature()
    {
        if (!_dx11Inited)
            return nullptr;

        return _module.D3D11_CreateFeature;
    }

    static PFN_D3D11_EvaluateFeature D3D11_EvaluateFeature()
    {
        if (!_dx11Inited)
            return nullptr;

        return _module.D3D11_EvaluateFeature;
    }

    static PFN_D3D11_ReleaseFeature D3D11_ReleaseFeature()
    {
        if (!_dx11Inited)
            return nullptr;

        return _module.D3D11_ReleaseFeature;
    }

    static PFN_D3D11_Shutdown D3D11_Shutdown()
    {
        if (!_dx11Inited)
            return nullptr;

        return _module.D3D11_Shutdown;
    }

    static PFN_Nvngx_D3D11_Shutdown1 D3D11_Shutdown1()
    {
        if (!_dx11Inited)
            return nullptr;

        return _module.D3D11_Shutdown1;
    }

    // DirectX12
    static bool InitDx12(ID3D12Device* InDevice)
    {
        if (_dx12Inited)
            return true;

        InitNVNGX();

        if (_module.dll == nullptr)
            return false;

        _dx12Inited = InitDx12Module(_module, InDevice) == NVSDK_NGX_Result_Success;
        return _dx12Inited;
    }

    // Loading is only tried once per name, nullptr if it failed or it can't run DLSS on the API
    static CustomNgxModule* LoadCustom(const std::string& name, API api)
    {
        std::scoped_lock lock(_customMutex);

        auto [it, inserted] = _customModules.try_emplace(name);
        auto& custom = it->second;
        auto& module = custom.module;

        if (inserted)
        {
            auto exports = CustomProviders::Load(name, CustomProviders::Kind::Dlss);

            if (!exports)
                return nullptr;

            module.SetSource(exports);
            module.LoadExports();
        }

        bool usable = false;

        if (api == API::Vulkan)
            usable = (module.VULKAN_Init_ProjectID != nullptr || module.VULKAN_Init_Ext != nullptr) &&
                     module.VULKAN_CreateFeature != nullptr && module.VULKAN_EvaluateFeature != nullptr &&
                     module.VULKAN_ReleaseFeature != nullptr;
        else
            usable = (module.D3D12_Init_ProjectID != nullptr || module.D3D12_Init_Ext != nullptr) &&
                     module.D3D12_CreateFeature != nullptr && module.D3D12_EvaluateFeature != nullptr &&
                     module.D3D12_ReleaseFeature != nullptr;

        return usable ? &custom : nullptr;
    }

    // Inited once until ShutdownCustomDx12
    static bool InitCustomDx12(CustomNgxModule& custom, ID3D12Device* InDevice)
    {
        if (!custom.dx12Inited)
            custom.dx12Inited = InitDx12Module(custom.module, InDevice) == NVSDK_NGX_Result_Success;

        return custom.dx12Inited;
    }

    static void ShutdownCustomDx12(CustomNgxModule& custom, ID3D12Device* InDevice)
    {
        if (!custom.dx12Inited)
            return;

        if (custom.module.D3D12_Shutdown != nullptr)
            custom.module.D3D12_Shutdown();
        else if (custom.module.D3D12_Shutdown1 != nullptr)
        {
            unsigned int remaining = 0;
            custom.module.D3D12_Shutdown1(InDevice, &remaining);
        }

        custom.dx12Inited = false;
    }

    // Inited once, like NVIDIA's runtime on Vulkan
    static bool InitCustomVulkan(CustomNgxModule& custom, VkInstance InInstance, VkPhysicalDevice InPD,
                                 VkDevice InDevice, PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA)
    {
        if (!custom.vulkanInited)
            custom.vulkanInited =
                InitVulkanModule(custom.module, InInstance, InPD, InDevice, InGIPA, InGDPA) == NVSDK_NGX_Result_Success;

        return custom.vulkanInited;
    }

    static void SetDx12Inited(bool value) { _dx12Inited = value; }

    static bool IsDx12Inited() { return _dx12Inited; }

    static PFN_D3D12_Init_ProjectID D3D12_Init_ProjectID() { return _module.D3D12_Init_ProjectID; }

    static PFN_D3D12_Init D3D12_Init() { return _module.D3D12_Init; }

    static PFN_D3D12_Init_Ext D3D12_Init_Ext() { return _module.D3D12_Init_Ext; }

    static PFN_D3D12_GetFeatureRequirements D3D12_GetFeatureRequirements()
    {
        return _module.D3D12_GetFeatureRequirements;
    }

    static PFN_D3D12_GetCapabilityParameters D3D12_GetCapabilityParameters()
    {
        return _module.D3D12_GetCapabilityParameters;
    }

    static PFN_D3D12_AllocateParameters D3D12_AllocateParameters() { return _module.D3D12_AllocateParameters; }

    static PFN_D3D12_GetParameters D3D12_GetParameters() { return _module.D3D12_GetParameters; }

    static PFN_D3D12_DestroyParameters D3D12_DestroyParameters()
    {
        if (!_dx12Inited)
            return nullptr;

        return _module.D3D12_DestroyParameters;
    }

    static PFN_D3D12_CreateFeature D3D12_CreateFeature()
    {
        if (!_dx12Inited)
            return nullptr;

        return _module.D3D12_CreateFeature;
    }

    static PFN_D3D12_EvaluateFeature D3D12_EvaluateFeature()
    {
        if (!_dx12Inited)
            return nullptr;

        return _module.D3D12_EvaluateFeature;
    }

    static PFN_D3D12_ReleaseFeature D3D12_ReleaseFeature()
    {
        if (!_dx12Inited)
            return nullptr;

        return _module.D3D12_ReleaseFeature;
    }

    static PFN_D3D12_Shutdown D3D12_Shutdown()
    {
        if (!_dx12Inited)
            return nullptr;

        return _module.D3D12_Shutdown;
    }

    static PFN_Nvngx_D3D12_Shutdown1 D3D12_Shutdown1()
    {
        if (!_dx12Inited)
            return nullptr;

        return _module.D3D12_Shutdown1;
    }

    // Vulkan
    static bool InitVulkan(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                           PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA)
    {
        if (_vulkanInited)
            return true;

        InitNVNGX();

        if (_module.dll == nullptr)
            return false;

        _vulkanInited =
            InitVulkanModule(_module, InInstance, InPD, InDevice, InGIPA, InGDPA) == NVSDK_NGX_Result_Success;

        return _vulkanInited;
    }

    static void SetVulkanInited(bool value) { _vulkanInited = value; }

    static bool IsVulkanInited() { return _vulkanInited; }

    static PFN_VULKAN_Init_ProjectID VULKAN_Init_ProjectID() { return _module.VULKAN_Init_ProjectID; }

    static PFN_VULKAN_Init_ProjectID_Ext VULKAN_Init_ProjectID_Ext() { return _module.VULKAN_Init_ProjectID_Ext; }

    static PFN_VULKAN_Init_Ext VULKAN_Init_Ext() { return _module.VULKAN_Init_Ext; }

    static PFN_VULKAN_Init_Ext2 VULKAN_Init_Ext2() { return _module.VULKAN_Init_Ext2; }

    static PFN_VULKAN_Init VULKAN_Init() { return _module.VULKAN_Init; }

    static PFN_VULKAN_GetFeatureDeviceExtensionRequirements VULKAN_GetFeatureDeviceExtensionRequirements()
    {
        return _module.VULKAN_GetFeatureDeviceExtensionRequirements;
    }

    static PFN_VULKAN_GetFeatureInstanceExtensionRequirements VULKAN_GetFeatureInstanceExtensionRequirements()
    {
        return _module.VULKAN_GetFeatureInstanceExtensionRequirements;
    }

    static PFN_VULKAN_GetFeatureRequirements VULKAN_GetFeatureRequirements()
    {
        return _module.VULKAN_GetFeatureRequirements;
    }

    static PFN_VULKAN_GetCapabilityParameters VULKAN_GetCapabilityParameters()
    {
        return _module.VULKAN_GetCapabilityParameters;
    }

    static PFN_VULKAN_AllocateParameters VULKAN_AllocateParameters() { return _module.VULKAN_AllocateParameters; }

    static PFN_VULKAN_GetParameters VULKAN_GetParameters() { return _module.VULKAN_GetParameters; }

    static PFN_VULKAN_DestroyParameters VULKAN_DestroyParameters()
    {
        if (!_vulkanInited)
            return nullptr;

        return _module.VULKAN_DestroyParameters;
    }

    static PFN_VULKAN_CreateFeature VULKAN_CreateFeature()
    {
        if (!_vulkanInited)
            return nullptr;

        return _module.VULKAN_CreateFeature;
    }

    static PFN_VULKAN_CreateFeature1 VULKAN_CreateFeature1()
    {
        if (!_vulkanInited)
            return nullptr;

        return _module.VULKAN_CreateFeature1;
    }

    static PFN_VULKAN_EvaluateFeature VULKAN_EvaluateFeature()
    {
        if (!_vulkanInited)
            return nullptr;

        return _module.VULKAN_EvaluateFeature;
    }

    static PFN_VULKAN_ReleaseFeature VULKAN_ReleaseFeature()
    {
        if (!_vulkanInited)
            return nullptr;

        return _module.VULKAN_ReleaseFeature;
    }

    static PFN_VULKAN_Shutdown VULKAN_Shutdown()
    {
        if (!_vulkanInited)
            return nullptr;

        return _module.VULKAN_Shutdown;
    }

    static PFN_Nvngx_VULKAN_Shutdown1 VULKAN_Shutdown1()
    {
        if (!_vulkanInited)
            return nullptr;

        return _module.VULKAN_Shutdown1;
    }

    static PFN_UpdateFeature UpdateFeature()
    {
        if (_module.dll == nullptr)
            InitNVNGX();

        return _module.UpdateFeature;
    }
};
