#pragma once

// AMD driver flip metering ("Driver Frame Pacing") extension for DX12.
// Reverse engineered from amd_fidelityfx_framegeneration_dx12.dll (FFX SDK 2.4), MLFrameGenerationSwapChainDX12.
//
// Usage, as done by the FSR ML frame generation swapchain:
//   1. AmdExtD3DCreateInterface(device, IID_IAmdExtD3DFactory) -> factory
//   2. factory->CreateInterface(realSwapChain, IID_IAmdExtFlipMeter) -> flip meter (one per swapchain)
//   3. GetCaps, then SetConfig with the mode to use. Only enabled while the window is exclusive fullscreen
//      or covers its whole output (independent flip), otherwise SetConfig is called with mode Off.
//      Repeated after ResizeBuffers / fullscreen transitions, and with mode Off before releasing.
//   4. For every IDXGISwapChain::Present (generated and real frames), call Present on the flip meter right
//      before it, passing the back buffer about to be presented. When this succeeds FSR doesn't CPU-sleep
//      until the target time, the driver paces the flip instead. On failure FSR falls back to its own sleep.

#include <Unknwn.h>
#include <cstdint>

struct ID3D12Resource;

typedef HRESULT(__cdecl* PFN_AmdExtD3DCreateInterface)(IUnknown* pOuter, REFIID riid, void** ppvObject);

MIDL_INTERFACE("014937EC-9288-446F-A9AC-D75A8E3A984F")
IAmdExtD3DFactory : public IUnknown
{
  public:
    virtual HRESULT CreateInterface(IUnknown * pOuter, REFIID riid, void** ppvObject) = 0;
};

enum AmdExtFlipMeterMode : uint32_t
{
    AmdExtFlipMeterMode_Off = 0,
    // KMD flip timer, paces every generated frame by AmdExtFlipMeterPresentInfo::framesLeft
    AmdExtFlipMeterMode_FrameIndex = 1,
    // What FSR requests. Goes to the DirectCapture pacer, which paces one generated frame and flips any more right away
    AmdExtFlipMeterMode_Adaptive = 2,
    AmdExtFlipMeterMode_FixedRate = 3, // Needs AmdExtFlipMeterConfig::targetRate > 0
};

// AmdExtFlipMeterCaps::supportedModes bits
enum AmdExtFlipMeterCapsBits : uint32_t
{
    AmdExtFlipMeterCaps_Mode1 = 1u << 0,
    AmdExtFlipMeterCaps_Adaptive = 1u << 1,
    AmdExtFlipMeterCaps_FixedRate = 1u << 2,
    AmdExtFlipMeterCaps_DoNotEnable = 1u << 3, // FSR never turns metering on when set
    AmdExtFlipMeterCaps_Bit5 = 1u << 5,        // FSR copies it to AmdExtFlipMeterConfig_Bit1
    AmdExtFlipMeterCaps_Bit6 = 1u << 6,        // FSR copies it to AmdExtFlipMeterConfig_Bit2
    AmdExtFlipMeterCaps_FlipTimer = 1u << 7,   // FSR copies it to AmdExtFlipMeterConfig_AllowFlipTimer
};

// AmdExtFlipMeterConfig::flags bits
enum AmdExtFlipMeterConfigFlags : uint32_t
{
    AmdExtFlipMeterConfig_AllowFlipTimer = 1u << 0, // Allows the driver's CPU deferred flip timer
    AmdExtFlipMeterConfig_Bit1 = 1u << 1,
    AmdExtFlipMeterConfig_Bit2 = 1u << 2,
    AmdExtFlipMeterConfig_Bit3 = 1u << 3, // FSR sets it from a static global
};

// Values seen in the status fields. FSR initializes every status to NotAvailable before a call.
// After a failed SetConfig on creation FSR keeps the interface only for status 0 or 10.
enum AmdExtFlipMeterStatus : uint32_t
{
    AmdExtFlipMeterStatus_Ok = 0,
    AmdExtFlipMeterStatus_Retry = 10,
    AmdExtFlipMeterStatus_NotAvailable = 11,
};

struct AmdExtFlipMeterCaps
{
    uint32_t supportedModes; // AmdExtFlipMeterCapsBits, bit (mode - 1) = mode supported
    // Registry Gmx_DriverFramePacingMaxGenFrames in the adapter's driver key, default 1. FSR only requires it to be
    // non zero. amdxc64 caches it on the interface, its SetConfig rejects more with E_INVALIDARG and the KMD with
    // status 5
    uint32_t maxGeneratedFrames;
    uint32_t unknown08; // FSR stores it but never reads it
    uint32_t status;    // AmdExtFlipMeterStatus
};
static_assert(sizeof(AmdExtFlipMeterCaps) == 0x10);

struct AmdExtFlipMeterConfig
{
    uint32_t mode;            // AmdExtFlipMeterMode
    uint32_t targetRate;      // Only used by FixedRate
    uint32_t generatedFrames; // Generated frames per real frame, 1..maxGeneratedFrames. FSR always uses 1
    uint32_t flags;           // AmdExtFlipMeterConfigFlags
    uint32_t pacerType;       // Out: 0 = KMD flip timer, 1 = DirectCapture PID pacer
    uint32_t status;          // Out: AmdExtFlipMeterStatus
    uint32_t reserved18[10];
};
static_assert(sizeof(AmdExtFlipMeterConfig) == 0x40);

// AmdExtFlipMeterPresentInfo::flags bits
enum AmdExtFlipMeterPresentFlags : uint32_t
{
    // FSR sets it when the game's frame id didn't increase by exactly 1 since the previous frame
    AmdExtFlipMeterPresent_Discontinuity = 1u << 1,
};

struct AmdExtFlipMeterPresentInfo
{
    // Count of the app's IDXGISwapChain::Present calls, same value for the generated and real frame of it
    uint64_t presentId;

    // QueryPerformanceCounter time this frame should be shown at.
    // FSR uses the QPC taken right after the previous IDXGISwapChain::Present returned + the pacing delay.
    // Its pacing delay is half of the average time between app frames minus a jitter margin.
    uint64_t targetQpc;

    // FSR: pacing delay in ns * number of presents queued for this app frame (2 with FG, 1 without),
    // so roughly the app's frame time in ns
    uint32_t frameTimeNs;

    // Presents of the app frame still to come after this one: generatedFrames for the first generated frame down to 0
    // for the real one, FSR's 1 and 0 at 2X.
    // In FrameIndex mode the KMD holds present k = generatedFrames - framesLeft until the arrival of the app frame's
    // first present + k * frame time / (generatedFrames + 1), the frame time being the median between first presents.
    // It holds one flip at a time, a present that arrives while one is held doesn't get paced.
    uint32_t framesLeft;
    uint32_t flags; // AmdExtFlipMeterPresentFlags
    uint32_t reserved1C;
    uint64_t reserved20[4];
};
static_assert(sizeof(AmdExtFlipMeterPresentInfo) == 0x40);

MIDL_INTERFACE("543820DD-06C9-4F7E-A3D0-CA260D794ADD")
IAmdExtFlipMeter : public IUnknown
{
  public:
    // FSR passes index 0 and sizeof(AmdExtFlipMeterCaps)
    virtual HRESULT GetCaps(uint32_t index, AmdExtFlipMeterCaps* caps, uint32_t capsSize) = 0;

    // Writes back pacerType and status
    virtual HRESULT SetConfig(AmdExtFlipMeterConfig * config) = 0;

    virtual HRESULT GetConfig(AmdExtFlipMeterConfig * config) = 0;

    // Called for every swapchain present while mode != Off.
    // backBuffer is the swapchain's current back buffer (GetBuffer(GetCurrentBackBufferIndex())).
    // status is AmdExtFlipMeterStatus, FSR initializes it to NotAvailable.
    virtual HRESULT Present(ID3D12Resource * backBuffer, const AmdExtFlipMeterPresentInfo* info, uint32_t* status) = 0;
};
