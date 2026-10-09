#include "pch.h"
#include "input_reflex.h"
#include <nvapi/NvApiHooks.h>
#include <hooks/Vulkan_Hooks.h>
#include <low_latency/low_latency_tech/ll_reflex_vk.h>

#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

// OptiScaler's own Streamline (DLSSG output): its calls marked as OptiScaler's, and the ones its modules make on
// Streamline's own threads (the async markers of the generated frames' presents)
static bool FromOptiScaler(void* returnAddress)
{
    if (ReflexHooks::optiScalerCall)
        return true;

    auto& state = State::Instance();

    if (state.optiSlInterposer == nullptr)
        return false;

    auto module = Util::GetCallerModule(returnAddress);

    return module != nullptr &&
           (module == state.optiSlInterposer || module == state.optiSlCommon || module == state.optiSlDLSSG ||
            module == state.optiSlReflex || module == state.optiSlPCL || module == state.optiDLSSG);
}

// With NVIDIA's nvapi OptiScaler's Streamline reaches the driver, with fakenvapi it's the OptiScaler input
template <typename Fn> static Fn ToDriver(unsigned int id)
{
    if (fakenvapi::isUsingAsMainNvapi())
        return nullptr;

    return reinterpret_cast<Fn>(ReflexHooks::getHookedReflex(id));
}

static NvAPI_Status ToNvApi(InputResult result, const char* call)
{
    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput ||
        result == InputResult::NotEnoughReports)
    {
        return NVAPI_OK;
    }

    LOG_ERROR("{} result: {}", call, magic_enum::enum_name(result));

    return result == InputResult::NoReadyOutput ? NVAPI_OK : NVAPI_ERROR;
}

NvAPI_Status InputReflex::D3D_SetSleepMode(IUnknown* pDev, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams)
{
    if (!pSetSleepModeParams || !pDev)
        return NVAPI_INVALID_ARGUMENT;

    bool own = FromOptiScaler(_ReturnAddress());

    if (own)
    {
        if (auto driver = ToDriver<decltype(&NvAPI_D3D_SetSleepMode)>(GET_ID(NvAPI_D3D_SetSleepMode)))
            return driver(pDev, pSetSleepModeParams);

        optiScalerLowLatency = pSetSleepModeParams->bLowLatencyMode;
    }
    else
    {
        ReflexHooks::gameSetSleepMode();
    }

    SleepMode sleepMode {};
    sleepMode.low_latency_enabled = pSetSleepModeParams->bLowLatencyMode;
    sleepMode.low_latency_boost = pSetSleepModeParams->bLowLatencyBoost;
    sleepMode.minimum_interval_us = pSetSleepModeParams->minimumIntervalUs;
    sleepMode.use_markers_to_optimize = pSetSleepModeParams->bUseMarkersToOptimize;
    sleepMode.use_min_queue_time = pSetSleepModeParams->bUseMinQueueTime;

    return ToNvApi(InputCommon::set_sleep_mode(own ? optiScalerContext : inputContext, pDev, &sleepMode),
                   "set_sleep_mode");
}

NvAPI_Status InputReflex::D3D_GetSleepStatus(IUnknown* pDevice, NV_GET_SLEEP_STATUS_PARAMS* pGetSleepStatusParams)
{
    if (!pGetSleepStatusParams || !pDevice)
        return NVAPI_INVALID_ARGUMENT;

    bool own = FromOptiScaler(_ReturnAddress());

    if (own && !fakenvapi::isUsingAsMainNvapi() && NvApiHooks::o_NvAPI_QueryInterface != nullptr)
    {
        if (auto driver = GET_INTERFACE(NvAPI_D3D_GetSleepStatus, NvApiHooks::o_NvAPI_QueryInterface))
            return driver(pDevice, pGetSleepStatusParams);
    }

    SleepParams sleepParams {};

    auto result = InputCommon::get_sleep_status(own ? optiScalerContext : inputContext, pDevice, &sleepParams);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
    {
        // OptiScaler's Streamline reads back the low latency it set, the game's input decides the output's
        pGetSleepStatusParams->bLowLatencyMode = own ? optiScalerLowLatency : sleepParams.low_latency_enabled;
        pGetSleepStatusParams->bFsVrr = sleepParams.fullscreen_vrr;
        pGetSleepStatusParams->bCplVsyncOn = sleepParams.control_panel_vsync_override;
        pGetSleepStatusParams->sleepIntervalUs = sleepParams.sleep_interval_us;
        pGetSleepStatusParams->bUseGameSleep = sleepParams.use_game_sleep;
        pGetSleepStatusParams->bFullscreenIFlip = sleepParams.fullscreen_i_flip;
        pGetSleepStatusParams->fgMultiplier = sleepParams.fg_multiplier;

        return NVAPI_OK;
    }

    if (own)
        pGetSleepStatusParams->bLowLatencyMode = optiScalerLowLatency;

    return ToNvApi(result, "get_sleep_status");
}

NvAPI_Status InputReflex::D3D_Sleep(IUnknown* pDev)
{
    if (!pDev)
        return NVAPI_INVALID_ARGUMENT;

    if (FromOptiScaler(_ReturnAddress()))
    {
        if (auto driver = ToDriver<decltype(&NvAPI_D3D_Sleep)>(GET_ID(NvAPI_D3D_Sleep)))
            return driver(pDev);

        return ToNvApi(InputCommon::sleep(optiScalerContext, pDev), "sleep");
    }

    auto result = InputCommon::sleep(inputContext, pDev);

    // The Reflex output's sleep goes through OptiScaler's Streamline itself, with the other outputs it still sleeps
    // for the game so DLSSG follows its frames
    if (InputCommon::active_output() != LowLatencyMode::Reflex)
        ReflexHooks::streamlineSleep();

    return ToNvApi(result, "sleep");
}

NvAPI_Status InputReflex::D3D_GetLatency(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams)
{
    if (!pDev || !pGetLatencyParams)
        return NVAPI_INVALID_ARGUMENT;

    bool own = FromOptiScaler(_ReturnAddress());

    if (own)
    {
        if (auto driver = ToDriver<decltype(&NvAPI_D3D_GetLatency)>(GET_ID(NvAPI_D3D_GetLatency)))
            return driver(pDev, pGetLatencyParams);
    }

    return ToNvApi(InputCommon::get_latency(own ? optiScalerContext : inputContext, pDev, pGetLatencyParams),
                   "get_latency");
}

// The game's marker after OptiScaler's tracking and game quirks. OptiScaler's Streamline reaches the Reflex output
// itself with a marker it took, the other outputs get the game's.
NvAPI_Status InputReflex::SendGameMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* params, bool toStreamline)
{
    MarkerParams markerParams {};
    markerParams.frame_id = params->frameID;
    markerParams.marker_type = (MarkerType) params->markerType; // requires enums to match

    return ToNvApi(InputCommon::set_marker(inputContext, pDev, markerParams,
                                           !toStreamline || InputCommon::active_output() != LowLatencyMode::Reflex),
                   "set_marker");
}

NvAPI_Status InputReflex::D3D_SetLatencyMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams)
{
    if (!pDev || !pSetLatencyMarkerParams)
        return NVAPI_INVALID_ARGUMENT;

    if (FromOptiScaler(_ReturnAddress()))
    {
        if (auto driver = ToDriver<decltype(&NvAPI_D3D_SetLatencyMarker)>(GET_ID(NvAPI_D3D_SetLatencyMarker)))
            return driver(pDev, pSetLatencyMarkerParams);

        MarkerParams markerParams {};
        markerParams.frame_id = pSetLatencyMarkerParams->frameID;
        markerParams.marker_type = (MarkerType) pSetLatencyMarkerParams->markerType;

        return ToNvApi(InputCommon::set_marker(optiScalerContext, pDev, markerParams), "set_marker");
    }

    return ReflexHooks::processGameMarker(pDev, pSetLatencyMarkerParams, &SendGameMarker);
}

NvAPI_Status InputReflex::D3D12_SetAsyncFrameMarker(ID3D12CommandQueue* pCommandQueue,
                                                    NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams)
{
    if (!pCommandQueue || !pSetAsyncFrameMarkerParams)
        return NVAPI_INVALID_ARGUMENT;

    bool own = FromOptiScaler(_ReturnAddress());

    if (own)
    {
        if (auto driver = ToDriver<decltype(&NvAPI_D3D12_SetAsyncFrameMarker)>(GET_ID(NvAPI_D3D12_SetAsyncFrameMarker)))
        {
            return driver(pCommandQueue, pSetAsyncFrameMarkerParams);
        }
    }

    ReflexHooks::trackAsyncMarker(pSetAsyncFrameMarkerParams->frameID);

    MarkerParams markerParams {};
    markerParams.frame_id = pSetAsyncFrameMarkerParams->frameID;
    markerParams.marker_type = (MarkerType) pSetAsyncFrameMarkerParams->markerType; // requires enums to match

    return ToNvApi(
        InputCommon::set_async_marker(own ? optiScalerAsyncContext : inputContext, pCommandQueue, markerParams),
        "set_async_marker");
}

// The game gets its own timeline semaphore, the output's sleep doesn't need the driver's nvapi Vulkan Reflex
NvAPI_Status InputReflex::Vulkan_InitLowLatencyDevice(HANDLE vkDevice, HANDLE* signalSemaphoreHandle)
{
    if (!vkDevice || !signalSemaphoreHandle)
        return NVAPI_INVALID_ARGUMENT;

    if (vkSemaphore == VK_NULL_HANDLE)
    {
        VkSemaphoreTypeCreateInfo timelineInfo {};
        timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timelineInfo.initialValue = 0;

        VkSemaphoreCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        createInfo.pNext = &timelineInfo;

        if (VulkanHooks::o_vkCreateSemaphore == nullptr ||
            VulkanHooks::o_vkCreateSemaphore((VkDevice) vkDevice, &createInfo, nullptr, &vkSemaphore) != VK_SUCCESS)
        {
            LOG_ERROR("Can't create the Reflex semaphore");
            return NVAPI_ERROR;
        }
    }

    *signalSemaphoreHandle = (HANDLE) vkSemaphore;
    return NVAPI_OK;
}

NvAPI_Status InputReflex::Vulkan_DestroyLowLatencyDevice(HANDLE vkDevice)
{
    if (!vkDevice)
        return NVAPI_INVALID_ARGUMENT;

    if (vkSemaphore != VK_NULL_HANDLE)
    {
        if (auto destroy =
                (PFN_vkDestroySemaphore) VulkanHooks::GetDeviceProcAddr((VkDevice) vkDevice, "vkDestroySemaphore"))
        {
            destroy((VkDevice) vkDevice, vkSemaphore, nullptr);
        }

        vkSemaphore = VK_NULL_HANDLE;
    }

    return NVAPI_OK;
}

NvAPI_Status InputReflex::Vulkan_GetSleepStatus(HANDLE vkDevice,
                                                NV_VULKAN_GET_SLEEP_STATUS_PARAMS* pGetSleepStatusParams)
{
    if (!vkDevice || !pGetSleepStatusParams)
        return NVAPI_INVALID_ARGUMENT;

    SleepParams sleepParams {};

    auto result = InputCommon::get_sleep_status(vulkanContext, (IUnknown*) vkDevice, &sleepParams);

    if (result == InputResult::Ok || result == InputResult::UsingDifferentInput)
    {
        pGetSleepStatusParams->bLowLatencyMode = sleepParams.low_latency_enabled;
        return NVAPI_OK;
    }

    return ToNvApi(result, "get_sleep_status");
}

NvAPI_Status InputReflex::Vulkan_SetSleepMode(HANDLE vkDevice, NV_VULKAN_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams)
{
    if (!vkDevice || !pSetSleepModeParams)
        return NVAPI_INVALID_ARGUMENT;

    SleepMode sleepMode {};
    sleepMode.low_latency_enabled = pSetSleepModeParams->bLowLatencyMode;
    sleepMode.low_latency_boost = pSetSleepModeParams->bLowLatencyBoost;
    sleepMode.minimum_interval_us = pSetSleepModeParams->minimumIntervalUs;
    sleepMode.use_markers_to_optimize = true;

    return ToNvApi(InputCommon::set_sleep_mode(vulkanContext, (IUnknown*) vkDevice, &sleepMode), "set_sleep_mode");
}

NvAPI_Status InputReflex::Vulkan_Sleep(HANDLE vkDevice, NvU64 signalValue)
{
    if (!vkDevice)
        return NVAPI_INVALID_ARGUMENT;

    auto result = InputCommon::sleep(vulkanContext, (IUnknown*) vkDevice);

    // The game waits for it whatever the output did
    if (vkSemaphore != VK_NULL_HANDLE && VulkanHooks::o_vkSignalSemaphore != nullptr)
    {
        VkSemaphoreSignalInfo signalInfo {};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
        signalInfo.semaphore = vkSemaphore;
        signalInfo.value = signalValue;

        VulkanHooks::o_vkSignalSemaphore((VkDevice) vkDevice, &signalInfo);
    }

    return ToNvApi(result, "sleep");
}

NvAPI_Status InputReflex::Vulkan_GetLatency(HANDLE vkDevice, NV_VULKAN_LATENCY_RESULT_PARAMS* pGetLatencyParams)
{
    if (!vkDevice || !pGetLatencyParams)
        return NVAPI_INVALID_ARGUMENT;

    if (pGetLatencyParams->version != NV_VULKAN_LATENCY_RESULT_PARAMS_VER1)
    {
        LOG_ERROR("Unsupported version {}", pGetLatencyParams->version);
        return NVAPI_INCOMPATIBLE_STRUCT_VERSION;
    }

    FrameReport reports[NVAPI_BUFFER_SIZE] {};
    auto result = InputCommon::get_frame_reports(vulkanContext, (IUnknown*) vkDevice, reports);

    // The Vulkan reports are the D3D ones without the GPU times
    for (size_t i = 0; i < NVAPI_BUFFER_SIZE; i++)
    {
        auto& reportOut = pGetLatencyParams->frameReport[i];
        std::memset(&reportOut, 0, sizeof(reportOut));
        std::memcpy(&reportOut, &reports[i], offsetof(FrameReport, gpuActiveRenderTimeUs));
    }

    return ToNvApi(result, "get_frame_reports");
}

NvAPI_Status InputReflex::Vulkan_SetLatencyMarker(HANDLE vkDevice,
                                                  NV_VULKAN_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams)
{
    if (!vkDevice || !pSetLatencyMarkerParams)
        return NVAPI_INVALID_ARGUMENT;

    MarkerParams markerParams {};
    markerParams.frame_id = pSetLatencyMarkerParams->frameID;
    markerParams.marker_type = (MarkerType) pSetLatencyMarkerParams->markerType; // requires enums to match

    return ToNvApi(InputCommon::set_marker(vulkanContext, (IUnknown*) vkDevice, markerParams), "set_marker");
}

NvAPI_Status InputReflex::Vulkan_NotifyOutOfBandVkQueue(HANDLE vkDevice, HANDLE queueHandle,
                                                        NV_VULKAN_OUT_OF_BAND_QUEUE_TYPE queueType)
{
    if (!vkDevice || !queueHandle)
        return NVAPI_INVALID_ARGUMENT;

    // Only the Reflex output has a use for it, the queue types match
    if (auto reflex = InputCommon::reflex_vk_output())
        reflex->notify_out_of_band((VkQueue) queueHandle, (VkOutOfBandQueueTypeNV) queueType);

    return NVAPI_OK;
}
