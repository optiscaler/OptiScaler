#include "pch.h"

#include <misc/IdentifyGpu.h>

#include "input_common.h"
#include "input_xell.h"
#include <low_latency/low_latency_tech/ll_xell.h>
#include <low_latency/low_latency_tech/ll_antilag2.h>
#include <low_latency/low_latency_tech/ll_latencyflex.h>
#include <low_latency/low_latency_tech/ll_reflex.h>
#include <low_latency/low_latency_tech/ll_reflex_vk.h>
#include <low_latency/low_latency_tech/ll_antilag_vk.h>
#include <hooks/Vulkan_Hooks.h>
#include <inputs/FG/XeFG_Inputs_Dx12.h>
#include <framegen/IFGFeature_Dx12.h>
#include <nvapi/fakenvapi.h>

static bool marker_supported(InputMarkerMode mode, MarkerType type)
{
    switch (mode)
    {
    case InputMarkerMode::NoMarkers:
        return false;
    case InputMarkerMode::SimStartOnly:
        return type == MarkerType::SIMULATION_START;
    case InputMarkerMode::SimStartAndPresentStart:
        return type == MarkerType::SIMULATION_START || type == MarkerType::PRESENT_START ||
               type == MarkerType::OUT_OF_BAND_PRESENT_START;
    case InputMarkerMode::SimOnly:
        return type == MarkerType::SIMULATION_START || type == MarkerType::SIMULATION_END;
    default:
        return true;
    }
}

// private
bool InputCommon::deinit_current_tech()
{
    // currently_active_tech becomes nullptr
    // but we need to wait for all users of the old one to release
    auto old_tech = currently_active_tech.exchange(nullptr);

    if (old_tech)
    {
        LOG_TRACE("Deiniting current tech");
        while (old_tech.use_count() > 1)
            std::this_thread::yield();

        old_tech->deinit();

        std::memset(frame_reports, 0, sizeof(frame_reports));

        return true;
    }

    return false;
}

bool InputCommon::init_tech(IUnknown* pDevice, API api, LowLatencyMode desiredMode)
{
    if (!currently_active_tech.load() && delay_deinit == 0)
    {
        auto try_init = [&](auto low_latency_tech, const char* name) -> bool
        {
            if (low_latency_tech->init(pDevice))
            {
                LOG_INFO("LowLatency algo: {}", name);
                currently_active_tech.store(std::move(low_latency_tech));
                return true;
            }
            return false;
        };

        bool isInitialized = false;
        switch (desiredMode)
        {
        case LowLatencyMode::XeLL:
            isInitialized = try_init(std::make_shared<XeLL>(), "XeLL");
            break;
        case LowLatencyMode::AntiLag2:
            isInitialized = try_init(std::make_shared<AntiLag2>(), "AntiLag2");
            break;
        case LowLatencyMode::Reflex:
            if (api == API::Vulkan)
                isInitialized = try_init(std::make_shared<ReflexVk>(), "Reflex Vulkan");
            else
                isInitialized = try_init(std::make_shared<Reflex>(), "Reflex");
            break;
        case LowLatencyMode::AntiLagVk:
            isInitialized = try_init(std::make_shared<AntiLagVk>(), "AntiLag Vulkan");
            break;
        case LowLatencyMode::LatencyFlex:
            isInitialized = try_init(std::make_shared<LatencyFlex>(), "LatencyFlex");
            break;
        default:
            break;
        }

        if (!isInitialized && desiredMode != LowLatencyMode::LatencyFlex)
        {
            failed_output = desiredMode;
            failed_time = std::chrono::steady_clock::now();
            isInitialized = try_init(std::make_shared<LatencyFlex>(), "LatencyFlex (Fallback)");
            if (isInitialized)
            {
                Config::Instance()->LowLatencyOutput.set_volatile_value(LowLatencyMode::LatencyFlex);
            }
        }

        if (auto current_tech = currently_active_tech.load(); current_tech && isInitialized)
        {
            activeOutput = current_tech->get_mode();
            output_api = api;

            // XeFG needs XeLL in low latency mode, disabling it only stops sending the sleep and marker calls
            current_tech->set_forced_mode(xefg_paced());
            current_tech->set_low_latency_override(Config::Instance()->FN_ForceReflex.value_or_default());

            apply_sleep_mode(current_tech.get()); // Restore any potential sleep mode
            return true;
        }
    }

    return false;
}

LowLatencyMode InputCommon::default_output(API api)
{
    // TODO: add avaliableOutput, somehow ?
    auto vendorId = IdentifyGpu::getPrimaryGpu().vendorId;

    // Only the device's extensions, there's no XeLL for Vulkan
    if (api == API::Vulkan)
    {
        if (vendorId == VendorId::AMD && VulkanHooks::o_vkAntiLagUpdateAMD != nullptr)
            return LowLatencyMode::AntiLagVk;

        if (vendorId == VendorId::Nvidia && VulkanHooks::o_vkSetLatencySleepModeNV != nullptr)
            return LowLatencyMode::Reflex;

        return LowLatencyMode::LatencyFlex;
    }

    // XeLL is D3D12 only
    if (vendorId == VendorId::Intel && api != API::DX11)
        return LowLatencyMode::XeLL;

    if (vendorId == VendorId::AMD)
        return LowLatencyMode::AntiLag2;

    if (vendorId == VendorId::Nvidia && !fakenvapi::isUsingAsMainNvapi())
        return LowLatencyMode::Reflex;

    return LowLatencyMode::LatencyFlex;
}

LowLatencyMode InputCommon::for_api(LowLatencyMode mode, API api)
{
    if (api == API::Vulkan && mode == LowLatencyMode::AntiLag2)
        return LowLatencyMode::AntiLagVk;

    if (api != API::Vulkan && mode == LowLatencyMode::AntiLagVk)
        return LowLatencyMode::AntiLag2;

    if (api != API::DX12 && mode == LowLatencyMode::XeLL)
        return default_output(api);

    return mode;
}

API InputCommon::device_api(const InputContext& inputContext, IUnknown* pDevice)
{
    if (inputContext.api != API::NotSelected)
        return inputContext.api;

    // Reflex and UE take either D3D device
    thread_local IUnknown* lastDevice = nullptr;
    thread_local API lastApi = API::NotSelected;

    if (pDevice == nullptr)
        return lastApi;

    if (pDevice != lastDevice)
    {
        ID3D11Device* device11 = nullptr;

        if (pDevice->QueryInterface(IID_PPV_ARGS(&device11)) == S_OK)
        {
            device11->Release();
            lastApi = API::DX11;
        }
        else
        {
            lastApi = API::DX12;
        }

        lastDevice = pDevice;
    }

    return lastApi;
}

API InputCommon::register_call(const InputContext& inputContext, IUnknown* pDevice)
{
    auto api = device_api(inputContext, pDevice);

    // Ignore context that Opti creates
    if (!inputContext.localContext)
    {
        if (api != API::NotSelected)
            input_api[static_cast<size_t>(inputContext.caller)] = api;

        avaliableInputs.set(inputContext.caller);
    }

    return api;
}

API InputCommon::uses_api()
{
    if (activeInput != LowLatencyInput::None)
    {
        if (auto api = input_api[static_cast<size_t>(activeInput)].load(); api != API::NotSelected)
            return api;
    }

    return State::Instance().swapchainApi;
}

std::optional<LowLatencyMode> InputCommon::forced_output()
{
    // OptiScaler's XeFG paces through XeLL, the game's XeFG through its own XeLL (no output). OptiScaler's DLSSG and
    // FSR-FG take any output, the game's own FSR-FG and DLSSG default to their own (default_input, default_output).
    if (State::Instance().activeFgOutput == FGOutput::XeFG || InputXeLL::IsNative())
        return LowLatencyMode::XeLL;

    return std::nullopt;
}

bool InputCommon::xefg_paced()
{
    // OptiScaler's XeFG needs XeLL in low latency mode and knows its generated frames
    return activeOutput == LowLatencyMode::XeLL && State::Instance().activeFgOutput == FGOutput::XeFG;
}

LowLatencyInput InputCommon::default_input()
{
    // With the XeFG input the game's own low latency for its frame generation is XeLL, Reflex markers it also sends
    // belong to its other paths. A game that turns XeLL off uses another frame generation, The Witcher 3 its DLSSG
    // with Reflex.
    if (State::Instance().activeFgInput == FGInput::XeFG && avaliableInputs[LowLatencyInput::XeLL] &&
        get_sleep_copy(LowLatencyInput::XeLL).low_latency_enabled)
    {
        return LowLatencyInput::XeLL;
    }

    // The game's FSR-FG only reports its generated frames through AntiLag 2
    if (avaliableInputs[LowLatencyInput::AntiLag2] && reports_frame_generation(LowLatencyInput::AntiLag2))
        return LowLatencyInput::AntiLag2;

    if (avaliableInputs[LowLatencyInput::Reflex])
        return LowLatencyInput::Reflex;

    // A game can have XeLL without it being enabled in its settings, it does nothing until it is
    if (avaliableInputs[LowLatencyInput::XeLL] && get_sleep_copy(LowLatencyInput::XeLL).low_latency_enabled)
        return LowLatencyInput::XeLL;

    if (avaliableInputs[LowLatencyInput::AntiLag2])
        return LowLatencyInput::AntiLag2;

    if (avaliableInputs[LowLatencyInput::UeLowLatency])
        return LowLatencyInput::UeLowLatency;

    if (avaliableInputs[LowLatencyInput::XeLL])
        return LowLatencyInput::XeLL;

    // OptiScaler's own Streamline (DLSSG output) when the game has nothing
    if (avaliableInputs[LowLatencyInput::OptiScaler])
        return LowLatencyInput::OptiScaler;

    return LowLatencyInput::None;
}

const char* InputCommon::incompatibility(LowLatencyInput input, LowLatencyMode output)
{
    // Reflex and XeLL send everything, with frame ids. OptiScaler sends its sleep and present markers, with ids.
    if (input != LowLatencyInput::AntiLag2 && input != LowLatencyInput::UeLowLatency &&
        input != LowLatencyInput::OptiScaler)
    {
        return nullptr;
    }

    if (output == LowLatencyMode::XeLL && input != LowLatencyInput::OptiScaler)
    {
        return input == LowLatencyInput::AntiLag2
                   ? "XeLL needs frame ids and frame markers, AntiLag 2 has neither"
                   : "XeLL needs render submit and present markers, UE only sends simulation markers";
    }

    // The other modes end frames on render submit markers, Reflex ID also sleeps on simulation start markers
    if (output == LowLatencyMode::LatencyFlex &&
        (LFXMode) Config::Instance()->FN_LatencyFlexMode.value_or_default() != LFXMode::Conservative)
    {
        return "LatencyFlex only works in the Conservative mode with this input";
    }

    return nullptr;
}

std::optional<LowLatencyInput> InputCommon::forced_input()
{
    // The game's XeFG (also with the XeFG passthrough) runs on the game's XeLL
    if (XeFGInputs::Passthrough() || InputXeLL::IsNative())
        return LowLatencyInput::XeLL;

    return std::nullopt;
}

bool InputCommon::update_low_latency_tech(IUnknown* pDevice, API api, std::optional<LowLatencyMode> mode)
{
    // The game's XeFG runs on its own XeLL, nothing else may drive an output next to it (update() removes it)
    if (InputXeLL::IsNative())
    {
        activeInput = LowLatencyInput::XeLL;
        return true;
    }

    // An output asked for (OptiScaler's XeFG) starts without one
    if (avaliableInputs.count() == 0 && !mode.has_value())
    {
        LOG_TRACE("No avaliable inputs");

        // To reflect it in the menu in some way
        // Config::Instance()->LowLatencyInput.set_volatile_value(LowLatencyInput::Auto);

        return true;
    }

    LowLatencyMode desiredMode = LowLatencyMode::None;
    LowLatencyInput desiredInput = Config::Instance()->LowLatencyInput.value_or_default();

    if (!pDevice)
    {
        // Some outputs might call sleep with nullptr
        if (activeOutput != LowLatencyMode::None && !mode.has_value())
            return true; // Allow it if we already have an output and not trying to set manually
        else
            return false;
    }

    if (mode.has_value())
        desiredMode = mode.value();

    if (desiredMode != LowLatencyMode::None && desiredMode == activeOutput && api == output_api)
    {
        // No need to do anything
        return true;
    }

    // TODO: add option for totally disabling specific inputs on boot

    if (auto forced = forced_input())
        desiredInput = *forced;

    // None was the earlier name of the default
    if (desiredInput == LowLatencyInput::None)
        desiredInput = LowLatencyInput::Auto;

    // The selection stays, the input is used once the game starts using it
    if (!avaliableInputs[desiredInput] && desiredInput != LowLatencyInput::Auto)
    {
        static std::atomic<LowLatencyInput> unavailable = LowLatencyInput::_;

        if (unavailable.exchange(desiredInput) != desiredInput)
            LOG_WARN("Selected Low Latency Input is not avaliable yet: {}", magic_enum::enum_name(desiredInput));

        desiredInput = LowLatencyInput::Auto;
    }

    if (desiredInput == LowLatencyInput::Auto)
        desiredInput = default_input();

    // We can just change the activeInput because it only controls what calls get through
    if (activeInput != desiredInput)
    {
        activeInput = desiredInput;
        LOG_INFO("Low latency input: {}", magic_enum::enum_name(activeInput));

        // The previous input's frame reports would stay in the overlay, its frame ids don't continue
        std::memset(frame_reports, 0, sizeof(frame_reports));

        for (auto& id : last_marker_frame_ids)
            id = 0;

        if (auto current_tech = currently_active_tech.load())
            apply_sleep_mode(current_tech.get()); // Restore any potential sleep mode
    }

    // The output runs on the active input's device, another API's device can't start it. OptiScaler's frame
    // generation asks for its output with its own device.
    if (auto inputApi = input_api[static_cast<size_t>(activeInput)].load();
        !mode.has_value() && inputApi != API::NotSelected && api != inputApi)
    {
        return true;
    }

    // The frame generation that forces the output runs it on its own device, OptiScaler's XeFG on D3D12 also for
    // D3D11 games through interop. Calls from an API without that output only drive it, the frame generation starts
    // it (set_low_latency_tech).
    if (auto forced = forced_output(); forced.has_value() && !mode.has_value() && for_api(*forced, api) != *forced)
        return true;

    if (desiredMode == LowLatencyMode::None)
        desiredMode = Config::Instance()->LowLatencyOutput.value_or_default();

    if (desiredMode == LowLatencyMode::Auto || desiredMode == LowLatencyMode::None)
        desiredMode = default_output(api);

    // The frame generation output decides the low latency output it works with
    if (auto forced = forced_output())
        desiredMode = *forced;

    desiredMode = for_api(desiredMode, api);

    // An output that couldn't start keeps its fallback instead of being retried every call
    if (desiredMode == failed_output && currently_active_tech.load() != nullptr &&
        std::chrono::steady_clock::now() - failed_time < std::chrono::seconds(10))
    {
        desiredMode = activeOutput;
    }

    if (activeOutput == desiredMode && api == output_api)
    {
        delay_deinit = 0;
        return true;
    }

    // Beyond this point activeOutput needs changing

    std::scoped_lock lock(create_tech_mutex);

    if (init_tech(pDevice, api, desiredMode))
        return true;

    auto try_reinit = [&]() -> bool
    {
        if (!deinit_current_tech())
        {
            LOG_ERROR("Couldn't deinitialize low latency tech");
            return false;
        }

        return init_tech(pDevice, api, desiredMode);
    };

    // WAR: FSR FG might still be using AntiLag 2, give Opti time to set AL2 context to null
    if (delay_deinit > 0)
    {
        if (--delay_deinit == 0)
            return try_reinit();
    }
    else
    {
        bool al2 = false;
        {
            auto current_tech = currently_active_tech.load();
            if (current_tech && current_tech->get_mode() == LowLatencyMode::AntiLag2)
                al2 = true;
        }

        if (al2)
        {
            delay_deinit = 50;
        }
        else
        {
            return try_reinit();
        }
    }

    return true;
}

void InputCommon::add_marker_to_report(const MarkerParams& marker_params)
{
    auto current_timestamp = Util::GetTimestamp() / 1000;
    static auto last_sim_start = current_timestamp;
    static auto _2nd_last_sim_start = current_timestamp;
    auto current_report = &frame_reports[marker_params.frame_id % FRAME_REPORTS_BUFFER_SIZE];

    if (current_report->frameID != marker_params.frame_id)
    {
        *current_report = FrameReport {};
    }

    current_report->frameID = marker_params.frame_id;
    current_report->gpuFrameTimeUs = (uint32_t) (last_sim_start - _2nd_last_sim_start);
    current_report->gpuActiveRenderTimeUs = 100;
    current_report->driverStartTime = current_timestamp;
    current_report->driverEndTime = current_timestamp + 100;
    current_report->gpuRenderStartTime = current_timestamp;
    current_report->gpuRenderEndTime = current_timestamp + 100;
    current_report->osRenderQueueStartTime = current_timestamp;
    current_report->osRenderQueueEndTime = current_timestamp + 100;
    switch (marker_params.marker_type)
    {
    case MarkerType::SIMULATION_START:
        _2nd_last_sim_start = last_sim_start;
        last_sim_start = Util::GetTimestamp() / 1000;
        current_report->simStartTime = last_sim_start;
        break;
    case MarkerType::SIMULATION_END:
        current_report->simEndTime = Util::GetTimestamp() / 1000;
        break;
    case MarkerType::RENDERSUBMIT_START:
        current_report->renderSubmitStartTime = Util::GetTimestamp() / 1000;
        break;
    case MarkerType::RENDERSUBMIT_END:
        current_report->renderSubmitEndTime = Util::GetTimestamp() / 1000;
        break;
    case MarkerType::PRESENT_START:
        current_report->presentStartTime = Util::GetTimestamp() / 1000;
        break;
    case MarkerType::PRESENT_END:
        current_report->presentEndTime = Util::GetTimestamp() / 1000;
        break;
    case MarkerType::INPUT_SAMPLE:
        current_report->inputSampleTime = Util::GetTimestamp() / 1000;
        break;
    default:
        break;
    }
}

// public
InputResult InputCommon::set_low_latency_tech(IUnknown* pDevice, LowLatencyMode mode)
{
    if (!update_low_latency_tech(pDevice, API::DX12, mode))
        return InputResult::LowLatencyUpdateFail;

    return InputResult::Ok;
}

InputResult InputCommon::sleep(const InputContext& inputContext, IUnknown* pDevice, std::optional<uint32_t> frame_id)
{
    if (!update_low_latency_tech(pDevice, register_call(inputContext, pDevice)))
        return InputResult::LowLatencyUpdateFail;

    if (inputContext.caller != activeInput)
        return InputResult::UsingDifferentInput;

    presents_without_input = 0;

    if (auto current_tech = currently_active_tech.load())
        current_tech->sleep(frame_id);
    else
        return InputResult::NoReadyOutput;

    return InputResult::Ok;
}

InputResult InputCommon::set_marker(const InputContext& inputContext, IUnknown* pDevice,
                                    const MarkerParams& marker_params, bool toOutput)
{
    if (!update_low_latency_tech(pDevice, register_call(inputContext, pDevice)))
        return InputResult::LowLatencyUpdateFail;

    if (inputContext.caller != activeInput)
        return InputResult::UsingDifferentInput;

    if (!marker_supported(inputContext.markerMode, marker_params.marker_type))
        return InputResult::InputNotSupported;

    // Some games send a frame's marker twice (The Witcher 3 ends its render submit again after present), the first
    // one is the right one. Frame id 0 is what inputs without frame ids send.
    if (auto type = (size_t) marker_params.marker_type;
        type < last_marker_frame_ids.size() && marker_params.frame_id != 0)
    {
        if (last_marker_frame_ids[type].exchange(marker_params.frame_id) == marker_params.frame_id)
            return InputResult::Ok;
    }

    presents_without_input = 0;

    if (marker_params.marker_type == MarkerType::PRESENT_START)
        last_present_start_frame_id = marker_params.frame_id;

    // Without frame ids there's nothing to report, the overlay's timings need them
    if (!inputContext.noFrameId)
    {
        if (marker_params.marker_type == MarkerType::SIMULATION_START)
            State::Instance().reflexFrameId = marker_params.frame_id;

        add_marker_to_report(marker_params);
    }

    if (!toOutput)
        return InputResult::Ok;

    // Markers without a frame id only let AntiLag 2 and LatencyFlex follow the sleep calls, Reflex and XeLL use ids
    if (marker_params.frame_id == 0 && (activeOutput == LowLatencyMode::Reflex || activeOutput == LowLatencyMode::XeLL))
    {
        return InputResult::Ok;
    }

    if (auto current_tech = currently_active_tech.load())
        current_tech->set_marker(pDevice, marker_params);
    else
        return InputResult::NoReadyOutput;

    LOG_TRACE_LOWLATENCY("{}: {}", magic_enum::enum_name(marker_params.marker_type), marker_params.frame_id);

    return InputResult::Ok;
}

InputResult InputCommon::set_async_marker(const InputContext& inputContext, ID3D12CommandQueue* pCommandQueue,
                                          const MarkerParams& marker_params)
{
    auto fgOutput = State::Instance().activeFgOutput;

    // Async markers only come with frame generation, the game's own when it's the game's input
    if (!inputContext.localContext)
        mark_frame_generation(inputContext.caller);

    // With the Reflex output OptiScaler's FSR-FG sends the async markers of its own presents (fg_output_present),
    // the game's describe presents that don't happen
    if (activeOutput == LowLatencyMode::Reflex && fgOutput == FGOutput::FSRFG)
        return InputResult::UsingDifferentInput;

    // Always allow Opti's local contexts through, like XeLL, AL2 or the async markers of its DLSSG output's
    // Streamline (with the Reflex output those go to the driver directly)
    if (inputContext.caller != activeInput && !inputContext.localContext)
        return InputResult::UsingDifferentInput;

    if (!currently_active_tech.load()) // can't init using ID3D12CommandQueue, can only check if available
        return InputResult::LowLatencyUpdateFail;

    if (!marker_supported(inputContext.markerMode, marker_params.marker_type))
        return InputResult::InputNotSupported;

    // TODO: could consider adding async markers to the report but would require some rewriting
    // add_marker_to_report(marker_params);

    if (auto current_tech = currently_active_tech.load())
        current_tech->set_async_marker(pCommandQueue, marker_params);
    else
        return InputResult::NoReadyOutput;

    LOG_TRACE_LOWLATENCY("{}: {}", magic_enum::enum_name(marker_params.marker_type), marker_params.frame_id);

    return InputResult::Ok;
}

InputResult InputCommon::set_sleep_mode(const InputContext& inputContext, IUnknown* pDevice, SleepMode* sleep_mode)
{
    // Ignore context that Opti creates. Its contexts (XeFG's XeLL) don't stand for the game's setting either, which
    // is kept even without an output yet: AntiLag 2 and XeLL only send it when it changes.
    if (!inputContext.localContext)
        get_sleep_copy(inputContext.caller) = *sleep_mode;

    if (!update_low_latency_tech(pDevice, register_call(inputContext, pDevice)))
        return InputResult::LowLatencyUpdateFail;

    if (inputContext.caller != activeInput)
        return InputResult::UsingDifferentInput;

    if (auto current_tech = currently_active_tech.load())
        apply_sleep_mode(current_tech.get());
    else
        return InputResult::NoReadyOutput;

    return InputResult::Ok;
}

InputResult InputCommon::get_sleep_status(const InputContext& inputContext, IUnknown* pDevice,
                                          SleepParams* sleep_params)
{
    if (!update_low_latency_tech(pDevice, register_call(inputContext, pDevice)))
        return InputResult::LowLatencyUpdateFail;

    // Get functions don't really need to worry about this check
    // if (inputContext.caller != activeInput)
    //     return InputResult::UsingDifferentInput;

    if (auto current_tech = currently_active_tech.load())
        current_tech->get_sleep_status(sleep_params);
    else
        return InputResult::NoReadyOutput;

    return InputResult::Ok;
}

InputResult InputCommon::get_latency(const InputContext& inputContext, IUnknown* pDev, void* latency_params)
{
    // if (inputContext.caller != activeInput)
    //     return InputResult::UsingDifferentInput;

    if (!update_low_latency_tech(pDev, register_call(inputContext, pDev)))
        return InputResult::LowLatencyUpdateFail;

    if (!latency_params)
        return InputResult::InvalidParameter;

    // OptiScaler's own Streamline uses Reflex too
    if (inputContext.caller == LowLatencyInput::Reflex || inputContext.caller == LowLatencyInput::OptiScaler)
    {
        if (activeOutput == LowLatencyMode::Reflex)
        {
            if (auto reflex_tech = std::dynamic_pointer_cast<Reflex>(currently_active_tech.load()))
            {
                if (reflex_tech->get_latency((NV_LATENCY_RESULT_PARAMS*) latency_params) == NVAPI_OK)
                    return InputResult::Ok;
            }
        }

        NV_LATENCY_RESULT_PARAMS* reports = (NV_LATENCY_RESULT_PARAMS*) latency_params;

        if (reports->version != NV_LATENCY_RESULT_PARAMS_VER1)
        {
            LOG_ERROR("Unsupported version {}", reports->version);
            return InputResult::InvalidParameter;
        }

        static_assert(sizeof(reports->frameReport) == NVAPI_BUFFER_SIZE * sizeof(FrameReport));

        if (!copy_frame_reports((FrameReport*) reports->frameReport))
            return InputResult::NotEnoughReports;

        return InputResult::Ok;
    }
    else if (inputContext.caller == LowLatencyInput::XeLL)
    {
        xell_frame_report_t* reports = (xell_frame_report_t*) latency_params;
        constexpr size_t reportCount = 64; // 64 reports, if the app allocated less then it's on them

        if (activeOutput == LowLatencyMode::XeLL)
        {
            if (auto current_tech = currently_active_tech.load())
            {
                auto xell_tech = std::static_pointer_cast<XeLL>(current_tech);
                auto result = xell_tech->xellGetFramesReports(reports);

                if (result == XELL_RESULT_SUCCESS)
                    return InputResult::Ok;
            }
        }

        // Hopefully not too slow
        // XeLL doesn't seem to make any guarantees about ordering so no sort needed
        for (auto i = 0; i < reportCount; i++)
        {
            auto& reportOut = reports[i];
            auto& reportIn = frame_reports[i];

            reportOut.m_frame_id = reportIn.frameID & 0xFFFFFFFF;
            reportOut.m_sim_start_ts = reportIn.simStartTime;
            reportOut.m_sim_end_ts = reportIn.simEndTime;
            reportOut.m_render_submit_start_ts = reportIn.renderSubmitStartTime;
            reportOut.m_render_submit_end_ts = reportIn.renderSubmitEndTime;
            reportOut.m_present_start_ts = reportIn.presentStartTime;
            reportOut.m_present_end_ts = reportIn.presentEndTime;
        }

        return InputResult::Ok;
    }
    else
    {
        return InputResult::InputNotSupported;
    }
}

bool InputCommon::copy_frame_reports(FrameReport* reports)
{
    // Assume no frame reports collected yet, report all zeros
    if (frame_reports[FRAME_REPORTS_BUFFER_SIZE - 1].frameID == 0)
    {
        std::memset(reports, 0, NVAPI_BUFFER_SIZE * sizeof(FrameReport));
        // spdlog::warn("GetLatency: Not enough data to report");
        return false;
    }

    // Sort frame reports, find the oldest
    size_t minIdx = 0;
    uint64_t minID = frame_reports[0].frameID;
    for (size_t i = 1; i < FRAME_REPORTS_BUFFER_SIZE; i++)
    {
        if (frame_reports[i].frameID < minID)
        {
            minID = frame_reports[i].frameID;
            minIdx = i;
        }
    }

    // Copy starting from older before wrapping around
    size_t firstChunk = std::min<uint64_t>(NVAPI_BUFFER_SIZE, FRAME_REPORTS_BUFFER_SIZE - minIdx);
    std::memcpy(reports, frame_reports + minIdx, firstChunk * sizeof(FrameReport));

    // Copy the rest after wrapping around
    if (firstChunk < NVAPI_BUFFER_SIZE)
        std::memcpy(reports + firstChunk, frame_reports, (NVAPI_BUFFER_SIZE - firstChunk) * sizeof(FrameReport));

    return true;
}

InputResult InputCommon::get_frame_reports(const InputContext& inputContext, IUnknown* pDev, FrameReport* reports)
{
    if (!update_low_latency_tech(pDev, register_call(inputContext, pDev)))
        return InputResult::LowLatencyUpdateFail;

    if (reports == nullptr)
        return InputResult::InvalidParameter;

    return copy_frame_reports(reports) ? InputResult::Ok : InputResult::NotEnoughReports;
}

#define UPDATE_TIMING_ENTRY(name, type)                                                                                \
    if (frameReport.name##EndTime >= frameReport.name##StartTime)                                                      \
    {                                                                                                                  \
        double name##Pos = (double) (frameReport.name##StartTime - start) / rangeNs;                                   \
        double name##Length = (double) (frameReport.name##EndTime - frameReport.name##StartTime) / rangeNs;            \
        timingDataOut.type = TimingEntry { .position = name##Pos, .length = name##Length };                            \
    }                                                                                                                  \
    else                                                                                                               \
    {                                                                                                                  \
        timingDataOut.type.reset();                                                                                    \
    }

bool InputCommon::get_timing_data(TimingData& timingDataOut)
{
    InputContext tempContext {};

    if (activeOutput == LowLatencyMode::Reflex)
    {
        // TODO: allocate struct and get everything from reflex
        timingDataOut = {};
        return false;
    }

    // auto processFrameReport = [&](const auto& frameReport) -> bool
    //{
    //     uint64_t start = UINT64_MAX;
    //     uint64_t end = 0;

    //    // Please don't look, just thought it would be least work
    //    auto pTimes = (const uint64_t*) &frameReport.simStartTime;
    //    for (auto i = 0; i < 11; i++)
    //    {
    //        auto& time = pTimes[i];
    //        if (time == 0)
    //            continue;

    //        if (time < start)
    //            start = time;

    //        if (time > end)
    //            end = time;
    //    }

    //    if (end < start)
    //        return false;

    //    double rangeNs = static_cast<double>(end - start);

    //    timingData[TimingType::TimeRange] = TimingEntry { .position = 0, .length = rangeNs };
    //    UPDATE_TIMING_ENTRY(sim, Simulation)
    //    UPDATE_TIMING_ENTRY(renderSubmit, RenderSubmit)
    //    UPDATE_TIMING_ENTRY(present, Present)
    //    UPDATE_TIMING_ENTRY(driver, Driver)
    //    UPDATE_TIMING_ENTRY(osRenderQueue, OsRenderQueue)
    //    UPDATE_TIMING_ENTRY(gpuRender, GpuRender)

    //    if (frameReport.frameID != 0)
    //        State::Instance().reflexFrameId = frameReport.frameID;

    //    return true;
    //};

    // if (_lastSleepDev && o_NvAPI_D3D_GetLatency)
    //{
    //     // Not calling free on this but it's static so hopefully fine
    //     static NV_LATENCY_RESULT_PARAMS* results = new NV_LATENCY_RESULT_PARAMS();
    //     results->version = NV_LATENCY_RESULT_PARAMS_VER;

    //    if (auto result = hkNvAPI_D3D_GetLatency(_lastSleepDev, results); result != NVAPI_OK)
    //    {
    //        LOG_WARN("NvAPI_D3D_GetLatency failed: {}", magic_enum::enum_name(result));
    //        return false;
    //    }

    //    // 64th element has the latest data
    //    return processFrameReport(results->frameReport[63]);
    //}

    // if (_lastVkSleepDev && o_NvAPI_Vulkan_GetLatency)
    //{
    //     // Not calling free on this but it's static so hopefully fine
    //     static NV_VULKAN_LATENCY_RESULT_PARAMS* results = new NV_VULKAN_LATENCY_RESULT_PARAMS();
    //     results->version = NV_VULKAN_LATENCY_RESULT_PARAMS_VER;

    //    if (auto result = hkNvAPI_Vulkan_GetLatency(_lastVkSleepDev, results); result != NVAPI_OK)
    //    {
    //        LOG_WARN("NvAPI_Vulkan_GetLatency failed: {}", magic_enum::enum_name(result));
    //        return false;
    //    }

    //    // 64th element has the latest data
    //    return processFrameReport(results->frameReport[63]);
    //}

    {
        size_t maxIdx = 0;
        uint64_t maxID = frame_reports[0].frameID;
        for (size_t i = 1; i < FRAME_REPORTS_BUFFER_SIZE; i++)
        {
            if (frame_reports[i].frameID > maxID)
            {
                maxID = frame_reports[i].frameID;
                maxIdx = i;
            }
        }

        auto highestValidId = (NVAPI_BUFFER_SIZE + maxIdx) % FRAME_REPORTS_BUFFER_SIZE;
        auto& frameReport = frame_reports[highestValidId]; // grab last one

        uint64_t start = UINT64_MAX;
        uint64_t end = 0;

        // Please don't look, just thought it would be least work
        auto pTimes = (const uint64_t*) &frameReport.simStartTime;
        for (auto i = 0; i < 11; i++)
        {
            auto& time = pTimes[i];
            if (time == 0)
                continue;

            if (time < start)
                start = time;

            if (time > end)
                end = time;
        }

        // No reports (an input without frame ids, or one that just changed) or just one marker
        if (end <= start)
        {
            timingDataOut = {};
            return false;
        }

        double rangeNs = static_cast<double>(end - start);

        timingDataOut.timeRange = TimingEntry { .position = 0, .length = rangeNs };
        UPDATE_TIMING_ENTRY(sim, simulation)
        UPDATE_TIMING_ENTRY(renderSubmit, renderSubmit)
        UPDATE_TIMING_ENTRY(present, present)
        UPDATE_TIMING_ENTRY(driver, driver)
        UPDATE_TIMING_ENTRY(osRenderQueue, osRenderQueue)
        UPDATE_TIMING_ENTRY(gpuRender, gpuRender)

        return true;
    };

    return false;
}

// As DLSSG: async present markers on the game's queue around the game's present, with the frame multiplier for
// Reflex Sync in between. Every marker uses the game's frame id.
void InputCommon::fg_game_present(ID3D12CommandQueue* gameQueue, bool presented, uint32_t frameMultiplier)
{
    auto reflex_tech = std::dynamic_pointer_cast<Reflex>(currently_active_tech.load());

    if (reflex_tech == nullptr || gameQueue == nullptr)
        return;

    uint64_t frame_id = last_present_start_frame_id;
    reflex_tech->set_async_marker(gameQueue,
                                  { frame_id, presented ? MarkerType::PRESENT_END : MarkerType::PRESENT_START });

    if (!presented)
    {
        reflex_tech->set_reflex_sync(frameMultiplier);
        fg_frame_id = frame_id;
        fg_new_batch = true;
    }
}

// As DLSSG on its present thread: out of band render submit markers around each batch of generated and real
// frames, out of band present markers around each of their presents, all with the game's frame id
void InputCommon::fg_output_present(ID3D12CommandQueue* presentQueue, bool presented, uint32_t frameMultiplier)
{
    // Only the FG presenter thread calls this
    static uint64_t batchFrameId = 0;
    static uint32_t batchPresents = 0;
    static bool batchOpen = false;

    auto reflex_tech = std::dynamic_pointer_cast<Reflex>(currently_active_tech.load());

    if (reflex_tech == nullptr || presentQueue == nullptr)
        return;

    auto send = [&](MarkerType type) { reflex_tech->set_async_marker(presentQueue, { batchFrameId, type }); };

    if (!presented && fg_new_batch.exchange(false))
    {
        // A batch cut short still gets its end
        if (batchOpen)
            send(MarkerType::OUT_OF_BAND_RENDERSUBMIT_END);

        batchFrameId = fg_frame_id;
        batchPresents = 0;
        batchOpen = true;
        send(MarkerType::OUT_OF_BAND_RENDERSUBMIT_START);
    }

    if (batchFrameId == 0)
        return;

    send(presented ? MarkerType::OUT_OF_BAND_PRESENT_END : MarkerType::OUT_OF_BAND_PRESENT_START);

    if (presented && batchOpen && ++batchPresents >= frameMultiplier)
    {
        send(MarkerType::OUT_OF_BAND_RENDERSUBMIT_END);
        batchOpen = false;
    }
}

void InputCommon::apply_sleep_mode(LowLatencyTech* tech)
{
    auto sleepMode = get_sleep_copy(activeInput);

    if (auto interval = fps_limit_interval_us.load(); interval != 0)
        sleepMode.minimum_interval_us = interval;

    tech->set_sleep_mode(&sleepMode);
}

void InputCommon::update()
{
    auto& state = State::Instance();
    auto current_tech = currently_active_tech.load();
    bool active = false;
    ++present_count;

    auto fpsLimit = Config::Instance()->FramerateLimit.value_or_default();

    // The game's XeFG with its own XeLL, only OptiScaler's FPS limit goes to it. XeLL knows the generated frames.
    if (InputXeLL::IsNative())
    {
        if (current_tech != nullptr)
        {
            current_tech.reset();
            std::scoped_lock lock(create_tech_mutex);
            deinit_current_tech();
            activeOutput = LowLatencyMode::None;
        }

        limits_fps = InputXeLL::LimitFps(fpsLimit > 0.0f ? static_cast<uint32_t>(std::round(1'000'000 / fpsLimit)) : 0);
        return;
    }

    // Force State can change at any time, XeLL takes a changed state through its sleep mode
    if (current_tech != nullptr)
    {
        bool xefgPaced = xefg_paced();

        // AntiLag 2, LatencyFlex and XeLL without XeFG fall apart with more than 1 generated DLSSG frame. Only when it
        // starts, Force State can be changed back afterwards.
        static bool lastMultiFrame = false;
        bool multiFrame =
            state.dlssgLastSetMode == sl::DLSSGMode::eDynamic || state.dlssgDetectedInterpolationCount > 1;

        if (!std::exchange(lastMultiFrame, multiFrame) && multiFrame && activeOutput != LowLatencyMode::Reflex &&
            !xefgPaced && Config::Instance()->FN_ForceReflex.value_or_default() != ForceReflex::ForceEnable)
        {
            LOG_INFO("Multi frame generation, disabling {}", magic_enum::enum_name(activeOutput));
            Config::Instance()->FN_ForceReflex.set_volatile_value(ForceReflex::ForceDisable);
        }

        // OptiScaler's XeFG can start and stop at any time
        static bool lastXefgPaced = false;
        static ForceReflex lastOverride = ForceReflex::InGame;
        auto lowLatencyOverride = Config::Instance()->FN_ForceReflex.value_or_default();
        current_tech->set_low_latency_override(lowLatencyOverride);
        current_tech->set_forced_mode(xefgPaced);

        bool overrideChanged = std::exchange(lastOverride, lowLatencyOverride) != lowLatencyOverride;

        if (overrideChanged)
            LOG_INFO("Low latency Force State: {}", magic_enum::enum_name(lowLatencyOverride));

        if (std::exchange(lastXefgPaced, xefgPaced) != xefgPaced || overrideChanged)
            apply_sleep_mode(current_tech.get());
    }

    // The output only sleeps while the game's low latency calls drive it
    if (current_tech != nullptr && !state.reflexLimitsFps && activeInput != LowLatencyInput::None &&
        ++presents_without_input <= 20)
    {
        auto mode = current_tech->get_mode();

        // Their limiters only work with low latency enabled, for XeLL with XeFG it only skips the sleep calls.
        // Vulkan's Reflex has no hooks to limit through, its sleep mode limits without low latency too.
        if (mode == LowLatencyMode::AntiLag2 || mode == LowLatencyMode::XeLL || mode == LowLatencyMode::LatencyFlex ||
            mode == LowLatencyMode::AntiLagVk)
        {
            active = current_tech->is_enabled();
        }
        else if (mode == LowLatencyMode::Reflex && output_api == API::Vulkan)
        {
            active = true;
        }
    }

    uint32_t interval = 0;

    if (active && fpsLimit > 0.0f)
    {
        interval = static_cast<uint32_t>(std::round(1'000'000 / fpsLimit));

        // The limit is for the presented frames, the output limits the game's. XeFG paces through XeLL, which knows
        // about its generated frames. The game's own DLSSG reports its count through the NGX evaluate.
        auto fg = state.currentFG;

        if (xefg_paced())
        {
            // XeLL already counts XeFG's generated frames
        }
        else if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
        {
            interval *= fg->GetInterpolatedFrameCount() + 1;
        }
        else if (state.dlssgDetectedInterpolationCount > 0)
        {
            interval *= state.dlssgDetectedInterpolationCount + 1;
        }
        else if (activeInput == LowLatencyInput::AntiLag2 && reports_frame_generation(LowLatencyInput::AntiLag2))
        {
            // TODO: The game's FSR-FG as one generated frame, count AntiLag 2's interpolated presents for more
            interval *= 2;
        }
    }

    limits_fps = active;

    if (fps_limit_interval_us.exchange(interval) != interval)
    {
        LOG_INFO("FPS limit through {}: {} us", magic_enum::enum_name(activeOutput), interval);

        if (current_tech != nullptr)
            apply_sleep_mode(current_tech.get());
    }
}

InputResult InputCommon::mark_present_start(IUnknown* pDevice)
{
    // TODO: could allow AL2 but need to check the InputMarkerMode of the active AL2 input
    if (activeInput != LowLatencyInput::UeLowLatency)
        return InputResult::InputNotSupported;

    // TODO: this is missing the frame id required by other outputs
    if (activeOutput != LowLatencyMode::AntiLag2)
        return InputResult::GenericError;

    if (auto current_tech = currently_active_tech.load())
    {
        MarkerParams marker_params {};
        marker_params.frame_id = 0; // TODO: will be needed if used with non-AL2
        marker_params.marker_type = MarkerType::PRESENT_START;

        // AL2 doesn't actually use pDevice but whatever
        current_tech->set_marker(pDevice, marker_params);
    }
    else
    {
        return InputResult::NoReadyOutput;
    }

    return InputResult::Ok;
}

std::shared_ptr<ReflexVk> InputCommon::reflex_vk_output()
{
    return std::dynamic_pointer_cast<ReflexVk>(currently_active_tech.load());
}

xell_context_handle_t InputCommon::xell_output_context()
{
    auto current_tech = currently_active_tech.load();

    if (current_tech == nullptr || current_tech->get_mode() != LowLatencyMode::XeLL)
        return nullptr;

    return (xell_context_handle_t) current_tech->get_tech_context();
}
