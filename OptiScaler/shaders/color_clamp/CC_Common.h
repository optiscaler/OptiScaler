#pragma once

#include "SysUtils.h"

// Source of precompile/color_clamp.hlsl, compiled at runtime when the precompiled shader fails
static std::string colorClampShader = R"(
Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Dest : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint width, height;
    Dest.GetDimensions(width, height);

    if (DTid.x >= width || DTid.y >= height)
        return;

    float4 color = Source.Load(int3(DTid.xy, 0));

    [unroll]
    for (uint i = 0; i < 4; i++)
        color[i] = isnan(color[i]) ? 0.0f : clamp(color[i], -65504.0f, 65504.0f);

    Dest[DTid.xy] = color;
}
)";
