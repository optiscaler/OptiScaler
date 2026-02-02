cbuffer Params : register(b0)
{
    float DiffThreshold;
};

Texture2D<float3> Hudless : register(t0);
Texture2D<float3> PresentCopy : register(t1);

RWTexture2D<float3> Present : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;

    float3 hudless = Hudless.Load(int3(pixelCoord, 0));
    float3 present = PresentCopy.Load(int3(pixelCoord, 0));
    
    float3 diff = abs(hudless - present);
    float delta = max(max(diff.r, diff.g), diff.b);

    float uiMask = smoothstep(DiffThreshold, DiffThreshold * 2.0f, delta);
    
    float3 ui = (present - hudless) * uiMask;
    
    Present[pixelCoord] = hudless + ui;
}
