#include "pch.h"
#include "XeFG_Vk.h"

#include <Config.h>
#include <State.h>
#include <hooks/Vulkan_Hooks.h>
#include <proxies/KernelBase_Proxy.h>
#include <proxies/XeLL_Proxy.h>
#include <low_latency/input/input_common.h>
#include <misc/DeferredRelease.h>
#include <misc/FrameLimit.h>
#include <misc/IdentifyGpu.h>

#include <magic_enum.hpp>
#include <psapi.h>

const MfgXeFGVkApi* XeFG_Vk::EngineApi()
{
    static const MfgXeFGVkApi* api = nullptr;

    if (api != nullptr)
        return api;

    // Any loaded module exporting the entry
    HMODULE modules[1024] {};
    DWORD needed = 0;

    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
        return nullptr;

    const auto count = std::min<size_t>(needed / sizeof(HMODULE), std::size(modules));

    for (size_t i = 0; i < count; i++)
    {
        auto getApi = (PFN_MfgXeFGVkGetApi) KernelBaseProxy::GetProcAddress_()(modules[i], MFG_XEFG_VK_GET_API_NAME);

        if (getApi == nullptr)
            continue;

        auto found = getApi(MFG_XEFG_VK_API_VERSION);

        // Older engines have no SetHostRoutes
        if (found != nullptr && found->size >= offsetof(MfgXeFGVkApi, SetHostRoutes) &&
            found->version == MFG_XEFG_VK_API_VERSION)
        {
            LOG_INFO("XeFG Vulkan engine found, version: {}, size: {}", found->version, found->size);
            api = found;

            // Vulkan_Hooks.cpp answers these for XeFG's swapchain
            if (found->size >= offsetof(MfgXeFGVkApi, SetHostRoutes) + sizeof(found->SetHostRoutes) &&
                found->SetHostRoutes != nullptr)
            {
                found->SetHostRoutes(MFG_XEFG_VK_ROUTES_ALL);
            }

            break;
        }
    }

    return api;
}

PFN_vkVoidFunction VKAPI_PTR XeFG_Vk::OriginalFunction(void* user, const char* name)
{
    if (name == nullptr)
        return nullptr;

    // The engine's XeLL is XeFG's, not the game's, so it goes to the real XeLL past the XeLL input
    if (std::strncmp(name, "xell", 4) == 0)
        return (PFN_vkVoidFunction) XeLLProxy::RealFunction(name);

    // The driver's Reflex and Anti-Lag 2, past the low latency inputs' wrappers
    for (auto latencyName : { "vkSetLatencySleepModeNV", "vkLatencySleepNV", "vkSetLatencyMarkerNV",
                              "vkQueueNotifyOutOfBandNV", "vkGetLatencyTimingsNV", "vkAntiLagUpdateAMD" })
    {
        if (std::strcmp(name, latencyName) == 0)
        {
            auto self = static_cast<XeFG_Vk*>(user);
            return self != nullptr && self->_device != VK_NULL_HANDLE
                       ? VulkanHooks::GetDeviceProcAddr(self->_device, name)
                       : nullptr;
        }
    }

    // The engine's own swapchain calls go straight to the driver, past our hooks
    return VulkanHooks::OriginalSwapchainFunction(name);
}

void XeFG_Vk::UpdateLatency()
{
    if (_api == nullptr || _api->size < offsetof(MfgXeFGVkApi, SetLatency) + sizeof(_api->SetLatency) ||
        _api->SetLatency == nullptr)
    {
        return;
    }

    // As XeFG on D3D12: XeLL unless Reflex or Anti-Lag 2 is chosen and the GPU has it, the engine limits FPS there
    auto config = Config::Instance();
    auto configured = config->LowLatencyOutput.value_or_default();
    auto vendorId = IdentifyGpu::getPrimaryGpu().vendorId;

    MfgXeFGVkLatency latency { sizeof(latency) };

    if (configured == LowLatencyMode::Reflex && vendorId == VendorId::Nvidia)
        latency.mode = MFG_XEFG_VK_LATENCY_REFLEX;
    else if ((configured == LowLatencyMode::AntiLag2 || configured == LowLatencyMode::AntiLagVk) &&
             vendorId == VendorId::AMD)
        latency.mode = MFG_XEFG_VK_LATENCY_ANTILAG2;
    else
        latency.mode = MFG_XEFG_VK_LATENCY_XELL;

    latency.lowLatency = config->FN_ForceReflex.value_or_default() != ForceReflex::ForceDisable ? 1 : 0;
    latency.outputFpsLimit = FrameLimit::targetFps();

    if (std::memcmp(&latency, &_latency, sizeof(latency)) == 0)
        return;

    _latency = latency;
    _api->SetLatency(&latency);

    LOG_INFO("XeFG Vulkan low latency: {}, low latency mode: {}, FPS limit: {}",
             latency.mode == MFG_XEFG_VK_LATENCY_REFLEX     ? "Reflex"
             : latency.mode == MFG_XEFG_VK_LATENCY_ANTILAG2 ? "Anti-Lag 2"
                                                            : "XeLL",
             latency.lowLatency, latency.outputFpsLimit);
}

const char* XeFG_Vk::Name()
{
    if (_framesToInterpolate < 1)
        return "XeFG";

    _name = "XeFG " + std::to_string(_framesToInterpolate + 1) + "x";
    return _name.c_str();
}

XeFG_Vk::XeFG_Vk() : IFGFeature_Vk(), IFGFeature()
{
    _api = EngineApi();

    if (_api != nullptr)
        _maxInterpolationCount = (int) _api->MaxInterpolatedFrames();
}

XeFG_Vk::~XeFG_Vk() { Shutdown(); }

uint32_t XeFG_Vk::InitFlags()
{
    // The upscaler's flag unless the user set it
    auto inverted = Config::Instance()->FGXeFGDepthInverted.value_or(IsInvertedDepth());
    return inverted ? MFG_XEFG_VK_INIT_INVERTED_DEPTH : 0;
}

void XeFG_Vk::PrepareDevice(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo& createInfo)
{
    // The game's queue family, no extra queues of ours
    IFGFeature_Vk::PrepareDevice(physicalDevice, createInfo);

    if (_api == nullptr)
    {
        LOG_ERROR("XeFG Vulkan engine isn't loaded");
        return;
    }

    // The low latency output first: the engine adds its device extensions
    UpdateLatency();

    VkInstance instance = VK_NULL_HANDLE;
    auto apiVersion = VulkanHooks::InstanceApiVersion(physicalDevice, &instance);

    MfgXeFGVkDeviceInfo info {};
    info.size = sizeof(info);
    info.instance = instance;
    info.getInstanceProcAddr = &VulkanHooks::GetInstanceProcAddr;
    info.instanceApiVersion = apiVersion;
    info.physicalDevice = physicalDevice;

    const VkDeviceCreateInfo* edited = &createInfo;
    _deviceEdit = _api->PrepareDevice(&info, &createInfo, &edited);

    if (_deviceEdit == nullptr || edited == nullptr)
    {
        MfgXeFGVkStatus status { sizeof(status) };
        _api->GetStatus(&status);
        LOG_WARN("The device can't host XeFG: {}", status.reason);
        _deviceEdit = nullptr;
        return;
    }

    // The engine's queues, extensions and features, valid until OnDeviceCreated
    createInfo = *edited;
    LOG_INFO("Device prepared for XeFG");
}

void XeFG_Vk::OnDeviceCreated(VkDevice device)
{
    IFGFeature_Vk::OnDeviceCreated(device);

    if (_api == nullptr || _deviceEdit == nullptr)
        return;

    _deviceHosted = _api->DeviceCreated(_deviceEdit, device, &OriginalFunction, this) == VK_TRUE;
    _deviceEdit = nullptr;

    LOG_INFO("XeFG device: {}", _deviceHosted);
}

bool XeFG_Vk::CreateSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                              const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    if (_api == nullptr || !_deviceHosted || device != _device)
        return false;

    // XeFG under Streamline's DLSS-G crashes the driver: only with DLSS-G's swapchain hooks taken out of its plugin
    // JSON (FG input "DLSSG via Streamline")
    if (KernelBaseProxy::GetModuleHandleW_()(L"sl.dlss_g.dll") != nullptr)
    {
        auto& state = State::Instance();
        const bool dlssgInput = state.activeFgInput == FGInput::DLSSG;
        const bool inert = dlssgInput && state.slDlssgHooksCleared && !state.slDlssgHooksResolved;

        if (!inert)
        {
            if (!dlssgInput && Config::Instance()->FGInput.value_or_default() == FGInput::DLSSG)
                _refusal = "Streamline's DLSS-G is loaded: the FG Input change applies after a restart, save the "
                           "settings and restart the game";
            else if (!dlssgInput)
                _refusal = "Streamline's DLSS-G is loaded, set FG Input to \"DLSSG via Streamline\", save and restart";
            else if (!state.slDlssgHooksCleared)
                _refusal = "Streamline's DLSS-G loaded before OptiScaler could take out its swapchain hooks (try "
                           "another proxy name, e.g. winmm.dll or dxgi.dll)";
            else
                _refusal = "Streamline's DLSS-G kept its swapchain hooks although OptiScaler took them out";

            LOG_WARN("Leaving the swapchain to the game: {} (FG input {}, slInit seen {}, DLSS-G removed {}, all "
                     "features {}, hooks cleared {}, hooks resolved {})",
                     _refusal, magic_enum::enum_name(state.activeFgInput), state.slInitSeen, state.slDlssgRemoved,
                     state.slLoadsAllFeatures, state.slDlssgHooksCleared, state.slDlssgHooksResolved);
            return false;
        }

        LOG_INFO("Streamline's DLSS-G is loaded without its swapchain hooks, XeFG takes the swapchain (slInit seen {}, "
                 "DLSS-G removed {}, all features {})",
                 state.slInitSeen, state.slDlssgRemoved, state.slLoadsAllFeatures);
    }

    // XeFG handles one window, the game's other windows keep their own swapchains
    if (_swapchain != VK_NULL_HANDLE && pCreateInfo->surface != _surface && pCreateInfo->oldSwapchain != _swapchain)
    {
        LOG_INFO("Swapchain is for another surface, leaving it to the game");
        return false;
    }

    auto hwnd = VulkanHooks::SurfaceWindow(pCreateInfo->surface);
    auto initFlags = InitFlags();

    // The V-Sync override sets the engine's present mode
    VkSwapchainCreateInfoKHR vsyncInfo {};
    if (auto& force = Config::Instance()->ForceVsync; force.has_value())
    {
        const auto mode = force.value() ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;

        if (mode != pCreateInfo->presentMode)
        {
            LOG_INFO("V-Sync {}: present mode {} -> {}", force.value() ? "on" : "off", (UINT) pCreateInfo->presentMode,
                     (UINT) mode);
            vsyncInfo = *pCreateInfo;
            vsyncInfo.presentMode = mode;
            pCreateInfo = &vsyncInfo;
        }
    }

    LOG_INFO("Format: {}, color space: {}, extent: {}x{}, images: {}, present mode: {}, init flags: {}",
             (UINT) pCreateInfo->imageFormat, (UINT) pCreateInfo->imageColorSpace, pCreateInfo->imageExtent.width,
             pCreateInfo->imageExtent.height, pCreateInfo->minImageCount, (UINT) pCreateInfo->presentMode, initFlags);

    auto oldOwned = pCreateInfo->oldSwapchain != VK_NULL_HANDLE && _api->OwnsSwapchain(pCreateInfo->oldSwapchain);
    auto result = _api->CreateSwapchain(device, pCreateInfo, hwnd, initFlags, pSwapchain);

    if (oldOwned && pCreateInfo->oldSwapchain == _swapchain)
        _swapchain = VK_NULL_HANDLE;

    if (result == VK_SUCCESS)
    {
        _swapchain = *pSwapchain;
        _surface = pCreateInfo->surface;
        _hwnd = hwnd;
        _engineCount = -1;
        _debugView = false;
        _refusal.clear();
        _swapchainFormat = pCreateInfo->imageFormat;
        _swapchainExtent = pCreateInfo->imageExtent;
        _initFlags = initFlags;
        _swapchainOutdated = false;
        _framesToInterpolate = -1; // applied again on the new swapchain

        return true;
    }

    MfgXeFGVkStatus status { sizeof(status) };
    _api->GetStatus(&status);
    LOG_WARN("XeFG swapchain creation failed: {}, reason: {}", magic_enum::enum_name(result), status.reason);

    // The game gets its own swapchain. The old one being ours, the driver doesn't know it.
    if (!oldOwned)
        return false;

    auto createSwapchain = (PFN_vkCreateSwapchainKHR) VulkanHooks::OriginalSwapchainFunction("vkCreateSwapchainKHR");

    if (createSwapchain == nullptr)
        return false;

    VkSwapchainCreateInfoKHR createInfo = *pCreateInfo;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    // The game's own one, not tracked
    createSwapchain(device, &createInfo, pAllocator, pSwapchain);
    return true;
}

bool XeFG_Vk::DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
{
    if (_api == nullptr || swapchain == VK_NULL_HANDLE)
        return false;

    if (swapchain == _swapchain)
    {
        OwnedLockGuard lock(Mutex, 1);
        Deactivate();
        WaitForCommandBuffers();
        _swapchain = VK_NULL_HANDLE;
        _surface = VK_NULL_HANDLE;
    }

    // The engine keeps the game's old swapchain until its own one retired it
    return _api->DestroySwapchain(device, swapchain) == VK_TRUE;
}

VkResult XeFG_Vk::GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount,
                                     VkImage* pSwapchainImages)
{
    return _api->GetSwapchainImages(device, swapchain, pSwapchainImageCount, pSwapchainImages);
}

VkResult XeFG_Vk::AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
                                   VkFence fence, uint32_t* pImageIndex)
{
    // Nothing gets acquired or signaled, the game has to recreate the swapchain
    if (_swapchainOutdated)
        return VK_ERROR_OUT_OF_DATE_KHR;

    // The engine sleeps for the low latency output in there
    UpdateLatency();

    const auto start = Util::MillisecondsNow();
    const auto result = _api->AcquireNextImage(device, swapchain, timeout, semaphore, fence, pImageIndex);
    _acquireMs = Util::MillisecondsNow() - start;
    return result;
}

VkResult XeFG_Vk::QueuePresent(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    _presentQueue = queue;

    // The input copy ran on the game queue: this present waits for it with the game's semaphores
    VkPresentInfoKHR info = *pPresentInfo;
    std::vector<VkSemaphore> waits;

    if (_pendingInputWait != VK_NULL_HANDLE)
    {
        waits.assign(pPresentInfo->pWaitSemaphores, pPresentInfo->pWaitSemaphores + pPresentInfo->waitSemaphoreCount);
        waits.push_back(_pendingInputWait);
        info.waitSemaphoreCount = (uint32_t) waits.size();
        info.pWaitSemaphores = waits.data();
        _pendingInputWait = VK_NULL_HANDLE;
    }

    const auto start = Util::MillisecondsNow();
    const auto result = _api->QueuePresent(queue, &info);
    _presentMs = Util::MillisecondsNow() - start;

    // Our FPS limiter stays out while the engine limits
    MfgXeFGVkStatus status { sizeof(status) };
    _api->GetStatus(&status);
    State::Instance().xefgVkEngineLimitsFps = status.latencyLimited != 0;
    State::Instance().xefgVkEngineLatency = status.latencyMode;

    return result;
}

void XeFG_Vk::SetHdrMetadata(VkDevice device, VkSwapchainKHR swapchain, const VkHdrMetadataEXT* pMetadata)
{
    _api->SetHdrMetadata(device, swapchain, pMetadata);
}

VkResult XeFG_Vk::WaitForPresent(VkDevice device, VkSwapchainKHR swapchain, uint64_t presentId, uint64_t timeout)
{
    return _api->WaitForPresent(device, swapchain, presentId, timeout);
}

std::string XeFG_Vk::EngineReason() const
{
    if (_api == nullptr)
        return "No XeFG Vulkan engine loaded";

    if (!_refusal.empty())
        return _refusal;

    MfgXeFGVkStatus status { sizeof(status) };
    _api->GetStatus(&status);

    if (status.active)
        return {};

    return status.reason[0] != 0 ? status.reason : "waiting for the game's swapchain";
}

VkResult XeFG_Vk::ReleaseSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t imageIndexCount,
                                         const uint32_t* pImageIndices)
{
    return _api->ReleaseSwapchainImages(device, swapchain, imageIndexCount, pImageIndices);
}

uint64_t XeFG_Vk::RefreshDurationNs() const
{
    constexpr uint64_t second = 1'000'000'000ull;
    auto monitor = MonitorFromWindow(_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info {};
    info.cbSize = sizeof(info);
    DEVMODEW mode {};
    mode.dmSize = sizeof(mode);

    if (monitor != nullptr && GetMonitorInfoW(monitor, &info) &&
        EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
    {
        return second / mode.dmDisplayFrequency;
    }

    return second / 60;
}

void XeFG_Vk::Activate()
{
    if (_swapchain == VK_NULL_HANDLE || _isActive)
        return;

    _api->SetEnabled(_swapchain, VK_TRUE);
    _isActive = true;
    _lastDispatchedFrame = 0;
    _generationOff = false;

    // FG back on: Dynamic MFG starts its averages over but keeps what it learned
    _dynamic.Restart();
}

void XeFG_Vk::Deactivate()
{
    if (!_isActive)
        return;

    if (_swapchain != VK_NULL_HANDLE)
        _api->SetEnabled(_swapchain, VK_FALSE);

    _isActive = false;
}

void XeFG_Vk::DestroyFGContext() { Deactivate(); }

bool XeFG_Vk::ReleaseSwapchain(HWND hwnd)
{
    Deactivate();
    return true;
}

bool XeFG_Vk::Shutdown()
{
    Deactivate();
    DestroyCommandBuffers();

    // A present may still wait for one: released later
    for (auto& semaphore : _inputDone)
    {
        if (semaphore != VK_NULL_HANDLE && _device != VK_NULL_HANDLE)
            DeferredRelease::Enqueue([device = _device, semaphore]()
                                     { vkDestroySemaphore(device, semaphore, nullptr); });

        semaphore = VK_NULL_HANDLE;
    }

    _pendingInputWait = VK_NULL_HANDLE;
    _presentQueue = VK_NULL_HANDLE;
    State::Instance().xefgVkEngineLimitsFps = false;
    State::Instance().xefgVkEngineLatency = 0;
    return true;
}

void XeFG_Vk::ApplyFrameCount()
{
    auto config = Config::Instance();

    if (config->FGXeFGInterpolationCount.value_or_default() > _maxInterpolationCount)
        config->FGXeFGInterpolationCount = _maxInterpolationCount;

    int targetCount = std::clamp(config->FGInterpolationCount(config->FGXeFGInterpolationCount.value_or_default()), 1,
                                 std::max(_maxInterpolationCount, 1));

    // Dynamic MFG, the game's waits in the engine are its acquisition and present
    const auto dynamic =
        _dynamic.Update(targetCount, _framesToInterpolate, _generationOff, _acquireMs + _presentMs, _hwnd);

    // 1X: frame generation off, the engine keeps XeFG's count
    _generationOff = dynamic.generationOff;
    targetCount = dynamic.count;

    if (_framesToInterpolate != targetCount)
    {
        LOG_INFO("Interpolation count changed {} -> {}", _framesToInterpolate, targetCount);
        _framesToInterpolate = targetCount;
    }

    // Debug View
    if (const bool debugView = config->FGXeFGDebugView.value_or_default(); debugView != _debugView)
    {
        _api->SetDebugView(_swapchain, debugView ? VK_TRUE : VK_FALSE);
        _debugView = debugView;
    }

    // The engine's count, 0 while generation is off
    const int engineCount = _generationOff ? 0 : targetCount;

    if (_engineCount != engineCount && _api->SetInterpolatedFrames(_swapchain, (uint32_t) engineCount) == VK_TRUE)
    {
        if (_engineCount >= 0 && (engineCount == 0) != (_engineCount == 0))
            LOG_INFO("Dynamic MFG 1X: frame generation {}", engineCount == 0 ? "off" : "on");

        _engineCount = engineCount;
    }
}

void XeFG_Vk::EvaluateState(FG_Constants& fgConstants)
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

    // XeFG takes the depth direction when its swapchain is created
    if (!_swapchainOutdated && InitFlags() != _initFlags)
    {
        LOG_INFO("Inverted depth changed, recreating the swapchain");
        _swapchainOutdated = true;
    }

    if (!Config::Instance()->IsFGEnabled() && IsActive())
        Deactivate();

    if (state.fgChanged)
    {
        Deactivate();
        state.fgChanged = false;
        UpdateTarget();
    }

    state.scChanged = false;
}

bool XeFG_Vk::Present()
{
    OwnedLockGuard lock(Mutex, 2);

    if (_swapchain == VK_NULL_HANDLE)
        return false;

    // Game stopped sending new frames, menus and such
    if ((_fgFramePresentId - _lastFGFramePresentId) > 3 && IsActive() && !_waitingNewFrameData)
    {
        LOG_DEBUG("Pausing FG");
        Deactivate();
        _waitingNewFrameData = true;
        return false;
    }

    _fgFramePresentId++;

    ApplyFrameCount();

    return Dispatch();
}

bool XeFG_Vk::Dispatch()
{
    UINT64 willDispatchFrame = 0;
    auto fIndex = GetDispatchIndex(willDispatchFrame);

    if (fIndex < 0 || !IsActive() || IsPaused())
        return false;

    auto depth = GetResource(FG_ResourceType::Depth, fIndex);
    auto velocity = GetResource(FG_ResourceType::Velocity, fIndex);

    if (!depth || !velocity || depth->view == VK_NULL_HANDLE || velocity->view == VK_NULL_HANDLE)
    {
        LOG_WARN("Depth or Velocity is not ready, skipping");
        return false;
    }

    auto cmdBuffer = BeginCommandBuffer(fIndex);

    if (cmdBuffer == VK_NULL_HANDLE)
        return false;

    // The engine samples them, the copies get back their layouts after
    const auto readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ImageBarrier(cmdBuffer, depth->image, depth->format, depth->layout, readLayout);
    ImageBarrier(cmdBuffer, velocity->image, velocity->format, velocity->layout, readLayout);

    auto& state = State::Instance();
    auto config = Config::Instance();

    MfgXeFGVkFrame frame {};
    frame.size = sizeof(frame);
    frame.depth = { depth->image, depth->view, depth->format, readLayout, depth->width, depth->height };
    frame.motion = { velocity->image, velocity->view, velocity->format, readLayout, velocity->width, velocity->height };
    frame.jitterX = _jitterX[fIndex];
    frame.jitterY = _jitterY[fIndex];
    frame.motionScaleX = _mvScaleX[fIndex];
    frame.motionScaleY = _mvScaleY[fIndex];
    frame.cameraNear = _cameraNear[fIndex];
    frame.cameraFar = _cameraFar[fIndex];
    frame.cameraVFov = _cameraVFov[fIndex];
    frame.frameTimeMs = config->FTInput.value_or_default() == FrameTimeSource::Input ? (float) _ftDelta[fIndex]
                                                                                     : (float) state.lastFGFrameTime;
    frame.reset = _reset[fIndex];

    auto result = _api->RecordFrame(_swapchain, cmdBuffer, &frame);

    ImageBarrier(cmdBuffer, depth->image, depth->format, readLayout, depth->layout);
    ImageBarrier(cmdBuffer, velocity->image, velocity->format, readLayout, velocity->layout);

    vkEndCommandBuffer(cmdBuffer);

    // Before the frame's present. A game presenting from another queue (idTech) waits on a semaphore.
    VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    const bool otherPresentQueue = _presentQueue != VK_NULL_HANDLE && _presentQueue != _gameQueue;

    if (otherPresentQueue && _inputDone[fIndex] == VK_NULL_HANDLE)
    {
        VkSemaphoreCreateInfo semaphoreInfo { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

        if (vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &_inputDone[fIndex]) != VK_SUCCESS)
            _inputDone[fIndex] = VK_NULL_HANDLE;
    }

    const bool signal =
        otherPresentQueue && _inputDone[fIndex] != VK_NULL_HANDLE && _pendingInputWait == VK_NULL_HANDLE;

    if (signal)
    {
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &_inputDone[fIndex];
    }

    vkResetFences(_device, 1, &_fence[fIndex]);

    if (auto submitResult = vkQueueSubmit(_gameQueue, 1, &submitInfo, _fence[fIndex]); submitResult != VK_SUCCESS)
    {
        LOG_ERROR("vkQueueSubmit error: {}", magic_enum::enum_name(submitResult));
        return false;
    }

    if (signal)
        _pendingInputWait = _inputDone[fIndex];

    if (result != VK_SUCCESS)
        LOG_DEBUG("RecordFrame: {}, frame: {}, fIndex: {}", magic_enum::enum_name(result), willDispatchFrame, fIndex);

    return result == VK_SUCCESS;
}
