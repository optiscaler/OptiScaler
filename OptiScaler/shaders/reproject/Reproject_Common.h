#pragma once

#include "pch.h"

static std::string shaderCode = R"(
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
)";

static std::string maskHShaderCode = R"(
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
    
    float4 ReprojectionMatrixRow0;
    float4 ReprojectionMatrixRow1;
    float4 ReprojectionMatrixRow2;
};

Texture2D<float> Depth : register(t2);

#ifdef VK_MODE
[[vk::image_format("r8")]]
#endif
RWTexture2D<float> DepthMaskTempOut : register(u1);
#ifdef VK_MODE
[[vk::image_format("r8")]]
#endif
RWTexture2D<float> DepthMaskOut : register(u2);

// Depth mask prepass, horizontal part
// Thresholds depth and expands the cutout by CutoffExpandPx along X
// With CutoffExpandPx == 0 the vertical pass is skipped and the result goes straight to the final mask
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixelCoord = int2(dispatchThreadID.xy);

    if (pixelCoord.x >= int(DepthWidth) || pixelCoord.y >= int(DepthHeight))
        return;

    int radius = int(CutoffExpandPx);
    int maxX = int(DepthWidth) - 1;

    // Cutout if any depth in the window crosses the cutoff, so only min/max is needed
    float minDepth = 1.0f;
    float maxDepth = 0.0f;

    for (int x = -radius; x <= radius; ++x)
    {
        int sampleX = clamp(pixelCoord.x + x, 0, maxX);
        float d = Depth.Load(int3(sampleX, pixelCoord.y, 0));
        minDepth = min(minDepth, d);
        maxDepth = max(maxDepth, d);
    }

    bool isCutout = InvertedDepth ? (maxDepth > DepthCutoff) : (minDepth < DepthCutoff);
    float mask = isCutout ? 1.0f : 0.0f;

    if (radius == 0)
        DepthMaskOut[pixelCoord] = mask;
    else
        DepthMaskTempOut[pixelCoord] = mask;
}
)";

static std::string maskVShaderCode = R"(
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
    
    float4 ReprojectionMatrixRow0;
    float4 ReprojectionMatrixRow1;
    float4 ReprojectionMatrixRow2;
};

Texture2D<float> DepthMaskTemp : register(t5);

#ifdef VK_MODE
[[vk::image_format("r8")]]
#endif
RWTexture2D<float> DepthMaskOut : register(u2);

// Depth mask prepass, vertical part
// Expands the horizontally expanded mask by CutoffExpandPx along Y
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixelCoord = int2(dispatchThreadID.xy);

    if (pixelCoord.x >= int(DepthWidth) || pixelCoord.y >= int(DepthHeight))
        return;

    int radius = int(CutoffExpandPx);
    int maxY = int(DepthHeight) - 1;

    float mask = 0.0f;

    for (int y = -radius; y <= radius; ++y)
    {
        int sampleY = clamp(pixelCoord.y + y, 0, maxY);
        mask = max(mask, DepthMaskTemp.Load(int3(pixelCoord.x, sampleY, 0)));
    }

    DepthMaskOut[pixelCoord] = mask;
}
)";

static std::string latchShaderCode = R"(
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
};

// Running mouse total, written by the CPU on every mouse event
cbuffer LiveInput : register(b1)
{
    int2 LiveMouse;
    int2 _LivePad;
};

RWStructuredBuffer<float4> Latched : register(u3);

// Same as XMMatrixRotationNormal, row vector convention
float3x3 RotationNormal(float3 n, float angle)
{
    float s, c;
    sincos(angle, s, c);
    float c2 = 1.0f - c;

    return float3x3(
        n.x * n.x * c2 + c,       n.x * n.y * c2 + n.z * s, n.x * n.z * c2 - n.y * s,
        n.x * n.y * c2 - n.z * s, n.y * n.y * c2 + c,       n.y * n.z * c2 + n.x * s,
        n.x * n.z * c2 + n.y * s, n.y * n.z * c2 - n.x * s, n.z * n.z * c2 + c
    );
}

// Late latch pass, runs right before the reprojection so that it can use the newest mouse data
// that has reached the GPU, and builds the rotation once so the whole image uses the same value
[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    // int subtraction wraps around just like the running total on the CPU
    int2 delta = LateLatch != 0 ? LiveMouse - SimStartMouse : MouseDelta;

    float2 mouse = float2(delta) * PixelAngle;
    float yaw = mouse.x * Calibration.x + mouse.y * Calibration.y;
    float pitch = mouse.x * Calibration.z + mouse.y * Calibration.w;

    float3x3 viewToWorld = float3x3(CameraRight.xyz, CameraUp.xyz, CameraForward.xyz);
    float3x3 rotPitch = RotationNormal(CameraRight.xyz, pitch * PitchSign);
    float3x3 rotYaw = RotationNormal(float3(0.0f, 0.0f, 1.0f), yaw * YawSign);

    float3x3 rotation = mul(mul(mul(viewToWorld, rotPitch), rotYaw), transpose(viewToWorld));

    Latched[0] = float4(rotation[0], 0.0f);
    Latched[1] = float4(rotation[1], 0.0f);
    Latched[2] = float4(rotation[2], 0.0f);
    Latched[3] = float4(float2(delta), 0.0f, 0.0f); // for debugging
}
)";

static std::string cacheShaderCode = R"(
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
Texture2D<float> Depth : register(t2);
Texture2D<float> DepthMask : register(t4);
TextureCube<float3> PrevCacheColor : register(t6);
TextureCube<float2> PrevCacheGeo : register(t7);

// Direction cache, a world space cubemap centered on the camera of the newest rendered frame
// Color and distance are premultiplied by the confidence stored in Geo.y,
// so bilinear filtering ignores texels that were never written
#ifdef VK_MODE
[[vk::image_format("rgba16f")]]
#endif
RWTexture2DArray<float3> CacheColorOut : register(u4);
#ifdef VK_MODE
[[vk::image_format("rg16f")]]
#endif
RWTexture2DArray<float2> CacheGeoOut : register(u5);

SamplerState BilinearClampSampler : register(s1);

// Same as reproject.hlsl
bool IsDepthCutout(float2 uv)
{
    int2 maskPixel = int2(uv * float2(DepthWidth - 1, DepthHeight - 1));
    return DepthMask.Load(int3(maskPixel, 0)) > 0.5f;
}

float LinearViewZ(float2 uv)
{
    int2 depthPixel = min(int2(uv * float2(DepthWidth, DepthHeight)), int2(DepthWidth - 1, DepthHeight - 1));
    float d = Depth.Load(int3(depthPixel, 0));

    if (InvertedDepth)
    {
        if (CameraFar <= 0.0f)
            return CameraNear / d;

        return CameraNear * CameraFar / (CameraNear + d * (CameraFar - CameraNear));
    }

    if (CameraFar <= 0.0f)
        return CameraNear / (1.0f - d);

    return CameraNear * CameraFar / (CameraFar - d * (CameraFar - CameraNear));
}

// D3D cube face layout
float3 CubeTexelDir(uint3 id)
{
    float2 st = (float2(id.xy) + 0.5f) * InvCacheSize * 2.0f - 1.0f;
    float u = st.x;
    float v = st.y;

    float3 dir;
    if (id.z == 0)
        dir = float3(1.0f, -v, -u);
    else if (id.z == 1)
        dir = float3(-1.0f, -v, u);
    else if (id.z == 2)
        dir = float3(u, 1.0f, v);
    else if (id.z == 3)
        dir = float3(u, -1.0f, -v);
    else if (id.z == 4)
        dir = float3(u, -v, 1.0f);
    else
        dir = float3(-u, -v, -1.0f);

    return normalize(dir);
}

bool SamplePrev(float3 dir, out float3 color, out float dist, out float weight)
{
    float2 geo = PrevCacheGeo.SampleLevel(BilinearClampSampler, dir, 0.0f);
    weight = geo.y;

    if (weight < 0.01f)
    {
        color = 0.0f;
        dist = 0.0f;
        return false;
    }

    color = PrevCacheColor.SampleLevel(BilinearClampSampler, dir, 0.0f) / weight;
    dist = geo.x / weight;
    return true;
}

void WriteCache(uint3 id, float3 color, float dist, float weight)
{
    // Also catches NaN and inf from the depth linearization
    dist = (dist >= 0.0f && dist < CacheMaxDistance) ? dist : CacheMaxDistance;

    CacheColorOut[id] = color * weight;
    CacheGeoOut[id] = float2(dist * weight, weight);
}

// Runs once per rendered frame, before the reprojection
// Texels visible in the new frame get its color, the rest is carried over from the previous cache
// and moved to the new camera position using the stored distance
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= CacheSize || id.y >= CacheSize)
        return;

    float3 dir = CubeTexelDir(id);

    // Camera basis is orthonormal so this is the inverse of the view to world used by the reprojection
    float3 view = float3(dot(dir, CameraRight.xyz), dot(dir, CameraUp.xyz), dot(dir, CameraForward.xyz));

    if (view.z > 0.0001f)
    {
        float2 ndc = (view.xy / view.z) * float2(InvTanHalfFovX, InvTanHalfFovY);
        float2 uv = ndc * float2(0.5f, -0.5f) + 0.5f;

        // Hands and guns stay out of the cache, the area under them is what we need it for
        if (all(uv > 0.0f) && all(uv < 1.0f) && !IsDepthCutout(uv))
        {
            float3 color = Hudless.SampleLevel(BilinearClampSampler, uv, 0.0f);
            WriteCache(id, color, LinearViewZ(uv) / view.z, 1.0f);
            return;
        }
    }

    if (CacheReset)
    {
        WriteCache(id, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 color;
    float dist;
    float weight;

    if (!SamplePrev(dir, color, dist, weight))
    {
        WriteCache(id, 0.0f, 0.0f, 0.0f);
        return;
    }

    // Find the surface along dir from the new camera position
    // Start with the distance stored in the same direction, then refine it against the old cache
    float t = dist;
    float3 prevDir = dir;

    [unroll]
    for (int i = 0; i < 3; ++i)
    {
        prevDir = normalize(dir * t + CameraDelta.xyz);

        if (!SamplePrev(prevDir, color, dist, weight))
        {
            WriteCache(id, 0.0f, 0.0f, 0.0f);
            return;
        }

        float3 surface = prevDir * dist - CameraDelta.xyz;
        t = max(dot(surface, dir), CameraNear);
    }

    // Real parallax is tiny between two frames, a big jump means the distance doesn't match the camera units
    // Dropping the texel is better than smearing it somewhere it doesn't belong
    if (dot(prevDir, dir) < 0.996f) // ~5 degrees
    {
        WriteCache(id, 0.0f, 0.0f, 0.0f);
        return;
    }

    WriteCache(id, color, t, saturate(weight - CacheDecay));
}
)";

static std::string statsShaderCode = R"(
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

Texture2D<float> Depth : register(t2);
Texture2D<float> DepthMask : register(t4);
Texture2D<float2> Velocity : register(t10);

// STATS_BINS histogram of log10(parallax scale) followed by STATS_COUNTERS counters, read back by the CPU
RWStructuredBuffer<uint> StatsOut : register(u6);

#define STATS_BINS 128
#define STATS_COUNTERS 12
#define LOG_SCALE_MIN -3.0f
#define LOG_SCALE_MAX 6.0f

#define GRID_X 128
#define GRID_Y 72
#define THREADS 256

// The camera has to move the scene by at least this much for a sample to say anything about 3rd person
#define MIN_EXPECTED_PX 8.0f

// Counters
#define CENTER_SAMPLES 0 // middle of the screen, without hands and sky
#define CENTER_VALID 1   // of those, the ones the camera moves by at least MIN_EXPECTED_PX
#define CENTER_LOCKED 2  // of those, the ones that barely move, following the camera
#define OUTER_VALID 3    // outside the middle, the ones the camera moves by at least MIN_EXPECTED_PX
#define OUTER_MATCH 4    // of those, the ones whose motion vectors roughly match the camera motion
#define CENTER_AT_PIVOT 5 // middle of the screen, around the distance of the pivot of an orbiting camera
#define OUTER_X_VALID 6  // outside the middle, the ones the camera moves sideways by at least MIN_EXPECTED_PX
#define OUTER_X_OPPOSITE 7 // of those, the ones moving sideways the other way, the camera's right points left
#define OUTER_Y_VALID 8  // same vertically
#define OUTER_Y_OPPOSITE 9 // the camera's up points down
#define MOVE_ALONG 10    // samples whose parallax follows the camera movement
#define MOVE_AGAINST 11  // samples whose parallax goes the other way, the camera position is turned around

groupshared uint Histogram[STATS_BINS];
groupshared uint Counters[STATS_COUNTERS];

// Same as reproject.hlsl
bool IsDepthCutout(float2 uv)
{
    int2 maskPixel = int2(uv * float2(DepthWidth - 1, DepthHeight - 1));
    return DepthMask.Load(int3(maskPixel, 0)) > 0.5f;
}

float3 RayFromUV(float2 uv)
{
    float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    return float3(ndc * float2(TanHalfFovX, TanHalfFovY), 1.0f);
}

bool ProjectRay(float3 ray, out float2 uv)
{
    uv = 0.0f;

    if (ray.z <= 0.00001f)
        return false;

    float2 ndc = (ray.xy / ray.z) * float2(InvTanHalfFovX, InvTanHalfFovY);
    uv = ndc * float2(0.5f, -0.5f) + 0.5f;
    return true;
}

// Loose, the motion vectors roughly go where expected
bool Matches(float2 measuredPx, float2 expectedPx)
{
    float measuredLen = length(measuredPx);
    float expectedLen = length(expectedPx);

    return dot(measuredPx, expectedPx) > 0.7f * measuredLen * expectedLen && measuredLen > 0.5f * expectedLen &&
           measuredLen < 2.0f * expectedLen;
}

// Same as reproject_cache.hlsl, in units of the near plane distance
float LinearViewZ(float d)
{
    if (InvertedDepth)
    {
        if (CameraFar <= 0.0f)
            return CameraNear / d;

        return CameraNear * CameraFar / (CameraNear + d * (CameraFar - CameraNear));
    }

    if (CameraFar <= 0.0f)
        return CameraNear / (1.0f - d);

    return CameraNear * CameraFar / (CameraFar - d * (CameraFar - CameraNear));
}

// Runs once per rendered frame on a sparse grid, compares the game's motion vectors with the camera motion
//
// 3rd person: a player character in the middle of the screen keeps its place on screen while the camera moves
// the world past it. The expected motion includes the camera movement, so something that only stays in place because
// the camera circles around it, like an object the player keeps aiming at while strafing, isn't counted.
// When the camera orbits around a pivot instead, the character doesn't move against the scene around it, so the CPU
// finds the orbit from the camera motion alone and this only checks there is something at the pivot.
// The outer part of the screen tells whether the motion vectors match the camera at all. That can't depend on the
// parallax scale, it's estimated from these same motion vectors only while they match.
//
// Parallax scale: depth is linearized in units of the near plane which don't always match the units of the camera
// position. The part of the motion that the rotation doesn't explain comes from the camera movement, and its size
// against the depth gives the ratio between the two for every sample. The CPU picks the most common one.
[numthreads(16, 16, 1)]
void CSMain(uint groupIndex : SV_GroupIndex)
{
    for (uint i = groupIndex; i < STATS_BINS; i += THREADS)
        Histogram[i] = 0;

    if (groupIndex < STATS_COUNTERS)
        Counters[groupIndex] = 0;

    GroupMemoryBarrierWithGroupSync();

    const float2 screenPx = float2(ScreenWidth, ScreenHeight);
    const float2 invTan = float2(InvTanHalfFovX, InvTanHalfFovY);
    const bool parallax = CameraNear > 0.0f && dot(PrevViewMove.xyz, PrevViewMove.xyz) > 0.0f;
    const bool knownScale = CameraNear > 0.0f && PrevViewMove.w > 0.0f;
    const float binsPerDecade = STATS_BINS / (LOG_SCALE_MAX - LOG_SCALE_MIN);

    [loop]
    for (uint s = groupIndex; s < GRID_X * GRID_Y; s += THREADS)
    {
        float2 uv = (float2(s % GRID_X, s / GRID_X) + 0.5f) / float2(GRID_X, GRID_Y);

        if (IsDepthCutout(uv))
            continue;

        int2 depthPixel = min(int2(uv * float2(DepthWidth, DepthHeight)), int2(DepthWidth - 1, DepthHeight - 1));
        float d = Depth.Load(int3(depthPixel, 0));

        // Sky often has no motion vectors
        if (InvertedDepth ? d <= 0.0f : d >= 1.0f)
            continue;

        int2 mvPixel = min(int2(uv * float2(MvWidth, MvHeight)), int2(MvWidth - 1, MvHeight - 1));
        float2 measured = uv + Velocity.Load(int3(mvPixel, 0)) * MvToUv;

        // Where the sample was in the previous frame if only the camera rotated
        float3 ray = RayFromUV(uv);
        float3 rotated = float3(dot(ray, PrevViewX.xyz), dot(ray, PrevViewY.xyz), dot(ray, PrevViewZ.xyz));

        float2 rotatedUV;
        if (!ProjectRay(rotated, rotatedUV))
            continue;

        float viewZ = LinearViewZ(d);

        // And with the camera movement too, as far as the parallax scale is known
        float2 expectedUV = rotatedUV;
        if (knownScale && !ProjectRay(rotated * viewZ + PrevViewMove.xyz * PrevViewMove.w, expectedUV))
            continue;

        float2 expectedPx = (expectedUV - uv) * screenPx;
        float2 rotationPx = (rotatedUV - uv) * screenPx;
        float2 measuredPx = (measured - uv) * screenPx;
        float expectedLen = length(expectedPx);
        float measuredLen = length(measuredPx);

        bool center = all(uv > float2(0.3f, 0.3f)) && all(uv < float2(0.7f, 0.9f));

        if (center)
        {
            // Without the camera movement a circling camera can't be told apart from a 3rd person character
            if (knownScale)
            {
                InterlockedAdd(Counters[CENTER_SAMPLES], 1);

                if (viewZ > 0.5f * OrbitPivotDepth && viewZ < 1.3f * OrbitPivotDepth)
                    InterlockedAdd(Counters[CENTER_AT_PIVOT], 1);

                if (expectedLen >= MIN_EXPECTED_PX)
                {
                    InterlockedAdd(Counters[CENTER_VALID], 1);

                    if (measuredLen < 0.25f * expectedLen)
                        InterlockedAdd(Counters[CENTER_LOCKED], 1);
                }
            }
        }
        else if (max(expectedLen, length(rotationPx)) >= MIN_EXPECTED_PX)
        {
            InterlockedAdd(Counters[OUTER_VALID], 1);

            // The rotation alone explains the far away samples and those of a camera that only turns, with a wrong
            // parallax scale the movement would explain neither
            if (Matches(measuredPx, expectedPx) || Matches(measuredPx, rotationPx))
                InterlockedAdd(Counters[OUTER_MATCH], 1);

            // Right and up only affect the rotation, each axis on its own and only by direction,
            // the motion vectors' scale can differ between them
            // The camera movement adds parallax that can go either way, walking down stairs moves the scene up while
            // the view barely pitches. Once its scale is known it's taken out, and samples where it would still
            // decide the direction are left out, a slightly wrong scale shouldn't turn an axis around
            float2 movePx = knownScale ? expectedPx - rotationPx : 0.0f;
            float2 rotationOnlyPx = measuredPx - movePx;

            [unroll]
            for (int a = 0; a < 2; ++a)
            {
                if (abs(rotationPx[a]) >= MIN_EXPECTED_PX && abs(movePx[a]) < 0.5f * abs(rotationPx[a]) &&
                    abs(rotationOnlyPx[a]) >= 1.0f)
                {
                    InterlockedAdd(Counters[OUTER_X_VALID + 2 * a], 1);

                    if (rotationOnlyPx[a] * rotationPx[a] < 0.0f)
                        InterlockedAdd(Counters[OUTER_X_OPPOSITE + 2 * a], 1);
                }
            }
        }

        // Parallax scale estimate, always against the rotation only
        if (!parallax || length(measuredPx - rotationPx) < 1.5f)
            continue;

        // Previous view position of the sample in depth units is a + b * k, k being world units -> depth units
        // Solving the projection of it for the measured position gives k * c = e
        float3 a = rotated * viewZ;
        float3 b = PrevViewMove.xyz;
        float2 ndc = measured * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

        float2 c = invTan * b.xy - ndc * b.z;
        float2 e = ndc * a.z - invTan * a.xy;

        float cc = dot(c, c);
        if (!(cc > 1e-20f))
            continue;

        float k = dot(c, e) / cc;
        if (!(abs(k) > 0.0f))
            continue;

        // Moving on its own instead of with the camera movement
        if (length(e - k * c) > 0.35f * length(e))
            continue;

        if (k < 0.0f)
        {
            InterlockedAdd(Counters[MOVE_AGAINST], 1);
            continue;
        }

        InterlockedAdd(Counters[MOVE_ALONG], 1);

        int bin = int(floor((-log10(k) - LOG_SCALE_MIN) * binsPerDecade));
        if (bin >= 0 && bin < STATS_BINS)
            InterlockedAdd(Histogram[bin], 1);
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint j = groupIndex; j < STATS_BINS; j += THREADS)
        StatsOut[j] = Histogram[j];

    if (groupIndex < STATS_COUNTERS)
        StatsOut[STATS_BINS + groupIndex] = Counters[groupIndex];
}
)";
