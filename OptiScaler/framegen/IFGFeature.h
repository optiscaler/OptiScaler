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

typedef enum FG_ResourceType : uint32_t
{
    Depth = 0,
    Velocity,
    HudlessColor,
    UIColor,
    Distortion
};

typedef enum FG_ResourceValidity : uint32_t
{
    ValidNow = 0,
    UntilPresent,
    ValidButMakeCopy,
    JustTrackCmdlist
};

class IFGFeature
{
  protected:
    float _jitterX[BUFFER_COUNT] = {};
    float _jitterY[BUFFER_COUNT] = {};
    float _mvScaleX[BUFFER_COUNT] = {};
    float _mvScaleY[BUFFER_COUNT] = {};
    float _cameraNear[BUFFER_COUNT] = {};
    float _cameraFar[BUFFER_COUNT] = {};
    float _cameraVFov[BUFFER_COUNT] = {};
    float _cameraAspectRatio[BUFFER_COUNT] = {};
    float _cameraPosition[BUFFER_COUNT][3] {}; ///< The camera position in world space
    float _cameraUp[BUFFER_COUNT][3] {};       ///< The camera up normalized vector in world space.
    float _cameraRight[BUFFER_COUNT][3] {};    ///< The camera right normalized vector in world space.
    float _cameraForward[BUFFER_COUNT][3] {};  ///< The camera forward normalized vector in world space.
    float _meterFactor[BUFFER_COUNT] = {};
    float _ftDelta[BUFFER_COUNT] = {};
    UINT _interpolationWidth[BUFFER_COUNT] = {};
    UINT _interpolationHeight[BUFFER_COUNT] = {};
    std::optional<UINT> _interpolationLeft[BUFFER_COUNT];
    std::optional<UINT> _interpolationTop[BUFFER_COUNT];
    UINT _reset[BUFFER_COUNT] = {};

    UINT64 _frameCount = 0;
    UINT64 _lastDispatchedFrame = 0;
    UINT64 _willDispatchFrame = 0;

    bool _isActive = false;
    UINT64 _targetFrame = 0;
    FG_Constants _constants {};

    std::unordered_map<FG_ResourceType, bool> _resourceReady[BUFFER_COUNT] {};

    bool _noHudless[BUFFER_COUNT] = { true, true, true, true };
    bool _noUi[BUFFER_COUNT] = { true, true, true, true };
    bool _noDistortionField[BUFFER_COUNT] = { true, true, true, true };
    bool _waitingExecute[BUFFER_COUNT] {};

    IID streamlineRiid {};

    bool CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);
    int GetDispatchIndex();
    virtual void NewFrame() = 0;

  public:
    OwnedMutex Mutex;

    virtual feature_version Version() = 0;
    virtual const char* Name() = 0;

    virtual bool Present() = 0;
    virtual void Activate() = 0;
    virtual void Deactivate() = 0;
    virtual void DestroyFGContext() = 0;
    virtual bool ReleaseSwapchain(HWND hwnd) = 0;
    virtual bool Shutdown() = 0;

    int GetIndex();
    UINT64 StartNewFrame();

    virtual void SetResourceReady(FG_ResourceType type) = 0;
    bool IsResourceReady(FG_ResourceType type, int index = -1);

    bool IsUsingUI();
    bool IsUsingDistortionField();
    bool IsUsingHudless();

    void SetExecuted();
    bool WaitingExecution(int index = -1);

    bool IsActive();
    bool IsPaused();
    bool IsDispatched();
    bool IsLowResMV();
    bool IsAsync();
    bool IsHdr();
    bool IsJitteredMVs();
    bool IsInvertedDepth();
    bool IsInfiniteDepth();

    void SetJitter(float x, float y);
    void SetMVScale(float x, float y);
    void SetCameraValues(float nearValue, float farValue, float vFov, float aspectRatio, float meterFactor = 0.0f);
    void SetCameraData(float cameraPosition[3], float cameraUp[3], float cameraRight[3], float cameraForward[3]);
    void SetFrameTimeDelta(float delta);
    void SetReset(UINT reset);
    void SetInterpolationRect(UINT width, UINT height);
    void GetInterpolationRect(UINT& width, UINT& height, int index = -1);
    void SetInterpolationPos(UINT left, UINT top);
    void GetInterpolationPos(UINT& left, UINT& top, int index = -1);

    void ResetCounters();
    void UpdateTarget();

    UINT64 FrameCount();
    UINT64 TargetFrame();
    UINT64 LastDispatchedFrame();

    IFGFeature() = default;
};
