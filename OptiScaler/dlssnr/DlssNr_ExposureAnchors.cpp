#include "pch.h"

#include "DlssNr_ExposureScan.h"

#include <Config.h>
#include <Util.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

namespace DlssNr::ExposureScan
{
namespace { constexpr float kFloor = 1e-6f; constexpr float kCeiling = 1e4f; }
namespace
{
std::mutex g_anchorMutex;
std::vector<AnchorPoint> g_anchors;   // guarded by g_anchorMutex, kept sorted by scan ascending

void SortAnchorsLocked()
{
    std::sort(g_anchors.begin(), g_anchors.end(),
              [](const AnchorPoint& a, const AnchorPoint& b) { return a.scan < b.scan; });
}
}  // namespace

// Load the persisted table (or migrate a pre-existing single anchor) exactly once, before any lock
// is taken -- LoadAnchors/AnchorAdd take g_anchorMutex themselves, so this must not hold it.
void EnsureAnchorsLoaded()
{
    static std::once_flag once;
    std::call_once(once, [] {
        auto& cfg = *Config::Instance();
        const std::string ser = cfg.DlssNrScanAnchors.value_or_default();

        if (!ser.empty())
        {
            LoadAnchors(ser);
            return;
        }

        // Migration: fold a single-anchor ini from before this feature into a one-row table so the
        // user does not lose the calibration they already set.
        const float v = cfg.DlssNrScanAnchorValue.value_or_default();
        const float w = cfg.DlssNrScanAnchorWhitePoint.value_or_default();

        if (v > kFloor && w > 1e-6f)
            AnchorAdd(v, w);
    });
}

std::vector<AnchorPoint> Anchors()
{
    EnsureAnchorsLoaded();
    std::lock_guard<std::mutex> lock(g_anchorMutex);
    return g_anchors;
}

bool AnchorAdd(float scan, float white)
{
    if (!(scan > kFloor && scan < kCeiling) || !(white > 1e-6f))
        return false;

    std::lock_guard<std::mutex> lock(g_anchorMutex);

    // A near-duplicate scan value would make log(v_{k+1}) - log(v_k) ~ 0 and divide the interpolation
    // by zero. Replace the existing point's white instead of adding a second at the same place.
    for (auto& p : g_anchors)
    {
        if (scan > p.scan * 0.98f && scan < p.scan * 1.02f)
        {
            p.white = white;
            return true;
        }
    }

    if (g_anchors.size() >= 8)
        return false;

    g_anchors.push_back({ scan, white });
    SortAnchorsLocked();
    return true;
}

void AnchorSetWhite(int index, float white)
{
    std::lock_guard<std::mutex> lock(g_anchorMutex);

    if (index >= 0 && index < (int) g_anchors.size() && white > 1e-6f)
        g_anchors[index].white = white;
}

void AnchorRemove(int index)
{
    std::lock_guard<std::mutex> lock(g_anchorMutex);

    if (index >= 0 && index < (int) g_anchors.size())
        g_anchors.erase(g_anchors.begin() + index);
}

void AnchorClear()
{
    std::lock_guard<std::mutex> lock(g_anchorMutex);
    g_anchors.clear();
}

float AnchoredWhitePoint(float scanNow, bool inverted, float trim)
{
    EnsureAnchorsLoaded();
    trim = std::clamp(trim, 0.25f, 4.0f);

    std::lock_guard<std::mutex> lock(g_anchorMutex);

    if (g_anchors.empty() || !(scanNow > kFloor))
        return 0.0f;

    // One point is the original ratio law, and it needs the direction the two-point case reads off
    // the data instead.
    if (g_anchors.size() == 1)
    {
        const AnchorPoint& p = g_anchors[0];

        if (!(p.scan > kFloor) || !(p.white > 1e-6f))
            return 0.0f;

        const float ratio = inverted ? scanNow / p.scan : p.scan / scanNow;
        return std::clamp(p.white * ratio * trim, 0.01f, 4096.0f);
    }

    // Clamp at the ends: holding the outermost calibrated white point beats extrapolating a ratio
    // into a scene darker or brighter than anything the user calibrated.
    if (scanNow <= g_anchors.front().scan)
        return std::clamp(g_anchors.front().white * trim, 0.01f, 4096.0f);

    if (scanNow >= g_anchors.back().scan)
        return std::clamp(g_anchors.back().white * trim, 0.01f, 4096.0f);

    // Between two points: interpolate log(white) against log(scan). A white point is a divisor, so
    // log is the space in which it is linear -- and it makes the two-point case reproduce the exact
    // ratio law of the single-point case, so adding a second point never causes a jump.
    for (size_t k = 0; k + 1 < g_anchors.size(); ++k)
    {
        const AnchorPoint& a = g_anchors[k];
        const AnchorPoint& b = g_anchors[k + 1];

        if (scanNow >= a.scan && scanNow <= b.scan && b.scan > a.scan * 1.0001f)
        {
            const float t = (std::log(scanNow) - std::log(a.scan)) / (std::log(b.scan) - std::log(a.scan));
            const float w = std::exp(std::log(a.white) + t * (std::log(b.white) - std::log(a.white)));
            return std::clamp(w * trim, 0.01f, 4096.0f);
        }
    }

    return std::clamp(g_anchors.back().white * trim, 0.01f, 4096.0f);
}

void LoadAnchors(const std::string& serialized)
{
    std::vector<AnchorPoint> parsed;
    size_t i = 0;

    while (i < serialized.size() && parsed.size() < 8)
    {
        size_t semi = serialized.find(';', i);
        std::string tok = serialized.substr(i, semi == std::string::npos ? std::string::npos : semi - i);
        i = semi == std::string::npos ? serialized.size() : semi + 1;

        const size_t colon = tok.find(':');
        if (colon == std::string::npos)
            continue;

        try
        {
            const float v = std::stof(tok.substr(0, colon));
            const float w = std::stof(tok.substr(colon + 1));

            if (v > kFloor && v < kCeiling && w > 1e-6f)
                parsed.push_back({ v, w });
        }
        catch (...)
        {
            // A malformed token is skipped rather than aborting the whole table.
        }
    }

    std::lock_guard<std::mutex> lock(g_anchorMutex);
    g_anchors = std::move(parsed);
    SortAnchorsLocked();
}

std::string SerializeAnchors()
{
    std::lock_guard<std::mutex> lock(g_anchorMutex);

    std::string out;
    char buf[64];

    for (const AnchorPoint& p : g_anchors)
    {
        snprintf(buf, sizeof(buf), "%.6g:%.6g;", p.scan, p.white);
        out += buf;
    }

    return out;
}

}
