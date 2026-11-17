#ifdef VK_MODE
cbuffer Params : register(b0, space0)
#else
cbuffer Params : register(b0)
#endif
{
    float Sharpness;

    int DepthIsLinear;
    int DepthIsReversed;

    float DepthScale;
    float DepthBias;

    float DepthLinearA;
    float DepthLinearB;
    float DepthLinearC;

    int DynamicSharpenEnabled;
    int DisplaySizeMV;
    int Debug;

    float MotionSharpness;
    float MvScaleX;
    float MvScaleY;
    float MotionThreshold;
    float MotionScaleLimit;

    float DepthTextureScale;

    int ClampOutput;

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

// 16x16 thread group + 1-pixel border on each side for 3x3 color taps.
groupshared float4 s_color[18][18];

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

float Luma(float3 c)
{
    return dot(c, kLumaCoeff);
}

float Max3(float3 v)
{
    return max(v.r, max(v.g, v.b));
}

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
        // Reversed depth substitutes z' = 1 - z into A / (B - z*C):
        // A / ((B - C) + z*C)
        float revOffset = DepthLinearB - DepthLinearC;
        return DepthLinearA / max(revOffset + z * DepthLinearC, 1e-6);
    }

    return DepthLinearA / max(DepthLinearB - z * DepthLinearC, 1e-6);
}

float SafeLoadDepthLinearFromDepthCoord(int2 depthCoord)
{
    return LinearizeDepth(SafeLoadRawDepthAtCoord(depthCoord));
}

float2 EstimateDepthGradientFromTaps(float centerDepth, float depthUp, float depthLeft,
                                     float depthRight, float depthDown)
{
    float gxF = depthRight - centerDepth;
    float gxB = centerDepth - depthLeft;
    float gyF = depthDown - centerDepth;
    float gyB = centerDepth - depthUp;

    float gx = abs(gxF) < abs(gxB) ? gxF : gxB;
    float gy = abs(gyF) < abs(gyB) ? gyF : gyB;

    float maxGrad = max(abs(centerDepth) * 0.05, 1e-3);
    return clamp(float2(gx, gy), -maxGrad, maxGrad);
}

float DepthResidualGrad(float centerDepth, float sampleDepth, float2 gradient, int2 offset)
{
    float predicted = centerDepth + dot(float2(offset), gradient);
    float residual = abs(sampleDepth - predicted);

    static const float kDepthNearBias = 0.01;
    residual /= max(abs(centerDepth), kDepthNearBias);
    residual = max(residual - DepthBias, 0.0);

    return residual;
}

float DepthWeightFromResidual(float residual, float floorWeight)
{
    float w = saturate(1.0 - residual * DepthScale);
    return lerp(floorWeight, 1.0, w);
}

float ComputeTapHaloRisk(float centerLuma, float sampleLuma, float depthResidual)
{
    float depthBreak = smoothstep(0.0015, 0.018, depthResidual);

    float lumaDenom = max(max(abs(centerLuma), abs(sampleLuma)), 1e-4);
    float relativeLumaDiff = abs(sampleLuma - centerLuma) / lumaDenom;

    float lumaBreak = smoothstep(0.08, 0.45, relativeLumaDiff);
    float asymmetry = smoothstep(0.12, 0.65, relativeLumaDiff);

    float risk = depthBreak * lumaBreak * lerp(0.65, 1.0, asymmetry);

    return saturate(risk * risk * (3.0 - 2.0 * risk));
}

float DepthWeightGradAdaptive(float centerDepth, float sampleDepth, float2 gradient, int2 offset,
                              float centerLuma, float sampleLuma, out float haloRisk)
{
    float residual = DepthResidualGrad(centerDepth, sampleDepth, gradient, offset);

    haloRisk = ComputeTapHaloRisk(centerLuma, sampleLuma, residual);

    // Normal areas stay soft; high-risk silhouette taps get hard rejection.
    float floorWeight = lerp(0.45, 0.07, haloRisk);

    return DepthWeightFromResidual(residual, floorWeight);
}

// Expects positive view/world-space-like linear depth.
// If linear depth is normalized 0..1, this boost is effectively disabled.
float DistanceSharpnessBoost(float linearDepth)
{
    float d = max(linearDepth, 1e-4);
    float boost = saturate((log2(d) - 4.0) * 0.15);

    return lerp(1.0, 1.12, boost);
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
            // For lower-resolution motion, map the output pixel center into motion texture space.
            // Display-size motion uses the same integer pixel coordinate directly.
            float2 mvf = (float2(pixelCoord) + 0.5) * (float2(MotionWidth, MotionHeight) / float2(DisplayWidth, DisplayHeight));
            int2 mvCoord = int2(mvf);
            mv = SafeLoadMotion(mvCoord);
        }

        float motion = max(abs(mv.x * MvScaleX), abs(mv.y * MvScaleY));
        
        float denom = max(MotionScaleLimit - MotionThreshold, 0.05);

        float add = (max(motion - MotionThreshold, 0.0) / denom) * MotionSharpness;
        add = clamp(add, min(0.0, MotionSharpness), max(0.0, MotionSharpness));

        setSharpness += add;
    }

    return clamp(setSharpness, 0.0, 2.0);
}

float3 ApplyDebugTint(float3 color, float baseSharpness, float adaptiveSharpness, float edgeSharpness,
                      float finalSharpness, float distanceBoost, int debugMode)
{
    float motionBoost = max(adaptiveSharpness - baseSharpness, 0.0);
    float motionReduce = max(baseSharpness - adaptiveSharpness, 0.0);
    float edgeReduce = max(adaptiveSharpness - edgeSharpness, 0.0);
    float distanceIncrease = max(distanceBoost - 1.0, 0.0);

    if (debugMode > 0)
    {
        color.r *= 1.0 + 12.0 * motionBoost;
        color.g *= 1.0 + 12.0 * motionReduce;
        color.b *= 1.0 + 12.0 * edgeReduce;

        // Distance boost overlay, intentionally weaker than motion/edge debug.
        color.r *= 1.0 + 4.0 * distanceIncrease;
        color.g *= 1.0 + 2.0 * distanceIncrease;
    }

    return color;
}

float ComputeEdgeFactorFromCrossWeights(float centerLuma, float lumaUp, float lumaLeft, float lumaRight, float lumaDown,
                                        float wUp, float wLeft, float wRight, float wDown)
{
    float lumaSum = 0.0;
    lumaSum += abs(lumaUp - centerLuma);
    lumaSum += abs(lumaLeft - centerLuma);
    lumaSum += abs(lumaRight - centerLuma);
    lumaSum += abs(lumaDown - centerLuma);

    float minW = min(min(wUp, wLeft), min(wRight, wDown));
    float avgW = (wUp + wLeft + wRight + wDown) * 0.25;

    float depthEdge = lerp(minW, avgW, 0.50);

    float lumaAvg = lumaSum * 0.25;

    float lumaBase = max(max(abs(centerLuma), 0.25 * (abs(lumaUp) + abs(lumaLeft) + abs(lumaRight) + abs(lumaDown))), 1e-4);
    float relativeLumaAvg = lumaAvg / lumaBase;

    float lumaConfirm = saturate((relativeLumaAvg - 0.02) * 18.0);
    
    float depthBreakStrength = 1.0 - depthEdge;
    float depthTrustFloor = lerp(0.15, 0.30, smoothstep(0.25, 0.80, depthBreakStrength));
    float depthTrust = lerp(depthTrustFloor, 1.0, lumaConfirm);

    return lerp(1.0, depthEdge, depthTrust);
}

float3 ApplyLumaRatio(float3 color, float oldY, float newY)
{
    return color * (max(newY, 0.0) / max(oldY, 1e-6));
}

float SoftHaloPair(float a, float b)
{
    float geometric = sqrt(max(a * b, 0.0));
    float conservative = max(a, b);

    return lerp(geometric, conservative, 0.55);
}

// -----------------------------------------------------------------------------
// Directional adaptive sharpen core
// -----------------------------------------------------------------------------

float3 ApplyDirectionalSharpen(float3 centerColor, float3 upColor, float3 leftColor, float3 rightColor, float3 downColor,
                               float3 upLeftColor, float3 upRightColor, float3 downLeftColor, float3 downRightColor,
                               float finalSharpness, float edgeFactor,
                               float haloRiskH, float haloRiskV, float haloRiskDA, float haloRiskDB)
{
    float localScale = Max3(centerColor);
    localScale = max(localScale, Max3(upColor));
    localScale = max(localScale, Max3(leftColor));
    localScale = max(localScale, Max3(rightColor));
    localScale = max(localScale, Max3(downColor));
    localScale = max(localScale, Max3(upLeftColor));
    localScale = max(localScale, Max3(upRightColor));
    localScale = max(localScale, Max3(downLeftColor));
    localScale = max(localScale, Max3(downRightColor));
    localScale = max(localScale, 1e-4);

    float invLocalScale = rcp(localScale);
    
    float rawCenterY = Luma(max(centerColor, 0.0));

    float cY = max(Luma(max(centerColor, 0.0)) * invLocalScale, 1e-6);
    float uY = max(Luma(max(upColor, 0.0)) * invLocalScale, 1e-6);
    float lY = max(Luma(max(leftColor, 0.0)) * invLocalScale, 1e-6);
    float rY = max(Luma(max(rightColor, 0.0)) * invLocalScale, 1e-6);
    float dY = max(Luma(max(downColor, 0.0)) * invLocalScale, 1e-6);

    float ulY = max(Luma(max(upLeftColor, 0.0)) * invLocalScale, 1e-6);
    float urY = max(Luma(max(upRightColor, 0.0)) * invLocalScale, 1e-6);
    float dlY = max(Luma(max(downLeftColor, 0.0)) * invLocalScale, 1e-6);
    float drY = max(Luma(max(downRightColor, 0.0)) * invLocalScale, 1e-6);

    float minY = min(cY, min(min(uY, dY), min(lY, rY)));
    minY = min(minY, min(min(ulY, urY), min(dlY, drY)));

    float maxY = max(cY, max(max(uY, dY), max(lY, rY)));
    maxY = max(maxY, max(max(ulY, urY), max(dlY, drY)));

    float localRange3x3 = maxY - minY;
    float relativeRange = localRange3x3 / max(cY, 1e-4);

    // Direction candidates.
    float hY = (lY + rY) * 0.5;
    float vY = (uY + dY) * 0.5;
    float diagAY = (ulY + drY) * 0.5;
    float diagBY = (urY + dlY) * 0.5;

    float hDiff = abs(cY - hY);
    float vDiff = abs(cY - vY);
    float daDiff = abs(cY - diagAY);
    float dbDiff = abs(cY - diagBY);

    static const float kDirectionHaloSuppression = 0.35;

    hDiff *= lerp(1.0, 1.0 - kDirectionHaloSuppression, haloRiskH);
    vDiff *= lerp(1.0, 1.0 - kDirectionHaloSuppression, haloRiskV);
    daDiff *= lerp(1.0, 1.0 - kDirectionHaloSuppression, haloRiskDA);
    dbDiff *= lerp(1.0, 1.0 - kDirectionHaloSuppression, haloRiskDB);

    float bestDiff = hDiff;
    float secondDiff = max(max(vDiff, daDiff), dbDiff);
    float refY = hY;
    int selectedDir = 0; // 0=H, 1=V, 2=diagA, 3=diagB

    if (vDiff > bestDiff)
    {
        secondDiff = max(max(hDiff, daDiff), dbDiff);
        bestDiff = vDiff;
        refY = vY;
        selectedDir = 1;
    }

    if (daDiff > bestDiff)
    {
        secondDiff = max(max(hDiff, vDiff), dbDiff);
        bestDiff = daDiff;
        refY = diagAY;
        selectedDir = 2;
    }

    if (dbDiff > bestDiff)
    {
        secondDiff = max(max(hDiff, vDiff), daDiff);
        bestDiff = dbDiff;
        refY = diagBY;
        selectedDir = 3;
    }

    float selectedHaloRisk = haloRiskH;

    if (selectedDir == 1)
    {
        selectedHaloRisk = haloRiskV;
    }
    else if (selectedDir == 2)
    {
        selectedHaloRisk = haloRiskDA;
    }
    else if (selectedDir == 3)
    {
        selectedHaloRisk = haloRiskDB;
    }

    float maxHaloRisk = max(max(haloRiskH, haloRiskV), max(haloRiskDA, haloRiskDB));

    float directionSeparation = max(bestDiff - secondDiff, 0.0);

    float rawDirectionConfidence = saturate(directionSeparation / max(bestDiff, 1e-5));
    float directionConfidence = lerp(0.65, 1.0, rawDirectionConfidence);

    float ambiguityDamp = lerp(0.75, 1.0, smoothstep(0.22, 0.65, rawDirectionConfidence));

    // -------------------------------------------------------------------------
    // AA-ramp preservation
    // -------------------------------------------------------------------------

    float crossAvgY = (uY + dY + lY + rY) * 0.25;
    float isoDetail = (cY - crossAvgY) / max(crossAvgY, 1e-4);
    isoDetail = clamp(isoDetail, -4.0, 4.0);

    float rampRange = smoothstep(0.10, 0.45, relativeRange);
    float rampDirection = smoothstep(0.35, 0.85, directionConfidence);
    float rampDetail = smoothstep(0.025, 0.18, bestDiff);

    // Partial protection even if direction confidence is imperfect.
    float aaRampMask = rampRange * rampDetail * lerp(0.35, 1.0, rampDirection);

    float aaRampBlend = 0.30 * aaRampMask;
    refY = lerp(refY, crossAvgY, aaRampBlend);

    float detail = (cY - refY) / max(refY, 1e-4);
    detail = clamp(detail, -4.0, 4.0);

    float absDetail = abs(detail);
    float shapedDetail = sign(detail) * max(absDetail - 0.0010, 0.0);

    shapedDetail = shapedDetail / (1.0 + 1.45 * abs(shapedDetail));
    shapedDetail = clamp(shapedDetail, -0.42, 0.42);

    float rangeConfidence = lerp(0.72, 1.0, smoothstep(0.0004, 0.018, relativeRange));
    float edgeConfidence = lerp(0.18, 1.0, edgeFactor);

    float aaStrengthDamp = lerp(1.0, 0.55, aaRampMask);

    float posGainAA = lerp(1.50, 1.05, aaRampMask);
    float negGainAA = lerp(0.72, 0.48, aaRampMask);
    float detailGain = shapedDetail >= 0.0 ? posGainAA : negGainAA;

    float unstablePattern = (1.0 - rawDirectionConfidence) * smoothstep(0.08, 0.35, relativeRange) * (1.0 - aaRampMask);

    float strength = finalSharpness * 2.35 * directionConfidence * rangeConfidence * edgeConfidence *
                     aaStrengthDamp * ambiguityDamp * lerp(1.0, 0.78, unstablePattern);

    float edgeBlock = max(aaRampMask, maxHaloRisk);
    edgeBlock = saturate(edgeBlock * 1.35);

    float safeTextureMask = 1.0 - edgeBlock;

    float nonDirectionalFloor = 0.15 * safeTextureMask * (1.0 - aaRampMask);
    float nonDirectionalMask = max(1.0 - directionConfidence, nonDirectionalFloor);

    // Shape the isotropic fallback too.
    float absIso = abs(isoDetail);
    float shapedIso = sign(isoDetail) * max(absIso - 0.0015, 0.0);
    shapedIso = shapedIso / (1.0 + 1.25 * abs(shapedIso));
    shapedIso = clamp(shapedIso, -0.12, 0.12);

    float stableIsoDetail = smoothstep(0.006, 0.030, max(absIso - 0.0015, 0.0));
    float isoBoost = lerp(1.0, 1.50, stableIsoDetail);

    float newY = cY + cY * shapedDetail * strength * detailGain;
    float isoGain = shapedIso >= 0.0 ? 1.0 : 0.45;

    float lowContrastTexture = smoothstep(0.004, 0.045, relativeRange) * (1.0 - smoothstep(0.14, 0.40, relativeRange));
    float lowContrastIsoBoost = lerp(1.0, 1.50, lowContrastTexture);

    newY += cY * shapedIso * isoGain * finalSharpness * 0.18 * isoBoost * lowContrastIsoBoost *
            nonDirectionalMask * safeTextureMask * lerp(1.0, 0.45, unstablePattern);

    // -------------------------------------------------------------------------
    // Halo-risk-aware limiter
    // -------------------------------------------------------------------------

    float rangeY = max(maxY - minY, cY * 0.01);

    float haloLimiter = saturate(selectedHaloRisk);

    float lowerExpand = lerp(0.42, 0.06, haloLimiter);
    float upperExpand = lerp(0.48, 0.10, haloLimiter);

    float limitMin = max(0.0, minY - rangeY * lowerExpand);
    float limitMax = maxY + rangeY * upperExpand;

    newY = clamp(newY, limitMin, limitMax);

    float targetY = newY * localScale;

    return max(ApplyLumaRatio(centerColor, max(rawCenterY, 1e-6), targetY), 0.0);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID,
            uint3 GTid : SV_GroupThreadID,
            uint GI : SV_GroupIndex)
{
    int2 p = int2(DTid.xy);
    int2 lp = int2(GTid.xy);
    int2 groupBase = p - lp;

    // Flattened cooperative 18x18 color tile load.
    // 324 tile elements are distributed across 256 threads: every thread loads
    // one element, and the first 68 threads load one additional element.
    // Do not early-return before the group barrier; edge/out-of-dispatch threads
    // still participate and simply load clamped border pixels.
    for (uint i = GI; i < 324; i += 256)
    {
        uint lx = i % 18;
        uint ly = i / 18;

        int2 loadCoord = groupBase + int2(lx, ly) - int2(1, 1);
        s_color[ly][lx] = float4(SafeLoadColor(loadCoord), 0.0);
    }

    GroupMemoryBarrierWithGroupSync();

    if (p.x >= DisplayWidth || p.y >= DisplayHeight)
        return;

    float3 centerColor = s_color[lp.y + 1][lp.x + 1].rgb;
    float adaptiveSharpness = ComputeAdaptiveSharpness(p);

    if (adaptiveSharpness <= 0.0)
    {
        float3 outColor = max(centerColor, 0.0);

        if (Debug > 0)
            outColor = ApplyDebugTint(outColor, Sharpness, adaptiveSharpness, adaptiveSharpness, adaptiveSharpness, 1.0, Debug);

        if (ClampOutput > 0)
            outColor = saturate(outColor);

        Dest[p] = float4(outColor, 1.0);
        return;
    }

    float centerLuma = Luma(centerColor);

    // 9 color taps from LDS tile.
    float3 colorUpRaw = s_color[lp.y][lp.x + 1].rgb;
    float3 colorLeftRaw = s_color[lp.y + 1][lp.x].rgb;
    float3 colorRightRaw = s_color[lp.y + 1][lp.x + 2].rgb;
    float3 colorDownRaw = s_color[lp.y + 2][lp.x + 1].rgb;

    float3 colorUpLeftRaw = s_color[lp.y][lp.x].rgb;
    float3 colorUpRightRaw = s_color[lp.y][lp.x + 2].rgb;
    float3 colorDownLeftRaw = s_color[lp.y + 2][lp.x].rgb;
    float3 colorDownRightRaw = s_color[lp.y + 2][lp.x + 2].rgb;

    float lumaUp = Luma(colorUpRaw);
    float lumaLeft = Luma(colorLeftRaw);
    float lumaRight = Luma(colorRightRaw);
    float lumaDown = Luma(colorDownRaw);

    float lumaUpLeft = Luma(colorUpLeftRaw);
    float lumaUpRight = Luma(colorUpRightRaw);
    float lumaDownLeft = Luma(colorDownLeftRaw);
    float lumaDownRight = Luma(colorDownRightRaw);

    // Only cross depth taps are loaded.
    // Compute output-pixel to depth-texel mapping once.
    // Offsetting baseDf by +/-DepthTextureScale is equivalent to mapping p + offset
    // through OutputToDepthCoord(). For downsampled depth, multiple output pixels may
    // intentionally map to the same depth texel after floor().
    float2 baseDf = (float2(p) + 0.5) * DepthTextureScale;

    int2 depthCenterCoord = int2(floor(baseDf));
    int2 depthUpCoord = int2(floor(baseDf + float2(0.0, -DepthTextureScale)));
    int2 depthLeftCoord = int2(floor(baseDf + float2(-DepthTextureScale, 0.0)));
    int2 depthRightCoord = int2(floor(baseDf + float2(DepthTextureScale, 0.0)));
    int2 depthDownCoord = int2(floor(baseDf + float2(0.0, DepthTextureScale)));

    float centerDepth = SafeLoadDepthLinearFromDepthCoord(depthCenterCoord);
    float depthUp = SafeLoadDepthLinearFromDepthCoord(depthUpCoord);
    float depthLeft = SafeLoadDepthLinearFromDepthCoord(depthLeftCoord);
    float depthRight = SafeLoadDepthLinearFromDepthCoord(depthRightCoord);
    float depthDown = SafeLoadDepthLinearFromDepthCoord(depthDownCoord);
    
    float2 depthGrad = EstimateDepthGradientFromTaps(centerDepth, depthUp, depthLeft, depthRight, depthDown);

    float haloUp;
    float haloLeft;
    float haloRight;
    float haloDown;

    float wUp = DepthWeightGradAdaptive(centerDepth, depthUp, depthGrad, int2(0, -1), centerLuma, lumaUp, haloUp);
    float wLeft = DepthWeightGradAdaptive(centerDepth, depthLeft, depthGrad, int2(-1, 0), centerLuma, lumaLeft, haloLeft);
    float wRight = DepthWeightGradAdaptive(centerDepth, depthRight, depthGrad, int2(1, 0), centerLuma, lumaRight, haloRight);
    float wDown = DepthWeightGradAdaptive(centerDepth, depthDown, depthGrad, int2(0, 1), centerLuma, lumaDown, haloDown);

    // Cross depth-aware neighbor rejection.
    float3 colorUp = lerp(centerColor, colorUpRaw, wUp);
    float3 colorLeft = lerp(centerColor, colorLeftRaw, wLeft);
    float3 colorRight = lerp(centerColor, colorRightRaw, wRight);
    float3 colorDown = lerp(centerColor, colorDownRaw, wDown);

    // Synthetic diagonal protection from adjacent cross weights.
    // No diagonal depth loads.
    float wUpLeft = min(wUp, wLeft);
    float wUpRight = min(wUp, wRight);
    float wDownLeft = min(wDown, wLeft);
    float wDownRight = min(wDown, wRight);

    float haloUpLeft = max(haloUp, haloLeft);
    float haloUpRight = max(haloUp, haloRight);
    float haloDownLeft = max(haloDown, haloLeft);
    float haloDownRight = max(haloDown, haloRight);

    // Optional luma contrast strengthening for diagonal synthetic rejection.
    // This helps when diagonal color contrast is obviously risky even without diagonal depth.
    float diagDenomUL = max(max(abs(centerLuma), abs(lumaUpLeft)), 1e-4);
    float diagDenomUR = max(max(abs(centerLuma), abs(lumaUpRight)), 1e-4);
    float diagDenomDL = max(max(abs(centerLuma), abs(lumaDownLeft)), 1e-4);
    float diagDenomDR = max(max(abs(centerLuma), abs(lumaDownRight)), 1e-4);

    float diagContrastUL = smoothstep(0.18, 0.65, abs(lumaUpLeft - centerLuma) / diagDenomUL);
    float diagContrastUR = smoothstep(0.18, 0.65, abs(lumaUpRight - centerLuma) / diagDenomUR);
    float diagContrastDL = smoothstep(0.18, 0.65, abs(lumaDownLeft - centerLuma) / diagDenomDL);
    float diagContrastDR = smoothstep(0.18, 0.65, abs(lumaDownRight - centerLuma) / diagDenomDR);

    wUpLeft = lerp(wUpLeft, min(wUpLeft, 0.4), diagContrastUL * haloUpLeft);
    wUpRight = lerp(wUpRight, min(wUpRight, 0.4), diagContrastUR * haloUpRight);
    wDownLeft = lerp(wDownLeft, min(wDownLeft, 0.4), diagContrastDL * haloDownLeft);
    wDownRight = lerp(wDownRight, min(wDownRight, 0.4), diagContrastDR * haloDownRight);

    float3 colorUpLeft = lerp(centerColor, colorUpLeftRaw, wUpLeft);
    float3 colorUpRight = lerp(centerColor, colorUpRightRaw, wUpRight);
    float3 colorDownLeft = lerp(centerColor, colorDownLeftRaw, wDownLeft);
    float3 colorDownRight = lerp(centerColor, colorDownRightRaw, wDownRight);

    float edgeFactor = ComputeEdgeFactorFromCrossWeights(centerLuma, lumaUp, lumaLeft, lumaRight, lumaDown,
                                                         wUp, wLeft, wRight, wDown);

    float edgeSharpness = adaptiveSharpness * lerp(0.10, 1.0, edgeFactor);

    // DistanceSharpnessBoost only activates when centerDepth > ~16.
    // With normalized linear depth 0..1 this intentionally evaluates to 1.0.
    float distanceBoost = DistanceSharpnessBoost(centerDepth);
    float motionStability = (Sharpness > 1e-5) ? saturate(adaptiveSharpness / Sharpness) : 0.0;
    distanceBoost = lerp(1.0, distanceBoost, motionStability);

    float boostedSharpness = edgeSharpness * distanceBoost;

    float crossMin = min(centerLuma, min(min(lumaUp, lumaDown), min(lumaLeft, lumaRight)));
    float crossMax = max(centerLuma, max(max(lumaUp, lumaDown), max(lumaLeft, lumaRight)));
    float lumaRange = crossMax - crossMin;
    float lumaBase = max(crossMax, 1e-4);
    float lumaRangeRel = lumaRange / lumaBase;

    float unstable = saturate((lumaRangeRel - 0.16) * 3.0);
    unstable *= unstable;

    float motionAmount = saturate((adaptiveSharpness - Sharpness) / max(abs(MotionSharpness), 1e-4));
    float motionUnstable = unstable * motionAmount;

    boostedSharpness *= lerp(1.0, 0.85, motionUnstable);

    float finalSharpness = clamp(boostedSharpness, 0.0, 2.0);

    // Diagonal halo risks are conservative because diagonal depth is not sampled.
    // Each synthetic diagonal risk inherits from adjacent cross risks, so diagonal
    // directions may collapse toward the max cross halo risk. This is intentional:
    // it preserves halo safety while avoiding 4 extra depth loads.    
    float haloRiskH = max(haloLeft, haloRight);
    float haloRiskV = max(haloUp, haloDown);

    // Diagonal risks are intentionally conservative. No diagonal depth taps are loaded,
    // so each diagonal inherits adjacent cross risks and then softens the pair.
    // This favors silhouette safety over maximum diagonal detail.
    float haloRiskDA = SoftHaloPair(haloUpLeft, haloDownRight);
    float haloRiskDB = SoftHaloPair(haloUpRight, haloDownLeft);

    float3 output = ApplyDirectionalSharpen(centerColor, colorUp, colorLeft, colorRight, colorDown,
                                            colorUpLeft, colorUpRight, colorDownLeft, colorDownRight,
                                            finalSharpness, edgeFactor,
                                            haloRiskH, haloRiskV, haloRiskDA, haloRiskDB);

    if (Debug > 0)
    {
        output = ApplyDebugTint(output, Sharpness, adaptiveSharpness, edgeSharpness, finalSharpness, distanceBoost, Debug);
    }

    if (ClampOutput > 0)
        output = saturate(output);

    Dest[p] = float4(output, 1.0);
}
