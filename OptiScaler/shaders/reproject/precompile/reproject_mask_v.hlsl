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

Texture2D<float> DepthMaskTemp : register(t5);

RWTexture2D<float> DepthMaskOut : register(u2);

// Depth mask prepass, vertical part
// Expands the horizontally expanded mask by CutoffExpandPx along Y
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixelCoord = int2(dispatchThreadID.xy);

    if (pixelCoord.x >= int(DepthWidth) || pixelCoord.y >= int(DepthHeight))
        return;

    int radius = int(CutoffExpandPx);
    int maxY = int(DepthHeight) - 1;

    float mask = 0.0f;

    for (int y = -radius; y <= radius; ++y)
    {
        int sampleY = clamp(pixelCoord.y + y, 0, maxY);
        mask = max(mask, DepthMaskTemp.Load(int3(pixelCoord.x, sampleY, 0)));
    }

    DepthMaskOut[pixelCoord] = mask;
}
