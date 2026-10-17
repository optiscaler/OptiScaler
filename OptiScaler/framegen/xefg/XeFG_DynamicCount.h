#pragma once

#include <algorithm>
#include <cmath>

// Dynamic MFG for the XeFG output: picks the interpolated frame count every real frame so the presented frame rate
// follows a target, up to the count set in the Frame Gen slider. The target is not a limit; only the FPS limit or
// V-Sync caps the presented rate ("Use target as cap" uses XeFG_DynamicCap instead).
//
// XeFG 3.x changes its count at the next frame, without re-initialization for counts 2 and up (moving to or from 1
// re-initializes inside XeFG), so a change needs no FG toggle.
//
// When nothing caps the presented rate (no FPS limit at or near the target, not held at the refresh rate or by a
// limiter elsewhere at the target in the last 2 s), the presented rate itself shows how far each count is from the
// target, and the count is the one expected to land nearest what the rounding asks for:
//  - nearest (default): the presented rate nearest the target; 2X and 3X are a whole real frame apart, so at worst
//    about 20% off it there;
//  - up: the lowest count at or above the target (from 97% of it); the most frames when none reaches it;
//  - down: the highest count at or below the target (up to 103% of it).
// One step more or less is expected from the real frame time and the learned cost of a generated frame, and it is
// taken when it lands nearer by 3% of the target (for up / down, also when it lands 1% worse on the side the rounding
// avoids), held 0.3 s up / 1 s down. Each change is judged as below, on the frames after the 3 still in flight: one
// that doesn't land nearer is undone and waits, as a failed step down or an up step that didn't earn its frame does.
// Where it settles doesn't depend on the count it came from. Each generated frame still has to earn 30% of the rate a
// free one adds (the cost of a frame is GPU time): an up step that doesn't isn't taken, and while the target can't be
// reached, a step down that keeps all but that much is. At the most frames short of the target, before any cost is
// learned, a step less is tried now and then: when the rate holds, a limiter elsewhere holds it, and the rules for a
// cap take over.
//
// Under a cap the presented rate can't show the headroom, and these rules decide instead:
// The count wanted is target / real frame rate - 1, rounded down, to nearest (default) or up. The count changes only
// when that is 0.8 or more away from the current one's centre (n + 0.5, n, n - 0.5 for down / nearest / up). So the
// presented rate at n + 1 times the real rate is kept between
// target * (n + 1) / (n + 1.8 + o) and target * n / (n + 0.2 + o) at one step less, o = 0.5 / 0 / -0.5:
// to nearest, 2X holds from 71% of the target, 6X from 88%.
//  - Up one step when the presented rate stays under its band for 0.3 s.
//  - Down one step when one step less is expected inside its band for 1 s (after the last change's check; lapses
//    under 0.3 s are forgiven): the real frame rate the game could reach without the time it waited in OptiScaler
//    (its low latency or FPS limiter sleep and the present call: V-Sync, a full queue), plus what generating one
//    frame less saves. That cost is learned from earlier changes.
//  - A step down is also tried when it is expected to come close, or when the presented rate is held at a cap (the
//    target, the refresh rate or the FPS limit) while the game didn't wait in OptiScaler: then it waits for V-Sync
//    or a limiter elsewhere and its headroom can't be seen. A GPU bound game's waits also hold the CPU while the GPU
//    works, so they promise more than the GPU gives; only the wait between tries limits those.
//  - Tries wait 3 s. Each step down keeps its own wait and the presented rate it failed at: after a failure the wait
//    doubles (quadruples after falling more than 15% short or failing early, up to 60 s), and the step stays
//    blocked while the game is no lighter (presents at most 8% more at that count). A step that holds at a lighter
//    load doesn't clear it, and an up step that helped blocks the step back down at the same load for 20 s.
//  - Each change is judged on the frames after the 3 still in flight for the old count, for 1.5 s. A step down is
//    undone when they fall under the new count's band (7% more short ends it early); an up step soon after counts
//    as a failed step down too.
//  - When the target can't be reached (each generated frame costs GPU time, or V-Sync or an outside limiter holds
//    the presented rate under it), one more generated frame has to earn at least 30% of the rate it adds when free.
//    An up step that doesn't is taken back, and the rate kept counts as the target (for 20 s, or until it is
//    passed); up steps then wait (3 s doubling up to 60 s) unless the rate drops by 10%. Steps down are tried now
//    and then, and one that loses less than that share stands, from the most frames too.
//  - What it learned (waits, frame cost, caps) survives FG being switched off and on; a new target or rounding
//    clears the waits.
//  - 1X (count 0) when allowed: frame generation off, the real frames alone. With nearest rounding 1X holds from
//    56% of the target, and 2X steps down to it when 1X is expected at 83%. Turning frame generation off saves its
//    whole cost, learned apart from the cost of each further frame.
class XeFG_DynamicCount
{
  public:
    struct Sample
    {
        double nowMs = 0.0;      // monotonic clock
        double frameMs = 0.0;    // real frame interval
        double waitMs = 0.0;     // part of it the game waited in OptiScaler (low latency / FPS limiter sleep, present)
        double targetFps = 0.0;  // presented frames per second wanted
        double refreshFps = 0.0; // the display refresh rate, a V-Sync cap, 0 if unknown
        double limitFps = 0.0;   // OptiScaler's FPS limit, 0 if none
        int maxCount = 1;        // most interpolated frames allowed
        int currentCount = 1;    // interpolated frames in use
        int rounding = 1;        // Rounding: 0 down, 1 nearest, 2 up
        int minCount = 1;        // fewest interpolated frames: 0 allows 1X (frame generation off)
    };

    enum class Rounding
    {
        Down,
        Nearest,
        Up
    };

    enum class Reason
    {
        None,
        Up,
        Down,
        Probe,
        Undo,
        Capped
    };

    int Update(const Sample& s)
    {
        _reason = Reason::None;
        const int maxCount = std::max(s.maxCount, 1);
        const int minCount = std::clamp(s.minCount, 0, 1);
        const int count = std::clamp(s.currentCount, minCount, maxCount);

        // No target, or a hitch / load screen frame: keep the count and the averages.
        if (s.targetFps <= 0.0 || s.frameMs <= 0.0 || s.frameMs > 250.0)
            return count;

        const double nowMs = s.nowMs;
        const double work = std::max(s.frameMs - std::clamp(s.waitMs, 0.0, s.frameMs), 0.5);

        // The centre of a count's band against the rounding: + 0.5 down, 0 nearest, - 0.5 up
        const int rounding = std::clamp(s.rounding, 0, 2);
        _offset = rounding == (int) Rounding::Down ? 0.5 : rounding == (int) Rounding::Up ? -0.5 : 0.0;

        // A new goal: what failed for the old one may work now.
        if (std::abs(s.targetFps - _goalFps) > _goalFps * 0.03 || rounding != _goalRounding)
        {
            _goalFps = s.targetFps;
            _goalRounding = rounding;
            for (auto& step : _steps)
                step = Step {};
            _upBackoffMs = kMinBackoffMs;
            _upBlockedUntilMs = 0.0;
        }

        if (_frameMs <= 0.0)
        {
            _frameMs = s.frameMs;
            _workMs = work;

            // Just started: let the frame rate settle before deciding
            if (_fresh)
                _lastChangeMs = nowMs;

            _fresh = false;
        }
        else
        {
            _frameMs += kAlpha * (s.frameMs - _frameMs);
            _workMs += kAlpha * (work - _workMs);
        }

        const double presented = 1000.0 * (count + 1) / _frameMs;
        // Presented at one step less: from 2X to 1X frame generation stops altogether, which saves more than a frame
        const bool toOff = count == 1;
        const double saved = toOff && _offSamples > 0 ? _offMs : _genMs;
        const double lower = count * 1000.0 / std::max(_workMs - saved, 0.5);
        const double waitShare = 1.0 - _workMs / _frameMs;
        const double sinceChange = nowMs - _lastChangeMs;
        const auto atCap = [presented](double cap)
        { return cap > 0.0 && presented >= cap * 0.97 && presented <= cap * 1.03; };

        // A rate kept under the target counts as the target until it is passed or for a while: the load changes.
        if (_capFps > 0.0 && (presented > _capFps * 1.05 || nowMs >= _capUntilMs))
            _capFps = 0.0;

        double target = s.targetFps;
        if (_capFps > 0.0)
            target = std::min(target, _capFps);
        if (atCap(s.refreshFps))
            target = std::min(target, s.refreshFps);

        const bool hidden = waitShare < kHiddenWaitShare &&
                            (atCap(target) || atCap(s.targetFps) || atCap(s.refreshFps) || atCap(s.limitFps));

        _presented = presented;
        _lower = lower;
        _target = target;
        _waitShare = waitShare;

        // Nothing caps the presented rate: the count expected nearest the target is picked. A cap seen in the last
        // 2 s still counts (a dip under V-Sync or a limiter isn't the cap gone). What was held for the other rules
        // starts over.
        if (Capped(presented, waitShare, s) || hidden)
            _cappedUntilMs = nowMs + kCapMemoryMs;

        if (const bool free = nowMs >= _cappedUntilMs; free != _free)
        {
            _free = free;
            _lowSinceMs = -1.0;
            _downHold = Hold {};
            _tryHold = Hold {};
            _upHold = Hold {};
        }

        // A change is judged on the frames after the ones still in flight for the old count; nothing else is decided
        // meanwhile.
        if (_check != Check::None)
        {
            if (++_checkFrames > kCheckSkipFrames)
            {
                _checkSumMs += s.frameMs;
                _checkWorkSumMs += work;
                ++_checkCount;
            }

            const bool enough = _checkCount >= kCheckMinFrames && sinceChange >= kCheckMinMs;
            const bool end = enough && nowMs >= _checkUntilMs;
            const double mean = enough ? 1000.0 * (count + 1) * _checkCount / _checkSumMs : 0.0;

            if (_checkFree)
            {
                // Made without a cap: it has to land nearer what the rounding asks for than the count it came from,
                // and an up step's frame has to earn 30% of the rate a free frame adds. While the target couldn't be
                // reached, a step down that keeps all but 30% of the share its frame added stands. Clearly worse ends
                // the check early.
                const double before = Miss(_checkBefore, s.targetFps, rounding);
                const double after = enough ? Miss(mean, s.targetFps, rounding) : 0.0;
                const bool unreached = _checkBefore < s.targetFps * kReached;
                const bool capped = enough && Capped(mean, 1.0 - _checkWorkSumMs / _checkSumMs, s);
                bool good = false;
                bool bad = false;

                // The rate held with one frame less where it was expected to drop: a limiter elsewhere holds it (a
                // costly frame's saving gives back most of the rate, not all of it)
                const bool held =
                    _check == Check::Down && mean >= _checkBefore * 0.99 && _checkExpected < _checkBefore * 0.9;

                if (_check == Check::Down)
                {
                    const double share = 1.0 / (count + 2);
                    const bool keeps = unreached && mean >= _checkBefore * (1.0 - kWorth * share);
                    good = held || keeps || after < before;
                    bad = !held && after > before + kNearMargin &&
                          !(unreached && mean >= _checkBefore * (1.0 - 0.7 * share));
                }
                else
                {
                    good = after < before && mean >= _checkBefore * (1.0 + kWorth / count);
                    bad = after > before + kNearMargin;
                }

                if (enough && (bad || (end && !good)))
                {
                    LearnFree(count, capped);

                    if (_check == Check::Down && count < maxCount)
                    {
                        FailedDown(count + 1, nowMs, !end, _checkBefore);
                        return Change(count + 1, nowMs, Reason::Undo, Check::None, presented);
                    }

                    if (_check == Check::Up && count > minCount)
                    {
                        BlockUp(nowMs);
                        return Change(count - 1, nowMs, Reason::Undo, Check::None, presented);
                    }
                }

                if (!end)
                    return count;

                // Held by a limiter elsewhere: its rate counts as the target for a while, and the rules for a cap
                // decide
                if (held)
                    LearnCap(mean, nowMs);
                else
                    LearnFree(count, capped);

                if (_check == Check::Down)
                    DownHeld(count);
                else
                    UpHeld(count, mean, nowMs);
            }
            else if (_check == Check::Down)
            {
                // It has to give what a step down is taken on (the rounding's bar for one step less, at most the
                // target: a limiter at it holds the rate there), or when the target wasn't reached before, keep most
                // of the rate: lose less than 30% of the share a free frame adds.
                const bool unreached = _checkBefore < s.targetFps * kReached;
                const double share = 1.0 / (count + 2);
                const double pass = unreached ? _checkBefore * (1.0 - kWorth * share)
                                              : target * std::min(DownShare(count + 1), kReached);
                const double fail = unreached ? _checkBefore * (1.0 - 0.7 * share) : pass * 0.95;

                if (enough && count < maxCount && (mean < fail || (end && mean < pass)))
                {
                    Learn(count, mean);
                    // Far short, or short so clearly that it ended early: wait four times as long
                    FailedDown(count + 1, nowMs, mean < pass * 0.85 || !end, _checkBefore);
                    return Change(count + 1, nowMs, Reason::Undo, Check::None, presented);
                }

                if (!end)
                    return count;

                Learn(count, mean);
                DownHeld(count);

                if (unreached)
                    LearnCap(mean, nowMs);
            }
            else
            {
                // One more interpolated frame raises the presented rate by (count + 1) / count when free. It has
                // to earn 30% of that.
                const double share = 1.0 / count;

                if (!(enough && mean >= _checkBefore * (1.0 + share * 0.5)))
                {
                    if (!end)
                        return count;

                    if (mean < _checkBefore * (1.0 + share * kWorth))
                    {
                        LearnCap(_checkBefore, nowMs);
                        BlockUp(nowMs);
                        return Change(count - 1, nowMs, Reason::Capped, Check::None, presented);
                    }
                }

                Learn(count, mean);
                UpHeld(count, mean, nowMs);
            }

            _check = Check::None;
        }

        if (_free)
            return Free(s, count, minCount, maxCount, rounding, presented, nowMs, sinceChange);

        // The game got lighter since a step down failed: the count it went back to now presents 15% more for 1 s.
        // (The waits can't tell: a GPU bound game's swing widely.)
        // The step down from this count, blocked after a failure while the game is no lighter than when it failed: the
        // presented rate here is at most 8% above what it was then. (The waits can't tell: a GPU bound game's swing
        // widely.)
        Step& down = StepFrom(count);
        const bool knownBad = nowMs < down.blockedUntilMs && presented <= down.failFps * kLighter;
        _shownBackoffMs = down.backoffMs;

        // Up, unless an up step just didn't earn its frame and the rate hasn't dropped since
        const bool upBlocked = nowMs < _upBlockedUntilMs && presented >= _upBlockedFps * 0.9;

        if (count < maxCount && presented < target * HoldShare(count) && !upBlocked)
        {
            if (_lowSinceMs < 0.0)
                _lowSinceMs = nowMs;

            if (nowMs - _lowSinceMs >= kHoldUpMs && sinceChange >= kMinUpGapMs)
            {
                // So soon after a step down, that step down didn't hold after all.
                if (_lastDownMs >= 0.0 && nowMs - _lastDownMs < kDownHoldsMs && _lastDownFrom == count + 1)
                    FailedDown(count + 1, nowMs, false, _lastDownBefore);

                return Change(count + 1, nowMs, Reason::Up, Check::Up, presented);
            }
        }
        else
        {
            _lowSinceMs = -1.0;
        }

        // At the most frames and still under the band: a step down may keep most of the rate (each frame costs GPU
        // time, or a cap the waits don't show holds it). Try one now and then.
        const bool capTest = count == maxCount && presented < target * HoldShare(count) && waitShare < kHiddenWaitShare;

        if (count <= minCount)
            return count;

        // The holds below forgive short lapses: at a target the presented rate can't pass (the FPS limit), it dips
        // under it now and then.
        const bool canDown = sinceChange >= kMinDownGapMs && (presented >= target * HoldShare(count) || capTest);
        const double needed = target * DownShare(count); // one step less, expected

        // Down: one step less is expected in its band with room. A failed step down blocks the next for the back-off.
        if (Held(_downHold, canDown && !capTest && !knownBad && lower >= needed, nowMs) >= kHoldDownMs)
        {
            return Change(count - 1, nowMs, Reason::Down, Check::Down, presented);
        }

        // Try one step less now and then: when it is expected to come close (until the cost of a frame is learned, a
        // step less can raise the real frame rate by more than it shows), when the presented rate is held at a cap
        // the waits don't explain, or while the target can't be reached.
        const bool costKnown = (toOff ? _offSamples : _genSamples) > 0;
        const double tryShare = costKnown ? kTryShareLearned : kTryShare;
        const bool unreached = presented < s.targetFps * kReached;

        // While the target can't be reached, once the cost is learned and no cap hides the headroom, only a step
        // expected to keep close to what it has to (all but 30% of the share a free frame adds) is tried.
        const double keep = presented * (1.0 - kWorth / (count + 1)) * kTryShareLearned;
        const bool worthTry = unreached && (!costKnown || hidden || lower >= keep);
        const bool tryDown = canDown && (capTest || hidden || worthTry || lower >= needed * tryShare);

        if (Held(_tryHold, tryDown && !knownBad, nowMs) >= down.backoffMs)
            return Change(count - 1, nowMs, Reason::Probe, Check::Down, presented);

        return count;
    }

    // Everything, for Dynamic MFG switched off
    void Reset() { *this = XeFG_DynamicCount {}; }

    // The averages and timers, for FG switched off and on: what was learned stays
    void Restart()
    {
        _frameMs = 0.0;
        _workMs = 0.0;
        _lowSinceMs = -1.0;
        _downHold = Hold {};
        _tryHold = Hold {};
        _upHold = Hold {};
        _lastDownMs = -1.0;
        _check = Check::None;
        _fresh = true;
    }

    Reason LastReason() const { return _reason; }

    // State after the last Update, for logging
    double PresentedFps() const { return _presented; }
    double LowerFps() const { return _lower; } // expected presented rate at one step less
    double TargetFps() const { return _target; }
    double WaitShare() const { return _waitShare; }
    bool Uncapped() const { return _free; } // nothing caps the presented rate: the count nearest the target

    // Learned cost of one interpolated frame
    double FrameCostMs() const { return _free && _freeGenSamples > 0 ? _freeGenMs : _genMs; }
    double BackoffMs() const { return _shownBackoffMs; } // of the step down from the current count

  private:
    enum class Check
    {
        None,
        Down,
        Up
    };

    // How long a condition has held, through lapses of up to kGraceMs
    struct Hold
    {
        double sinceMs = -1.0;
        double lastMs = -1.0;
    };

    static constexpr double kAlpha = 0.1;
    static constexpr double kHysteresis = 0.8; // counts
    static constexpr double kReached = 0.97;   // the target itself counts as reached from here
    static constexpr double kHoldUpMs = 300.0;
    static constexpr double kHoldDownMs = 1000.0;
    static constexpr double kLighter = 1.08;      // presented rate above a failure's that counts as a lighter game
    static constexpr double kUpHoldsMs = 20000.0; // no step back down at the same load after an up step that helped
    static constexpr double kGraceMs = 300.0;
    static constexpr double kMinUpGapMs = 500.0;
    static constexpr double kMinDownGapMs = 1500.0; // the check of the last change
    static constexpr int kCheckSkipFrames = 3;      // frames in flight after a change, limited for the old count
    static constexpr int kCheckMinFrames = 6;
    static constexpr double kCheckMinMs = 400.0;
    static constexpr double kCheckMs = 1500.0;
    static constexpr double kDownHoldsMs = 10000.0;
    static constexpr double kTryShare = 0.75;
    static constexpr double kTryShareLearned = 0.95;
    static constexpr double kWorth = 0.3;            // share of a free frame's rate a generated frame has to earn
    static constexpr double kHiddenWaitShare = 0.05; // less waiting than this at a cap: the game waits elsewhere
    static constexpr double kCapRetestMs = 20000.0;
    static constexpr double kMinBackoffMs = 3000.0;
    static constexpr double kMaxBackoffMs = 60000.0;
    static constexpr double kNearMargin = 0.03;    // share of the target a step has to land nearer by, without a cap
    static constexpr double kSideMargin = 0.01;    // up / down without a cap: kept from the side they avoid
    static constexpr double kAtMost = 1.03;        // down rounding without a cap: at or below the target up to here
    static constexpr double kLimitNear = 1.1;      // an FPS limit up to this far above the target caps the rate near it
    static constexpr double kCapMemoryMs = 2000.0; // a cap seen this recently still holds the rate

    // A cap holds the presented rate: an FPS limit at or near the target (also while the rate dips under it), the
    // rate held at the FPS limit or the refresh rate (V-Sync), or at the target while the game didn't wait in
    // OptiScaler (a limiter elsewhere)
    static bool Capped(double fps, double waitShare, const Sample& s)
    {
        const auto at = [fps](double cap) { return cap > 0.0 && fps >= cap * 0.97 && fps <= cap * 1.03; };
        return (s.limitFps > 0.0 && s.limitFps <= s.targetFps * kLimitNear) || at(s.limitFps) || at(s.refreshFps) ||
               (waitShare < kHiddenWaitShare && at(s.targetFps));
    }

    // Without a cap, how far a presented rate is from what the rounding asks for, 0 at best. Nearest: its distance
    // to the target. Up: the lowest count at or above the target (from 97% of it). Down: the highest at or below it
    // (up to 103%). A rate on the side the rounding avoids misses by more than any on the other side.
    static double Miss(double fps, double targetFps, int rounding)
    {
        const double r = fps / targetFps;

        if (rounding == (int) Rounding::Up)
            return r >= kReached ? r - kReached : 1.0 + kReached - r;

        if (rounding == (int) Rounding::Down)
            return r <= kAtMost ? kAtMost - r : 1.0 + r - kAtMost;

        return std::abs(r - 1.0);
    }

    // A count expected to present fps, judged as if it landed 1% worse on the side the rounding avoids (its check
    // undoes one that lands there)
    static double Expected(double fps, int rounding)
    {
        if (rounding == (int) Rounding::Up)
            return fps * (1.0 - kSideMargin);

        if (rounding == (int) Rounding::Down)
            return fps * (1.0 + kSideMargin);

        return fps;
    }

    // Without a cap: one step more or less when it is expected to land nearer what the rounding asks for. Nothing
    // holds the frame, so all of the real frame time is the game's (or the GPU's) work, waits included: a GPU bound
    // game's waits don't promise anything.
    int Free(const Sample& s, int count, int minCount, int maxCount, int rounding, double presented, double nowMs,
             double sinceChange)
    {
        const double genMs = _freeGenSamples > 0 ? _freeGenMs : _genMs;
        const double offMs = _freeOffSamples > 0 ? _freeOffMs : _offSamples > 0 ? _offMs : genMs;
        const auto limited = [&s](double fps) { return s.limitFps > 0.0 ? std::min(fps, s.limitFps) : fps; };

        // From and to 1X, frame generation itself turns on or off, which costs more than a frame
        const double higher =
            count < maxCount ? limited((count + 2) * 1000.0 / (_frameMs + (count == 0 ? offMs : genMs))) : 0.0;
        const double lower =
            count > minCount ? limited(count * 1000.0 / std::max(_frameMs - (count == 1 ? offMs : genMs), 0.5)) : 0.0;
        const double miss = Miss(presented, s.targetFps, rounding);
        const bool unreached = presented < s.targetFps * kReached;

        _lower = lower;
        _target = s.targetFps;

        // Up when nearer and its frame earns 30% of the rate a free one adds (each generated frame costs GPU time),
        // unless an up step just didn't and the rate hasn't dropped since
        const bool upBlocked = nowMs < _upBlockedUntilMs && presented >= _upBlockedFps * 0.9;
        const bool up = higher >= presented * (1.0 + kWorth / (count + 1)) && !upBlocked &&
                        Miss(Expected(higher, rounding), s.targetFps, rounding) + kNearMargin < miss;

        if (Held(_upHold, up, nowMs) >= kHoldUpMs && sinceChange >= kMinUpGapMs)
        {
            // So soon after a step down, that step down didn't hold after all.
            if (_lastDownMs >= 0.0 && nowMs - _lastDownMs < kDownHoldsMs && _lastDownFrom == count + 1)
                FailedDown(count + 1, nowMs, false, _lastDownBefore);

            return Change(count + 1, nowMs, Reason::Up, Check::Up, presented);
        }

        // Down when nearer, or while the target can't be reached when the last frame doesn't earn its share (one step
        // less keeps all but 30% of what it adds); not while it failed from here at this load
        const Step& step = StepFrom(count);
        const bool knownBad = nowMs < step.blockedUntilMs && presented <= step.failFps * kLighter;
        const bool keeps = unreached && lower >= presented * (1.0 - kWorth / (count + 1));
        const bool canDown = lower > 0.0 && sinceChange >= kMinDownGapMs && !knownBad;
        _shownBackoffMs = step.backoffMs;

        if (Held(_downHold,
                 canDown && (keeps || Miss(Expected(lower, rounding), s.targetFps, rounding) + kNearMargin < miss),
                 nowMs) >= kHoldDownMs)
        {
            _checkExpected = lower;
            return Change(count - 1, nowMs, Reason::Down, Check::Down, presented);
        }

        // At the most frames, short of the target and not waiting in OptiScaler before any cost is learned: a limiter
        // elsewhere may hold the rate under the target, which hides what one step less gives. Try it now and then.
        const bool probe =
            canDown && count == maxCount && unreached && _waitShare < kHiddenWaitShare && _freeGenSamples == 0;

        if (Held(_tryHold, probe, nowMs) >= step.backoffMs)
        {
            _checkExpected = lower;
            return Change(count - 1, nowMs, Reason::Probe, Check::Down, presented);
        }

        return count;
    }

    // An up step that didn't earn its frame: no up step for the back-off while the rate doesn't drop
    void BlockUp(double nowMs)
    {
        _upBackoffMs = std::min(_upBackoffMs * 2.0, kMaxBackoffMs);
        _upBlockedUntilMs = nowMs + _upBackoffMs;
        _upBlockedFps = _checkBefore;
    }

    // An up step that helped shows one step less doesn't hold at this load: no step back down while the game is no
    // lighter, for 20 s at least
    void UpHeld(int count, double mean, double nowMs)
    {
        _upBackoffMs = kMinBackoffMs;
        Step& back = StepFrom(count);
        back.failFps = mean;
        back.blockedUntilMs = std::max(back.blockedUntilMs, nowMs + std::max(back.backoffMs, kUpHoldsMs));
    }

    // A step down holds where it failed before: that failure is history. One that holds at a lighter load leaves it.
    void DownHeld(int count)
    {
        if (Step& held = StepFrom(count + 1); held.failFps <= 0.0 || _checkBefore <= held.failFps * kLighter)
            held = Step {};
    }

    // Lowest presented rate, as a share of the target, the band of count n holds: (n + 1) / (n + 1.8 + o)
    double HoldShare(int n) const { return (n + 1.0) / (n + 1.0 + kHysteresis + _offset); }

    // Presented rate one step less (n times the real rate) is expected to reach for a step down: n / (n + 0.2 + o)
    double DownShare(int n) const { return n / (n + 1.0 - kHysteresis + _offset); }

    static double Held(Hold& hold, bool condition, double nowMs)
    {
        if (!condition)
        {
            if (hold.sinceMs >= 0.0 && nowMs - hold.lastMs > kGraceMs)
                hold = Hold {};

            return -1.0;
        }

        if (hold.sinceMs < 0.0 || nowMs - hold.lastMs > kGraceMs)
            hold.sinceMs = nowMs;

        hold.lastMs = nowMs;
        return nowMs - hold.sinceMs;
    }

    // What failed stepping down from one count. Each step keeps its own wait, so one that holds doesn't clear another's
    // (in a GPU bound game 4X -> 3X can hold while 3X -> 2X keeps failing).
    struct Step
    {
        double backoffMs = kMinBackoffMs;
        double blockedUntilMs = 0.0;
        double failFps = 0.0; // presented at this count when its step down failed
    };

    static constexpr int kSteps = 9; // counts 0 - 8

    Step& StepFrom(int count) { return _steps[std::clamp(count, 0, kSteps - 1)]; }

    // A step down that fell far short won't hold soon either: wait four times as long
    void FailedDown(int from, double nowMs, bool farShort, double fromFps)
    {
        Step& step = StepFrom(from);
        step.backoffMs = std::min(step.backoffMs * (farShort ? 4.0 : 2.0), kMaxBackoffMs);
        step.blockedUntilMs = nowMs + step.backoffMs;
        step.failFps = fromFps;
        _lastDownMs = -1.0;
    }

    void LearnCap(double capFps, double nowMs)
    {
        _capFps = capFps;
        _capUntilMs = nowMs + kCapRetestMs;
    }

    // The cost of one interpolated frame: how the frame time without the waits moved with the count. Only when the
    // presented rate moved with the count too, or the waits were seen on both sides; under an unseen cap the frame
    // time follows the cap instead.
    void Learn(int count, double mean)
    {
        if (_checkCount < kCheckMinFrames || count == _checkBeforeCount || _checkBefore <= 0.0)
            return;

        const double after = _checkWorkSumMs / _checkCount;
        const double afterWaitShare = 1.0 - _checkWorkSumMs / _checkSumMs;
        const double expected = (count + 1.0) / (_checkBeforeCount + 1.0) - 1.0;
        const bool moved = std::abs(mean / _checkBefore - 1.0) >= std::abs(expected) * 0.25;
        const bool seen = _checkBeforeWaitShare >= 0.1 && afterWaitShare >= 0.1;

        if (!moved && !seen)
            return;

        const double perFrame = (after - _checkBeforeWorkMs) / (count - _checkBeforeCount);

        // Noise or a load change (frame generation itself, 1X <-> 2X, can cost as much as the frame)
        const bool onOff = std::min(count, _checkBeforeCount) == 0;
        if (perFrame < -0.1 * after || perFrame > (onOff ? 1.0 : 0.4) * after)
            return;

        const double cost = std::max(perFrame, 0.0);

        // Frame generation on or off (1X) is learned apart from each further frame
        if (onOff)
        {
            _offMs = _offSamples == 0 ? cost : _offMs + 0.5 * (cost - _offMs);
            ++_offSamples;
            return;
        }

        _genMs = _genSamples == 0 ? cost : _genMs + 0.5 * (cost - _genMs);
        ++_genSamples;
    }

    // Without a cap, the cost of one interpolated frame from how the whole real frame time moved with the count (not
    // when the change landed at a cap: then the frame time follows the cap)
    void LearnFree(int count, bool capped)
    {
        if (capped || _checkCount < kCheckMinFrames || count == _checkBeforeCount || _checkBeforeFrameMs <= 0.0)
            return;

        const double after = _checkSumMs / _checkCount;
        const double perFrame = (after - _checkBeforeFrameMs) / (count - _checkBeforeCount);
        const bool onOff = std::min(count, _checkBeforeCount) == 0;

        if (perFrame < -0.1 * after || perFrame > (onOff ? 1.0 : 0.4) * after)
            return;

        const double cost = std::max(perFrame, 0.0);

        if (onOff)
        {
            _freeOffMs = _freeOffSamples == 0 ? cost : _freeOffMs + 0.5 * (cost - _freeOffMs);
            ++_freeOffSamples;
            return;
        }

        _freeGenMs = _freeGenSamples == 0 ? cost : _freeGenMs + 0.5 * (cost - _freeGenMs);
        ++_freeGenSamples;
    }

    int Change(int count, double nowMs, Reason reason, Check check, double presented)
    {
        _reason = reason;
        _lastChangeMs = nowMs;
        _lowSinceMs = -1.0;
        _downHold = Hold {};
        _tryHold = Hold {};
        _upHold = Hold {};

        if (reason == Reason::Down || reason == Reason::Probe)
        {
            _lastDownMs = nowMs;
            _lastDownFrom = count + 1;
            _lastDownBefore = presented;
        }

        _check = check;
        _checkUntilMs = nowMs + kCheckMs;
        _checkFree = _free;
        _checkBefore = presented;
        _checkBeforeFrameMs = _frameMs;
        _checkBeforeWorkMs = _workMs;
        _checkBeforeWaitShare = _frameMs > 0.0 ? 1.0 - _workMs / _frameMs : 0.0;
        _checkBeforeCount = check == Check::Up ? count - 1 : count + 1;
        _checkFrames = 0;
        _checkCount = 0;
        _checkSumMs = 0.0;
        _checkWorkSumMs = 0.0;

        // The real frame time changes with the count, start the averages over.
        _frameMs = 0.0;
        _workMs = 0.0;
        return count;
    }

    bool _fresh = true; // no frame seen since a (re)start
    double _frameMs = 0.0;
    double _workMs = 0.0;
    double _lastChangeMs = 0.0;
    double _lowSinceMs = -1.0;
    Hold _downHold;     // one step less expected to reach the target
    Hold _tryHold;      // a step down worth trying
    Hold _upHold;       // without a cap: one step more expected nearer the target
    bool _free = false; // nothing caps the presented rate
    double _cappedUntilMs = 0.0;
    double _lastDownMs = -1.0;
    int _lastDownFrom = -1;       // count the last step down was from
    double _lastDownBefore = 0.0; // and what it presented there

    Check _check = Check::None;
    double _checkUntilMs = 0.0;
    bool _checkFree = false;     // the change was made without a cap
    double _checkExpected = 0.0; // and the presented rate it was expected to give, for a step down
    double _checkBefore = 0.0;   // presented rate before the change
    double _checkBeforeFrameMs = 0.0;
    double _checkBeforeWorkMs = 0.0;
    double _checkBeforeWaitShare = 0.0;
    int _checkBeforeCount = 0;
    int _checkFrames = 0; // frames since the change
    int _checkCount = 0;  // of them averaged
    double _checkSumMs = 0.0;
    double _checkWorkSumMs = 0.0;

    double _goalFps = 0.0; // target and rounding, for noticing new ones
    int _goalRounding = -1;
    double _offset = 0.0; // band centre against the rounding
    double _genMs = 0.0;  // learned cost of one interpolated frame
    int _genSamples = 0;
    double _offMs = 0.0; // learned cost of frame generation itself, 1X to 2X
    int _offSamples = 0;
    double _freeGenMs = 0.0; // the same without a cap, from the whole real frame time
    int _freeGenSamples = 0;
    double _freeOffMs = 0.0;
    int _freeOffSamples = 0;
    double _capFps = 0.0; // a rate kept under the target, 0 if none
    double _capUntilMs = 0.0;
    double _upBlockedUntilMs = 0.0;
    double _upBlockedFps = 0.0;
    double _upBackoffMs = kMinBackoffMs;
    Step _steps[kSteps];
    double _shownBackoffMs = kMinBackoffMs;

    double _presented = 0.0;
    double _lower = 0.0;
    double _target = 0.0;
    double _waitShare = 0.0;
    Reason _reason = Reason::None;
};
