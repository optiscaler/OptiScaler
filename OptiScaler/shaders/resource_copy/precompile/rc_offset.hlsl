// Copies a Size rectangle from SrcOffset in Source to DstOffset in Dest
#ifdef VK_MODE
struct RCParams
{
    uint2 SrcOffset;
    uint2 DstOffset;
    uint2 Size;
};

[[vk::push_constant]] RCParams Params;
#define SrcOffset Params.SrcOffset
#define DstOffset Params.DstOffset
#define Size Params.Size
#else
cbuffer Params : register(b0)
{
    uint2 SrcOffset;
    uint2 DstOffset;
    uint2 Size;
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
    if (any(DTid.xy >= Size))
        return;

    Dest[DstOffset + DTid.xy] = Source.Load(int3(SrcOffset + DTid.xy, 0));
}
