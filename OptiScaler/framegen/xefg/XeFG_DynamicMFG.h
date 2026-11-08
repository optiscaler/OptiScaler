#pragma once

#include "XeFG_DynamicCount.h"
#include "XeFG_DynamicCap.h"

#include <Windows.h>

// Dynamic MFG for an FG output (XeFG, DLSSG): it passes the wait it measured and applies the result its own way
class XeFG_DynamicMFG
{
  public:
    struct Result
    {
        bool dynamic = false;       // Dynamic MFG runs this frame
        bool ownChange = false;     // it ran last frame too, a count change is its own and not the user's
        int count = 1;              // interpolated frames to use, kept while generation is off
        bool generationOff = false; // 1X: no frames generated
    };

    // What the output's user set. The XeFG output reads them from its own config, other outputs fill them in.
    struct Settings
    {
        bool enabled = false;     // Dynamic MFG runs
        float targetFps = 0.0f;   // 0 = the FPS limit, else just under the refresh rate
        int rounding = 1;         // 0 at most, 1 around, 2 at least the target
        bool allow1X = true;      // may turn frame generation off (1X), for an output that can do it cheaply
        bool targetAsCap = false; // the target is also the FPS limit

        // OptiScaler's own FPS limiter's sleep in the game's last present. The cap counts it as a limiter sleep. The
        // XeFG output leaves it at 0, its limiter is a low latency sleep that is measured already.
        double limiterSleepMs = 0.0;
    };

    // XeFG output: the settings come from its config
    // presentWaitMs: what the game waited in the output's present path this frame
    Result Update(int targetCount, int framesToInterpolate, bool generationOff, double presentWaitMs, HWND window);

    // Any output
    Result Update(const Settings& settings, int targetCount, int framesToInterpolate, bool generationOff,
                  double presentWaitMs, HWND window);

    // FG back on: the averages start over, what was learned stays
    void Restart();

  private:
    XeFG_DynamicCount _count;
    XeFG_DynamicCap _cap; // with "Use target as cap": holds the target, no rounding band, no 1X
    bool _active = false;
    bool _capActive = false;
    double _logMs = 0.0;
    int _refreshRate = 0;
    double _refreshCheckedMs = 0.0;
    bool _unfocused = false; // the game's window was in the background last frame

    // 1: the game is in front, 0: in the background, -1: no foreground window
    static int Focused(HWND window);
    float DisplayRefreshFps(double nowMs, HWND window);
    float TargetFps(double nowMs, HWND window, float setTarget);
};
