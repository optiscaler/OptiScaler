#pragma once

#include <algorithm>

// Dynamic MFG with "Use target as cap": picks the interpolated frame count every real frame so the presented frame
// rate reaches the target, up to the count set in the Frame Gen slider. OptiScaler holds the presented frame rate at
// the target with its FPS limit (FrameLimit::targetFps), so the count only has to be high enough to get there.
// Unlike XeFG_DynamicCount there is no rounding band and no 1X: any sustained shortfall steps up.
//  - Up one step when the presented rate stays more than 3% under the target for 0.3 s.
//  - Down one step when the real frame rate the game could reach, its frame time minus the time it slept in a low
//    latency or FPS limiter sleep, covers the target at one step less with 4% to spare for 1.5 s. When that can't
//    be measured (no limiter sleep, presented rate held at the target by V-Sync or an outside cap) a step down is
//    tried after a wait instead.
//  - A step down after which the presented rate stays more than 7% under the target is undone (judged on the frames
//    after the 3 still in flight for the old count, at least 6 and 0.4 s, nothing else decided for 1.5 s), and so is
//    one whose mean over those 1.5 s is more than 3% under (the bar Up uses, or the two take turns). The next waits
//    twice as long (5 s up to 60 s), unless the reachable rate has since grown by 10%: a GPU bound game's low
//    latency sleep also holds the CPU while the GPU works, so that measure can promise more than the GPU gives.
class XeFG_DynamicCap
{
  public:
    struct Sample
    {
        double nowMs = 0.0;     // monotonic clock
        double frameMs = 0.0;   // real frame interval
        double sleepMs = 0.0;   // part of it spent in a low latency or FPS limiter sleep, 0 if unknown
        double targetFps = 0.0; // presented frames per second wanted
        int maxCount = 1;       // most interpolated frames allowed
        int currentCount = 1;   // interpolated frames in use
    };

    enum class Reason
    {
        None,
        Up,
        Down,
        Probe,
        Undo
    };

    int Update(const Sample& s)
    {
        _reason = Reason::None;
        const int maxCount = std::max(s.maxCount, 1);
        const int count = std::clamp(s.currentCount, 1, maxCount);

        // No target, or a hitch / load screen frame: keep the count and the averages.
        if (s.targetFps <= 0.0 || s.frameMs <= 0.0 || s.frameMs > 250.0)
            return count;

        // An isolated hitch is no load change either
        if (_frameMs > 0.0 && s.frameMs > _frameMs * kHitchFactor && _hitchRun < kMaxHitchRun)
        {
            ++_hitchRun;
            return count;
        }
        _hitchRun = 0;

        const double work = std::max(s.frameMs - std::max(s.sleepMs, 0.0), 0.5);
        _sleepShare += kAlpha * ((s.sleepMs > 0.1 ? 1.0 : 0.0) - _sleepShare);
        _sleepSeen = _sleepShare > 0.5;

        if (_frameMs <= 0.0)
        {
            _frameMs = s.frameMs;
            _workMs = work;
            _lastChangeMs = s.nowMs;
            _cappedSinceMs = s.nowMs;
        }
        else
        {
            _frameMs += kAlpha * (s.frameMs - _frameMs);
            _workMs += kAlpha * (work - _workMs);
        }

        const double presented = 1000.0 * (count + 1) / _frameMs;
        const double reachable = 1000.0 / _workMs; // real frames per second without the limiter's sleep
        const double sinceChange = s.nowMs - _lastChangeMs;
        _presented = presented;
        _reachable = reachable;

        // A step down stands only if the presented rate holds once the real frame rate has risen to it. The frames
        // already in flight were limited for the old count, so the first few are skipped and the rest averaged;
        // nothing else is decided meanwhile.
        if (_checkUntilMs > 0.0)
        {
            if (++_checkFrames > kCheckSkipFrames)
            {
                _checkSumMs += s.frameMs;
                ++_checkCount;
            }

            if (_checkCount >= kCheckMinFrames && sinceChange >= kCheckMinMs &&
                1000.0 * (count + 1) * _checkCount / _checkSumMs < s.targetFps * 0.93 && count < maxCount)
            {
                _backoffMs = std::min(_backoffMs * 2.0, kMaxBackoffMs);
                _downBlockedUntilMs = s.nowMs + _backoffMs;
                _failedReachable = _downReachable;
                return Change(count + 1, s.nowMs, Reason::Undo, reachable);
            }

            if (s.nowMs < _checkUntilMs)
                return count;

            // At the end it has to hold the rate Up asks for, else the next Up takes it back and it is tried again
            // and again: undone, so the back-off grows.
            if (_checkCount > 0 && 1000.0 * (count + 1) * _checkCount / _checkSumMs < s.targetFps * 0.97 &&
                count < maxCount)
            {
                _backoffMs = std::min(_backoffMs * 2.0, kMaxBackoffMs);
                _downBlockedUntilMs = s.nowMs + _backoffMs;
                _failedReachable = _downReachable;
                return Change(count + 1, s.nowMs, Reason::Undo, reachable);
            }

            _checkUntilMs = 0.0;
            _backoffMs = kMinBackoffMs;
            _failedReachable = 0.0;
        }

        // Up
        if (count < maxCount && presented < s.targetFps * 0.97)
        {
            if (_lowSinceMs < 0.0)
                _lowSinceMs = s.nowMs;

            if (s.nowMs - _lowSinceMs >= kHoldUpMs && sinceChange >= kMinUpGapMs)
                return Change(count + 1, s.nowMs, Reason::Up, reachable);
        }
        else
        {
            _lowSinceMs = -1.0;
        }

        // A failed step down blocks the next for the back-off, or until the game got lighter.
        const bool blocked =
            s.nowMs < _downBlockedUntilMs && !(_failedReachable > 0.0 && reachable >= _failedReachable * 1.1);

        if (count <= 1 || blocked || sinceChange < kMinDownGapMs)
        {
            _highSinceMs = -1.0;
            return count;
        }

        // Down with evidence: one step less still reaches the target.
        if (reachable * count >= s.targetFps * 1.04)
        {
            if (_highSinceMs < 0.0)
                _highSinceMs = s.nowMs;

            if (s.nowMs - _highSinceMs >= kHoldDownMs)
            {
                _checkUntilMs = s.nowMs + kCheckMs;
                _downReachable = reachable;
                return Change(count - 1, s.nowMs, Reason::Down, reachable);
            }
        }
        else
        {
            _highSinceMs = -1.0;
        }

        // Down without evidence: held at the target with no limiter sleep to measure (V-Sync or an outside cap), try
        // one step less now and then. Above the target the frame time is the evidence. With a measured sleep this
        // tries too, after at least kSleepProbeMs: the wait that holds the rate may be one the sleep doesn't include.
        if (presented >= s.targetFps * 0.97 && presented <= s.targetFps * 1.03)
        {
            if (_cappedSinceMs < 0.0 || s.nowMs - _cappedLastMs > kGraceMs)
                _cappedSinceMs = s.nowMs;

            _cappedLastMs = s.nowMs;
            const double wait = _sleepSeen ? std::max(_backoffMs, kSleepProbeMs) : _backoffMs;

            if (s.nowMs - _cappedSinceMs >= std::min(wait, kHoldDownMs) && sinceChange >= wait)
            {
                _checkUntilMs = s.nowMs + kCheckMs;
                _downReachable = reachable;
                return Change(count - 1, s.nowMs, Reason::Probe, reachable);
            }
        }
        else if (_cappedSinceMs >= 0.0 && s.nowMs - _cappedLastMs > kGraceMs)
        {
            _cappedSinceMs = -1.0;
        }

        return count;
    }

    void Reset() { *this = XeFG_DynamicCap {}; }

    Reason LastReason() const { return _reason; }
    // The reachable real frame rate the last change was decided on
    double DecisionReachableFps() const { return _decisionReachable; }
    // For the log: the presented rate and the reachable real frame rate now, the step-down back-off
    double PresentedFps() const { return _presented; }
    double ReachableFps() const { return _reachable; }
    double BackoffMs() const { return _backoffMs; }

  private:
    static constexpr double kAlpha = 0.1;
    static constexpr double kHoldUpMs = 300.0;
    static constexpr double kHoldDownMs = 1500.0;
    static constexpr double kMinUpGapMs = 500.0;
    static constexpr double kMinDownGapMs = 2000.0;
    static constexpr int kCheckSkipFrames = 3; // frames in flight after a change, limited for the old count
    static constexpr int kCheckMinFrames = 6;
    static constexpr double kCheckMinMs = 400.0;
    static constexpr double kCheckMs = 1500.0;
    static constexpr double kMinBackoffMs = 5000.0;
    static constexpr double kMaxBackoffMs = 60000.0;
    static constexpr double kSleepProbeMs = 15000.0; // a try against a measured sleep's evidence
    static constexpr double kGraceMs = 300.0;        // a dip this short doesn't end the hold at the target
    static constexpr double kHitchFactor = 2.5;      // a frame this much longer than the average is a hitch
    static constexpr int kMaxHitchRun = 2;           // more in a row are a load change

    int Change(int count, double nowMs, Reason reason, double reachable)
    {
        _reason = reason;
        _decisionReachable = reachable;
        _lastChangeMs = nowMs;
        _lowSinceMs = -1.0;
        _highSinceMs = -1.0;
        _cappedSinceMs = -1.0;
        if (reason == Reason::Up || reason == Reason::Undo)
            _checkUntilMs = 0.0;

        _checkFrames = 0;
        _checkCount = 0;
        _checkSumMs = 0.0;

        // The real frame time changes with the count, start the averages over.
        _frameMs = 0.0;
        _workMs = 0.0;
        return count;
    }

    double _frameMs = 0.0;
    double _workMs = 0.0;
    double _lastChangeMs = 0.0;
    double _lowSinceMs = -1.0;
    double _highSinceMs = -1.0;
    double _cappedSinceMs = -1.0;
    double _cappedLastMs = -1.0;
    int _hitchRun = 0;
    double _checkUntilMs = 0.0;
    int _checkFrames = 0; // frames since a step down
    int _checkCount = 0;  // of them averaged
    double _checkSumMs = 0.0;
    double _downBlockedUntilMs = 0.0;
    double _backoffMs = kMinBackoffMs;
    double _downReachable = 0.0;   // reachable rate a step down was taken on
    double _failedReachable = 0.0; // the same for the last step down that was undone
    double _decisionReachable = 0.0;
    double _sleepShare = 0.0; // share of recent frames with a limiter sleep, kept across count changes
    bool _sleepSeen = false;
    double _presented = 0.0;
    double _reachable = 0.0;
    Reason _reason = Reason::None;
};
