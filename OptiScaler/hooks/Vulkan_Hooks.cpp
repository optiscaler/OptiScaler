#include "pch.h"

#include "Vulkan_Hooks.h"

#include <Util.h>
#include <Config.h>
#include <SysUtils.h>

#include <menu/menu_overlay_vk.h>
#include <proxies/KernelBase_Proxy.h>
#include <upscaler_time/UpscalerTime_Vk.h>

#include <misc/FrameLimit.h>
#include "Reflex_Hooks.h"

#include <spoofing/Vulkan_Spoofing.h>

#include <vulkan/vulkan.hpp>

#include <unordered_map>

#include <detours/detours.h>
#include <misc/IdentifyGpu.h>

#include "Hook_Utils.h"

// for menu rendering
static VkDevice _device = VK_NULL_HANDLE;
static VkInstance _instance = VK_NULL_HANDLE;
static VkPhysicalDevice _PD = VK_NULL_HANDLE;
static HWND _hwnd = nullptr;

static std::mutex _vkApiVersionMutex;
static std::unordered_map<VkInstance, uint32_t> _instanceApiVersions;
static std::unordered_map<VkPhysicalDevice, VkInstance> _physicalDeviceInstances;

static std::mutex _vkPresentMutex;

PFN_vkCreateDevice o_vkCreateDevice = nullptr;
PFN_vkCreateInstance o_vkCreateInstance = nullptr;
PFN_vkCreateWin32SurfaceKHR o_vkCreateWin32SurfaceKHR = nullptr;
static PFN_vkDestroyInstance o_vkDestroyInstance = nullptr;
PFN_vkQueuePresentKHR o_QueuePresentKHR = nullptr;
PFN_vkCreateSwapchainKHR o_CreateSwapchainKHR = nullptr;
static PFN_vkGetInstanceProcAddr o_vkGetInstanceProcAddr = nullptr;
static PFN_vkGetDeviceProcAddr o_vkGetDeviceProcAddr = nullptr;
static PFN_vkEnumeratePhysicalDevices o_vkEnumeratePhysicalDevices = nullptr;
static PFN_vkEnumeratePhysicalDeviceGroups o_vkEnumeratePhysicalDeviceGroups = nullptr;
static PFN_vkEnumeratePhysicalDeviceGroupsKHR o_vkEnumeratePhysicalDeviceGroupsKHR = nullptr;

// Those aren't hooked, just grabbed for use
static PFN_vkGetPhysicalDeviceFeatures2 o_vkGetPhysicalDeviceFeatures2 = nullptr;
PFN_vkCreateSemaphore VulkanHooks::o_vkCreateSemaphore = nullptr;
PFN_vkSignalSemaphore VulkanHooks::o_vkSignalSemaphore = nullptr;
PFN_vkAntiLagUpdateAMD VulkanHooks::o_vkAntiLagUpdateAMD = nullptr;

// Forward declaration
static VkResult hkvkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
static VkResult hkvkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                       const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);

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

static void HookDevice(VkDevice InDevice)
{
    if (o_CreateSwapchainKHR != nullptr || State::Instance().vulkanSkipHooks)
        return;

    LOG_FUNC();

    o_QueuePresentKHR = (PFN_vkQueuePresentKHR) (vkGetDeviceProcAddr(InDevice, "vkQueuePresentKHR"));
    o_CreateSwapchainKHR = (PFN_vkCreateSwapchainKHR) (vkGetDeviceProcAddr(InDevice, "vkCreateSwapchainKHR"));

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

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook VkDevice, error code: {:X}", detourResult);
            o_QueuePresentKHR = nullptr;
            o_CreateSwapchainKHR = nullptr;
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

    VkResult result;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        result = o_vkCreateInstance(&localCreateInfo, pAllocator, pInstance);
    }

    if (result == VK_SUCCESS)
    {
        {
            std::scoped_lock lock(_vkApiVersionMutex);
            _instanceApiVersions[*pInstance] = requestedApiVersion;
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

    VkDeviceCreateInfo localCreteInfo {};
    memcpy(&localCreteInfo, pCreateInfo, sizeof(VkDeviceCreateInfo));

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

    VkResult result;
    {
        VulkanDeviceFeatureState deviceFeatures(&localCreteInfo, getPhysicalDeviceFeatures2);
        auto* featureState = !State::Instance().vulkanSkipHooks && !State::Instance().creatingD3DDevice &&
                                     getPhysicalDeviceFeatures2 != nullptr
                                 ? &deviceFeatures
                                 : nullptr;

        VulkanSpoofing::hkvkCreateDevice(physicalDevice, &localCreteInfo, pAllocator, pDevice, featureState,
                                         requestedApiVersion);

        result = o_vkCreateDevice(physicalDevice, &localCreteInfo, pAllocator, pDevice);
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

    // get upscaler time
    UpscalerTimeVk::ReadUpscalingTime(_device);

    // ??? TODO: if we are hooking dxvk's vulkan calls then this present call could be either coming from dxvk or from a
    // native vk game
    if (!IdentifyGpu::getPrimaryGpu().usesDxvk)
        State::Instance().swapchainApi = Vulkan;

    // Tick feature to let it know if it's frozen
    if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)
    {
        if (auto currentFg = State::Instance().currentFG; currentFg != nullptr)
            currentFeature->TickFrozenCheck(currentFg->GetInterpolatedFrameCount());
        else
            currentFeature->TickFrozenCheck();
    }

    VkPresentInfoKHR localPresentInfo {};
    memcpy(&localPresentInfo, pPresentInfo, sizeof(VkPresentInfoKHR));

    // render menu if needed
    if (!MenuOverlayVk::QueuePresent(queue, &localPresentInfo))
    {
        LOG_ERROR("QueuePresent: false!");
        return VK_ERROR_OUT_OF_DATE_KHR;
    }

    ReflexHooks::update(false, true);

    // original call
    ScopedVulkanCreatingSC scopedVulkanCreatingSC {};
    auto result = o_QueuePresentKHR(queue, &localPresentInfo);

    // Unsure about Vulkan Reflex fps limit and if that could be causing an issue here
    if (!State::Instance().reflexLimitsFps)
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
    VkResult result = VK_SUCCESS;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        result = o_CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    }

    if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && *pSwapchain != VK_NULL_HANDLE &&
        !State::Instance().vulkanSkipHooks)
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

    auto result = VulkanSpoofing::hkvkGetDeviceProcAddr(orgFunc, pName);
    if (result != VK_NULL_HANDLE)
        return result;

    return orgFunc;
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
