#pragma once
#include "SysUtils.h"
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
};

enum FG_ResourceType : uint32_t
{
    Depth = 0,
    Velocity,
    HudlessColor,
    UIColor,
    Distortion,

    ResourceTypeCOUNT
};

enum class FG_ResourceValidity : uint32_t
{
    ValidNow = 0,
    UntilPresent,
    ValidButMakeCopy,
    JustTrackCmdlist,
    UntilPresentFromDispatch,

    ValidityCOUNT
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
    double _ftDelta[BUFFER_COUNT] = {};
    UINT64 _interpolationWidth[BUFFER_COUNT] = {};
    UINT _interpolationHeight[BUFFER_COUNT] = {};
    std::optional<UINT> _interpolationLeft[BUFFER_COUNT];
    std::optional<UINT> _interpolationTop[BUFFER_COUNT];
    UINT _reset[BUFFER_COUNT] = {};

    UINT64 _frameCount = 0;
    UINT64 _lastDispatchedFrame = 0;
    UINT64 _lastFGFrame = 0;
    bool _waitingNewFrameData = false;
    int _framesToInterpolate = -1;
    int _maxInterpolationCount = 1;
    bool _supportsDMFG = false;

    bool _isActive = false;
    UINT64 _targetFrame = 0;
    FG_Constants _constants {};

    std::unordered_map<FG_ResourceType, bool> _resourceReady[BUFFER_COUNT] {};
    std::unordered_map<FG_ResourceType, UINT64> _resourceFrame {};

    bool _noHudless[BUFFER_COUNT] = { true, true, true, true };
    bool _noUi[BUFFER_COUNT] = { true, true, true, true };
    bool _noDistortionField[BUFFER_COUNT] = { true, true, true, true };
    bool _waitingExecute[BUFFER_COUNT] {};

    IID streamlineRiid {};

    bool CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);
    int GetDispatchIndex(UINT64& willDispatchFrame);
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
    virtual bool HasResource(FG_ResourceType type, int index = -1) = 0;
    virtual bool SetInterpolatedFrameCount(UINT interpolatedFrameCount) = 0;

    int GetIndex();
    int GetIndexWillBeDispatched();
    UINT64 StartNewFrame();

    bool IsResourceReady(FG_ResourceType type, int index = -1);

    bool IsUsingUI();
    bool IsUsingUIAny(); // Same as IsUsingUI but checks if at least once buffer has UI
    bool IsUsingDistortionField();
    bool IsUsingHudless(int index = -1);
    bool IsUsingHudlessAny();

    void SetExecuted(int index = -1);
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

    void SetFrameCount(UINT64 frameId);
    void SetJitter(float x, float y, int index = -1);
    void SetMVScale(float x, float y, int index = -1);
    void SetCameraValues(float nearValue, float farValue, float vFov, float aspectRatio, float meterFactor = 0.0f,
                         int index = -1);
    void SetCameraData(float cameraPosition[3], float cameraUp[3], float cameraRight[3], float cameraForward[3],
                       int index = -1);
    void SetFrameTimeDelta(double delta, int index = -1);
    void SetReset(UINT reset, int index = -1);
    void SetInterpolationRect(UINT64 width, UINT height, int index = -1);
    void GetInterpolationRect(UINT64& width, UINT& height, int index = -1);
    void SetInterpolationPos(UINT left, UINT top, int index = -1);
    void GetInterpolationPos(UINT& left, UINT& top, int index = -1);
    void SetResourceReady(FG_ResourceType type, int index = -1);
    UINT GetInterpolatedFrameCount() const;
    int GetMaxInterpolationCount() const;
    bool GetDMFGSupport() const;

    void ResetCounters();
    void UpdateTarget();

    UINT64 FrameCount();
    UINT64 TargetFrame();
    UINT64 LastDispatchedFrame();

    IFGFeature() {}
};
