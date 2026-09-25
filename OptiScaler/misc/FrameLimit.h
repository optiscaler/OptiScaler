#pragma once
#include "SysUtils.h"

class FrameLimit
{
    static int timer_sleep(int64_t hundred_ns);
    static int busywait_sleep(int64_t ns);
    static int combined_sleep(int64_t ns);

  public:
    static void sleep(bool fgActive);
};
