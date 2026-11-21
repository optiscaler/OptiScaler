#include "pch.h"
#include "FrameLimit.h"

#include "Config.h"
#include "State.h"
#include <framegen/IFGFeature.h>
#include <framegen/dlssg/DLSSG_NvngxDynamic.h>
// #include "hooks/D3D11Hooks.h"

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

    if (WaitForSingleObject(timer, 1000) != WAIT_OBJECT_0)
        return 3;

    return 0;
};

inline int FrameLimit::busywait_sleep(int64_t ns)
{
    auto current_time = Util::GetTimestamp();
    auto wait_until = current_time + ns;
    while (current_time < wait_until)
    {
        current_time = Util::GetTimestamp();
    }
    return 0;
}

inline int FrameLimit::combined_sleep(int64_t ns)
{
    constexpr int64_t busywait_threshold = 2'000'000; // 2ms
    int status {};
    auto current_time = Util::GetTimestamp();
    if (ns <= busywait_threshold)
        status = busywait_sleep(ns);
    else
        status = timer_sleep((ns - busywait_threshold) / 100);

    // Busy wait also covers a failed timer
    if (int64_t sleep_deviation = ns - (Util::GetTimestamp() - current_time); sleep_deviation > 0)
        busywait_sleep(sleep_deviation);

    return status;
}

float FrameLimit::targetFps()
{
    auto& state = State::Instance();
    auto fpsLimit = Config::Instance()->FramerateLimit.value_or_default();

    // Dynamic MFG's cap, the XeFG and the DLSSG outputs have one
    const bool dynamicCapOutput = state.activeFgOutput == FGOutput::XeFG || state.activeFgOutput == FGOutput::DLSSG;

    if (auto fg = state.currentFG; fg != nullptr && fg->IsActive() && !fg->IsPaused() && dynamicCapOutput &&
                                   state.xefgDynamicCapFps > 0.0f &&
                                   (fpsLimit <= 0.0f || state.xefgDynamicCapFps < fpsLimit))
    {
        return state.xefgDynamicCapFps;
    }

    // The same for the game's own DLSSG through the custom nvngx replacement, there is no FG object for it
    if (const float nvngxCap = DlssgNvngxDynamic::CapFps();
        nvngxCap > 0.0f && (fpsLimit <= 0.0f || nvngxCap < fpsLimit))
    {
        return nvngxCap;
    }

    return fpsLimit;
}

void FrameLimit::sleep(bool fgActive)
{
    if (auto fpsCap = targetFps(); fpsCap > 0.0f)
    {
        uint64_t min_interval_us = std::clamp((uint64_t) (1'000'000 / fpsCap), 0ULL, 100'000'000ULL);

        // The limit counts presented frames, the real frames come once per batch
        if (fgActive)
        {
            auto fg = State::Instance().CurrentFG();
            min_interval_us *= fg != nullptr ? fg->GetInterpolatedFrameCount() + 1 : 2;
        }

        static uint64_t previous_frame_time = 0;
        uint64_t current_time = Util::GetTimestamp();
        uint64_t frame_time = current_time - previous_frame_time;

        State::Instance().lastFrameLimitSleepMs = 0.0;

        if (frame_time < 1000 * min_interval_us)
        {
            static bool errorLogged = false;

            if (auto res = combined_sleep(min_interval_us * 1000 - frame_time); res && !errorLogged)
            {
                LOG_ERROR("Sleep command failed: {}", res);
                errorLogged = true;
            }

            // Dynamic MFG's cap counts it as time the game could have used
            const double sleptMs = (Util::GetTimestamp() - current_time) / 1'000'000.0;
            State::Instance().lastFrameLimitSleepMs = sleptMs;
            DlssgNvngxDynamic::AddLimiterSleep(sleptMs);
        }
        previous_frame_time = Util::GetTimestamp();
    }
}
