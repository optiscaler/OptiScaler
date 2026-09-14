cbuffer Params : register(b0)
{
    // c0
    float DiffThreshold;
    float PinkAmount;
    float MouseDeltaX;
    float MouseDeltaY;

    float ScreenWidth;
    float ScreenHeight;
    float CameraVFov;
    float CameraAspectRatio;

    uint EdgeMode;
    float TanHalfFovY;
    float InvTanHalfFovY;
    float InvTanHalfFovX;

    float3 ReprojectionRow0;
    float _pad0;

    float3 ReprojectionRow1;
    float _pad1;

    float3 ReprojectionRow2;
    float _pad2;
};

Texture2D<float3> Hudless     : register(t0);
Texture2D<float3> PresentCopy : register(t1);

RWTexture2D<float3> Present : register(u0);

SamplerState LinearClampSampler : register(s0);


[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;

    // --------------------------------------------------------
    // Bounds
    // --------------------------------------------------------

    if (pixelCoord.x >= (uint)ScreenWidth ||
        pixelCoord.y >= (uint)ScreenHeight)
    {
        return;
    }


    // --------------------------------------------------------
    // Pixel -> UV
    // --------------------------------------------------------

    float2 uv =
        (float2(pixelCoord) + 0.5f) /
        float2(ScreenWidth, ScreenHeight);


    // --------------------------------------------------------
    // UI extraction
    // --------------------------------------------------------

    float3 hudless =
        Hudless.Load(int3(pixelCoord, 0));

    float3 present =
        PresentCopy.Load(int3(pixelCoord, 0));

    float3 diff =
        abs(hudless - present);

    float delta =
        max(
            max(diff.r, diff.g),
            diff.b
        );

    float uiMask =
        smoothstep(
            DiffThreshold,
            DiffThreshold * 2.0f,
            delta
        );


    // --------------------------------------------------------
    // Create camera ray.
    //
    // We intentionally DON'T normalize this.
    //
    // Normalization isn't needed because perspective projection
    // below only depends on X/Z and Y/Z.
    // --------------------------------------------------------

    float2 ndc;

    ndc.x =
        uv.x * 2.0f - 1.0f;

    ndc.y =
        1.0f - uv.y * 2.0f;


    float3 ray;

    ray.x =
        ndc.x *
        TanHalfFovY *
        CameraAspectRatio;

    ray.y =
        ndc.y *
        TanHalfFovY;

    ray.z = 1.0f;


    // --------------------------------------------------------
    // Apply precomputed inverse camera rotation.
    //
    // This replaces:
    //
    //   RotateAroundAxis()
    //   RotateAroundAxis()
    //   normalize()
    // --------------------------------------------------------

    float3 sourceRay;

    sourceRay.x =
        dot(ReprojectionRow0, ray);

    sourceRay.y =
        dot(ReprojectionRow1, ray);

    sourceRay.z =
        dot(ReprojectionRow2, ray);


    // --------------------------------------------------------
    // Perspective divide.
    //
    // We don't need to calculate tan(FOV) again because the
    // reciprocal constants were precomputed.
    // --------------------------------------------------------

    float2 sourceUV;

    if (sourceRay.z <= 0.00001f)
    {
        sourceUV = 0.0f;
    }
    else
    {
        float invZ =
            1.0f / sourceRay.z;

        float ndcX =
            sourceRay.x *
            invZ *
            InvTanHalfFovX;

        float ndcY =
            sourceRay.y *
            invZ *
            InvTanHalfFovY;

        sourceUV.x =
            ndcX * 0.5f + 0.5f;

        sourceUV.y =
            0.5f - ndcY * 0.5f;
    }


    // --------------------------------------------------------
    // Source image bounds
    // --------------------------------------------------------

    bool inside =
        sourceRay.z > 0.00001f &&
        sourceUV.x >= 0.0f &&
        sourceUV.x <= 1.0f &&
        sourceUV.y >= 0.0f &&
        sourceUV.y <= 1.0f;


    // --------------------------------------------------------
    // Reprojected game
    // --------------------------------------------------------

    float3 reprojectedGame;

    if (inside)
    {
        reprojectedGame =
            Hudless.SampleLevel(
                LinearClampSampler,
                sourceUV,
                0.0f
            );
    }
    else if (EdgeMode == 1)
    {
        reprojectedGame =
            Hudless.SampleLevel(
                LinearClampSampler,
                saturate(sourceUV),
                0.0f
            );
    }
    else
    {
        reprojectedGame =
            0.0f;
    }


    // --------------------------------------------------------
    // UI over reprojected game
    // --------------------------------------------------------

    float3 outputColor =
        lerp(
            reprojectedGame,
            present,
            uiMask
        );


    // --------------------------------------------------------
    // Optional UI debug
    // --------------------------------------------------------

    const float3 pink =
        float3(1.0f, 0.4f, 0.6f);

    outputColor =
        lerp(
            outputColor,
            pink,
            uiMask * PinkAmount
        );


    Present[pixelCoord] =
        outputColor;
}