#include "pch.h"

#include "DlssNr_ExposureScan_Internal.h"

#include <Config.h>
#include <Util.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

namespace DlssNr
{
namespace ExposureScan
{
namespace Detail
{

// Formats an exposure could plausibly be in: floating point, one or two channels.
//
// Two channels because eye adaptation commonly carries the value and something alongside it -- the
// previous frame's value, or a target it is easing toward. Anything wider is a picture rather than a
// number. Integer formats are excluded because an exposure is a scale and a normalised integer
// cannot hold one.
// outBytes is the size of the FIRST channel only -- that is the one texel this reads back, and a
// two-channel format that stored 4 or 8 there would be decoded as the wrong type. The copy uses the
// real format (outFormat) so it matches the source texture; the read uses outBytes.
bool PlausibleFormat(DXGI_FORMAT f, unsigned int* outBytes, const char** outName, DXGI_FORMAT* outFormat)
{
    *outFormat = f;
    switch (f)
    {
    case DXGI_FORMAT_R32_FLOAT:
        *outBytes = 4;
        *outName = "R32_FLOAT";
        return true;
    case DXGI_FORMAT_R16_FLOAT:
        *outBytes = 2;
        *outName = "R16_FLOAT";
        return true;
    case DXGI_FORMAT_R32G32_FLOAT:
        *outBytes = 4;
        *outName = "R32G32_FLOAT";
        return true;
    case DXGI_FORMAT_R16G16_FLOAT:
        *outBytes = 2;
        *outName = "R16G16_FLOAT";
        return true;
    default:
        return false;
    }
}

bool Wanted()
{
    return Config::Instance()->DlssNrWhitePointSource.value_or_default() == 2 ||
           Config::Instance()->DlssNrScanExposure.value_or_default();
}

} // namespace Detail
using namespace Detail;

// Everything the two entry points share: does this description look like a number rather than a
// picture, and if so what is it.
//
// Widened from the first attempt, which asked for at most 4x4 and one or two channels and found
// nothing anywhere. That was tuned on what an exposure buffer ought to look like rather than on what
// engines actually allocate: some keep a small histogram beside the value, some keep a few frames of
// history, and some put the whole thing in a four-channel texture and use one channel. The filter
// only has to be tight enough that the list stays readable.
bool LooksLikeANumber(const D3D12_RESOURCE_DESC& rd, std::string* outShape, unsigned int* outBytes,
                      bool* outIsBuffer, DXGI_FORMAT* outFormat)
{
    // An exposure is computed, so it is written by a shader. This is the one condition worth being
    // strict about: it removes almost everything without removing anything that could be the answer.
    if ((rd.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
        return false;

    if (rd.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        // 256 texels rather than 16. A 16x16 texture is still a number by any reasonable measure and
        // a 256-bin histogram is exactly how a lot of engines compute one.
        if (rd.Width * rd.Height > 256 || rd.Width == 0 || rd.Height == 0)
            return false;

        const char* name = nullptr;

        if (!PlausibleFormat(rd.Format, outBytes, &name, outFormat))
            return false;

        *outIsBuffer = false;
        *outShape = std::to_string((unsigned int) rd.Width) + "x" + std::to_string(rd.Height) + " " + name;
        return true;
    }

    if (rd.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        *outFormat = DXGI_FORMAT_UNKNOWN;

        // Unreal moved eye adaptation off a texture and onto a buffer, so buffers have to be in
        // scope or a whole engine's worth of games is invisible.
        //
        // 128 bytes, down from 4kB. The wider bound filled all twelve slots in Nioh 3 with 256, 512
        // and 768 byte buffers that never held anything but zero, and the real answer -- eight bytes
        // -- only made the list because it happened to be created early. A cap that can be filled by
        // junk is a cap that can hide the answer.
        if (rd.Width == 0 || rd.Width > 128)
            return false;

        *outIsBuffer = true;
        *outBytes = 4;
        *outShape = "buffer, " + std::to_string((unsigned int) rd.Width) + " bytes";
        return true;
    }

    return false;
}

void Adopt(ID3D12Resource* resource, const std::string& shape, unsigned int bytes, bool isBuffer,
           DXGI_FORMAT texFormat)
{
    ID3D12Device* resourceDevice = nullptr;
    if (FAILED(resource->GetDevice(IID_PPV_ARGS(&resourceDevice))) || resourceDevice == nullptr)
        return;
    const bool differentDevice = g_scan.device != nullptr && g_scan.device != resourceDevice;
    resourceDevice->Release();
    if (differentDevice)
        return;

    for (const Tracked& t : g_scan.tracked)
    {
        if (t.resource == resource)
            return;
    }

    if (g_scan.tracked.size() >= kMaxCandidates)
    {
        if (!g_scan.complained)
        {
            g_scan.complained = true;
            LOG_WARN("DLSS-NR exposure scan: more than {} candidates, so the filter is too loose here "
                     "rather than the game having {} exposures",
                     kMaxCandidates, kMaxCandidates);
        }

        return;
    }

    Tracked t;
    t.resource = resource;
    t.device = resourceDevice;
    t.shape = shape;
    t.isBuffer = isBuffer;
    t.bytes = bytes;
    t.texFormat = texFormat;
    resource->AddRef();

    g_scan.tracked.push_back(t);

    LOG_INFO("DLSS-NR exposure scan: candidate {} -- {}", g_scan.tracked.size(), shape);
}

void NoteResource(const D3D12_RESOURCE_DESC* desc, ID3D12Resource* resource)
{
    if (!Config::Instance()->DlssNrEnabled.value_or_default())
        return;

    if (desc == nullptr || resource == nullptr)
        return;

    std::string shape;
    unsigned int bytes = 4;
    bool isBuffer = false;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;

    std::lock_guard<std::mutex> lock(g_scanMutex);
    g_scan.examined++;

    if (!LooksLikeANumber(*desc, &shape, &bytes, &isBuffer, &fmt))
    {
        // Near-miss diagnostic. A resource a shader writes (UAV) that the filter rejected: logging its
        // shape -- bounded to the first 40 so it cannot flood -- reveals whether a game the scan finds
        // nothing in (Cyberpunk 2077) has an exposure the filter narrowly misses (a small UAV texture
        // in an unlisted format, or a UAV buffer just over 128 bytes -> widen precisely to match) or
        // nothing scannable at all (only large buffers/textures -> the exposure is baked in a bigger
        // buffer and no filter change can help). Read these against Examined() in the log.
        if ((desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 && g_scan.nearMissLogged < 40)
        {
            g_scan.nearMissLogged++;
            LOG_INFO("DLSS-NR scan near-miss #{}: UAV dim {} {}x{}x{} fmt {} (filter rejected)",
                     g_scan.nearMissLogged, (int) desc->Dimension, (unsigned int) desc->Width,
                     desc->Height, desc->DepthOrArraySize, (int) desc->Format);
        }

        return;
    }

    Adopt(resource, shape, bytes, isBuffer, fmt);
}

unsigned int Examined()
{
    std::lock_guard<std::mutex> lock(g_scanMutex);
    return g_scan.examined;
}

void NoteUav(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc)
{
    // Deliberately NOT gated on the scan setting, and that was a real bug rather than a nicety.
    //
    // An engine creates its eye adaptation view once, when it builds its render targets, which is
    // long before anybody opens a menu and ticks a box. Gating the recording meant every candidate
    // was thrown away before the scan could want it, and the readout then said "nothing matched
    // yet -- play for a few seconds", which is advice that could never come true no matter how long
    // anyone played.
    //
    // Recording is a resource description and a pointer. What is genuinely risky -- reading a buffer
    // the game owns, on an assumption about its state -- lives in Tick, and that is still gated.
    if (!Config::Instance()->DlssNrEnabled.value_or_default())
        return;

    if (resource == nullptr)
        return;

    const D3D12_RESOURCE_DESC rd = resource->GetDesc();

    std::string shape;
    unsigned int bytes = 4;
    bool isBuffer = false;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;

    std::lock_guard<std::mutex> lock(g_scanMutex);
    g_scan.examined++;

    if (!LooksLikeANumber(rd, &shape, &bytes, &isBuffer, &fmt))
        return;

    Adopt(resource, shape, bytes, isBuffer, fmt);
}

// How many frames of watching without movement before saying so. At sixty frames a second this is
// about half a minute, which is long enough to have walked somewhere with different light in it and
// short enough that nobody waits on it wondering.
constexpr unsigned int kPatience = 1800;

Verdict Where()
{
    if (!Wanted())
        return Verdict::Off;

    std::lock_guard<std::mutex> lock(g_scanMutex);

    if (g_scan.tracked.empty())
        return Verdict::Waiting;

    unsigned int mostReads = 0;

    for (const Tracked& t : g_scan.tracked)
    {
        if (t.moves)
            return Verdict::Found;

        mostReads = std::max(mostReads, t.reads);
    }

    return mostReads >= kPatience ? Verdict::Barren : Verdict::Watching;
}

const char* Headline()
{
    static std::string line;

    switch (Where())
    {
    case Verdict::Off:
        line = "";
        break;

    case Verdict::Waiting:
    {
        // The examined count is the whole diagnosis. Zero means the hook is not running and no
        // amount of playing will change that; a large number means the game genuinely has nothing
        // shaped like an exposure, which is an answer rather than a failure.
        const unsigned int seen = Examined();
        line = seen == 0 ? "DLSS-NR exposure scan: NOT RUNNING -- no resources seen at all"
                         : "DLSS-NR exposure scan: examined " + std::to_string(seen) +
                               " resources, none shaped like an exposure";
        break;
    }

    case Verdict::Watching:
    {
        std::lock_guard<std::mutex> lock(g_scanMutex);
        unsigned int mostReads = 0;

        for (const Tracked& t : g_scan.tracked)
            mostReads = std::max(mostReads, t.reads);

        line = "DLSS-NR exposure scan: watching " + std::to_string(g_scan.tracked.size()) +
               ", none moving yet -- walk between light and shade  (" +
               std::to_string(mostReads * 100 / kPatience) + "%)";
        break;
    }

    case Verdict::Found:
    {
        std::lock_guard<std::mutex> lock(g_scanMutex);

        // The widest travel wins where several move. An exposure swings by orders of magnitude
        // between a dark interior and open daylight; anything that merely wobbles is something else.
        size_t best = 0;
        float bestRatio = 0.0f;

        for (size_t i = 0; i < g_scan.tracked.size(); ++i)
        {
            const Tracked& t = g_scan.tracked[i];

            if (!t.moves || t.lowest <= kFloor)
                continue;

            const float ratio = t.highest / t.lowest;

            if (ratio > bestRatio)
            {
                bestRatio = ratio;
                best = i;
            }
        }

        char buf[192];
        // The live value is in here so the line visibly ticks. Without it the indicator looks stuck
        // the moment the range settles, which is exactly when it has succeeded.
        snprintf(buf, sizeof(buf),
                 "DLSS-NR exposure scan: FOUND -- candidate %zu = %.5f  (%.5f..%.5f, x%.0f)  done",
                 best + 1, g_scan.tracked[best].latest, g_scan.tracked[best].lowest,
                 g_scan.tracked[best].highest, bestRatio);
        line = buf;
        break;
    }

    case Verdict::Barren:
        line = "DLSS-NR exposure scan: nothing moved. No exposure to find here.";
        break;
    }

    return line.c_str();
}

float BestValue(int* outIndex, float* outLowest, float* outHighest)
{
    std::lock_guard<std::mutex> lock(g_scanMutex);

    int best = -1;
    float bestRatio = 0.0f;

    for (size_t i = 0; i < g_scan.tracked.size(); ++i)
    {
        const Tracked& t = g_scan.tracked[i];

        if (!t.moves || t.lowest <= kFloor)
            continue;

        const float ratio = t.highest / t.lowest;

        if (ratio > bestRatio)
        {
            bestRatio = ratio;
            best = (int) i;
        }
    }

    if (best < 0)
        return 0.0f;

    if (outIndex != nullptr)
        *outIndex = best + 1;

    if (outLowest != nullptr)
        *outLowest = g_scan.tracked[best].lowest;

    if (outHighest != nullptr)
        *outHighest = g_scan.tracked[best].highest;

    return g_scan.tracked[best].latest;
}

std::vector<Candidate> Report()
{
    std::lock_guard<std::mutex> lock(g_scanMutex);

    std::vector<Candidate> out;
    out.reserve(g_scan.tracked.size());

    for (const Tracked& t : g_scan.tracked)
    {
        Candidate c;
        c.shape = t.shape;
        c.latest = t.latest;
        c.lowest = t.lowest;
        c.highest = t.highest;
        c.reads = t.reads;
        c.moves = t.moves;
        out.push_back(c);
    }

    return out;
}

const char* Status()
{
    std::lock_guard<std::mutex> lock(g_scanMutex);
    return g_scan.status;
}

bool Scanning() { return Wanted(); }

} // namespace ExposureScan
} // namespace DlssNr
