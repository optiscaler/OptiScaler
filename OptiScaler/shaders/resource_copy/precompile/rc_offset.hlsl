// Copies a Size rectangle from SrcOffset in Source to DstOffset in Dest, an Extent bigger than Size repeats
// the rectangle's right and bottom edges to fill it
#ifdef VK_MODE
struct RCParams
{
    uint2 SrcOffset;
    uint2 DstOffset;
    uint2 Size;
    uint2 Extent;
};

[[vk::push_constant]] RCParams Params;
#define SrcOffset Params.SrcOffset
#define DstOffset Params.DstOffset
#define Size Params.Size
#define Extent Params.Extent
#else
cbuffer Params : register(b0)
{
    uint2 SrcOffset;
    uint2 DstOffset;
    uint2 Size;
    uint2 Extent;
};
#endif

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
    if (any(DTid.xy >= Extent))
        return;

    Dest[DstOffset + DTid.xy] = Source.Load(int3(SrcOffset + min(DTid.xy, Size - 1), 0));
}
