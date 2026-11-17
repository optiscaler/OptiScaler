#ifdef VK_MODE
cbuffer Params : register(b0, space0)
#else
cbuffer Params : register(b0)
#endif
{
    // Base sharpen amount
    float Sharpness;

    // Depth format
    int DepthIsLinear; // 0 = device/nonlinear, 1 = already linear
    int DepthIsReversed; // 0 = normal Z, 1 = reversed Z

    // Depth rejection
    float DepthScale;
    float DepthBias;

    // Nonlinear depth only.
    // These coefficients must be generated for the non-reversed depth convention.
    // If DepthIsReversed != 0, reversed handling is applied in shader.
    float DepthLinearA;
    float DepthLinearB;
    float DepthLinearC;

    // Motion adaptive
    int DynamicSharpenEnabled;
    int DisplaySizeMV;
    int Debug; // 0=off, 1=motion, 2=depth, 3=combined

    float MotionSharpness; // usually negative
    float MvScaleX;
    float MvScaleY;
    float MotionThreshold;
    float MotionScaleLimit;

    // Depth mapping from output pixel space to depth texel space
    float DepthTextureScale;

    // Output control
    int ClampOutput;

    // Dimensions
    int DisplayWidth;
    int DisplayHeight;
    int MotionWidth;
    int MotionHeight;
    int DepthWidth;
    int DepthHeight;
};

#ifdef VK_MODE
[[vk::binding(1, 0)]]
#endif
Texture2D<float4> Source : register(t0);

#ifdef VK_MODE
[[vk::binding(2, 0)]]
#endif
Texture2D<float2> Motion : register(t1);

#ifdef VK_MODE
[[vk::binding(3, 0)]]
#endif
Texture2D<float> DepthTex : register(t2);

#ifdef VK_MODE
[[vk::binding(4, 0)]]
#endif
RWTexture2D<float4> Dest : register(u0);

static const float3 kLumaCoeff = float3(0.2126, 0.7152, 0.0722);

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

int2 ClampCoord(int2 p)
{
    return int2(clamp(p.x, 0, DisplayWidth - 1), clamp(p.y, 0, DisplayHeight - 1));
}

int2 ClampMotionCoord(int2 p)
{
    return int2(clamp(p.x, 0, MotionWidth - 1), clamp(p.y, 0, MotionHeight - 1));
}

int2 ClampDepthCoord(int2 p)
{
    return int2(clamp(p.x, 0, DepthWidth - 1), clamp(p.y, 0, DepthHeight - 1));
}

float3 SafeLoadColor(int2 p)
{
    return Source.Load(int3(ClampCoord(p), 0)).rgb;
}

float SafeLoadRawDepthAtCoord(int2 p)
{
    return DepthTex.Load(int3(ClampDepthCoord(p), 0)).r;
}

float2 SafeLoadMotion(int2 p)
{
    return Motion.Load(int3(ClampMotionCoord(p), 0)).rg;
}

float LinearizeDepth(float rawDepth)
{
    float z = rawDepth;

    if (DepthIsLinear > 0)
    {
        if (DepthIsReversed > 0)
            z = 1.0 - z;

        return z;
    }

    if (DepthIsReversed > 0)
    {
        float nearPlane = DepthLinearB - DepthLinearC;
        return DepthLinearA / max(nearPlane + z * DepthLinearC, 1e-6);
    }

    return DepthLinearA / max(DepthLinearB - z * DepthLinearC, 1e-6);
}

float SafeLoadDepthLinearFromOutputPixel(int2 pixelCoord)
{
    float2 df = (float2(pixelCoord) + 0.5) * DepthTextureScale;
    int2 depthCoord = int2(df);
    return LinearizeDepth(SafeLoadRawDepthAtCoord(depthCoord));
}

float DistanceSharpnessBoost(float linearDepth)
{
    // Works best if linearDepth is view-space-ish positive distance.
    // log2 keeps the boost gradual and avoids overboosting very far depth.
    float d = max(linearDepth, 1e-4);
    float boost = saturate((log2(d) - 4.0) * 0.15);

    // 1.0 near, up to 1.35 far
    return lerp(1.0, 1.35, boost);
}

float2 EstimateDepthGradientFromTaps(
    float centerDepth,
    float depthUp,
    float depthLeft,
    float depthRight,
    float depthDown)
{
    float gxF = depthRight - centerDepth;
    float gxB = centerDepth - depthLeft;
    float gyF = depthDown - centerDepth;
    float gyB = centerDepth - depthUp;

    // Prefer the smoother local derivative.
    float gx = abs(gxF) < abs(gxB) ? gxF : gxB;
    float gy = abs(gyF) < abs(gyB) ? gyF : gyB;

    float maxGrad = abs(centerDepth) * 0.05;
    return clamp(float2(gx, gy), -maxGrad, maxGrad);
}

float DepthWeightGrad(float centerDepth, float sampleDepth, float2 gradient, int2 offset)
{
    float predicted = centerDepth + dot(float2(offset), gradient);
    float residual = abs(sampleDepth - predicted);

    residual /= max(abs(centerDepth), 1e-4);

    // More dead-zone before rejection starts.
    residual = max(residual - DepthBias - 1e-5, 0.0);

    // Softer falloff.
    float w = saturate(1.0 - residual * DepthScale);

    // Do not fully collapse taps unless it is a strong depth break.
    return lerp(0.65, 1.0, w);
}

float ComputeAdaptiveSharpness(int2 pixelCoord)
{
    float setSharpness = Sharpness;

    if (DynamicSharpenEnabled > 0)
    {
        float2 mv;

        if (DisplaySizeMV > 0)
        {
            mv = SafeLoadMotion(pixelCoord);
        }
        else
        {
            float2 mvf = (float2(pixelCoord) + 0.5) * (float2(MotionWidth, MotionHeight) / float2(DisplayWidth, DisplayHeight));
            int2 mvCoord = int2(mvf);
            mv = SafeLoadMotion(mvCoord);
        }

        float motion = max(abs(mv.x * MvScaleX), abs(mv.y * MvScaleY));
        float add = 0.0;

        if (motion > MotionThreshold)
        {
            float denom = max(MotionScaleLimit - MotionThreshold, 1e-6);
            add = ((motion - MotionThreshold) / denom) * MotionSharpness;
        }

        add = clamp(add, min(0.0, MotionSharpness), max(0.0, MotionSharpness));
        setSharpness += add;
    }

    return clamp(setSharpness, 0.0, 2.0);
}

float3 ApplyDebugTint(
    float3 color,
    float baseSharpness,
    float adaptiveSharpness,
    float edgeSharpness,
    float finalSharpness,
    float distanceBoost,
    int debugMode)
{
    float motionBoost = max(adaptiveSharpness - baseSharpness, 0.0);
    float motionReduce = max(baseSharpness - adaptiveSharpness, 0.0);

    // Blue means edge-based sharpen reduction only.
    float edgeReduce = max(adaptiveSharpness - edgeSharpness, 0.0);

    float distanceIncrease = max(distanceBoost - 1.0, 0.0);

    if (debugMode > 0)
    {
        color.r *= 1.0 + 12.0 * motionBoost;
        color.r += 0.35 * distanceIncrease;

        color.g *= 1.0 + 12.0 * motionReduce;
        color.b *= 1.0 + 12.0 * edgeReduce;
    }

    return color;
}

float ComputeEdgeFactorFromTaps(
    float centerLuma,
    float centerDepth,
    float2 depthGrad,
    float lumaUp,
    float lumaLeft,
    float lumaRight,
    float lumaDown,
    float depthUp,
    float depthLeft,
    float depthRight,
    float depthDown)
{
    float lumaSum = 0.0;
    lumaSum += abs(lumaUp - centerLuma);
    lumaSum += abs(lumaLeft - centerLuma);
    lumaSum += abs(lumaRight - centerLuma);
    lumaSum += abs(lumaDown - centerLuma);

    float depthEdge = 1.0;
    depthEdge = min(depthEdge, DepthWeightGrad(centerDepth, depthUp, depthGrad, int2(0, -1)));
    depthEdge = min(depthEdge, DepthWeightGrad(centerDepth, depthLeft, depthGrad, int2(-1, 0)));
    depthEdge = min(depthEdge, DepthWeightGrad(centerDepth, depthRight, depthGrad, int2(1, 0)));
    depthEdge = min(depthEdge, DepthWeightGrad(centerDepth, depthDown, depthGrad, int2(0, 1)));

    // Average visible brightness difference around this pixel.
    float lumaAvg = lumaSum * 0.25;

    // 0 = luma does not confirm the depth edge
    // 1 = luma strongly confirms the depth edge
    float lumaConfirm = saturate((lumaAvg - 0.02) * 18.0);

    // Luma is confirmation, not an edge source.
    // Even without luma confirmation, keep some depth protection.
    float depthTrust = lerp(0.40, 1.0, lumaConfirm);

    return lerp(1.0, depthEdge, depthTrust);
}

float ComputeLocalLumaRangeFromTaps(
    float centerLuma,
    float lumaUp,
    float lumaLeft,
    float lumaRight,
    float lumaDown)
{
    float lMin = centerLuma;
    float lMax = centerLuma;

    lMin = min(lMin, lumaUp);
    lMax = max(lMax, lumaUp);

    lMin = min(lMin, lumaLeft);
    lMax = max(lMax, lumaLeft);

    lMin = min(lMin, lumaRight);
    lMax = max(lMax, lumaRight);

    lMin = min(lMin, lumaDown);
    lMax = max(lMax, lumaDown);

    return lMax - lMin;
}

float Max3(float3 v)
{
    return max(v.r, max(v.g, v.b));
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    int2 p = int2(DTid.xy);

    if (p.x >= DisplayWidth || p.y >= DisplayHeight)
        return;

    float3 c = SafeLoadColor(p);
    float adaptiveSharpness = ComputeAdaptiveSharpness(p);

    if (adaptiveSharpness <= 0.0)
    {
        float3 outColor = c;

        if (Debug > 0)
            outColor = ApplyDebugTint(outColor, Sharpness, adaptiveSharpness, adaptiveSharpness, adaptiveSharpness, 1.0, Debug);

        if (ClampOutput > 0)
            outColor = saturate(outColor);

        Dest[p] = float4(outColor, 1.0);
        return;
    }

    // -------------------------------------------------------------------------
    // Shared center/cross data
    // -------------------------------------------------------------------------

    float centerDepth = SafeLoadDepthLinearFromOutputPixel(p);
    float centerLuma = dot(c, kLumaCoeff);

    int2 pUp = p + int2(0, -1);
    int2 pLeft = p + int2(-1, 0);
    int2 pRight = p + int2(1, 0);
    int2 pDown = p + int2(0, 1);

    // Cached cross-neighbor colors. These replace repeated loads in edge/luma/RCAS paths.
    float3 colorUp = SafeLoadColor(pUp);
    float3 colorLeft = SafeLoadColor(pLeft);
    float3 colorRight = SafeLoadColor(pRight);
    float3 colorDown = SafeLoadColor(pDown);

    // Cached cross-neighbor depths. These replace repeated depth loads and linearization.
    float depthUp = SafeLoadDepthLinearFromOutputPixel(pUp);
    float depthLeft = SafeLoadDepthLinearFromOutputPixel(pLeft);
    float depthRight = SafeLoadDepthLinearFromOutputPixel(pRight);
    float depthDown = SafeLoadDepthLinearFromOutputPixel(pDown);

    // Cached lumas derived from cached colors.
    float lumaUp = dot(colorUp, kLumaCoeff);
    float lumaLeft = dot(colorLeft, kLumaCoeff);
    float lumaRight = dot(colorRight, kLumaCoeff);
    float lumaDown = dot(colorDown, kLumaCoeff);

    // -------------------------------------------------------------------------
    // Adaptive sharpness / edge protection
    // -------------------------------------------------------------------------

    float2 depthGrad = EstimateDepthGradientFromTaps(centerDepth, depthUp, depthLeft, depthRight, depthDown);

    float edgeFactor = ComputeEdgeFactorFromTaps(centerLuma, centerDepth, depthGrad, lumaUp, lumaLeft,
                                                 lumaRight, lumaDown, depthUp, depthLeft, depthRight, depthDown);

    float edgeSharpness = adaptiveSharpness * lerp(0.2, 1.0, edgeFactor);

    float distanceBoost = DistanceSharpnessBoost(centerDepth);
    float motionStability = saturate(adaptiveSharpness / max(Sharpness, 1e-4));
    distanceBoost = lerp(1.0, distanceBoost, motionStability);

    float boostedSharpness = edgeSharpness * distanceBoost;

    float lumaRange = ComputeLocalLumaRangeFromTaps(centerLuma, lumaUp, lumaLeft, lumaRight, lumaDown);

    float unstable = saturate((lumaRange - 0.12) * 4.0);
    unstable *= unstable;

    boostedSharpness *= lerp(1.0, 0.9, unstable);
    float finalSharpness = min(boostedSharpness, 2.0);

    // -------------------------------------------------------------------------
    // RCAS 4-neighbor pattern
    // -------------------------------------------------------------------------

    float3 e = c;

    // Keep original RCAS naming convention.
    float3 bRaw = colorUp;
    float3 dRaw = colorLeft;
    float3 fRaw = colorRight;
    float3 hRaw = colorDown;

    // Normalize RCAS into a local 0..1-ish range for HDR/pre-tonemap input.
    // Use original taps before depth rejection so the scale reflects the true local neighborhood.
    float localScale = Max3(e);
    localScale = max(localScale, Max3(bRaw));
    localScale = max(localScale, Max3(dRaw));
    localScale = max(localScale, Max3(fRaw));
    localScale = max(localScale, Max3(hRaw));
    localScale = max(localScale, 1e-4);

    // Depth weights for cross taps.
    float wb = DepthWeightGrad(centerDepth, depthUp, depthGrad, int2(0, -1));
    float wd = DepthWeightGrad(centerDepth, depthLeft, depthGrad, int2(-1, 0));
    float wf = DepthWeightGrad(centerDepth, depthRight, depthGrad, int2(1, 0));
    float wh = DepthWeightGrad(centerDepth, depthDown, depthGrad, int2(0, 1));

    // Prevent RCAS from pulling color across depth discontinuities.
    // Unsafe neighbors are blended back toward center.
    float3 b = lerp(e, bRaw, wb);
    float3 d = lerp(e, dRaw, wd);
    float3 f = lerp(e, fRaw, wf);
    float3 h = lerp(e, hRaw, wh);

    localScale = max(localScale, 1.0);

    float3 en = max(e / localScale, 0.0);
    float3 bn = max(b / localScale, 0.0);
    float3 dn = max(d / localScale, 0.0);
    float3 fn = max(f / localScale, 0.0);
    float3 hn = max(h / localScale, 0.0);

    // RCAS min/max ring.
    float3 minRGB = min(min(bn, dn), min(fn, hn));
    float3 maxRGB = max(max(bn, dn), max(fn, hn));

    float2 peakC = float2(1.0, -4.0);

    // Limiter.
    float3 hitMin = minRGB / max(4.0 * maxRGB, 1e-5);
    float3 hitMax = (peakC.xxx - maxRGB) / max(4.0 * minRGB + peakC.yyy, -1e-5);

    float3 lobeRGB = max(-hitMin, hitMax);

    // RCAS is happier with roughly 0..1 range.
    float rcasSharpness = saturate(finalSharpness);
    float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * rcasSharpness;
    float rcpL = rcp(4.0 * lobe + 1.0);

    float3 output = (((bn + dn + fn + hn) * lobe + en) * rcpL) * localScale;
    output = max(output, 0.0);

    if (Debug > 0)
        output = ApplyDebugTint(output, Sharpness, adaptiveSharpness, edgeSharpness, finalSharpness, distanceBoost, Debug);

    if (ClampOutput > 0)
        output = saturate(output);

    Dest[p] = float4(output, 1.0);
}