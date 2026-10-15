// Removes a UI texture from the presented frame, leaving the frame as it was before the UI got blended on it.
// Same bindings as HudCopy, the frame is copied, read from FrameCopy and written back to Frame.

#ifdef VK_MODE
cbuffer Params : register(b0, space0)
#else
cbuffer Params : register(b0)
#endif
{
    float OpaqueAlpha;
    uint Premultiplied;
};

#ifdef VK_MODE
[[vk::binding(1, 0)]]
#endif
Texture2D<float4> Ui : register(t0);

#ifdef VK_MODE
[[vk::binding(2, 0)]]
#endif
Texture2D<float3> FrameCopy : register(t1);

#ifdef VK_MODE
[[vk::binding(3, 0)]]
#endif
RWTexture2D<float3> Frame : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;

    float4 ui = Ui.Load(int3(pixelCoord, 0));
    float3 frame = FrameCopy.Load(int3(pixelCoord, 0));

    // frame = uiColor + scene * (1 - alpha)
    float3 uiColor = Premultiplied != 0 ? ui.rgb : ui.rgb * ui.a;

    // Opaque UI leaves nothing of the scene, black still differs from the UI so FG treats it as UI
    float3 scene = ui.a < OpaqueAlpha ? saturate((frame - uiColor) / (1.0f - ui.a)) : 0.0f;

    Frame[pixelCoord] = scene;
}
