#pragma once

// Looking for the exposure the game computed but never handed over.
//
// The problem this exists for: a game whose exposure moves with the lighting, and which does not
// pass an exposure texture to the upscaler. Spider-Man and Kingdom Come 2 are both this. The white
// point is then a constant divisor against a moving exposure, and the picture drifts or flickers as
// the scene's lighting changes -- which is not a value the user chose badly, it is one number being
// asked to be two things.
//
// The observation this is built on: those games still COMPUTE an exposure. Essentially every engine
// with eye adaptation writes one into a tiny texture or buffer every frame, reads its own previous
// value to smooth it, and uses it downstream. It simply never reaches the upscaler. So the number is
// not something to reconstruct from pixels -- it is sitting in memory, unread.
//
// Which matters because every attempt to reconstruct it from pixels has failed, twice, for the same
// reason each time. Anything measured off a frame is a statement about that frame's content, and the
// finished frame is downstream of bloom, depth of field, the interface and our own edit. The game's
// own number is upstream of all of it.
//
// WHAT THIS DOES NOT DO
//
// It does not decide anything. It watches, and it reports what it found, and that is deliberate: the
// last two times a number was inferred here it went straight into the interface as a suggestion and
// was wrong -- 8 where 240 was right, and confidently offered on a black screen. So this ships as an
// instrument. Whether a found value is worth consuming is a question for after the readout has been
// looked at in real games, not a question this answers.
//
// A found buffer also carries no contract. NVIDIA's exposure texture is defined by the SDK as "the
// final exposure scale"; a buffer we discovered ourselves might hold a multiplier, a divisor, or a
// stop value in log space, and nothing in it says which. That is why the eventual use is a RATIO
// against the value at the moment the user set their white point, where the units cancel -- not an
// absolute number.

#include <cstdint>
#include <string>
#include <vector>

struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;
struct D3D12_UNORDERED_ACCESS_VIEW_DESC;

namespace DlssNr
{
namespace ExposureScan
{

// One thing that looks like it could be an exposure, and what it has been seen doing.
struct Candidate
{
    std::string shape;  // "1x1 R32_FLOAT" and the like, for the readout
    float latest = 0.0f;
    float lowest = 0.0f;
    float highest = 0.0f;
    unsigned int reads = 0;
    bool moves = false; // has it changed by more than noise since it was first read
};

// Called from the resource tracker every time the game creates an unordered access view. Cheap and
// silent for everything that does not match; the shape being looked for is rare enough that the
// filter rejects almost every call on its first comparison.
void NoteUav(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc);

// Called from the device's resource creation hooks, which is the detection point that actually
// works. The unordered access view hook above only exists inside the frame generation HUD fix and is
// not installed unless that is running -- so in a game without frame generation it never fires once,
// which is why the first version of this found nothing anywhere. Creation is also the better place:
// the description is right there, it happens before anything else, and it catches a resource whose
// view is made through a descriptor copy rather than through CreateUnorderedAccessView.
void NoteResource(const D3D12_RESOURCE_DESC* desc, ID3D12Resource* resource);

// How many resources have been looked at. The number that distinguishes "this game has no exposure
// buffer" from "the hook is not running", which are the same empty list and very different problems.
unsigned int Examined();

// The best candidate's live value, or 0 if nothing has been found. Widest travel wins, same rule as
// the indicator. Written so the log can carry the scan's number and the game's own exposure on one
// line -- which is the only way to tell, offline and after the fact, whether the scan found the
// right buffer or merely a moving one.
float BestValue(int* outIndex = nullptr, float* outLowest = nullptr, float* outHighest = nullptr);

// Called from the Neural Rendering pass. The shared scanner follows the first rendering device
// until Shutdown; other devices are skipped. An epoch suppresses repeated passes/owners in one frame.
// This is a frame identity, not evidence that GPU work has completed.
void Tick(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
          uint64_t submissionEpoch = UINT64_MAX);

// What the scan has to say right now, in one line, for an indicator that can be read while playing.
enum class Verdict
{
    Off,       // not scanning
    Waiting,   // scanning, nothing shaped like an exposure has appeared
    Watching,  // candidates found, none of them has moved yet
    Found,     // at least one candidate moves -- this is the result
    Barren     // watched long enough with no movement to keep hoping
};

Verdict Where();

// Fills a short line describing the state. Null-terminated, safe to draw every frame.
const char* Headline();

// For the menu.
std::vector<Candidate> Report();
const char* Status();
bool Scanning();

// ---------------------------------------------------------------------------------------------
// Multi-point anchoring. See dlssnr/design/multi-point-anchoring.md.
//
// The white point driven by the scan is calibrated from one or more points the user placed --
// (scanValue, whitePoint) pairs -- and interpolated between them in log space. One point is the
// original single-anchor ratio law; two or more fit the buffer's actual, possibly nonlinear,
// relationship so it holds across the whole lighting range rather than only near one anchor.
//
// The table is guarded by the same lock as the rest of the scan, so the menu (any thread) and the
// pass (render thread) never tear it.
struct AnchorPoint
{
    float scan;   // the scan's value when this point was captured
    float white;  // the white point that looked right there
};

// A snapshot for the menu to draw, sorted by scan ascending.
std::vector<AnchorPoint> Anchors();

// Add the current point. Rejected (returns false) if the scan value is out of range or within a
// small factor of an existing point's -- a near-duplicate would divide by ~zero in the interpolation.
bool AnchorAdd(float scan, float white);

// Edit the white point of an existing row (the slider, when a row is selected).
void AnchorSetWhite(int index, float white);

void AnchorRemove(int index);
void AnchorClear();

// The interpolated white point for the current best scan value, already trimmed and clamped. Returns
// 0 when there is no usable anchor, so the caller falls through to its other sources. `inverted` only
// affects the single-point ratio law; with two or more points the direction is encoded in the data.
float AnchoredWhitePoint(float scanNow, bool inverted, float trim);

// Config round-trip: "v0:w0;v1:w1;..." ascending. Load replaces the table; Serialize is for saving.
void LoadAnchors(const std::string& serialized);
std::string SerializeAnchors();

// Release the scan's references to captured resources (call at feature teardown -- see the .cpp).
void ReleaseTrackedResources();

// Call after the scanner's GPU copies have completed, before destroying its device.
void Shutdown();

} // namespace ExposureScan
} // namespace DlssNr
