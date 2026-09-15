#pragma once
#include "DlssNr_Dx12_ModelState.h"
#include <dlssnr/DlssNr_Placement.h>
#include <dlssnr/DlssNr_FinishedReady.h>
#include <dlssnr/PassProfiles.h>

#include <set>
#include <wrl/client.h>
#include <resource_tracking/ResTrack_Dx12.h>
#include <dlssnr/DlssNr_FinishedPictureBridge_Dx11.h>
#include <dlssnr/DlssNr_HoldParameters_Dx12.h>
#include <upscalers/ShaderPipeline_Dx12.h>

#include <dlssnr/DlssNr.h>

#include <dlssnr/DlssNr_Capture.h>
#include <dlssnr/DlssNr_PipelineCapture.h>
#include <dlssnr/DlssNr_Proxy.h>
#include <dlssnr/DlssNr_GpuLifetime.h>
#include <dlssnr/DlssNr_ExposureScan.h>

#include "DlssNr_Dx12.h"
#include "DlssNr_ActiveColor.h"
#include "DlssNr_Upscaler_Dx12.h"
#include <dlssnr/DlssNr_Pipeline_Dx12.h>
#include "DlssNr_Guides.h"
#include "DlssNr_SeamClock.h"

#include <Config.h>
#include <State.h>
#include <Util.h>

#include <proxies/NVNGX_Proxy.h>
#include <hooks/D3D12_Hooks.h>
#include <gpu_time/GpuTime_Dx12.h>
#include "DlssNr_GpuTime.h"

#include <mutex>
#include <algorithm>
#include <cstring>
#include "DlssNr_ResidualPair.h"
#include "../output_scaling/OS_Dx12.h"


using DlssNr::Profiles::NrPassTuning;
using DlssNr::Profiles::PassPreset;
using DlssNr::Profiles::PassStyle;
using DlssNr::Profiles::PassTuning;

using DlssNr::CalibrationReading;

struct DlssNr_Dx12::State
{

    // NGX result names for diagnostics.
    const char* NgxResultName(unsigned int r);



    using NrState = DlssNr::Detail::ModelStateDx12;
    NrState nr;
    DlssNr_Dx12& shader;
    struct Enlarger
    {
        template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
        std::unique_ptr<DlssNr::PrivateUpscalerDx12> dlss;
        DlssNr::GpuLifetime lifetime;
        ComPtr<ID3D12Resource> input, output, depth, motion, exposure;
        ComPtr<ID3D12CommandQueue> queue;
        ID3D12CommandList* creation = nullptr;
        unsigned w = 0, h = 0, outW = 0, outH = 0;
        uint64_t lastFrame = 0;
        bool submitted = false, failed = false, depthInverted = false, readable = false, reset = true;
        ~Enlarger() { dlss.reset(); } // Release NGX before its borrowed input/output resources.
    };
    std::unique_ptr<Enlarger> enlarger;
    std::vector<std::unique_ptr<Enlarger>> retiredEnlargers;
    bool collectingEnlargers = false;
    std::string enlargementStatus;
    void ReleaseEnlarger();
    void CollectEnlargers();
    ID3D12Resource* EnlargeMatchedResidual(ID3D12GraphicsCommandList* cmd, ID3D12Device* device,
        ID3D12Resource* proxy, ID3D12Resource* answer, ID3D12Resource* depth, ID3D12Resource* motion,
        const DlssNrFrameInfo& frame, const DlssNrConstants& resolve, bool reset, ID3D12CommandQueue* queue);

    // What the pass costs on the GPU, for the breakdown in the overlay.
    std::unique_ptr<DlssNrGpuTime> gpuTime;

    // Model-only timing separates NGX cost from encoding, copies and composition.
    std::unique_ptr<DlssNrGpuTime> ngxTime;
    std::optional<double> lastNgxTime;
    std::optional<double> lastGpuTime;

    // Writes matched before/after frames on request, so comparisons stop depending on video.
    capture::FrameCapture captureFrames;
    DlssNr::PipelineCaptureFrame* pipelineCapture = nullptr; // Owned by lifetime retirement after End.
    std::filesystem::path pipelineCaptureDirectory;
    unsigned pipelineCaptureRemaining = 0;

    // One capture happens on its own each session, so there is always a fresh sample without anyone having
    // to remember to ask. Started after the scene has had a moment to settle: the first frames after a
    // feature is built carry its reset, and are not representative of anything.
    static constexpr unsigned long long kAutoCaptureAfterFrames = 180;
    bool autoCaptureDone = false;

    // Cleared once per run, so a session's captures are its own and nothing accumulates across launches.

    unsigned long long frames = 0;

    // Logical frame identity for deferred pairing; feature readiness keeps the raw submission counter.
    DlssNrSeamClock seamClock;

    struct InputHold
    {
        struct Texture
        {
            const char* name;
            const char* alias;
            ID3D12Resource* frozen = nullptr;
            D3D12_RESOURCE_DESC desc {};
        };
        std::array<Texture, 4> textures {{
            { NVSDK_NGX_Parameter_Color, "DLSSD.Color" },
            { NVSDK_NGX_Parameter_Depth, "DLSSD.Depth" },
            { NVSDK_NGX_Parameter_MotionVectors, "DLSSD.MotionVectors" },
            { NVSDK_NGX_Parameter_ExposureTexture, "DLSSD.ExposureTexture" }
        }};
        NrHoldParameters_Dx12 parameters;
        D3D12_RESOURCE_DESC outputDesc {};
        bool active = false;
        unsigned route = 0;
        uint64_t generation = 0;
        ID3D12CommandList* captureCommands = nullptr;
    } inputHold;

    static bool SameHoldShape(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b);

    void ReleaseInputHold();

    void BeginInputHold(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params,
                        const D3D12_RESOURCE_STATES* states);

    // A capture requested from outside the game: when the render path has no fence of its own, the write
    // waits until this frame count, by which point the GPU is certainly past the copies.
    unsigned long long captureWriteAtFrame = 0;

    // Dropping a file named dlssnr-capture.trigger beside OptiScaler requests a capture, so a session can
    // be asked for one from outside the game -- no alt-tab, no menu. Checked once a second, effectively.
    void CheckCaptureTrigger();

    // The encoded mean is aimed here. Mid-grey rather than anything brighter: the model has to see both the
    // shadow detail it might lift and the highlights it must not blow out.
    static constexpr float kTargetEncodedMean = 0.45f;

    // How fast the derived value follows the scene. Readings arrive a few times a second, and an exposure
    // that lunges at every cut is worse than one that arrives a moment late.
    static constexpr float kWhitePointBlend = 0.25f;

    // Recomputes the white point from a measured mean. Inverting the encode for the white point that puts
    // that mean at the target gives wp = mean * (1 - t^g) / t^g.
    float WhitePointForMean(float meanLuma);

    DlssNr::GpuLifetime lifetime;

    void ParkNrResource(ID3D12Resource*& resource);

    void TickNrRetired([[maybe_unused]] uint64_t epoch);

    // The inject point decides which buffer is being measured -- the upscaler's linear output or the
    // finished frame in swapchain format -- so a reading taken before a change describes a different
    // picture to one taken after. Everything else that depends on the format is invalidated here.
    void ForgetCalibration();

    void ReleaseSurfacesIfFormatChanged(DXGI_FORMAT needed);

    // The meter's grid is R32_FLOAT, which makes a row exactly 64 * 4 = 256 bytes -- the alignment a
    // texture-to-buffer copy demands, met without padding, so the readback is a flat array of floats.
    static constexpr unsigned int kMeterRowBytes = kDlssNrMeterGrid * sizeof(float);
    static constexpr unsigned int kMeterBytes = kMeterRowBytes * kDlssNrMeterGrid;

    // Records the copy of this frame's grid into whichever readback buffer is furthest from being read.
    // Same shape as the meter's copy, against the calibration surface and its own ring.
    void CopyCalibrationToReadback(ID3D12GraphicsCommandList* cmdList);

    void CopyMeterToReadback(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device, bool exposureBound);

    // Consume texel zero from the delayed readback ring, retaining the last plausible exposure.
    void ConsumeCalibrationReadback();

    void ConsumeMeterReadback();

    // Invalidate pending and held samples when the exposure source changes.
    void InvalidateExposureMeter();

    // Resolve the encode divisor from game exposure or the configured manual fallback.
    float ResolveWhitePoint(const Config& cfg, bool isHdrBuffer);

    ID3D12Resource* CreateScratch(ID3D12Device* device, DXGI_FORMAT format, unsigned int width, unsigned int height);

    void Barrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* res, D3D12_RESOURCE_STATES from,
                 D3D12_RESOURCE_STATES to);

    // A typeless resource cannot be viewed, and NGX builds its own views with nothing to tell it which
    // format to use. Depth is very often declared typeless, so the typed member of the same family is
    // substituted; CopyResource accepts that as a destination for the typeless original.
    DXGI_FORMAT TypedGuideFormat(DXGI_FORMAT f);

    bool IsTypeless(DXGI_FORMAT f);

    // Creates a typed twin of a guide buffer, matching everything but the format.
    ID3D12Resource* CreateGuideClone(ID3D12Device* device, ID3D12Resource* source);

    // Typeless guides are copied into a typed resource for NGX. The clone rests in COPY_DEST.
    // Return the original typed resource when no conversion is required.
    ID3D12Resource* ReadableGuide(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* source,
                                  ID3D12Resource** clone);

    // Try both SR and ray-reconstruction parameter names; absent values return null.
    bool FormatCanHoldLinearHdr(DXGI_FORMAT format);

    ID3D12Resource* GetResource(NVSDK_NGX_Parameter* params, const char* a, const char* b);

    bool TuningMatchesFeature(const Config& cfg, unsigned int requestedPasses);

    // Guards the module's state. Every caller is now on the game's render thread, so this is no longer
    // holding two threads apart -- but the D3D11-on-D3D12 bridge enters from its own call site, and the
    // cost is a CPU-side lock on a path that already records command lists.
    std::recursive_mutex mutex;

    // Restore the caller's compute bindings on every exit, without capturing NR's own state.
    struct ScopedNrStateEnvelope
    {
        ID3D12GraphicsCommandList* cmd;
        ScopedSkipHeapCapture skipHeap;

        bool tracking;
        explicit ScopedNrStateEnvelope(ID3D12GraphicsCommandList* c)
            : cmd(c), tracking(D3D12Hooks::IsRootSignatureTrackingEnabled())
        {
            D3D12Hooks::SetRootSignatureTracking(false);
        }

        ~ScopedNrStateEnvelope()
        {
            if (tracking)
                D3D12Hooks::RestoreRoot(cmd);
            D3D12Hooks::SetRootSignatureTracking(tracking);
        }
    };

    // Report each missing-input or readiness reason once per owner.
    void ReportSkipOnce(const char* reason);

    struct DeferredSrContext
    {
        State& owner;
        explicit DeferredSrContext(State& state) : owner(state) {}

        static constexpr unsigned MarkerCount = 16;
        struct Generation
        {
            ID3D12Device* device = nullptr;
            ID3D12CommandQueue* queue = nullptr; // identity/reference only; no private submissions
            unsigned w = 0, h = 0, outW = 0, outH = 0, flags = 0;
            DXGI_FORMAT inputFormat {}, outputFormat {};
            ID3D12Resource *edited = nullptr, *residualInput = nullptr, *residualOutput = nullptr, *clean = nullptr,
                           *composed = nullptr, *exposure = nullptr, *readback = nullptr;
            ID3D12QueryHeap* queries = nullptr;
            ID3D12Resource* accumulatedEdit[2] {};
            unsigned accumulatedIndex = 0;
            bool accumulationReadable = false, accumulationValid = false;
            volatile UINT64* completed = nullptr;
            bool occupied[MarkerCount] {};
            unsigned nextMarker = 0, lastMarker = 0;
            bool everRecorded = false, smallReadable = false, reset = true, failed = false;
            bool rayReconstruction = false, finishedPicture = false, privateRr = false;
            DlssNr::PrivateUpscaler backend = DlssNr::PrivateUpscaler::DLSS;
            std::unique_ptr<DlssNr::PrivateUpscalerDx12> upscaler;
            DlssNr::PrivateUpscalerFrameDx12 frame;
            unsigned long long createEpoch = 0;
            unsigned long long lastBeginEpoch = 0;
            bool began = false;
            std::unique_ptr<DlssNr_Dx12> codec;
            bool Idle() const { return !everRecorded || completed[lastMarker] != 0; }
            ~Generation()
            {
                DlssNr_Dx12::Retire(std::move(codec));
                upscaler.reset(); // Completion protects all four backend histories.
                if (readback && completed)
                    readback->Unmap(0, nullptr);
                for (auto* r : { edited, residualInput, residualOutput, clean, composed, exposure, readback })
                    if (r)
                        r->Release();
                for (auto* r : accumulatedEdit)
                    if (r) r->Release();
                if (queries)
                    queries->Release();
                if (queue)
                    queue->Release();
                if (device)
                    device->Release();
            }
        };

        // Record an actual GPU completion marker after EACH seam. A later CPU frame/Present count alone
        // does not prove a resource is no longer in flight. Slots aren't reused until the GPU wrote them.
        struct Use
        {
            Generation& g;
            ID3D12GraphicsCommandList* cmd;
            unsigned slot;
            bool valid;
            Use(Generation& gen, ID3D12GraphicsCommandList* commands) : g(gen), cmd(commands), slot(g.nextMarker)
            {
                valid = !g.occupied[slot] || g.completed[slot] != 0;
                if (!valid)
                    return;
                g.completed[slot] = 0;
                g.occupied[slot] = true;
                g.lastMarker = slot;
                g.everRecorded = true;
                g.nextMarker = (slot + 1) % MarkerCount;
            }
            ~Use()
            {
                if (!valid)
                    return;
                cmd->EndQuery(g.queries, D3D12_QUERY_TYPE_TIMESTAMP, slot);
                cmd->ResolveQueryData(g.queries, D3D12_QUERY_TYPE_TIMESTAMP, slot, 1, g.readback,
                                      slot * sizeof(UINT64));
            }
        };

        std::unique_ptr<Generation> current;
        std::vector<std::unique_ptr<Generation>> retired;
        std::string status = "not started";
        struct Pending
        {
            ID3D12GraphicsCommandList* cmd = nullptr;
            NVSDK_NGX_Parameter* caller = nullptr;
            ID3D12Resource* output = nullptr;
            float scale = 1;
        } pending;

        void Say(const std::string& text);
        void Cancel();
        void Collect();

        unsigned UInt(NVSDK_NGX_Parameter* p, const char* key, unsigned fallback = 0);
        float Float(NVSDK_NGX_Parameter* p, const char* key, float fallback);

        bool Allocate(Generation& g);

        void Before(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* source, unsigned long long epoch,
                    unsigned long long submittedEpoch, ID3D12CommandQueue* queue, bool interop, bool rayReconstruction);

        void After(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* source, unsigned long long epoch);

        void ReleaseResources();
    };
    DeferredSrContext deferredSr { *this };

    std::string SynchronousDeferredDlssStatus();

    struct LateContext
    {
        State& owner;
        explicit LateContext(State& state) : owner(state) {}

        template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
        struct Slot
        {
            ComPtr<ID3D12Resource> depth, motion, linear, encoded, residual, cleanScene;
            ComPtr<ID3D12Resource> response[2]; // per-slot ping-pong; reused only after its GPU fence
            unsigned responseIndex = 0, responseWidth = 0, responseHeight = 0;
            uint64_t responseEpoch = 0;
            float responseExposure = 1.0f;
            DXGI_COLOR_SPACE_TYPE responseSpace = DXGI_COLOR_SPACE_CUSTOM;
            bool cleanSceneValid = false, responseValid = false;
            ComPtr<ID3D12Fence> fence;
            ComPtr<ID3D12CommandQueue> producerQueue;
            ComPtr<ID3D12CommandAllocator> allocator;
            ComPtr<ID3D12GraphicsCommandList> commands;
            ID3D12CommandList* producer = nullptr; // identity only; never dereferenced
            DlssNrFrameInfo frame {};
            uint64_t ready = 0, done = 0, serial = 0;
            bool pending = false, submitted = false, residualOnly = false, sceneLinear = true;
        };
        std::array<Slot, 4> slots;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> producerQueue;
        Dx11FinishedPictureBridge dx11;
        // One clean presentation snapshot for the owner, never one per rotating backbuffer/slot.
        ComPtr<ID3D12Resource> heldFinished;
        ComPtr<ID3D12Fence> heldFence;
        ComPtr<ID3D12CommandQueue> heldQueue;
        uint64_t heldReady = 0, heldGeneration = 0;
        D3D12_RESOURCE_STATES heldState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        DXGI_COLOR_SPACE_TYPE heldSpace = DXGI_COLOR_SPACE_CUSTOM;
        bool heldValid = false, heldFailed = false;
        bool reportedQueueDelay = false;
        Slot* heldSlot = nullptr;
        uint64_t heldSlotSerial = 0;
        uint64_t serial = 0, successes = 0;
        std::string status = "Waiting for a finished picture.";
        bool reset = true;
        std::atomic<bool> tracking { false };
        static constexpr GUID colorSpaceKey = {
            0x34a31e7b, 0x84c5, 0x44ef, { 0xa7, 0x4d, 0x6b, 0xd3, 0x60, 0x8c, 0xe5, 0x22 }
        };

        void Say(const char* message);

        bool Finished(const Slot& slot);

        void Cancel();

        bool Clone(ComPtr<ID3D12Resource>& copy, ID3D12Resource* source);

        Slot* Acquire(ID3D12GraphicsCommandList* cmd);

        void Arm(Slot& slot, ID3D12GraphicsCommandList* cmd);

        void Capture(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params, bool rr);

        bool CaptureResidual(ID3D12GraphicsCommandList* cmd, ID3D12Resource* clean, ID3D12Resource* residual,
                             float scale, bool sceneLinear, bool reset);
    };
    LateContext late { *this };

    void FinishedPictureResetCommandList(ID3D12CommandList* cmd);

    bool WaitForFinishedPicture();

    std::string FinishedPictureStatus();

    void FinishedPictureSubmitted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists);

    void FinishedPictureColorSpace(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE colorSpace);

    DXGI_COLOR_SPACE_TYPE FinishedColorSpace(IDXGISwapChain* swapchain, DXGI_FORMAT format);

    void ApplyToFinishedPicture(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue);

    void ApplyToFinishedPictureDx11(IDXGISwapChain* swapchain);

    bool ApplyFinishedColor(ID3D12Resource* color, ID3D12CommandQueue* queue, DXGI_COLOR_SPACE_TYPE colorSpace,
                            bool gameFrameHandoff = false);

    DlssNr::Proxy::Settings ModelSettings(const Config& cfg, unsigned int pass);
    bool PrepareRunModels(ID3D12GraphicsCommandList* cmdList, ID3D12Device* device,
                          const DlssNrFrameInfo& frame, const D3D12_RESOURCE_DESC& desc,
                          DlssNr::ColorExtent native, DlssNr::ColorExtent work,
                          float workScale, unsigned int requestedPasses);
    struct EncodeContext
    {
        ID3D12GraphicsCommandList* cmdList;
        ID3D12Device* device;
        ID3D12Resource* target;
        D3D12_RESOURCE_STATES targetState;
        const DlssNrFrameInfo& frame;
        float workScale;
        bool targetSupportsUav;
        float whitePoint = 1.0f, exposurePreMul = 0.0f;
        unsigned int useGameExposure = 0;
        ID3D12Resource* exposureTex = nullptr;
        ID3D12Resource* modelInput = nullptr;
    };
    void EncodeInput(EncodeContext& context);
    DlssNrConstants MakeResolveConstants(const EncodeContext& context, unsigned int effectivePasses);
    void EndGpuTiming(ID3D12GraphicsCommandList* cmdList, ID3D12CommandQueue* timingQueue);

    void Run(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colour, ID3D12Resource* depth, ID3D12Resource* motion,
             ID3D12Resource* output, const DlssNrFrameInfo& frame, ID3D12CommandQueue* timingQueue);

    std::string DeferredDlssStatus();

    void RetryAfterFailure();

    // Adapt the game's NGX parameters into the explicit NR frame contract.
    void EvaluateInternal(ID3D12GraphicsCommandList* cmdList, NVSDK_NGX_Parameter* params, bool beforeUpscale,
                          ID3D12CommandQueue* timingQueue, bool rayReconstruction, unsigned long long submissionEpoch,
                          bool interop);

    // The pass. Resources in, nothing read from anywhere the caller cannot see.

    DlssNr::CalibrationReading Calibration();

    // What the game offers by way of exposure, and what has been read from it. For the menu, so a user
    // can see whether this game supplies one at all without having to read a log.

    void ReleaseResources();

    struct GuideReport
    {
        bool valid;
        bool depthInverted;
        float mvScaleX;
        float mvScaleY;
        unsigned int guideW;
        unsigned int guideH;
        unsigned int frameW;
        unsigned int frameH;
    };
    GuideReport loggedGuides {};
    struct ComposeReport
    {
        bool valid;
        float whitePoint;
        float transfer;
        float colour;
        float maxRatio;
        unsigned int passthrough;
        unsigned int debugView;
        unsigned int compareMode;
        unsigned int residual;
        unsigned int workW;
        unsigned int workH;
        unsigned int passes;
    };
    ComposeReport loggedCompose {};

    struct ExposureReport
    {
        bool valid;
        float pre;
        bool havePre;
        bool haveTexture;
        bool autoFlag;
    };
    ExposureReport logged {};
    std::set<std::string> seen;
    unsigned long long resets = 0;
    bool reportedHdr = false;
    bool reportedHdrValue = false;
    bool reportedBefore = false;
    bool warnedSuper = false;
    unsigned int loggedConfigured = 0;
    unsigned int loggedEffective = 0;
    unsigned int lastSuper = 0;
    unsigned long long lastSplitLog = 0;
    unsigned lastFinishedMode = 0;
    bool reportedPadding = false;
    bool warnedSubrect = false;
    ApiUpscalerInput saidApi = (ApiUpscalerInput) -1;
    float loggedExposure = -1.0f;
    float loggedScan = -1.0f;

    bool modelRunning = false;
    ID3D12Resource* buffer = nullptr;
    D3D12_RESOURCE_STATES bufferState = D3D12_RESOURCE_STATE_COMMON;
    uint32_t featureFlags = 0;
    DlssNr::ControlRequests controls = DlssNr::ReadControlRequests();
    explicit State(DlssNr_Dx12& owner) : shader(owner) {}
    void ConsumeControls();
    void Publish();
    ~State()
    {
        WaitForFinishedPicture();
        ReleaseInputHold();
        ReleaseResources();
        SAFE_RELEASE(buffer);
    }
};
