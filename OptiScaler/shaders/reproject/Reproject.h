#pragma once

#include "SysUtils.h"

#include <DirectXMath.h>
#include <atomic>

#include "mouseInputs/InputCollection.h"

struct alignas(16) ReprojectionParams
{
    uint32_t ScreenWidth;
    uint32_t ScreenHeight;
    float InvScreenWidth;
    float InvScreenHeight;

    uint32_t DepthWidth;
    uint32_t DepthHeight;
    float _Pad0[2];

    float UiDiffThreshold;
    float DepthCutoff;
    float DitherWidthPx;
    uint32_t CutoffExpandPx;

    uint32_t EdgeMode;
    uint32_t ShowStaticElements;
    uint32_t InvertedDepth;
    uint32_t FakeFrame; // need to provide fakePresent

    float TanHalfFovX;
    float TanHalfFovY;
    float InvTanHalfFovX;
    float InvTanHalfFovY;

    // Inputs for the latch pass which builds the reprojection rotation on the GPU
    DirectX::XMFLOAT4 CameraRight;   // xyz, normalized
    DirectX::XMFLOAT4 CameraUp;      // xyz, normalized
    DirectX::XMFLOAT4 CameraForward; // xyz, normalized
    DirectX::XMFLOAT4 Calibration;   // yawFromX, yawFromY, pitchFromX, pitchFromY

    float PixelAngle;
    float YawSign;
    float PitchSign;
    uint32_t LateLatch; // 1 = read mouse from the live buffer when the GPU executes, 0 = use MouseDelta

    DirectX::XMINT2 MouseDelta;    // mouse delta since sim start, captured on the CPU
    DirectX::XMINT2 SimStartMouse; // running mouse total at sim start, for late latching

    // Direction cache, see reproject_cache.hlsl
    DirectX::XMFLOAT4 CameraDelta; // xyz = camera position - cache origin, zero when parallax correction is off

    float CameraNear;
    float CameraFar; // 0 = infinite
    uint32_t CacheSize;
    float InvCacheSize;

    uint32_t CacheEnabled;
    uint32_t CacheReset;
    float CacheDecay;       // confidence lost per rendered frame by texels that aren't visible
    float CacheMaxDistance; // limited by the fp16 storage

    // Motion vector statistics, see reproject_stats.hlsl
    DirectX::XMFLOAT4 PrevViewX; // rows of the rotation from the current view to the previous rendered view
    DirectX::XMFLOAT4 PrevViewY;
    DirectX::XMFLOAT4 PrevViewZ;
    DirectX::XMFLOAT4 PrevViewMove; // xyz = camera movement since the previous rendered frame, in its view space
                                    // w = world units -> depth units (1 / parallax scale), 0 when not known

    DirectX::XMFLOAT2 MvToUv; // motion vector -> uv offset to the previous frame
    uint32_t MvWidth;
    uint32_t MvHeight;

    float OrbitPivotDepth; // distance to the pivot of an orbiting camera in depth units, 0 when there is none
    float _Pad1[3];
};

// Written by the CPU on every mouse event, read by the latch pass
struct alignas(16) ReprojectionLiveInput
{
    DirectX::XMINT2 Mouse; // running mouse total, see InputCollection
    uint32_t _Pad0[2];
};

struct FilloutData
{
    float diffThreshold;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t depthWidth;
    uint32_t depthHeight;
    bool invertedDepth;
    bool fakeFrame;

    float cameraVFov;
    float cameraAspectRatio;
    float cameraUp[3];
    float cameraRight[3];
    float cameraForward[3];
    float cameraPrevForward[3];
    float cameraPosition[3];

    // Previous rendered frame, for the motion vector statistics
    bool hasPrevCamera = false;
    float cameraPrevUp[3];
    float cameraPrevRight[3];
    float cameraPrevPosition[3];
    float cameraNear;
    float cameraFar;

    uint64_t frameId; // direction cache is updated once per frameId
    bool reset;

    // Optional, without them there is no parallax scale estimate or 3rd person detection
    bool hasMotionVectors = false;
    float mvScaleX; // motion vector -> pixels of the motion vector texture
    float mvScaleY;
    uint32_t mvWidth;
    uint32_t mvHeight;

    DirectX::XMINT2 mouseDeltaSinceSim; // for reprojection
    DirectX::XMINT2 mouseDeltaSimToSim; // for calibration

    // Late latching, mouseDeltaSinceSim gets replaced by (live mouse - simStartMouse) when the GPU executes
    bool lateLatch = false;
    DirectX::XMINT2 simStartMouse {};
};

struct CalibrationState
{
    float Sxx = 0.0f, Sxy = 0.0f, Syy = 0.0f;
    float BxYaw = 0.0f, ByYaw = 0.0f;
    float BxPitch = 0.0f, ByPitch = 0.0f;
    float Syaw = 0.0f, Spitch = 0.0f;

    float yawFromX = 1.0f, yawFromY = 0.0f;
    float pitchFromX = 0.0f, pitchFromY = 1.0f;
    int sampleCount = 0;

    void Reset() { *this = CalibrationState {}; }

    // Share of the camera rotation the mouse explains, 0 until there are enough samples
    float Fit() const
    {
        const float total = Syaw + Spitch;

        if (sampleCount < 20 || !(total > 0.0f))
            return 0.0f;

        return (yawFromX * BxYaw + yawFromY * ByYaw + pitchFromX * BxPitch + pitchFromY * ByPitch) / total;
    }

    void Update(float mx, float my, float camYawDelta, float camPitchDelta)
    {
        if (mx * mx + my * my < 1e-12f)
            return;

        constexpr float DECAY = 0.99f;

        Sxx = Sxx * DECAY + mx * mx;
        Sxy = Sxy * DECAY + mx * my;
        Syy = Syy * DECAY + my * my;

        BxYaw = BxYaw * DECAY + mx * camYawDelta;
        ByYaw = ByYaw * DECAY + my * camYawDelta;
        BxPitch = BxPitch * DECAY + mx * camPitchDelta;
        ByPitch = ByPitch * DECAY + my * camPitchDelta;
        Syaw = Syaw * DECAY + camYawDelta * camYawDelta;
        Spitch = Spitch * DECAY + camPitchDelta * camPitchDelta;

        sampleCount++;

        const float det = Sxx * Syy - Sxy * Sxy;
        if (std::abs(det) >= 1e-10f && sampleCount >= 20)
        {
            const float invDet = 1.0f / det;
            yawFromX = std::clamp((BxYaw * Syy - ByYaw * Sxy) * invDet, -1.5f, 1.5f);
            yawFromY = std::clamp((ByYaw * Sxx - BxYaw * Sxy) * invDet, -1.5f, 1.5f);
            pitchFromX = std::clamp((BxPitch * Syy - ByPitch * Sxy) * invDet, -1.5f, 1.5f);
            pitchFromY = std::clamp((ByPitch * Sxx - BxPitch * Sxy) * invDet, -1.5f, 1.5f);
        }
    }
};

// The API independent part of the reprojection: the shader constants, the mouse calibration, the motion vector
// statistics with the 3rd person detection and the bookkeeping of the direction cache.
// Reproject_Dx12 and Reproject_Vk own the GPU resources and record the passes.
class Reproject
{
  protected:
    // Motion vector statistics, once per rendered frame, see reproject_stats.hlsl
    // The results are read back StatsLatency rendered frames later, by then the GPU is long done with them
    static constexpr uint32_t StatsBins = 128;
    static constexpr uint32_t StatsCounters = 12;
    static constexpr uint32_t StatsCount = StatsBins + StatsCounters;
    static constexpr float StatsLogScaleMin = -3.0f;
    static constexpr float StatsLogScaleMax = 6.0f;
    static constexpr uint32_t StatsLatency = 8;

    // Persistently mapped readback of each slot, set up by the API side
    const uint32_t* _statsMapped[StatsLatency] = {};

    bool _cacheValid = false;

    // What the next dispatch records
    struct DispatchPlan
    {
        bool cacheUpdate = false;
        uint32_t prevCache = 0;   // read by the cache update
        uint32_t newestCache = 0; // written by the cache update, read by the reprojection
        bool statsUpdate = false;
        uint32_t statsSlot = 0; // readback slot the stats are copied to
    };

    // cacheReady: the cache resources of params.CacheSize exist
    // Adjusts params to what can be done this time
    DispatchPlan PlanDispatch(ReprojectionParams& params, bool cacheReady, bool hasVelocity);

    // After the passes of the plan were recorded
    void PlanRecorded(const DispatchPlan& plan);

  private:
    uint32_t _statsSlot = 0;
    bool _statsWritten[StatsLatency] = {};
    uint64_t _statsFrameId = UINT64_MAX;
    bool _statsPending = false; // set by FilloutStruct, applied by the dispatch
    uint32_t _framesWithoutMvs = 0;

    // What the camera was doing when the stats in a slot were taken
    struct OrbitSample
    {
        bool rotating = false;       // enough rotation to tell whether the camera orbits
        bool orbiting = false;       // camera rigidly orbits a pivot, OrbitPivotDepth was set
        bool still = false;          // camera rotates in place, nothing to orbit around
        float fit = 0.0f;            // share of the camera movement the orbit explains, for logging
        DirectX::XMFLOAT3 offset {}; // camera position relative to the pivot in (right, up, forward), for logging
    };

    OrbitSample _pendingOrbit;
    float _pendingOrbitPivotDepth = 0.0f;
    OrbitSample _statsOrbit[StatsLatency];

    // Some games' camera right or up point the other way on the screen, they get turned around for everything
    struct CameraFlip
    {
        bool right = false;
        bool up = false;
        // The camera position goes the other way than the axes say, the rotation alone can't tell
        // right and up both turned around from forward turned around, only the parallax of the movement can
        bool move = false;

        bool operator==(const CameraFlip&) const = default;
    };

    CameraFlip _cameraFlip;
    CameraFlip _statsFlip[StatsLatency]; // _cameraFlip when the stats in a slot were taken

    // Orbit camera fit, the camera position moving with the rotation as if attached to a pivot by a fixed offset
    // Least squares over the last frames: camera movement = (rotation change) * offset
    float _orbitAtA[3][3] = {};
    float _orbitAtb[3] = {};
    float _orbitBtb = 0.0f;

    float _parallaxHistogram[StatsBins] = {};
    std::atomic<float> _parallaxEstimate = 0.0f; // 0 = not known yet
    uint32_t _parallaxFrames = 0;                // frames in the histogram since it was last cleared

    float _mvTrust = 0.0f;     // how well the motion vectors follow the camera rotation, 0..1
    float _mvOppositeX = 0.0f; // how often they move sideways the other way than the camera rotation, 0..1
    float _mvOppositeY = 0.0f;
    float _moveOpposite = 0.0f; // how often the parallax goes the other way than the camera movement, 0..1
    std::atomic<float> _thirdPersonScore = 0.0f; // something in the middle stays in place while the scene moves
    std::atomic<float> _orbitScore = 0.0f;       // the camera orbits something in the middle
    bool _lockedDetected = false;
    bool _orbitDetected = false;
    std::atomic<bool> _thirdPerson = false;
    std::atomic<bool> _hasStats = false;
    bool _bypass = false; // reprojection paused, the frame is passed through

    // Direction cache, ping-ponged cubemaps
    uint32_t _cacheIndex = 0; // the one with the newest data
    DirectX::XMFLOAT3 _cacheOrigin {};
    uint64_t _cacheFrameId = UINT64_MAX;
    float _cacheMoveAverage = 0.0f; // camera movement per frame, for detecting teleports

    // Set by FilloutStruct, applied by the dispatch
    bool _cacheUpdatePending = false;
    DirectX::XMFLOAT3 _pendingCacheOrigin {};
    uint64_t _pendingCacheFrameId = 0;

    // Some games build the camera from the mouse input of an earlier simulation than the frame's own
    // There is a calibration for each lag, the one that fits best is used
    static constexpr uint32_t InputLags = 3;
    CalibrationState _calibration[InputLags];
    uint32_t _inputLag = 0;
    DirectX::XMINT2 _simDeltas[InputLags] {}; // mouse movement between the last simulation starts, newest first
    uint64_t _calibrationFrameId = UINT64_MAX;
    bool _isFirstFrame = true;

    void ProcessStats(const uint32_t* stats, const OrbitSample& orbit, const CameraFlip& flip);
    void UpdateOrbitFit(const FilloutData& data, float parallaxScale);
    void FilloutStats(const FilloutData& data, ReprojectionParams& params);

  public:
    void FilloutStruct(FilloutData data, ReprojectionParams& params);

    // Depth to camera position units, estimated from the motion vectors, 0 when not known
    float ParallaxScaleEstimate() const { return _parallaxEstimate; }

    // The one in use, estimated or set by the user, 0 when not known
    float ParallaxScale() const;

    // Something in the middle of the screen follows the camera, usually a 3rd person character
    bool ThirdPersonDetected() const { return _thirdPerson; }
    float ThirdPersonScore() const { return _thirdPersonScore; }
    float OrbitScore() const { return _orbitScore; }

    // Motion vectors have been provided and look usable
    bool HasMotionStats() const { return _hasStats; }

    // Reprojection paused by the 3rd person detection, valid after FilloutStruct
    bool IsBypassed() const { return _bypass; }

    virtual ~Reproject() = default;
};
