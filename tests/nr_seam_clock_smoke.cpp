// Headless sequence regression for PR #11; no game or NVIDIA runtime is loaded.
#include "../OptiScaler/shaders/dlssnr/DlssNr_SeamClock.h"
#include "../OptiScaler/dlssnr/DlssNr_Placement.h"
#include <cassert>
#include <cstdio>

int main()
{
    // One deferred option selects private SR regardless of whether the game uses RR.
    struct Case { bool before, deferred, legacy, finished, wantsBefore, wantsDeferred; };
    const Case cases[] = {
        { false, false, false, false, false, false },
        { true, false, false, false, true, false },
        { false, true, false, false, true, true },
        { false, true, false, true, true, true },
        { false, false, false, true, false, false },
        { true, false, false, true, true, true },
        { true, false, true, false, true, true },
        { true, false, true, true, true, true },
        { false, false, true, false, false, false },
    };
    for (const auto& c : cases)
    {
        const auto p = DlssNr::ResolvePlacement(c.before, c.deferred, c.legacy, c.finished);
        assert(p.beforeUpscale == c.wantsBefore && p.deferred == c.wantsDeferred && p.finished == c.finished);
    }
    std::puts("PASS: unified early-edit routing, final-picture composition and legacy INI alias");
    DlssNrSeamClock clock;
    assert(clock.AtSeam(true, false, 10) == 10);
    // Present ticks between the paired calls: keep identity, then advance once next frame.
    assert(clock.AtSeam(false, false, 11) == 10);
    assert(clock.AtSeam(true, false, 11) == 11);
    assert(clock.AtSeam(false, false, 11) == 11);
    // A stalled Present counter must not look like a duplicate native evaluate.
    assert(clock.AtSeam(true, false, 11) == 12);
    assert(clock.AtSeam(false, false, 12) == 12);
    assert(clock.AtSeam(true, false, 12) == 13);
    // A genuine gap and a swapchain counter reset remain monotonic.
    assert(clock.AtSeam(true, false, 20) == 20);
    assert(clock.AtSeam(false, false, 21) == 20);
    assert(clock.AtSeam(true, false, 0) == 21);
    // Bridges retain their own epoch, including duplicate evaluations.
    assert(clock.AtSeam(true, true, 7) == 7);
    assert(clock.AtSeam(true, true, 7) == 7);
    assert(clock.AtSeam(false, true, 7) == 7);
    assert(clock.AtSeam(true, true, 8) == 8);
    std::puts("PASS: deferred seam pairing, stalls, gaps, resets, and bridge epochs");
}
