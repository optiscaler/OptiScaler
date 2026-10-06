#pragma once

#include "pch.h"

class MenuBlur_Common
{
  protected:
    enum class BlurMode : uint32_t
    {
        Downsample = 0,
        Horizontal = 1,
        Vertical = 2,
    };

    struct InternalMenuBlurParams
    {
        uint32_t Mode;
        float Spacing;
        float Padding[2];
    };

    // Downsample factor of the blurred texture, matches the shader
    static constexpr uint32_t DownsampleFactor = 4;
    // Number of horizontal + vertical blur pass pairs
    static constexpr uint32_t BlurIterations = 2;

    const std::string shaderCode = R"(
        #ifdef VK_MODE
        struct BlurParams
        {
            uint Mode;
            float Spacing;
            float2 Padding;
        };

        [[vk::push_constant]] BlurParams Params;
        #define PARAM(x) Params.x
        #else
        cbuffer Params : register(b0)
        {
            uint Mode; // 0: 4x downsample, 1: horizontal blur, 2: vertical blur
            float Spacing; // Tap spacing multiplier of the blur passes
            float2 Padding;
        };
        #define PARAM(x) x
        #endif

        #ifdef VK_MODE
        [[vk::combinedImageSampler]] [[vk::binding(0, 0)]]
        #endif
        Texture2D<float4> InTexture : register(t0);

        #ifdef VK_MODE
        [[vk::combinedImageSampler]] [[vk::binding(0, 0)]]
        #endif
        SamplerState LinearClamp : register(s0);

        #ifdef VK_MODE
        [[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]]
        #endif
        RWTexture2D<float4> OutTexture : register(u0);

        [numthreads(8, 8, 1)]
        void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
        {
            uint outWidth, outHeight;
            OutTexture.GetDimensions(outWidth, outHeight);

            if (dispatchThreadId.x >= outWidth || dispatchThreadId.y >= outHeight)
                return;

            uint inWidth, inHeight;
            InTexture.GetDimensions(inWidth, inHeight);
            float2 invInSize = 1.0f / float2(inWidth, inHeight);

            float3 color;

            if (PARAM(Mode) == 0)
            {
                // Each output pixel covers a 4x4 block, every bilinear tap averages a 2x2 quad of it
                float2 base = float2(dispatchThreadId.xy) * 4.0f + 1.0f;

                color = InTexture.SampleLevel(LinearClamp, base * invInSize, 0).rgb;
                color += InTexture.SampleLevel(LinearClamp, (base + float2(2.0f, 0.0f)) * invInSize, 0).rgb;
                color += InTexture.SampleLevel(LinearClamp, (base + float2(0.0f, 2.0f)) * invInSize, 0).rgb;
                color += InTexture.SampleLevel(LinearClamp, (base + float2(2.0f, 2.0f)) * invInSize, 0).rgb;
                color *= 0.25f;
            }
            else
            {
                // 9 tap gaussian done with 5 bilinear taps
                const float offsets[2] = { 1.3846153846f, 3.2307692308f };
                const float weights[3] = { 0.2270270270f, 0.3162162162f, 0.0702702703f };

                float2 dir = (PARAM(Mode) == 1 ? float2(1.0f, 0.0f) : float2(0.0f, 1.0f)) * PARAM(Spacing) * invInSize;
                float2 uv = (float2(dispatchThreadId.xy) + 0.5f) * invInSize;

                color = InTexture.SampleLevel(LinearClamp, uv, 0).rgb * weights[0];

                [unroll]
                for (int i = 0; i < 2; i++)
                {
                    color += InTexture.SampleLevel(LinearClamp, uv + dir * offsets[i], 0).rgb * weights[i + 1];
                    color += InTexture.SampleLevel(LinearClamp, uv - dir * offsets[i], 0).rgb * weights[i + 1];
                }
            }

            OutTexture[dispatchThreadId.xy] = float4(color, 1.0f);
        }
    )";
};
