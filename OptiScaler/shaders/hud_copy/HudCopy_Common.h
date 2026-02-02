#pragma once

#include "pch.h"
#include <d3dcompiler.h>

struct CompareParams
{
    float DiffThreshold = 0.02f;
    float PinkAmount = 0.6f;
    float InvOutputSize[2] = { 0, 0 };
};

static std::string shaderCode = R"(
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
)";

static ID3DBlob* HudCopy_CompileShader(const char* shaderCode, const char* entryPoint, const char* target)
{
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, entryPoint, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shaderBlob, &errorBlob);

    if (FAILED(hr))
    {
        LOG_ERROR("error while compiling shader");

        if (errorBlob)
        {
            LOG_ERROR("error while compiling shader : {0}", (char*) errorBlob->GetBufferPointer());
            errorBlob->Release();
        }

        if (shaderBlob)
            shaderBlob->Release();

        return nullptr;
    }

    if (errorBlob)
        errorBlob->Release();

    return shaderBlob;
}
