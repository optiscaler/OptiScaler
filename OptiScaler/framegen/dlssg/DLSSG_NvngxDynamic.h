#pragma once

#include <Logger.h>
#include <Util.h>
#include <Config.h>
#include <State.h>

#include <framegen/xefg/XeFG_DynamicMFG.h>

#include <algorithm>
#include <atomic>
#include <mutex>

// Dynamic MFG in software for the game's own DLSSG through an nvngx provider (the FG Nvngx replacement). The game's
// Streamline asks the provider for numFramesToGenerate frames, so OptiScaler picks the count in its
// slDLSSGSetOptions hook. The provider's NGX evaluate is where a real frame is seen, MultiFrameIndex 1 starts one.
// The settings are the DLSSG output's. No 1X: DLSSG's off is its mode and that rebuilds its swapchain.
// "Use as FPS limit" is the DLSSG output's too: the cap is published as the FPS limit (FrameLimit::targetFps), like
// the FPS limit it counts presented frames in every limiter, and the limiters' sleeps tell the cap what the game
// could have used.
class DlssgNvngxDynamic
{
  public:
    // The provider's NGX evaluate, once per generated frame
    static void OnEvaluate(int frameIndex)
    {
        if (frameIndex > 1)
            return;

        const auto now = Util::MillisecondsNow();
        const auto last = _lastRealFrameMs.exchange(now);

        if (last > 0.0)
            _realFrameMs = now - last;

        _realFrames++;
    }

    // For the custom replacement (XeMFG XMX): its provider takes a new count on every frame
    static bool Supported()
    {
        const auto& state = State::Instance();
        return state.activeFgInput == FGInput::NvngxFG && state.activeFgNvngx == FGNvngxReplacement::Custom;
    }

    // The most the game, or Override DLSSG Ratio, asked for in the last options call, 0 until there is one
    static int MaxCount() { return _maxCount; }

    // FrameLimit::targetFps(): the cap in presented FPS while "Use as FPS limit" runs, 0 otherwise. It ends by itself
    // when the options stop coming, the game turned DLSSG off.
    static float CapFps()
    {
        if (Util::MillisecondsNow() - _lastApplyMs > 1000.0)
            return 0.0f;

        return _capFps;
    }

    // FrameLimit::sleep(): how long OptiScaler's own FPS limiter slept in a present. The game's DLSSG presents several
    // times per real frame, they add up.
    static void AddLimiterSleep(double ms) { _limiterSleepMs.fetch_add(std::min(ms, 1000.0)); }

    // From the options hook. askedCount is numFramesToGenerate as the game or the override has it and the provider
    // allows it, it is the most. Returns the count to give Streamline.
    static uint32_t Apply(uint32_t askedCount)
    {
        // The menu sets the options again too, from another thread
        std::scoped_lock lock(_mutex);

        auto config = Config::Instance();
        auto& state = State::Instance();

        const int maxCount = static_cast<int>(askedCount);
        _maxCount = maxCount;
        _lastApplyMs = Util::MillisecondsNow();

        XeFG_DynamicMFG::Settings settings;
        settings.enabled = Supported() && config->FGDLSSGDynamicMFG.value_or_default() &&
                           !config->FGDLSSGOverrideForceDMFG.value_or_default() && maxCount > 1;

        if (!settings.enabled)
        {
            // Lets the controller know it stopped, what it learned stays
            _dynamic.Update(settings, maxCount, _lastCount, false, 0.0, nullptr);
            Stop();
            return askedCount;
        }

        // Frame generation is off or hasn't started: begin at the most, the averages start over once it runs
        const bool fgRunning = state.dlssgDetectedInterpolationCount > 0;

        if (!fgRunning)
        {
            if (_wasRunning)
                _dynamic.Restart();

            Stop();
            return askedCount;
        }

        _wasRunning = true;

        // One decision per real frame, the game and the menu may set the options more often
        const auto frames = _realFrames.load();

        if (_lastCount >= 1 && frames == _decidedFrames)
            return static_cast<uint32_t>(std::clamp(_lastCount, 1, maxCount));

        _decidedFrames = frames;

        settings.targetFps = config->FGDLSSGFramerateTargetDMFG.value_or_default();
        settings.rounding = config->FGDLSSGRoundingDMFG.value_or_default();
        settings.allow1X = false;
        settings.targetAsCap = config->FGDLSSGTargetAsCapDMFG.value_or_default();

        // The swapchain the game presents to may see the generated frames as well, the evaluates know the real ones
        settings.realFrameMs = _realFrameMs;

        // What OptiScaler's FPS limiter slept since the last decision. The low latency layer's sleep is the
        // controller's own to take.
        settings.limiterSleepMs = _limiterSleepMs.exchange(0.0);

        HWND window = state.currentSwapchainDesc.OutputWindow;

        if (window == nullptr)
            window = GameWindow();

        // The game's wait in the present path isn't known here
        const auto result = _dynamic.Update(settings, maxCount, _lastCount, false, 0.0, window);

        _lastCount = std::clamp(result.count, 1, maxCount);

        const bool cap = settings.targetAsCap && result.dynamic && state.xefgDynamicCapFps > 0.0f;
        _capFps = cap ? state.xefgDynamicCapFps : 0.0f;

        if (cap)
            WarnWhenNotHeld(state.xefgDynamicCapFps);
        else
            _overCapSinceMs = 0.0;

        return static_cast<uint32_t>(_lastCount);
    }

  private:
    // Back to what the game asks for, no cap
    static void Stop()
    {
        _lastCount = -1;
        _wasRunning = false;
        _capFps = 0.0f;
        _overCapSinceMs = 0.0;
        _limiterSleepMs = 0.0;
    }

    // The cap relies on a limiter that holds the presented rate at it. Say it when nothing does.
    static void WarnWhenNotHeld(float capFps)
    {
        const double realFrameMs = _realFrameMs;

        if (realFrameMs <= 0.0 || _lastCount < 1)
            return;

        const auto now = Util::MillisecondsNow();
        const double presentedFps = (_lastCount + 1) * 1000.0 / realFrameMs;

        if (presentedFps <= capFps * 1.15)
        {
            _overCapSinceMs = 0.0;
            return;
        }

        if (_overCapSinceMs == 0.0)
            _overCapSinceMs = now;

        if (now - _overCapSinceMs > 10000.0 && now - _lastWarnMs > 60000.0)
        {
            _lastWarnMs = now;
            LOG_WARN("Dynamic MFG cap: ~{:.0f} FPS presented against the {:.0f} FPS cap for {:.0f} s, no FPS limiter "
                     "is holding it",
                     presentedFps, capFps, (now - _overCapSinceMs) / 1000.0);
        }
    }

    // The game's window when the swapchain description has none (Vulkan), looked up now and then
    static HWND GameWindow()
    {
        static HWND window = nullptr;
        static double lookedUpMs = 0.0;
        const auto now = Util::MillisecondsNow();

        if ((window == nullptr || !IsWindow(window)) && now - lookedUpMs > 2000.0)
        {
            window = Util::GetProcessWindow();
            lookedUpMs = now;
        }

        return window;
    }

    inline static std::atomic<double> _lastRealFrameMs { 0.0 };
    inline static std::atomic<double> _realFrameMs { 0.0 };
    inline static std::atomic<uint64_t> _realFrames { 0 };
    inline static std::atomic<int> _maxCount { 0 };
    inline static std::atomic<double> _lastApplyMs { 0.0 };
    inline static std::atomic<float> _capFps { 0.0f };
    inline static std::atomic<double> _limiterSleepMs { 0.0 };

    // Only under the lock
    inline static std::mutex _mutex;
    inline static XeFG_DynamicMFG _dynamic;
    inline static uint64_t _decidedFrames = 0;
    inline static int _lastCount = -1;
    inline static bool _wasRunning = false;
    inline static double _overCapSinceMs = 0.0;
    inline static double _lastWarnMs = 0.0;
};
