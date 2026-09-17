#pragma once

#include "pch.h"

static std::string shaderCode = R"(
cbuffer Params : register(b0)
{
    float UiDiffThreshold;
    uint ScreenWidth;
    uint ScreenHeight;
    uint EdgeMode;

    float TanHalfFovX;
    float TanHalfFovY;
    float InvTanHalfFovX;
    float InvTanHalfFovY;

    row_major float3x3 ReprojectionMatrix;
};

Texture2D<float3> Hudless : register(t0);
Texture2D<float3> PresentCopy : register(t1);
RWTexture2D<float3> Present : register(u0);
SamplerState LinearClampSampler : register(s0);

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;

    if (pixelCoord.x >= ScreenWidth || pixelCoord.y >= ScreenHeight)
        return;

    // Screen UV calculation
    float2 invScreenSize = 1.0f / float2(ScreenWidth, ScreenHeight);
    float2 uv = (float2(pixelCoord) + 0.5f) * invScreenSize;

    // UI Mask extraction
    float3 hudless = Hudless.Load(int3(pixelCoord, 0));
    float3 present = PresentCopy.Load(int3(pixelCoord, 0));
    float3 diff = abs(hudless - present);
    float delta = max(diff.x, max(diff.y, diff.z));
    float uiMask = smoothstep(UiDiffThreshold, UiDiffThreshold * 2.0f, delta);

    // Vectorized Camera Ray (un-normalized)
    float2 ndc = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    float3 ray = float3(ndc * float2(TanHalfFovX, TanHalfFovY), 1.0f);

    // Reproject Ray using matrix multiplication
    float3 sourceRay = mul(ReprojectionMatrix, ray);

    // Perspective Divide & Source UV Calculation
    float2 sourceUV = 0.0f;
    bool isValidDepth = sourceRay.z > 0.00001f;

    if (isValidDepth)
    {
        float2 sourceNDC = (sourceRay.xy / sourceRay.z) * float2(InvTanHalfFovX, InvTanHalfFovY);
        sourceUV = sourceNDC * float2(0.5f, -0.5f) + 0.5f;
    }

    // Bounds & Reprojection
    bool inside = isValidDepth && all(sourceUV >= 0.0f) && all(sourceUV <= 1.0f);

    float3 reprojectedGame = 0.0f;
    if (inside)
    {
        reprojectedGame = Hudless.SampleLevel(LinearClampSampler, sourceUV, 0.0f);
    }
    else if (EdgeMode == 1 && isValidDepth)
    {
        reprojectedGame = Hudless.SampleLevel(LinearClampSampler, saturate(sourceUV), 0.0f);
    }

    // Final UI Blend
    Present[pixelCoord] = lerp(reprojectedGame, present, uiMask);
}

)";
