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
