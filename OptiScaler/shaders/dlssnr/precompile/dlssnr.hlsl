
cbuffer Params : register(b0)
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
};

// Colours outside the AP1 gamut are impossible on any display and read as sparkle where a bright
// saturated pixel is pushed further. Clamping inside AP1 and coming back keeps everything reachable.
float3 ClampAp1(float3 color)
{
    const float3x3 bt709_to_ap1 = { 0.613097, 0.339523, 0.047379,
                                    0.070194, 0.916354, 0.013452,
                                    0.020616, 0.109570, 0.869815 };
    const float3x3 ap1_to_bt709 = { 1.705051, -0.621792, -0.083259,
                                    -0.130256, 1.140805, -0.010548,
                                    -0.024003, -0.128969, 1.152972 };
    return mul(ap1_to_bt709, max(mul(bt709_to_ap1, color), float3(0.0, 0.0, 0.0)));
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
float3 HueOkLab(float3 incorrect, float3 correct)
{
    float3 incorrectLab = ToOkLab(incorrect);
    const float3 correctLab = ToOkLab(correct);
    const float incorrectChroma = length(incorrectLab.yz);
    const float correctChroma = length(correctLab.yz);
    incorrectLab.yz = correctLab.yz * (correctChroma == 0.0 ? 1.0 : incorrectChroma / correctChroma);
    return ClampAp1(FromOkLab(incorrectLab));
}

Texture2D<float4>   gSource   : register(t0);  // encode: the frame. resolve: the proxy.
Texture2D<float4>   gModel    : register(t1);  // resolve: what the model returned.
Texture2D<float4>   gOriginal : register(t2);  // resolve: the untouched frame.
Texture2D<float4>   gMotion   : register(t3);  // resolve, accumulating: the game's motion vectors.
RWTexture2D<float4> gTarget   : register(u0);  // encode: the proxy. resolve: the frame.
RWTexture2D<float4> gKeep     : register(u1);  // encode: the untouched copy. unused by the resolve.
SamplerState        gLinear   : register(s0);  // so the edit can be read at a different size

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

    return display;
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

    // The meter. One thread per tile of a 64x64 grid over the frame, writing that tile's mean
    // luminance. The frame is raw linear here -- this runs before the encode, on purpose, because the
    // number being looked for is what the encode's divisor should be.
    //
    // A mean per tile, then a percentile across tiles on the CPU. Not the frame's mean, which is what
    // the meter this replaces measured: that reads scene brightness, and a dark scene then asks for a
    // small divisor and hands the model a blown picture anyway. Not the frame's maximum either, which
    // one specular hit decides.
    if (gMode == 3)
    {
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
        float3 display = SoftKnee(frame / max(gWhitePoint, 1e-4));

        gTarget[id.xy] = float4(LinearToSrgb(display), source.a);
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
    float4 originalSample = gCompareMode == 1 ? gOriginal.SampleLevel(gLinear, cmpUv, 0)
                                              : gOriginal.Load(int3(id.xy, 0));

    // All three pictures have to share a scale before their luminances can be compared. The proxy and
    // the model come back from an sRGB decode, so they sit in 0..1 where 1 is the white point; the
    // frame is raw linear and runs well past that. Comparing them unnormalised is a real bug and it
    // reads exactly like the model has stopped adding detail: with the frame several times larger,
    // the shadow branch never fires, every pixel takes the highlight branch, and the clamp flattens
    // the result to a near-constant scale. Colour still moves, because that comes from the model's
    // own hue, which is what makes the failure so confusing to look at.
    const float normScale = gPassthrough != 0 ? 1.0 : max(gWhitePoint, 1e-4);
    float3 original = originalSample.rgb / normScale;

    float originalLuma = dot(original, kLuma);
    float proxyLuma = dot(proxy, kLuma);

    if (gDebugView == 1)
    {
        gTarget[id.xy] = float4(proxy * gWhitePoint, originalSample.a);
        return;
    }

    if (gDebugView == 2)
    {
        gTarget[id.xy] = float4(model * gWhitePoint, originalSample.a);
        return;
    }

    float3 edit = model - proxy;

    // Coring was tried here and removed: the per-frame churn's amplitude overlaps the real detail's,
    // so an amplitude threshold cannot separate them -- it only relocated the noise to the threshold.

    if (gDebugView == 3)
    {
        // Amplified and centred on grey, so both directions of the edit are visible at once.
        float3 shown = saturate(0.5 + edit * 20.0);
        gTarget[id.xy] = float4(SrgbToLinear(shown) * gWhitePoint, originalSample.a);
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

    if (gTransfer == 1 && modelRanSmall)
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
        float3 fullProxy = saturate(SoftKnee(original));
        proxy = fullProxy;
        proxyLuma = dot(proxy, kLuma);

        // At the same rate there is no residual to carry: the model's own picture is already at the
        // frame's resolution, and P + (m - p) collapses to m exactly.
        model = CubeScaleResidual(fullProxy, fullProxy + edit);
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

        upgraded = lerp(original, HueOkLab(model * ratio, model), gTransferStrength);
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
    float boundedRatio = clamp(lumaRatio, 1.0 / guard, guard);

    // Exactly one while the ratio is already inside the guard, so a frame that never needed bounding
    // is untouched rather than rounded, and strength zero stays bit-identical.
    upgraded *= boundedRatio / max(lumaRatio, 1e-6);

    // Both ends of the blend now sit inside the same guard, so neither needs a second clamp.
    float3 result = lerp(original * boundedRatio, upgraded, gColourStrength);

    // Back out of the normalised space the composition worked in.
    result *= normScale;

    // The side being shown untouched takes the frame as it arrived, past every step above.
    if (showOriginal)
        result = originalSample.rgb;

    // The letterbox. The sampler clamps rather than wrapping, so without this the bars would be the
    // frame's edge row smeared down the screen.
    if (outsideFrame)
        result = float3(0.0, 0.0, 0.0);

    // A hairline so the two sides are never mistaken for one picture.
    if (onDivider)
        result = float3(gWhitePoint, gWhitePoint, gWhitePoint);

    gTarget[id.xy] = float4(max(result, float3(0.0, 0.0, 0.0)), originalSample.a);
}
