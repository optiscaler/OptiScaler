#include "pch.h"
#include "DLSSG_Vk.h"

#include <State.h>
#include <hooks/Reflex_Hooks.h>
#include <hooks/Vulkan_Hooks.h>

#include <magic_enum.hpp>

DLSSG_Vk::DLSSG_Vk() : IFGFeature_Vk(), DLSSG_Common(), IFGFeature()
{
    // Streamline's own, its vkCreateDevice adds them for any feature
    _requiredExtensions = { VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME };
    _requiredFeatures12 = { "timelineSemaphore", "descriptorIndexing" };

    for (auto feature : { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL })
    {
        sl::FeatureRequirements requirements {};
        if (StreamlineProxy::GetFeatureRequirements()(feature, requirements) != sl::Result::eOk)
            continue;

        _graphicsQueueCount += requirements.vkNumGraphicsQueuesRequired;
        _computeQueueCount += requirements.vkNumComputeQueuesRequired;

        auto append = [](std::vector<std::string>& target, const char** names, uint32_t count)
        { target.insert(target.end(), names, names + count); };

        append(_requiredExtensions, requirements.vkDeviceExtensions, requirements.vkNumDeviceExtensions);
        append(_requiredFeatures12, requirements.vkFeatures12, requirements.vkNumFeatures12);
        append(_requiredFeatures13, requirements.vkFeatures13, requirements.vkNumFeatures13);

        // The game's instance already exists
        for (uint32_t i = 0; i < requirements.vkNumInstanceExtensions; i++)
            LOG_WARN("Can't add instance extension: {}", requirements.vkInstanceExtensions[i]);
    }

    LOG_INFO("Streamline wants {} graphics and {} compute queues", _graphicsQueueCount, _computeQueueCount);

    _extraQueues.resize(_graphicsQueueCount + _computeQueueCount);

    for (uint32_t i = 0; i < _extraQueues.size(); i++)
    {
        // DLSSG presents the generated frames from its graphics queue
        if (i < _graphicsQueueCount)
        {
            _extraQueues[i].flags = VK_QUEUE_GRAPHICS_BIT;
            _extraQueues[i].present = true;
        }
        else
        {
            _extraQueues[i].flags = VK_QUEUE_COMPUTE_BIT;
        }
    }
}

DLSSG_Vk::~DLSSG_Vk() { Shutdown(); }

// Graphics families with more queues than the game takes
static bool HasFreeGraphicsQueue(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo& createInfo)
{
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

    for (uint32_t family = 0; family < familyCount; family++)
    {
        if (!(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            continue;

        uint32_t used = 0;

        for (uint32_t i = 0; i < createInfo.queueCreateInfoCount; i++)
        {
            if (createInfo.pQueueCreateInfos[i].queueFamilyIndex == family)
                used += createInfo.pQueueCreateInfos[i].queueCount;
        }

        if (used < families[family].queueCount)
            return true;
    }

    return false;
}

void DLSSG_Vk::PrepareDevice(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo& createInfo)
{
    // Some drivers have a single graphics queue, which the game takes. A compute queue that can present stands in for
    // it, FFX presents its generated frames from one too.
    if (!HasFreeGraphicsQueue(physicalDevice, createInfo))
    {
        LOG_INFO("No free graphics queue, Streamline's graphics queues are compute ones");

        for (uint32_t i = 0; i < _graphicsQueueCount; i++)
            _extraQueues[i].flags = VK_QUEUE_COMPUTE_BIT;
    }

    IFGFeature_Vk::PrepareDevice(physicalDevice, createInfo);
    AddExtensions(physicalDevice, createInfo);
    EnableFeatures(physicalDevice, createInfo);
}

void DLSSG_Vk::AddExtensions(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo& createInfo)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, available.data());

    _deviceExtensions.assign(createInfo.ppEnabledExtensionNames,
                             createInfo.ppEnabledExtensionNames + createInfo.enabledExtensionCount);

    for (const auto& name : _requiredExtensions)
    {
        if (std::ranges::any_of(_deviceExtensions, [&](const char* enabled) { return name == enabled; }))
            continue;

        if (std::ranges::none_of(available,
                                 [&](const VkExtensionProperties& ext) { return name == ext.extensionName; }))
        {
            LOG_WARN("Device extension {} isn't supported", name);
            continue;
        }

        LOG_INFO("Adding device extension: {}", name);
        _deviceExtensions.push_back(name.c_str());
    }

    createInfo.enabledExtensionCount = (uint32_t) _deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = _deviceExtensions.data();
}

void DLSSG_Vk::EnableFeatures(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo& createInfo)
{
    auto toNames = [](const std::vector<std::string>& features)
    {
        std::vector<const char*> names;

        for (const auto& feature : features)
            names.push_back(feature.c_str());

        return names;
    };

    auto names12 = toNames(_requiredFeatures12);
    auto names13 = toNames(_requiredFeatures13);
    auto required12 = sl::getVkPhysicalDeviceVulkan12Features((uint32_t) names12.size(), names12.data());
    auto required13 = sl::getVkPhysicalDeviceVulkan13Features((uint32_t) names13.size(), names13.data());

    VkPhysicalDeviceVulkan13Features supported13 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features supported12 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                                   &supported13 };
    VkPhysicalDeviceFeatures2 supported { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &supported12 };
    vkGetPhysicalDeviceFeatures2(physicalDevice, &supported);

    bool merged12 = false;
    bool merged13 = false;

    // TODO: only the game's own Vulkan 1.2/1.3 feature structs get Streamline's features, games chaining the per
    // feature structs instead would need them added the way Streamline's vkCreateDevice does
    for (auto next = (VkBaseOutStructure*) createInfo.pNext; next != nullptr; next = next->pNext)
    {
        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
        {
            sl::getMergedSupportedVkPhysicalDeviceVulkanFeatures(next, (VkBaseOutStructure*) &required12,
                                                                 (VkBaseOutStructure*) &supported12);
            merged12 = true;
        }
        else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES)
        {
            sl::getMergedSupportedVkPhysicalDeviceVulkanFeatures(next, (VkBaseOutStructure*) &required13,
                                                                 (VkBaseOutStructure*) &supported13);
            merged13 = true;
        }
    }

    auto join = [](const std::vector<std::string>& names)
    {
        std::string result;

        for (const auto& name : names)
            result += (result.empty() ? "" : ", ") + name;

        return result;
    };

    if (!merged12)
        LOG_WARN("No Vulkan 1.2 features from the game, Streamline's aren't enabled: {}", join(_requiredFeatures12));

    if (!merged13 && !_requiredFeatures13.empty())
        LOG_WARN("No Vulkan 1.3 features from the game, Streamline's aren't enabled: {}", join(_requiredFeatures13));
}

bool DLSSG_Vk::SetupStreamline(VkDevice device)
{
    // Streamline takes one device for the process' lifetime, the last created one has the queues reserved
    if (_slDevice != VK_NULL_HANDLE || device != _device)
        return device == _slDevice;

    auto getProc = StreamlineProxy::VkGetDeviceProcAddr();

    if (getProc == nullptr || State::Instance().VulkanInstance == VK_NULL_HANDLE)
    {
        LOG_ERROR("Missing sl.interposer.dll's vkGetDeviceProcAddr or the game's VkInstance");
        return false;
    }

    // Streamline uses its queues of a kind as consecutive ones of one family. Sharing the game's instead breaks, its
    // DLSSG submits to them from its own threads.
    auto hasQueues = [&](uint32_t first, uint32_t count)
    {
        for (uint32_t i = first; i < first + count; i++)
        {
            auto& queue = _extraQueues[i];

            if (queue.queue == VK_NULL_HANDLE || queue.family != _extraQueues[first].family ||
                queue.index != _extraQueues[first].index + (i - first))
            {
                return false;
            }
        }

        return true;
    };

    if (!hasQueues(0, _graphicsQueueCount) || !hasQueues(_graphicsQueueCount, _computeQueueCount))
    {
        LOG_ERROR(
            "DLSSG needs {} graphics and {} compute queues next to the game's, the GPU driver doesn't have enough "
            "free ones (RADV for example), DLSSG is disabled",
            _graphicsQueueCount, _computeQueueCount);
        return false;
    }

    auto queueStart = [&](uint32_t first, uint32_t count)
    {
        return count > 0 ? std::pair { _extraQueues[first].family, _extraQueues[first].index }
                         : std::pair { _gameQueueFamily, 0u };
    };

    sl::VulkanInfo info {};
    info.device = _device;
    info.instance = State::Instance().VulkanInstance;
    info.physicalDevice = _physicalDevice;
    std::tie(info.graphicsQueueFamily, info.graphicsQueueIndex) = queueStart(0, _graphicsQueueCount);
    std::tie(info.computeQueueFamily, info.computeQueueIndex) = queueStart(_graphicsQueueCount, _computeQueueCount);

    LOG_INFO("Streamline's graphics queue: {}/{}, compute queue: {}/{}", info.graphicsQueueFamily,
             info.graphicsQueueIndex, info.computeQueueFamily, info.computeQueueIndex);

    if (!StreamlineProxy::SetVulkanInfo(info))
        return false;

    _sl.createSwapchain = (PFN_vkCreateSwapchainKHR) getProc(_device, "vkCreateSwapchainKHR");
    _sl.destroySwapchain = (PFN_vkDestroySwapchainKHR) getProc(_device, "vkDestroySwapchainKHR");
    _sl.getSwapchainImages = (PFN_vkGetSwapchainImagesKHR) getProc(_device, "vkGetSwapchainImagesKHR");
    _sl.acquireNextImage = (PFN_vkAcquireNextImageKHR) getProc(_device, "vkAcquireNextImageKHR");
    _sl.queuePresent = (PFN_vkQueuePresentKHR) getProc(_device, "vkQueuePresentKHR");
    _sl.setHdrMetadata = (PFN_vkSetHdrMetadataEXT) getProc(_device, "vkSetHdrMetadataEXT");

    _slDevice = _device;
    return true;
}

// Streamline's DLSSG finds a swapchain's window through the surfaces it saw being created. The game's are older than
// Streamline's setup, so Streamline gets them again from its vkCreateWin32SurfaceKHR, which the hooks answer with them.
void DLSSG_Vk::ShowSurface(VkSurfaceKHR surface)
{
    auto createSurface = (PFN_vkCreateWin32SurfaceKHR) KernelBaseProxy::GetProcAddress_()(StreamlineProxy::Module(),
                                                                                          "vkCreateWin32SurfaceKHR");

    VkWin32SurfaceCreateInfoKHR info { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    info.hinstance = GetModuleHandleW(nullptr);
    info.hwnd = VulkanHooks::SurfaceWindow(surface);

    if (createSurface == nullptr || info.hwnd == nullptr)
    {
        LOG_WARN("Can't show the swapchain's surface to Streamline");
        return;
    }

    auto instance = State::Instance().VulkanInstance;
    VkSurfaceKHR shown = VK_NULL_HANDLE;

    VulkanHooks::existingSurface = surface;
    createSurface(instance, &info, nullptr, &shown);
    VulkanHooks::existingSurface = VK_NULL_HANDLE;

    // The driver made a new one, Streamline didn't go through the hooks
    if (shown != surface && shown != VK_NULL_HANDLE)
    {
        LOG_WARN("Streamline got a new surface instead of the game's");
        vkDestroySurfaceKHR(instance, shown, nullptr);
    }
}

bool DLSSG_Vk::CreateSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                               const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    if (!SetupStreamline(device))
    {
        LOG_WARN("Streamline isn't set up for this device");
        return false;
    }

    StreamlineProxy::SetFeatureLoaded()(sl::kFeatureDLSS_G, true);

    ShowSurface(pCreateInfo->surface);

    auto result = _sl.createSwapchain(device, pCreateInfo, pAllocator, pSwapchain);

    if (result != VK_SUCCESS)
    {
        LOG_ERROR("Streamline's vkCreateSwapchainKHR error: {}", magic_enum::enum_name(result));
        return false;
    }

    QueryState();

    _swapchain = *pSwapchain;
    _swapchainFormat = pCreateInfo->imageFormat;
    _swapchainExtent = pCreateInfo->imageExtent;

    return true;
}

bool DLSSG_Vk::DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
{
    // Streamline keeps data for all the swapchains it created, the replaced ones included
    if (device != _slDevice || _sl.destroySwapchain == nullptr)
        return false;

    if (swapchain == _swapchain)
    {
        Deactivate();
        _swapchain = VK_NULL_HANDLE;
    }

    _sl.destroySwapchain(device, swapchain, pAllocator);
    return true;
}

VkResult DLSSG_Vk::GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount,
                                      VkImage* pSwapchainImages)
{
    return _sl.getSwapchainImages(device, swapchain, pSwapchainImageCount, pSwapchainImages);
}

VkResult DLSSG_Vk::AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
                                    VkFence fence, uint32_t* pImageIndex)
{
    return _sl.acquireNextImage(device, swapchain, timeout, semaphore, fence, pImageIndex);
}

VkResult DLSSG_Vk::QueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    auto& state = State::Instance();
    const bool active = IsActive() && !IsPaused();

    state.dlssgDetectedInterpolationCount = active ? GetInterpolatedFrameCount() : 0;

    // The game's Vulkan Reflex markers don't reach this Streamline, DLSSG paces the generated frames with these
    sl::FrameToken* token = nullptr;
    auto frameId = (uint32_t) FrameCount();
    const bool marked = active && StreamlineProxy::GetNewFrameToken()(token, &frameId) == sl::Result::eOk;

    ScopedOptiScalerReflex optiScalerCall {};

    if (marked)
        StreamlineProxy::PCLSetMarker()(sl::PCLMarker::ePresentStart, *token);

    auto result = _sl.queuePresent(queue, pPresentInfo);

    if (marked)
    {
        StreamlineProxy::PCLSetMarker()(sl::PCLMarker::ePresentEnd, *token);
        StreamlineProxy::ReflexSleep()(*token);
    }

    return result;
}

void DLSSG_Vk::SetHdrMetadata(VkDevice device, VkSwapchainKHR swapchain, const VkHdrMetadataEXT* pMetadata)
{
    if (_sl.setHdrMetadata != nullptr)
        _sl.setHdrMetadata(device, 1, &swapchain, pMetadata);
}

void DLSSG_Vk::EvaluateState(FG_Constants& fgConstants)
{
    OwnedLockGuard lock(Mutex, 555);

    auto& state = State::Instance();
    _constants = fgConstants;

    if (_swapchain == VK_NULL_HANDLE || state.isShuttingDown)
        return;

    if (!Config::Instance()->IsFGEnabled() || state.fgChanged)
        Deactivate();

    if (state.fgChanged)
    {
        state.fgChanged = false;
        UpdateTarget();
    }

    state.scChanged = false;
}

bool DLSSG_Vk::Present()
{
    OwnedLockGuard lock(Mutex, 2);

    // Game stopped sending new frames, menus and such
    if ((_fgFramePresentId - _lastFGFramePresentId) > 3 && IsActive() && !_waitingNewFrameData)
    {
        LOG_DEBUG("Pausing FG");
        Deactivate();
        _waitingNewFrameData = true;
        return false;
    }

    _fgFramePresentId++;

    return Dispatch();
}

bool DLSSG_Vk::Dispatch()
{
    UINT64 willDispatchFrame = 0;
    auto fIndex = GetDispatchIndex(willDispatchFrame);

    if (fIndex < 0 || !IsActive() || IsPaused())
        return false;

    auto velocity = GetResource(FG_ResourceType::Velocity, fIndex);

    if (!velocity || !HasResource(FG_ResourceType::Depth, fIndex))
    {
        LOG_WARN("Depth or Velocity is not ready, skipping");
        return false;
    }

    SetOptions(false);

    if (!SetConstants(fIndex, (uint32_t) willDispatchFrame, velocity->width, velocity->height))
        return false;

    return TagResources(fIndex);
}

// All tagged as valid until present, the ones only valid at the time the input got them are in our copies
bool DLSSG_Vk::TagResources(int fIndex)
{
    static const std::pair<FG_ResourceType, sl::BufferType> tagTypes[] = {
        { FG_ResourceType::Depth, sl::kBufferTypeDepth },
        { FG_ResourceType::Velocity, sl::kBufferTypeMotionVectors },
        { FG_ResourceType::HudlessColor, sl::kBufferTypeHUDLessColor },
        { FG_ResourceType::UIColor, sl::kBufferTypeUIColorAndAlpha },
    };

    for (auto [type, bufferType] : tagTypes)
    {
        auto resource = GetResource(type, fIndex);

        if (!resource)
            continue;

        if (resource->view == VK_NULL_HANDLE)
        {
            LOG_WARN("{} has no image view, not tagging it", magic_enum::enum_name(type));
            continue;
        }

        sl::Resource slResource { sl::ResourceType::eTex2d, (void*) resource->image, nullptr, (void*) resource->view,
                                  (uint32_t) resource->layout };
        slResource.width = resource->imageWidth;
        slResource.height = resource->imageHeight;
        slResource.nativeFormat = resource->format;
        slResource.mipLevels = 1;
        slResource.arrayLayers = 1;
        slResource.flags = 0;
        slResource.usage = VK_IMAGE_USAGE_SAMPLED_BIT;

        sl::Extent extent {};
        extent.width = resource->width;
        extent.height = resource->height;

        sl::ResourceTag tag { &slResource, bufferType, sl::ResourceLifecycle::eValidUntilPresent, &extent };

        auto result = StreamlineProxy::SetTagForFrame()(*_frameToken, _viewport, &tag, 1, nullptr);

        if (result != sl::Result::eOk)
        {
            LOG_ERROR("SetTagForFrame error for {}: {}", magic_enum::enum_name(type), magic_enum::enum_name(result));
            PauseAfterError();
            return false;
        }
    }

    return true;
}

void DLSSG_Vk::DestroyFGContext() { Deactivate(); }

// The swapchain goes with the game's vkDestroySwapchainKHR
bool DLSSG_Vk::ReleaseSwapchain(HWND hwnd)
{
    Deactivate();
    return true;
}

bool DLSSG_Vk::Shutdown()
{
    Deactivate();

    if (State::Instance().isShuttingDown && StreamlineProxy::Shutdown() != nullptr)
        StreamlineProxy::Shutdown()();

    return true;
}
