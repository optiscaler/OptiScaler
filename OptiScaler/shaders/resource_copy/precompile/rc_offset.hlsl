// Copies a Size rectangle from SrcOffset in Source to DstOffset in Dest
cbuffer Params : register(b0)
{
    uint2 SrcOffset;
    uint2 DstOffset;
    uint2 Size;
};

Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Dest : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= Size))
        return;

    Dest[DstOffset + DTid.xy] = Source.Load(int3(SrcOffset + DTid.xy, 0));
}
