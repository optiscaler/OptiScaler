// Copies the color input with every channel kept finite: +-Inf becomes +-65504 (fp16 max), NaN becomes 0

#ifdef VK_MODE
[[vk::binding(0, 0)]]
#endif
Texture2D<float4> Source : register(t0);

#ifdef VK_MODE
[[vk::binding(1, 0)]]
#endif
RWTexture2D<float4> Dest : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint width, height;
    Dest.GetDimensions(width, height);

    if (DTid.x >= width || DTid.y >= height)
        return;

    float4 color = Source.Load(int3(DTid.xy, 0));

    // NaN goes first, clamp would turn it into 65504
    [unroll]
    for (uint i = 0; i < 4; i++)
        color[i] = isnan(color[i]) ? 0.0f : clamp(color[i], -65504.0f, 65504.0f);

    Dest[DTid.xy] = color;
}
