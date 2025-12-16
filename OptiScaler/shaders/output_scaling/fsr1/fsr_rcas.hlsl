#define A_GPU 1
#define A_HLSL 1
//#define A_HALF
#define FSR_RCAS_F

#include "ffx_a.h"

cbuffer cb : register(b0)
{
    uint4 Const0;
    uint2 ProjCentre;
    uint SquaredRadius;
    uint DebugMode;
};

SamplerState samLinearClamp : register(s0);
Texture2D<AF4> InputTexture : register(t0);
Texture2D<AF4> MotionTexture : register(t1);
RWTexture2D<AF4> OutputTexture : register(u0);

AF4 FsrRcasLoadF(ASU2 p)
{
    return InputTexture.Load(int3(ASU2(p), 0));
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b)
{
}

#include "ffx_fsr1.h"

void Sharpen(int2 pos)
{
	AF3 c;
    FsrRcasF(c.r, c.g, c.b, pos, Const0);
    OutputTexture[pos] = AF4(c, 1);
}

[numthreads(64, 1, 1)]
void main(uint3 LocalThreadId : SV_GroupThreadID, uint3 WorkGroupId : SV_GroupID, uint3 Dtid : SV_DispatchThreadID)
{
	// Do remapping of local xy in workgroup for a more PS-like swizzle pattern.
	AU2 gxy = ARmp8x8(LocalThreadId.x) + AU2(WorkGroupId.x << 4u, WorkGroupId.y << 4u);
	AU2 groupCentre = AU2((WorkGroupId.x << 4u) + 8u, (WorkGroupId.y << 4u) + 8u);
	AU2 dc = ProjCentre - groupCentre;
	AU2 pos = gxy + Const0.zw;
    
    if (dot(dc, dc) <= SquaredRadius)
    {
		// only do RCAS for workgroups inside the given radius
        Sharpen(pos);
        pos.x += 8u;
        Sharpen(pos);
        pos.y += 8u;
        Sharpen(pos);
        pos.x -= 8u;
        Sharpen(pos);
    }
    else
    {
		AF4 mul = AF4(1, 1, 1, 1) - DebugMode * AF4(0, 0.3, 0.3, 0);
        OutputTexture[pos] = mul * InputTexture[pos];
        pos.x += 8u;
        OutputTexture[pos] = mul * InputTexture[pos];
        pos.y += 8u;
        OutputTexture[pos] = mul * InputTexture[pos];
        pos.x -= 8u;
        OutputTexture[pos] = mul * InputTexture[pos];
    }
}
