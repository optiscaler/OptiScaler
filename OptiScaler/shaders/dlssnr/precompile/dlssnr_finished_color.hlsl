// HDR10 <-> linear Rec.709 in scRGB units (1 = 80 nits).
// Separate from the existing NR shader so SDR and ordinary NR keep their compiled code.
#ifdef VULKAN
[[vk::binding(0, 0)]]
#endif
cbuffer Params : register(b0)
{
    uint mode; float exposureScale; uint width; uint height;
    float sceneIsLinear; float curveUpdateWeight; uint curveHistoryValid; float maxRatio;
};
#ifdef VULKAN
[[vk::binding(1, 0)]]
#endif
Texture2D<float4> source : register(t0);
#ifdef VULKAN
[[vk::binding(2, 0)]]
#endif
Texture2D<float4> reference : register(t1);
#ifdef VULKAN
[[vk::binding(3, 0)]]
#endif
Texture2D<float4> original : register(t2);
#ifdef VULKAN
[[vk::binding(4, 0)]]
#endif
Texture2D<float4> response : register(t3);
#ifdef VULKAN
[[vk::binding(5, 0)]]
#endif
RWTexture2D<float4> target : register(u0);

float3 DecodePQ(float3 code)
{
    const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0, c2 = 2413.0 / 128.0, c3 = 2392.0 / 128.0;
    float3 p = pow(saturate(code), 1.0 / m2);
    return pow(max(p - c1, 0.0) / max(c2 - c3 * p, 1e-6), 1.0 / m1) * 125.0;
}
float3 EncodePQ(float3 light)
{
    const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0, c2 = 2413.0 / 128.0, c3 = 2392.0 / 128.0;
    float3 p = pow(saturate(light / 125.0), m1);
    return pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}

// Modes 0..5 retain their original meanings. 6/7 fit scRGB/PQ response;
// 8/9 apply it. The curve is 48 half-stop bins in exposure-normalised scene light.
// Match kDlssNrHdrCurveBins in DlssNr_Common.h.
static const uint kCurveBins = 48;
static const float kCurveMin = -12.0, kCurveStep = 0.5;
static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);
static const float3x3 kTo709 = {
     1.6604910, -0.5876411, -0.0728499,
    -0.1245505,  1.1328999, -0.0083494,
    -0.0181508, -0.1005789,  1.1187297 };

float3 DisplayLight(float3 code, bool pq)
{
    return pq ? mul(kTo709, DecodePQ(code)) : code;
}

bool CurvePair(uint sampleIndex, uint2 size, bool pq, out float2 pair)
{
    uint2 cell = uint2(sampleIndex % 64, sampleIndex / 64);
    uint2 at = min(uint2((float2(cell) + 0.5) * float2(size) / float2(64, 16)), size - 1);
    float3 scene = reference.Load(int3(at, 0)).rgb;
    float3 display = DisplayLight(source.Load(int3(at, 0)).rgb, pq);
    float x = dot(scene, kLuma) / max(exposureScale, 1e-4);
    float y = dot(display, kLuma);
    pair = float2(log2(max(x, 1e-12)), log2(max(y, 1e-12)));
    return all(isfinite(scene)) && all(isfinite(display)) && x > 1e-6 && y > 1e-6;
}

float4 FitResponse(uint bin, bool pq)
{
    uint w, h;
    source.GetDimensions(w, h);
    uint rw, rh;
    reference.GetDimensions(rw, rh);
    if (w != rw || h != rh || bin >= kCurveBins) return 0.0;
    float center = kCurveMin + (bin + 0.5) * kCurveStep;
    float4 fit = 0.0;
    // A local linear fit, then robust refit to suppress inconsistent HUD/effect samples.
    // No CPU readback or histogram atomics; about 98K paired samples across all bins.
    [loop] for (uint iteration = 0; iteration < 2; ++iteration)
    {
        float total = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, syy = 0.0;
        [loop] for (uint i = 0; i < 1024; ++i)
        {
            float2 pair;
            if (!CurvePair(i, uint2(w, h), pq, pair)) continue;
            float x = pair.x - center, y = pair.y;
            float weight = saturate(1.0 - abs(x) / 0.75);
            if (iteration != 0)
            {
                float error = (y - (fit.x + fit.y * x)) / 0.5;
                weight *= saturate(1.0 - error * error);
            }
            total += weight;
            sx += weight * x; sy += weight * y;
            sxx += weight * x * x; sxy += weight * x * y; syy += weight * y * y;
        }
        if (total < 4.0) return 0.0;
        float mx = sx / total, my = sy / total;
        float variance = max(sxx / total - mx * mx, 0.0);
        if (variance < 0.005) return 0.0; // A flat scene cannot identify a brightness response.
        float covariance = sxy / total - mx * my;
        float slope = covariance / variance;
        if (!isfinite(slope) || slope < -0.05 || slope > 2.0) return 0.0;
        slope = max(slope, 0.0);
        float errorVariance = max(syy / total - my * my - 2.0 * slope * covariance + slope * slope * variance, 0.0);
        float confidence = saturate(total / 12.0) * saturate(variance / 0.04) *
                           saturate(1.0 - errorVariance / 0.04);
        fit = float4(my - slope * mx, slope, confidence, 1.0);
    }
    if (curveHistoryValid != 0 && fit.z > 0.5)
    {
        float4 previous = original.Load(int3(bin, 0, 0));
        // Reuse only a consistent fit. Scene/exposure changes must not drag an old curve forward.
        if (all(isfinite(previous)) && previous.z > 0.5 && abs(previous.x - fit.x) < 0.25 &&
            abs(previous.y - fit.y) < 0.25)
            fit.xy = lerp(previous.xy, fit.xy, clamp(curveUpdateWeight, 0.1, 1.0));
    }
    return all(isfinite(fit)) ? fit : 0.0;
}

// x = predicted log2 display luminance, y = confidence. No extrapolation outside observed bins.
float2 LookupResponse(float sceneLog)
{
    float at = (sceneLog - kCurveMin) / kCurveStep - 0.5;
    if (!isfinite(at) || at < 0.0 || at >= kCurveBins - 1.0) return 0.0;
    uint index = uint(at);
    float4 a = response.Load(int3(index, 0, 0));
    float4 b = response.Load(int3(index + 1, 0, 0));
    if (!all(isfinite(a)) || !all(isfinite(b)) || b.x < a.x) return 0.0;
    return float2(lerp(a.x, b.x, frac(at)), min(a.z, b.z));
}

float3 MatchBrightness(float3 light, float3 scene, float3 gain)
{
    float3 fallback = light * gain;
    float baseY = dot(scene, kLuma) / max(exposureScale, 1e-4);
    float editedY = dot(scene * gain, kLuma) / max(exposureScale, 1e-4);
    float finalY = dot(light, kLuma), fallbackY = dot(fallback, kLuma);
    if (!all(isfinite(scene)) || min(min(baseY, editedY), min(finalY, fallbackY)) <= 1e-6) return fallback;
    float2 baseFit = LookupResponse(log2(baseY)), editedFit = LookupResponse(log2(editedY));
    // Per-pixel agreement also rejects many overlays/bloom regions that do not follow the global fit.
    float confidence = min(baseFit.y, editedFit.y) * saturate(1.0 - abs(log2(finalY) - baseFit.x) / 0.35);
    if (confidence <= 0.0) return fallback;
    float delta = exp2(editedFit.x) - exp2(baseFit.x);
    if (!isfinite(delta) || delta * (editedY - baseY) < 0.0) return fallback;
    float limit = clamp(maxRatio, 1.0, 8.0);
    float wantedY = clamp(finalY + delta, finalY / limit, finalY * limit);
    // Correct luminance only; retain the existing RGB transfer's chromaticity approximation.
    // A common scale preserves chromaticity while keeping every RGB gain inside the original bounds.
    float minScale = (1.0 / limit) / min(gain.r, min(gain.g, gain.b));
    float maxScale = limit / max(gain.r, max(gain.g, gain.b));
    float3 matched = fallback * clamp(wantedY / fallbackY, minScale, maxScale);
    return all(isfinite(matched)) ? lerp(fallback, matched, confidence) : fallback;
}
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) return;
    if (mode == 6 || mode == 7)
    {
        target[id.xy] = FitResponse(id.x, mode == 7);
        return;
    }
    float4 pixel = source.Load(int3(id.xy, 0));
    const float3x3 to709 = {
         1.6604910, -0.5876411, -0.0728499,
        -0.1245505,  1.1328999, -0.0083494,
        -0.0181508, -0.1005789,  1.1187297 };
    const float3x3 to2020 = {
        0.6274039, 0.3292830, 0.0433131,
        0.0690973, 0.9195404, 0.0113623,
        0.0163914, 0.0880133, 0.8955953 };
    if (mode == 5)
    {
        // Carry a bounded relative change through DLSS. An absolute signed residual
        // around 0.5 loses small dark-scene edits when stored in FP16; dividing that
        // quantization error by the dark SR reference produces large colour noise.
        float3 base = max(pixel.rgb, 0.0);
        float3 edited = max(reference.Load(int3(id.xy, 0)).rgb, 0.0);
        if (!all(isfinite(base)) || !all(isfinite(edited)))
        { target[id.xy] = float4(0.5, 0.5, 0.5, 1.0); return; }
        if (sceneIsLinear < 0.5)
        { base = pow(base, 2.2); edited = pow(edited, 2.2); }
        float floorValue = max(max(base.r, max(base.g, base.b)) * 0.02,
                               max(exposureScale, 1e-4) * 1e-4);
        float limit = clamp(maxRatio, 1.0, 8.0);
        float3 gain = clamp(1.0 + (edited - base) / max(base, floorValue), 1.0 / limit, limit);
        target[id.xy] = float4(0.5 + log2(gain) / 8.0, 1.0);
        return;
    }
    if (mode >= 2)
    {
        // t0 = finished picture, t2 = log-gain carrier upscaled by private DLSS.
        // Apply bounded relative changes in linear light. This approximates the game's
        // unknown tonemapper and colour grading; no scene-linear delta is added to display code.
        float3 carrier = original.Load(int3(id.xy, 0)).rgb;
        if (!all(isfinite(carrier)) || all(carrier == 0.5))
        { target[id.xy] = pixel; return; }
        float limit = clamp(maxRatio, 1.0, 8.0);
        float3 gain = exp2(clamp((carrier - 0.5) * 8.0, -log2(limit), log2(limit)));
        bool pq = mode == 4 || mode == 9;
        float3 light = mode == 2 ? pow(max(pixel.rgb, 0.0), 2.2) :
                       pq ? mul(to709, DecodePQ(pixel.rgb)) : pixel.rgb;
        light = (mode == 8 || mode == 9) ? MatchBrightness(light, reference.Load(int3(id.xy, 0)).rgb, gain)
                                        : light * gain;
        float3 result = mode == 2 ? pow(saturate(light), 1.0 / 2.2) :
                        pq ? EncodePQ(mul(to2020, light)) : light;
        target[id.xy] = float4(all(isfinite(result)) ? result : pixel.rgb, pixel.a);
        return;
    }
    if (mode == 0)
    {
        // Negative components carry wide-gamut colours; retain them in FP16.
        target[id.xy] = float4(mul(to709, DecodePQ(pixel.rgb)), pixel.a);
    }
    else
    {
        float4 base = original.Load(int3(id.xy, 0));
        float3 result = EncodePQ(mul(to2020, pixel.rgb));
        target[id.xy] = float4(all(isfinite(result)) ? result : base.rgb, base.a);
    }
}
