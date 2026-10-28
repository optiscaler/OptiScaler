#include "pch.h"
#include "FSRFG_Vk.h"

#include <State.h>
#include <hooks/Vulkan_Hooks.h>
#include <fsr4/FSR4DriverProviderVk.h>

#include <magic_enum.hpp>

static void fgLogCallback(uint32_t type, const wchar_t* message)
{
    auto message_str = wstring_to_string(std::wstring(message));

    if (type == FFX_API_MESSAGE_TYPE_ERROR)
        spdlog::error("FFX FG Callback: {}", message_str);
    else if (type == FFX_API_MESSAGE_TYPE_WARNING)
        spdlog::warn("FFX FG Callback: {}", message_str);
    else
        spdlog::debug("FFX FG Callback: {}", message_str);
}

static FfxApiResource GetFfxResource(const VkFGResource& resource, uint32_t state)
{
    FfxApiResourceDescription desc {};
    desc.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    desc.format = ffxApiGetSurfaceFormatVK(resource.format);
    desc.width = resource.imageWidth;
    desc.height = resource.imageHeight;
    desc.depth = 1;
    desc.mipCount = 1;
    desc.usage = FFX_API_RESOURCE_USAGE_READ_ONLY;

    if (ffxApiIsDepthFormat(resource.format))
        desc.usage |= FFX_API_RESOURCE_USAGE_DEPTHTARGET;

    return ffxApiGetResourceVK(resource.image, desc, state);
}

// Layouts that FFX can transition from, see getVKImageLayoutFromResourceState
static std::optional<uint32_t> GetFfxState(VkImageLayout layout)
{
    switch (layout)
    {
    case VK_IMAGE_LAYOUT_GENERAL:
        return FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return FFX_API_RESOURCE_STATE_COMPUTE_READ;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return FFX_API_RESOURCE_STATE_RENDER_TARGET;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return FFX_API_RESOURCE_STATE_COPY_SRC;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return FFX_API_RESOURCE_STATE_COPY_DEST;
    default:
        return std::nullopt;
    }
}

// Reverse of GetFfxState, as FFX's getVKImageLayoutFromResourceState does it
static VkImageLayout GetLayout(uint32_t state)
{
    switch (state)
    {
    case FFX_API_RESOURCE_STATE_COMPUTE_READ:
    case FFX_API_RESOURCE_STATE_PIXEL_READ:
    case FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case FFX_API_RESOURCE_STATE_COPY_SRC:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case FFX_API_RESOURCE_STATE_COPY_DEST:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case FFX_API_RESOURCE_STATE_RENDER_TARGET:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case FFX_API_RESOURCE_STATE_PRESENT:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default:
        return VK_IMAGE_LAYOUT_GENERAL;
    }
}

// Same as the DX12 FSR FG and Nvngx FG
static std::optional<float> HudCutoffThreshold()
{
    if (auto cutoff = Config::Instance()->FGHudCutoff.value_or_default(); cutoff > 0.0f)
        return cutoff / 10.0f;

    if (State::Instance().gameQuirks & GameQuirk::FSRFGHudlessMismatchFixup)
        return 0.03f;

    return std::nullopt;
}

static VkImageInfo GetImageInfo(VkImage image, VkImageView view, VkFormat format, uint32_t width, uint32_t height)
{
    return { view, image, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }, format, width, height };
}

// FFX imports its swapchain functions from vulkan-1.dll. Overlays like RTSS hook those and the driver's functions
// behind them, and don't expect the real swapchain created inside the game's swapchain call, or presents from FFX's
// threads on queues they never saw. FFX calls the driver directly, the game's swapchain still goes through them.
static void BypassOverlayHooks(HMODULE ffxModule)
{
    static bool vulkanImports = false;

    auto file = [](PVOID, HMODULE, LPCSTR pszFile) -> BOOL
    {
        vulkanImports = pszFile != nullptr && _stricmp(pszFile, "vulkan-1.dll") == 0;
        return TRUE;
    };

    auto func = [](PVOID, DWORD, LPCSTR pszFunc, PVOID* ppvFunc) -> BOOL
    {
        if (!vulkanImports || pszFunc == nullptr || ppvFunc == nullptr)
            return TRUE;

        auto deviceFunc = (PVOID) VulkanHooks::OriginalSwapchainFunction(pszFunc);

        if (deviceFunc == nullptr || *ppvFunc == deviceFunc)
            return TRUE;

        DWORD oldProtect = 0;
        if (!VirtualProtect(ppvFunc, sizeof(PVOID), PAGE_READWRITE, &oldProtect))
        {
            LOG_WARN("Can't redirect FFX's {}", pszFunc);
            return TRUE;
        }

        *ppvFunc = deviceFunc;
        VirtualProtect(ppvFunc, sizeof(PVOID), oldProtect, &oldProtect);
        LOG_DEBUG("FFX's {} goes to the driver", pszFunc);

        return TRUE;
    };

    DetourEnumerateImportsEx(ffxModule, nullptr, file, func);
}

// The ones FFX's getRealSwapchainCreateInfo accepts
static size_t SwapchainExtensionSize(VkStructureType type)
{
    switch (type)
    {
    case VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT:
        return sizeof(VkImageCompressionControlEXT);
    case VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO:
        return sizeof(VkImageFormatListCreateInfo);
    case VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT:
        return sizeof(VkSurfaceFullScreenExclusiveInfoEXT);
    case VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT:
        return sizeof(VkSurfaceFullScreenExclusiveWin32InfoEXT);
    case VK_STRUCTURE_TYPE_SWAPCHAIN_COUNTER_CREATE_INFO_EXT:
        return sizeof(VkSwapchainCounterCreateInfoEXT);
    case VK_STRUCTURE_TYPE_SWAPCHAIN_DISPLAY_NATIVE_HDR_CREATE_INFO_AMD:
        return sizeof(VkSwapchainDisplayNativeHdrCreateInfoAMD);
    case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT:
        return sizeof(VkSwapchainPresentModesCreateInfoEXT);
    default:
        return 0;
    }
}

FSRFG_Vk::FSRFG_Vk() : IFGFeature_Vk(), IFGFeature()
{
    _extraQueues.resize(3);
    _extraQueues[PresentQueue].present = true;
    _extraQueues[AsyncComputeQueue].flags = VK_QUEUE_COMPUTE_BIT;
}

FSRFG_Vk::~FSRFG_Vk() { Shutdown(); }

bool FSRFG_Vk::CreateSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                               const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    auto& presentQueue = _extraQueues[PresentQueue];
    auto& acquireQueue = _extraQueues[ImageAcquireQueue];
    auto& asyncQueue = _extraQueues[AsyncComputeQueue];

    if (device != _device || _gameQueue == VK_NULL_HANDLE || presentQueue.queue == VK_NULL_HANDLE ||
        acquireQueue.queue == VK_NULL_HANDLE)
    {
        LOG_WARN("Queues for the FG swapchain are missing");
        return false;
    }

    if (pCreateInfo->oldSwapchain != VK_NULL_HANDLE && !OwnsSwapchain(pCreateInfo->oldSwapchain))
    {
        LOG_WARN("Old swapchain isn't an FG one");
        return false;
    }

    // FFX handles a single swapchain, taking another window's would destroy the one the game still renders to
    if (_swapchain != VK_NULL_HANDLE && pCreateInfo->surface != _surface)
    {
        LOG_INFO("Swapchain is for another surface, leaving it to the game");
        return false;
    }

    // Only one swapchain can exist for a window
    if (_swapchain != VK_NULL_HANDLE)
    {
        _retiredSwapchain = _swapchain;
        ReleaseSwapchainContext();
    }

    LOG_INFO("Format: {}, color space: {}, extent: {}x{}, images: {}, usage: {:X}, flags: {:X}, present mode: {}",
             (UINT) pCreateInfo->imageFormat, (UINT) pCreateInfo->imageColorSpace, pCreateInfo->imageExtent.width,
             pCreateInfo->imageExtent.height, pCreateInfo->minImageCount, (UINT) pCreateInfo->imageUsage,
             (UINT) pCreateInfo->flags, (UINT) pCreateInfo->presentMode);

    VkSwapchainCreateInfoKHR createInfo = *pCreateInfo;
    createInfo.oldSwapchain = VK_NULL_HANDLE;
    createInfo.pNext = nullptr;

    // FFX fails on extension structs its real swapchain doesn't take, like the low latency one
    std::vector<std::vector<std::byte>> extensions;

    for (auto next = (const VkBaseInStructure*) pCreateInfo->pNext; next != nullptr; next = next->pNext)
    {
        auto size = SwapchainExtensionSize(next->sType);

        if (size == 0)
        {
            LOG_DEBUG("Dropping swapchain extension struct: {}", (UINT) next->sType);
            continue;
        }

        auto& copy = extensions.emplace_back(size);
        std::memcpy(copy.data(), next, size);

        auto copyHeader = (VkBaseOutStructure*) copy.data();
        copyHeader->pNext = (VkBaseOutStructure*) createInfo.pNext;
        createInfo.pNext = copyHeader;
    }

    auto queueInfo = [&](const ExtraQueue& queue)
    {
        // FFX validates the family of an unused queue too
        return VkQueueInfoFFXAPI { queue.queue, queue.queue != VK_NULL_HANDLE ? queue.family : _gameQueueFamily,
                                   nullptr };
    };

    QueryVersions();
    _swapchainOutdated = false;

    // ML FG only works with the swapchain of the same module, the version can't be switched without recreating it
    _ffxSwapchain = FfxApiProxy::VkFfxModule_FG();

    if (IsDriverVersionSelected())
    {
        if (FSR4DriverProviderVk::IsFgSupported(device, _physicalDevice, &VulkanHooks::GetDeviceProcAddr))
            _ffxSwapchain = FSR4DriverProviderVk::FgSwapchainModule();
        else
            LOG_WARN("Driver provided FG is not supported on this device, using the SDK one");
    }

    if (_ffxSwapchain->CreateContext == nullptr)
    {
        LOG_WARN("No FFX swapchain provider");
        return false;
    }

    LOG_INFO("Using {} FG swapchain from {}", UsesDriver() ? "driver" : "SDK",
             Util::ModulePath(_ffxSwapchain->dll).filename().string());

    ffxCreateContextDescFrameGenerationSwapChainVersionVKV2 swapchainVersion {};
    swapchainVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VERSION_VK_V2;
    swapchainVersion.version = FFX_FGSWAPCHAIN_VERSION_VK_V2;

    ffxCreateContextDescFrameGenerationSwapChainVK desc {};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;

    if (_ffxSwapchain->vkBackendV2)
        desc.header.pNext = &swapchainVersion.header;

    desc.physicalDevice = _physicalDevice;
    desc.device = device;
    desc.swapchain = &_swapchain;
    desc.allocator = const_cast<VkAllocationCallbacks*>(pAllocator);
    desc.createInfo = createInfo;
    desc.gameQueue = { _gameQueue, _gameQueueFamily, nullptr };
    desc.asyncComputeQueue = queueInfo(asyncQueue);
    desc.presentQueue = queueInfo(presentQueue);
    desc.imageAcquireQueue = queueInfo(acquireQueue);

    if (!UsesDriver())
    {
        BypassOverlayHooks(_ffxSwapchain->dll);

        // Logs why the swapchain can't be created, SDK 2.x takes it without a context
        if (_ffxSwapchain->vkBackendV2)
        {
            ffxConfigureDescGlobalDebug1 fgLogging {};
            fgLogging.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
            fgLogging.fpMessage = &fgLogCallback;
            fgLogging.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_VERBOSE;
            _ffxSwapchain->Configure(nullptr, &fgLogging.header);
        }
    }

    auto result = _ffxSwapchain->CreateContext(&_swapChainContext, &desc.header, nullptr);
    LOG_INFO("Swapchain context creation result: {}", FfxApiProxy::ReturnCodeToString(result));

    if (result != FFX_API_RETURN_OK)
    {
        _swapChainContext = nullptr;
        _swapchain = VK_NULL_HANDLE;
        return false;
    }

    _functions = {};
    _functions.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
    _ffxSwapchain->Query(&_swapChainContext, &_functions.header);

    _surface = pCreateInfo->surface;
    _swapchainAllocator = pAllocator;
    _swapchainFormat = pCreateInfo->imageFormat;
    _swapchainExtent = pCreateInfo->imageExtent;
    *pSwapchain = _swapchain;

    return true;
}

bool FSRFG_Vk::DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
{
    if (swapchain == VK_NULL_HANDLE)
        return false;

    // The new FFX swapchain can get the old one's address, so check this first
    if (swapchain == _retiredSwapchain)
    {
        _retiredSwapchain = VK_NULL_HANDLE;
        return true;
    }

    if (!OwnsSwapchain(swapchain))
        return false;

    ReleaseSwapchainContext();
    return true;
}

VkResult FSRFG_Vk::GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount,
                                      VkImage* pSwapchainImages)
{
    return _functions.pOutGetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages);
}

VkResult FSRFG_Vk::AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
                                    VkFence fence, uint32_t* pImageIndex)
{
    // Nothing gets acquired or signaled, the game has to recreate the swapchain
    if (_swapchainOutdated)
        return VK_ERROR_OUT_OF_DATE_KHR;

    return _functions.pOutAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
}

VkResult FSRFG_Vk::QueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    static bool warned = false;
    if (queue != _gameQueue && !warned)
    {
        warned = true;
        LOG_WARN("Game presents from a different queue than the one given to FFX");
    }

    return _functions.pOutQueuePresentKHR(queue, pPresentInfo);
}

void FSRFG_Vk::SetHdrMetadata(VkDevice device, VkSwapchainKHR swapchain, const VkHdrMetadataEXT* pMetadata)
{
    if (_functions.pOutSetHdrMetadataEXT != nullptr)
        _functions.pOutSetHdrMetadataEXT(device, 1, &swapchain, pMetadata);
}

void FSRFG_Vk::ReleaseSwapchainContext()
{
    OwnedLockGuard lock(Mutex, 1);

    DestroyFGContext();

    if (_swapChainContext != nullptr)
    {
        // Presents still in flight would use the swapchain after it's destroyed
        ffxDispatchDescFrameGenerationSwapChainWaitForPresentsVK waitDesc {};
        waitDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_FGSWAPCHAIN_WAIT_FOR_PRESENTS_VK;
        auto waitResult = _ffxSwapchain->Dispatch(&_swapChainContext, &waitDesc.header);
        LOG_DEBUG("Wait for presents result: {}", FfxApiProxy::ReturnCodeToString(waitResult));

        auto result = _ffxSwapchain->DestroyContext(&_swapChainContext, nullptr);
        LOG_INFO("Destroy swapchain context result: {}", FfxApiProxy::ReturnCodeToString(result));
        _swapChainContext = nullptr;

        // SDK 2.x leaves the swapchain alive after its context, with it the window it presents to.
        // SDK 1.x and the driver destroy it with the context
        if (!UsesDriver() && _ffxSwapchain->vkBackendV2 && _swapchain != VK_NULL_HANDLE &&
            _functions.pOutDestroySwapchainFFXAPI != nullptr)
        {
            _functions.pOutDestroySwapchainFFXAPI(_device, _swapchain, _swapchainAllocator, _swapchain);
            LOG_INFO("Destroyed the FG swapchain");
        }

        // It holds the DXGI swapchain under the Vulkan one
        if (State::Instance().amdFlipMeterCreated)
            LOG_WARN("The swapchain's IAmdExtFlipMeter is still alive, its window may stay in use");
    }

    for (auto& [image, view] : _presentViews)
        vkDestroyImageView(_device, view, nullptr);

    _presentViews.clear();
    _swapchain = VK_NULL_HANDLE;
    _surface = VK_NULL_HANDLE;
    _functions = {};
}

bool FSRFG_Vk::ReleaseSwapchain(HWND hwnd)
{
    ReleaseSwapchainContext();
    return true;
}

bool FSRFG_Vk::Shutdown()
{
    ReleaseSwapchainContext();
    DestroyCommandBuffers();

    for (auto& copy : _hudlessCopies)
        DestroyCopy(copy);

    _hudCopy.reset();
    _hudlessCompare.reset();

    return true;
}

void FSRFG_Vk::CreateContext()
{
    auto& state = State::Instance();
    auto config = Config::Instance();

    QueryVersions();

    const bool driver = UsesDriver();

    if (driver != IsDriverVersionSelected() && !_swapchainOutdated)
    {
        LOG_INFO("Selected FG version needs a {} swapchain, making the game recreate it", driver ? "SDK" : "driver");
        _swapchainOutdated = true;
    }

    _ffx = driver ? FSR4DriverProviderVk::FgModule() : FfxApiProxy::VkFfxModule_FG();

    ffxCreateBackendVKDesc backendDesc {};
    backendDesc.header.type = FfxApiProxy::VkBackendType(_ffx);
    backendDesc.vkDevice = _device;
    backendDesc.vkPhysicalDevice = _physicalDevice;
    backendDesc.vkDeviceProcAddr = &VulkanHooks::GetDeviceProcAddr;

    ffxCreateContextDescFrameGeneration createFg {};
    createFg.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    createFg.header.pNext = &backendDesc.header;
    createFg.displaySize = { _swapchainExtent.width, _swapchainExtent.height };
    createFg.backBufferFormat = ffxApiGetSurfaceFormatVK(_swapchainFormat);

    if (_constants.displayWidth != 0 && _constants.displayHeight != 0)
        createFg.maxRenderSize = { _constants.displayWidth, _constants.displayHeight };
    else
        createFg.maxRenderSize = createFg.displaySize;

    auto flags = _constants.flags;

    if (flags & FG_Flags::Hdr)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;

    if (flags & FG_Flags::InvertedDepth)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;

    if (flags & FG_Flags::JitteredMVs)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

    if ((flags & FG_Flags::DisplayResolutionMVs) && !(state.gameQuirks & GameQuirk::ForceFGRenderSizeMVs))
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;

    if (flags & FG_Flags::Async)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;

    if (flags & FG_Flags::InfiniteDepth)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;

    if (spdlog::default_logger()->level() == SPDLOG_LEVEL_TRACE)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;

    ffxOverrideVersion override {};
    override.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;

    ffxCreateContextDescFrameGenerationVersionV2 fgVersion {};
    fgVersion.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION_V2;
    fgVersion.version = FFX_FRAMEGENERATION_VERSION_V2;

    if (driver)
    {
        LOG_INFO("Using driver provided FG {}", FSR4DriverProviderVk::FgVersionName());

        // Version override is not needed
        backendDesc.header.pNext = &fgVersion.header;
        _version.parse_version(FSR4DriverProviderVk::FgVersionName());
    }
    else if (!state.ffxFGVersionIds.empty())
    {
        auto index = config->FfxFGIndex.value_or_default();

        // Driver entry is last, the SDK's default is the first one
        if (IsDriverVersionSelected())
            index = 0;

        override.versionId = state.ffxFGVersionIds[index];
        _version.parse_version(state.ffxFGVersionNames[index]);

        override.header.pNext = createFg.header.pNext;
        createFg.header.pNext = &override.header;
    }

    if (_ffx->CreateContext == nullptr)
    {
        LOG_ERROR("No FFX FG provider");
        _fgContext = nullptr;
        _isActive = false;
        return;
    }

    ffxReturnCode_t result;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        result = _ffx->CreateContext(&_fgContext, &createFg.header, nullptr);
    }

    LOG_INFO("FG context creation result: {}", FfxApiProxy::ReturnCodeToString(result));

    _contextFlags = flags;
    _isActive = result == FFX_API_RETURN_OK;
    _lastDispatchedFrame = 0;

    if (!_isActive)
    {
        _fgContext = nullptr;
        return;
    }

    ffxConfigureDescGlobalDebug1 fgLogging {};
    fgLogging.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
    fgLogging.fpMessage = &fgLogCallback;
    fgLogging.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_VERBOSE;
    _ffx->Configure(&_fgContext, &fgLogging.header);

    ConfigureFramePaceTuning();
}

void FSRFG_Vk::ConfigureFramePaceTuning()
{
    State::Instance().fsrfgFramePaceTuningChanged = false;

    auto config = Config::Instance();

    // Versions are FFX SDK ones here, 1.1.3 shipped FSR 3.1.3 which added the tuning
    if (_swapChainContext == nullptr || _version < feature_version { 1, 1, 3 } ||
        !config->FGFramePacingTuning.value_or_default())
        return;

    // The Vulkan swapchain only reads safetyMarginInMs and varianceFactor
    FfxApiSwapchainFramePacingTuning fpt {};
    fpt.safetyMarginInMs = config->FGFPTSafetyMarginInMs.value_or_default();
    fpt.varianceFactor = config->FGFPTVarianceFactor.value_or_default();

    ffxConfigureDescFrameGenerationSwapChainKeyValueVK cfgDesc {};
    cfgDesc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_KEYVALUE_VK;
    cfgDesc.key = FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING_VK;
    cfgDesc.ptr = &fpt;

    auto result = _ffxSwapchain->Configure(&_swapChainContext, &cfgDesc.header);
    LOG_DEBUG("Frame pacing tuning configure result: {}", FfxApiProxy::ReturnCodeToString(result));
}

// SDK versions followed by the driver's ML FG
void FSRFG_Vk::QueryVersions()
{
    auto& state = State::Instance();
    FfxApiProxy::VULKAN_QueryFgVersions(state.ffxFGVersionIds, state.ffxFGVersionNames);

    auto config = Config::Instance();

    if (config->FfxFGIndex.value_or_default() < 0 ||
        config->FfxFGIndex.value_or_default() >= state.ffxFGVersionIds.size())
    {
        config->FfxFGIndex.set_volatile_value(0);
    }
}

bool FSRFG_Vk::IsDriverVersionSelected()
{
    auto& ids = State::Instance().ffxFGVersionIds;
    auto index = Config::Instance()->FfxFGIndex.value_or_default();

    return FSR4DriverProviderVk::IsFgAvailable() && index >= 0 && index < ids.size() &&
           ids[index] == FSR4DriverProviderVk::FgVersionId();
}

bool FSRFG_Vk::UsesDriver() const { return _ffxSwapchain == FSR4DriverProviderVk::FgSwapchainModule(); }

void FSRFG_Vk::DestroyFGContext()
{
    Deactivate();

    if (_fgContext == nullptr)
        return;

    // Our prepare commands use the context's resources
    WaitForCommandBuffers();

    auto result = _ffx->DestroyContext(&_fgContext, nullptr);
    LOG_INFO("Destroy FG context result: {}", FfxApiProxy::ReturnCodeToString(result));
    _fgContext = nullptr;
}

ffxReturnCode_t FSRFG_Vk::ConfigureEnabled(bool enabled)
{
    ffxConfigureDescFrameGeneration fgConfig {};
    fgConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    fgConfig.frameGenerationEnabled = enabled;
    fgConfig.swapChain = _swapchain;

    auto result = _ffx->Configure(&_fgContext, &fgConfig.header);
    LOG_INFO("Enabled: {}, result: {}", enabled, FfxApiProxy::ReturnCodeToString(result));

    return result;
}

void FSRFG_Vk::Activate()
{
    if (_fgContext == nullptr || _swapchain == VK_NULL_HANDLE || _isActive)
        return;

    if (ConfigureEnabled(true) == FFX_API_RETURN_OK)
    {
        _isActive = true;
        _lastDispatchedFrame = 0;
    }
}

void FSRFG_Vk::Deactivate()
{
    if (!_isActive)
        return;

    if (_fgContext == nullptr || ConfigureEnabled(false) == FFX_API_RETURN_OK)
        _isActive = false;
}

void FSRFG_Vk::EvaluateState(FG_Constants& fgConstants)
{
    OwnedLockGuard lock(Mutex, 555);

    auto& state = State::Instance();
    _constants = fgConstants;

    if (_swapchain == VK_NULL_HANDLE)
        return;

    if (state.isShuttingDown)
    {
        DestroyFGContext();
        return;
    }

    if (_fgContext != nullptr && fgConstants.flags != _contextFlags)
    {
        LOG_DEBUG("FG flags changed, recreating the context");
        state.fgChanged = true;
        state.scChanged = true;
    }

    if (Config::Instance()->IsFGEnabled())
    {
        if (_fgContext == nullptr)
        {
            CreateContext();
            UpdateTarget();

            // Each failed creation leaks in FFX
            if (_fgContext == nullptr)
            {
                LOG_ERROR("Can't create the FG context, disabling FG");
                Config::Instance()->FGEnabled.set_volatile_value(false);
            }
        }
        else if (state.fgChanged)
        {
            Deactivate();
            UpdateTarget();

            if (state.scChanged)
                DestroyFGContext();
        }
    }
    else
    {
        if (IsActive())
            Deactivate();

        // The change flags are cleared below, so a pending recreation (e.g. FFX FG version) must happen now
        if (state.scChanged)
            DestroyFGContext();
    }

    if (state.fgChanged)
    {
        state.fgChanged = false;
        UpdateTarget();
    }

    state.scChanged = false;
}

bool FSRFG_Vk::Present()
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

bool FSRFG_Vk::Dispatch()
{
    if (_fgContext == nullptr)
        return false;

    UINT64 willDispatchFrame = 0;
    auto fIndex = GetDispatchIndex(willDispatchFrame);

    if (fIndex < 0 || !IsActive() || IsPaused())
        return false;

    if (State::Instance().fsrfgFramePaceTuningChanged)
        ConfigureFramePaceTuning();

    auto depth = GetResource(FG_ResourceType::Depth, fIndex);
    auto velocity = GetResource(FG_ResourceType::Velocity, fIndex);

    if (!depth || !velocity)
    {
        LOG_WARN("Depth or Velocity is not ready, skipping");
        return false;
    }

    auto& state = State::Instance();
    auto config = Config::Instance();

    auto cmdBuffer = BeginCommandBuffer(fIndex);

    if (cmdBuffer == VK_NULL_HANDLE)
        return false;

    ffxConfigureDescFrameGeneration fgConfig {};
    fgConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    fgConfig.swapChain = _swapchain;
    fgConfig.frameGenerationEnabled = _isActive;
    fgConfig.allowAsyncWorkloads =
        config->FGAsync.value_or_default() && _extraQueues[AsyncComputeQueue].queue != VK_NULL_HANDLE;
    fgConfig.onlyPresentGenerated = state.fgOnlyGenerated;
    fgConfig.frameID = willDispatchFrame;

    if (config->FGDebugView.value_or_default())
        fgConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW;

    if (config->FGDebugTearLines.value_or_default())
        fgConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES;

    if (config->FGDebugResetLines.value_or_default())
        fgConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_RESET_INDICATORS;

    if (config->FGDebugPacingLines.value_or_default())
        fgConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES;

    // Zero size means the whole swapchain
    auto rectWidth = (uint32_t) _interpolationWidth[fIndex];
    auto rectHeight = _interpolationHeight[fIndex];

    // Bigger than the swapchain, like a window smaller than the game's render size. The game scales the image down
    // afterwards so neither the rect nor the hudless match what gets presented.
    const bool fitsSwapchain = rectWidth <= _swapchainExtent.width && rectHeight <= _swapchainExtent.height;

    if (!fitsSwapchain)
    {
        LOG_TRACE("Interpolation rect {}x{} is bigger than the swapchain, ignoring it and hudless", rectWidth,
                  rectHeight);
        rectWidth = 0;
        rectHeight = 0;
    }

    _hudlessInCopy[fIndex] = false;

    if (auto hudless = GetResource(FG_ResourceType::HudlessColor, fIndex); hudless && fitsSwapchain)
    {
        // FFX only accepts a few formats close to the swapchain's, converting avoids that.
        // The HUD shaders change and read our copy.
        if (hudless->format != _swapchainFormat || HudCutoffThreshold().has_value() || state.fgHudlessCompare)
        {
            // Already copied by SetResource in the right format, no need for another one
            if (auto own = OwnCopy(*hudless, (int) fIndex); own != nullptr && hudless->format == _swapchainFormat)
            {
                DestroyCopy(_hudlessCopies[fIndex]);
                _hudlessTarget[fIndex] = *own;
                _hudlessInCopy[fIndex] = true;
            }
            else
            {
                _hudlessInCopy[fIndex] = CopyImage(cmdBuffer, *hudless, _hudlessCopies[fIndex], _swapchainFormat);
                _hudlessTarget[fIndex] = _hudlessCopies[fIndex];
            }

            if (!_hudlessInCopy[fIndex])
                hudless.reset();
        }

        if (auto hudlessState = hudless ? GetFfxState(hudless->layout) : std::nullopt; hudlessState.has_value())
            fgConfig.HUDLessColor = GetFfxResource(*hudless, hudlessState.value());
        else if (hudless)
            LOG_WARN("Hudless layout {} isn't supported", magic_enum::enum_name(hudless->layout));
    }

    fgConfig.generationRect.left =
        config->FGRectLeft.value_or(_interpolationLeft[fIndex].value_or((_swapchainExtent.width - rectWidth) / 2));
    fgConfig.generationRect.top =
        config->FGRectTop.value_or(_interpolationTop[fIndex].value_or((_swapchainExtent.height - rectHeight) / 2));
    fgConfig.generationRect.width = config->FGRectWidth.value_or(rectWidth);
    fgConfig.generationRect.height = config->FGRectHeight.value_or(rectHeight);

    fgConfig.frameGenerationCallbackUserContext = this;
    fgConfig.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t
    { return reinterpret_cast<FSRFG_Vk*>(pUserCtx)->DispatchCallback(params); };

    auto result = _ffx->Configure(&_fgContext, &fgConfig.header);
    LOG_DEBUG("Configure result: {}, frame: {}, fIndex: {}", FfxApiProxy::ReturnCodeToString(result), willDispatchFrame,
              fIndex);

    // The begun command buffer is just reset on the next use
    if (result != FFX_API_RETURN_OK || !_isActive)
        return false;

    ffxDispatchDescFrameGenerationPrepareCameraInfo dfgCameraData {};
    dfgCameraData.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;

    std::memcpy(dfgCameraData.cameraPosition, _cameraPosition[fIndex], 3 * sizeof(float));
    std::memcpy(dfgCameraData.cameraUp, _cameraUp[fIndex], 3 * sizeof(float));
    std::memcpy(dfgCameraData.cameraRight, _cameraRight[fIndex], 3 * sizeof(float));
    std::memcpy(dfgCameraData.cameraForward, _cameraForward[fIndex], 3 * sizeof(float));

    ffxDispatchDescFrameGenerationPrepare dfgPrepare {};
    dfgPrepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;

    // Hacky way but should do
    const float* cameraDataRaw = dfgCameraData.cameraPosition;
    bool cameraDataIsZeroed = std::all_of(cameraDataRaw, cameraDataRaw + 12, [](float v) { return v == 0.0f; });

    // Don't link the camera struct if it's just all zeros
    // Specifically the driver upgrade dll is looking at this and falling back to FSR 3 FG
    if (!cameraDataIsZeroed)
        dfgPrepare.header.pNext = &dfgCameraData.header;

    dfgPrepare.commandList = cmdBuffer;
    dfgPrepare.frameID = willDispatchFrame;
    dfgPrepare.flags = fgConfig.flags;
    dfgPrepare.renderSize = { depth->width, depth->height };
    dfgPrepare.jitterOffset = { _jitterX[fIndex], _jitterY[fIndex] };
    dfgPrepare.motionVectorScale = { _mvScaleX[fIndex], _mvScaleY[fIndex] };
    dfgPrepare.cameraFar = _cameraFar[fIndex];
    dfgPrepare.cameraNear = _cameraNear[fIndex];
    dfgPrepare.cameraFovAngleVertical = _cameraVFov[fIndex];
    dfgPrepare.viewSpaceToMetersFactor = _meterFactor[fIndex];
    dfgPrepare.frameTimeDelta = config->FTInput.value_or_default() == FrameTimeSource::Input
                                    ? (float) _ftDelta[fIndex]
                                    : (float) state.lastFGFrameTime;

    // Read as shader resources, the game gets back its layouts after
    const auto readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dfgPrepare.depth = GetFfxResource(*depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dfgPrepare.motionVectors = GetFfxResource(*velocity, FFX_API_RESOURCE_STATE_COMPUTE_READ);

    ImageBarrier(cmdBuffer, depth->image, depth->format, depth->layout, readLayout);
    ImageBarrier(cmdBuffer, velocity->image, velocity->format, velocity->layout, readLayout);

    result = _ffx->Dispatch(&_fgContext, &dfgPrepare.header);
    LOG_DEBUG("Prepare result: {}, frame: {}, fIndex: {}", FfxApiProxy::ReturnCodeToString(result), willDispatchFrame,
              fIndex);

    ImageBarrier(cmdBuffer, depth->image, depth->format, readLayout, depth->layout);
    ImageBarrier(cmdBuffer, velocity->image, velocity->format, readLayout, velocity->layout);

    vkEndCommandBuffer(cmdBuffer);

    // FFX dispatches the interpolation on the game queue too, which keeps it after the prepare
    VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkResetFences(_device, 1, &_fence[fIndex]);

    if (auto submitResult = vkQueueSubmit(_gameQueue, 1, &submitInfo, _fence[fIndex]); submitResult != VK_SUCCESS)
    {
        LOG_ERROR("vkQueueSubmit error: {}", magic_enum::enum_name(submitResult));
        return false;
    }

    return result == FFX_API_RETURN_OK;
}

ffxReturnCode_t FSRFG_Vk::DispatchCallback(ffxDispatchDescFrameGeneration* params)
{
    auto fIndex = params->frameID % BUFFER_COUNT;
    auto& state = State::Instance();
    auto config = Config::Instance();

    params->reset = !config->FGSkipReset.value_or_default() && _reset[fIndex] != 0;

    // Async needs consecutive frame ids unless resetting, they skip after pauses
    if (params->frameID != _lastDispatchedFrameId + 1 && !params->reset)
    {
        LOG_DEBUG("Frame id {} after {}, resetting", params->frameID, _lastDispatchedFrameId);
        params->reset = true;
    }

    if (params->frameID == _lastDispatchedFrameId)
    {
        LOG_WARN("Dispatched with the same frame id! frameID: {}", params->frameID);
        params->numGeneratedFrames = 0;
        return FFX_API_RETURN_OK;
    }

    _lastDispatchedFrameId = params->frameID;

    if (!config->IsFGEnabled() || _fgContext == nullptr || state.fgChanged || state.scChanged || !IsActive())
    {
        LOG_WARN("Cancel dispatch, frameID: {}", params->frameID);
        params->numGeneratedFrames = 0;
    }

    DispatchHudShaders(params);

    return _ffx->Dispatch(&_fgContext, &params->header);
}

void FSRFG_Vk::DispatchHudShaders(ffxDispatchDescFrameGeneration* params)
{
    auto fIndex = params->frameID % BUFFER_COUNT;
    auto cmdBuffer = (VkCommandBuffer) params->commandList;
    auto threshold = HudCutoffThreshold();
    auto& state = State::Instance();

    if (cmdBuffer == VK_NULL_HANDLE || params->presentColor.resource == nullptr || !_hudlessInCopy[fIndex] ||
        (!threshold.has_value() && !state.fgHudlessCompare))
    {
        return;
    }

    auto& copy = _hudlessTarget[fIndex];
    auto hudlessInfo = GetImageInfo(copy.image, copy.view, copy.format, copy.width, copy.height);
    const auto hudlessLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    auto presentImage = (VkImage) params->presentColor.resource;
    auto presentInfo = GetImageInfo(presentImage, PresentView(presentImage), _swapchainFormat,
                                    params->presentColor.description.width, params->presentColor.description.height);
    auto presentLayout = GetLayout(params->presentColor.state);

    if (presentInfo.ImageView == VK_NULL_HANDLE)
        return;

    if (threshold.has_value())
    {
        if (_hudCopy == nullptr)
            _hudCopy = std::make_unique<HudCopy_Vk>("HudCopy", _device, _physicalDevice);

        // Hudless takes the present's colors where the two barely differ, so a slightly off hudless doesn't show
        // up as UI. Unlike changing the present, the real frames stay as the game made them.
        if (_hudCopy->IsInit())
            _hudCopy->Dispatch(cmdBuffer, presentInfo, presentLayout, hudlessInfo, hudlessLayout, threshold.value());
    }

    if (state.fgHudlessCompare)
    {
        if (_hudlessCompare == nullptr)
            _hudlessCompare = std::make_unique<HCC_Vk>("HudlessCompare", _device, _physicalDevice);

        if (_hudlessCompare->IsInit())
            _hudlessCompare->Dispatch(cmdBuffer, hudlessInfo, hudlessLayout, presentInfo, presentLayout);
    }
}

VkImageView FSRFG_Vk::PresentView(VkImage image)
{
    if (auto it = _presentViews.find(image); it != _presentViews.end())
        return it->second;

    VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = _swapchainFormat;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkImageView view = VK_NULL_HANDLE;

    if (vkCreateImageView(_device, &viewInfo, nullptr, &view) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateImageView failed for the present image");
        return VK_NULL_HANDLE;
    }

    _presentViews[image] = view;
    return view;
}
