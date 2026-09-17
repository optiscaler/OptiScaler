#pragma once
#include "DlssNr_ExposureScan.h"
#include <mutex>
namespace DlssNr::ExposureScan::Detail
{
// How many candidates are worth keeping. The shape being looked for is rare -- in a frame's worth of
// A cap on how many candidates are tracked. Kept low originally as a statement that a tight filter
// should find only a handful -- but buffer-heavy engines crowd the real exposure out of a low cap:
// Cyberpunk's REDengine creates dozens of tiny UAV buffers the same 4/12 bytes as an exposure, and its
// real one can land past slot 24. Now that the scan is crash-safe (references dropped at feature
// teardown), the real discriminator is MOVEMENT, not scarcity, so a larger cap costs only a few tiny
// copies a frame and stops the answer being crowded out.
constexpr size_t kMaxCandidates = 64;

// Ring depth for the readbacks. Four, so the slot being read is four frames behind the slot being
// written and the read never waits on the GPU. Same depth and the same reason as the meter's.
constexpr unsigned int kSlots = 4;

// Every candidate's value lands in one buffer, at its own offset, so there is one copy per candidate
// but only one buffer per slot. 16 bytes each is enough for the widest format worth reading.
// D3D12 requires a placed-footprint offset to be a multiple of
// D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT (512). A texture candidate at index i copies to i*kStride,
// so the stride is that alignment; buffer copies have no such rule and are unaffected by the waste.
constexpr unsigned int kStride = 512;

// What an exposure could plausibly be. Outside these it is a flag, a counter, a sentinel or a
// zeroed buffer nobody has written yet -- Nioh 3 offers all four, including one holding 1000000.
constexpr float kFloor = 1e-6f;
constexpr float kCeiling = 1e4f;

struct Tracked
{
    ID3D12Resource* resource = nullptr;
    ID3D12Device* device = nullptr; // Identity; the retained resource keeps its device alive.
    std::string shape;
    bool isBuffer = false;
    unsigned int bytes = 4;
    DXGI_FORMAT texFormat = DXGI_FORMAT_UNKNOWN;  // the source texture's format, for CopyTextureRegion

    float latest = 0.0f;
    float lowest = 0.0f;
    float highest = 0.0f;
    unsigned int reads = 0;
    unsigned int inRange = 0;   // reads that could plausibly be an exposure
    bool moves = false;
};

struct ScanState
{
    ID3D12Device* device = nullptr;
    unsigned long long lastEpoch = UINT64_MAX;
    unsigned int examined = 0;
    std::vector<Tracked> tracked;
    ID3D12Resource* readback[kSlots] = {};
    size_t readbackCounts[kSlots] = {};
    unsigned long long frames = 0;
    const char* status = "not started";
    bool complained = false;
    unsigned int nearMissLogged = 0;   // bounded diagnostic; see NoteResource
};

extern ScanState g_scan;
extern std::mutex g_scanMutex;
// Serializes ring allocation, recording and shutdown. Resource creation hooks only take
// g_scanMutex, so allocating readbacks while holding this mutex cannot reenter it.
extern std::mutex g_tickMutex;

bool Wanted();
}
