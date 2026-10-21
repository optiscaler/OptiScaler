#ifdef VK_MODE
[[vk::binding(0, 0)]]
#endif
Texture2D<float2> SourceTexture : register(t0);

#ifdef VK_MODE
[[vk::binding(1, 0)]]
[[vk::image_format("rg16f")]]
#endif
RWTexture2D<float2> DestinationTexture : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    DestinationTexture.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
        return;

    DestinationTexture[dispatchThreadID.xy] = SourceTexture.Load(int3(dispatchThreadID.xy, 0));
}
