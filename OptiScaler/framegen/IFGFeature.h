#pragma once
#include <pch.h>

#include <OwnedMutex.h>
#include "UtilCom.h"

#include <dxgi1_6.h>

class IFGFeature
{
  protected:
    float _jitterX = 0.0;
    float _jitterY = 0.0;
    float _mvScaleX = 0.0;
    float _mvScaleY = 0.0;
    float _cameraNear = 0.0;
    float _cameraFar = 0.0;
    float _cameraVFov = 0.0;
    float _meterFactor = 0.0;
    float _ftDelta = 0.0;
    UINT _reset = 0;

    UINT64 _frameCount = 0;
    UINT64 _lastUpscaledFrameId = 0;

    bool _isActive = false;
    UINT64 _targetFrame = 0;

    IID streamlineRiid {};

    bool CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);

    // If given interface has a Streamline proxy, return that. Otherwise return the interface unchanged.
    template<typename Intf>
    [[nodiscard]] Util::ComPtr<Intf> CheckForRealObject(std::string_view functionName, Util::ComPtr<Intf> pObject)
    {
        if (streamlineRiid.Data1 == 0)
        {
            auto iidResult = IIDFromString(L"{ADEC44E2-61F0-45C3-AD9F-1B37379284FF}", &streamlineRiid);

            if (iidResult != S_OK)
                return pObject;
        }

        Util::ComPtr<Intf> pRealObject;
        if (pObject->QueryInterface(streamlineRiid, std::out_ptr(pRealObject)) == S_OK && pRealObject != nullptr)
        {
            LOG_INFO("{} Streamline proxy found!", functionName);
            return pRealObject;
        }

        return pObject;
    }

  public:
    OwnedMutex Mutex;

    virtual feature_version Version() = 0;
    virtual const char* Name() = 0;

    virtual UINT64 UpscaleStart() = 0;
    virtual void UpscaleEnd() = 0;
    virtual void Present() = 0;

    virtual void SetVelocityAndDepthReady() = 0;
    virtual void SetHudlessReady() = 0;
    virtual void SetHudlessDispatchReady() = 0;

    virtual bool UpscalerInputsReady() = 0;
    virtual bool HudlessReady() = 0;
    virtual bool ReadyForExecute() = 0;

    virtual void FgDone() = 0;
    virtual void ReleaseObjects() = 0;
    virtual void StopAndDestroyContext(bool destroy, bool shutDown, bool useMutex) = 0;

    bool IsActive();
    int GetIndex();

    void SetJitter(float x, float y);
    void SetMVScale(float x, float y);
    void SetCameraValues(float nearValue, float farValue, float vFov, float meterFactor = 0.0f);
    void SetFrameTimeDelta(float delta);
    void SetReset(UINT reset);

    void ResetCounters();
    void UpdateTarget();

    UINT64 FrameCount();
    UINT64 TargetFrame();

    IFGFeature() = default;
};
