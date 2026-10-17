#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <mutex>

// Measures output-present cadence independently of the render/menu cadence.
// The timestamp is sampled by the presenting thread, and all state is guarded
// because Streamline may present real and interpolated frames on different threads.
class PresentRateMeter final
{
    static constexpr std::size_t Capacity = 512;
    static constexpr double WindowMs = 125.0;
    static constexpr double MinimumSpanMs = 50.0;

    std::mutex _mutex;
    std::array<double, Capacity> _timestamps {};
    std::size_t _head = 0;
    std::size_t _count = 0;
    bool _enabled = false;

    double At(std::size_t index) const { return _timestamps[(_head + index) % Capacity]; }

    void Clear()
    {
        _head = 0;
        _count = 0;
    }

    void DropOldest()
    {
        _head = (_head + 1) % Capacity;
        --_count;
        if (_count == 0)
            _head = 0;
    }

    void Prune(double nowMs)
    {
        while (_count > 0 && nowMs - At(0) > WindowMs)
            DropOldest();
    }

  public:
    // Called from the game's Vulkan present thread. Repeated 'true' calls are
    // harmless; a mode transition clears all data from the previous epoch.
    void SetEnabled(bool enabled)
    {
        std::lock_guard lock(_mutex);
        if (_enabled != enabled)
        {
            _enabled = enabled;
            Clear();
        }
    }

    // Called on each actual FG output present, not on each menu frame.
    void Present(double nowMs)
    {
        if (!std::isfinite(nowMs))
            return;

        std::lock_guard lock(_mutex);
        if (!_enabled)
            return;

        if (_count > 0)
        {
            // The timestamp is acquired before the lock. Concurrent callers
            // can reach the lock in a different order, so keep the ring sorted.
            nowMs = std::max(nowMs, At(_count - 1));
        }

        Prune(nowMs);

        // Bounded memory even if a game produces thousands of presents/s.
        if (_count == Capacity)
            DropOldest();

        _timestamps[(_head + _count) % Capacity] = nowMs;
        ++_count;
    }

    // Called from the menu thread. Zero means there are not enough recent
    // output presents; the ordinary game-present timing remains the fallback.
    double FrameTime(double nowMs)
    {
        if (!std::isfinite(nowMs))
            return 0.0;

        std::lock_guard lock(_mutex);
        if (!_enabled || _count == 0)
            return 0.0;

        nowMs = std::max(nowMs, At(_count - 1));
        Prune(nowMs);
        if (_count < 2)
            return 0.0;

        const double spanMs = At(_count - 1) - At(0);
        if (spanMs < MinimumSpanMs)
            return 0.0;

        // N actual present events contain N-1 measured intervals. This
        // produces the true event rate, unaffected by menu-frame batching.
        return spanMs / static_cast<double>(_count - 1);
    }
};
