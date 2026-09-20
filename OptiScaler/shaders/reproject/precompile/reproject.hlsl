cbuffer Params : register(b0)
{
    float UiDiffThreshold;
    uint ScreenWidth;
    uint ScreenHeight;
    uint EdgeMode;

    float TanHalfFovX;
    float TanHalfFovY;
    float InvTanHalfFovX;
    float InvTanHalfFovY;

    float DepthCutoff;
    uint InvertedDepth;
    uint ShowStaticElements;
    float Pad0;
    
    float4 ReprojectionMatrixRow0;
    float4 ReprojectionMatrixRow1;
    float4 ReprojectionMatrixRow2;
};

Texture2D<float3> Hudless : register(t0);
Texture2D<float3> PresentCopy : register(t1);
Texture2D<float> Depth : register(t2);

RWTexture2D<float3> Present : register(u0);
SamplerState LinearClampSampler : register(s0);

float Bayer4x4(uint2 p)
{
    static const float4x4 bayer =
    {
        0.0f / 16.0f, 8.0f / 16.0f, 2.0f / 16.0f, 10.0f / 16.0f,
       12.0f / 16.0f, 4.0f / 16.0f, 14.0f / 16.0f, 6.0f / 16.0f,
        3.0f / 16.0f, 11.0f / 16.0f, 1.0f / 16.0f, 9.0f / 16.0f,
       15.0f / 16.0f, 7.0f / 16.0f, 13.0f / 16.0f, 5.0f / 16.0f
    };

    return bayer[p.y & 3][p.x & 3];
}

float HashNoise(uint2 p)
{
    uint n = p.x * 374761393u + p.y * 668265263u;
    n = (n ^ (n >> 13u)) * 1274126177u;
    n ^= n >> 16u;

    return n * (1.0f / 4294967295.0f);
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;

    if (pixelCoord.x >= ScreenWidth || pixelCoord.y >= ScreenHeight)
        return;

    // Screen UV calculation
    float2 invScreenSize = 1.0f / float2(ScreenWidth, ScreenHeight);
    float2 uv = (float2(pixelCoord) + 0.5f) * invScreenSize;

    // UI Mask extraction
    float3 hudless = Hudless.Load(int3(pixelCoord, 0));
    float3 present = PresentCopy.Load(int3(pixelCoord, 0));
    float3 diff = abs(hudless - present);
    float delta = max(diff.x, max(diff.y, diff.z));
    float uiMask = smoothstep(UiDiffThreshold, UiDiffThreshold * 2.0f, delta);
    
    // Add depth cutout mask to the uiMask
    float depth = Depth.Load(int3(pixelCoord, 0));
    bool isCutout = InvertedDepth ? depth > DepthCutoff : depth < DepthCutoff;
    uiMask = max(uiMask, isCutout ? 1.0f : 0.0f);
       
    float3 reprojectedGame = float3(0.0f, 1.0f, 0.0f); // Green

    // Vectorized Camera Ray (un-normalized)
    float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    float3 ray = float3(ndc * float2(TanHalfFovX, TanHalfFovY), 1.0f);

    // sourceUV is the reprojected position of the pixel that we want
    float3 sourceRay = float3(
        dot(ReprojectionMatrixRow0.xyz, ray),
        dot(ReprojectionMatrixRow1.xyz, ray),
        dot(ReprojectionMatrixRow2.xyz, ray)
    );

    // Perspective Divide & Source UV Calculation
    float2 sourceUV = 0.0f;
    if (sourceRay.z > 0.00001f)
    {
        float2 sourceNDC = (sourceRay.xy / sourceRay.z) * float2(InvTanHalfFovX, InvTanHalfFovY);
        sourceUV = sourceNDC * float2(0.5f, -0.5f) + 0.5f;
    }
    else
    {
        Present[pixelCoord] = lerp(reprojectedGame, present, uiMask);
        return;
    }

    bool inside = all(sourceUV >= 0.0f) && all(sourceUV <= 1.0f);
    bool modeWithBackground = EdgeMode == 2 || EdgeMode == 3;
    
    if (inside)
    {
        // Depth cutoff
        float reprojectedDepth = Depth.SampleLevel(LinearClampSampler, sourceUV, 0.0f);
        bool isCutoutReprojected = InvertedDepth ? reprojectedDepth > DepthCutoff : reprojectedDepth < DepthCutoff;
        
        if (isCutoutReprojected)
            reprojectedGame = hudless; // try to fill gap with unprojected hudless
        else
            reprojectedGame = Hudless.SampleLevel(LinearClampSampler, sourceUV, 0.0f); // the fun part
                
        if (modeWithBackground)
        {
            const float ditherWidthPx = ScreenHeight / 16.0f;

            // Only measure distance to reprojected edges that fall inside the screen bounds
            float distLeft = (sourceUV.x < uv.x) ? sourceUV.x * ScreenWidth : ditherWidthPx;
            float distRight = (sourceUV.x > uv.x) ? (1.0f - sourceUV.x) * ScreenWidth : ditherWidthPx;
            float distTop = (sourceUV.y < uv.y) ? sourceUV.y * ScreenHeight : ditherWidthPx;
            float distBottom = (sourceUV.y > uv.y) ? (1.0f - sourceUV.y) * ScreenHeight : ditherWidthPx;

            float edgeDistancePx = min(min(distLeft, distRight), min(distTop, distBottom));
            float projectedProbability = smoothstep(0.0f, ditherWidthPx, edgeDistancePx);
        
            float pattern = 0.0f;
            if (EdgeMode == 2) // Dither
            {
                pattern = Bayer4x4(pixelCoord);
            }
            else if (EdgeMode == 3) // Noise
            {
                pattern = HashNoise(pixelCoord);
            }
            
            reprojectedGame = pattern < projectedProbability ? reprojectedGame : hudless;
        }
    }
    else
    {
        // Outside the reprojection
        if (EdgeMode == 0) // Black
        {
            reprojectedGame = 0.0f;
        }
        else if (EdgeMode == 1) // Strech
        {
            reprojectedGame = Hudless.SampleLevel(LinearClampSampler, saturate(sourceUV), 0.0f);
        }
        else if (modeWithBackground)
        {
            reprojectedGame = hudless;
        }
    }
    
    // Final UI Blend
    float3 composedImage = lerp(reprojectedGame, present, uiMask);
    
    const float3 pink = float3(1.0, 0.4, 0.6);
    Present[pixelCoord] = lerp(composedImage, pink, uiMask * 0.6f * ShowStaticElements);
}
