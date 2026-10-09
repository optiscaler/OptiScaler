#include "pch.h"

#include "Vulkan_Hooks.h"

#include <Util.h>
#include <Config.h>
#include <SysUtils.h>

#include <menu/menu_overlay_vk.h>
#include <proxies/KernelBase_Proxy.h>
#include <upscalers/IFeature_Vk.h>

#include <misc/FrameLimit.h>
#include "Reflex_Hooks.h"

#include <spoofing/Vulkan_Spoofing.h>

#include <vulkan/vulkan.hpp>

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

static std::mutex _vkPresentMutex;

PFN_vkCreateDevice o_vkCreateDevice = nullptr;
PFN_vkCreateInstance o_vkCreateInstance = nullptr;
PFN_vkCreateWin32SurfaceKHR o_vkCreateWin32SurfaceKHR = nullptr;
PFN_vkQueuePresentKHR o_QueuePresentKHR = nullptr;
PFN_vkCreateSwapchainKHR o_CreateSwapchainKHR = nullptr;
static PFN_vkDestroySwapchainKHR o_DestroySwapchainKHR = nullptr;
static PFN_vkGetInstanceProcAddr o_vkGetInstanceProcAddr = nullptr;
static PFN_vkGetDeviceProcAddr o_vkGetDeviceProcAddr = nullptr;

// Those aren't hooked, just grabbed for use
static PFN_vkGetPhysicalDeviceFeatures2 o_vkGetPhysicalDeviceFeatures2 = nullptr;
PFN_vkCreateSemaphore VulkanHooks::o_vkCreateSemaphore = nullptr;
PFN_vkSignalSemaphore VulkanHooks::o_vkSignalSemaphore = nullptr;
PFN_vkAntiLagUpdateAMD VulkanHooks::o_vkAntiLagUpdateAMD = nullptr;

// The device with VK_NV_low_latency2 enabled
static VkDevice _lowLatency2Device = VK_NULL_HANDLE;

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
#ifdef LOW_LATENCY_INPUTS
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
#endif

    return nullptr;
}

static void HookDevice(VkDevice InDevice)
{
    if (o_CreateSwapchainKHR != nullptr || State::Instance().vulkanSkipHooks)
        return;

    LOG_FUNC();

    o_QueuePresentKHR = (PFN_vkQueuePresentKHR) (vkGetDeviceProcAddr(InDevice, "vkQueuePresentKHR"));
    o_CreateSwapchainKHR = (PFN_vkCreateSwapchainKHR) (vkGetDeviceProcAddr(InDevice, "vkCreateSwapchainKHR"));
    o_DestroySwapchainKHR = (PFN_vkDestroySwapchainKHR) (vkGetDeviceProcAddr(InDevice, "vkDestroySwapchainKHR"));

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

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook VkDevice, error code: {:X}", detourResult);
            o_QueuePresentKHR = nullptr;
            o_CreateSwapchainKHR = nullptr;
            o_DestroySwapchainKHR = nullptr;
        }
    }
}

VALIDATE_HOOK(hkvkCreateWin32SurfaceKHR, PFN_vkCreateWin32SurfaceKHR)
static VkResult hkvkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
    LOG_FUNC();

    auto result = o_vkCreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);

    auto procHwnd = Util::GetProcessWindow();
    LOG_DEBUG("procHwnd: {0:X}, swapchain hwnd: {1:X}", (UINT64) procHwnd, (UINT64) pCreateInfo->hwnd);

    if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks)
    {
        MenuOverlayVk::DestroyVulkanObjects(false);

        _instance = instance;
        State::Instance().VulkanInstance = instance;
        LOG_DEBUG("_instance captured: {0:X}", (UINT64) _instance);
        _hwnd = pCreateInfo->hwnd;
        LOG_DEBUG("_hwnd captured: {0:X}", (UINT64) _hwnd);
    }

    LOG_FUNC_RESULT(result);

    return result;
}

VALIDATE_HOOK(hkvkCreateInstance, PFN_vkCreateInstance)
static VkResult hkvkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                   VkInstance* pInstance)
{
    LOG_FUNC();

    VkInstanceCreateInfo localCreateInfo {};
    memcpy(&localCreateInfo, pCreateInfo, sizeof(VkInstanceCreateInfo));

    VulkanSpoofing::hkvkCreateInstance(&localCreateInfo, pAllocator, pInstance);

    VkResult result;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        result = o_vkCreateInstance(&localCreateInfo, pAllocator, pInstance);
    }

    if (result == VK_SUCCESS)
    {
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

VALIDATE_HOOK(hkvkCreateDevice, PFN_vkCreateDevice)
static VkResult hkvkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    LOG_FUNC();

    VkDeviceCreateInfo localCreteInfo {};
    memcpy(&localCreteInfo, pCreateInfo, sizeof(VkDeviceCreateInfo));

    // Check support for AntiLag before spoof
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    VkPhysicalDeviceAntiLagFeaturesAMD antiLagFeatures = {};
    antiLagFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ANTI_LAG_FEATURES_AMD;

    features2.pNext = &antiLagFeatures;

    if (o_vkGetPhysicalDeviceFeatures2)
    {
        o_vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
        State::Instance().vkAntiLagSupported = antiLagFeatures.antiLag != 0;
    }

    VulkanSpoofing::hkvkCreateDevice(physicalDevice, &localCreteInfo, pAllocator, pDevice);

    auto result = o_vkCreateDevice(physicalDevice, &localCreteInfo, pAllocator, pDevice);

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
        !State::Instance().creatingD3DDevice && TimelineSemaphoresEnabled(&localCreteInfo))
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
            if (auto currentFg = State::Instance().currentFG; currentFg != nullptr)
                currentFeature->TickFrozenCheck(currentFg->GetInterpolatedFrameCount());
            else
                currentFeature->TickFrozenCheck();
        }
    }

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

    // original call
    ScopedVulkanCreatingSC scopedVulkanCreatingSC {};
    auto result = o_QueuePresentKHR(queue, &localPresentInfo);

    // Unsure about Vulkan Reflex fps limit and if that could be causing an issue here
    if (!State::Instance().reflexLimitsFps && !InputCommon::can_limit_fps())
        FrameLimit::sleep(false);

    LOG_FUNC_RESULT(result);
    return result;
}

VALIDATE_HOOK(hkvkCreateSwapchainKHR, PFN_vkCreateSwapchainKHR)
static VkResult hkvkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                       const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
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

#ifdef LOW_LATENCY_INPUTS
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
#endif

    VkResult result = VK_SUCCESS;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        result = o_CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    }

#ifdef LOW_LATENCY_INPUTS
    if (result == VK_SUCCESS && lowLatencySwapchain)
        VulkanHooks::SetLowLatencySwapchain(*pSwapchain);
#endif

    if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && *pSwapchain != VK_NULL_HANDLE &&
        !State::Instance().vulkanSkipHooks && !dxgiBacked)
    {
        State::Instance().screenWidth = static_cast<float>(pCreateInfo->imageExtent.width);
        State::Instance().screenHeight = static_cast<float>(pCreateInfo->imageExtent.height);

        LOG_DEBUG("if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && pSwapchain != "
                  "VK_NULL_HANDLE)");

        _device = device;
        LOG_DEBUG("_device captured: {0:X}", (UINT64) _device);

        MenuOverlayVk::CreateSwapchain(device, _PD, _instance, _hwnd, pCreateInfo, pAllocator, pSwapchain);
    }

    LOG_FUNC_RESULT(result);
    return result;
}

VALIDATE_HOOK(hkvkDestroySwapchainKHR, PFN_vkDestroySwapchainKHR)
static void hkvkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
{
    if (swapchain != VK_NULL_HANDLE)
        VulkanHooks::ForgetLowLatencySwapchain(swapchain);

    o_DestroySwapchainKHR(device, swapchain, pAllocator);
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
    else if (procName == std::string("vkCreateDevice"))
    {
        if (o_vkCreateDevice == nullptr)
            o_vkCreateDevice = (PFN_vkCreateDevice) orgFunc;

        LOG_DEBUG("vkCreateDevice");
        return (PFN_vkVoidFunction) hkvkCreateDevice;
    }

    if (auto lowLatencyInput = LowLatencyInputHook(pName, orgFunc, _ReturnAddress()))
        return lowLatencyInput;

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

    if (auto lowLatencyInput = LowLatencyInputHook(pName, orgFunc, _ReturnAddress()))
        return lowLatencyInput;

    auto result = VulkanSpoofing::hkvkGetDeviceProcAddr(orgFunc, pName);
    if (result != VK_NULL_HANDLE)
        return result;

    return orgFunc;
}

PFN_vkVoidFunction VulkanHooks::GetDeviceProcAddr(VkDevice device, const char* pName)
{
    if (o_vkGetDeviceProcAddr != nullptr)
        return o_vkGetDeviceProcAddr(device, pName);

    return vkGetDeviceProcAddr(device, pName);
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

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetInstanceProcAddr");
    o_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetDeviceProcAddr");
    o_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateWin32SurfaceKHR");
    o_vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR) address;

    // address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCmdPipelineBarrier");
    // o_vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetPhysicalDeviceFeatures2");
    o_vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateSemaphore");
    o_vkCreateSemaphore = (PFN_vkCreateSemaphore) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkSignalSemaphore");
    o_vkSignalSemaphore = (PFN_vkSignalSemaphore) address;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_vkCreateDevice != nullptr)
        DetourAttach(&(PVOID&) o_vkCreateDevice, hkvkCreateDevice);

    if (o_vkGetInstanceProcAddr != nullptr)
        DetourAttach(&(PVOID&) o_vkGetInstanceProcAddr, hkvkGetInstanceProcAddr);

    if (o_vkGetDeviceProcAddr != nullptr)
        DetourAttach(&(PVOID&) o_vkGetDeviceProcAddr, hkvkGetDeviceProcAddr);

    if (o_vkCreateInstance != nullptr)
        DetourAttach(&(PVOID&) o_vkCreateInstance, hkvkCreateInstance);

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
        o_vkGetInstanceProcAddr = nullptr;
        o_vkGetDeviceProcAddr = nullptr;
        o_vkCreateWin32SurfaceKHR = nullptr;
        // o_vkCmdPipelineBarrier = nullptr;
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

    if (o_vkCreateDevice != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateDevice, hkvkCreateDevice);

    if (o_vkCreateInstance != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateInstance, hkvkCreateInstance);

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
        o_vkCreateDevice = nullptr;
        o_vkCreateInstance = nullptr;
        o_vkGetInstanceProcAddr = nullptr;
        o_vkGetDeviceProcAddr = nullptr;
        o_vkCreateWin32SurfaceKHR = nullptr;
        // o_vkCmdPipelineBarrier = nullptr;
    }
}
