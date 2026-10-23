#include "pch.h"

#include "Vulkan_Hooks.h"

#include <Util.h>
#include <Config.h>
#include <SysUtils.h>

#include <menu/menu_overlay_vk.h>
#include <menu/menu_common.h>
#include <proxies/KernelBase_Proxy.h>
#include <upscalers/IFeature_Vk.h>
#include <framegen/ffx/FSRFG_Vk.h>
#include <framegen/dlssg/DLSSG_Vk.h>
#include <framegen/reprojection/Reprojection_Vk.h>
#include <framegen/xefg/XeFG_Vk.h>
#include <fsr4/FSR4DriverProviderVk.h>

#include <misc/FrameLimit.h>
#include <misc/DeferredRelease.h>
#include "Reflex_Hooks.h"

#include <spoofing/Vulkan_Spoofing.h>
#include <hudfix/Hudfix_Vk.h>

#include <vulkan/vulkan.h>

#include <unordered_map>

#include <detours/detours.h>
#include <misc/IdentifyGpu.h>

#include "Hook_Utils.h"

#include <low_latency/input/input_common.h>
#include <low_latency/input/input_reflex_vk.h>
#include <low_latency/input/input_antilag_vk.h>

#pragma intrinsic(_ReturnAddress)

// for menu rendering
static VkDevice _device = VK_NULL_HANDLE;
static VkInstance _instance = VK_NULL_HANDLE;
static VkPhysicalDevice _PD = VK_NULL_HANDLE;
static HWND _hwnd = nullptr;

static std::mutex _vkApiVersionMutex;
static std::unordered_map<VkInstance, uint32_t> _instanceApiVersions;
static std::unordered_map<VkInstance, uint32_t> _instanceUsedApiVersions; // after raising it for FG
static std::unordered_map<VkPhysicalDevice, VkInstance> _physicalDeviceInstances;

static std::mutex _surfaceWindowsMutex;
static std::unordered_map<VkSurfaceKHR, HWND> _surfaceWindows;

static std::mutex _vkPresentMutex;

PFN_vkCreateDevice o_vkCreateDevice = nullptr;
PFN_vkCreateInstance o_vkCreateInstance = nullptr;
PFN_vkCreateWin32SurfaceKHR o_vkCreateWin32SurfaceKHR = nullptr;
static PFN_vkDestroyInstance o_vkDestroyInstance = nullptr;
PFN_vkQueuePresentKHR o_QueuePresentKHR = nullptr;
PFN_vkCreateSwapchainKHR o_CreateSwapchainKHR = nullptr;
static PFN_vkDestroySwapchainKHR o_DestroySwapchainKHR = nullptr;
static PFN_vkGetSwapchainImagesKHR o_GetSwapchainImagesKHR = nullptr;
static PFN_vkAcquireNextImageKHR o_AcquireNextImageKHR = nullptr;
static PFN_vkAcquireNextImage2KHR o_AcquireNextImage2KHR = nullptr;
static PFN_vkSetHdrMetadataEXT o_SetHdrMetadataEXT = nullptr;
static PFN_vkWaitForPresentKHR o_WaitForPresentKHR = nullptr;
static PFN_vkWaitForPresent2KHR o_WaitForPresent2KHR = nullptr;
static PFN_vkGetInstanceProcAddr o_vkGetInstanceProcAddr = nullptr;
static PFN_vkGetDeviceProcAddr o_vkGetDeviceProcAddr = nullptr;
static PFN_vkEnumeratePhysicalDevices o_vkEnumeratePhysicalDevices = nullptr;
static PFN_vkEnumeratePhysicalDeviceGroups o_vkEnumeratePhysicalDeviceGroups = nullptr;
static PFN_vkEnumeratePhysicalDeviceGroupsKHR o_vkEnumeratePhysicalDeviceGroupsKHR = nullptr;

// Those aren't hooked, just grabbed for use
static PFN_vkGetPhysicalDeviceFeatures2 o_vkGetPhysicalDeviceFeatures2 = nullptr;
PFN_vkCreateSemaphore VulkanHooks::o_vkCreateSemaphore = nullptr;
PFN_vkAntiLagUpdateAMD VulkanHooks::o_vkAntiLagUpdateAMD = nullptr;

// The device with VK_NV_low_latency2 enabled
static VkDevice _lowLatency2Device = VK_NULL_HANDLE;

// Set while the FG output handles a swapchain call
static thread_local bool _inFGCall = false;

struct ScopedFGCall
{
    ScopedFGCall() { _inFGCall = true; }
    ~ScopedFGCall() { _inFGCall = false; }
};

// FFX's and Streamline's swapchains call the driver through these hooks, those go straight to it.
// Streamline's DLSSG also presents the generated frames from its own thread.
static bool FromFGOutput(void* returnAddress)
{
    if (_inFGCall)
        return true;

    auto& state = State::Instance();
    return state.optiSlInterposer != nullptr && state.IsOptiStreamlineModule(Util::GetCallerModule(returnAddress));
}

// FG outputs present the real and the generated frames through this
static VkResult FGOutputQueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    MenuCommon::FGOutputPresent();
    return o_QueuePresentKHR(queue, pPresentInfo);
}

// OptiScaler's Streamline calls the driver's swapchain functions directly. Overlays like RTSS hook the same ones on top
// of OptiScaler, through them its calls, also those from its own present thread, would look like the game's.
static PFN_vkVoidFunction StreamlineSwapchainFunction(const char* pName, void* returnAddress)
{
    auto func = VulkanHooks::OriginalSwapchainFunction(pName);

    if (func == nullptr || !State::Instance().IsOptiStreamlineModule(Util::GetCallerModule(returnAddress)))
        return nullptr;

    return func;
}

// VK_NV_low_latency2's sleep signals a timeline semaphore
static bool TimelineSemaphoresEnabled(const VkDeviceCreateInfo* pCreateInfo)
{
    for (auto next = (const VkBaseInStructure*) pCreateInfo->pNext; next != nullptr; next = next->pNext)
    {
        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES &&
            ((const VkPhysicalDeviceVulkan12Features*) next)->timelineSemaphore)
        {
            return true;
        }

        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES &&
            ((const VkPhysicalDeviceTimelineSemaphoreFeatures*) next)->timelineSemaphore)
        {
            return true;
        }
    }

    return false;
}

// Forward declaration
static VkResult hkvkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
static VkResult hkvkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                       const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
static void hkvkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator);
static VkResult hkvkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount,
                                          VkImage* pSwapchainImages);
static VkResult hkvkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                        VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);
static VkResult hkvkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo,
                                         uint32_t* pImageIndex);
static void hkvkSetHdrMetadataEXT(VkDevice device, uint32_t swapchainCount, const VkSwapchainKHR* pSwapchains,
                                  const VkHdrMetadataEXT* pMetadata);
static VkResult hkvkWaitForPresentKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t presentId, uint64_t timeout);
static VkResult hkvkWaitForPresent2KHR(VkDevice device, VkSwapchainKHR swapchain,
                                       const VkPresentWait2InfoKHR* pPresentWait2Info);

// Swapchain calls coming from dxvk/vkd3d-proton's DXGI based modules
// For those the menu is drawn on the DXGI swapchain instead
static bool IsDxgiBackedCaller(void* returnAddress)
{
    // dxvk and vkd3d-proton present from their own threads
    thread_local HMODULE lastModule = nullptr;
    thread_local bool lastResult = false;

    auto module = Util::GetCallerModule(returnAddress);

    if (module == nullptr || module == dllModule)
        return false;

    if (module == lastModule)
        return lastResult;

    auto name = Util::WhoIsTheCaller(returnAddress);
    to_lower_in_place(name);

    lastModule = module;
    lastResult = name == "dxgi.dll" || name == "d3d11.dll" || name == "d3d10core.dll" || name == "d3d12.dll" ||
                 name == "d3d12core.dll";

    LOG_DEBUG("Swapchain caller: {}, DXGI backed: {}", name, lastResult);

    return lastResult;
}

// The game's low latency calls go to the low latency inputs, nullptr for the other functions. dxvk and vkd3d-proton
// use them for the D3D games' own, those are the D3D inputs.
static PFN_vkVoidFunction LowLatencyInputHook(const char* pName, PFN_vkVoidFunction orgFunc, void* returnAddress)
{
    if (pName == nullptr || State::Instance().vulkanSkipHooks)
        return nullptr;

    // Also captured here in case the device was created before OptiScaler's hooks
    auto hook = [&]<typename PFN>(PFN& original, auto input) -> PFN_vkVoidFunction
    {
        if (IsDxgiBackedCaller(returnAddress))
            return nullptr;

        if (original == nullptr)
            original = (PFN) orgFunc;

        return (PFN_vkVoidFunction) input;
    };

    if (std::strcmp(pName, "vkSetLatencySleepModeNV") == 0)
        return hook(VulkanHooks::o_vkSetLatencySleepModeNV, &InputReflexVk::SetLatencySleepMode);

    if (std::strcmp(pName, "vkLatencySleepNV") == 0)
        return hook(VulkanHooks::o_vkLatencySleepNV, &InputReflexVk::LatencySleep);

    if (std::strcmp(pName, "vkSetLatencyMarkerNV") == 0)
        return hook(VulkanHooks::o_vkSetLatencyMarkerNV, &InputReflexVk::SetLatencyMarker);

    if (std::strcmp(pName, "vkGetLatencyTimingsNV") == 0)
        return hook(VulkanHooks::o_vkGetLatencyTimingsNV, &InputReflexVk::GetLatencyTimings);

    if (std::strcmp(pName, "vkQueueNotifyOutOfBandNV") == 0)
        return hook(VulkanHooks::o_vkQueueNotifyOutOfBandNV, &InputReflexVk::QueueNotifyOutOfBand);

    if (std::strcmp(pName, "vkAntiLagUpdateAMD") == 0)
        return hook(VulkanHooks::o_vkAntiLagUpdateAMD, &InputAntiLagVk::AntiLagUpdate);

    return nullptr;
}

static bool EnableRadvCooperativeMatrix2()
{
    using PFN_wine_set_unix_env = LONG(WINAPI*)(const char*, const char*);

    const auto getModuleHandleW = KernelBaseProxy::GetModuleHandleW_();
    const auto getProcAddress = KernelBaseProxy::GetProcAddress_();
    if (!getModuleHandleW || !getProcAddress)
        return false;

    const auto ntdll = getModuleHandleW(L"ntdll.dll");
    if (!ntdll)
        return false;

    const auto setUnixEnv = reinterpret_cast<PFN_wine_set_unix_env>(getProcAddress(ntdll, "__wine_set_unix_env"));
    return setUnixEnv && setUnixEnv("radv_cooperative_matrix2_nv", "true") >= 0;
}

// Core private-data functions, detoured as games call them straight from vulkan-1.dll. Answered for XeFG's swapchain.
static PFN_vkSetPrivateData o_vkSetPrivateData = nullptr;
static PFN_vkGetPrivateData o_vkGetPrivateData = nullptr;
static VkResult VKAPI_CALL hkvkSetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle,
                                              VkPrivateDataSlot privateDataSlot, uint64_t data);
static void VKAPI_CALL hkvkGetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle,
                                          VkPrivateDataSlot privateDataSlot, uint64_t* pData);

static void HookDevice(VkDevice InDevice)
{
    if (o_CreateSwapchainKHR != nullptr || State::Instance().vulkanSkipHooks)
        return;

    LOG_FUNC();

    o_QueuePresentKHR = (PFN_vkQueuePresentKHR) (vkGetDeviceProcAddr(InDevice, "vkQueuePresentKHR"));
    o_CreateSwapchainKHR = (PFN_vkCreateSwapchainKHR) (vkGetDeviceProcAddr(InDevice, "vkCreateSwapchainKHR"));
    o_DestroySwapchainKHR = (PFN_vkDestroySwapchainKHR) (vkGetDeviceProcAddr(InDevice, "vkDestroySwapchainKHR"));
    o_GetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR) (vkGetDeviceProcAddr(InDevice, "vkGetSwapchainImagesKHR"));
    o_AcquireNextImageKHR = (PFN_vkAcquireNextImageKHR) (vkGetDeviceProcAddr(InDevice, "vkAcquireNextImageKHR"));
    o_AcquireNextImage2KHR = (PFN_vkAcquireNextImage2KHR) (vkGetDeviceProcAddr(InDevice, "vkAcquireNextImage2KHR"));
    o_SetHdrMetadataEXT = (PFN_vkSetHdrMetadataEXT) (vkGetDeviceProcAddr(InDevice, "vkSetHdrMetadataEXT"));
    o_WaitForPresentKHR = (PFN_vkWaitForPresentKHR) (vkGetDeviceProcAddr(InDevice, "vkWaitForPresentKHR"));
    o_WaitForPresent2KHR = (PFN_vkWaitForPresent2KHR) (vkGetDeviceProcAddr(InDevice, "vkWaitForPresent2KHR"));
    o_vkSetPrivateData = (PFN_vkSetPrivateData) (vkGetDeviceProcAddr(InDevice, "vkSetPrivateData"));
    o_vkGetPrivateData = (PFN_vkGetPrivateData) (vkGetDeviceProcAddr(InDevice, "vkGetPrivateData"));

    if (o_CreateSwapchainKHR)
    {
        LOG_DEBUG("Hooking VkDevice");

        // Hook
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_QueuePresentKHR != nullptr)
            DetourAttach(&(PVOID&) o_QueuePresentKHR, hkvkQueuePresentKHR);

        if (o_CreateSwapchainKHR != nullptr)
            DetourAttach(&(PVOID&) o_CreateSwapchainKHR, hkvkCreateSwapchainKHR);

        if (o_DestroySwapchainKHR != nullptr)
            DetourAttach(&(PVOID&) o_DestroySwapchainKHR, hkvkDestroySwapchainKHR);

        if (o_GetSwapchainImagesKHR != nullptr)
            DetourAttach(&(PVOID&) o_GetSwapchainImagesKHR, hkvkGetSwapchainImagesKHR);

        if (o_AcquireNextImageKHR != nullptr)
            DetourAttach(&(PVOID&) o_AcquireNextImageKHR, hkvkAcquireNextImageKHR);

        if (o_AcquireNextImage2KHR != nullptr)
            DetourAttach(&(PVOID&) o_AcquireNextImage2KHR, hkvkAcquireNextImage2KHR);

        if (o_SetHdrMetadataEXT != nullptr)
            DetourAttach(&(PVOID&) o_SetHdrMetadataEXT, hkvkSetHdrMetadataEXT);

        if (o_WaitForPresentKHR != nullptr)
            DetourAttach(&(PVOID&) o_WaitForPresentKHR, hkvkWaitForPresentKHR);

        if (o_WaitForPresent2KHR != nullptr)
            DetourAttach(&(PVOID&) o_WaitForPresent2KHR, hkvkWaitForPresent2KHR);

        if (o_vkSetPrivateData != nullptr)
            DetourAttach(&(PVOID&) o_vkSetPrivateData, hkvkSetPrivateData);

        if (o_vkGetPrivateData != nullptr)
            DetourAttach(&(PVOID&) o_vkGetPrivateData, hkvkGetPrivateData);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook VkDevice, error code: {:X}", detourResult);
            o_QueuePresentKHR = nullptr;
            o_CreateSwapchainKHR = nullptr;
            o_DestroySwapchainKHR = nullptr;
            o_GetSwapchainImagesKHR = nullptr;
            o_AcquireNextImageKHR = nullptr;
            o_AcquireNextImage2KHR = nullptr;
            o_SetHdrMetadataEXT = nullptr;
            o_WaitForPresentKHR = nullptr;
            o_WaitForPresent2KHR = nullptr;
            o_vkSetPrivateData = nullptr;
            o_vkGetPrivateData = nullptr;
        }
    }
}

// Created with the device as the output's queues have to be added to it
static IFGFeature_Vk* CreateFGFeature()
{
    auto& state = State::Instance();

    const bool supportedInput = state.activeFgInput == FGInput::DLSSG || state.activeFgInput == FGInput::Upscaler;

    if (state.currentVkFG != nullptr || !supportedInput)
        return state.currentVkFG;

    if (state.activeFgOutput == FGOutput::FSRFG)
    {
        // Driver provided ML FG works without amd_fidelityfx_vk.dll
        bool sdkLoaded = FfxApiProxy::InitFfxVk();
        bool driverLoaded = FSR4DriverProviderVk::InitFg();

        if (sdkLoaded || driverLoaded)
            state.currentVkFG = new FSRFG_Vk();
        else
            LOG_ERROR("Can't load amd_fidelityfx_vk.dll, FSR FG won't be available");
    }
    else if (state.activeFgOutput == FGOutput::DLSSG)
    {
        if (StreamlineProxy::InitWithVulkan())
            state.currentVkFG = new DLSSG_Vk();
        else
            LOG_ERROR("Can't init Streamline, DLSSG won't be available");
    }
    else if (state.activeFgOutput == FGOutput::Reprojection)
    {
        state.currentVkFG = new Reprojection_Vk();
    }
    else if (state.activeFgOutput == FGOutput::XeFG)
    {
        // XeFG is D3D12 only, the engine runs it on Vulkan
        if (XeFG_Vk::EngineApi() != nullptr)
            state.currentVkFG = new XeFG_Vk();
        else
            LOG_ERROR("XeFG Vulkan engine isn't loaded, XeFG won't be available");
    }

    return state.currentVkFG;
}

// swapchain_maintenance1's image release on XeFG's swapchain goes to its engine, the driver doesn't know that handle
static PFN_vkReleaseSwapchainImagesKHR o_vkReleaseSwapchainImagesKHR = nullptr;
static PFN_vkReleaseSwapchainImagesEXT o_vkReleaseSwapchainImagesEXT = nullptr;

static XeFG_Vk* XeFGOwning(VkSwapchainKHR swapchain)
{
    auto fg = dynamic_cast<XeFG_Vk*>(State::Instance().currentVkFG);
    return fg != nullptr && fg->OwnsSwapchain(swapchain) ? fg : nullptr;
}

static VkResult VKAPI_CALL hkvkReleaseSwapchainImagesKHR(VkDevice device,
                                                         const VkReleaseSwapchainImagesInfoKHR* pReleaseInfo)
{
    if (auto fg = pReleaseInfo != nullptr ? XeFGOwning(pReleaseInfo->swapchain) : nullptr)
        return fg->ReleaseSwapchainImages(device, pReleaseInfo->swapchain, pReleaseInfo->imageIndexCount,
                                          pReleaseInfo->pImageIndices);

    return o_vkReleaseSwapchainImagesKHR(device, pReleaseInfo);
}

static VkResult VKAPI_CALL hkvkReleaseSwapchainImagesEXT(VkDevice device,
                                                         const VkReleaseSwapchainImagesInfoEXT* pReleaseInfo)
{
    if (auto fg = pReleaseInfo != nullptr ? XeFGOwning(pReleaseInfo->swapchain) : nullptr)
        return fg->ReleaseSwapchainImages(device, pReleaseInfo->swapchain, pReleaseInfo->imageIndexCount,
                                          pReleaseInfo->pImageIndices);

    return o_vkReleaseSwapchainImagesEXT(device, pReleaseInfo);
}

// The common swapchain extensions' entry points, answered here for XeFG's swapchain (SetHostRoutes)
static PFN_vkAcquireFullScreenExclusiveModeEXT o_vkAcquireFullScreenExclusiveModeEXT = nullptr;
static PFN_vkReleaseFullScreenExclusiveModeEXT o_vkReleaseFullScreenExclusiveModeEXT = nullptr;
static PFN_vkGetRefreshCycleDurationGOOGLE o_vkGetRefreshCycleDurationGOOGLE = nullptr;
static PFN_vkGetPastPresentationTimingGOOGLE o_vkGetPastPresentationTimingGOOGLE = nullptr;
static PFN_vkSetLocalDimmingAMD o_vkSetLocalDimmingAMD = nullptr;
static PFN_vkGetSwapchainStatusKHR o_vkGetSwapchainStatusKHR = nullptr;
static PFN_vkGetSwapchainCounterEXT o_vkGetSwapchainCounterEXT = nullptr;
static PFN_vkSetDebugUtilsObjectNameEXT o_vkSetDebugUtilsObjectNameEXT = nullptr;
static PFN_vkSetDebugUtilsObjectTagEXT o_vkSetDebugUtilsObjectTagEXT = nullptr;
static PFN_vkDebugMarkerSetObjectNameEXT o_vkDebugMarkerSetObjectNameEXT = nullptr;
static PFN_vkDebugMarkerSetObjectTagEXT o_vkDebugMarkerSetObjectTagEXT = nullptr;
static PFN_vkSetPrivateData o_vkSetPrivateDataEXT = nullptr;
static PFN_vkGetPrivateData o_vkGetPrivateDataEXT = nullptr;

// Private data the game stores on XeFG's handle: (handle, slot) -> value
static std::mutex xefgPrivateDataMutex;
static std::map<std::pair<uint64_t, uint64_t>, uint64_t> xefgPrivateData;

static XeFG_Vk* XeFGOwningObject(VkObjectType objectType, uint64_t objectHandle)
{
    return objectType == VK_OBJECT_TYPE_SWAPCHAIN_KHR ? XeFGOwning((VkSwapchainKHR) objectHandle) : nullptr;
}

// The engine's own swapchain has the game's full screen exclusive mode
static VkResult VKAPI_CALL hkvkAcquireFullScreenExclusiveModeEXT(VkDevice device, VkSwapchainKHR swapchain)
{
    if (XeFGOwning(swapchain))
        return VK_SUCCESS;

    return o_vkAcquireFullScreenExclusiveModeEXT(device, swapchain);
}

static VkResult VKAPI_CALL hkvkReleaseFullScreenExclusiveModeEXT(VkDevice device, VkSwapchainKHR swapchain)
{
    if (XeFGOwning(swapchain))
        return VK_SUCCESS;

    return o_vkReleaseFullScreenExclusiveModeEXT(device, swapchain);
}

static VkResult VKAPI_CALL hkvkGetRefreshCycleDurationGOOGLE(VkDevice device, VkSwapchainKHR swapchain,
                                                             VkRefreshCycleDurationGOOGLE* pDisplayTimingProperties)
{
    if (auto fg = XeFGOwning(swapchain); fg != nullptr && pDisplayTimingProperties != nullptr)
    {
        pDisplayTimingProperties->refreshDuration = fg->RefreshDurationNs();
        return VK_SUCCESS;
    }

    return o_vkGetRefreshCycleDurationGOOGLE(device, swapchain, pDisplayTimingProperties);
}

static VkResult VKAPI_CALL hkvkGetPastPresentationTimingGOOGLE(VkDevice device, VkSwapchainKHR swapchain,
                                                               uint32_t* pPresentationTimingCount,
                                                               VkPastPresentationTimingGOOGLE* pPresentationTimings)
{
    // XeFG presents the frames, no timing of the game's presents is available
    if (XeFGOwning(swapchain) && pPresentationTimingCount != nullptr)
    {
        *pPresentationTimingCount = 0;
        return VK_SUCCESS;
    }

    return o_vkGetPastPresentationTimingGOOGLE(device, swapchain, pPresentationTimingCount, pPresentationTimings);
}

static void VKAPI_CALL hkvkSetLocalDimmingAMD(VkDevice device, VkSwapchainKHR swapChain, VkBool32 localDimmingEnable)
{
    if (XeFGOwning(swapChain))
        return;

    o_vkSetLocalDimmingAMD(device, swapChain, localDimmingEnable);
}

static VkResult VKAPI_CALL hkvkGetSwapchainStatusKHR(VkDevice device, VkSwapchainKHR swapchain)
{
    if (XeFGOwning(swapchain))
        return VK_SUCCESS;

    return o_vkGetSwapchainStatusKHR(device, swapchain);
}

static VkResult VKAPI_CALL hkvkGetSwapchainCounterEXT(VkDevice device, VkSwapchainKHR swapchain,
                                                      VkSurfaceCounterFlagBitsEXT counter, uint64_t* pCounterValue)
{
    // The engine refuses swapchains created with surface counters
    if (XeFGOwning(swapchain))
        return VK_ERROR_OUT_OF_DATE_KHR;

    return o_vkGetSwapchainCounterEXT(device, swapchain, counter, pCounterValue);
}

static VkResult VKAPI_CALL hkvkSetDebugUtilsObjectNameEXT(VkDevice device,
                                                          const VkDebugUtilsObjectNameInfoEXT* pNameInfo)
{
    if (pNameInfo != nullptr && XeFGOwningObject(pNameInfo->objectType, pNameInfo->objectHandle))
        return VK_SUCCESS;

    return o_vkSetDebugUtilsObjectNameEXT(device, pNameInfo);
}

static VkResult VKAPI_CALL hkvkSetDebugUtilsObjectTagEXT(VkDevice device, const VkDebugUtilsObjectTagInfoEXT* pTagInfo)
{
    if (pTagInfo != nullptr && XeFGOwningObject(pTagInfo->objectType, pTagInfo->objectHandle))
        return VK_SUCCESS;

    return o_vkSetDebugUtilsObjectTagEXT(device, pTagInfo);
}

static VkResult VKAPI_CALL hkvkDebugMarkerSetObjectNameEXT(VkDevice device,
                                                           const VkDebugMarkerObjectNameInfoEXT* pNameInfo)
{
    if (pNameInfo != nullptr && pNameInfo->objectType == VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT &&
        XeFGOwning((VkSwapchainKHR) pNameInfo->object))
    {
        return VK_SUCCESS;
    }

    return o_vkDebugMarkerSetObjectNameEXT(device, pNameInfo);
}

static VkResult VKAPI_CALL hkvkDebugMarkerSetObjectTagEXT(VkDevice device,
                                                          const VkDebugMarkerObjectTagInfoEXT* pTagInfo)
{
    if (pTagInfo != nullptr && pTagInfo->objectType == VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT &&
        XeFGOwning((VkSwapchainKHR) pTagInfo->object))
    {
        return VK_SUCCESS;
    }

    return o_vkDebugMarkerSetObjectTagEXT(device, pTagInfo);
}

static VkResult SetXeFGPrivateData(uint64_t objectHandle, VkPrivateDataSlot slot, uint64_t data)
{
    std::scoped_lock lock(xefgPrivateDataMutex);
    xefgPrivateData[{ objectHandle, (uint64_t) slot }] = data;
    return VK_SUCCESS;
}

static void GetXeFGPrivateData(uint64_t objectHandle, VkPrivateDataSlot slot, uint64_t* pData)
{
    std::scoped_lock lock(xefgPrivateDataMutex);
    auto found = xefgPrivateData.find({ objectHandle, (uint64_t) slot });
    *pData = found != xefgPrivateData.end() ? found->second : 0;
}

static VkResult VKAPI_CALL hkvkSetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle,
                                              VkPrivateDataSlot privateDataSlot, uint64_t data)
{
    if (XeFGOwningObject(objectType, objectHandle))
        return SetXeFGPrivateData(objectHandle, privateDataSlot, data);

    return o_vkSetPrivateData(device, objectType, objectHandle, privateDataSlot, data);
}

static VkResult VKAPI_CALL hkvkSetPrivateDataEXT(VkDevice device, VkObjectType objectType, uint64_t objectHandle,
                                                 VkPrivateDataSlot privateDataSlot, uint64_t data)
{
    if (XeFGOwningObject(objectType, objectHandle))
        return SetXeFGPrivateData(objectHandle, privateDataSlot, data);

    return o_vkSetPrivateDataEXT(device, objectType, objectHandle, privateDataSlot, data);
}

static void VKAPI_CALL hkvkGetPrivateData(VkDevice device, VkObjectType objectType, uint64_t objectHandle,
                                          VkPrivateDataSlot privateDataSlot, uint64_t* pData)
{
    if (pData != nullptr && XeFGOwningObject(objectType, objectHandle))
        return GetXeFGPrivateData(objectHandle, privateDataSlot, pData);

    o_vkGetPrivateData(device, objectType, objectHandle, privateDataSlot, pData);
}

static void VKAPI_CALL hkvkGetPrivateDataEXT(VkDevice device, VkObjectType objectType, uint64_t objectHandle,
                                             VkPrivateDataSlot privateDataSlot, uint64_t* pData)
{
    if (pData != nullptr && XeFGOwningObject(objectType, objectHandle))
        return GetXeFGPrivateData(objectHandle, privateDataSlot, pData);

    o_vkGetPrivateDataEXT(device, objectType, objectHandle, privateDataSlot, pData);
}

template <class T> static PFN_vkVoidFunction RouteXeFG(T& original, PFN_vkVoidFunction orgFunc, T hook)
{
    if (original == nullptr)
        original = (T) orgFunc;

    return (PFN_vkVoidFunction) hook;
}

// The swapchain calls above on XeFG's swapchain, only for functions the driver has
static PFN_vkVoidFunction XeFGSwapchainHook(const char* pName, PFN_vkVoidFunction orgFunc)
{
    if (State::Instance().activeFgOutput != FGOutput::XeFG || State::Instance().vulkanSkipHooks || orgFunc == nullptr)
        return nullptr;

    if (std::strcmp(pName, "vkAcquireFullScreenExclusiveModeEXT") == 0)
        return RouteXeFG(o_vkAcquireFullScreenExclusiveModeEXT, orgFunc, hkvkAcquireFullScreenExclusiveModeEXT);
    if (std::strcmp(pName, "vkReleaseFullScreenExclusiveModeEXT") == 0)
        return RouteXeFG(o_vkReleaseFullScreenExclusiveModeEXT, orgFunc, hkvkReleaseFullScreenExclusiveModeEXT);
    if (std::strcmp(pName, "vkGetRefreshCycleDurationGOOGLE") == 0)
        return RouteXeFG(o_vkGetRefreshCycleDurationGOOGLE, orgFunc, hkvkGetRefreshCycleDurationGOOGLE);
    if (std::strcmp(pName, "vkGetPastPresentationTimingGOOGLE") == 0)
        return RouteXeFG(o_vkGetPastPresentationTimingGOOGLE, orgFunc, hkvkGetPastPresentationTimingGOOGLE);
    if (std::strcmp(pName, "vkSetLocalDimmingAMD") == 0)
        return RouteXeFG(o_vkSetLocalDimmingAMD, orgFunc, hkvkSetLocalDimmingAMD);
    if (std::strcmp(pName, "vkGetSwapchainStatusKHR") == 0)
        return RouteXeFG(o_vkGetSwapchainStatusKHR, orgFunc, hkvkGetSwapchainStatusKHR);
    if (std::strcmp(pName, "vkGetSwapchainCounterEXT") == 0)
        return RouteXeFG(o_vkGetSwapchainCounterEXT, orgFunc, hkvkGetSwapchainCounterEXT);
    if (std::strcmp(pName, "vkSetDebugUtilsObjectNameEXT") == 0)
        return RouteXeFG(o_vkSetDebugUtilsObjectNameEXT, orgFunc, hkvkSetDebugUtilsObjectNameEXT);
    if (std::strcmp(pName, "vkSetDebugUtilsObjectTagEXT") == 0)
        return RouteXeFG(o_vkSetDebugUtilsObjectTagEXT, orgFunc, hkvkSetDebugUtilsObjectTagEXT);
    if (std::strcmp(pName, "vkDebugMarkerSetObjectNameEXT") == 0)
        return RouteXeFG(o_vkDebugMarkerSetObjectNameEXT, orgFunc, hkvkDebugMarkerSetObjectNameEXT);
    if (std::strcmp(pName, "vkDebugMarkerSetObjectTagEXT") == 0)
        return RouteXeFG(o_vkDebugMarkerSetObjectTagEXT, orgFunc, hkvkDebugMarkerSetObjectTagEXT);
    if (std::strcmp(pName, "vkSetPrivateDataEXT") == 0)
        return RouteXeFG(o_vkSetPrivateDataEXT, orgFunc, (PFN_vkSetPrivateData) hkvkSetPrivateDataEXT);
    if (std::strcmp(pName, "vkGetPrivateDataEXT") == 0)
        return RouteXeFG(o_vkGetPrivateDataEXT, orgFunc, (PFN_vkGetPrivateData) hkvkGetPrivateDataEXT);

    if (std::strcmp(pName, "vkReleaseSwapchainImagesKHR") == 0)
    {
        if (o_vkReleaseSwapchainImagesKHR == nullptr)
            o_vkReleaseSwapchainImagesKHR = (PFN_vkReleaseSwapchainImagesKHR) orgFunc;

        return (PFN_vkVoidFunction) hkvkReleaseSwapchainImagesKHR;
    }

    if (std::strcmp(pName, "vkReleaseSwapchainImagesEXT") == 0)
    {
        if (o_vkReleaseSwapchainImagesEXT == nullptr)
            o_vkReleaseSwapchainImagesEXT = (PFN_vkReleaseSwapchainImagesEXT) orgFunc;

        return (PFN_vkVoidFunction) hkvkReleaseSwapchainImagesEXT;
    }

    return nullptr;
}

VALIDATE_HOOK(hkvkCreateWin32SurfaceKHR, PFN_vkCreateWin32SurfaceKHR)
static VkResult hkvkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
    if (VulkanHooks::existingSurface != VK_NULL_HANDLE)
    {
        *pSurface = VulkanHooks::existingSurface;
        return VK_SUCCESS;
    }

    LOG_FUNC();

    auto result = o_vkCreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);

    auto procHwnd = Util::GetProcessWindow();
    LOG_DEBUG("procHwnd: {0:X}, swapchain hwnd: {1:X}", (UINT64) procHwnd, (UINT64) pCreateInfo->hwnd);

    if (result == VK_SUCCESS)
    {
        std::scoped_lock lock(_surfaceWindowsMutex);
        _surfaceWindows[*pSurface] = pCreateInfo->hwnd;
    }

    if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks)
    {
        _instance = instance;
        State::Instance().VulkanInstance = instance;
        LOG_DEBUG("_instance captured: {0:X}", (UINT64) _instance);

        if (!MenuOverlayVk::OnOtherWindow(pCreateInfo->hwnd))
        {
            MenuOverlayVk::DestroyVulkanObjects(false);

            _hwnd = pCreateInfo->hwnd;
            LOG_DEBUG("_hwnd captured: {0:X}", (UINT64) _hwnd);
        }
    }

    LOG_FUNC_RESULT(result);

    return result;
}

static void TrackPhysicalDevice(VkInstance instance, VkPhysicalDevice physicalDevice)
{
    if (physicalDevice == VK_NULL_HANDLE)
        return;

    std::scoped_lock lock(_vkApiVersionMutex);
    _physicalDeviceInstances[physicalDevice] = instance;
}

static uint32_t GetPhysicalDeviceApiVersion(VkPhysicalDevice physicalDevice, VkInstance* instance = nullptr)
{
    if (instance != nullptr)
        *instance = VK_NULL_HANDLE;

    std::scoped_lock lock(_vkApiVersionMutex);

    const auto physicalDeviceIt = _physicalDeviceInstances.find(physicalDevice);
    if (physicalDeviceIt != _physicalDeviceInstances.end())
    {
        if (instance != nullptr)
            *instance = physicalDeviceIt->second;

        const auto instanceIt = _instanceApiVersions.find(physicalDeviceIt->second);
        if (instanceIt != _instanceApiVersions.end())
            return instanceIt->second;
    }

    LOG_WARN("No Vulkan instance API version tracked for VkPhysicalDevice {0:X}; API version is unknown",
             (UINT64) physicalDevice);

    return 0;
}

static void UntrackInstance(VkInstance instance)
{
    std::scoped_lock lock(_vkApiVersionMutex);
    _instanceApiVersions.erase(instance);
    _instanceUsedApiVersions.erase(instance);

    for (auto it = _physicalDeviceInstances.begin(); it != _physicalDeviceInstances.end();)
    {
        if (it->second == instance)
            it = _physicalDeviceInstances.erase(it);
        else
            ++it;
    }
}

VALIDATE_HOOK(hkvkEnumeratePhysicalDevices, PFN_vkEnumeratePhysicalDevices)
static VkResult hkvkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount,
                                             VkPhysicalDevice* pPhysicalDevices)
{
    const auto result = o_vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);

    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pPhysicalDeviceCount != nullptr &&
        pPhysicalDevices != nullptr)
    {
        for (uint32_t i = 0; i < *pPhysicalDeviceCount; ++i)
            TrackPhysicalDevice(instance, pPhysicalDevices[i]);
    }

    return result;
}

static void TrackPhysicalDeviceGroups(VkInstance instance, uint32_t groupCount,
                                      const VkPhysicalDeviceGroupProperties* groups)
{
    for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
    {
        const auto& group = groups[groupIndex];
        for (uint32_t deviceIndex = 0; deviceIndex < group.physicalDeviceCount; ++deviceIndex)
            TrackPhysicalDevice(instance, group.physicalDevices[deviceIndex]);
    }
}

VALIDATE_HOOK(hkvkEnumeratePhysicalDeviceGroups, PFN_vkEnumeratePhysicalDeviceGroups)
static VkResult hkvkEnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount,
                                                  VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties)
{
    const auto result =
        o_vkEnumeratePhysicalDeviceGroups(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);

    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pPhysicalDeviceGroupCount != nullptr &&
        pPhysicalDeviceGroupProperties != nullptr)
        TrackPhysicalDeviceGroups(instance, *pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);

    return result;
}

VALIDATE_HOOK(hkvkEnumeratePhysicalDeviceGroupsKHR, PFN_vkEnumeratePhysicalDeviceGroupsKHR)
static VkResult hkvkEnumeratePhysicalDeviceGroupsKHR(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount,
                                                     VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties)
{
    const auto result =
        o_vkEnumeratePhysicalDeviceGroupsKHR(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);

    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pPhysicalDeviceGroupCount != nullptr &&
        pPhysicalDeviceGroupProperties != nullptr)
        TrackPhysicalDeviceGroups(instance, *pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);

    return result;
}

VALIDATE_HOOK(hkvkCreateInstance, PFN_vkCreateInstance)
static VkResult hkvkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                   VkInstance* pInstance)
{
    LOG_FUNC();

    VkInstanceCreateInfo localCreateInfo {};
    memcpy(&localCreateInfo, pCreateInfo, sizeof(VkInstanceCreateInfo));

    const uint32_t requestedApiVersion =
        pCreateInfo->pApplicationInfo != nullptr && pCreateInfo->pApplicationInfo->apiVersion != 0
            ? pCreateInfo->pApplicationInfo->apiVersion
            : VK_API_VERSION_1_0;

    if (State::Instance().isRunningOnLinux && !State::Instance().vulkanSkipHooks &&
        !State::Instance().creatingD3DDevice)
    {
        static const bool radvCooperativeMatrix2Enabled = EnableRadvCooperativeMatrix2();
        if (radvCooperativeMatrix2Enabled)
        {
            LOG_INFO("Enabled RADV cooperative matrix 2 support");
        }
    }

    VulkanSpoofing::hkvkCreateInstance(&localCreateInfo, pAllocator, pInstance);

    // FFX and Streamline use Vulkan 1.2 core functions like vkWaitSemaphores. Wine only provides those to 1.2 apps,
    // older ones crash calling them, Streamline's own vkCreateInstance raises the version too.
    VkApplicationInfo appInfo { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    const auto fgOutput = State::Instance().activeFgOutput;

    if ((fgOutput == FGOutput::FSRFG || fgOutput == FGOutput::DLSSG || fgOutput == FGOutput::XeFG) &&
        !State::Instance().creatingD3DDevice)
    {
        if (localCreateInfo.pApplicationInfo != nullptr)
            appInfo = *localCreateInfo.pApplicationInfo;

        // The XeFG engine needs 1.3 (SPIR-V 1.6) when the loader has it
        uint32_t neededVersion = VK_API_VERSION_1_2;

        if (fgOutput == FGOutput::XeFG)
        {
            auto enumerateVersion = (PFN_vkEnumerateInstanceVersion) VulkanHooks::GetInstanceProcAddr(
                VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
            uint32_t loaderVersion = VK_API_VERSION_1_0;

            if (enumerateVersion != nullptr && enumerateVersion(&loaderVersion) == VK_SUCCESS &&
                loaderVersion >= VK_API_VERSION_1_3)
            {
                neededVersion = VK_API_VERSION_1_3;
            }
        }

        if (appInfo.apiVersion < neededVersion)
        {
            LOG_INFO("Raising the API version from {}.{} to {}.{} for FG", VK_API_VERSION_MAJOR(appInfo.apiVersion),
                     VK_API_VERSION_MINOR(appInfo.apiVersion), VK_API_VERSION_MAJOR(neededVersion),
                     VK_API_VERSION_MINOR(neededVersion));

            appInfo.apiVersion = neededVersion;
            localCreateInfo.pApplicationInfo = &appInfo;
        }
    }

    const uint32_t usedApiVersion =
        localCreateInfo.pApplicationInfo != nullptr && localCreateInfo.pApplicationInfo->apiVersion != 0
            ? localCreateInfo.pApplicationInfo->apiVersion
            : VK_API_VERSION_1_0;

    VkResult result;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        result = o_vkCreateInstance(&localCreateInfo, pAllocator, pInstance);
    }

    // OptiScaler's own Streamline only makes short lived ones
    if (result == VK_SUCCESS && !StreamlineProxy::IsInitializing())
    {
        {
            std::scoped_lock lock(_vkApiVersionMutex);
            _instanceApiVersions[*pInstance] = requestedApiVersion;
            _instanceUsedApiVersions[*pInstance] = usedApiVersion;
        }

        State::Instance().VulkanInstance = *pInstance;
        LOG_DEBUG("State::Instance().VulkanInstance captured: {0:X}", (UINT64) State::Instance().VulkanInstance);

#ifdef VULKAN_DEBUG_LAYER
        auto address = vkGetInstanceProcAddr(State::Instance().VulkanInstance, "vkCreateDebugUtilsMessengerEXT");
        auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT) address;
        VkDebugUtilsMessengerEXT debugMessenger;
        vkCreateDebugUtilsMessengerEXT(State::Instance().VulkanInstance, &VulkanSpoofing::debugCreateInfo, nullptr,
                                       &debugMessenger);
#endif
    }

    // Disabled to prevent unnecessary object release
    // if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks)
    //{
    //     MenuOverlayVk::DestroyVulkanObjects(false);
    // }

    LOG_FUNC_RESULT(result);

    return result;
}

VALIDATE_HOOK(hkvkDestroyInstance, PFN_vkDestroyInstance)
static void hkvkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    UntrackInstance(instance);
    o_vkDestroyInstance(instance, pAllocator);
}

VALIDATE_HOOK(hkvkCreateDevice, PFN_vkCreateDevice)
static VkResult hkvkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    LOG_FUNC();

    VkDeviceCreateInfo localCreateInfo {};
    memcpy(&localCreateInfo, pCreateInfo, sizeof(VkDeviceCreateInfo));

    VkInstance physicalDeviceInstance = VK_NULL_HANDLE;
    const uint32_t requestedApiVersion = GetPhysicalDeviceApiVersion(physicalDevice, &physicalDeviceInstance);

    PFN_vkGetPhysicalDeviceFeatures2 getPhysicalDeviceFeatures2 = o_vkGetPhysicalDeviceFeatures2;

    if (requestedApiVersion < VK_API_VERSION_1_1)
    {
        getPhysicalDeviceFeatures2 = nullptr;

        if (physicalDeviceInstance != VK_NULL_HANDLE && o_vkGetInstanceProcAddr != nullptr)
        {
            getPhysicalDeviceFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
                o_vkGetInstanceProcAddr(physicalDeviceInstance, "vkGetPhysicalDeviceFeatures2KHR"));
        }
    }

    // Check support for AntiLag before spoof
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    VkPhysicalDeviceAntiLagFeaturesAMD antiLagFeatures = {};
    antiLagFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ANTI_LAG_FEATURES_AMD;

    features2.pNext = &antiLagFeatures;

    if (getPhysicalDeviceFeatures2)
    {
        getPhysicalDeviceFeatures2(physicalDevice, &features2);
        State::Instance().vkAntiLagSupported = antiLagFeatures.antiLag != 0;
    }

    IFGFeature_Vk* fg = nullptr;
    VulkanDeviceFeatureState fgFeatures(&localCreateInfo, getPhysicalDeviceFeatures2);

    // Before spoofing, which drops the extensions it only pretends to have
    if (!State::Instance().vulkanSkipHooks && !State::Instance().creatingD3DDevice &&
        getPhysicalDeviceFeatures2 != nullptr && !IsDxgiBackedCaller(_ReturnAddress()) &&
        (fg = CreateFGFeature()) != nullptr)
    {
        if (fgFeatures.EnableTimelineSemaphores(physicalDevice))
        {
            fg->PrepareDevice(physicalDevice, localCreateInfo);
        }
        else
        {
            LOG_WARN("Timeline semaphores aren't supported, can't use FG with this device");
            fg = nullptr;
        }
    }

    VkResult result;
    bool timelineSemaphoresEnabled = false;
    {
        VulkanDeviceFeatureState deviceFeatures(&localCreateInfo, getPhysicalDeviceFeatures2);
        VulkanDeviceFeatureState* featureState = nullptr;
        if (!State::Instance().vulkanSkipHooks && !State::Instance().creatingD3DDevice &&
            getPhysicalDeviceFeatures2 != nullptr)
            featureState = &deviceFeatures;

        VulkanSpoofing::hkvkCreateDevice(physicalDevice, &localCreateInfo, pAllocator, pDevice, featureState,
                                         requestedApiVersion);

        timelineSemaphoresEnabled = TimelineSemaphoresEnabled(&localCreateInfo);

        result = o_vkCreateDevice(physicalDevice, &localCreateInfo, pAllocator, pDevice);
    }

    if (result == VK_SUCCESS)
        DeferredRelease::DropOwnersExcept((uint64_t) *pDevice);

    if (result == VK_SUCCESS && fg != nullptr)
    {
        fg->OnDeviceCreated(*pDevice);
        HookDevice(*pDevice);
        Hudfix_Vk::HookDevice(*pDevice);
    }

    if (result == VK_SUCCESS && Config::Instance()->OverlayMenu.value_or_default())
    {
        if (!State::Instance().vulkanSkipHooks)
        {
            // Disabled to prevent unnecessary object release
            // MenuOverlayVk::DestroyVulkanObjects(false);

            _PD = physicalDevice;
            LOG_DEBUG("_PD captured: {0:X}", (UINT64) _PD);
            _device = *pDevice;
            LOG_DEBUG("_device captured: {0:X}", (UINT64) _device);
            HookDevice(_device);
            MenuOverlayVk::DeviceCreated(_device, &localCreateInfo);
        }

        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

        VkPhysicalDeviceIDProperties idProps {};
        idProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 props2 {};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &idProps;

        vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

        if (idProps.deviceLUIDValid == VK_TRUE)
        {
            auto primaryGpu = IdentifyGpu::getPrimaryGpu();
            auto luid = (PLUID) idProps.deviceLUID;
            if (!IsEqualLUID(*luid, primaryGpu.luid))
                LOG_WARN("VkDevice created with non-primary GPU");
        }
    }

    if (State::Instance().vkAntiLagSupported)
    {
        if (result == VK_SUCCESS && o_vkGetDeviceProcAddr)
        {
            VulkanHooks::o_vkAntiLagUpdateAMD =
                (PFN_vkAntiLagUpdateAMD) o_vkGetDeviceProcAddr(*pDevice, "vkAntiLagUpdateAMD");
        }
        else
        {
            State::Instance().vkAntiLagSupported = false;
            LOG_WARN("Vulkan AntiLag can't be enabled");
        }
    }

    // The driver's VK_NV_low_latency2, only there when the device has it enabled
    if (result == VK_SUCCESS && o_vkGetDeviceProcAddr && !State::Instance().vulkanSkipHooks &&
        !State::Instance().creatingD3DDevice && timelineSemaphoresEnabled)
    {
        auto getProc = [&](const char* name) { return o_vkGetDeviceProcAddr(*pDevice, name); };

        if (auto setSleepMode = (PFN_vkSetLatencySleepModeNV) getProc("vkSetLatencySleepModeNV"))
        {
            VulkanHooks::o_vkSetLatencySleepModeNV = setSleepMode;
            VulkanHooks::o_vkLatencySleepNV = (PFN_vkLatencySleepNV) getProc("vkLatencySleepNV");
            VulkanHooks::o_vkSetLatencyMarkerNV = (PFN_vkSetLatencyMarkerNV) getProc("vkSetLatencyMarkerNV");
            VulkanHooks::o_vkGetLatencyTimingsNV = (PFN_vkGetLatencyTimingsNV) getProc("vkGetLatencyTimingsNV");
            VulkanHooks::o_vkQueueNotifyOutOfBandNV =
                (PFN_vkQueueNotifyOutOfBandNV) getProc("vkQueueNotifyOutOfBandNV");

            _lowLatency2Device = *pDevice;
            LOG_INFO("VK_NV_low_latency2 enabled");
        }
    }

#ifdef USE_QUEUE_SUBMIT_2_KHR
    if (result == VK_SUCCESS)
        hkvkGetDeviceProcAddr(*pDevice, "vkQueueSubmit2KHR");
#endif

    LOG_FUNC_RESULT(result);

    return result;
}

VALIDATE_HOOK(hkvkQueuePresentKHR, PFN_vkQueuePresentKHR)
static VkResult hkvkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    auto fg = State::Instance().currentVkFG;

    // FFX presents the real swapchain from its own queue
    if (FromFGOutput(_ReturnAddress()) || (fg != nullptr && fg->OwnsQueue(queue)))
        return FGOutputQueuePresent(queue, pPresentInfo);

    LOG_FUNC();

    // Upscaler GPU time computation
    if (auto vkFeature = dynamic_cast<IFeature_Vk*>(State::Instance().currentFeature); vkFeature != nullptr)
    {
        if (auto upscalerTimeOpt = vkFeature->ReadUpscalerTime(nullptr); upscalerTimeOpt.has_value())
        {
            vkFeature->ReadDetailedGpuTimes(nullptr, State::Instance().detailedGpuTimes);

            auto upscalerTime = upscalerTimeOpt.value();
            // filter out possibly wrong measured high values
            if (upscalerTime < 100.0)
            {
                State::Instance().frameTimeMutex.lock();
                State::Instance().upscaleTimes.push_back(upscalerTime);
                State::Instance().upscaleTimes.pop_front();
                State::Instance().frameTimeMutex.unlock();
            }
        }
    }

    // dxvk/vkd3d-proton presents are handled by the DXGI swapchain
    const bool dxgiBacked = IsDxgiBackedCaller(_ReturnAddress());

    if (!dxgiBacked)
    {
        State::Instance().swapchainApi = Vulkan;

        // Tick feature to let it know if it's frozen
        if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)
        {
            if (auto currentFg = State::Instance().CurrentFG(); currentFg != nullptr)
                currentFeature->TickFrozenCheck(currentFg->GetInterpolatedFrameCount());
            else
                currentFeature->TickFrozenCheck();
        }
    }

    // Ends the hudless search before the menu renders to the swapchain
    if (!dxgiBacked)
        Hudfix_Vk::PresentStart();

    VkPresentInfoKHR localPresentInfo {};
    memcpy(&localPresentInfo, pPresentInfo, sizeof(VkPresentInfoKHR));

    // render menu if needed
    if (!dxgiBacked && !MenuOverlayVk::QueuePresent(queue, &localPresentInfo))
    {
        LOG_ERROR("QueuePresent: false!");
        return VK_ERROR_OUT_OF_DATE_KHR;
    }

    ReflexHooks::update(false, true);

    // dxvk/vkd3d-proton's go through the DXGI swapchain
    if (!dxgiBacked)
        InputCommon::update();

    const bool fgPresent = !dxgiBacked && fg != nullptr && localPresentInfo.swapchainCount == 1 &&
                           fg->OwnsSwapchain(localPresentInfo.pSwapchains[0]);

    MenuCommon::FGOutputPresents(fgPresent && fg->IsActive() && !fg->IsPaused());

    if (fgPresent)
    {
        auto& state = State::Instance();
        state.fgLastFrame++;

        static double lastPresentTime = 0.0;
        auto now = Util::MillisecondsNow();
        state.lastFGFrameTime = lastPresentTime > 0.0 ? now - lastPresentTime : 0.0;
        lastPresentTime = now;

        if (fg->IsActive() && !fg->IsPaused())
            fg->Present();
    }

    // original call
    ScopedVulkanCreatingSC scopedVulkanCreatingSC {};
    VkResult result;

    if (fgPresent)
    {
        ScopedFGCall fgCall {};
        result = fg->QueuePresent(queue, &localPresentInfo);
    }
    else
    {
        result = o_QueuePresentKHR(queue, &localPresentInfo);
    }

    // dxvk/vkd3d-proton's are counted by the DXGI swapchain
    if (!dxgiBacked)
        DeferredRelease::Process();

    // Unsure about Vulkan Reflex fps limit and if that could be causing an issue here
    // The XeFG engine limits FPS itself with the Reflex or Anti-Lag 2 output
    if (!State::Instance().reflexLimitsFps && !InputCommon::can_limit_fps() &&
        !(fgPresent && State::Instance().xefgVkEngineLimitsFps))
    {
        FrameLimit::sleep(fgPresent && fg->IsActive() && !fg->IsPaused());
    }

    LOG_FUNC_RESULT(result);
    return result;
}

VALIDATE_HOOK(hkvkCreateSwapchainKHR, PFN_vkCreateSwapchainKHR)
static VkResult hkvkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                       const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    if (FromFGOutput(_ReturnAddress()))
        return o_CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);

    LOG_FUNC();

    ScopedVulkanCreatingSC scopedVulkanCreatingSC {};

    // dxvk/vkd3d-proton swapchains get the menu on the DXGI swapchain
    const bool dxgiBacked = IsDxgiBackedCaller(_ReturnAddress());

    // Menu blur copies the swapchain image
    VkSwapchainCreateInfoKHR localCreateInfo {};
    if (pCreateInfo != nullptr && Config::Instance()->OverlayMenu.value_or_default() &&
        !State::Instance().vulkanSkipHooks && !dxgiBacked && _PD != VK_NULL_HANDLE &&
        (pCreateInfo->imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0)
    {
        VkSurfaceCapabilitiesKHR surfaceCaps {};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_PD, pCreateInfo->surface, &surfaceCaps) == VK_SUCCESS &&
            (surfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0)
        {
            localCreateInfo = *pCreateInfo;
            localCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            pCreateInfo = &localCreateInfo;
        }
    }

    // The Reflex output needs the swapchain's low latency mode, games with VK_NV_low_latency2 enable it themselves
    VkSwapchainLatencyCreateInfoNV latencyCreateInfo {};
    bool lowLatencySwapchain = false;

    if (pCreateInfo != nullptr && device == _lowLatency2Device && !State::Instance().vulkanSkipHooks && !dxgiBacked)
    {
        auto next = (const VkBaseInStructure*) pCreateInfo->pNext;

        while (next != nullptr && next->sType != VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV)
            next = next->pNext;

        if (next != nullptr)
        {
            lowLatencySwapchain = ((const VkSwapchainLatencyCreateInfoNV*) next)->latencyModeEnable;
        }
        else
        {
            if (pCreateInfo != &localCreateInfo)
            {
                localCreateInfo = *pCreateInfo;
                pCreateInfo = &localCreateInfo;
            }

            latencyCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV;
            latencyCreateInfo.pNext = localCreateInfo.pNext;
            latencyCreateInfo.latencyModeEnable = VK_TRUE;
            localCreateInfo.pNext = &latencyCreateInfo;
            lowLatencySwapchain = true;
        }
    }

    VkResult result = VK_SUCCESS;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        bool fgSwapchain = false;

        if (auto fg = State::Instance().currentVkFG;
            fg != nullptr && pCreateInfo != nullptr && !dxgiBacked && !State::Instance().vulkanSkipHooks)
        {
            ScopedFGCall fgCall {};
            fgSwapchain = fg->CreateSwapchain(device, pCreateInfo, pAllocator, pSwapchain);
        }

        if (!fgSwapchain)
            result = o_CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    }

    if (result == VK_SUCCESS && lowLatencySwapchain)
        VulkanHooks::SetLowLatencySwapchain(*pSwapchain);

    if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && *pSwapchain != VK_NULL_HANDLE &&
        !State::Instance().vulkanSkipHooks && !dxgiBacked)
    {
        State::Instance().screenWidth = static_cast<float>(pCreateInfo->imageExtent.width);
        State::Instance().screenHeight = static_cast<float>(pCreateInfo->imageExtent.height);

        LOG_DEBUG("if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && pSwapchain != "
                  "VK_NULL_HANDLE)");

        _device = device;
        LOG_DEBUG("_device captured: {0:X}", (UINT64) _device);

        // The hooks are also there for an FG output with the overlay menu off, which has no ImGui context
        if (Config::Instance()->OverlayMenu.value_or_default())
        {
            auto hwnd = VulkanHooks::SurfaceWindow(pCreateInfo->surface);
            MenuOverlayVk::CreateSwapchain(device, _PD, _instance, hwnd != nullptr ? hwnd : _hwnd, pCreateInfo,
                                           pAllocator, pSwapchain);
        }
    }

    LOG_FUNC_RESULT(result);
    return result;
}

VALIDATE_HOOK(hkvkDestroySwapchainKHR, PFN_vkDestroySwapchainKHR)
static void hkvkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
{
    if (FromFGOutput(_ReturnAddress()))
    {
        o_DestroySwapchainKHR(device, swapchain, pAllocator);
        return;
    }

    if (swapchain != VK_NULL_HANDLE)
        VulkanHooks::ForgetLowLatencySwapchain(swapchain);

    if (auto fg = State::Instance().currentVkFG; fg != nullptr)
    {
        ScopedFGCall fgCall {};

        if (fg->DestroySwapchain(device, swapchain, pAllocator))
            return;
    }

    o_DestroySwapchainKHR(device, swapchain, pAllocator);
}

VALIDATE_HOOK(hkvkGetSwapchainImagesKHR, PFN_vkGetSwapchainImagesKHR)
static VkResult hkvkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount,
                                          VkImage* pSwapchainImages)
{
    if (auto fg = State::Instance().currentVkFG;
        fg != nullptr && fg->OwnsSwapchain(swapchain) && !FromFGOutput(_ReturnAddress()))
    {
        ScopedFGCall fgCall {};
        auto result = fg->GetSwapchainImages(device, swapchain, pSwapchainImageCount, pSwapchainImages);

        if (result == VK_SUCCESS && pSwapchainImages != nullptr)
            Hudfix_Vk::AddSwapchainImages(pSwapchainImages, *pSwapchainImageCount);

        return result;
    }

    return o_GetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages);
}

VALIDATE_HOOK(hkvkAcquireNextImageKHR, PFN_vkAcquireNextImageKHR)
static VkResult hkvkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                        VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{
    if (auto fg = State::Instance().currentVkFG;
        fg != nullptr && fg->OwnsSwapchain(swapchain) && !FromFGOutput(_ReturnAddress()))
    {
        ScopedFGCall fgCall {};
        return fg->AcquireNextImage(device, swapchain, timeout, semaphore, fence, pImageIndex);
    }

    return o_AcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
}

// RDR2 acquires with this one, the FG output's swapchain only has the plain acquire. The device mask is for device
// groups, the FG output runs on one GPU.
VALIDATE_HOOK(hkvkAcquireNextImage2KHR, PFN_vkAcquireNextImage2KHR)
static VkResult hkvkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo,
                                         uint32_t* pImageIndex)
{
    if (auto fg = State::Instance().currentVkFG; fg != nullptr && pAcquireInfo != nullptr &&
                                                 fg->OwnsSwapchain(pAcquireInfo->swapchain) &&
                                                 !FromFGOutput(_ReturnAddress()))
    {
        ScopedFGCall fgCall {};
        return fg->AcquireNextImage(device, pAcquireInfo->swapchain, pAcquireInfo->timeout, pAcquireInfo->semaphore,
                                    pAcquireInfo->fence, pImageIndex);
    }

    return o_AcquireNextImage2KHR(device, pAcquireInfo, pImageIndex);
}

VALIDATE_HOOK(hkvkSetHdrMetadataEXT, PFN_vkSetHdrMetadataEXT)
static void hkvkSetHdrMetadataEXT(VkDevice device, uint32_t swapchainCount, const VkSwapchainKHR* pSwapchains,
                                  const VkHdrMetadataEXT* pMetadata)
{
    auto fg = State::Instance().currentVkFG;

    for (uint32_t i = 0; i < swapchainCount; i++)
    {
        if (fg != nullptr && fg->OwnsSwapchain(pSwapchains[i]) && !FromFGOutput(_ReturnAddress()))
        {
            ScopedFGCall fgCall {};
            fg->SetHdrMetadata(device, pSwapchains[i], &pMetadata[i]);
        }
        else
            o_SetHdrMetadataEXT(device, 1, &pSwapchains[i], &pMetadata[i]);
    }
}

// The FG output's swapchain isn't a driver one and the game's present ids don't reach the real presents,
// the output paces the presents itself
VALIDATE_HOOK(hkvkWaitForPresentKHR, PFN_vkWaitForPresentKHR)
static VkResult hkvkWaitForPresentKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t presentId, uint64_t timeout)
{
    if (auto fg = State::Instance().currentVkFG;
        fg != nullptr && fg->OwnsSwapchain(swapchain) && !FromFGOutput(_ReturnAddress()))
    {
        if (auto xefg = XeFGOwning(swapchain))
        {
            ScopedFGCall fgCall {};
            return xefg->WaitForPresent(device, swapchain, presentId, timeout);
        }

        return VK_SUCCESS;
    }

    return o_WaitForPresentKHR(device, swapchain, presentId, timeout);
}

VALIDATE_HOOK(hkvkWaitForPresent2KHR, PFN_vkWaitForPresent2KHR)
static VkResult hkvkWaitForPresent2KHR(VkDevice device, VkSwapchainKHR swapchain,
                                       const VkPresentWait2InfoKHR* pPresentWait2Info)
{
    if (auto fg = State::Instance().currentVkFG;
        fg != nullptr && fg->OwnsSwapchain(swapchain) && !FromFGOutput(_ReturnAddress()))
    {
        if (auto xefg = XeFGOwning(swapchain); xefg != nullptr && pPresentWait2Info != nullptr)
        {
            ScopedFGCall fgCall {};
            return xefg->WaitForPresent(device, swapchain, pPresentWait2Info->presentId, pPresentWait2Info->timeout);
        }

        return VK_SUCCESS;
    }

    return o_WaitForPresent2KHR(device, swapchain, pPresentWait2Info);
}

VALIDATE_HOOK(hkvkGetInstanceProcAddr, PFN_vkGetInstanceProcAddr)
PFN_vkVoidFunction hkvkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    auto orgFunc = o_vkGetInstanceProcAddr(instance, pName);

    if (orgFunc == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    auto procName = std::string(pName);

    if (procName == std::string("vkCreateInstance"))
    {
        if (o_vkCreateInstance == nullptr)
            o_vkCreateInstance = (PFN_vkCreateInstance) orgFunc;

        LOG_DEBUG("vkCreateInstance");
        return (PFN_vkVoidFunction) hkvkCreateInstance;
    }
    else if (procName == std::string("vkDestroyInstance"))
    {
        if (o_vkDestroyInstance == nullptr)
            o_vkDestroyInstance = (PFN_vkDestroyInstance) orgFunc;

        return (PFN_vkVoidFunction) hkvkDestroyInstance;
    }
    else if (procName == std::string("vkEnumeratePhysicalDevices"))
    {
        if (o_vkEnumeratePhysicalDevices == nullptr)
            o_vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices) orgFunc;

        return (PFN_vkVoidFunction) hkvkEnumeratePhysicalDevices;
    }
    else if (procName == std::string("vkEnumeratePhysicalDeviceGroups"))
    {
        if (o_vkEnumeratePhysicalDeviceGroups == nullptr)
            o_vkEnumeratePhysicalDeviceGroups = (PFN_vkEnumeratePhysicalDeviceGroups) orgFunc;

        return (PFN_vkVoidFunction) hkvkEnumeratePhysicalDeviceGroups;
    }
    else if (procName == std::string("vkEnumeratePhysicalDeviceGroupsKHR"))
    {
        if (o_vkEnumeratePhysicalDeviceGroupsKHR == nullptr)
            o_vkEnumeratePhysicalDeviceGroupsKHR = (PFN_vkEnumeratePhysicalDeviceGroupsKHR) orgFunc;

        return (PFN_vkVoidFunction) hkvkEnumeratePhysicalDeviceGroupsKHR;
    }
    else if (procName == std::string("vkCreateDevice"))
    {
        if (o_vkCreateDevice == nullptr)
            o_vkCreateDevice = (PFN_vkCreateDevice) orgFunc;

        LOG_DEBUG("vkCreateDevice");
        return (PFN_vkVoidFunction) hkvkCreateDevice;
    }

    if (auto direct = StreamlineSwapchainFunction(pName, _ReturnAddress()))
        return direct;

    if (auto lowLatencyInput = LowLatencyInputHook(pName, orgFunc, _ReturnAddress()))
        return lowLatencyInput;

    if (auto xefgSwapchain = XeFGSwapchainHook(pName, orgFunc))
        return xefgSwapchain;

    auto result = VulkanSpoofing::hkvkGetInstanceProcAddr(orgFunc, pName);
    if (result != VK_NULL_HANDLE)
        return result;

    return orgFunc;
}

VALIDATE_HOOK(hkvkGetDeviceProcAddr, PFN_vkGetDeviceProcAddr)
PFN_vkVoidFunction hkvkGetDeviceProcAddr(VkDevice device, const char* pName)
{
    auto orgFunc = o_vkGetDeviceProcAddr(device, pName);

    if (orgFunc == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    auto procName = std::string(pName);

    if (procName == std::string("vkCreateInstance"))
    {
        if (o_vkCreateInstance == nullptr)
            o_vkCreateInstance = (PFN_vkCreateInstance) orgFunc;

        LOG_DEBUG("vkCreateInstance");
        return (PFN_vkVoidFunction) hkvkCreateInstance;
    }
    else if (procName == std::string("vkCreateDevice"))
    {
        if (o_vkCreateDevice == nullptr)
            o_vkCreateDevice = (PFN_vkCreateDevice) orgFunc;

        LOG_DEBUG("vkCreateDevice");
        return (PFN_vkVoidFunction) hkvkCreateDevice;
    }

    if (auto direct = StreamlineSwapchainFunction(pName, _ReturnAddress()))
        return direct;

    if (auto lowLatencyInput = LowLatencyInputHook(pName, orgFunc, _ReturnAddress()))
        return lowLatencyInput;

    if (auto xefgSwapchain = XeFGSwapchainHook(pName, orgFunc))
        return xefgSwapchain;

    auto result = VulkanSpoofing::hkvkGetDeviceProcAddr(orgFunc, pName);
    if (result != VK_NULL_HANDLE)
        return result;

    return orgFunc;
}

void VulkanHooks::SetLowLatencySwapchain(VkSwapchainKHR swapchain)
{
    // The game's handle of the FG swapchain isn't a driver one, and FFX's real one has no low latency mode
    if (auto fg = State::Instance().currentVkFG; fg != nullptr && fg->OwnsSwapchain(swapchain))
        swapchain = VK_NULL_HANDLE;

    lowLatencySwapchain = swapchain;
}

HWND VulkanHooks::SurfaceWindow(VkSurfaceKHR surface)
{
    std::scoped_lock lock(_surfaceWindowsMutex);

    auto it = _surfaceWindows.find(surface);
    return it != _surfaceWindows.end() ? it->second : nullptr;
}

// A promoted core function is missing on a device that only has its extension (RDR2's vkGetSemaphoreCounterValue and
// vkCmdPushDescriptorSetWithTemplate are null), the extension's name then gives the same function. FFX and OptiScaler
// ask for the core names.
PFN_vkVoidFunction VulkanHooks::GetDeviceProcAddr(VkDevice device, const char* pName)
{
    auto getProc = o_vkGetDeviceProcAddr != nullptr ? o_vkGetDeviceProcAddr : vkGetDeviceProcAddr;

    if (auto function = getProc(device, pName))
        return function;

    if (pName == nullptr)
        return nullptr;

    for (auto suffix : { "KHR", "EXT" })
    {
        auto name = std::string(pName) + suffix;

        if (auto function = getProc(device, name.c_str()))
        {
            LOG_DEBUG("{} is missing, using {}", pName, name);
            return function;
        }
    }

    return nullptr;
}

// vulkan-1's exported vkSignalSemaphore dispatches through the device's core 1.2 entry, which a device with only
// VK_KHR_timeline_semaphore (RDR2) doesn't have
VkResult VulkanHooks::SignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo* pSignalInfo)
{
    static std::mutex mutex;
    static VkDevice lastDevice = VK_NULL_HANDLE;
    static PFN_vkSignalSemaphore signalSemaphore = nullptr;

    {
        std::scoped_lock lock(mutex);

        if (device != lastDevice)
        {
            signalSemaphore = (PFN_vkSignalSemaphore) GetDeviceProcAddr(device, "vkSignalSemaphore");

            if (signalSemaphore == nullptr)
                LOG_ERROR("Device {:X} has neither vkSignalSemaphore nor vkSignalSemaphoreKHR", (size_t) device);

            lastDevice = device;
        }
    }

    if (signalSemaphore == nullptr)
        return VK_ERROR_FEATURE_NOT_PRESENT;

    return signalSemaphore(device, pSignalInfo);
}

PFN_vkVoidFunction VulkanHooks::GetInstanceProcAddr(VkInstance instance, const char* pName)
{
    if (o_vkGetInstanceProcAddr != nullptr)
        return o_vkGetInstanceProcAddr(instance, pName);

    return vkGetInstanceProcAddr(instance, pName);
}

uint32_t VulkanHooks::InstanceApiVersion(VkPhysicalDevice physicalDevice, VkInstance* instance)
{
    if (instance != nullptr)
        *instance = VK_NULL_HANDLE;

    std::scoped_lock lock(_vkApiVersionMutex);

    const auto physicalDeviceIt = _physicalDeviceInstances.find(physicalDevice);

    // Enumerated outside of our hooks: the only instance, if there's one
    if (physicalDeviceIt == _physicalDeviceInstances.end())
    {
        if (_instanceUsedApiVersions.size() != 1)
            return 0;

        if (instance != nullptr)
            *instance = _instanceUsedApiVersions.begin()->first;

        return _instanceUsedApiVersions.begin()->second;
    }

    if (instance != nullptr)
        *instance = physicalDeviceIt->second;

    const auto versionIt = _instanceUsedApiVersions.find(physicalDeviceIt->second);
    return versionIt != _instanceUsedApiVersions.end() ? versionIt->second : 0;
}

PFN_vkVoidFunction VulkanHooks::OriginalSwapchainFunction(const char* pName)
{
    if (std::strcmp(pName, "vkCreateSwapchainKHR") == 0)
        return (PFN_vkVoidFunction) o_CreateSwapchainKHR;

    if (std::strcmp(pName, "vkDestroySwapchainKHR") == 0)
        return (PFN_vkVoidFunction) o_DestroySwapchainKHR;

    if (std::strcmp(pName, "vkGetSwapchainImagesKHR") == 0)
        return (PFN_vkVoidFunction) o_GetSwapchainImagesKHR;

    if (std::strcmp(pName, "vkAcquireNextImageKHR") == 0)
        return (PFN_vkVoidFunction) o_AcquireNextImageKHR;

    if (std::strcmp(pName, "vkQueuePresentKHR") == 0)
        return o_QueuePresentKHR != nullptr ? (PFN_vkVoidFunction) FGOutputQueuePresent : nullptr;

    if (std::strcmp(pName, "vkSetHdrMetadataEXT") == 0)
        return (PFN_vkVoidFunction) o_SetHdrMetadataEXT;

    return nullptr;
}

void VulkanHooks::Hook(HMODULE vulkan1)
{
    if (vulkanModule == nullptr)
        vulkanModule = vulkan1;

    VulkanSpoofing::HookForVulkanSpoofing(vulkan1);
    VulkanSpoofing::HookForVulkanExtensionSpoofing(vulkan1);
    VulkanSpoofing::HookForVulkanVRAMSpoofing(vulkan1);

    if (o_vkCreateDevice != nullptr)
        return;

    FARPROC address = nullptr;

    o_vkCreateDevice = (PFN_vkCreateDevice) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateDevice");
    o_vkCreateInstance = (PFN_vkCreateInstance) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateInstance");
    o_vkDestroyInstance = (PFN_vkDestroyInstance) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkDestroyInstance");

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetInstanceProcAddr");
    o_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetDeviceProcAddr");
    o_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkEnumeratePhysicalDevices");
    o_vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkEnumeratePhysicalDeviceGroups");
    o_vkEnumeratePhysicalDeviceGroups = (PFN_vkEnumeratePhysicalDeviceGroups) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkEnumeratePhysicalDeviceGroupsKHR");
    o_vkEnumeratePhysicalDeviceGroupsKHR = (PFN_vkEnumeratePhysicalDeviceGroupsKHR) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateWin32SurfaceKHR");
    o_vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR) address;

    // address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCmdPipelineBarrier");
    // o_vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetPhysicalDeviceFeatures2");
    o_vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateSemaphore");
    o_vkCreateSemaphore = (PFN_vkCreateSemaphore) address;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_vkCreateDevice != nullptr)
        DetourAttach(&(PVOID&) o_vkCreateDevice, hkvkCreateDevice);

    if (o_vkGetInstanceProcAddr != nullptr)
        DetourAttach(&(PVOID&) o_vkGetInstanceProcAddr, hkvkGetInstanceProcAddr);

    if (o_vkGetDeviceProcAddr != nullptr)
        DetourAttach(&(PVOID&) o_vkGetDeviceProcAddr, hkvkGetDeviceProcAddr);

    if (o_vkEnumeratePhysicalDevices != nullptr)
        DetourAttach(&(PVOID&) o_vkEnumeratePhysicalDevices, hkvkEnumeratePhysicalDevices);

    if (o_vkEnumeratePhysicalDeviceGroups != nullptr)
        DetourAttach(&(PVOID&) o_vkEnumeratePhysicalDeviceGroups, hkvkEnumeratePhysicalDeviceGroups);

    if (o_vkEnumeratePhysicalDeviceGroupsKHR != nullptr)
        DetourAttach(&(PVOID&) o_vkEnumeratePhysicalDeviceGroupsKHR, hkvkEnumeratePhysicalDeviceGroupsKHR);

    if (o_vkCreateInstance != nullptr)
        DetourAttach(&(PVOID&) o_vkCreateInstance, hkvkCreateInstance);

    if (o_vkDestroyInstance != nullptr)
        DetourAttach(&(PVOID&) o_vkDestroyInstance, hkvkDestroyInstance);

    if (o_vkCreateWin32SurfaceKHR != nullptr)
        DetourAttach(&(PVOID&) o_vkCreateWin32SurfaceKHR, hkvkCreateWin32SurfaceKHR);

    // if (o_vkCmdPipelineBarrier != nullptr)
    //     DetourAttach(&(PVOID&) o_vkCmdPipelineBarrier, hkvkCmdPipelineBarrier);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to hook Vulkan, error code: {:X}", detourResult);
        o_vkCreateDevice = nullptr;
        o_vkCreateInstance = nullptr;
        o_vkDestroyInstance = nullptr;
        o_vkGetInstanceProcAddr = nullptr;
        o_vkGetDeviceProcAddr = nullptr;
        o_vkEnumeratePhysicalDevices = nullptr;
        o_vkEnumeratePhysicalDeviceGroups = nullptr;
        o_vkEnumeratePhysicalDeviceGroupsKHR = nullptr;
        o_vkCreateWin32SurfaceKHR = nullptr;
        // o_vkCmdPipelineBarrier = nullptr;

        std::scoped_lock lock(_vkApiVersionMutex);
        _instanceApiVersions.clear();
        _physicalDeviceInstances.clear();
    }
}

void VulkanHooks::Unhook()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_QueuePresentKHR != nullptr)
        DetourDetach(&(PVOID&) o_QueuePresentKHR, hkvkQueuePresentKHR);

    if (o_CreateSwapchainKHR != nullptr)
        DetourDetach(&(PVOID&) o_CreateSwapchainKHR, hkvkCreateSwapchainKHR);

    if (o_DestroySwapchainKHR != nullptr)
        DetourDetach(&(PVOID&) o_DestroySwapchainKHR, hkvkDestroySwapchainKHR);

    if (o_GetSwapchainImagesKHR != nullptr)
        DetourDetach(&(PVOID&) o_GetSwapchainImagesKHR, hkvkGetSwapchainImagesKHR);

    if (o_AcquireNextImageKHR != nullptr)
        DetourDetach(&(PVOID&) o_AcquireNextImageKHR, hkvkAcquireNextImageKHR);

    if (o_AcquireNextImage2KHR != nullptr)
        DetourDetach(&(PVOID&) o_AcquireNextImage2KHR, hkvkAcquireNextImage2KHR);

    if (o_SetHdrMetadataEXT != nullptr)
        DetourDetach(&(PVOID&) o_SetHdrMetadataEXT, hkvkSetHdrMetadataEXT);

    if (o_WaitForPresentKHR != nullptr)
        DetourDetach(&(PVOID&) o_WaitForPresentKHR, hkvkWaitForPresentKHR);

    if (o_WaitForPresent2KHR != nullptr)
        DetourDetach(&(PVOID&) o_WaitForPresent2KHR, hkvkWaitForPresent2KHR);

    if (o_vkSetPrivateData != nullptr)
        DetourDetach(&(PVOID&) o_vkSetPrivateData, hkvkSetPrivateData);

    if (o_vkGetPrivateData != nullptr)
        DetourDetach(&(PVOID&) o_vkGetPrivateData, hkvkGetPrivateData);

    if (o_vkCreateDevice != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateDevice, hkvkCreateDevice);

    if (o_vkEnumeratePhysicalDevices != nullptr)
        DetourDetach(&(PVOID&) o_vkEnumeratePhysicalDevices, hkvkEnumeratePhysicalDevices);

    if (o_vkEnumeratePhysicalDeviceGroups != nullptr)
        DetourDetach(&(PVOID&) o_vkEnumeratePhysicalDeviceGroups, hkvkEnumeratePhysicalDeviceGroups);

    if (o_vkEnumeratePhysicalDeviceGroupsKHR != nullptr)
        DetourDetach(&(PVOID&) o_vkEnumeratePhysicalDeviceGroupsKHR, hkvkEnumeratePhysicalDeviceGroupsKHR);

    if (o_vkCreateInstance != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateInstance, hkvkCreateInstance);

    if (o_vkDestroyInstance != nullptr)
        DetourDetach(&(PVOID&) o_vkDestroyInstance, hkvkDestroyInstance);

    if (o_vkCreateWin32SurfaceKHR != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateWin32SurfaceKHR, hkvkCreateWin32SurfaceKHR);

    // if (o_vkCmdPipelineBarrier != nullptr)
    //     DetourDetach(&(PVOID&) o_vkCmdPipelineBarrier, hkvkCmdPipelineBarrier);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook Vulkan, error code: {:X}", detourResult);
    }
    else
    {
        o_QueuePresentKHR = nullptr;
        o_CreateSwapchainKHR = nullptr;
        o_DestroySwapchainKHR = nullptr;
        o_GetSwapchainImagesKHR = nullptr;
        o_AcquireNextImageKHR = nullptr;
        o_AcquireNextImage2KHR = nullptr;
        o_SetHdrMetadataEXT = nullptr;
        o_WaitForPresentKHR = nullptr;
        o_WaitForPresent2KHR = nullptr;
        o_vkSetPrivateData = nullptr;
        o_vkGetPrivateData = nullptr;
        o_vkCreateDevice = nullptr;
        o_vkCreateInstance = nullptr;
        o_vkDestroyInstance = nullptr;
        o_vkGetInstanceProcAddr = nullptr;
        o_vkGetDeviceProcAddr = nullptr;
        o_vkEnumeratePhysicalDevices = nullptr;
        o_vkEnumeratePhysicalDeviceGroups = nullptr;
        o_vkEnumeratePhysicalDeviceGroupsKHR = nullptr;
        o_vkCreateWin32SurfaceKHR = nullptr;
        // o_vkCmdPipelineBarrier = nullptr;

        std::scoped_lock lock(_vkApiVersionMutex);
        _instanceApiVersions.clear();
        _physicalDeviceInstances.clear();
    }
}
