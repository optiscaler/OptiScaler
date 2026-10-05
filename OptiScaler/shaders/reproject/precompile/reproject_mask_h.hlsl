cbuffer Params : register(b0)
{
    uint ScreenWidth;
    uint ScreenHeight;
    float InvScreenWidth;
    float InvScreenHeight;
    
    uint DepthWidth;
    uint DepthHeight;
    float2 _Pad0;
    
    float UiDiffThreshold;
    float DepthCutoff;
    float DitherWidthPx;
    uint CutoffExpandPx;
    
    uint EdgeMode;
    uint ShowStaticElements;
    uint InvertedDepth;
    uint FakeFrame; // need to provide fakePresent

    float TanHalfFovX;
    float TanHalfFovY;
    float InvTanHalfFovX;
    float InvTanHalfFovY;
    
    float4 ReprojectionMatrixRow0;
    float4 ReprojectionMatrixRow1;
    float4 ReprojectionMatrixRow2;
};

Texture2D<float> Depth : register(t2);

RWTexture2D<float> DepthMaskTempOut : register(u1);
RWTexture2D<float> DepthMaskOut : register(u2);

// Depth mask prepass, horizontal part
// Thresholds depth and expands the cutout by CutoffExpandPx along X
// With CutoffExpandPx == 0 the vertical pass is skipped and the result goes straight to the final mask
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixelCoord = int2(dispatchThreadID.xy);

    if (pixelCoord.x >= int(DepthWidth) || pixelCoord.y >= int(DepthHeight))
        return;

    int radius = int(CutoffExpandPx);
    int maxX = int(DepthWidth) - 1;

    // Cutout if any depth in the window crosses the cutoff, so only min/max is needed
    float minDepth = 1.0f;
    float maxDepth = 0.0f;

    for (int x = -radius; x <= radius; ++x)
    {
        int sampleX = clamp(pixelCoord.x + x, 0, maxX);
        float d = Depth.Load(int3(sampleX, pixelCoord.y, 0));
        minDepth = min(minDepth, d);
        maxDepth = max(maxDepth, d);
    }

    bool isCutout = InvertedDepth ? (maxDepth > DepthCutoff) : (minDepth < DepthCutoff);
    float mask = isCutout ? 1.0f : 0.0f;

    if (radius == 0)
        DepthMaskOut[pixelCoord] = mask;
    else
        DepthMaskTempOut[pixelCoord] = mask;
}
