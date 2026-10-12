#include "pch.h"
#include "FSRFG_Vk.h"

#include <State.h>
#include <hooks/Vulkan_Hooks.h>

#include <magic_enum.hpp>

static void fgLogCallback(uint32_t type, const wchar_t* message)
{
    auto message_str = wstring_to_string(std::wstring(message));

    if (type == FFX_API_MESSAGE_TYPE_ERROR)
        spdlog::error("FFX FG Callback: {}", message_str);
    else if (type == FFX_API_MESSAGE_TYPE_WARNING)
        spdlog::warn("FFX FG Callback: {}", message_str);
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

    ffxCreateContextDescFrameGenerationSwapChainVK desc {};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
    desc.physicalDevice = _physicalDevice;
    desc.device = device;
    desc.swapchain = &_swapchain;
    desc.allocator = const_cast<VkAllocationCallbacks*>(pAllocator);
    desc.createInfo = createInfo;
    desc.gameQueue = { _gameQueue, _gameQueueFamily, nullptr };
    desc.asyncComputeQueue = queueInfo(asyncQueue);
    desc.presentQueue = queueInfo(presentQueue);
    desc.imageAcquireQueue = queueInfo(acquireQueue);

    auto result = FfxApiProxy::VULKAN_CreateContext()(&_swapChainContext, &desc.header, nullptr);
    LOG_INFO("Swapchain context creation result: {}", FfxApiProxy::ReturnCodeToString(result));

    if (result != FFX_API_RETURN_OK)
    {
        _swapChainContext = nullptr;
        _swapchain = VK_NULL_HANDLE;
        return false;
    }

    _functions = {};
    _functions.header.type = FFX_API_QUERY_DESC_TYPE_FGSWAPCHAIN_FUNCTIONS_VK;
    FfxApiProxy::VULKAN_Query()(&_swapChainContext, &_functions.header);

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
        auto result = FfxApiProxy::VULKAN_DestroyContext()(&_swapChainContext, nullptr);
        LOG_INFO("Destroy swapchain context result: {}", FfxApiProxy::ReturnCodeToString(result));
        _swapChainContext = nullptr;
    }

    for (auto& [image, view] : _presentViews)
        vkDestroyImageView(_device, view, nullptr);

    _presentViews.clear();
    _swapchain = VK_NULL_HANDLE;
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

    ffxQueryDescGetVersions versionQuery {};
    versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    uint64_t versionCount = 0;
    versionQuery.outputCount = &versionCount;
    FfxApiProxy::VULKAN_Query()(nullptr, &versionQuery.header);

    state.ffxFGVersionIds.resize(versionCount);
    state.ffxFGVersionNames.resize(versionCount);
    versionQuery.versionIds = state.ffxFGVersionIds.data();
    versionQuery.versionNames = state.ffxFGVersionNames.data();
    FfxApiProxy::VULKAN_Query()(nullptr, &versionQuery.header);

    ffxCreateBackendVKDesc backendDesc {};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
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

    if (versionCount > 0)
    {
        if (config->FfxFGIndex.value_or_default() < 0 || config->FfxFGIndex.value_or_default() >= versionCount)
            config->FfxFGIndex.set_volatile_value(0);

        auto index = config->FfxFGIndex.value_or_default();
        override.versionId = state.ffxFGVersionIds[index];
        _version.parse_version(state.ffxFGVersionNames[index]);

        override.header.pNext = createFg.header.pNext;
        createFg.header.pNext = &override.header;
    }

    ffxReturnCode_t result;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        result = FfxApiProxy::VULKAN_CreateContext()(&_fgContext, &createFg.header, nullptr);
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
    FfxApiProxy::VULKAN_Configure()(&_fgContext, &fgLogging.header);
}

void FSRFG_Vk::DestroyFGContext()
{
    Deactivate();

    if (_fgContext == nullptr)
        return;

    // Our prepare commands use the context's resources
    WaitForCommandBuffers();

    auto result = FfxApiProxy::VULKAN_DestroyContext()(&_fgContext, nullptr);
    LOG_INFO("Destroy FG context result: {}", FfxApiProxy::ReturnCodeToString(result));
    _fgContext = nullptr;
}

ffxReturnCode_t FSRFG_Vk::ConfigureEnabled(bool enabled)
{
    ffxConfigureDescFrameGeneration fgConfig {};
    fgConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    fgConfig.frameGenerationEnabled = enabled;
    fgConfig.swapChain = _swapchain;

    auto result = FfxApiProxy::VULKAN_Configure()(&_fgContext, &fgConfig.header);
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
    else if (IsActive())
    {
        Deactivate();
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
            _hudlessInCopy[fIndex] = CopyImage(cmdBuffer, *hudless, _hudlessCopies[fIndex], _swapchainFormat);

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

    auto result = FfxApiProxy::VULKAN_Configure()(&_fgContext, &fgConfig.header);
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

    result = FfxApiProxy::VULKAN_Dispatch()(&_fgContext, &dfgPrepare.header);
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

    return FfxApiProxy::VULKAN_Dispatch()(&_fgContext, &params->header);
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

    auto& copy = _hudlessCopies[fIndex];
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

VkCommandBuffer FSRFG_Vk::BeginCommandBuffer(int index)
{
    // Fence is created last, without it the rest might be missing too
    if (_fence[index] == VK_NULL_HANDLE)
    {
        VkCommandPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = _gameQueueFamily;

        if (_commandPool[index] == VK_NULL_HANDLE &&
            vkCreateCommandPool(_device, &poolInfo, nullptr, &_commandPool[index]) != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateCommandPool failed for {}", index);
            return VK_NULL_HANDLE;
        }

        VkCommandBufferAllocateInfo allocInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocInfo.commandPool = _commandPool[index];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (_commandBuffer[index] == VK_NULL_HANDLE &&
            vkAllocateCommandBuffers(_device, &allocInfo, &_commandBuffer[index]) != VK_SUCCESS)
        {
            LOG_ERROR("vkAllocateCommandBuffers failed for {}", index);
            return VK_NULL_HANDLE;
        }

        VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateFence(_device, &fenceInfo, nullptr, &_fence[index]) != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateFence failed for {}", index);
            return VK_NULL_HANDLE;
        }
    }

    if (vkWaitForFences(_device, 1, &_fence[index], VK_TRUE, 1'000'000'000) != VK_SUCCESS)
    {
        LOG_ERROR("Waiting for the FG command buffer {} timed out", index);
        return VK_NULL_HANDLE;
    }

    vkResetCommandPool(_device, _commandPool[index], 0);

    VkCommandBufferBeginInfo beginInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(_commandBuffer[index], &beginInfo);

    return _commandBuffer[index];
}

void FSRFG_Vk::WaitForCommandBuffers()
{
    for (auto fence : _fence)
    {
        if (fence != VK_NULL_HANDLE)
            vkWaitForFences(_device, 1, &fence, VK_TRUE, 1'000'000'000);
    }
}

void FSRFG_Vk::DestroyCommandBuffers()
{
    WaitForCommandBuffers();

    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        if (_fence[i] != VK_NULL_HANDLE)
            vkDestroyFence(_device, _fence[i], nullptr);

        // Frees the command buffer too
        if (_commandPool[i] != VK_NULL_HANDLE)
            vkDestroyCommandPool(_device, _commandPool[i], nullptr);

        _fence[i] = VK_NULL_HANDLE;
        _commandPool[i] = VK_NULL_HANDLE;
        _commandBuffer[i] = VK_NULL_HANDLE;
    }
}
