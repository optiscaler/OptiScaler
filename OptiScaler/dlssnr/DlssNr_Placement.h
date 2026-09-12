#pragma once

namespace DlssNr
{
struct Placement
{
    bool beforeUpscale;
    bool deferred;
    bool finished;
};

// The old across-RR key is accepted as an alias for the unified private-upscaler route.
constexpr Placement ResolvePlacement(bool before, bool deferred, bool legacyAcrossRr, bool finished)
{
    deferred = deferred || (before && legacyAcrossRr) || (finished && before);
    return { before || deferred, deferred, finished };
}
} // namespace DlssNr
