// Based on this Reshade shader
// https://github.com/RdenBlaauwen/RCAS-for-ReShade

#ifdef VK_MODE
cbuffer Params : register(b0, space0)
#else
cbuffer Params : register(b0)
#endif
{
    float Sharpness;
    float Contrast;

    // Motion Vector Stuff
    int DynamicSharpenEnabled;
    int DisplaySizeMV;
    int Debug;
    
    float MotionSharpness;
    float MotionTextureScale;
    float MvScaleX;
    float MvScaleY;
    float Threshold;
    float ScaleLimit;
    int DisplayWidth;
    int DisplayHeight;
    int RenderWidth;
    int RenderHeight;
    int DepthEnabled;
    int DepthInverted;
    float DepthSharpness;
    float DepthEdgeThreshold;
};

#ifdef VK_MODE
[[vk::binding(1, 0)]]
#endif
Texture2D<float3> Source : register(t0);

#ifdef VK_MODE
[[vk::binding(2, 0)]]
#endif
Texture2D<float2> Motion : register(t1);

#ifdef VK_MODE
[[vk::binding(3, 0)]]
#endif
Texture2D<float> Depth : register(t2);

#ifdef VK_MODE
[[vk::binding(4, 0)]]
#endif
RWTexture2D<float3> Dest : register(u0);

int2 getDepthCoord(int2 pixel)
{
    if (RenderWidth <= 0 || RenderHeight <= 0 || DisplayWidth <= 0 || DisplayHeight <= 0)
        return pixel;

    float2 uv = (float2(pixel) + 0.5f) / float2(DisplayWidth, DisplayHeight);
    int2 coord = int2(uv * float2(RenderWidth, RenderHeight));

    return clamp(coord, int2(0, 0), int2(RenderWidth - 1, RenderHeight - 1));
}

float sampleDepth(int2 pixel)
{
    return Depth.Load(int3(getDepthCoord(pixel), 0)).r;
}

float getDepthNearWeight(float depth, int linearDepth)
{
    if (linearDepth > 0)
    {
        float linearValue = abs(depth);
        float nearWeight = rcp(1.0f + linearValue);

        if (DepthInverted > 0)
            nearWeight = 1.0f - nearWeight;

        return saturate(nearWeight);
    }

    return DepthInverted > 0 ? saturate(depth) : (1.0f - saturate(depth));
}

float getDepthEdgeSignal(float depthMin, float depthMax, int linearDepth)
{
    float depthRange = max(depthMax - depthMin, 0.0f);

    if (linearDepth > 0)
    {
        float depthScale = max(max(abs(depthMin), abs(depthMax)), 1e-5f);
        return depthRange / depthScale;
    }

    return depthRange;
}

float getRCASLuma(float3 rgb)
{
    return dot(rgb, float3(0.5, 1.0, 0.5));
}

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    float setSharpness = Sharpness;
    float depthEdge = 0.0f;
  
    if (DynamicSharpenEnabled > 0)
    {
        float2 mv;
        float motion;
        float add = 0.0f;

        if (DisplaySizeMV > 0)
            mv = Motion.Load(int3(DTid.x, DTid.y, 0)).rg;
        else
            mv = Motion.Load(int3(DTid.x * MotionTextureScale, DTid.y * MotionTextureScale, 0)).rg;

        motion = max(abs(mv.r * MvScaleX), abs(mv.g * MvScaleY));

        if (motion > Threshold)
            add = (motion / (ScaleLimit - Threshold)) * MotionSharpness;
    
        if ((add > MotionSharpness && MotionSharpness > 0.0f) || (add < MotionSharpness && MotionSharpness < 0.0f))
            add = MotionSharpness;
    
        setSharpness += add;

        if (setSharpness > 1.3f)
            setSharpness = 1.3f;
        else if (setSharpness < 0.0f)
            setSharpness = 0.0f;
    }

    if (DepthEnabled > 0 && setSharpness > 0.0f)
    {
        int2 pixel = int2(DTid.xy);
        float depthCenter = sampleDepth(pixel);
        float depthB = sampleDepth(pixel + int2(0, -1));
        float depthD = sampleDepth(pixel + int2(-1, 0));
        float depthF = sampleDepth(pixel + int2(1, 0));
        float depthH = sampleDepth(pixel + int2(0, 1));

        float depthMin = min(min(depthB, depthD), min(depthF, depthH));
        float depthMax = max(max(depthB, depthD), max(depthF, depthH));
        float maxAbsDepth = max(abs(depthCenter), max(max(abs(depthB), abs(depthD)), max(abs(depthF), abs(depthH))));
        // DLSSD often feeds linear depth instead of hardware depth.
        int linearDepth = maxAbsDepth > 1.0f ? 1 : 0;
        float nearWeight = getDepthNearWeight(depthCenter, linearDepth);

        depthEdge = saturate(getDepthEdgeSignal(depthMin, depthMax, linearDepth) / max(DepthEdgeThreshold, 1e-5f));
        nearWeight = sqrt(saturate(nearWeight));

        setSharpness *= 1.0f + (nearWeight * DepthSharpness);
        setSharpness = clamp(setSharpness, 0.0f, 1.3f);
    }
    
    float3 e = Source.Load(int3(DTid.x, DTid.y, 0)).rgb;
  
    // skip sharpening if set value == 0
    if (setSharpness == 0.0f)
    {
        if (Debug > 0 && DynamicSharpenEnabled > 0 && Sharpness > 0)
            e.g *= 1 + (12.0f * Sharpness);

        Dest[DTid.xy] = e;
        return;
    }

    float3 b = Source.Load(int3(DTid.x, DTid.y - 1, 0)).rgb;
    float3 d = Source.Load(int3(DTid.x - 1, DTid.y, 0)).rgb;
    float3 f = Source.Load(int3(DTid.x + 1, DTid.y, 0)).rgb;
    float3 h = Source.Load(int3(DTid.x, DTid.y + 1, 0)).rgb;
  
    // Min and max of ring.
    float3 minRGB = min(min(b, d), min(f, h));
    float3 maxRGB = max(max(b, d), max(f, h));
  
    // Immediate constants for peak range.
    float2 peakC = float2(1.0, -4.0);
  
    // Standard RCAS limiters
    float3 hitMin = minRGB * rcp(4.0 * maxRGB);
    float3 hitMax = (peakC.xxx - maxRGB) * rcp(4.0 * minRGB + peakC.yyy);
    float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * setSharpness;
    
    // Apply contrast adaptation only if Contrast > 0
    if (Contrast >= -10.0)
    {
        // Smooth minimum distance to signal limit divided by smooth max (directly from CAS.fx)
        float3 amp = saturate(min(minRGB, 2.0 - maxRGB) / max(maxRGB, 1e-5));
        
        // Shaping amount based on local contrast
        amp = rsqrt(amp);
        
        // Calculate the contrast adaptation factor
        float peak = -3.0 * Contrast + 8.0;
        float contrastFactor = 1.0 / max(amp.g * peak, 1.0); // Using green as representative
        
        // Apply contrast modulation - more subtle approach
        // This scales lobe strength based on local contrast without introducing softness
        lobe *= lerp(1.0, contrastFactor, Contrast); // Reduced intensity of effect with 0.5 multiplier
    }

    if (DepthEnabled > 0)
        lobe *= (1.0f - depthEdge);
    
    // Resolve with medium precision rcp
    float rcpL = rcp(4.0 * lobe + 1.0);
    float3 output = ((b + d + f + h) * lobe + e) * rcpL;
  
    if (Debug > 0 && DynamicSharpenEnabled > 0)
    {
        if (Sharpness < setSharpness)
            output.r *= 1 + (12.0f * (setSharpness - Sharpness));
        else
            output.g *= 1 + (12.0f * (Sharpness - setSharpness));
    }
  
    Dest[DTid.xy] = output;
}
