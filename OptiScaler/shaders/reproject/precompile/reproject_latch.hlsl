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
