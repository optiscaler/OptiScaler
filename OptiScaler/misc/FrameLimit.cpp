#include "pch.h"
#include "FrameLimit.h"

#include "Config.h"
// #include "hooks/D3D11Hooks.h"

inline uint64_t FrameLimit::get_timestamp()
{
    FILETIME fileTime;
    GetSystemTimePreciseAsFileTime(&fileTime);

    uint64_t time = (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;

    return time * 100;
}

// https://learn.microsoft.com/en-us/windows/win32/sync/using-waitable-timer-objects
inline int FrameLimit::timer_sleep(int64_t hundred_ns)
{
    static HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due_time;

    due_time.QuadPart = -hundred_ns;

    if (!timer)
        return 1;

    if (!SetWaitableTimerEx(timer, &due_time, 0, NULL, NULL, NULL, 0))
        return 2;

    if (WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0)
        return 3;

    return 0;
};

inline int FrameLimit::busywait_sleep(int64_t ns)
{
    auto current_time = get_timestamp();
    auto wait_until = current_time + ns;
    while (current_time < wait_until)
    {
        current_time = get_timestamp();
    }
    return 0;
}

inline int FrameLimit::combined_sleep(int64_t ns)
{
    constexpr int64_t busywait_threshold = 2'000'000; // 2ms
    int status {};
    auto current_time = get_timestamp();
    if (ns <= busywait_threshold)
        status = busywait_sleep(ns);
    else
        status = timer_sleep((ns - busywait_threshold) / 100);

    // Busy wait also covers a failed timer
    if (int64_t sleep_deviation = ns - (get_timestamp() - current_time); sleep_deviation > 0)
        busywait_sleep(sleep_deviation);

    return status;
}

void FrameLimit::sleep(bool fgActive)
{
    if (auto fpsCap = Config::Instance()->FramerateLimit.value_or_default(); fpsCap > 0.0f)
    {
        uint64_t min_interval_us = std::clamp((uint64_t) (1'000'000 / fpsCap), 0ULL, 100'000'000ULL);

        if (fgActive)
            min_interval_us *= 2;

        static uint64_t previous_frame_time = 0;
        uint64_t current_time = get_timestamp();
        uint64_t frame_time = current_time - previous_frame_time;
        if (frame_time < 1000 * min_interval_us)
        {
            static bool errorLogged = false;

            if (auto res = combined_sleep(min_interval_us * 1000 - frame_time); res && !errorLogged)
            {
                LOG_ERROR("Sleep command failed: {}", res);
                errorLogged = true;
            }
        }
        previous_frame_time = get_timestamp();
    }
}
