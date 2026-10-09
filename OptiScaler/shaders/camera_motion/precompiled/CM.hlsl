cbuffer Params : register(b0)
{
    float4 Reprojection0;
    float4 Reprojection1;
    float4 Reprojection2;
    float4 Reprojection3;
    uint Width;
    uint Height;
    uint Left;
    uint Top;
    float DepthScaleX;
    float DepthScaleY;
    uint DepthLeft;
    uint DepthTop;
};

Texture2D<float4> Velocity : register(t0);
Texture2D<float> Depth : register(t1);

RWTexture2D<float2> Output : register(u0);

// Velocity in UV units as current - previous, alpha 0 where the game wrote no velocity.
// Output in pixels as previous - current, the camera's motion fills the pixels without velocity.
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= Width || dispatchThreadID.y >= Height)
        return;

    uint2 pixel = dispatchThreadID.xy + uint2(Left, Top);
    float4 velocity = Velocity.Load(int3(pixel, 0));
    float2 size = float2(Width, Height);

    if (velocity.a != 0.0f)
    {
        Output[pixel] = -velocity.xy * size;
        return;
    }

    float2 uv = (float2(dispatchThreadID.xy) + 0.5f) / size;
    uint2 depthPixel = uint2(float2(dispatchThreadID.xy) * float2(DepthScaleX, DepthScaleY)) + uint2(DepthLeft, DepthTop);
    float depth = Depth.Load(int3(depthPixel, 0));

    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 previous = ndc.x * Reprojection0 + ndc.y * Reprojection1 + depth * Reprojection2 + Reprojection3;

    if (previous.w <= 0.0f)
    {
        Output[pixel] = float2(0.0f, 0.0f);
        return;
    }

    float2 previousNdc = previous.xy / previous.w;
    float2 previousUv = float2(previousNdc.x * 0.5f + 0.5f, 0.5f - previousNdc.y * 0.5f);

    Output[pixel] = (previousUv - uv) * size;
}
