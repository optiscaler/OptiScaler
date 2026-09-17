
#ifdef VK_MODE
[[vk::binding(0, 0)]]
cbuffer Params : register(b0, space0)
#else
cbuffer Params : register(b0)
#endif
{
    uint  gMode;
    float gWhitePoint;
    uint  gWidth;
    uint  gHeight;
    float gTransferStrength;
    float gColourStrength;
    uint  gDebugView;
    float gMaxRatio;
    uint  gPassthrough;
    float gMvScaleX;     // motion vector units -> pixels of this dispatch
    float gMvScaleY;
    uint  gGuideWidth;   // the motion texture's valid region
    uint  gGuideHeight;
    uint  gCompareMode;  // 0 off, 1 side by side, 2 wipe
    float gCompareSplit; // where the wipe cuts, 0..1
    float gCompareZoom;  // side by side: 1 fits the frame, 2 fills the half
    uint  gCompareSwap;  // put the edited frame on the other side
    uint  gTransfer;     // 0 classic, 1 matched residual -- how a below-size model comes back
    float gDebugScale;   // what the debug views are scaled by, held still while the meter moves
    uint  gReversibleMode; // 0 knee, 1 Neutwo+composed, 2 Neutwo+replace, 3 hybrid+composed, 4 hybrid+replace
    uint  gApplyModel;     // 0 output the clean frame (pass still runs), 1 apply the model's edit
    uint  gUseGameExposure;// D3D12 source-1 only: 1 = read the game's live exposure in-shader (t4)
    float gExposurePreMul; // preExposure * trim, so the live white point is gExposurePreMul / exposure
    uint  gSkinProtection;
    uint  gShowSkinMask;
    float gSkinDetail;
    float gSkinColour;
    float gEnvironmentDetail;
    float gEnvironmentColour;
};

// Bringing an impossible colour back into a possible one.
//
// A colour with a negative component is not a colour any display can show, and the composition can
// produce one: the model's answer is rescaled by a ratio and its chroma rebuilt, and either step can
// push a saturated pixel past the edge of the gamut.
//
// This used to be a hard clamp -- convert to AP1, max() every channel against zero, convert back --
// which is a per-channel operation on exactly the pixels most likely to breach, and per-channel
// operations on saturated pixels are the hue distorter this file warns about everywhere else. The
// channel that hits the wall first decides the colour of the rest.
//
// Instead the whole colour is scaled toward the neutral axis by one factor, so its hue survives and
// only its saturation gives way. And it is exactly nothing when nothing is out of gamut: with every
// component non-negative the scale is 1 and the colour comes back bit-for-bit.
//
// Taken from RenoDX's DLSS 5 addon by clshortfuse (https://github.com/clshortfuse/renodx), whose
// implementation this is -- the D65 adaptation state, the reversible scale and the LMS basis are
// theirs. See Licenses/RenoDX_ATTRIBUTION.txt.

float SanitizeFinite(float v, float fallback) { return isfinite(v) ? v : fallback; }

// Approximate skin-colour selection, not a face/skin segmentation network. Warm
// materials may be selected and coloured lighting can hide skin. The preview is
// deliberately exposed so users can check this before relying on protection.
float SkinColourWeight(float3 rgb)
{
    rgb = saturate(rgb);
    float y = dot(rgb, float3(0.299, 0.587, 0.114));
    float cb = (rgb.b - y) * 0.564 + 0.5;
    float cr = (rgb.r - y) * 0.713 + 0.5;
    float2 distance = (float2(cb, cr) - float2(0.405, 0.600)) / float2(0.090, 0.110);
    float chroma = max(rgb.r, max(rgb.g, rgb.b)) - min(rgb.r, min(rgb.g, rgb.b));
    return (1.0 - smoothstep(0.55, 1.35, length(distance))) * smoothstep(0.02, 0.10, chroma);
}

float3 SanitizeFinite3(float3 v, float3 fallback)
{
    return float3(SanitizeFinite(v.x, fallback.x), SanitizeFinite(v.y, fallback.y),
                  SanitizeFinite(v.z, fallback.z));
}

float SafeDivide(float numerator, float denominator, float fallback)
{
    return abs(denominator) > 1e-8 ? numerator / denominator : fallback;
}

// Hunt-Pointer-Estevez LMS over linear BT.709, carrying the fixed D65 adaptation state the
// compression is defined against. The signal itself never leaves BT.709.
float3 LMSToBT709(float3 color)
{
    const float3x3 m = { 5.62059812, -4.57145756, 0.15577924,
                         -1.15555585, 2.25800438, -0.15415806,
                         0.03059913, -0.19018011, 1.06820532 };
    return mul(m, color);
}

float3 BT709ToLMS(float3 color)
{
    const float3x3 m = { 0.30569589, 0.62271286, 0.04528636,
                         0.15776262, 0.76968599, 0.08807030,
                         0.01933082, 0.11919478, 0.95053215 };
    return mul(m, color);
}

// The neutral colour of the same luminance as what is being compressed -- the point everything is
// pulled toward, so that pulling changes saturation and not hue.
float3 D65NeutralBT709(float3 adaptiveStateLms, float luminance)
{
    float3 d65 = LMSToBT709(max(adaptiveStateLms, 1e-8));
    float d65Y = max(dot(d65, float3(0.2126, 0.7152, 0.0722)), 1e-8);
    return d65 * (luminance / d65Y);
}

// The largest scale toward the neutral axis that leaves no channel negative. One for a colour that
// was already representable, which is why this is safe to run on every pixel.
float GamutCompressionScale(float3 color, float3 adaptiveStateLms)
{
    color = SanitizeFinite3(color, float3(0.0, 0.0, 0.0));

    const float y = dot(color, float3(0.2126, 0.7152, 0.0722));

    if (!(y > 1e-8))
        return 1.0;

    const float3 neutral = D65NeutralBT709(adaptiveStateLms, y);
    float scale = 1.0;

    if (color.r < 0.0 && neutral.r > color.r)
        scale = min(scale, SafeDivide(neutral.r, neutral.r - color.r, 1.0));

    if (color.g < 0.0 && neutral.g > color.g)
        scale = min(scale, SafeDivide(neutral.g, neutral.g - color.g, 1.0));

    if (color.b < 0.0 && neutral.b > color.b)
        scale = min(scale, SafeDivide(neutral.b, neutral.b - color.b, 1.0));

    return saturate(SanitizeFinite(scale, 1.0));
}

float3 ClampAp1(float3 color)
{
    const float3 adaptiveStateLms = BT709ToLMS(float3(0.18, 0.18, 0.18));
    const float scale = GamutCompressionScale(color, adaptiveStateLms);

    // Nothing was out of gamut. Leave the colour exactly as it arrived.
    if (scale >= 1.0)
        return color;

    const float y = dot(color, float3(0.2126, 0.7152, 0.0722));
    const float3 neutral = D65NeutralBT709(adaptiveStateLms, y);

    return SanitizeFinite3(neutral + (color - neutral) * scale, max(neutral, 0.0));
}

// ---------------------------------------------------------------------------------------------
// The composition below (UpgradeToneMap's two-branch ratio, the OkLab hue correction, and the blend
// between a luminance-only result and the model's own colour) is taken from RenoDX's DLSS 5 addon by
// clshortfuse -- https://github.com/clshortfuse/renodx. It is their design, not ours; see
// Licenses/RenoDX_LICENSE.txt. The OkLab matrices are Bjorn Ottosson's published constants and the
// AP1, sRGB and PQ transforms are standard colour science.
// ---------------------------------------------------------------------------------------------

// OkLab, so the model's colour can be reached without its hue being invented on the way. A ratio
// applied to an RGB triple does not move hue, but a difference added to one does -- which is what the
// old composition did, and why a warm subject could come back green. Here the result's chroma is
// rebuilt in the model's own hue direction and only its magnitude is taken from the scaled colour.
float3 CbrtSigned(float3 v) { return sign(v) * pow(abs(v), 1.0 / 3.0); }

float3 ToOkLab(float3 color)
{
    const float3x3 rgb_to_lms = { 0.4122214708, 0.5363325363, 0.0514459929,
                                  0.2119034982, 0.6806995451, 0.1073969566,
                                  0.0883024619, 0.2817188376, 0.6299787005 };
    const float3x3 lms_to_lab = { 0.2104542553, 0.7936177850, -0.0040720468,
                                  1.9779984951, -2.4285922050, 0.4505937099,
                                  0.0259040371, 0.7827717662, -0.8086757660 };
    return mul(lms_to_lab, CbrtSigned(mul(rgb_to_lms, color)));
}

float3 FromOkLab(float3 lab)
{
    const float3x3 lab_to_lms = { 1.0, 0.3963377774, 0.2158037573,
                                  1.0, -0.1055613458, -0.0638541728,
                                  1.0, -0.0894841775, -1.2914855480 };
    const float3x3 lms_to_rgb = { 4.0767416621, -3.3077115913, 0.2309699292,
                                  -1.2684380046, 2.6097574011, -0.3413193965,
                                  -0.0041960863, -0.7034186147, 1.7076147010 };
    float3 lms = mul(lab_to_lms, lab);
    return mul(lms_to_rgb, lms * lms * lms);
}

// Takes the hue and the chroma direction from `correct`, and only the chroma magnitude from
// `incorrect`. Scaling a colour by a luminance ratio changes how saturated it reads; this puts the
// saturation back where the model meant it without letting the hue drift.
// Takes the hue and chroma direction from `correct` and only the chroma magnitude from
// `incorrect`, so a rescaled colour keeps the model's own hue rather than drifting toward whatever
// the scaling did to its channels.
float3 HueOkLab(float3 incorrect, float3 correct)
{
    float3 incorrectLab = ToOkLab(incorrect);
    const float3 correctLab = ToOkLab(correct);
    const float incorrectChroma = length(incorrectLab.yz);
    const float correctChroma = length(correctLab.yz);

    // Normalise the direction before scaling it, rather than scaling by a ratio of magnitudes.
    //
    // The two are the same algebra -- correctLab.yz * (incorrectChroma / correctChroma) is
    // (correctLab.yz / correctChroma) * incorrectChroma -- but only this order is bounded. The
    // other divides by correctChroma while guarding it with `== 0.0`, which is an exact float
    // comparison and so catches only a chroma that is precisely zero. A model pixel that is merely
    // very close to grey has a chroma of about 1e-7, sails past that guard, and turns a hue
    // direction with no meaningful magnitude into a multiplier of ten thousand. The result is a
    // saturated colour pulled out of numerical noise.
    //
    // Written this way the direction is unit length by construction and the result cannot exceed
    // incorrectChroma, whatever the model returned. Nioh 3 is where this showed: a night scene
    // leaves most of the frame near-achromatic, so near-zero chroma is the common case rather than
    // the edge, and the speckle it produced was reported as green noise.
    const float2 hueDirection = correctChroma > 1e-5 ? correctLab.yz / correctChroma : float2(0.0, 0.0);

    incorrectLab.yz = hueDirection * incorrectChroma;

    return ClampAp1(FromOkLab(incorrectLab));
}

// Bindings are stated for SPIR-V rather than inferred. D3D keeps b, t, u and s in separate register
// files, so b0 and t0 do not collide; Vulkan has one number line per descriptor set, and dxc's default
// mapping would put both at binding 0. The numbers below are the order the pass binds them in, and
// DlssNr_Vk's descriptor set layout has to agree with them entry for entry.
#ifdef VK_MODE
[[vk::binding(1, 0)]]
#endif
Texture2D<float4>   gSource   : register(t0);  // encode: the frame. resolve: the proxy.
#ifdef VK_MODE
[[vk::binding(2, 0)]]
#endif
Texture2D<float4>   gModel    : register(t1);  // resolve: what the model returned.
#ifdef VK_MODE
[[vk::binding(3, 0)]]
#endif
Texture2D<float4>   gOriginal : register(t2);  // resolve: the untouched frame.
#ifdef VK_MODE
[[vk::binding(4, 0)]]
#endif
Texture2D<float4>   gMotion   : register(t3);  // resolve, accumulating: the game's motion vectors.

// The game's 1x1 exposure texture, bound at t4 (DispatchPass's "prev edit" SRV slot). D3D12 only:
// Vulkan has no eighth descriptor for it and keeps computing the white point on the CPU, so the whole
// live path is compiled out under VK_MODE and gUseGameExposure is never set on that backend.
#ifndef VK_MODE
Texture2D<float4>   gExposure : register(t4);
#endif
#ifdef VK_MODE
[[vk::binding(5, 0)]]
#endif
RWTexture2D<float4> gTarget   : register(u0);  // encode: the proxy. resolve: the frame.
#ifdef VK_MODE
[[vk::binding(6, 0)]]
#endif
RWTexture2D<float4> gKeep     : register(u1);  // encode: the untouched copy. unused by the resolve.
#ifdef VK_MODE
[[vk::binding(7, 0)]]
#endif
SamplerState        gLinear   : register(s0);  // so the edit can be read at a different size

// The picture white point. Sources 0 (paper white) and 2 (scan) resolve it on the CPU and pass it in
// gWhitePoint; source 1 (the game's own exposure) also passes a CPU value in gWhitePoint as a fallback,
// but when the exposure texture is bound (D3D12) it is recomputed HERE from the live exposure --
// gExposurePreMul (= preExposure * trim) / exposure -- which removes the 3-4 frame CPU-readback lag the
// meter path has. The clamp matches the CPU path's [0.01, 4096]. Vulkan compiles the live path out and
// always returns the CPU value, so its behaviour is unchanged.
float WhitePoint()
{
#ifndef VK_MODE
    if (gUseGameExposure != 0)
    {
        float e = gExposure.Load(int3(0, 0, 0)).r;
        if (e > 1e-6 && e < 1e6)
            return clamp(gExposurePreMul / e, 0.01, 4096.0);
        // A missing or absurd sample falls through to the CPU value the meter path still maintains.
    }
#endif
    return max(gWhitePoint, 1e-4);
}


static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

// sRGB rather than a plain 2.2 power: it is what an SDR game buffer actually carries, and the model was
// trained on those.
float3 LinearToSrgb(float3 v)
{
    v = saturate(v);
    return lerp(v * 12.92, 1.055 * pow(max(v, 1e-8), 1.0 / 2.4) - 0.055, step(0.0031308, v));
}

float3 SrgbToLinear(float3 v)
{
    v = saturate(v);
    return lerp(v / 12.92, pow((v + 0.055) / 1.055, 2.4), step(0.04045, v));
}

// The edit at an arbitrary position, exactly as the resolve computes its own.
float3 EditAt(float2 uvq)
{
    float3 p = gSource.SampleLevel(gLinear, uvq, 0).rgb;
    float3 m = gModel.SampleLevel(gLinear, uvq, 0).rgb;

    if (gPassthrough == 0)
    {
        p = SrgbToLinear(p);
        m = SrgbToLinear(m);
    }

    return m - p;
}


// The soft knee, shared by the encode and the resolve.
//
// The encode applies it on the way in; the resolve has to be able to reproduce it, because the
// matched-residual path needs the frame's own proxy at full resolution and the encode only ever
// wrote a reduced one. It is a pure function of the pixel, so recomputing costs less than the
// texture read it replaces.
float3 SoftKnee(float3 display)
{
    if (gPassthrough != 0)
        return display;

    float displayLuma = dot(display, kLuma);

    if (displayLuma > 0.75)
    {
        float rolled = 0.75 + 0.25 * (1.0 - exp(-(displayLuma - 0.75) / 0.25));
        display *= rolled / displayLuma;
    }

    // Per-channel headroom, with the hue kept.
    //
    // The roll-off above is on luminance, and luminance is a weighted sum in which blue counts for
    // seven percent. A saturated blue can therefore sit at B = 2 with a luminance of 0.14, pass the
    // knee untouched, and be clipped per channel by the saturate in LinearToSrgb -- and clipping one
    // channel of a triple is a hue rotation, so blue arrives as cyan. That was the green cast over
    // every blue thing in GTA V at colour strength 1: the sky, the denim, the minimap. The model was
    // shown a cyan proxy, answered in cyan, and at colour strength 1 its hue is the frame's hue.
    //
    // One scalar on the whole triple cannot move hue, so the peak channel is brought to 1 that way.
    // Only pixels that were already being clipped are touched, so everything else is bit-identical
    // to before, and the resolve's reconstruction of this proxy stays exact because it goes through
    // this same function.
    float peak = max(display.r, max(display.g, display.b));

    if (peak > 1.0)
        display /= peak;

    return display;
}

// The reversible proxy, from RenoDX's Sep-2 DLSS 5 addon (clshortfuse) -- an unclipped, hue-preserving
// encode meant to be reproduced exactly, so the model is shown the highlight gradation the soft knee
// compresses into a razor-thin band near white. Neutwo maps [0, inf) -> [0, 1) with no clip point,
// applied as ONE scalar on the peak channel so the three channels keep their ratios and hue cannot
// bend. The knee reaches its asymptote within a stop of white -- scene 2 and scene 4 arrive ~0.001
// apart, nothing the model can resolve between; Neutwo puts them ~0.076 apart.
//
// This changes only WHAT the proxy is. The resolve already works in a hybrid space -- proxy and model
// in display [0,1], original in linear -- and its ratio bridges the two, so nothing downstream has to
// change: the proxy is decoded by the same SrgbToLinear, compared the same way, and the ratio carries
// the model's answer back to the original's linear luminance exactly as before. Only the matched-
// residual proxy rebuild, which reproduces the encode, switches curve with it.
//
// Gamut nuance (RenoDX also compresses toward the D65 neutral axis first) is deferred: out-of-BT.709
// negative channels are clamped to zero here, enough for the highlight question this measures. See
// Licenses/RenoDX_LICENSE.txt.
float Neutwo(float x) { return x * rsqrt(x * x + 1.0); } // [0, inf) -> [0, 1), no clip point

float3 NeutwoEncode(float3 v)
{
    v = max(v, 0.0);
    float m = max(v.r, max(v.g, v.b));

    if (m <= 1e-6)
        return v;

    // One scalar taken from the peak channel keeps the hue; the peak lands at Neutwo(m) < 1, so no
    // channel clips and LinearToSrgb's saturate never fires -- the proxy is fully invertible.
    return v * (Neutwo(m) / m);
}

// The exact inverse of NeutwoEncode, for the "replace" decode: y/sqrt(1 - y^2) on the peak channel,
// same one-scalar-preserves-hue trick. The inverse diverges at 1, so the peak is clamped just below
// it -- this is the steep-highlight-slope the toggle's help warns about: a highlight at the ceiling
// decodes to a very large but finite value. Only the replace path uses this; the composed path never
// decodes (its ratio bridges display->linear instead), so mode 0/1 are untouched by it.
float3 NeutwoDecode(float3 y)
{
    y = max(y, 0.0);
    float m = max(y.r, max(y.g, y.b));
    m = min(m, 0.999999);

    if (m <= 1e-6)
        return y;

    float x = m * rsqrt(max(1.0 - m * m, 1e-8)); // Neutwo^-1 of the peak
    return y * (x / m);
}

// The hybrid proxy (mode 3): the fix for the two curves each only winning in some scenes. The soft
// knee is fine in the midtones but crushes highlights; Neutwo fixes the highlights but compresses the
// midtones too, so it only helps where the knee was hurting (bright content) and is a downgrade in
// soft-lit content. The hybrid is IDENTITY below the knee -- so midtones are exactly what the soft
// knee already gave (as good as Off) -- and an unclipped, gentle Neutwo-of-the-excess ABOVE it, so
// highlights get the gradation the model needs. C1-continuous at the knee. One proxy that is >= Off
// everywhere: no midtone loss, plus the highlight win. (Composed only -- mode 3 does not replace.)
float HybridCurve(float m)
{
    const float k = 0.75; // knee point: identity below, gentle unclipped roll above

    if (m <= k)
        return m;

    const float e = (m - k) / (1.0 - k);         // excess above the knee, [0, inf)
    return k + (1.0 - k) * (e * rsqrt(e * e + 1.0)); // Neutwo(e) scaled into [k, 1); -> 1, never clips
}

float3 HybridEncode(float3 v)
{
    v = max(v, 0.0);
    float m = max(v.r, max(v.g, v.b));

    if (m <= 1e-6)
        return v;

    // One scalar on the peak channel, hue preserved. Below the knee the scalar is 1 (identity); above
    // it the peak lands at HybridCurve(m) < 1, so no channel clips.
    return v * (HybridCurve(m) / m);
}

// The exact inverse of the hybrid curve, for the hybrid REPLACE decode (mode 4). Because it is IDENTITY
// below the knee, the steep expansion is confined to genuine highlights: midtone model wobble is not
// amplified, so hybrid-replace flashes far less than Neutwo-replace while keeping the raw model detail.
float HybridCurveInv(float y)
{
    const float k = 0.75;

    if (y <= k)
        return y;

    float u = (y - k) / (1.0 - k);                  // Neutwo(e), in [0,1)
    u = min(u, 0.999999);                           // the inverse diverges at 1
    const float e = u * rsqrt(max(1.0 - u * u, 1e-8)); // Neutwo^-1 of the excess
    return k + (1.0 - k) * e;
}

float3 HybridDecode(float3 y)
{
    y = max(y, 0.0);
    float m = max(y.r, max(y.g, y.b));

    if (m <= 1e-6)
        return y;

    return y * (HybridCurveInv(m) / m);
}

// Scale a residual so the result cannot leave the unit cube, without changing its direction.
//
// The model's edit is carried up from a smaller raster and laid on the frame's own proxy, so nothing
// guarantees the sum is still a colour. Clamping per channel would bend the hue -- the channel that
// hits the wall first decides the colour of the rest -- so the whole residual is scaled by the
// largest factor that keeps every channel inside, and the direction survives.
//
// hhkbble's, from the multi-pass PR against this fork.
float3 CubeScaleResidual(float3 P, float3 T)
{
    if (gPassthrough != 0)
        return T;

    float3 d = T - P;
    float alpha = 1.0;

    [unroll] for (int c = 0; c < 3; ++c)
    {
        if (d[c] > 1e-6)
            alpha = min(alpha, (1.0 - P[c]) / d[c]);
        else if (d[c] < -1e-6)
            alpha = min(alpha, (0.0 - P[c]) / d[c]);
    }

    return P + saturate(alpha) * d;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight)
        return;

    // Normalised, so the source may be any size relative to this dispatch.
    float2 uv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight);

    // Experimental private-DLSS carrier, not an ordinary colour image. Neutral 0.5 encodes zero;
    // values below it carry darkening. A reversible signed compression avoids clipping negative
    // edits at the DLSS input. Scale small linear-light edits up before storing them in FP16;
    // at unit scale, a dark scene's edits round to neutral before DLSS even sees them.
    if (gMode == 9)
    {
        float3 source = gSource.Load(int3(id.xy, 0)).rgb;
        float3 answer = gModel.Load(int3(id.xy, 0)).rgb;
        if (gPassthrough == 0) { source = SrgbToLinear(source); answer = SrgbToLinear(answer); }
        float3 d = SanitizeFinite3(answer - source, 0.0);
        gTarget[id.xy] = float4(0.5 + 0.5 * d / (1.0 / 64.0 + abs(d)), 1.0);
        return;
    }
    if (gMode == 10)
    {
        // Mode-local fields: guide active sizes/origins, and motion-to-working-pixel scale.
        uint2 dp = min(uint2(uv * uint2(gGuideWidth, gGuideHeight)),
                       uint2(gGuideWidth, gGuideHeight) - 1) + uint2(gDebugView, gCompareMode);
        uint2 size = uint2(gTransferStrength, gColourStrength);
        uint2 mp = min(uint2(uv * size), size - 1) + uint2(gCompareSwap, gTransfer);
        float z = gSource.Load(int3(dp, 0)).r;
        float2 mv = gModel.Load(int3(mp, 0)).xy * float2(gMvScaleX, gMvScaleY);
        gTarget[id.xy] = float4(isfinite(z) ? z : 0.0, 0, 0, 1);
        gKeep[id.xy] = float4(all(isfinite(mv)) ? mv : float2(0, 0), 0, 1);
        return;
    }
    if (gMode == 5)
    {
        float3 difference = SanitizeFinite3(gModel.Load(int3(id.xy, 0)).rgb -
                                            gSource.Load(int3(id.xy, 0)).rgb, 0.0);
        float3 d = difference / max(gExposurePreMul, 1e-4);
        gTarget[id.xy] = float4(0.5 + 0.5 * d / (1.0 + abs(d)), 1.0);
        return;
    }
    if (gMode == 6)
    {
        float4 base = gSource.Load(int3(id.xy, 0));
        float3 encoded = SanitizeFinite3(gModel.Load(int3(id.xy, 0)).rgb, 0.5);
        // Limit the inverse near its poles: DLSS can ring outside the carrier's [0,1] range.
        float3 signedEdit = clamp(2.0 * encoded - 1.0, -0.999, 0.999);
        float3 edit = signedEdit / (1.0 - abs(signedEdit)) * max(gExposurePreMul, 1e-4);
        gTarget[id.xy] = float4(max(SanitizeFinite3(base.rgb + edit, base.rgb), 0.0), base.a);
        return;
    }
    if (gMode == 7)
    {
        gTarget[id.xy] = 1.0;
        return;
    }

    if (gMode == 8)
    {
        // Already encoded: restore the input range without applying the tone curve again.
        float4 raw = gSource.Load(int3(id.xy, 0));
        gTarget[id.xy] = float4(saturate(SanitizeFinite3(raw.rgb, 0.5)), raw.a);
        return;
    }

    // The meter. One thread per tile of a 64x64 grid over the frame, writing that tile's mean
    // luminance. The frame is raw linear here -- this runs before the encode, on purpose, because the
    // number being looked for is what the encode's divisor should be.
    //
    // A mean per tile, then a percentile across tiles on the CPU. Not the frame's mean, which is what
    // the meter this replaces measured: that reads scene brightness, and a dark scene then asks for a
    // small divisor and hands the model a blown picture anyway. Not the frame's maximum either, which
    // one specular hit decides.
    // What scale is this game's buffer on?
    //
    // Not a taste question. The composition divides the frame by paper white to work in a normalised
    // space, and the right divisor is the one that lands the picture in [0,1]. Nioh 3 needs about 240
    // because its linear buffer holds values around two hundred; GTA V's exposure yields 2.7. Below
    // the correct value the frame is never normalised, the headroom branch computes ratios in the
    // hundreds, and ToOkLab is handed values far outside the range its cube root was built for -- the
    // green tint.
    //
    // Measured from the UNTOUCHED copy the encode kept, never from the frame this pass writes. That
    // distinction is the whole reason this is safe where the old white point meter was not: that one
    // read its own output and chased it, walking one Enshrouded session from 0.010 to 97.910. There
    // is no path from what this pass writes back into what this reads.
    //
    // Per tile, the peak luminance rather than the mean. The mean is scene brightness and says
    // nothing about scale; the peak says where the top of the range is, which is exactly what the
    // divisor has to match. One specular hit cannot decide the answer because the host takes a
    // percentile across tiles afterwards.
    if (gMode == 4)
    {
        uint fullW, fullH;
        gSource.GetDimensions(fullW, fullH);

        const uint tx0 = (uint) (((float) id.x * (float) fullW) / (float) gWidth);
        const uint tx1 = (uint) (((float) (id.x + 1) * (float) fullW) / (float) gWidth);
        const uint ty0 = (uint) (((float) id.y * (float) fullH) / (float) gHeight);
        const uint ty1 = (uint) (((float) (id.y + 1) * (float) fullH) / (float) gHeight);

        // Sixteen samples a side rather than eight, and offset half a step in so the lattice does not
        // sit on the tile's own corner.
        //
        // A fixed sample count over a growing tile means a shrinking fraction of it is read: eight per
        // side covers about 17% of a tile at 1080p but only 4% at 4K, so the same scene reported a
        // lower peak -- and therefore a smaller suggested divisor -- the higher the resolution. That is
        // a measurement that changes with the setting rather than with the game.
        const uint stepX = max((tx1 - tx0) / 16u, 1u);
        const uint stepY = max((ty1 - ty0) / 16u, 1u);

        float peak = 0.0;

        for (uint ty = ty0; ty < max(ty1, ty0 + 1u); ty += stepY)
        {
            for (uint tx = tx0; tx < max(tx1, tx0 + 1u); tx += stepX)
            {
                const float3 c = max(gSource.Load(int3(min(tx, fullW - 1u), min(ty, fullH - 1u), 0)).rgb, 0.0);
                peak = max(peak, dot(c, kLuma));
            }
        }

        gTarget[id.xy] = float4(peak, 0.0, 0.0, 1.0);
        return;
    }

    if (gMode == 3)
    {
        // Tile (0,0) carries the game's own exposure rather than a tile mean.
        //
        // The exposure is a 1x1 texture the game owns, in a resource state this pass did not set and
        // must not assume. Copying it would mean transitioning someone else's resource on a guess,
        // which is how a device is lost. Reading it as an SRV in a pass that is already running costs
        // nothing and touches no state -- and it rides back on the readback that already exists.
        //
        // The motion slot is free here: the meter has no use for motion vectors.
        if (id.x == 0 && id.y == 0)
        {
            gTarget[id.xy] = float4(gMotion.Load(int3(0, 0, 0)).r, 0.0, 0.0, 1.0);
            return;
        }

        uint fullW, fullH;
        gSource.GetDimensions(fullW, fullH);

        const uint tx0 = (uint) (((float) id.x * (float) fullW) / (float) gWidth);
        const uint tx1 = (uint) (((float) (id.x + 1) * (float) fullW) / (float) gWidth);
        const uint ty0 = (uint) (((float) id.y * (float) fullH) / (float) gHeight);
        const uint ty1 = (uint) (((float) (id.y + 1) * (float) fullH) / (float) gHeight);

        // A tile of a 4K frame is 60x34 pixels. Sampling a bounded number of them is within a percent
        // of the true mean and keeps the pass flat regardless of resolution.
        const uint stepX = max((tx1 - tx0) / 8u, 1u);
        const uint stepY = max((ty1 - ty0) / 8u, 1u);

        float sum = 0.0;
        uint taken = 0;

        for (uint ty = ty0; ty < max(ty1, ty0 + 1u); ty += stepY)
        {
            for (uint tx = tx0; tx < max(tx1, tx0 + 1u); tx += stepX)
            {
                float3 c = max(gSource.Load(int3(min(tx, fullW - 1u), min(ty, fullH - 1u), 0)).rgb, 0.0);
                sum += dot(c, kLuma);
                taken++;
            }
        }

        gTarget[id.xy] = float4(taken > 0u ? sum / (float) taken : 0.0, 0.0, 0.0, 1.0);
        return;
    }

    if (gMode == 2)
    {
        uint srcW, srcH;
        gSource.GetDimensions(srcW, srcH);

        // Nothing to do when the sizes already agree.
        if (srcW == gWidth && srcH == gHeight)
        {
            gTarget[id.xy] = gSource.Load(int3(id.xy, 0));
            return;
        }

        // An exact area average rather than a bilinear tap.
        //
        // A bilinear sample of a shrinking image reads four texels and ignores the rest, so most of
        // the picture never reaches the model and what does is weighted by where the sample landed
        // rather than by how much of the pixel it covers. That is aliasing on the way in: the model
        // is shown a picture with detail that was never there and misses detail that was, and its
        // answer changes with sub-pixel motion for no reason in the scene.
        //
        // This integrates the source over the exact footprint of the destination pixel, which is the
        // correct box resample and costs a handful of loads at these ratios.
        //
        // hhkbble's, from the multi-pass PR against this fork.
        const float x0 = ((float) id.x * (float) srcW) / (float) gWidth;
        const float x1 = ((float) (id.x + 1) * (float) srcW) / (float) gWidth;
        const float y0 = ((float) id.y * (float) srcH) / (float) gHeight;
        const float y1 = ((float) (id.y + 1) * (float) srcH) / (float) gHeight;
        const float area = (x1 - x0) * (y1 - y0);

        const int i0 = (int) floor(x0);
        const int i1 = (int) ceil(x1) - 1;
        const int j0 = (int) floor(y0);
        const int j1 = (int) ceil(y1) - 1;

        float3 acc = 0.0;

        for (int j = j0; j <= j1; ++j)
        {
            const int jj = clamp(j, 0, (int) srcH - 1);
            const float aY = max(y0, (float) j);
            const float bY = min(y1, (float) j + 1.0);
            const float wy = max(bY - aY, 0.0);

            for (int i = i0; i <= i1; ++i)
            {
                const int ii = clamp(i, 0, (int) srcW - 1);
                const float aX = max(x0, (float) i);
                const float bX = min(x1, (float) i + 1.0);
                acc += gSource.Load(int3(ii, jj, 0)).rgb * (max(bX - aX, 0.0) * wy);
            }
        }

        const int acx = clamp((int) floor(((float) id.x + 0.5) * (float) srcW / (float) gWidth), 0, (int) srcW - 1);
        const int acy = clamp((int) floor(((float) id.y + 0.5) * (float) srcH / (float) gHeight), 0, (int) srcH - 1);

        gTarget[id.xy] = float4(acc / area, gSource.Load(int3(acx, acy, 0)).a);
        return;
    }

    if (gMode == 0)
    {
        float4 source = gSource.Load(int3(id.xy, 0));
        float3 frame = max(source.rgb, float3(0.0, 0.0, 0.0));

        // Kept so the resolve has the frame as it was, rather than having to reconstruct it.
        gKeep[id.xy] = float4(frame, source.a);

        // Some games hand DLSS a frame that has already been through their tonemapper. The game says
        // which in its own DLSS creation flags, and converting one that needs no conversion is pure
        // damage, so it goes through untouched.
        if (gPassthrough != 0)
        {
            gTarget[id.xy] = float4(frame, source.a);
            return;
        }

        // What the model is shown. Mode 2 -- the default -- scales the frame and encodes it, and that
        // is all: the game is going to tone map this picture later, so tone mapping it here as well
        // shows the model a doubly compressed image. Measured against Cyberpunk's own numbers, the
        // Reinhard proxy handed the model a scene value of 1.0 as 0.55 and 1.5 as 0.64 -- flat, dark,
        // and nothing like the finished frame it was trained on. The model then synthesised weakly,
        // judged tone on a picture that does not exist, and its answer had to be un-crushed on the way
        // back. Mode 0 keeps that old curve, mode 1 the fitted one.
        // A soft knee instead of a hard ceiling. Anything above 0.75 is rolled off rather than
        // clipped, so the model is never shown a field of flat white whose blown pixels flip between
        // frames -- unstable input is unstable output, and this is where a bright scene would produce
        // it. The resolve reproduces this exactly, so the two agree on what the frame's own proxy is.
        // The classic soft knee, or -- when the reversible proxy is on -- the unclipped Neutwo encode
        // that shows the model highlight gradation the knee throws away. Reached only when the frame
        // is not passthrough (handled and returned above), so NeutwoEncode never sees a tone-mapped
        // frame. Both are undone by the resolve: the knee approximately, Neutwo exactly.
        float3 normalized = frame / WhitePoint();
        float3 display;
        if (gReversibleMode == 0)
            display = SoftKnee(normalized);        // soft knee
        else if (gReversibleMode >= 3)
            display = HybridEncode(normalized);    // 3 hybrid composed, 4 hybrid replace -- same curve
        else
            display = NeutwoEncode(normalized);    // 1 composed, 2 replace -- both the full Neutwo proxy

        // The reversible proxy forces opaque alpha -- feature 18 expects an opaque colour input, and
        // the frame's own alpha is not part of what the model reads. The knee path keeps the frame's
        // alpha, so the default stays byte-identical.
        float alpha = gReversibleMode != 0 ? 1.0 : source.a;

        gTarget[id.xy] = float4(LinearToSrgb(display), alpha);
        return;
    }

    // Comparison, decided before anything is read, because side by side changes which part of the
    // frame this pixel is showing rather than just which version of it.
    //
    //   1  side by side  each half carries the whole frame, so both are squeezed horizontally
    //   2  wipe          one frame cut at the split, nothing resampled
    //
    // Neither needs the menu open to stay up. The wipe's split is a setting like any other; the menu
    // is only how you drag it.
    float2 cmpUv = uv;
    bool showOriginal = false;
    bool onDivider = false;
    bool outsideFrame = false;

    if (gCompareMode == 1)
    {
        showOriginal = (uv.x < 0.5) != (gCompareSwap != 0);

        // Each half is half as wide as the frame and just as tall, so the frame cannot fill it and
        // keep its shape. Stretching it to fit is what made both sides look squashed. Fitting it
        // properly leaves the halves letterboxed, which is the honest way round: a comparison that
        // changes the shape of what it is comparing is not showing you the picture.
        //
        // Zoom decides which is given up. At 1 the whole frame is there at its right proportions
        // with bars above and below; at 2 the half is filled and the sides are cropped away.
        float2 half2 = float2(uv.x < 0.5 ? uv.x * 2.0 : (uv.x - 0.5) * 2.0, uv.y) - 0.5;
        cmpUv = float2(0.5 + half2.x / gCompareZoom, 0.5 + half2.y * 2.0 / gCompareZoom);

        outsideFrame = cmpUv.x < 0.0 || cmpUv.x > 1.0 || cmpUv.y < 0.0 || cmpUv.y > 1.0;
        onDivider = abs(uv.x - 0.5) < (1.0 / max(gWidth, 1u));
    }
    else if (gCompareMode == 2)
    {
        showOriginal = (uv.x < gCompareSplit) != (gCompareSwap != 0);
        onDivider = abs(uv.x - gCompareSplit) < (1.0 / max(gWidth, 1u));
    }

    // Sampled rather than loaded: when the model ran at a reduced resolution these are smaller than the
    // frame, and its edit is enlarged here while the frame underneath stays untouched.
    float4 proxySample = gSource.SampleLevel(gLinear, cmpUv, 0);
    float4 modelSample = gModel.SampleLevel(gLinear, cmpUv, 0);

    // Nothing was encoded on the way in, so nothing is decoded here either.
    float3 proxy = gPassthrough != 0 ? proxySample.rgb : SrgbToLinear(proxySample.rgb);
    float3 model = gPassthrough != 0 ? modelSample.rgb : SrgbToLinear(modelSample.rgb);

    // The model's own answer, kept before the matched-residual block below can rewrite `model`, so the
    // replace decode uses what the model returned rather than the residual reconstruction.
    float3 modelDirect = model;
    float4 originalSample = gCompareMode == 1 ? gOriginal.SampleLevel(gLinear, cmpUv, 0)
                                              : gOriginal.Load(int3(id.xy, 0));

    // All three pictures have to share a scale before their luminances can be compared. The proxy and
    // the model come back from an sRGB decode, so they sit in 0..1 where 1 is the white point; the
    // frame is raw linear and runs well past that. Comparing them unnormalised is a real bug and it
    // reads exactly like the model has stopped adding detail: with the frame several times larger,
    // the shadow branch never fires, every pixel takes the highlight branch, and the clamp flattens
    // the result to a near-constant scale. Colour still moves, because that comes from the model's
    // own hue, which is what makes the failure so confusing to look at.
    const float normScale = gPassthrough != 0 ? 1.0 : WhitePoint();
    float3 original = originalSample.rgb / normScale;

    float originalLuma = dot(original, kLuma);
    float proxyLuma = dot(proxy, kLuma);

    // Apply the model. Off outputs the frame as the upscaler produced it (clean) while the pass keeps
    // running -- so with Hold frame you can freeze a frame and toggle this to A/B the same frozen frame
    // with and without Neural Rendering. In passthrough the frame is already display-referred.
    if (gApplyModel == 0)
    {
        gTarget[id.xy] = float4(max(originalSample.rgb, 0.0), originalSample.a);
        return;
    }

    if (gDebugView == 1)
    {
        gTarget[id.xy] = float4(proxy * gDebugScale, originalSample.a);
        return;
    }

    if (gDebugView == 2)
    {
        gTarget[id.xy] = float4(model * gDebugScale, originalSample.a);
        return;
    }

    float3 edit = model - proxy;
    if (gTransfer == 2)
    {
        float3 carrier = clamp(2.0 * SanitizeFinite3(modelSample.rgb, 0.5) - 1.0, -0.999, 0.999);
        edit = (1.0 / 64.0) * carrier / (1.0 - abs(carrier));
    }

    // Coring was tried here and removed: the per-frame churn's amplitude overlaps the real detail's,
    // so an amplitude threshold cannot separate them -- it only relocated the noise to the threshold.

    if (gDebugView == 3)
    {
        // Amplified and centred on grey, so both directions of the edit are visible at once.
        float3 shown = saturate(0.5 + edit * 20.0);
        gTarget[id.xy] = float4(SrgbToLinear(shown) * gDebugScale, originalSample.a);
        return;
    }

    // There is no accumulator here, and this is where one used to be.
    //
    // The edit was averaged over time -- blended with its own reprojected history to keep the part
    // that stays and cancel the part that re-randomises. It was measured as a dead end twice, once
    // with a trained DLAA pass, for the same reason both times: the model re-decides its detail with
    // the framing, so an old answer does not belong to a new frame and reprojecting it only moves
    // where the disagreement lands. The composition is re-anchored to the model every frame instead,
    // which is what makes it steady.
    //
    // Said plainly because the comment that survived the removal did not say it, and a later reader
    // took it for a description of live code and planned on top of machinery that is not here.

    // Matched residual: put the two pictures being compared at the same resolution first.
    //
    // Classic hands the composition below a low-resolution `proxy` and a low-resolution `model`
    // against a full-resolution `original`. Those disagree by the downsample's blur as well as by the
    // model's edit, and the composition cannot tell the two apart -- it reads the blur as headroom
    // the frame has and the model never saw, which is a term that grows as the model's raster
    // shrinks. That is the resolution-dependent colour shift measured at 50%.
    //
    // Here the frame's own proxy is rebuilt at full resolution -- the encode is a pure function, so
    // SoftKnee reproduces it exactly -- and only the model's *difference* is carried up from small.
    // Both pictures handed to the composition are then full resolution and the only thing that came
    // from the reduced raster is the edit itself, which is what was wanted from it.
    //
    // The residual and its cube scaling are hhkbble's, from the multi-pass PR against this fork.
    //
    // Taken only when the model actually worked below the frame. At the same rate the arithmetic
    // collapses -- fullProxy + (model - proxy) is model, because proxy already is the frame's own
    // full-resolution proxy -- but only in exact arithmetic. The one this pass reads has been through
    // an sRGB encode, a texture, and a decode, while the one SoftKnee rebuilds has not, so the two
    // agree to within the proxy surface's precision rather than exactly. Skipping the path when there
    // is no residual to carry makes 100% bit-identical to Classic instead of nearly identical, which
    // is what lets this default to on: the shipped configuration cannot be changed by it at all.
    uint proxyW, proxyH;
    gSource.GetDimensions(proxyW, proxyH);
    const bool modelRanSmall = proxyW != gWidth || proxyH != gHeight;

    if ((gTransfer == 1 && modelRanSmall) || gTransfer == 2)
    {
        // Saturated, because that is what the encode does and this has to reproduce it exactly.
        //
        // The encode writes LinearToSrgb(SoftKnee(frame / paperwhite)), and LinearToSrgb saturates
        // before it does anything else -- so the proxy the Classic path reads back is always inside
        // the unit cube. SoftKnee alone is not: it rolls luminance off above 0.75 but leaves a
        // channel free to sit above 1, and with a measured white point of 0.1 in a dark red interior
        // the red channel of anything lit is far above 1.
        //
        // CubeScaleResidual then computes (1 - P) / d to find how far the residual may travel before
        // leaving the cube. With P above 1 that numerator is negative, alpha comes out negative,
        // saturate(alpha) is zero, and the entire edit is discarded -- leaving the knee'd proxy as
        // the answer, which is darker than the frame everywhere the knee fired. That is the darker,
        // redder 50% picture: not the working scale, and not the residual idea, just a proxy that was
        // never clamped the way the one it stands in for is.
        // Rebuilt with the same curve the encode used, so the two agree on the frame's own proxy.
        // Passthrough must reproduce the encode's passthrough branch, which writes the frame raw --
        // SoftKnee returns it unchanged there, so both non-passthrough branches are gated behind the
        // same passthrough check the encode has. Without this, a reversible + matched-residual +
        // already-tone-mapped frame would Neutwo-compress a frame the encode left raw. Neutwo already
        // lands in [0,1), so it needs no saturate.
        float3 fullProxy = gPassthrough != 0
                               ? saturate(original)
                               : (gReversibleMode == 0   ? saturate(SoftKnee(original))
                                  : gReversibleMode >= 3 ? HybridEncode(original)
                                                         : NeutwoEncode(original));
        proxy = fullProxy;
        proxyLuma = dot(proxy, kLuma);

        // At the same rate there is no residual to carry: the model's own picture is already at the
        // frame's resolution, and P + (m - p) collapses to m exactly.
        model = CubeScaleResidual(fullProxy, fullProxy + edit);
        if (gTransfer == 2) modelDirect = model;
    }

    // The composition. The model's answer is not treated as a difference to add onto the frame -- it
    // is a complete picture in its own right, and it is brought back by rescaling it to sit where the
    // original's luminance says it should. Adding a difference is what let colour run away: nothing
    // bounded where the sum landed, so a warm subject could arrive green. Here both ends of every
    // blend are well-formed pictures, so everything between them is one too.
    float modelLuma = dot(model, kLuma);
    float3 upgraded;

    if (modelLuma <= 1e-5)
    {
        // The model can return an empty frame for an input it cannot read. Rescaling that collapses
        // the picture to black, so the frame is handed back untouched instead.
        upgraded = original;
    }
    else
    {
        float ratio;

        if (originalLuma < proxyLuma)
        {
            // Below what the proxy showed: the frame's own luminance is the target.
            ratio = originalLuma / max(proxyLuma, 1e-6);
        }
        else
        {
            // Above it, the difference is headroom the proxy could not represent -- brightness the
            // frame really has and the model never saw. It is handed back on top of the model's own
            // answer rather than scaled away, which is what kept highlights from being muted.
            ratio = (modelLuma + max(0.0, originalLuma - proxyLuma)) / modelLuma;
        }

        // Saturated deliberately. lerp past 1 extrapolates -- it walks beyond the target instead
        // of towards it -- and the target is the only well-formed picture in the pair, so the
        // guarantee stated above holds on [0,1] and nowhere else. Past it the channels spread apart
        // faster than luminance does, and the guard below cannot pull them back: it scales the whole
        // triple by one scalar, which corrects luminance while preserving the spread. A lit face at
        // strength 2 clips to white, and it starts to show just past 1.
        //
        // Strength above 1 is carried below instead, as an amplification of the luminance ratio,
        // which the guard does bound.
        upgraded = lerp(original, HueOkLab(model * ratio, model), saturate(gTransferStrength));
    }

    // Detail strength decides how much of the model's picture is reached at all; colour strength
    // decides whether its colour comes with it. At 0 the frame keeps the game's own hue exactly and
    // only its light carries the model's verdict; at 1 the model's colour arrives as well.
    float upgradedLuma = dot(upgraded, kLuma);

    // A ratio against a dark pixel is unbounded, and clamping it is not the same as taming it.
    //
    // In linear light divided by paper white a shadowed pixel sits around a thousandth, so a tiny
    // absolute edit from the model becomes an enormous ratio, hits the clamp, and doubles that
    // pixel's brightness. The next frame it lands slightly differently and the pixel drops back.
    // That is the boiling: patches of lighter colour crawling over otherwise still geometry, worst
    // where the picture is darkest.
    //
    // Adding the same floor above and below leaves bright pixels alone -- where luminance is far
    // larger than the floor the term vanishes -- while making the ratio fall smoothly to one as
    // luminance approaches zero. No edit at all is the right answer for a pixel with no light in it.
    const float kRatioFloor = 1.0 / 512.0;
    float lumaRatio = (upgradedLuma + kRatioFloor) / (originalLuma + kRatioFloor);

    // Where detail strength above 1 goes.
    //
    // Raising the ratio to a power rather than extending the blend keeps every property that
    // matters: it cannot go negative, it leaves a pixel the model did not change alone -- one to any
    // power is one -- and it moves brightening and darkening by the same factor in opposite
    // directions, so it does not favour either. Most importantly the result is still a ratio, so the
    // guard below binds it, which is exactly what the extrapolated blend escaped.
    //
    // The correction further down divides by the unamplified ratio, so the composed picture ends up
    // at the original's luminance times the bounded ratio either way. Strength 1 leaves this the
    // identity and the pass bit-identical to before.
    const float amplified = pow(max(lumaRatio, 1e-6), 1.0 + max(gTransferStrength - 1.0, 0.0));

    // The guard binds the composed picture, not only the luminance-only end of the blend below.
    //
    // It used to bind `original * lumaRatio` and nothing else -- the colour-strength-zero end. At
    // colour strength 1, which is the default, that end is never reached, so the guard did nothing
    // at all and whatever the model returned was handed back unbounded. Where the soft knee fires
    // that stays hidden, because the headroom term above makes the frame's own brightness dominate
    // the result. Where the knee does not fire -- any dark scene -- the ratio degenerates to one,
    // the composition reduces to the model's own picture, and every frame the model re-decided
    // arrived whole. That is the flicker reported in Nioh 3, and it worsened with paper white
    // because the model's answer is multiplied by it on the way out.
    //
    // Two-sided, because the failure measured there was a collapse and not a runaway: red fell 57%
    // while an upward-only bound sat watching it. The control's own help text said darkening was
    // deliberately uncapped; that was decided before there was a case against it.
    //
    // One scalar, taken from luminance, applied to the whole triple. A per-channel bound is a hue
    // distorter -- on a saturated pixel the smallest channel reaches the bound first, so an
    // achromatic edit lands as a colour shift.
    const float guard = max(gMaxRatio, 1.0);
    float boundedRatio = clamp(amplified, 1.0 / guard, guard);

    // Exactly one while the ratio is already inside the guard, so a frame that never needed bounding
    // is untouched rather than rounded, and strength zero stays bit-identical.
    upgraded *= boundedRatio / max(lumaRatio, 1e-6);

    // Both ends of the blend now sit inside the same guard, so neither needs a second clamp.
    //
    // Colour strength 0..1 blends toward the model's colour, as before. Above 1 it OVER-SATURATES: the
    // blend caps at the model's colour (min), then the excess scales CHROMA in OkLab -- L and hue kept,
    // only a and b grow -- and ClampAp1 below pulls anything past the gamut back by desaturating toward
    // neutral, NOT by clipping channels. So an over-driven colour rolls off at the gamut boundary
    // (maximally vivid but still a real colour with detail) instead of flattening into a blown peak.
    // At strength 1 the boost is the identity, so <=1 is bit-identical to before.
    float3 result = lerp(original * boundedRatio, upgraded, min(gColourStrength, 1.0));

    if (gColourStrength > 1.0)
        result = ClampAp1(FromOkLab(float3(1.0, gColourStrength, gColourStrength) * ToOkLab(max(result, 0.0))));

    // Replace mode: the model's answer IS the picture, decoded through Neutwo's exact inverse, with
    // NONE of the composition above -- no ratio, no highlight guard, no palette blend. This is the
    // RenoDX reversible-bridge behaviour and the second half of the A/B: composed vs pure model. On a
    // passthrough frame the model already worked in the frame's own space, so it is taken directly.
    if (gReversibleMode == 2)
        result = gPassthrough != 0 ? modelDirect : NeutwoDecode(modelDirect);
    else if (gReversibleMode == 4)
        result = gPassthrough != 0 ? modelDirect : HybridDecode(modelDirect);

    // Back out of the normalised space the composition worked in.
    result *= normScale;

    if (gSkinProtection != 0)
    {
        // Classify the untouched frame, never NR's recoloured output. Controls
        // attenuate the final edit, including replace mode and all model passes.
        float3 displayRgb = gPassthrough != 0 ? original : LinearToSrgb(saturate(original));
        float mask = SkinColourWeight(displayRgb);
        float detail = lerp(gEnvironmentDetail, gSkinDetail, mask);
        float colour = lerp(gEnvironmentColour, gSkinColour, mask);
        float baseY = dot(max(originalSample.rgb, 0.0), kLuma);
        float editedY = dot(max(result, 0.0), kLuma);
        float wantedY = lerp(baseY, editedY, detail);
        float3 baseChroma = originalSample.rgb / max(baseY, 1e-6);
        float3 editedChroma = result / max(editedY, 1e-6);
        // Exact endpoints avoid changing the default image or fully protected pixels.
        if (detail == 0.0 && colour == 0.0)
            result = originalSample.rgb;
        else if (detail != 1.0 || colour != 1.0)
            result = ClampAp1(lerp(baseChroma, editedChroma, colour) * wantedY);
        if (gShowSkinMask != 0)
            result = mask.xxx * normScale;
    }

    // The side being shown untouched takes the frame as it arrived, past every step above.
    if (showOriginal)
        result = originalSample.rgb;

    // The letterbox. The sampler clamps rather than wrapping, so without this the bars would be the
    // frame's edge row smeared down the screen.
    if (outsideFrame)
        result = float3(0.0, 0.0, 0.0);

    // A hairline so the two sides are never mistaken for one picture.
    if (onDivider)
        result = float3(WhitePoint(), WhitePoint(), WhitePoint());

    gTarget[id.xy] = float4(max(result, float3(0.0, 0.0, 0.0)), originalSample.a);
}
