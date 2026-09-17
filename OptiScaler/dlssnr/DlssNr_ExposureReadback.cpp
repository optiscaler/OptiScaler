#include "pch.h"

#include "DlssNr_ExposureScan_Internal.h"

#include <Config.h>
#include <Util.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

namespace DlssNr::ExposureScan
{
namespace Detail
{
ScanState g_scan;
std::mutex g_scanMutex;
std::mutex g_tickMutex;
float HalfToFloat(uint16_t h)
{
    const uint32_t sign = (uint32_t) (h & 0x8000u) << 16;
    uint32_t exponent = (h >> 10) & 0x1Fu;
    uint32_t mantissa = h & 0x3FFu;

    if (exponent == 0)
    {
        if (mantissa == 0)
        {
            const uint32_t bits = sign;
            float out;
            std::memcpy(&out, &bits, sizeof(out));
            return out;
        }

        // Subnormal: normalise it by hand.
        exponent = 1;

        while ((mantissa & 0x400u) == 0)
        {
            mantissa <<= 1;
            --exponent;
        }

        mantissa &= 0x3FFu;
    }
    else if (exponent == 31)
    {
        const uint32_t bits = sign | 0x7F800000u | (mantissa << 13);
        float out;
        std::memcpy(&out, &bits, sizeof(out));
        return out;
    }

    const uint32_t bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    float out;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

void Barrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* res, D3D12_RESOURCE_STATES from,
             D3D12_RESOURCE_STATES to)
{
    if (res == nullptr || from == to)
        return;

    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = res;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = from;
    barrier.Transition.StateAfter = to;
    cmdList->ResourceBarrier(1, &barrier);
}

bool EnsureReadback(ID3D12Device* device)
{
    for (unsigned int i = 0; i < kSlots; ++i)
    {
        if (g_scan.readback[i] != nullptr)
            continue;

        D3D12_HEAP_PROPERTIES heap {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kStride * kMaxCandidates;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(&g_scan.readback[i]))))
        {
            std::lock_guard<std::mutex> lock(g_scanMutex);
            g_scan.status = "could not allocate the readback buffers";
            return false;
        }
    }

    return true;
}

// Whether the scan should be running at all.
//
// Choosing it as the white point's source is the whole of the answer for anybody using this.
// The separate setting survives as a developer override, for the one case a user has no reason
// to want: watching the scan in a game that supplies a REAL exposure, so the two can be
// compared in the log. That is validation work, not a control, and it does not belong in a
// panel.
}
using namespace Detail;
void Tick(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, uint64_t submissionEpoch)
{
    if (!Wanted())
        return;

    if (device == nullptr || cmdList == nullptr)
        return;

    std::lock_guard<std::mutex> tickLock(g_tickMutex);
    {
        std::lock_guard<std::mutex> lock(g_scanMutex);
        if (g_scan.device != nullptr && g_scan.device != device)
            return;
        if (submissionEpoch != UINT64_MAX && submissionEpoch == g_scan.lastEpoch)
            return;
        if (g_scan.device == nullptr)
        {
            g_scan.device = device;
            device->AddRef();
            // Discovery starts before a rendering device is known. Never record a copy of a
            // resource discovered on another device into this device's command list.
            std::erase_if(g_scan.tracked, [device](const Tracked& candidate)
            {
                if (candidate.device == device)
                    return false;
                candidate.resource->Release();
                return true;
            });
        }
    }

    // Outside g_scanMutex, deliberately. EnsureReadback creates a committed resource, and that call is
    // detoured to hkCreateCommittedResource -> NoteResource, which takes g_scanMutex. Holding it
    // here would be a self-deadlock. g_tickMutex serializes different NR owners and shutdown.
    if (!EnsureReadback(device))
        return;

    std::lock_guard<std::mutex> lock(g_scanMutex);

    if (g_scan.tracked.empty())
    {
        g_scan.status = "no buffer in this game is shaped like an exposure";
        return;
    }
    g_scan.lastEpoch = submissionEpoch;

    // Read the slot written four frames ago before overwriting it. Retired by now, so this reads
    // mapped memory rather than waiting on the GPU.
    if (g_scan.frames >= kSlots)
    {
        ID3D12Resource* old = g_scan.readback[g_scan.frames % kSlots];
        void* mapped = nullptr;
        D3D12_RANGE range { 0, kStride * kMaxCandidates };

        if (old != nullptr && SUCCEEDED(old->Map(0, &range, &mapped)) && mapped != nullptr)
        {
            const unsigned char* base = (const unsigned char*) mapped;

            const auto readableCount = std::min(g_scan.tracked.size(), g_scan.readbackCounts[g_scan.frames % kSlots]);
            for (size_t i = 0; i < readableCount; ++i)
            {
                Tracked& t = g_scan.tracked[i];
                const unsigned char* at = base + i * kStride;

                float value = 0.0f;

                if (t.bytes == 2)
                {
                    uint16_t half = 0;
                    std::memcpy(&half, at, sizeof(half));
                    value = HalfToFloat(half);
                }
                else
                {
                    std::memcpy(&value, at, sizeof(value));
                }

                if (!std::isfinite(value))
                    continue;

                // Only values that could BE an exposure are allowed into the range, and this is
                // the whole of "8 watching, none moving" never changing.
                //
                // A buffer is usually zero the first time it is read -- created but not yet
                // written, or read a frame before the game fills it. That zero became `lowest`,
                // and since movement is a ratio guarded by `lowest > kFloor`, one early zero
                // disqualified that candidate for the rest of the session however the light
                // changed. The range has to be built from plausible samples, not from whichever
                // sample happened to be first.
                if (value <= kFloor || value >= kCeiling)
                {
                    t.latest = value;
                    t.reads++;
                    continue;
                }

                if (t.inRange == 0)
                {
                    t.lowest = value;
                    t.highest = value;
                }
                else
                {
                    t.lowest = std::min(t.lowest, value);
                    t.highest = std::max(t.highest, value);
                }

                t.inRange++;

                // "Moves" is the whole point of the readout, and the first version of this test
                // was wrong in a way that mattered: a spread of ten percent of the highest value
                // seen is a threshold of zero when the highest value seen is zero, so three buffers
                // sitting at 0.00000 with float noise around them all reported MOVES.
                //
                // Ratios, not differences, and only over values that could be an exposure at all.
                // An exposure is positive, is not a thousandth of a thousandth, and does not sit at
                // a million. Nioh 3's real one runs 0.0019 to 0.616 -- a factor of three hundred --
                // so a quarter is a low bar that noise cannot reach.
                if (t.inRange > 1 && t.highest > t.lowest * 1.25f)
                    t.moves = true;

                t.latest = value;
                t.reads++;
            }

            D3D12_RANGE nothingWritten { 0, 0 };
            old->Unmap(0, &nothingWritten);
        }
    }

    // Periodic movement readout. The menu's Advanced panel shows which candidate tracks the light, but
    // the log did not -- so a game the scan is being taught (Cyberpunk) could not be cracked from a log
    // alone. Every ~300 frames, name the candidates that MOVE and their travel: the exposure is the one
    // that swings widely between bright and dark. Throttled, and only while the scan is wanted.
    if (g_scan.frames > 0 && g_scan.frames % 300 == 0)
    {
        unsigned int movers = 0;

        for (size_t i = 0; i < g_scan.tracked.size(); ++i)
        {
            const Tracked& t = g_scan.tracked[i];

            if (!t.moves)
                continue;

            movers++;
            LOG_INFO("DLSS-NR scan mover: candidate {} ({}) range {:.5f}..{:.5f} (x{:.1f}), latest {:.5f}",
                     (unsigned int) (i + 1), t.shape, t.lowest, t.highest,
                     t.lowest > kFloor ? t.highest / t.lowest : 0.0f, t.latest);
        }

        if (movers == 0)
            LOG_INFO("DLSS-NR scan: {} candidates tracked, none moving yet -- go between bright and dark",
                     (unsigned int) g_scan.tracked.size());
    }

    ID3D12Resource* dst = g_scan.readback[g_scan.frames % kSlots];

    if (dst == nullptr)
        return;

    // The state a candidate is in is the game's business and nothing here has a contract about it.
    //
    // UNORDERED_ACCESS is the assumption, and it is the reasonable one: every candidate got here by
    // having an unordered access view created on it, which is what a compute shader writes through,
    // and an eye adaptation buffer is written every frame and read by the next pass. It is still an
    // assumption, which is why the whole scan is behind a setting that is off by default -- getting
    // this wrong on someone's machine costs them a frame or a device, and nobody who has not asked
    // for the scan should be exposed to that.
    for (size_t i = 0; i < g_scan.tracked.size(); ++i)
    {
        Tracked& t = g_scan.tracked[i];

        if (t.resource == nullptr)
            continue;

        Barrier(cmdList, t.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);

        if (t.isBuffer)
        {
            cmdList->CopyBufferRegion(dst, i * kStride, t.resource, 0, t.bytes);
        }
        else
        {
            D3D12_TEXTURE_COPY_LOCATION src {};
            src.pResource = t.resource;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION to {};
            to.pResource = dst;
            to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            to.PlacedFootprint.Offset = i * kStride;
            to.PlacedFootprint.Footprint.Format = t.texFormat;
            to.PlacedFootprint.Footprint.Width = 1;
            to.PlacedFootprint.Footprint.Height = 1;
            to.PlacedFootprint.Footprint.Depth = 1;
            to.PlacedFootprint.Footprint.RowPitch = 256;

            D3D12_BOX one { 0, 0, 0, 1, 1, 1 };
            cmdList->CopyTextureRegion(&to, 0, 0, 0, &src, &one);
        }

        Barrier(cmdList, t.resource, D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    g_scan.readbackCounts[g_scan.frames % kSlots] = g_scan.tracked.size();
    g_scan.frames++;
    g_scan.status = "";
}

// Drop the scan's references to the resources it captured, WITHOUT touching our own readback buffers.
//
// The scan AddRef's every candidate it adopts (Adopt) but nothing ever released them -- Shutdown() has
// no callers -- so a Streamline/DLSS-D-owned resource that passes the filter is pinned by our stray
// AddRef, and when the driver frees its (placed) heap at feature teardown the surviving wrapper points
// at freed memory: the use-after-free that removed the device in Cyberpunk (a lock on a freed object in
// nvwgf2umx). Calling this at feature release drops our references first, so nothing we hold outlives
// the heap. Only the candidates (foreign resources) are released here -- NOT the readback ring, which
// is ours and may have GPU copies in flight; freeing that here would be a new hazard. Capture is gated
// on NR being ENABLED (not on the scan source), so this releases whatever was captured whenever NR is
// on -- scan selected or not; it is a no-op only when NR is off (nothing captured), so FSR/XeSS users
// with NR off pay nothing. The scan re-adopts candidates next frame.
void ReleaseTrackedResources()
{
    std::lock_guard<std::mutex> lock(g_scanMutex);

    for (Tracked& t : g_scan.tracked)
    {
        if (t.resource != nullptr)
            t.resource->Release();
    }

    g_scan.tracked.clear();
    g_scan.complained = false;
    // The ring may still contain copies of the old candidates. Do not interpret those values
    // as newly adopted resources that happen to occupy the same list positions.
    std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
}

void Shutdown()
{
    std::lock_guard<std::mutex> tickLock(g_tickMutex);
    std::lock_guard<std::mutex> lock(g_scanMutex);

    for (Tracked& t : g_scan.tracked)
    {
        if (t.resource != nullptr)
            t.resource->Release();
    }

    g_scan.tracked.clear();

    for (unsigned int i = 0; i < kSlots; ++i)
    {
        if (g_scan.readback[i] != nullptr)
        {
            g_scan.readback[i]->Release();
            g_scan.readback[i] = nullptr;
        }
    }

    g_scan.frames = 0;
    std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
    g_scan.lastEpoch = UINT64_MAX;
    if (g_scan.device != nullptr)
    {
        g_scan.device->Release();
        g_scan.device = nullptr;
    }
    g_scan.status = "not started";
}

}
