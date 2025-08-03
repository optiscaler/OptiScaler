#pragma once
#include <pch.h>

#include <OwnedMutex.h>

#include <dxgi1_6.h>
#include <flag-set-cpp/flag_set.hpp>

enum class FG_Flags : uint64_t
{
    Async,
    DisplayResolutionMVs,
    JitteredMVs,
    InvertedDepth,
    InfiniteDepth,
    Hdr,
    _
};

struct FG_Constants
{
    flag_set<FG_Flags> flags;
    uint32_t displayWidth;
    uint32_t displayHeight;
    // uint32_t maxRenderWidth;
    // uint32_t maxRenderHeight;
};

class IFGFeature
{
  protected:
    float _jitterX = 0.0;
    float _jitterY = 0.0;
    float _mvScaleX = 0.0;
    float _mvScaleY = 0.0;
    bool _mvScaleMultiplyByResolution = false;
    float _cameraNear = 0.0;
    float _cameraFar = 0.0;
    float _cameraVFov = 0.0;
    float _cameraPosition[3] {}; ///< The camera position in world space
    float _cameraUp[3] {};       ///< The camera up normalized vector in world space.
    float _cameraRight[3] {};    ///< The camera right normalized vector in world space.
    float _cameraForward[3] {};  ///< The camera forward normalized vector in world space.
    float _meterFactor = 0.0;
    float _ftDelta = 0.0;
    UINT _reset = 0;

    UINT64 _frameCount = 0;
    UINT64 _lastDispatchedFrame = 0;

    bool _isActive = false;
    UINT64 _targetFrame = 0;

    IID streamlineRiid {};

    bool CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);

  public:
    OwnedMutex Mutex;

    virtual feature_version Version() = 0;
    virtual const char* Name() = 0;

    virtual void Present() = 0;

    virtual void SetVelocityReady() = 0;
    virtual void SetDepthReady() = 0;
    virtual void SetHudlessReady() = 0;
    virtual void SetUIReady() = 0;
    virtual void SetHudlessDispatchReady() = 0;

    virtual bool UpscalerInputsReady() = 0;
    virtual bool HudlessReady() = 0;
    virtual bool ReadyForExecute() = 0;

    virtual void ReleaseObjects() = 0;
    virtual void StopAndDestroyContext(bool destroy, bool shutDown, bool useMutex) = 0;

    UINT64 StartNewFrame();

    bool IsActive();
    int GetIndex();

    void SetJitter(float x, float y);
    void SetMVScale(float x, float y, bool multiplyByResolution = false);
    void SetCameraValues(float nearValue, float farValue, float vFov, float meterFactor = 0.0f);
    void SetCameraData(float cameraPosition[3], float cameraUp[3], float cameraRight[3], float cameraForward[3]);
    void SetFrameTimeDelta(float delta);
    void SetReset(UINT reset);

    void ResetCounters();
    void UpdateTarget();

    UINT64 FrameCount();
    UINT64 LastDispatchedFrame();
    UINT64 TargetFrame();

    IFGFeature() = default;
};
