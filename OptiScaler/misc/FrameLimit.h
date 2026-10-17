#pragma once
#include "SysUtils.h"

class FrameLimit
{
    static int timer_sleep(int64_t hundred_ns);
    static int busywait_sleep(int64_t ns);
    static int combined_sleep(int64_t ns);

  public:
    static void sleep(bool fgActive);

    // The presented frame rate to limit to, 0 = none: the FPS limit, or XeFG's Dynamic MFG target while it runs with
    // "Use target as cap" when that is lower. Every limiter path (Reflex, XeLL, the low latency outputs, this one)
    // uses it.
    static float targetFps();
};
