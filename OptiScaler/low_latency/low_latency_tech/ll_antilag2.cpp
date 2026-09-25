#include "pch.h"
#include "ll_antilag2.h"
#include <nvapi/fakenvapi/log.h>
#include <hooks/Amdxc64_Hooks.h>

inline HRESULT AntiLag2::al2_sleep()
{
    int max_fps = 0;

    // TODO: test more, config for this?

    // if (effective_fg_state && minimum_interval_us != 0) {
    //     static uint64_t previous_frame_time = 0;
    //     uint64_t current_time = Util::GetTimestamp();
    //     uint64_t frame_time = current_time - previous_frame_time;
    //     if (frame_time < 1000 * minimum_interval_us) {
    //         if (auto res = eepy(minimum_interval_us * 1000 - frame_time); res)
    //             LOG_ERROR("Sleep command failed: {}", res);
    //     }
    //     previous_frame_time = Util::GetTimestamp();
    // } else {
    //     max_fps = minimum_interval_us > 0 ? (int) std::round(1000000.0f / minimum_interval_us) : 0;
    // }
    max_fps = minimum_interval_us > 0 ? (int) std::round(1000000.0f / minimum_interval_us) : 0;

    HRESULT result = {};

    // auto pre_sleep = Util::GetTimestamp();

    if (dx12_ctx.m_pAntiLagAPI)
        result = AMD::AntiLag2DX12::Update(&dx12_ctx, is_enabled(), max_fps);
    else if (dx11_ctx.m_pAntiLagAPI)
        result = AMD::AntiLag2DX11::Update(&dx11_ctx, is_enabled(), max_fps);

    // log_event("al2_sleep", "{}", Util::GetTimestamp() - pre_sleep);

    LOG_TRACE_LOWLATENCY("FSR Anti-Lag 2.0 Call Spot: {}",
                         current_call_spot == CallSpot::SimulationStart ? "SimulationStart" : "SleepCall");

    return result;
}

void AntiLag2::set_fg_type(bool interpolated, uint64_t frame_id)
{
    AMD::AntiLag2DX12::SetFrameGenFrameType(&dx12_ctx, interpolated);
}

bool AntiLag2::init(IUnknown* pDevice)
{
    if (!dx12_ctx.m_pAntiLagAPI && !dx11_ctx.m_pAntiLagAPI && pDevice)
    {
        ID3D12Device* device = nullptr;

        // Specifically for Linux where the amdxc64.dll may not be loaded yet
        {
            std::scoped_lock lock(amdxc64_load_mutex);
            LoadLibraryA("amdxc64.dll");
            amdxc64_load_times++;
        }

        HRESULT hr = pDevice->QueryInterface(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
        if (hr == S_OK)
        {
            Amdxc64Hooks::giveGameAl2Proxy = false;
            ScopedSkipVulkanHooks skipVulkanHooks {};
            HRESULT init_return = AMD::AntiLag2DX12::Initialize(&dx12_ctx, device);
            Amdxc64Hooks::giveGameAl2Proxy = true;
            if (init_return == S_OK)
            {
                LOG_INFO("FSR Anti-Lag 2.0 DX12 initialized");
                return true;
            }
            else
            {
                LOG_INFO("FSR Anti-Lag 2.0 DX12 initialization failed");
            }
        }
        else
        {
            // dxvk + windows, antilag2 dx11 calls vkCreateDevice late and kills the overlay
            ScopedSkipVulkanHooks skipVulkanHooks {};
            HRESULT init_return = AMD::AntiLag2DX11::Initialize(&dx11_ctx);
            if (init_return == S_OK)
            {
                LOG_INFO("FSR Anti-Lag 2.0 DX11 initialized");
                return true;
            }
            else
            {
                LOG_INFO("FSR Anti-Lag 2.0 DX11 initialization failed");
            }
        }
    }
    else
    {
        LOG_WARN("Initialization of FSR Anti-Lag 2.0 was attempted while the context is not null");
    }

    return false;
}

void AntiLag2::deinit()
{
    {
        std::scoped_lock lock(amdxc64_load_mutex);

        const auto module = GetModuleHandleA("amdxc64.dll");
        for (uint64_t i = 0; i < amdxc64_load_times; i++)
            FreeLibrary(module);

        amdxc64_load_times = 0;
    }

    if (dx12_ctx.m_pAntiLagAPI)
    {
        // WAR for AL2 not deiniting properly when it's enabled
        AMD::AntiLag2DX12::Update(&dx12_ctx, false, 0);

        if (!AMD::AntiLag2DX12::DeInitialize(&dx12_ctx))
            LOG_INFO("FSR Anti-Lag 2.0 DX12 deinitialized");
    }

    if (dx11_ctx.m_pAntiLagAPI && !AMD::AntiLag2DX11::DeInitialize(&dx11_ctx))
        LOG_INFO("FSR Anti-Lag 2.0 DX11 deinitialized");
}

void* AntiLag2::get_tech_context()
{
    if (dx12_ctx.m_pAntiLagAPI)
        return &dx12_ctx;
    else if (dx11_ctx.m_pAntiLagAPI)
        return &dx11_ctx;

    return nullptr;
}

void AntiLag2::get_sleep_status(SleepParams* sleep_params)
{
    sleep_params->low_latency_enabled = is_enabled();
    sleep_params->fullscreen_vrr = true;
    sleep_params->control_panel_vsync_override = false;
}

void AntiLag2::set_sleep_mode(SleepMode* sleep_mode)
{
    // UNUSED:
    // low_latency_boost
    // use_markers_to_optimize

    low_latency_enabled = sleep_mode->low_latency_enabled;
    minimum_interval_us =
        sleep_mode->minimum_interval_us; // don't convert to fps due to fg fps limit fallback using intervals
}

void AntiLag2::sleep(std::optional<uint32_t> frame_id)
{
    last_sleep_framecount = simulation_framecount;

    if (current_call_spot == CallSpot::SleepCall)
        al2_sleep();
}

void AntiLag2::set_marker(IUnknown* pDevice, const MarkerParams& marker_params)
{
    switch (marker_params.marker_type)
    {
    case MarkerType::SIMULATION_START:
        simulation_framecount++;

        if (last_sleep_framecount + call_spot_switch_threshold < simulation_framecount)
            current_call_spot = CallSpot::SimulationStart;
        else
            current_call_spot = CallSpot::SleepCall;

        if (current_call_spot == CallSpot::SimulationStart)
            al2_sleep();
        break;

    case MarkerType::PRESENT_START:
        AMD::AntiLag2DX12::MarkEndOfFrameRendering(&dx12_ctx);
        break;
    }
}

void AntiLag2::set_async_marker(IUnknown* pCommandQueue, const MarkerParams& marker_params)
{
    if (marker_params.marker_type == MarkerType::OUT_OF_BAND_PRESENT_START)
    {
        static uint64_t previous_frame_id = marker_params.frame_id;
        set_fg_type(previous_frame_id == marker_params.frame_id, marker_params.frame_id);
        previous_frame_id = marker_params.frame_id;
    }
}
