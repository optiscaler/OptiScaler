#pragma once

#include <xell.h>

#include <low_latency/low_latency_tech/low_latency_tech.h>
#include <Config.h>

enum class InputMarkerMode
{
    NoMarkers,
    SimStartOnly,            // AntiLag 2, its sleep marks the simulation start
    SimStartAndPresentStart, // AntiLag 2 with frame generation
    SimOnly,                 // Start and End
    FullMarkers,             // TODO: add something about xell's partial async markers
};

struct InputContext
{
    LowLatencyInput caller;
    bool localContext; // input created by Opti
    bool noFrameId;
    InputMarkerMode markerMode;
};

enum class InputResult : uint32_t
{
    Ok,
    UsingDifferentInput,
    InputNotSupported,
    InvalidParameter,
    LowLatencyUpdateFail,
    NotEnoughReports,
    NoReadyOutput,
    GenericError,
};

struct TimingData
{
    std::optional<TimingEntry> timeRange; // in ns, value stored in length
    std::optional<TimingEntry> simulation;
    std::optional<TimingEntry> renderSubmit;
    std::optional<TimingEntry> present;
    std::optional<TimingEntry> driver;
    std::optional<TimingEntry> osRenderQueue;
    std::optional<TimingEntry> gpuRender;
};

class InputCommon
{
    inline static std::atomic<std::shared_ptr<LowLatencyTech>> currently_active_tech;
    inline static std::mutex create_tech_mutex {};

    inline static FrameReport frame_reports[FRAME_REPORTS_BUFFER_SIZE] {};
    inline static std::array<std::atomic_uint64_t, 6> last_marker_frame_ids {}; // Simulation start to present end
    inline static LowLatencyMode failed_output = LowLatencyMode::None;
    inline static std::chrono::steady_clock::time_point failed_time {};
    inline static std::atomic_uint64_t last_present_start_frame_id = 0;
    inline static std::atomic_uint32_t delay_deinit = 0;
    inline static std::array<SleepMode, static_cast<size_t>(LowLatencyInput::_)> sleep_mode_copies {};
    inline static std::atomic_uint32_t presents_without_input = 0;
    inline static std::atomic_uint32_t fps_limit_interval_us = 0; // OptiScaler's FPS limit for the output, 0 = none
    inline static std::atomic_bool limits_fps = false;
    // The present each input last reported the game's own frame generation on (async markers), 0 = never
    inline static std::atomic_uint64_t present_count = 0;
    inline static std::array<std::atomic_uint64_t, static_cast<size_t>(LowLatencyInput::_)> last_fg_present {};

    static void mark_frame_generation(LowLatencyInput input)
    {
        last_fg_present[static_cast<size_t>(input)] = present_count + 1;
    }

    static bool reports_frame_generation(LowLatencyInput input)
    {
        auto last = last_fg_present[static_cast<size_t>(input)].load();
        return last != 0 && present_count + 1 - last < 20;
    }

    static bool xefg_paced();

    inline static flag_set<LowLatencyInput> avaliableInputs {};
    inline static LowLatencyInput activeInput = LowLatencyInput::None;
    inline static LowLatencyMode activeOutput = LowLatencyMode::None;
    inline static bool enabled = false;

    static bool deinit_current_tech();
    static bool init_tech(IUnknown* pDevice, LowLatencyMode desiredMode);
    static bool update_low_latency_tech(IUnknown* pDevice, std::optional<LowLatencyMode> mode = std::nullopt);
    static void add_marker_to_report(const MarkerParams& marker_params);
    static void set_input_avaliable(LowLatencyInput input) { avaliableInputs.set(input); };
    static SleepMode& get_sleep_copy(LowLatencyInput input) { return sleep_mode_copies[static_cast<size_t>(input)]; }
    static void apply_sleep_mode(LowLatencyTech* tech);

  public:
    static InputResult set_low_latency_tech(IUnknown* pDevice, LowLatencyMode mode);

    static InputResult sleep(const InputContext& inputContext, IUnknown* pDevice,
                             std::optional<uint32_t> frame_id = std::nullopt);
    // toOutput false only tracks the marker, the output gets it another way
    static InputResult set_marker(const InputContext& inputContext, IUnknown* pDevice,
                                  const MarkerParams& marker_params, bool toOutput = true);
    static InputResult set_async_marker(const InputContext& inputContext, ID3D12CommandQueue* pCommandQueue,
                                        const MarkerParams& marker_params);
    static InputResult set_sleep_mode(const InputContext& inputContext, IUnknown* pDevice, SleepMode* sleep_mode);
    static InputResult get_sleep_status(const InputContext& inputContext, IUnknown* pDevice, SleepParams* sleep_params);
    static InputResult
    get_latency(const InputContext& inputContext, IUnknown* pDev,
                void* latency_params); // NV_LATENCY_RESULT_PARAMS* for reflex, xell_frame_report_t* for xell,
    static bool get_timing_data(TimingData& timingDataOut);
    static uint64_t get_last_present_start_frame_id() { return last_present_start_frame_id; };
    static flag_set<LowLatencyInput> get_avaliable_inputs() { return avaliableInputs; };
    static void get_currently_active(LowLatencyInput& activeInput, LowLatencyMode& activeOutput)
    {
        activeInput = InputCommon::activeInput;
        activeOutput = InputCommon::activeOutput;
    }

    static InputResult mark_present_start(IUnknown* pDevice);

    // Called on every present: Force State and OptiScaler's FPS limit for the active output. The Reflex output is
    // limited through the Reflex hooks.
    static void update();
    static bool can_limit_fps() { return limits_fps; }

    // OptiScaler's own frame generation presenter (FSR-FG) gives the Reflex output the async calls DLSSG makes
    static void fg_game_present(ID3D12CommandQueue* gameQueue, bool presented, uint32_t frameMultiplier);
    static void fg_output_present(ID3D12CommandQueue* presentQueue, bool presented, uint32_t frameMultiplier);

    // The best of the inputs the game sends: Reflex, XeLL enabled in the game, AntiLag 2, UE, XeLL, then
    // OptiScaler's own Streamline
    static LowLatencyInput default_input();

    // The GPU vendor's own low latency output
    static LowLatencyMode default_output();

    // Why the input can't drive the output, nullptr when it can
    static const char* incompatibility(LowLatencyInput input, LowLatencyMode output);

    // Set when the frame generation in use decides the low latency output or input: OptiScaler's XeFG needs XeLL,
    // the game's XeFG keeps its own XeLL
    static std::optional<LowLatencyMode> forced_output();
    static std::optional<LowLatencyInput> forced_input();

    // The XeLL output's context, for OptiScaler's XeFG
    static xell_context_handle_t xell_output_context();
};
