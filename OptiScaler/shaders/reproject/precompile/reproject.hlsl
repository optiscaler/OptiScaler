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
    float2 Pad0;
    
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

    // Vectorized Camera Ray (un-normalized)
    float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    float3 ray = float3(ndc * float2(TanHalfFovX, TanHalfFovY), 1.0f);

    // Reproject Ray using matrix multiplication
    float3 sourceRay = float3(
        dot(ReprojectionMatrixRow0.xyz, ray),
        dot(ReprojectionMatrixRow1.xyz, ray),
        dot(ReprojectionMatrixRow2.xyz, ray)
    );

    // Perspective Divide & Source UV Calculation
    float2 sourceUV = 0.0f;
    bool isValidDepth = sourceRay.z > 0.00001f;

    if (isValidDepth)
    {
        float2 sourceNDC = (sourceRay.xy / sourceRay.z) * float2(InvTanHalfFovX, InvTanHalfFovY);
        sourceUV = sourceNDC * float2(0.5f, -0.5f) + 0.5f;
    }

    // Bounds & Reprojection
    bool inside = isValidDepth && all(sourceUV >= 0.0f) && all(sourceUV <= 1.0f);
    bool modeWithBackground = EdgeMode == 2 || EdgeMode == 3;

    float3 reprojectedGame = 0.0f;
    if (inside)
    {
        reprojectedGame = Hudless.SampleLevel(LinearClampSampler, sourceUV, 0.0f);
    }
    else if (EdgeMode == 1 && isValidDepth)
    {
        reprojectedGame = Hudless.SampleLevel(LinearClampSampler, saturate(sourceUV), 0.0f);
    }
    else if (modeWithBackground)
    {
        float3 background = Hudless.Load(int3(pixelCoord, 0));
        reprojectedGame = inside ? lerp(background, reprojectedGame, 1.0f) : background;
    }

    if (modeWithBackground && inside)
    {
        const float ditherWidthPx = ScreenHeight / 16.0f;

        // Only measure distance to reprojected edges that fall inside the screen bounds
        float distLeft = (sourceUV.x < uv.x) ? sourceUV.x * ScreenWidth : ditherWidthPx;
        float distRight = (sourceUV.x > uv.x) ? (1.0f - sourceUV.x) * ScreenWidth : ditherWidthPx;
        float distTop = (sourceUV.y < uv.y) ? sourceUV.y * ScreenHeight : ditherWidthPx;
        float distBottom = (sourceUV.y > uv.y) ? (1.0f - sourceUV.y) * ScreenHeight : ditherWidthPx;

        float edgeDistancePx = min(min(distLeft, distRight), min(distTop, distBottom));
        float projectedProbability = smoothstep(0.0f, ditherWidthPx, edgeDistancePx);
        
        float dither = 0.0f;
        if (EdgeMode == 2)
            dither = Bayer4x4(pixelCoord);
        else if (EdgeMode == 3)
            dither = HashNoise(pixelCoord);
        
        float3 background = Hudless.Load(int3(pixelCoord, 0));

        reprojectedGame = dither < projectedProbability ? reprojectedGame : background;
    }
    
    // Final UI Blend
    Present[pixelCoord] = lerp(reprojectedGame, present, uiMask);
}
