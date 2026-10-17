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
    
    float4 CameraRight;
    float4 CameraUp;
    float4 CameraForward;
    float4 Calibration; // yawFromX, yawFromY, pitchFromX, pitchFromY

    float PixelAngle;
    float YawSign;
    float PitchSign;
    uint LateLatch;

    int2 MouseDelta;
    int2 SimStartMouse;

    float4 CameraDelta; // xyz = camera position - cache origin, zero when parallax correction is off

    float CameraNear;
    float CameraFar; // 0 = infinite
    uint CacheSize;
    float InvCacheSize;

    uint CacheEnabled;
    uint CacheReset;
    float CacheDecay;
    float CacheMaxDistance;

    // Motion vector statistics, see reproject_stats.hlsl
    float4 PrevViewX; // rows of the rotation from the current view to the view of the previous rendered frame
    float4 PrevViewY;
    float4 PrevViewZ;
    float4 PrevViewMove; // xyz = camera movement since the previous rendered frame in its view space, world units
                         // w = world units -> depth units (1 / parallax scale), 0 when not known

    float2 MvToUv; // motion vector -> uv offset to the previous frame
    uint MvWidth;
    uint MvHeight;

    float OrbitPivotDepth; // distance to the pivot of an orbiting camera in depth units, 0 when there is none
    float3 _Pad1;
};

Texture2D<float3> Hudless : register(t0);
Texture2D<float3> PresentCopy : register(t1);
Texture2D<float> Depth : register(t2);
Texture2D<float3> FakePresent : register(t3);
Texture2D<float> DepthMask : register(t4);

// Direction cache, see reproject_cache.hlsl
TextureCube<float3> CacheColor : register(t8);
TextureCube<float2> CacheGeo : register(t9);

#ifdef VK_MODE
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float3> Present : register(u0);

// Rows of the reprojection rotation, written once per dispatch by the latch pass
RWStructuredBuffer<float4> Latched : register(u3);
SamplerState LinearClampSampler : register(s0);
SamplerState BilinearClampSampler : register(s1);

float Bayer4x4(uint2 p)
{
    static const float4x4 bayer =
    {
        0.0f / 16.0f, 8.0f / 16.0f, 2.0f / 16.0f, 10.0f / 16.0f,
       12.0f / 16.0f, 4.0f / 16.0f, 14.0f / 16.0f, 6.0f / 16.0f,
        3.0f / 16.0f, 11.0f / 16.0f, 1.0f / 16.0f, 9.0f / 16.0f,
       15.0f / 16.0f, 7.0f / 16.0f, 13.0f / 16.0f, 5.0f / 16.0f
    };

    return bayer[p.y & 3][p.x & 3];
}

float HashNoise(uint2 p)
{
    uint n = p.x * 374761393u + p.y * 668265263u;
    n = (n ^ (n >> 13u)) * 1274126177u;
    n ^= n >> 16u;

    return n * (1.0f / 4294967295.0f);
}

// Depth cutout mask is built by the reproject_mask_h/reproject_mask_v prepass (already expanded by CutoffExpandPx)
bool IsDepthCutout(float2 uv)
{
    int2 maskPixel = int2(uv * float2(DepthWidth - 1, DepthHeight - 1));
    return DepthMask.Load(int3(maskPixel, 0)) > 0.5f;
}

// Returns how much the cache can be trusted in that direction, color is only valid when > 0
float SampleCache(float3 dir, out float3 color)
{
    color = 0.0f;

    if (CacheEnabled == 0)
        return 0.0f;

    float2 geo = CacheGeo.SampleLevel(BilinearClampSampler, dir, 0.0f);

    if (geo.y < 0.01f)
        return 0.0f;

    color = CacheColor.SampleLevel(BilinearClampSampler, dir, 0.0f) / geo.y;

    // Fades out old data and softens the border of the area the cache knows about
    return smoothstep(0.0f, 0.3f, geo.y);
}

float3 SourcePoint(float2 uv)
{
    float3 color;

    if (FakeFrame == 1)
        color = FakePresent.SampleLevel(LinearClampSampler, uv, 0.0f);
    else
        color = Hudless.SampleLevel(LinearClampSampler, uv, 0.0f);

    return color;
}

float3 SourceBilinear(float2 uv)
{
    float3 color;

    if (FakeFrame == 1)
        color = FakePresent.SampleLevel(BilinearClampSampler, uv, 0.0f);
    else
        color = Hudless.SampleLevel(BilinearClampSampler, uv, 0.0f);

    return color;
}

// Camera ray (un-normalized) through a screen uv
float3 RayFromUV(float2 uv)
{
    float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    return float3(ndc * float2(TanHalfFovX, TanHalfFovY), 1.0f);
}

// Perspective divide, false when the ray points behind the camera
bool ProjectRay(float3 ray, out float2 uv)
{
    uv = 0.0f;

    if (ray.z <= 0.00001f)
        return false;

    float2 ndc = (ray.xy / ray.z) * float2(InvTanHalfFovX, InvTanHalfFovY);
    uv = ndc * float2(0.5f, -0.5f) + 0.5f;
    return true;
}

// Source landed on the hands, take the nearest scene pixel past them along the reprojection
bool StepPastCutout(float2 src, float2 uv, out float3 color)
{
    color = 0.0f;

    const float2 screenSize = float2(ScreenWidth, ScreenHeight);
    const float2 away = normalize((src - uv) * screenSize + float2(0.001f, 0.0f)) / screenSize;
    const float stepPx = max(2.0f, ScreenHeight / 540.0f);

    [loop]
    for (int k = 1; k <= 48; ++k)
    {
        [unroll]
        for (int side = 0; side < 2; ++side)
        {
            float2 q = src + away * (side ? -k : k) * stepPx;

            if (all(q >= 0.0f) && all(q <= 1.0f) && !IsDepthCutout(q))
            {
                color = SourcePoint(q);
                return true;
            }
        }
    }

    return false;
}

// Outside the rendered frame, a blur of the pixels along the edge that widens with the distance past it
// Neighbouring rows get mixed so there are no streaks, hands are mostly left out so they don't smear into the edge
float3 SoftEdge(float2 src)
{
    const float2 px = float2(ScreenWidth, ScreenHeight);
    const float2 halfPx = 0.5f / px;
    const float2 inside = clamp(src, halfPx, 1.0f - halfPx);

    float2 beyond = (src - inside) * px;
    float d = length(beyond);
    float2 inward = -beyond / max(d, 0.001f);
    float2 along = float2(-inward.y, inward.x);
    float spread = min(4.0f + 0.6f * d, 192.0f);

    float3 color = 0.0f;
    float total = 0.0f;

    [unroll]
    for (int i = -3; i <= 3; ++i)
    {
        [unroll]
        for (int j = 0; j < 3; ++j)
        {
            float2 q = inside + (along * (float(i) * spread / 3.0f) + inward * (float(j) * spread * 0.5f + 0.5f)) / px;
            q = clamp(q, halfPx, 1.0f - halfPx);

            float w = 1.0f / (1.0f + 0.25f * float(i * i) + float(j));
            w *= IsDepthCutout(q) ? 0.01f : 1.0f;

            color += SourceBilinear(q) * w;
            total += w;
        }
    }

    return color / total;
}

// How well the cache covers the strip past an edge of the rendered frame around beyond, along is one pixel along the edge
// The least covered spot counts and only solid coverage does, the border of what the cache knows is patchy
// and following it closely would make the dithering come and go in streaks along the edge
float CacheCoverage(float2 beyond, float2 along)
{
    float coverage = 1.0f;

    [unroll]
    for (int i = -2; i <= 2; ++i)
    {
        float3 ray = RayFromUV(beyond + along * (float(i) * 0.5f * DitherWidthPx));
        float3 dir = normalize(ray.x * CameraRight.xyz + ray.y * CameraUp.xyz + ray.z * CameraForward.xyz);

        float3 color;
        coverage = min(coverage, SampleCache(dir, color));
    }

    return smoothstep(0.5f, 1.0f, coverage);
}

// Fill for the parts of the screen the rendered frame doesn't cover, the direction cache goes over it where it can
float3 EdgeFill(float2 src, float3 hudless)
{
    if (EdgeMode == 0) // Debug
        return float3(0.0f, 1.0f, 0.0f);

    if (EdgeMode == 1) // Strech
        return SourcePoint(saturate(src));

    if (EdgeMode == 4) // Soft edge
        return SoftEdge(src);

    // Dithering and noise, the unprojected image
    return hudless;
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;

    if (pixelCoord.x >= ScreenWidth || pixelCoord.y >= ScreenHeight)
        return;

    // Screen UV calculation
    float2 uv = (float2(pixelCoord) + 0.5f) * float2(InvScreenWidth, InvScreenHeight);

    // UI Mask extraction
    float3 hudless = Hudless.Load(int3(pixelCoord, 0));
    float3 present = PresentCopy.Load(int3(pixelCoord, 0));
    float3 diff = abs(hudless - present);
    float delta = max(diff.x, max(diff.y, diff.z));
    float uiMask = smoothstep(UiDiffThreshold, UiDiffThreshold * 2.0f, delta);

    // Add depth cutout mask to the uiMask
    bool isCutout = IsDepthCutout(uv);
    uiMask = max(uiMask, isCutout ? 1.0f : 0.0f);

    const float3 pink = float3(1.0, 0.4, 0.6);
    const float3 green = float3(0.0f, 1.0f, 0.0f);
    const float3 blue = float3(0.0f, 0.3f, 1.0f);

    if (FakeFrame == 1)
        hudless = FakePresent.Load(int3(pixelCoord, 0));

    // src is the reprojected position of the pixel that we want
    float3 sourceRay = RayFromUV(uv);
    sourceRay = sourceRay.x * Latched[0].xyz + sourceRay.y * Latched[1].xyz + sourceRay.z * Latched[2].xyz;

    float2 src;
    bool valid = ProjectRay(sourceRay, src);

    // World direction of the pixel that we want, for the direction cache
    float3 worldDir = normalize(sourceRay.x * CameraRight.xyz + sourceRay.y * CameraUp.xyz + sourceRay.z * CameraForward.xyz);

    float3 cacheColor;
    float cacheWeight = SampleCache(worldDir, cacheColor);

    // Tint cache fills in debug mode
    if (EdgeMode == 0)
        cacheColor = lerp(cacheColor, blue, 0.3f);

    // Dithering and noise blend the reprojection into the unprojected image around the edges
    bool modeWithBackground = EdgeMode == 2 || EdgeMode == 3;

    float3 reprojectedGame = 0.0f; // Black

    if (!valid)
    {
        // Behind the rendered camera
        reprojectedGame = lerp(EdgeMode == 0 ? green : reprojectedGame, cacheColor, cacheWeight);
    }
    else if (all(src >= 0.0f) && all(src <= 1.0f))
    {
        if (!IsDepthCutout(src))
        {
            // the fun part
            reprojectedGame = SourcePoint(src);

            if (modeWithBackground)
            {
                // Only measure distance to reprojected edges that fall inside the screen bounds
                float distLeft = (src.x < uv.x) ? src.x * ScreenWidth : DitherWidthPx;
                float distRight = (src.x > uv.x) ? (1.0f - src.x) * ScreenWidth : DitherWidthPx;
                float distTop = (src.y < uv.y) ? src.y * ScreenHeight : DitherWidthPx;
                float distBottom = (src.y > uv.y) ? (1.0f - src.y) * ScreenHeight : DitherWidthPx;

                float edgeDistancePx = min(min(distLeft, distRight), min(distTop, distBottom));
                float projectedProbability = smoothstep(0.0f, DitherWidthPx, edgeDistancePx);

                // Where the cache continues the image past that edge there is no seam to hide
                // The cache at this pixel itself is no help, it always holds the same view as the reprojection
                if (edgeDistancePx < DitherWidthPx && CacheEnabled != 0)
                {
                    const float past = 0.125f * DitherWidthPx;

                    float2 beyond = src;
                    float2 along;
                    if (edgeDistancePx == distLeft)
                    {
                        beyond.x = -past * InvScreenWidth;
                        along = float2(0.0f, InvScreenHeight);
                    }
                    else if (edgeDistancePx == distRight)
                    {
                        beyond.x = 1.0f + past * InvScreenWidth;
                        along = float2(0.0f, InvScreenHeight);
                    }
                    else if (edgeDistancePx == distTop)
                    {
                        beyond.y = -past * InvScreenHeight;
                        along = float2(InvScreenWidth, 0.0f);
                    }
                    else
                    {
                        beyond.y = 1.0f + past * InvScreenHeight;
                        along = float2(InvScreenWidth, 0.0f);
                    }

                    projectedProbability = lerp(projectedProbability, 1.0f, CacheCoverage(beyond, along));
                }

                float pattern = EdgeMode == 2 ? Bayer4x4(pixelCoord) : HashNoise(pixelCoord);
                reprojectedGame = pattern < projectedProbability ? reprojectedGame : hudless;
            }
        }
        else
        {
            // Landed on the hands, what's behind them was never rendered
            float3 fill = green;
            if (EdgeMode != 0 && (modeWithBackground || !StepPastCutout(src, uv, fill)))
                fill = hudless;

            reprojectedGame = lerp(fill, cacheColor, cacheWeight);
        }
    }
    else
    {
        // Outside the rendered frame
        reprojectedGame = lerp(EdgeFill(src, hudless), cacheColor, cacheWeight);
    }

    // Final UI Blend
    float3 composedImage = lerp(reprojectedGame, present, uiMask);

    // Pink line on fake frames
    if (FakeFrame == 1 && EdgeMode == 0)
        composedImage = lerp(composedImage, pink, uv.x < 0.02);

    Present[pixelCoord] = lerp(composedImage, pink, uiMask * 0.6f * ShowStaticElements);
}
