#include "pch.h"

#include "DLSSG_Common.h"

#include <hooks/Reflex_Hooks.h>

#include <magic_enum.hpp>

#include <DirectXMath.h>

using namespace DirectX;

feature_version DLSSG_Common::Version()
{
    if (StreamlineProxy::LoadStreamline())
        return StreamlineProxy::Version();

    return { 0, 0, 0 };
}

void DLSSG_Common::Activate()
{
    LOG_DEBUG("");

    if (!_isActive)
    {
        UpdateTarget();
        _isActive = true;
    }
}

void DLSSG_Common::Deactivate()
{
    LOG_DEBUG("");

    if (_isActive)
    {
        sl::DLSSGOptions options {};
        options.mode = sl::DLSSGMode::eOff;
        options.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;

        if (StreamlineProxy::DLSSGSetOptions() != nullptr)
            StreamlineProxy::DLSSGSetOptions()(_viewport, options); // Potential crash point on exit

        sl::ReflexOptions reflexConst = {};
        reflexConst.mode = sl::ReflexMode::eOff;
        reflexConst.useMarkersToOptimize = false;
        ScopedOptiScalerReflex optiScalerCall {};

        if (StreamlineProxy::ReflexSetOptions() != nullptr)
            StreamlineProxy::ReflexSetOptions()(reflexConst);

        _isActive = false;
    }
}

void DLSSG_Common::PauseAfterError()
{
    State::Instance().fgChanged = true;
    UpdateTarget();
    Deactivate();
}

void DLSSG_Common::QueryState()
{
    sl::DLSSGState dlssgState {};
    sl::DLSSGOptions dlssgOptions {};
    if (StreamlineProxy::DLSSGGetState()(_viewport, dlssgState, &dlssgOptions) == sl::Result::eOk)
    {
        State::Instance().dlssgMfgMax = dlssgState.numFramesToGenerateMax;
        _maxInterpolationCount = dlssgState.numFramesToGenerateMax;
        LOG_INFO("Max supported interpolations: {}", dlssgState.numFramesToGenerateMax);

        _supportsDMFG = dlssgState.bIsDynamicMFGSupported == sl::Boolean::eTrue;
    }
}

void DLSSG_Common::SetOptions(bool gameMarkersReachStreamline)
{
    auto config = Config::Instance();

    if (config->FGDLSSGInterpolationCount.value_or_default() > _maxInterpolationCount)
    {
        config->FGDLSSGInterpolationCount = _maxInterpolationCount;
        LOG_WARN("Requested interpolation count is higher than max supported, setting to max: {}",
                 _maxInterpolationCount);
    }

    const int targetCount =
        std::clamp(config->FGInterpolationCount(config->FGDLSSGInterpolationCount.value_or_default()), 1,
                   std::max(_maxInterpolationCount, 1));

    if (_framesToInterpolate != targetCount)
    {
        LOG_INFO("Interpolation count changed {} -> {}", _framesToInterpolate, targetCount);

        _framesToInterpolate = targetCount;
    }

    sl::DLSSGOptions options {};
    options.mode = sl::DLSSGMode::eOn;
    options.numFramesToGenerate = _framesToInterpolate;
    options.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;

    if (config->FGDLSSGForceDMFG.value_or_default())
    {
        options.mode = sl::DLSSGMode::eDynamic;
        options.dynamicTargetFrameRate = config->FGDLSSGFramerateTargetDMFG.value_or_default();
    }

    auto dlssgSetOptionsResult = StreamlineProxy::DLSSGSetOptions()(_viewport, options);

    if (dlssgSetOptionsResult != sl::Result::eOk)
        LOG_ERROR("Couldn't set DLSSG options, error: {}", magic_enum::enum_name(dlssgSetOptionsResult));

    sl::ReflexOptions reflexConst = {};
    reflexConst.mode = sl::ReflexMode::eLowLatency;
    reflexConst.useMarkersToOptimize = gameMarkersReachStreamline && ReflexHooks::gameIsSendingMarkers();

    ScopedOptiScalerReflex optiScalerCall {};
    auto reflexSetOptionsResult = StreamlineProxy::ReflexSetOptions()(reflexConst);

    if (reflexSetOptionsResult != sl::Result::eOk)
        LOG_ERROR("Couldn't set Reflex options, error: {}", magic_enum::enum_name(reflexSetOptionsResult));
}

bool DLSSG_Common::SetConstants(int fIndex, uint32_t frameId, uint32_t mvWidth, uint32_t mvHeight)
{
    sl::Constants constData = {};

    // Streamline wants near < far, the upscaler input has them swapped for inverted depth like FFX wants
    float cameraNear = std::min(_cameraNear[fIndex], _cameraFar[fIndex]);
    float cameraFar = std::max(_cameraNear[fIndex], _cameraFar[fIndex]);

    if (IsInfiniteDepth())
        cameraFar = std::numeric_limits<float>::infinity();

    if (_cameraPosition[fIndex][0] != 0.0f || _cameraPosition[fIndex][1] != 0.0f || _cameraPosition[fIndex][2] != 0.0f)
    {
        constData.cameraPos.x = _cameraPosition[fIndex][0];
        constData.cameraPos.y = _cameraPosition[fIndex][1];
        constData.cameraPos.z = _cameraPosition[fIndex][2];

        constData.cameraUp.x = _cameraUp[fIndex][0];
        constData.cameraUp.y = _cameraUp[fIndex][1];
        constData.cameraUp.z = _cameraUp[fIndex][2];

        constData.cameraFwd.x = _cameraForward[fIndex][0];
        constData.cameraFwd.y = _cameraForward[fIndex][1];
        constData.cameraFwd.z = _cameraForward[fIndex][2];

        constData.cameraRight.x = _cameraRight[fIndex][0];
        constData.cameraRight.y = _cameraRight[fIndex][1];
        constData.cameraRight.z = _cameraRight[fIndex][2];
    }
    else
    {
        constData.cameraPos = { 0.0f, 0.0f, 0.0f };
        constData.cameraUp = { 0.0f, 0.0f, 1.0f };
        constData.cameraRight = { 0.0f, 1.0f, 0.0f };
        constData.cameraFwd = { 1.0f, 0.0f, 0.0f };
        constData.cameraPinholeOffset = { 0.0f, 0.0f };

        auto prev = XMMatrixIdentity();

        XMFLOAT4X4 temp;
        XMStoreFloat4x4(&temp, prev);
        memcpy(&constData.clipToLensClip, &temp, sizeof(sl::float4x4));
        memcpy(&constData.clipToPrevClip, &temp, sizeof(sl::float4x4));
        memcpy(&constData.prevClipToClip, &temp, sizeof(sl::float4x4));
    }

    {
        XMMATRIX cameraViewToClip {};
        XMMATRIX clipToCameraView {};

        float vFov = _cameraVFov[fIndex];

        // XMMatrixPerspectiveFovRH will fail if input values are incorrect
        if (cameraNear > 0.f && std::isfinite(cameraFar) && !XMScalarNearEqual(vFov, 0.0f, 0.00001f) &&
            !XMScalarNearEqual(_cameraAspectRatio[fIndex], 0.0f, 0.00001f))
        {
            if (XMScalarNearEqual(cameraNear, cameraFar, 0.00001f))
                cameraFar++;

            // A reverse-Z projection for inverted depth, like the game's. DLSSG replacements like Nukem's read near
            // and far back out of it, a regular one loses the far plane to float precision.
            auto projectionNear = IsInvertedDepth() ? cameraFar : cameraNear;
            auto projectionFar = IsInvertedDepth() ? cameraNear : cameraFar;

            cameraViewToClip =
                XMMatrixPerspectiveFovRH(vFov, _cameraAspectRatio[fIndex], projectionNear, projectionFar);
            clipToCameraView = XMMatrixInverse(nullptr, cameraViewToClip);
        }
        else
        {
            LOG_WARN("Can't calculate projectionMatrix");
        }

        // Convert to sl::float4x4 for Streamline
        XMFLOAT4X4 temp;
        XMStoreFloat4x4(&temp, cameraViewToClip);
        memcpy(&constData.cameraViewToClip, &temp, sizeof(sl::float4x4));
        XMStoreFloat4x4(&temp, clipToCameraView);
        memcpy(&constData.clipToCameraView, &temp, sizeof(sl::float4x4));
    }

    constData.cameraAspectRatio = _cameraAspectRatio[fIndex];
    constData.cameraFOV = _cameraVFov[fIndex];
    constData.cameraNear = cameraNear;
    constData.cameraFar = cameraFar;

    constData.jitterOffset.x = _jitterX[fIndex];
    constData.jitterOffset.y = _jitterY[fIndex];

    constData.mvecScale.x = _mvScaleX[fIndex] / (float) mvWidth;
    constData.mvecScale.y = _mvScaleY[fIndex] / (float) mvHeight;

    if (!Config::Instance()->FGSkipReset.value_or_default())
        constData.reset = _reset[fIndex] != 0 ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    else
        constData.reset = sl::Boolean::eFalse;

    constData.depthInverted = IsInvertedDepth() ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constData.cameraMotionIncluded = sl::Boolean::eTrue;
    constData.motionVectors3D = sl::Boolean::eFalse;
    constData.orthographicProjection = sl::Boolean::eFalse;
    constData.motionVectorsDilated = IsLowResMV() ? sl::Boolean::eFalse : sl::Boolean::eTrue;
    constData.motionVectorsJittered = IsJitteredMVs() ? sl::Boolean::eTrue : sl::Boolean::eFalse;

    auto tokenResult = StreamlineProxy::GetNewFrameToken()(_frameToken, &frameId);
    if (tokenResult != sl::Result::eOk)
    {
        LOG_ERROR("GetNewFrameToken error: {} ({})", magic_enum::enum_name(tokenResult), (UINT) tokenResult);
        PauseAfterError();
        return false;
    }

    auto result = StreamlineProxy::SetConstants()(constData, *_frameToken, _viewport);
    if (result != sl::Result::eOk)
    {
        LOG_ERROR("SetConstants error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        PauseAfterError();
        return false;
    }

    return true;
}
