#include "HooksVk.h"

#include <Util.h>
#include <Config.h>

#include <menu/menu_overlay_vk.h>

#include <proxies/Kernel32_Proxy.h>

#include <detours/detours.h>
#include <misc/FrameLimit.h>
#include <nvapi/ReflexHooks.h>

// for menu rendering
static VkDevice _device = VK_NULL_HANDLE;
static VkInstance _instance = VK_NULL_HANDLE;
static VkPhysicalDevice _PD = VK_NULL_HANDLE;
static HWND _hwnd = nullptr;

static std::mutex _vkPresentMutex;

// hooking
typedef VkResult (*PFN_QueuePresentKHR)(VkQueue, const VkPresentInfoKHR*);
typedef VkResult (*PFN_CreateSwapchainKHR)(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*,
                                           VkSwapchainKHR*);
typedef VkResult (*PFN_vkCreateWin32SurfaceKHR)(VkInstance, const VkWin32SurfaceCreateInfoKHR*,
                                                const VkAllocationCallbacks*, VkSurfaceKHR*);

PFN_vkCreateDevice o_vkCreateDevice = nullptr;
PFN_vkCreateInstance o_vkCreateInstance = nullptr;
PFN_vkCreateWin32SurfaceKHR o_vkCreateWin32SurfaceKHR = nullptr;
PFN_vkCmdPipelineBarrier o_vkCmdPipelineBarrier = nullptr;
PFN_QueuePresentKHR o_QueuePresentKHR = nullptr;
PFN_CreateSwapchainKHR o_CreateSwapchainKHR = nullptr;

static VkResult hkvkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
static VkResult hkvkQueuePresentKHR(VkQueue queue, VkPresentInfoKHR* pPresentInfo);
static VkResult hkvkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                       VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);

static void HookDevice(VkDevice InDevice)
{
    if (o_CreateSwapchainKHR != nullptr || State::Instance().vulkanSkipHooks)
        return;

    LOG_FUNC();

    o_QueuePresentKHR = (PFN_QueuePresentKHR) (vkGetDeviceProcAddr(InDevice, "vkQueuePresentKHR"));
    o_CreateSwapchainKHR = (PFN_CreateSwapchainKHR) (vkGetDeviceProcAddr(InDevice, "vkCreateSwapchainKHR"));

    if (o_CreateSwapchainKHR)
    {
        LOG_DEBUG("Hooking VkDevice");

        // Hook
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_QueuePresentKHR, hkvkQueuePresentKHR);
        DetourAttach(&(PVOID&) o_CreateSwapchainKHR, hkvkCreateSwapchainKHR);

        DetourTransactionCommit();
    }
}

static void hkvkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                                   VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                                   uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
                                   uint32_t bufferMemoryBarrierCount,
                                   const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
                                   const VkImageMemoryBarrier* pImageMemoryBarriers)
{
    if (State::Instance().gameQuirks & GameQuirk::VulkanDLSSBarrierFixup)
    {
        // AMD drivers on the cards around RDNA2 didn't treat VK_IMAGE_LAYOUT_UNDEFINED in the same way Nvidia does.
        // Doesn't seem like a bug, just a different way of handling an UB but we need to adjust.

        // DLSSG Present
        if (imageMemoryBarrierCount == 2)
        {
            if (pImageMemoryBarriers[0].oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
                pImageMemoryBarriers[0].newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                pImageMemoryBarriers[1].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                pImageMemoryBarriers[1].newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            {
                LOG_TRACE("Changing an UNDEFINED barrier in DLSSG Present");

                VkImageMemoryBarrier newImageBarriers[2];
                std::memcpy(newImageBarriers, pImageMemoryBarriers, sizeof(newImageBarriers));

                newImageBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                return o_vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
                                              memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
                                              pBufferMemoryBarriers, imageMemoryBarrierCount, newImageBarriers);
            }
        }

        // DLSS
        // Those are already in the correct layouts
        if (imageMemoryBarrierCount == 4)
        {
            if (pImageMemoryBarriers[0].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                pImageMemoryBarriers[1].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                pImageMemoryBarriers[2].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                pImageMemoryBarriers[3].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
            {
                LOG_TRACE("Removing an UNDEFINED barrier in DLSS");
                return;
            }
        }
    }

    return o_vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount,
                                  pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers,
                                  imageMemoryBarrierCount, pImageMemoryBarriers);
}

static VkResult hkvkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
    LOG_FUNC();

    auto result = o_vkCreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);

    auto procHwnd = Util::GetProcessWindow();
    LOG_DEBUG("procHwnd: {0:X}, swapchain hwnd: {1:X}", (UINT64) procHwnd, (UINT64) pCreateInfo->hwnd);

    // && procHwnd == pCreateInfo->hwnd) // On linux sometimes procHwnd != pCreateInfo->hwnd
    if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks)
    {
        MenuOverlayVk::DestroyVulkanObjects(false);

        _instance = instance;
        LOG_DEBUG("_instance captured: {0:X}", (UINT64) _instance);
        _hwnd = pCreateInfo->hwnd;
        LOG_DEBUG("_hwnd captured: {0:X}", (UINT64) _hwnd);
    }

    LOG_FUNC_RESULT(result);

    return result;
}

static VkResult hkvkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                   VkInstance* pInstance)
{
    LOG_FUNC();

    State::Instance().skipSpoofing = true;
    auto result = o_vkCreateInstance(pCreateInfo, pAllocator, pInstance);
    State::Instance().skipSpoofing = false;

    if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks)
    {
        MenuOverlayVk::DestroyVulkanObjects(false);

        State::Instance().VulkanInstance = *pInstance;
        LOG_DEBUG("State::Instance().VulkanInstance captured: {0:X}", (UINT64) State::Instance().VulkanInstance);
    }

    LOG_FUNC_RESULT(result);

    return result;
}

static VkResult hkvkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    LOG_FUNC();

    auto result = o_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);

    if (o_vkCmdPipelineBarrier == nullptr)
    {
        o_vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier) vkGetDeviceProcAddr(*pDevice, "vkCmdPipelineBarrier");

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_vkCmdPipelineBarrier != nullptr)
            DetourAttach(&(PVOID&) o_vkCmdPipelineBarrier, hkvkCmdPipelineBarrier);

        DetourTransactionCommit();
    }

    if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks && Config::Instance()->OverlayMenu.value())
    {
        MenuOverlayVk::DestroyVulkanObjects(false);

        _PD = physicalDevice;
        LOG_DEBUG("_PD captured: {0:X}", (UINT64) _PD);
        _device = *pDevice;
        LOG_DEBUG("_device captured: {0:X}", (UINT64) _device);
        HookDevice(_device);

        State::Instance().skipSpoofing = true;

        VkPhysicalDeviceProperties prop {};
        vkGetPhysicalDeviceProperties(physicalDevice, &prop);

        auto szName = std::string(prop.deviceName);

        if (szName.size() > 0)
            State::Instance().DeviceAdapterNames[*pDevice] = szName;

        State::Instance().skipSpoofing = false;
    }

    LOG_FUNC_RESULT(result);

    return result;
}

static VkResult hkvkQueuePresentKHR(VkQueue queue, VkPresentInfoKHR* pPresentInfo)
{
    LOG_FUNC();

    // get upscaler time
    if (HooksVk::vkUpscaleTrig && HooksVk::queryPool != VK_NULL_HANDLE)
    {
        // Retrieve timestamps
        uint64_t timestamps[2];
        vkGetQueryPoolResults(_device, HooksVk::queryPool, 0, 2, sizeof(timestamps), timestamps, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT);

        // Calculate elapsed time in milliseconds
        double elapsedTimeMs = (timestamps[1] - timestamps[0]) * HooksVk::timeStampPeriod / 1e6;

        if (elapsedTimeMs > 0.0 && elapsedTimeMs < 5000.0)
        {
            State::Instance().frameTimeMutex.lock();
            State::Instance().upscaleTimes.push_back(elapsedTimeMs);
            State::Instance().upscaleTimes.pop_front();
            State::Instance().frameTimeMutex.unlock();
        }

        HooksVk::vkUpscaleTrig = false;
    }

    State::Instance().swapchainApi = Vulkan;

    // Tick feature to let it know if it's frozen
    if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)
        currentFeature->TickFrozenCheck();

    // render menu if needed
    if (!MenuOverlayVk::QueuePresent(queue, pPresentInfo))
    {
        LOG_ERROR("QueuePresent: false!");
        return VK_ERROR_OUT_OF_DATE_KHR;
    }

    ReflexHooks::update(false, true);

    // original call
    State::Instance().vulkanCreatingSC = true;
    auto result = o_QueuePresentKHR(queue, pPresentInfo);
    State::Instance().vulkanCreatingSC = false;

    // Unsure about Vulkan Reflex fps limit and if that could be causing an issue here
    if (!State::Instance().reflexLimitsFps)
        FrameLimit::sleep();

    LOG_FUNC_RESULT(result);
    return result;
}

static VkResult hkvkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                       VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    LOG_FUNC();

    State::Instance().vulkanCreatingSC = true;
    auto result = o_CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    State::Instance().vulkanCreatingSC = false;

    if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && *pSwapchain != VK_NULL_HANDLE &&
        !State::Instance().vulkanSkipHooks)
    {
        State::Instance().screenWidth = pCreateInfo->imageExtent.width;
        State::Instance().screenHeight = pCreateInfo->imageExtent.height;

        LOG_DEBUG("if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && pSwapchain != "
                  "VK_NULL_HANDLE)");

        _device = device;
        LOG_DEBUG("_device captured: {0:X}", (UINT64) _device);

        MenuOverlayVk::CreateSwapchain(device, _PD, _instance, _hwnd, pCreateInfo, pAllocator, pSwapchain);
    }

    LOG_FUNC_RESULT(result);
    return result;
}

void HooksVk::HookVk(HMODULE vulkan1)
{
    if (o_vkCreateDevice != nullptr)
        return;

    o_vkCreateDevice = (PFN_vkCreateDevice) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateDevice");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_vkCreateDevice != nullptr)
        DetourAttach(&(PVOID&) o_vkCreateDevice, hkvkCreateDevice);

    DetourTransactionCommit();

    if (Config::Instance()->OverlayMenu.value())
    {
        o_vkCreateInstance = (PFN_vkCreateInstance) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateInstance");
        o_vkCreateWin32SurfaceKHR =
            (PFN_vkCreateWin32SurfaceKHR) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateWin32SurfaceKHR");

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_vkCreateInstance != nullptr)
            DetourAttach(&(PVOID&) o_vkCreateInstance, hkvkCreateInstance);

        if (o_vkCreateWin32SurfaceKHR != nullptr)
            DetourAttach(&(PVOID&) o_vkCreateWin32SurfaceKHR, hkvkCreateWin32SurfaceKHR);

        DetourTransactionCommit();
    }
}

void HooksVk::UnHookVk()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_QueuePresentKHR != nullptr)
        DetourDetach(&(PVOID&) o_QueuePresentKHR, hkvkQueuePresentKHR);

    if (o_CreateSwapchainKHR != nullptr)
        DetourDetach(&(PVOID&) o_CreateSwapchainKHR, hkvkCreateSwapchainKHR);

    if (o_vkCreateDevice != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateDevice, hkvkCreateDevice);

    if (o_vkCreateInstance != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateInstance, hkvkCreateInstance);

    if (o_vkCreateWin32SurfaceKHR != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateWin32SurfaceKHR, hkvkCreateWin32SurfaceKHR);

    if (o_vkCmdPipelineBarrier != nullptr)
        DetourDetach(&(PVOID&) o_vkCmdPipelineBarrier, hkvkCmdPipelineBarrier);

    DetourTransactionCommit();
}
