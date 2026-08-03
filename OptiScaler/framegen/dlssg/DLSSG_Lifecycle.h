#pragma once

#include <algorithm>
#include <cstdint>

namespace optiscaler::dlssg
{
enum class LifecycleMode : uint8_t
{
    Off = 0,
    Observe = 1,
    Cycle = 2,
};

enum class LifecyclePhase : uint8_t
{
    Idle = 0,
    WarmingInputs,
    ReenablePending,
    FailedClosed,
};

enum LifecycleTrigger : uint32_t
{
    TriggerNone = 0,
    TriggerFgChanged = 1U << 0,
    TriggerSwapchainChanged = 1U << 1,
    TriggerReset = 1U << 2,
    TriggerRequested = 1U << 3,
};

struct LifecycleObservation
{
    bool fgChanged = false;
    bool swapchainChanged = false;
    bool reset = false;
    bool validInputs = false;
    bool runtimeReady = true;
};

struct LifecycleDecision
{
    LifecyclePhase phase = LifecyclePhase::Idle;
    uint32_t triggerMask = TriggerNone;
    uint32_t validWarmupFrames = 0;
    bool logSignalChange = false;
    bool logHeartbeat = false;
    bool beginCycle = false;
    bool warmupCompleted = false;
    bool reenable = false;
};

class LifecycleController
{
  private:
    LifecycleMode _mode = LifecycleMode::Off;
    LifecyclePhase _phase = LifecyclePhase::Idle;
    uint32_t _warmupFrames = 10;
    uint32_t _validWarmupFrames = 0;
    uint32_t _lastTriggerMask = TriggerNone;
    uint64_t _observationCount = 0;
    uint64_t _heartbeatInterval = 600;
    bool _cycleRequested = false;

  public:
    void Configure(LifecycleMode mode, uint32_t warmupFrames, uint64_t heartbeatInterval = 600);
    void RequestCycle();
    LifecycleDecision Step(const LifecycleObservation& observation);
    void FailClosed();

    LifecycleMode Mode() const { return _mode; }
    LifecyclePhase Phase() const { return _phase; }
    uint32_t ValidWarmupFrames() const { return _validWarmupFrames; }
    uint32_t WarmupFrames() const { return _warmupFrames; }
    bool OwnsActivation() const { return _mode == LifecycleMode::Cycle && _phase != LifecyclePhase::Idle; }
};

inline void LifecycleController::Configure(LifecycleMode mode, uint32_t warmupFrames, uint64_t heartbeatInterval)
{
    _warmupFrames = std::clamp(warmupFrames, 1U, 120U);
    _heartbeatInterval = heartbeatInterval;

    if (_mode == mode)
        return;

    _mode = mode;
    _phase = LifecyclePhase::Idle;
    _validWarmupFrames = 0;
    _cycleRequested = false;
}

inline void LifecycleController::RequestCycle() { _cycleRequested = true; }

inline LifecycleDecision LifecycleController::Step(const LifecycleObservation& observation)
{
    LifecycleDecision decision {};
    _observationCount++;

    if (observation.fgChanged)
        decision.triggerMask |= TriggerFgChanged;
    if (observation.swapchainChanged)
        decision.triggerMask |= TriggerSwapchainChanged;
    if (observation.reset)
        decision.triggerMask |= TriggerReset;
    if (_cycleRequested)
        decision.triggerMask |= TriggerRequested;

    decision.logSignalChange = decision.triggerMask != _lastTriggerMask;
    decision.logHeartbeat = _mode != LifecycleMode::Off && _heartbeatInterval > 0 &&
                            (_observationCount % _heartbeatInterval) == 0;
    _lastTriggerMask = decision.triggerMask;

    if (_mode != LifecycleMode::Cycle || _phase == LifecyclePhase::FailedClosed)
    {
        decision.phase = _phase;
        decision.validWarmupFrames = _validWarmupFrames;
        return decision;
    }

    if (_phase == LifecyclePhase::Idle && decision.triggerMask != TriggerNone && observation.runtimeReady)
    {
        _phase = LifecyclePhase::WarmingInputs;
        _validWarmupFrames = 0;
        _cycleRequested = false;
        decision.beginCycle = true;
        decision.phase = _phase;
        return decision;
    }

    if (_phase != LifecyclePhase::Idle)
        _cycleRequested = false;

    if (_phase == LifecyclePhase::WarmingInputs)
    {
        if (observation.validInputs && observation.runtimeReady)
            _validWarmupFrames++;
        else
            _validWarmupFrames = 0;

        if (_validWarmupFrames >= _warmupFrames)
        {
            _phase = LifecyclePhase::ReenablePending;
            decision.warmupCompleted = true;
        }
    }
    else if (_phase == LifecyclePhase::ReenablePending && observation.validInputs && observation.runtimeReady)
    {
        _phase = LifecyclePhase::Idle;
        decision.reenable = true;
    }

    decision.phase = _phase;
    decision.validWarmupFrames = _validWarmupFrames;
    return decision;
}

inline void LifecycleController::FailClosed()
{
    _phase = LifecyclePhase::FailedClosed;
    _cycleRequested = false;
}

inline const char* LifecycleModeName(LifecycleMode mode)
{
    switch (mode)
    {
    case LifecycleMode::Off:
        return "off";
    case LifecycleMode::Observe:
        return "observe";
    case LifecycleMode::Cycle:
        return "cycle";
    }

    return "unknown";
}

inline const char* LifecyclePhaseName(LifecyclePhase phase)
{
    switch (phase)
    {
    case LifecyclePhase::Idle:
        return "idle";
    case LifecyclePhase::WarmingInputs:
        return "warming-inputs";
    case LifecyclePhase::ReenablePending:
        return "reenable-pending";
    case LifecyclePhase::FailedClosed:
        return "failed-closed";
    }

    return "unknown";
}
} // namespace optiscaler::dlssg
