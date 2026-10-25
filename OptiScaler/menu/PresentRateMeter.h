#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

// Measures output-present cadence independently of the render/menu cadence.
// The timestamp is sampled by the presenting thread, and all state is guarded
// because Streamline may present real and interpolated frames on different threads.
class PresentRateMeter final
{
    static constexpr std::size_t Capacity = 512;
    static constexpr double WindowMs = 125.0;
    static constexpr double MinimumSpanMs = 50.0;

    struct Sample
    {
        double ms;
        std::uint64_t presents;
    };

    std::mutex _mutex;
    bool _enabled = false;
    std::uint64_t _presents = 0;
    double _lastPresentMs = 0.0;

    // Recent FrameTime calls, which happen at the same point of every game frame
    std::deque<Sample> _samples;

    // Unfiltered intervals between the output presents, for the frame time graph
    std::deque<double> _intervals;

  public:
    // Called from the game's Vulkan present thread. Repeated 'true' calls are
    // harmless; a mode transition clears all data from the previous epoch.
    void SetEnabled(bool enabled)
    {
        std::lock_guard lock(_mutex);
        if (_enabled == enabled)
            return;

        _enabled = enabled;
        _samples.clear();
        _intervals.clear();
        _lastPresentMs = 0.0;
    }

    // Called on each actual FG output present, not on each menu frame.
    void Present(double nowMs)
    {
        std::lock_guard lock(_mutex);
        if (!_enabled)
            return;

        ++_presents;

        // The timestamp is acquired before the lock. Concurrent callers
        // can reach the lock in a different order, keep the intervals non-negative.
        nowMs = std::max(nowMs, _lastPresentMs);

        if (_lastPresentMs > 0.0)
        {
            if (_intervals.size() == Capacity)
                _intervals.pop_front();

            _intervals.push_back(nowMs - _lastPresentMs);
        }

        _lastPresentMs = nowMs;
    }

    // Called from the menu thread. Zero means there are not enough recent
    // output presents; the ordinary game-present timing remains the fallback.
    double FrameTime(double nowMs)
    {
        std::lock_guard lock(_mutex);
        if (!_enabled)
            return 0.0;

        while (!_samples.empty() && nowMs - _samples.front().ms > WindowMs)
            _samples.pop_front();

        _samples.push_back({ nowMs, _presents });

        // The window starts at an earlier call, so it spans whole game frames. Measuring between the first
        // and last present instead overcounts when the generated and real frames aren't evenly spaced,
        // the window then often catches one extra present.
        const double spanMs = nowMs - _samples.front().ms;
        const auto presents = _presents - _samples.front().presents;
        if (spanMs < MinimumSpanMs || presents < 2)
            return 0.0;

        return spanMs / static_cast<double>(presents);
    }

    // Called from the menu thread. Hands over the intervals collected since the last call, returns false
    // when the FG output presents aren't measured.
    template <typename F> bool TakeIntervals(F&& callback)
    {
        std::lock_guard lock(_mutex);

        for (double ms : _intervals)
            callback(ms);

        _intervals.clear();
        return _enabled;
    }
};
