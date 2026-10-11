#include "pch.h"
#include <Config.h>

#include "Reproject_Dx12.h"
#include "Reproject_Common.h"
#include "precompile/reproject_Shader.h"
#include "precompile/reproject_mask_h_Shader.h"
#include "precompile/reproject_mask_v_Shader.h"
#include "precompile/reproject_latch_Shader.h"
#include "precompile/reproject_cache_Shader.h"
#include "precompile/reproject_stats_Shader.h"

#include <numbers>
#include <magic_enum.hpp>
#include "mouseInputs/RawInputHook.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

inline static int GetFormatGroup(DXGI_FORMAT format)
{
    switch (format)
    {

    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 1;

    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return 2;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
        return 3;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
        return 4;

    case DXGI_FORMAT_R11G11B10_FLOAT:
        return 5;

    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
        return 6;

    case DXGI_FORMAT_B5G6R5_UNORM:
        return 7;

    case DXGI_FORMAT_B5G5R5A1_UNORM:
        return 8;

    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return 9;

    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
        return 10;

    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return 11;

    default:
        return -1;
    }
}

inline static bool CompareResourceFormats(DXGI_FORMAT sc, DXGI_FORMAT hudless)
{
    if (sc == hudless)
        return true;

    auto scGroup = GetFormatGroup(sc);
    auto hudlessGroup = GetFormatGroup(hudless);
    return scGroup == hudlessGroup;
}

void Reproject_Dx12::FilloutStruct(const FilloutData& data, ReprojectionParams& params)
{
    params.ScreenWidth = data.screenWidth;
    params.ScreenHeight = data.screenHeight;
    params.InvScreenWidth = 1.0f / data.screenWidth;
    params.InvScreenHeight = 1.0f / data.screenHeight;

    params.DepthWidth = data.depthWidth;
    params.DepthHeight = data.depthHeight;

    params.UiDiffThreshold = data.diffThreshold;
    params.DepthCutoff = Config::Instance()->ReprojectionDepthCutoff.value_or_default();
    params.DitherWidthPx = data.screenHeight / 16.0f; // TODO: configurable
    params.CutoffExpandPx = Config::Instance()->ReprojectionCutoffExpand.value_or_default();

    params.InvertedDepth = data.invertedDepth;
    params.ShowStaticElements = State::Instance().fgHudlessCompare;
    params.FakeFrame = data.fakeFrame;

    auto fillMode = Config::Instance()->ReprojectionFillMode.value_or_default();
    if (fillMode == ReprojectionFill::Debug)
        params.EdgeMode = 0;
    else if (fillMode == ReprojectionFill::StrechEdge)
        params.EdgeMode = 1;
    else if (fillMode == ReprojectionFill::Dithering)
        params.EdgeMode = 2;
    else if (fillMode == ReprojectionFill::Noise)
        params.EdgeMode = 3;
    else if (fillMode == ReprojectionFill::SoftEdge)
        params.EdgeMode = 4;

    const float tanHalfFovY = std::tan(data.cameraVFov * 0.5f);
    const float pixelAngle = 2.0f * std::atan(tanHalfFovY / data.screenHeight);

    params.TanHalfFovY = tanHalfFovY;
    params.TanHalfFovX = tanHalfFovY * data.cameraAspectRatio;
    params.InvTanHalfFovY = 1.0f / tanHalfFovY;
    params.InvTanHalfFovX = 1.0f / params.TanHalfFovX;

    // Compute actual camera rotation deltas between frames
    if (!_isFirstFrame && !data.fakeFrame)
    {
        XMVECTOR prevFwd = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(data.cameraPrevForward)));
        XMVECTOR currFwd = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(data.cameraForward)));

        XMFLOAT3 p, c;
        XMStoreFloat3(&p, prevFwd);
        XMStoreFloat3(&c, currFwd);

        // Yaw delta (wrapped to [-pi, pi])
        float camYawDelta = std::atan2(c.y, c.x) - std::atan2(p.y, p.x);

        if (camYawDelta > std::numbers::pi_v<float>)
            camYawDelta -= 2.0f * std::numbers::pi_v<float>;

        if (camYawDelta < -std::numbers::pi_v<float>)
            camYawDelta += 2.0f * std::numbers::pi_v<float>;

        // Pitch delta
        const float camPitchDelta = std::asin(std::clamp(c.z, -1.0f, 1.0f)) - std::asin(std::clamp(p.z, -1.0f, 1.0f));

        const float mouseX = data.mouseDeltaSimToSim.x * pixelAngle;
        const float mouseY = data.mouseDeltaSimToSim.y * pixelAngle;

        _calibration.Update(mouseX, mouseY, camYawDelta, camPitchDelta);
    }
    else
    {
        _isFirstFrame = false;
    }

    // Compute camera basis
    XMVECTOR camRight = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(data.cameraRight)));
    XMVECTOR camUp = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(data.cameraUp)));
    XMVECTOR camForward = XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(data.cameraForward)));

    constexpr float TEST_ANGLE = 0.001f;

    // Find which matrix rotation direction corresponds to positive camera motion.
    XMVECTOR testPitchForward =
        XMVector3Normalize(XMVector3TransformNormal(camForward, XMMatrixRotationAxis(camRight, TEST_ANGLE)));

    float pitchTest = std::asin(std::clamp(XMVectorGetZ(testPitchForward), -1.0f, 1.0f)) -
                      std::asin(std::clamp(XMVectorGetZ(camForward), -1.0f, 1.0f));

    int pitchSign = (pitchTest >= 0.0f) ? 1 : -1;

    XMVECTOR testYawForward = XMVector3Normalize(
        XMVector3TransformNormal(camForward, XMMatrixRotationAxis(XMVectorSet(0, 0, 1, 0), TEST_ANGLE)));

    float yawTest = std::atan2(XMVectorGetY(testYawForward), XMVectorGetX(testYawForward)) -
                    std::atan2(XMVectorGetY(camForward), XMVectorGetX(camForward));

    if (yawTest > std::numbers::pi_v<float>)
        yawTest -= 2.0f * std::numbers::pi_v<float>;

    if (yawTest < -std::numbers::pi_v<float>)
        yawTest += 2.0f * std::numbers::pi_v<float>;

    int yawSign = (std::abs(yawTest) >= 1e-8f && yawTest >= 0.0f) ? 1 : -1;

    // The rotation itself is built by the latch pass on the GPU, see reproject_latch.hlsl
    XMStoreFloat4(&params.CameraRight, camRight);
    XMStoreFloat4(&params.CameraUp, camUp);
    XMStoreFloat4(&params.CameraForward, camForward);
    params.Calibration = { _calibration.yawFromX, _calibration.yawFromY, _calibration.pitchFromX,
                           _calibration.pitchFromY };

    params.PixelAngle = pixelAngle;
    params.YawSign = (float) yawSign;
    params.PitchSign = (float) pitchSign;

    FilloutStats(data, params);

    // Without any mouse delta the latch pass builds an identity rotation, so the frame is passed through as it is
    _bypass = _thirdPerson && Config::Instance()->ReprojectionThirdPersonDetection.value_or_default();

    params.LateLatch = (data.lateLatch && !_bypass) ? 1 : 0;
    params.MouseDelta = _bypass ? XMINT2 { 0, 0 } : data.mouseDeltaSinceSim;
    params.SimStartMouse = data.simStartMouse;

    // Direction cache, starts over after a bypass as it would only hold the 3rd person character
    params.CacheEnabled = (Config::Instance()->ReprojectionDirectionCache.value_or_default() && !_bypass) ? 1 : 0;

    // About half of the screen pixel density at usual FOVs, not tied to the FOV so that zooming doesn't reset it
    params.CacheSize = std::clamp((static_cast<uint32_t>(data.screenHeight * 0.7f) + 15u) & ~15u, 256u, 1024u);
    params.InvCacheSize = 1.0f / params.CacheSize;
    params.CacheDecay = 1.0f / 60.0f; // TODO: configurable
    params.CacheMaxDistance = 60000.0f;

    // Some games swap near and far with inverted depth
    const float camNear = std::min(data.cameraNear, data.cameraFar);
    const float camFar = std::max(data.cameraNear, data.cameraFar);
    const bool parallaxValid = std::isfinite(camNear) && camNear > 0.0f;

    params.CameraNear = parallaxValid ? camNear : 0.0f;
    params.CameraFar = (parallaxValid && std::isfinite(camFar) && camFar < 1e30f) ? camFar : 0.0f;

    _cacheUpdatePending = params.CacheEnabled && !data.fakeFrame && data.frameId != _cacheFrameId;

    if (!params.CacheEnabled)
        _cacheValid = false;

    if (_cacheUpdatePending)
    {
        _pendingCacheOrigin = { data.cameraPosition[0], data.cameraPosition[1], data.cameraPosition[2] };
        _pendingCacheFrameId = data.frameId;

        XMFLOAT3 move = { _pendingCacheOrigin.x - _cacheOrigin.x, _pendingCacheOrigin.y - _cacheOrigin.y,
                          _pendingCacheOrigin.z - _cacheOrigin.z };
        const float moveLength = XMVectorGetX(XMVector3Length(XMLoadFloat3(&move)));

        // Camera cut or teleport, much further than the camera usually moves in a frame
        // Only frames with movement count, so walking after standing still doesn't trigger it
        bool teleport = _cacheValid && _cacheMoveAverage > 0.0f && moveLength > 500.0f * _cacheMoveAverage;

        if (teleport)
            LOG_DEBUG("Cache reset, camera moved {} with an average of {}", moveLength, _cacheMoveAverage);
        else if (moveLength > 0.0f)
            _cacheMoveAverage = _cacheMoveAverage > 0.0f ? std::lerp(_cacheMoveAverage, moveLength, 0.1f) : moveLength;

        // Depth gives distances in units of the near plane, which don't always match the camera position units
        // (UE reports near as 0.01 with positions in cm), dividing the delta is the same as scaling every distance
        // Without a usable depth or scale the cache still works, just without moving the old data with the camera
        const float parallaxScale = ParallaxScale();

        if (parallaxValid && parallaxScale > 0.0f)
            params.CameraDelta = { move.x / parallaxScale, move.y / parallaxScale, move.z / parallaxScale, 0.0f };

        params.CacheReset = (data.reset || !_cacheValid || teleport) ? 1 : 0;

        LOG_TRACE("Cache update frame: {}, reset: {} (fg reset: {}), delta: {:.3f} {:.3f} {:.3f}, near: {}, far: {}",
                  data.frameId, params.CacheReset, data.reset, params.CameraDelta.x, params.CameraDelta.y,
                  params.CameraDelta.z, data.cameraNear, data.cameraFar);
    }
}

void Reproject_Dx12::FilloutStats(const FilloutData& data, ReprojectionParams& params)
{
    // Written StatsLatency stats dispatches ago, the next one reuses the slot
    if (_statsWritten[_statsSlot])
    {
        ProcessStats(_statsMapped[_statsSlot], _statsOrbit[_statsSlot]);
        _statsWritten[_statsSlot] = false;
    }

    _statsPending = false;

    if (data.fakeFrame || data.frameId == _statsFrameId)
        return;

    _statsFrameId = data.frameId;

    if (!data.hasMotionVectors || data.mvWidth == 0 || data.mvHeight == 0)
    {
        // Don't keep acting on old results when the motion vectors are gone
        if (++_framesWithoutMvs > 60)
        {
            _hasStats = false;
            _thirdPerson = false;
            _lockedDetected = false;
            _orbitDetected = false;
            _thirdPersonScore = 0.0f;
            _orbitScore = 0.0f;
        }

        return;
    }

    _framesWithoutMvs = 0;

    if (!data.hasPrevCamera || data.reset)
        return;

    auto load = [](const float* v) { return XMVector3Normalize(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(v))); };

    const XMVECTOR right = load(data.cameraRight);
    const XMVECTOR up = load(data.cameraUp);
    const XMVECTOR forward = load(data.cameraForward);

    const XMVECTOR prevAxes[3] = { load(data.cameraPrevRight), load(data.cameraPrevUp), load(data.cameraPrevForward) };
    XMFLOAT4* rows[3] = { &params.PrevViewX, &params.PrevViewY, &params.PrevViewZ };

    const XMVECTOR move = XMVectorSubtract(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(data.cameraPosition)),
                                           XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(data.cameraPrevPosition)));
    float moveInView[3];

    for (int i = 0; i < 3; i++)
    {
        *rows[i] = { XMVectorGetX(XMVector3Dot(prevAxes[i], right)), XMVectorGetX(XMVector3Dot(prevAxes[i], up)),
                     XMVectorGetX(XMVector3Dot(prevAxes[i], forward)), 0.0f };

        moveInView[i] = XMVectorGetX(XMVector3Dot(prevAxes[i], move));
    }

    const bool moveValid = std::isfinite(moveInView[0]) && std::isfinite(moveInView[1]) && std::isfinite(moveInView[2]);
    const float parallaxScale = ParallaxScale();
    const float worldToDepth = (moveValid && parallaxScale > 0.0f) ? 1.0f / parallaxScale : 0.0f;

    params.PrevViewMove =
        moveValid ? XMFLOAT4 { moveInView[0], moveInView[1], moveInView[2], worldToDepth } : XMFLOAT4 {};

    params.MvToUv = { data.mvScaleX / data.mvWidth, data.mvScaleY / data.mvHeight };
    params.MvWidth = data.mvWidth;
    params.MvHeight = data.mvHeight;

    UpdateOrbitFit(data, parallaxScale);
    params.OrbitPivotDepth = _pendingOrbitPivotDepth;

    _statsPending = true;
}

void Reproject_Dx12::UpdateOrbitFit(const FilloutData& data, float parallaxScale)
{
    auto load = [](const float* v) { return XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(v)); };
    auto axis = [&](const float* v) { return XMVector3Normalize(load(v)); };

    // How the camera axes changed, the camera offset from the pivot is in (right, up, forward)
    const XMVECTOR columns[3] = { XMVectorSubtract(axis(data.cameraRight), axis(data.cameraPrevRight)),
                                  XMVectorSubtract(axis(data.cameraUp), axis(data.cameraPrevUp)),
                                  XMVectorSubtract(axis(data.cameraForward), axis(data.cameraPrevForward)) };

    const XMVECTOR move = XMVectorSubtract(load(data.cameraPosition), load(data.cameraPrevPosition));

    auto dot = [](XMVECTOR a, XMVECTOR b) { return XMVectorGetX(XMVector3Dot(a, b)); };

    // About the last 20 rendered frames
    constexpr float decay = 0.95f;
    constexpr float window = 1.0f / (1.0f - decay);

    _pendingOrbit = {};
    _pendingOrbitPivotDepth = 0.0f;

    // A jump far beyond the recent movement (respawn, cutscene) would dominate the fit long after, start over
    const float moveSq = dot(move, move);
    if (_orbitBtb > 0.0f && moveSq > 100.0f * _orbitBtb / window)
    {
        std::memset(_orbitAtA, 0, sizeof(_orbitAtA));
        std::memset(_orbitAtb, 0, sizeof(_orbitAtb));
        _orbitBtb = 0.0f;
        return;
    }

    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
            _orbitAtA[i][j] = _orbitAtA[i][j] * decay + dot(columns[i], columns[j]);

        _orbitAtb[i] = _orbitAtb[i] * decay + dot(columns[i], move);
    }

    _orbitBtb = _orbitBtb * decay + moveSq;

    // Missing camera data would spoil the fit for the whole window
    bool finite = std::isfinite(_orbitBtb);
    for (int i = 0; i < 3; i++)
        finite = finite && std::isfinite(_orbitAtb[i]) && std::isfinite(_orbitAtA[i][i]);

    if (!finite)
    {
        std::memset(_orbitAtA, 0, sizeof(_orbitAtA));
        std::memset(_orbitAtb, 0, sizeof(_orbitAtb));
        _orbitBtb = 0.0f;
        return;
    }

    // Squared change of the axes, a couple of degrees of rotation over the window
    const float rotation = _orbitAtA[0][0] + _orbitAtA[1][1] + _orbitAtA[2][2];
    if (!(rotation > 1e-3f) || parallaxScale <= 0.0f)
        return;

    _pendingOrbit.rotating = true;

    // Rotating around one axis leaves it unchanged, which makes the system singular, the ridge keeps it solvable
    const float ridge = 1e-3f * rotation;
    const XMMATRIX ata(_orbitAtA[0][0] + ridge, _orbitAtA[0][1], _orbitAtA[0][2], 0.0f, _orbitAtA[1][0],
                       _orbitAtA[1][1] + ridge, _orbitAtA[1][2], 0.0f, _orbitAtA[2][0], _orbitAtA[2][1],
                       _orbitAtA[2][2] + ridge, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);

    XMVECTOR determinant;
    const XMMATRIX inverse = XMMatrixInverse(&determinant, ata);

    // Symmetric, so the row vector transform is the same as the column one
    const XMVECTOR atb = XMVectorSet(_orbitAtb[0], _orbitAtb[1], _orbitAtb[2], 0.0f);
    const XMVECTOR offset = XMVector3TransformNormal(atb, inverse);

    const float offsetLength = XMVectorGetX(XMVector3Length(offset));
    const float fit = _orbitBtb > 0.0f ? dot(offset, atb) / _orbitBtb : 0.0f;
    _pendingOrbit.fit = fit;
    XMStoreFloat3(&_pendingOrbit.offset, offset);

    // Barely moving compared to the near plane distance, a 1st person camera turning in place
    // Some games swap near and far with inverted depth
    float cameraNear = std::min(data.cameraNear, data.cameraFar);
    if (!std::isfinite(cameraNear) || cameraNear <= 0.0f)
        cameraNear = 1e-4f;

    if (std::sqrt(_orbitBtb) / parallaxScale < cameraNear)
    {
        _pendingOrbit.still = true;
        return;
    }

    // Movement driven by the player instead of the rotation doesn't follow it frame by frame
    // Walking forward while turning steadily is a rigid circle too, but around a point to the side,
    // while a 3rd person camera sits behind what it orbits
    const bool behind = _pendingOrbit.offset.z < -0.7f * offsetLength;

    if (fit >= 0.9f && behind && std::isfinite(offsetLength))
    {
        _pendingOrbit.orbiting = true;
        _pendingOrbitPivotDepth = offsetLength / parallaxScale;
    }
}

float Reproject_Dx12::ParallaxScale() const
{
    if (Config::Instance()->ReprojectionParallaxAuto.value_or_default())
        return _parallaxEstimate;

    return Config::Instance()->ReprojectionParallaxScale.value_or_default();
}

void Reproject_Dx12::ProcessStats(const uint32_t* stats, const OrbitSample& orbit)
{
    // Same order as the counters in reproject_stats.hlsl
    const uint32_t* counters = stats + StatsBins;
    const uint32_t centerSamples = counters[0];
    const uint32_t centerValid = counters[1];
    const uint32_t centerLocked = counters[2];
    const uint32_t outerValid = counters[3];
    const uint32_t outerConsistent = counters[4];
    const uint32_t centerAtPivot = counters[5];

    // Only frames with enough camera motion say anything
    if (outerValid >= 64)
        _mvTrust = std::lerp(_mvTrust, (float) outerConsistent / outerValid, 0.1f);

    // Motion vectors that don't follow the camera (wrong scale, no camera motion in them) would make
    // the whole screen look like it follows the camera and give random parallax scales
    if (_mvTrust < 0.35f)
    {
        _hasStats = false;
        _thirdPerson = false;
        _lockedDetected = false;
        _orbitDetected = false;
        return;
    }

    _hasStats = true;

    // Parallax scale, the most common one over the last few seconds
    float total = 0.0f;
    for (uint32_t i = 0; i < StatsBins; i++)
    {
        _parallaxHistogram[i] = _parallaxHistogram[i] * 0.98f + stats[i];
        total += _parallaxHistogram[i];
    }

    constexpr int window = 2;
    constexpr int lastBin = (int) StatsBins - 1;
    int peak = -1;
    float peakMass = 0.0f;

    for (int i = 0; i <= lastBin; i++)
    {
        float mass = 0.0f;
        for (int j = std::max(i - window, 0); j <= std::min(i + window, lastBin); j++)
            mass += _parallaxHistogram[j];

        if (mass > peakMass)
        {
            peakMass = mass;
            peak = i;
        }
    }

    // Moving objects spread their votes all over, the static scene agrees on one value
    if (peak >= 0 && total >= 200.0f && peakMass >= 0.3f * total)
    {
        float weighted = 0.0f;
        for (int j = std::max(peak - window, 0); j <= std::min(peak + window, lastBin); j++)
            weighted += _parallaxHistogram[j] * j;

        const float bin = weighted / peakMass + 0.5f;
        const float logScale = StatsLogScaleMin + bin * (StatsLogScaleMax - StatsLogScaleMin) / StatsBins;
        const float estimate = std::pow(10.0f, logScale);

        if (_parallaxEstimate == 0.0f)
            LOG_INFO("Parallax scale estimate: {}", estimate);

        _parallaxEstimate = estimate;
    }

    // 3rd person, two ways, each with hysteresis so that a moment of something following the camera doesn't toggle it

    // Something in the middle stays in place while the camera moves the scene past it, mostly when walking around
    // Samples the camera barely moves say nothing, so the score only goes down when nearly the whole middle
    // of the screen has been checked
    if (centerValid >= 32)
    {
        const float locked = (float) centerLocked / centerValid;
        const bool fullView = centerValid >= 0.85f * centerSamples;

        float score = _thirdPersonScore;
        if (locked > score || fullView)
            score = std::lerp(score, locked, 0.08f);

        _thirdPersonScore = score;
        // With the camera movement accounted for, a 1st person view has next to nothing locked,
        // while a car seen from behind covers only part of the middle and not on every frame
        _lockedDetected = _lockedDetected ? score > 0.015f : score > 0.04f;
    }

    // The camera orbits a pivot with something there in the middle of the screen, mostly when looking around
    // The motion vectors can't tell this apart from strafing around an object while aiming at it,
    // but that movement comes from the player and doesn't follow the rotation frame by frame
    if (orbit.rotating && centerSamples >= 32)
    {
        float target = 0.0f;
        float rate = 0.05f;

        if (orbit.orbiting)
            target = centerAtPivot >= 0.15f * centerSamples ? 1.0f : 0.0f;
        else if (!orbit.still)
            rate = 0.002f; // moving and rotating at the same time (driving through a turn), says little either way

        const float score = std::lerp(_orbitScore.load(), target, rate);

        _orbitScore = score;
        _orbitDetected = _orbitDetected ? score > 0.3f : score > 0.6f;
    }

    const bool detected = _lockedDetected || _orbitDetected;

    if (detected != _thirdPerson)
    {
        LOG_INFO("3rd person {}, score: {:.3f}, orbit score: {:.3f}", detected ? "detected" : "no longer detected",
                 _thirdPersonScore.load(), _orbitScore.load());
    }

    _thirdPerson = detected;

    LOG_TRACE("Stats center: {}/{}/{}, at pivot: {}, outer: {}/{}, mv trust: {:.2f}, orbit: {}{}{} fit {:.2f} "
              "offset {:.2f} {:.2f} {:.2f}, "
              "3rd person score: {:.3f}, orbit score: {:.3f}, parallax votes: {:.0f}, scale: {}",
              centerLocked, centerValid, centerSamples, centerAtPivot, outerConsistent, outerValid, _mvTrust,
              orbit.rotating ? "R" : "-", orbit.orbiting ? "O" : "-", orbit.still ? "S" : "-", orbit.fit,
              orbit.offset.x, orbit.offset.y, orbit.offset.z, _thirdPersonScore.load(), _orbitScore.load(), total,
              _parallaxEstimate.load());
}

bool Reproject_Dx12::CreateStatsResources()
{
    auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto outputDesc =
        CD3DX12_RESOURCE_DESC::Buffer(StatsCount * sizeof(uint32_t), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto result =
        _device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &outputDesc,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&_statsOutput));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource (stats) error {1:x}", _name, (unsigned int) result);
        return false;
    }

    _statsOutput->SetName(L"Reproject_Stats");

    auto readbackHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    auto readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(StatsCount * sizeof(uint32_t));

    for (uint32_t i = 0; i < StatsLatency; i++)
    {
        result =
            _device->CreateCommittedResource(&readbackHeapProps, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_statsReadback[i]));

        if (result != S_OK)
        {
            LOG_ERROR("[{0}] CreateCommittedResource (stats readback) error {1:x}", _name, (unsigned int) result);
            return false;
        }

        _statsReadback[i]->SetName(L"Reproject_StatsReadback");

        // Readback heaps can stay mapped, a slot is only read once the GPU is done with it
        void* mapped = nullptr;
        CD3DX12_RANGE readRange(0, StatsCount * sizeof(uint32_t));
        if (_statsReadback[i]->Map(0, &readRange, &mapped) != S_OK)
        {
            LOG_ERROR("[{0}] Failed to map stats readback", _name);
            return false;
        }

        _statsMapped[i] = static_cast<const uint32_t*>(mapped);
    }

    return true;
}

bool Reproject_Dx12::CreateCacheResources(uint32_t size, DXGI_FORMAT colorFormat)
{
    if (_cacheColor[0] != nullptr)
    {
        auto desc = _cacheColor[0]->GetDesc();

        if (desc.Width == size && desc.Format == colorFormat)
            return true;

        LOG_WARN("[{0}] Release cache {1} ({2}), new one: {3} ({4})", _name, desc.Width, (uint32_t) desc.Format, size,
                 (uint32_t) colorFormat);
        ReleaseCacheResources();
    }

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    for (int i = 0; i < 2; i++)
    {
        auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D(colorFormat, size, size, 6, 1, 1, 0,
                                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto geoDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16_FLOAT, size, size, 6, 1, 1, 0,
                                                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        auto result = _device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &colorDesc,
                                                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                       IID_PPV_ARGS(&_cacheColor[i]));

        if (result == S_OK)
        {
            result = _device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &geoDesc,
                                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                      IID_PPV_ARGS(&_cacheGeo[i]));
        }

        if (result != S_OK)
        {
            LOG_ERROR("[{0}] CreateCommittedResource (cache) error {1:x}", _name, (unsigned int) result);
            ReleaseCacheResources();
            return false;
        }

        _cacheColor[i]->SetName(L"Reproject_CacheColor");
        _cacheGeo[i]->SetName(L"Reproject_CacheGeo");
    }

    // Contents are undefined until the first update
    _cacheValid = false;
    return true;
}

void Reproject_Dx12::ReleaseCacheResources()
{
    for (int i = 0; i < 2; i++)
    {
        SAFE_RELEASE(_cacheColor[i]);
        SAFE_RELEASE(_cacheGeo[i]);
    }

    _cacheValid = false;
}

void Reproject_Dx12::ResourceBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
                                     D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    if (beforeState == afterState)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
}

bool Reproject_Dx12::CreateMaskResource(ID3D12Resource** resource, uint32_t width, uint32_t height, const wchar_t* name)
{
    if (*resource != nullptr)
    {
        auto desc = (*resource)->GetDesc();

        if (desc.Width == width && desc.Height == height)
            return true;

        LOG_WARN("[{0}] Release {1}x{2}, new one: {3}x{4}", _name, desc.Width, desc.Height, width, height);
        (*resource)->Release();
        (*resource) = nullptr;
    }

    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8_UNORM, width, height, 1, 1, 1, 0,
                                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto result =
        _device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         nullptr, IID_PPV_ARGS(resource));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) result);
        return false;
    }

    (*resource)->SetName(name);
    return true;
}

Reproject_Dx12::Reproject_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    CD3DX12_STATIC_SAMPLER_DESC samplers[2] = { CD3DX12_STATIC_SAMPLER_DESC(0), CD3DX12_STATIC_SAMPLER_DESC(1) };
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

    for (auto& sampler : samplers)
    {
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    }

    // SRV: Hudless, PresentCopy, Depth, FakePresent, DepthMask, DepthMaskTemp,
    //      PrevCacheColor, PrevCacheGeo, CacheColor, CacheGeo, Velocity
    // UAV: Present, DepthMaskTemp, DepthMask, Latched, CacheColorOut, CacheGeoOut, Stats
    // CBV: Params, LiveInput
    if (!SetupRootSignature(InDevice, 11, 7, 2, 0, 0, 2, samplers))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ReprojectionParams));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    for (int i = 0; i < Reproject_NUM_OF_HEAPS; i++)
    {
        auto result = InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(&_constantBuffers[i]));

        if (result != S_OK)
        {
            LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) result);
            return;
        }
    }

    if (!CreateComputePipeline(InDevice, &_pipelineState, reproject_cso, sizeof(reproject_cso), shaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_maskPipelineH, reproject_mask_h_cso, sizeof(reproject_mask_h_cso),
                               maskHShaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create mask H compute pipeline", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_maskPipelineV, reproject_mask_v_cso, sizeof(reproject_mask_v_cso),
                               maskVShaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create mask V compute pipeline", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_latchPipeline, reproject_latch_cso, sizeof(reproject_latch_cso),
                               latchShaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create latch compute pipeline", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_cachePipeline, reproject_cache_cso, sizeof(reproject_cache_cso),
                               cacheShaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create cache compute pipeline", _name);
        return;
    }

    if (!CreateComputePipeline(InDevice, &_statsPipeline, reproject_stats_cso, sizeof(reproject_stats_cso),
                               statsShaderCode.c_str()))
    {
        LOG_ERROR("[{0}] Failed to create stats compute pipeline", _name);
        return;
    }

    if (!CreateStatsResources())
        return;

    // Live mouse input, stays mapped for the whole lifetime so the input thread can write into it at any time
    auto liveDesc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    auto result =
        InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &liveDesc,
                                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_liveInput));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource (live input) error {1:x}", _name, (unsigned int) result);
        return;
    }

    _liveInput->SetName(L"Reproject_LiveInput");

    CD3DX12_RANGE readRange(0, 0);
    if (_liveInput->Map(0, &readRange, &_liveInputMapped) != S_OK)
    {
        LOG_ERROR("[{0}] Failed to map live input", _name);
        _liveInputMapped = nullptr;
        return;
    }

    std::memset(_liveInputMapped, 0, sizeof(ReprojectionLiveInput));

    // Result of the latch pass, 3 rotation rows + used mouse delta
    auto latchedDesc = CD3DX12_RESOURCE_DESC::Buffer(4 * sizeof(XMFLOAT4), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    result = InDevice->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &latchedDesc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&_latchedOutput));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource (latched output) error {1:x}", _name, (unsigned int) result);
        return;
    }

    _latchedOutput->SetName(L"Reproject_LatchedOutput");

    _init = InitHeaps(InDevice, _frameHeaps, Reproject_NUM_OF_HEAPS);

    if (_init)
        InputCollection::getInstance().registerLiveSink(_liveInputMapped);

    // TODO: make this owned or something, so that hooks stay in place if there are
    // two instances of this shader and one gets destroyed
    RawInputHook::getInstance().start();
}

bool Reproject_Dx12::Dispatch(ID3D12GraphicsCommandList* cmdList, ReprojectionParams& params,
                              ID3D12Resource* realPresent, D3D12_RESOURCE_STATES realPresentState,
                              ID3D12Resource* fakePresent, D3D12_RESOURCE_STATES fakePresentState,
                              ID3D12Resource* hudless, D3D12_RESOURCE_STATES hudlessState, ID3D12Resource* depth,
                              D3D12_RESOURCE_STATES depthState, ID3D12Resource* velocity,
                              D3D12_RESOURCE_STATES velocityState)
{
    if (!_init || !_device || !realPresent || !hudless || !cmdList || !depth)
        return false;

    ScopedGpuTime_Dx12 scopedGpuTime(GpuTime.get(), cmdList);

    _counter++;
    _counter = _counter % Reproject_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];
    auto& currentBuffer = _buffer[_counter];

    if (!currentBuffer)
    {
        LOG_DEBUG("[{0}] Start!", _name);

        auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                             D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

        auto result = Shader_Dx12::CreateBufferResource(_device, realPresent, D3D12_RESOURCE_STATE_COPY_DEST,
                                                        &currentBuffer, resourceFlags);

        if (result)
            currentBuffer->SetName(L"Reproject_Buffer");

        return result;
    }

    // Not every caller provides depth size
    if (params.DepthWidth == 0 || params.DepthHeight == 0)
    {
        auto depthDesc = depth->GetDesc();
        params.DepthWidth = static_cast<uint32_t>(depthDesc.Width);
        params.DepthHeight = depthDesc.Height;
    }

    if (!CreateMaskResource(&_depthMaskTemp, params.DepthWidth, params.DepthHeight, L"Reproject_DepthMaskTemp") ||
        !CreateMaskResource(&_depthMask, params.DepthWidth, params.DepthHeight, L"Reproject_DepthMask"))
    {
        return false;
    }

    auto presentFormat = realPresent->GetDesc().Format;
    auto hudlessFormat = hudless->GetDesc().Format;

    if (presentFormat != _loggedPresentFormat || hudlessFormat != _loggedHudlessFormat)
    {
        LOG_INFO("[{0}] Present format: {1} ({2}), hudless format: {3} ({4})", _name,
                 magic_enum::enum_name(presentFormat), (uint32_t) presentFormat, magic_enum::enum_name(hudlessFormat),
                 (uint32_t) hudlessFormat);

        _loggedPresentFormat = presentFormat;
        _loggedHudlessFormat = hudlessFormat;
    }

    // R11G11B10 can't store negative values (scRGB) and loses precision every frame on the carried over texels,
    // as they get premultiplied by a decaying confidence
    constexpr auto cacheColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    bool cacheReady = params.CacheEnabled && CreateCacheResources(params.CacheSize, cacheColorFormat);
    bool cacheUpdate = cacheReady && _cacheUpdatePending;

    // Can't sample a cache that was never written
    params.CacheEnabled = (cacheReady && (_cacheValid || cacheUpdate)) ? 1 : 0;

    if (cacheUpdate && !_cacheValid)
        params.CacheReset = 1;

    // Update reads prevCache and writes into the other one, the reprojection reads the newest
    uint32_t prevCache = _cacheIndex;
    uint32_t newestCache = cacheUpdate ? 1 - _cacheIndex : _cacheIndex;

    bool statsUpdate = _statsPending && velocity != nullptr;
    _statsPending = false;

    ResourceBarrier(cmdList, currentBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    ResourceBarrier(cmdList, realPresent, realPresentState, D3D12_RESOURCE_STATE_COPY_SOURCE);

    cmdList->CopyResource(currentBuffer, realPresent);

    // Make sure present is in D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    if (fakePresent)
        ResourceBarrier(cmdList, fakePresent, fakePresentState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ResourceBarrier(cmdList, realPresent, D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ResourceBarrier(cmdList, hudless, hudlessState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ResourceBarrier(cmdList, depth, depthState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (statsUpdate)
        ResourceBarrier(cmdList, velocity, velocityState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ResourceBarrier(cmdList, currentBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Create views
    auto presentDesc = realPresent->GetDesc();

    // Hudless is often a typeless version of the present format (Cyberpunk: R8G8B8A8_TYPELESS vs R8G8B8A8_UNORM),
    // viewing it as present keeps the same encoding, anything else gets its own format as it can't be cast
    auto hudlessViewFormat =
        CompareResourceFormats(presentDesc.Format, hudlessFormat) ? presentDesc.Format : hudlessFormat;
    CreateShaderResourceView(_device, hudless, currentHeap.GetSrvCPU(0), hudlessViewFormat);
    CreateShaderResourceView(_device, realPresent, currentHeap.GetSrvCPU(1));
    CreateShaderResourceView(_device, depth, currentHeap.GetSrvCPU(2));
    if (fakePresent)
        CreateShaderResourceView(_device, fakePresent, currentHeap.GetSrvCPU(3));
    CreateShaderResourceView(_device, _depthMask, currentHeap.GetSrvCPU(4));
    CreateShaderResourceView(_device, _depthMaskTemp, currentHeap.GetSrvCPU(5));

    CreateUnorderedAccessView(_device, currentBuffer, currentHeap.GetUavCPU(0), 0);
    CreateUnorderedAccessView(_device, _depthMaskTemp, currentHeap.GetUavCPU(1), 0);
    CreateUnorderedAccessView(_device, _depthMask, currentHeap.GetUavCPU(2), 0);

    if (statsUpdate)
    {
        CreateShaderResourceView(_device, velocity, currentHeap.GetSrvCPU(10));
    }
    else
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc {};
        nullSrvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
        nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullSrvDesc.Texture2D.MipLevels = 1;
        _device->CreateShaderResourceView(nullptr, &nullSrvDesc, currentHeap.GetSrvCPU(10));
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC statsUavDesc {};
    statsUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    statsUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    statsUavDesc.Buffer.NumElements = StatsCount;
    statsUavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    _device->CreateUnorderedAccessView(_statsOutput, nullptr, &statsUavDesc, currentHeap.GetUavCPU(6));

    // Cache views, null descriptors when there is no cache
    auto createCubeSrv = [&](ID3D12Resource* resource, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE handle)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MipLevels = 1;
        _device->CreateShaderResourceView(resource, &srvDesc, handle);
    };

    auto createArrayUav = [&](ID3D12Resource* resource, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE handle)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc {};
        uavDesc.Format = format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.ArraySize = 6;
        _device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, handle);
    };

    constexpr auto cacheGeoFormat = DXGI_FORMAT_R16G16_FLOAT;

    createCubeSrv(cacheReady ? _cacheColor[prevCache] : nullptr, cacheColorFormat, currentHeap.GetSrvCPU(6));
    createCubeSrv(cacheReady ? _cacheGeo[prevCache] : nullptr, cacheGeoFormat, currentHeap.GetSrvCPU(7));
    createCubeSrv(cacheReady ? _cacheColor[newestCache] : nullptr, cacheColorFormat, currentHeap.GetSrvCPU(8));
    createCubeSrv(cacheReady ? _cacheGeo[newestCache] : nullptr, cacheGeoFormat, currentHeap.GetSrvCPU(9));
    createArrayUav(cacheUpdate ? _cacheColor[newestCache] : nullptr, cacheColorFormat, currentHeap.GetUavCPU(4));
    createArrayUav(cacheUpdate ? _cacheGeo[newestCache] : nullptr, cacheGeoFormat, currentHeap.GetUavCPU(5));

    D3D12_UNORDERED_ACCESS_VIEW_DESC latchedUavDesc {};
    latchedUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    latchedUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    latchedUavDesc.Buffer.NumElements = 4;
    latchedUavDesc.Buffer.StructureByteStride = sizeof(XMFLOAT4);
    _device->CreateUnorderedAccessView(_latchedOutput, nullptr, &latchedUavDesc, currentHeap.GetUavCPU(3));

    if (!CreateConstantsBuffer(_device, _constantBuffers[_counter], params, currentHeap.GetCbvCPU(0)))
    {
        LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
        return false;
    }

    D3D12_CONSTANT_BUFFER_VIEW_DESC liveCbvDesc {};
    liveCbvDesc.BufferLocation = _liveInput->GetGPUVirtualAddress();
    liveCbvDesc.SizeInBytes = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    _device->CreateConstantBufferView(&liveCbvDesc, currentHeap.GetCbvCPU(1));

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    cmdList->SetComputeRootSignature(_rootSignature);
    cmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    // Depth mask prepass
    UINT maskDispatchWidth = (params.DepthWidth + InNumThreadsX - 1) / InNumThreadsX;
    UINT maskDispatchHeight = (params.DepthHeight + InNumThreadsY - 1) / InNumThreadsY;

    // Writes straight to _depthMask when CutoffExpandPx == 0
    cmdList->SetPipelineState(_maskPipelineH);
    cmdList->Dispatch(maskDispatchWidth, maskDispatchHeight, 1);

    if (params.CutoffExpandPx > 0)
    {
        ResourceBarrier(cmdList, _depthMaskTemp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmdList->SetPipelineState(_maskPipelineV);
        cmdList->Dispatch(maskDispatchWidth, maskDispatchHeight, 1);

        ResourceBarrier(cmdList, _depthMaskTemp, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    ResourceBarrier(cmdList, _depthMask, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Motion vector statistics, a single group
    if (statsUpdate)
    {
        cmdList->SetPipelineState(_statsPipeline);
        cmdList->Dispatch(1, 1, 1);

        ResourceBarrier(cmdList, _statsOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyResource(_statsReadback[_statsSlot], _statsOutput);
        ResourceBarrier(cmdList, _statsOutput, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        _statsWritten[_statsSlot] = true;
        _statsOrbit[_statsSlot] = _pendingOrbit;
        _statsSlot = (_statsSlot + 1) % StatsLatency;
    }

    // Direction cache update, once per rendered frame
    if (cacheUpdate)
    {
        ResourceBarrier(cmdList, _cacheColor[newestCache], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrier(cmdList, _cacheGeo[newestCache], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        UINT cacheDispatchSize = (params.CacheSize + 7) / 8;

        cmdList->SetPipelineState(_cachePipeline);
        cmdList->Dispatch(cacheDispatchSize, cacheDispatchSize, 6);

        ResourceBarrier(cmdList, _cacheColor[newestCache], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrier(cmdList, _cacheGeo[newestCache], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        _cacheIndex = newestCache;
        _cacheValid = true;
        _cacheOrigin = _pendingCacheOrigin;
        _cacheFrameId = _pendingCacheFrameId;
        _cacheUpdatePending = false;
    }

    // Late latch, as close as possible to the reprojection itself
    cmdList->SetPipelineState(_latchPipeline);
    cmdList->Dispatch(1, 1, 1);

    auto latchBarrier = CD3DX12_RESOURCE_BARRIER::UAV(_latchedOutput);
    cmdList->ResourceBarrier(1, &latchBarrier);

    cmdList->SetPipelineState(_pipelineState);

    UINT dispatchWidth = static_cast<UINT>((presentDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    UINT dispatchHeight = (presentDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    cmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    if (fakePresent)
        ResourceBarrier(cmdList, fakePresent, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, fakePresentState);
    ResourceBarrier(cmdList, realPresent, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, realPresentState);
    ResourceBarrier(cmdList, hudless, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, hudlessState);
    ResourceBarrier(cmdList, depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, depthState);
    if (statsUpdate)
        ResourceBarrier(cmdList, velocity, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, velocityState);
    ResourceBarrier(cmdList, currentBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ResourceBarrier(cmdList, _depthMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    return true;
}

bool Reproject_Dx12::Dispatch(IDXGISwapChain3* sc, ID3D12GraphicsCommandList* cmdList, ReprojectionParams& params,
                              ID3D12Resource* hudless, D3D12_RESOURCE_STATES hudlessState, ID3D12Resource* depth,
                              D3D12_RESOURCE_STATES depthState, ID3D12Resource* velocity,
                              D3D12_RESOURCE_STATES velocityState)
{
    if (!_init || !sc)
        return false;

    DXGI_SWAP_CHAIN_DESC scDesc {};
    if (sc->GetDesc(&scDesc) != S_OK)
    {
        LOG_WARN("Can't get swapchain desc!");
        return false;
    }

    // Get SwapChain Buffer
    ComPtr<ID3D12Resource> scBuffer;
    auto scIndex = sc->GetCurrentBackBufferIndex();
    if (auto result = sc->GetBuffer(scIndex, IID_PPV_ARGS(&scBuffer)); result != S_OK)
    {
        LOG_ERROR("sc->GetBuffer({}) error: {:X}", scIndex, (unsigned long) result);
        return false;
    }

    auto present = scBuffer.Get();
    auto presentState = D3D12_RESOURCE_STATE_PRESENT;
    auto result = Dispatch(cmdList, params, present, presentState, nullptr, D3D12_RESOURCE_STATE_COMMON, hudless,
                           hudlessState, depth, depthState, velocity, velocityState);

    if (result)
    {
        // Already in D3D12_RESOURCE_STATE_COPY_SOURCE
        auto currentBuffer = GetCurrentBuffer();

        ResourceBarrier(cmdList, present, presentState, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(present, currentBuffer);

        ResourceBarrier(cmdList, present, D3D12_RESOURCE_STATE_COPY_DEST, presentState);
    }

    return result;
}

ID3D12Resource* Reproject_Dx12::GetCurrentBuffer() { return _buffer[_counter]; }

Reproject_Dx12::~Reproject_Dx12()
{
    // Always stop the input thread from writing into the mapped memory
    InputCollection::getInstance().unregisterLiveSink(_liveInputMapped);

    if (!_init || State::Instance().isShuttingDown)
        return;

    SAFE_RELEASE(_rootSignature);
    SAFE_RELEASE(_constantBuffer);
    SAFE_RELEASE(_maskPipelineH);
    SAFE_RELEASE(_maskPipelineV);
    SAFE_RELEASE(_latchPipeline);
    SAFE_RELEASE(_cachePipeline);
    SAFE_RELEASE(_statsPipeline);
    SAFE_RELEASE(_statsOutput);

    for (uint32_t i = 0; i < StatsLatency; i++)
    {
        if (_statsMapped[i])
            _statsReadback[i]->Unmap(0, nullptr);

        SAFE_RELEASE(_statsReadback[i]);
    }

    ReleaseCacheResources();
    SAFE_RELEASE(_liveInput);
    SAFE_RELEASE(_latchedOutput);
    SAFE_RELEASE(_depthMaskTemp);
    SAFE_RELEASE(_depthMask);

    for (int i = 0; i < Reproject_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
        SAFE_RELEASE(_constantBuffers[i]);
    }

    RawInputHook::getInstance().stop();
}
