#pragma once

#include "SysUtils.h"

struct alignas(256) RFConstants
{
    UINT width;
    UINT height;
    UINT offset;
    UINT velocity;
};

inline static std::string rfCode = R"(
cbuffer Params : register(b0)
{
    uint width;
    uint height;
    uint offset;
    uint velocity;
};

// Input texture
Texture2D<float4> SourceTexture : register(t0);

// Output texture
RWTexture2D<float4> DestinationTexture : register(u0);

float3 LinearToSrgb(float3 color)
{
    float3 low = color * 12.92;
    float3 high = 1.055 * pow(color, 1.0 / 2.4) - 0.055;
    return lerp(high, low, step(color, 0.0031308));
}

// velocity: 0 copy, 1 motion vectors, 2 color, 3 color read from sRGB into a UNORM output
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.y < offset || dispatchThreadID.y > (height + offset))
        return;

    uint2 pixelCoord = uint2(dispatchThreadID.x, height - (dispatchThreadID.y - offset));
    float4 src = SourceTexture.Load(int3(dispatchThreadID.xy, 0));

    if (velocity == 1)
    {
        DestinationTexture[pixelCoord] = float4(src.r, -src.g, 0, 0);
        return;
    }

    if (velocity == 3)
        src.rgb = LinearToSrgb(saturate(src.rgb));

    DestinationTexture[pixelCoord] = src;
}
)";
