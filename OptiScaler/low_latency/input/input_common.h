#pragma once

#include <xell.h>

#include <low_latency/low_latency_tech/low_latency_tech.h>
#include <Config.h>

enum class InputMarkerMode
{
    NoMarkers,
    PresentStartOnly,
    SimOnly,     // Start and End
    FullMarkers, // TODO: add something about xell's partial async markers
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

  public:
    static InputResult set_low_latency_tech(IUnknown* pDevice, LowLatencyMode mode);

    static InputResult sleep(const InputContext& inputContext, IUnknown* pDevice,
                             std::optional<uint32_t> frame_id = std::nullopt);
    static InputResult set_marker(const InputContext& inputContext, IUnknown* pDevice,
                                  const MarkerParams& marker_params);
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

    // OptiScaler's own frame generation presenter (FSR-FG) gives the Reflex output the async calls DLSSG makes
    static void fg_game_present(ID3D12CommandQueue* gameQueue, bool presented, uint32_t frameMultiplier);
    static void fg_output_present(ID3D12CommandQueue* presentQueue, bool presented, uint32_t frameMultiplier);

    // The GPU vendor's own low latency output
    static LowLatencyMode default_output();

    // Set when the frame generation in use decides the low latency output or input
    static std::optional<LowLatencyMode> forced_output();
    static std::optional<LowLatencyInput> forced_input();

    // passthrough when possible, fillout with local frame_reports if not

    // XeLL-specific calls for passthrough, TODO: only allow when caller == activeOutput == activeInput == XeLL
    static xell_result_t
    pass_xellD3D12SetAppQueue(const InputContext& inputContext,
                              ID3D12CommandQueue* appQueue); // if it's ID3D12CommandQueue* then some translation with
                                                             // async markers can be made
    static xell_result_t pass_xellSetDisplayInfo(const InputContext& inputContext, void* displayInfo);
    static xell_result_t pass_xellSetFgEnabled(const InputContext& inputContext, uint32_t param1, uint32_t param2);
    static xell_result_t pass_xellSetGeneratedFramesCount(const InputContext& inputContext, uint32_t frameId,
                                                          uint32_t framesCount);
};
