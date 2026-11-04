#ifdef VK_MODE
[[vk::binding(0, 0)]]
#endif
Texture2D<float4> SourceTexture : register(t0);

// The copies write many formats, the store converts to the view's format (shaderStorageImageWriteWithoutFormat)
#ifdef VK_MODE
[[vk::binding(1, 0)]]
[[vk::image_format("unknown")]]
#endif
RWTexture2D<float4> DestinationTexture : register(u0);

// Shader to perform the conversion
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    float4 srcColor = SourceTexture.Load(int3(dispatchThreadID.xy, 0));
    DestinationTexture[dispatchThreadID.xy] = srcColor;
}