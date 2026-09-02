#pragma once

// Everything about the Neural Rendering composition pass that is not Direct3D 12.
//
// Two kinds of thing live here. The constants the shader reads, so a Vulkan implementation can share
// the struct rather than redefine it and drift; and the parameter names the model is driven by, which
// are the model's own and identical whatever API is calling it.
//
// The Direct3D 12 side is DlssNr_Dx12, which implements Shader_Dx12 the way RCAS and Output Scaling
// do. The model itself is separate again: creating and evaluating an NGX feature is not a dispatch,
// so it does not belong in a shader class.

#include <cstdint>

// Which of the three passes a dispatch is. One shader, because all three read and write the same set
// of resources and differ only in what they compute.
enum DlssNrMode : uint32_t
{
    DlssNrMode_Encode = 0,    // the frame -> a tone-mapped proxy, plus an untouched copy
    DlssNrMode_Resolve = 1,   // proxy + the model's answer + the untouched copy -> the edited frame
    DlssNrMode_Downsample = 2 // the proxy -> a smaller proxy, when the model works below full size
};

// What the composition shader reads.
//
// The model does not replace the frame. It is shown a tone-mapped proxy of the picture, and its
// answer is transferred back onto the real frame -- so most of these describe how much of that answer
// to take, not what the model should do.
// Aligned to 256 because a constant buffer view's size must be a multiple of it. Without this the
// buffer is created at the struct's natural size, the view is invalid, and the device is removed a
// few milliseconds later -- with nothing in any log to say why. Every other shader here does the
// same thing; it is not optional.
// What the caller knows about the frame, and the pass cannot work out for itself.
//
// Everything here is a property of how the game encodes its buffers, not a setting: the user's
// choices -- preset, intensity, strengths, paper white -- stay in Config, so a caller placing this
// pass in a new pipeline does not have to plumb a dozen sliders through it.
//
// Sizes are deliberately absent. The output's dimensions come from its own descriptor and the guide
// sizes from theirs, so there is one less thing for a call site to get wrong.
struct DlssNrFrameInfo
{
    // Which way round depth runs. The game states this when it creates its own upscaler.
    bool DepthInverted = false;

    // How the game encodes its motion vectors, as the game itself reports it. Passed through: every
    // resource already carries a subrect saying how big it is, so scaling by the resolution ratio on
    // top of that counts it twice.
    float MvScaleX = 1.0f;
    float MvScaleY = 1.0f;

    // Throw away the model's history. Set it on a cut, a teleport, or the first frame of a feature.
    bool Reset = false;

    // Whether the colour buffer holds linear, open-ended light or a frame that has already been
    // through a tonemapper. Getting this wrong encodes an encoded frame a second time, which looks
    // washed out and banded.
    bool ColourIsLinearHdr = true;
};

struct alignas(256) DlssNrConstants
{
    uint32_t Mode;
    float WhitePoint;

    uint32_t Width;
    uint32_t Height;

    // How much of the model's edit lands, and how much of it is allowed to be colour rather than
    // luminance. Separating the two is what keeps saturated highlights from shifting hue.
    float TransferStrength;
    float ColourStrength;

    uint32_t DebugView;

    // A ceiling on how far a pixel may be brightened. The transfer is a ratio, and a ratio against a
    // near-black proxy pixel is unbounded without one.
    float MaxRatio;

    // Set when the game's buffer is already tone-mapped, in which case there is nothing to convert
    // and the transfer is the identity.
    uint32_t Passthrough;

    float MvScaleX;
    float MvScaleY;

    // Depth and motion vectors come from the upscaler's inputs and so may be at render resolution
    // while colour and output are at display resolution.
    uint32_t GuideWidth;
    uint32_t GuideHeight;

    // Showing the pass against itself. 0 off, 1 side by side, 2 a wipe.
    //
    // Both are drawn by the resolve rather than by a pass of their own, because the resolve is the
    // one place that already holds the frame as the upscaler produced it and the frame the model
    // edited. Comparing them anywhere else would mean keeping a second copy of one of them.
    uint32_t CompareMode;
    float CompareSplit;

    // How much of the frame side by side shows. 1 fits the whole thing at its right shape and
    // letterboxes what is left over; 2 fills the half and crops to the middle instead.
    float CompareZoom;

    // Which side the edited frame is on. Swapping matters because the eye is not even-handed about
    // left and right, so a difference can look like an improvement purely from where it sits.
    uint32_t CompareSwap;

    // How a model that worked below the frame's size is brought back. 0 classic, 1 matched residual.
    //
    // Classic composes the model's own low-resolution picture against the full-resolution frame, so
    // the two disagree by the blur the downsample introduced as well as by the edit -- and the
    // composition reads that disagreement as headroom the frame has and the model never saw. Matched
    // residual takes only the model's *difference* from low resolution and lays it on the frame's own
    // full-resolution proxy, so the two pictures being compared are at the same scale and the only
    // thing carried up from small is the edit itself.
    //
    // The idea and the cube-scaled residual are hhkbble's, from the multi-pass PR against this fork.
    uint32_t Transfer;
};

class DlssNr_Common
{
  protected:
    // The model's own parameter names, spelled once.
    //
    // These are not ours to choose and they do not vary by API, which is the whole reason they are
    // here rather than in the Direct3D 12 file. Getting one wrong is silent: the model keeps its
    // previous value and the control simply appears to do nothing.
    static constexpr const char* kEnabled = "DLSSNR.Enabled";
    static constexpr const char* kWidth = "DLSSNR.Width";
    static constexpr const char* kHeight = "DLSSNR.Height";

    static constexpr const char* kColor = "DLSSNR.Color";
    static constexpr const char* kDepth = "DLSSNR.Depth";
    static constexpr const char* kMotion = "DLSSNR.MVec";
    static constexpr const char* kOutput = "DLSSNR.Output";

    static constexpr const char* kDepthInverted = "DLSSNR.DepthInverted";
    static constexpr const char* kReset = "DLSSNR.Reset";
    static constexpr const char* kMvScaleX = "DLSSNR.MVecScaleX";
    static constexpr const char* kMvScaleY = "DLSSNR.MVecScaleY";

    // Read once, while the feature is built. Writing these only at evaluate does nothing at all,
    // which is why several of them appeared to be dead controls for a long time.
    static constexpr const char* kPreset = "DLSSNR.Hint.Render.Preset";
    static constexpr const char* kIntensity = "DLSSNR.Intensity";
    static constexpr const char* kStyle = "DLSSNR.Style";
    static constexpr const char* kLocalStructure = "DLSSNR.LocalStructureStrength";
    static constexpr const char* kLocalTone = "DLSSNR.LocalToneStrength";
    static constexpr const char* kGlobalTone = "DLSSNR.GlobalToneStrength";

    // Despite the name, this is the automatic *skin* mask, not an interface mask.
    static constexpr const char* kAutoMask = "DLSSNR.UseAutoMask";

    // Defaults to -1, meaning follow local structure. It is not a 0..1 strength and -1 is not "off".
    static constexpr const char* kSkinStructure = "DLSSNR.SkinStructureStrength";

    // The interface layer, its alpha, and the composited frame. The model accepts all three and is
    // currently given none of them: with no interface supplied there is nothing to correct.
    static constexpr const char* kUi = "DLSSNR.UI";
    static constexpr const char* kUiAlpha = "DLSSNR.UIAlpha";
    static constexpr const char* kBackbuffer = "DLSSNR.Backbuffer";
    static constexpr const char* kUiCorrection = "DLSSNR.UICorrection";
};
