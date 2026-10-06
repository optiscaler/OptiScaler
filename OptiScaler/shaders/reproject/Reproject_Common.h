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
};

Texture2D<float3> Hudless : register(t0);
Texture2D<float3> PresentCopy : register(t1);
Texture2D<float> Depth : register(t2);
Texture2D<float3> FakePresent : register(t3);
Texture2D<float> DepthMask : register(t4);

RWTexture2D<float3> Present : register(u0);

// Rows of the reprojection rotation, written once per dispatch by the latch pass
RWStructuredBuffer<float4> Latched : register(u3);
SamplerState LinearClampSampler : register(s0);

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
       
    float3 reprojectedGame = 0.0f; // Black
    
    // Vectorized Camera Ray (un-normalized)
    float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    float3 ray = float3(ndc * float2(TanHalfFovX, TanHalfFovY), 1.0f);

    // sourceUV is the reprojected position of the pixel that we want
    float3 sourceRay = ray.x * Latched[0].xyz + ray.y * Latched[1].xyz + ray.z * Latched[2].xyz;

    // Perspective Divide & Source UV Calculation
    float2 sourceUV = 0.0f;
    if (sourceRay.z > 0.00001f)
    {
        float2 sourceNDC = (sourceRay.xy / sourceRay.z) * float2(InvTanHalfFovX, InvTanHalfFovY);
        sourceUV = sourceNDC * float2(0.5f, -0.5f) + 0.5f;
    }
    else
    {
        Present[pixelCoord] = lerp(reprojectedGame, present, uiMask);
        return;
    }

    bool inside = all(sourceUV >= 0.0f) && all(sourceUV <= 1.0f);
    bool modeWithBackground = EdgeMode == 2 || EdgeMode == 3;
    
    const float3 pink = float3(1.0, 0.4, 0.6);
    const float3 green = float3(0.0f, 1.0f, 0.0f);
    
    if (FakeFrame == 1)
        hudless = FakePresent.Load(int3(pixelCoord, 0));
    
    if (inside)
    {
        // Depth cutoff
        bool isCutoutReprojected = IsDepthCutout(sourceUV);
                
        if (isCutoutReprojected)
        {
            reprojectedGame = lerp(hudless, green, EdgeMode == 0); // try to fill gap with unprojected hudless, or green for debug
        }
        else
        {
            // the fun part
            if (FakeFrame == 1)
                reprojectedGame = FakePresent.SampleLevel(LinearClampSampler, sourceUV, 0.0f);
            else
                reprojectedGame = Hudless.SampleLevel(LinearClampSampler, sourceUV, 0.0f);
        }
                
        if (modeWithBackground)
        {
            // Only measure distance to reprojected edges that fall inside the screen bounds
            float distLeft = (sourceUV.x < uv.x) ? sourceUV.x * ScreenWidth : DitherWidthPx;
            float distRight = (sourceUV.x > uv.x) ? (1.0f - sourceUV.x) * ScreenWidth : DitherWidthPx;
            float distTop = (sourceUV.y < uv.y) ? sourceUV.y * ScreenHeight : DitherWidthPx;
            float distBottom = (sourceUV.y > uv.y) ? (1.0f - sourceUV.y) * ScreenHeight : DitherWidthPx;

            float edgeDistancePx = min(min(distLeft, distRight), min(distTop, distBottom));
            float projectedProbability = smoothstep(0.0f, DitherWidthPx, edgeDistancePx);
        
            float pattern = 0.0f;
            if (EdgeMode == 2) // Dither
            {
                pattern = Bayer4x4(pixelCoord);
            }
            else if (EdgeMode == 3) // Noise
            {
                pattern = HashNoise(pixelCoord);
            }
            
            reprojectedGame = pattern < projectedProbability ? reprojectedGame : hudless;
        }
    }
    else
    {
        // Outside the reprojection
        if (EdgeMode == 0) // Debug
        {
            reprojectedGame = green;
        }
        else if (EdgeMode == 1) // Strech
        {
            if (FakeFrame == 1)
                reprojectedGame = FakePresent.SampleLevel(LinearClampSampler, saturate(sourceUV), 0.0f);
            else
                reprojectedGame = Hudless.SampleLevel(LinearClampSampler, saturate(sourceUV), 0.0f);
        }
        else if (modeWithBackground)
        {
            reprojectedGame = hudless;
        }
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

RWTexture2D<float> DepthMaskTempOut : register(u1);
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
