#pragma once

#include "SysUtils.h"

// Generated from precompile/baba_neural_sharpen.hlsl
// Split in several literals because of the MSVC limit for a single string literal
static std::string babaNeuralSharpenCode = R"(
// Neural sharpening, ported from BaBa_Sharpen_Neural.fx (ReShade Baba)
// Original shader: Barbatos Neural Sharpening 2.1 / 12-Channel Neural Network 2.8, MIT license
//
// The ReShade version needs two passes (luma texture + apply), here it is a single compute pass:
// every thread group loads the luma of its tile (+1 pixel border) to groupshared memory.

#ifdef VK_MODE
cbuffer Params : register(b0, space0)
#else
cbuffer Params : register(b0)
#endif
{
    // Base strength already mapped to [0, 2], 1.0 is the "Intensity" default of the ReShade shader
    float Sharpness;
    float EdgeResponse; // -1 = smooth edges, +1 = enhance edges, 0 = uniform
    int Model;          // 0 = Model A (standard), 1 = Model B (edge-enhanced)
    int ViewMode;       // 0 = normal, 1 = map only, 2 = residual map

    int ClampOutput;
    int DynamicSharpenEnabled;
    int DisplaySizeMV;
    int Debug; // MAS debug tint

    float MotionSharpness; // usually negative
    float MvScaleX;
    float MvScaleY;

    float MotionThreshold;
    float MotionScaleLimit;
    int DisplayWidth;
    int DisplayHeight;

    int MotionWidth;
    int MotionHeight;
    int IsHdr; // input is linear and unbounded, see EncodeLuma
};

#ifdef VK_MODE
[[vk::binding(1, 0)]]
#endif
Texture2D<float4> Source : register(t0);

#ifdef VK_MODE
[[vk::binding(2, 0)]]
#endif
Texture2D<float2> Motion : register(t1);

#ifdef VK_MODE
[[vk::binding(3, 0)]]
#endif
RWTexture2D<float4> Dest : register(u0);

// ---- Model A weights (copied from ModelAB_weights.fxh, namespace ModelA)
static const float4 A_Local_W[27] = {
    float4(0.157846898, -0.124734171, -0.127510592, -0.142722160),
    float4(-0.081266180, -0.035668157, -0.128144190, 0.029555680),
    float4(0.210917711, -0.152093813, -0.240400285, 0.259769797),
    float4(-0.018440859, -0.024522314, -0.128880441, -0.146906286),
    float4(0.013216234, 0.053734783, 0.141454548, 0.003527662),
    float4(-0.405102581, -0.348911375, -0.129119381, 0.220478624),
    float4(0.160163894, 0.025609946, -0.154936627, 0.000249316),
    float4(0.003316377, 0.011136279, 0.057656068, -0.143218592),
    float4(0.270902693, 0.031090865, -0.147552043, 0.070456423),
    float4(-0.024166472, -0.277416319, 0.124570601, -0.051763635),
    float4(-0.063075431, 0.229678214, 0.024368009, 0.056972858),
    float4(-0.169356808, 0.238923475, -0.007092264, 0.328647912),
    float4(-0.493815362, 0.346248239, 0.331841409, 0.500756621),
    float4(-0.000619092, -0.624130189, -0.436149389, 0.508056283),
    float4(0.077492014, 0.104612425, -0.268455684, -0.233587697),
    float4(-0.042992529, -0.168081835, -0.129785717, 0.158413351),
    float4(0.051667836, 0.455829173, 0.141271994, -0.077929094),
    float4(0.113687925, 0.209046662, -0.484994620, -0.358390838),
    float4(0.177939489, 0.052703582, -0.057644196, -0.147503659),
    float4(-0.029891005, -0.118537180, 0.029043604, 0.004845856),
    float4(0.132930592, -0.184339643, -0.218361959, 0.174553171),
    float4(-0.058614843, -0.305103749, 0.254780143, -0.034503106),
    float4(0.153854638, 0.107778028, 0.209778413, -0.009218552),
    float4(-0.360170394, -0.132584587, 0.632588685, -0.138400137),
    float4(0.190503091, 0.291831911, -0.109605379, -0.063311636),
    float4(0.040927604, -0.064029731, 0.092040010, -0.070735231),
    float4(0.178212926, 0.068329029, -0.240759194, -0.314655423)
};
static const float4 A_Local_B[3] = {
    float4(0.013958196, -0.034602143, 0.017349342, 0.051420901),
    float4(-0.002181380, 0.005415063, -0.001871437, -0.040856525),
    float4(0.021341776, 0.016098073, -0.027485607, -0.000488010)
};
static const float4 A_Local_A[3] = {
    float4(-2.387171745, -0.272749335, -2.923240185, -0.953309059),
    float4(-2.168637514, 0.855447471, -0.171175957, 0.460944146),
    float4(0.312436283, -0.096803114, -0.173512161, -1.026450276)
};
static const float4 A_Mix_W[36] = {
    float4(-0.073890910, 0.442915738, 0.062885523, -0.237529442),
    float4(-0.354415715, 0.255197883, -0.292075455, -0.725630701),
    float4(0.864738226, -0.477833390, -0.162099168, -0.246071026),
    float4(-0.159830540, 0.038192000, -0.158631608, 0.733967006),
    float4(-0.008712847, 2.043683290, 0.222858787, -0.048534606),
    float4(0.136713073, 1.056981683, 0.002627724, -0.462668061),
    float4(-0.014323278, -1.849385142, -0.047311105, -0.018056788),
    float4(0.160120249, -1.340586066, -0.508502781, 0.158020362),
    float4(-1.483291268, -0.006965798, 0.160122022, 0.114788808),
    float4(-0.399437845, 0.087671049, -0.143500865, -0.267752349),
    float4(1.759939194, 0.065428883, -0.316779077, -0.108699806),
    float4(0.717141092, -0.080086760, 0.126877829, -0.354385585),
    float4(0.471214026, 0.549063981, -0.014970109, -0.376401037),
    float4(-0.918480992, -0.645617664, -0.418581784, 0.288688481),
    float4(-0.821215689, -0.385795057, 0.239962846, 0.137052476),
    float4(0.160448000, 0.688369751, 0.691993833, -0.424544305),
    float4(0.332775176, -0.024426594, 0.425284505, 0.222185135),
    float4(0.145794243, -0.145882696, -0.765440702, -0.024649546),
    float4(-0.084028967, -1.247046232, -0.231615975, 0.664888382),
    float4(0.312038898, 1.263586879, 0.364498079, 0.522409737),
    float4(0.051209208, 0.561254084, -0.455035418, -0.320654094),
    float4(0.305358291, -0.254941225, -0.346134901, -0.292018741),
    float4(0.262363881, -2.439566135, -0.590004444, -0.150398821),
    float4(-0.222656667, -0.202635348, -0.225798979, -0.067541674),
    float4(-1.269040704, -0.059846044, 0.084006079, 0.706222057),
    float4(-0.877907932, -0.114583023, 0.211630091, 0.710830331),
    float4(-1.432761431, -0.421258569, 0.412224859, 0.558480740),
    float4(-0.141609788, -0.390305877, 0.055484608, 0.364328563),
    float4(0.175326273, -0.270830810, -0.473862469, 0.326092124),
    float4(0.375253499, 0.119935840, -0.175328150, 0.249779537),
    float4(-0.483496517, -1.311672688, 0.200376466, -0.228427365),
    float4(-0.040529206, -1.643486500, -0.170545414, -0.095221654),
    float4(-0.319517046, -0.295663029, -0.056098495, -0.444056034),
    float4(-0.355080426, -0.660095990, 0.286984503, 0.303410649),
    float4(-0.786878586, 0.552567244, -0.875478148, 0.512069345),
    float4(0.965228558, -0.123915620, 0.122943603, 0.157081172)
};
static const float4 A_Mix_B[3] = {
    float4(0.031355277, 0.220565408, -0.028643996, -0.000396760),
    float4(0.053762030, 0.191098660, 0.028867338, -0.012472775),
    float4(0.009898989, -0.032807596, 0.000105271, -0.024801077)
};
static const float4 A_Mix_A[3] = {
    float4(-1.785561442, -0.498042375, -0.242182761, -1.958031893),
    float4(-5.690943241, -0.729003668, -0.219683945, -3.025264025),
    float4(-2.192382336, -1.276227236, -0.214304626, -2.779862165)
};
static const float4 A_Out_W[3] = {
    float4(-1.723722577, -1.003309250, 1.881006837, 0.553851724),
    float4(-0.404766232, 0.851177633, 0.785482407, -0.269340128),
    float4(-0.382485956, 0.502349734, -0.833594918, 1.040693045)
};
static const float A_Out_Bias = 0.041042306;
// ---- Model B weights (copied from ModelAB_weights.fxh, namespace ModelB)
static const float4 B_Local_W[27] = {
    float4(-0.180299282, 0.014662747, 0.062037338, 0.162913963),
    float4(-0.096845955, -0.120704472, 0.018369380, 0.099958651),
    float4(-0.069051631, 0.084565893, 0.073071629, 0.168920413),
    float4(-0.040158097, -0.134107947, 0.069352902, 0.256246716),
    float4(-0.170313105, -0.429769307, -0.140258655, -0.072976060),
    float4(0.006422973, -0.032386098, 0.088514760, -0.294761181),
    float4(-0.186079368, 0.002835276, 0.152039975, 0.139015242),
    float4(-0.001664574, 0.045396138, 0.168260053, -0.068442158),
    float4(0.247336522, 0.189809218, -0.182572842, 0.225187257),
    float4(-0.070876308, 0.080869898, 0.216372654, -0.001347039),
    float4(0.298240304, -0.487868726, -0.011222760, 0.326351106),
    float4(-0.563833356, 0.083959579, -0.075837053, -0.235026225),
    float4(0.436923504, 0.340757638, -0.517588019, 0.083469912),
    float4(-0.197693869, 0.114102028, 0.453443885, -0.177208096),
    float4(0.035827920, -0.366167694, 0.453837216, 0.398048311),
    float4(-0.022785911, -0.090594806, 0.096829072, -0.151360869),
    float4(-0.159646124, 0.201200619, -0.038751587, 0.155050546),
    float4(0.400151104, -0.125081092, -0.035440426, -0.357304394),
    float4(0.123323008, 0.150079280, -0.071160279, -0.032094222),
    float4(-0.247537255, 0.395797133, -0.195361555, -0.067078769),
    float4(-0.094768256, 0.182795376, -0.047163446, 0.144929826),
    float4(0.147065058, 0.056526992, -0.022634532, -0.406054407),
    float4(0.090287514, -0.481248379, -0.089255974, -0.089840382),
    float4(-0.188313007, 0.200342402, -0.209137321, -0.289527595),
    float4(-0.157484218, -0.062786549, 0.087097272, -0.055152047),
    float4(-0.246287793, -0.003776026, -0.153421938, 0.065058291),
    float4(0.229671970, 0.031603150, 0.160108879, 0.226898223)
};
static const float4 B_Local_B[3] = {
    float4(0.076806329, -0.002794162, 0.061713792, -0.002923975),
    float4(0.026175125, -0.153714553, 0.074806444, -0.009952844),
    float4(-0.002780868, -0.010205056, -0.021362821, 0.022398105)
};
static const float4 B_Local_A[3] = {
    float4(-1.442547083, -1.845595598, -1.306786060, -0.781065464),
    float4(-0.109224759, -0.269420534, -0.060583767, 4.149562359),
    float4(-0.792430758, -1.190022588, 0.142513722, 0.115136072)
};
static const float4 B_Mix_W[36] = {
    float4(-0.229082733, -0.512871683, 0.736965120, -0.114540882),
    float4(0.480870575, 0.391051710, -0.417410672, 0.369169980),
    float4(0.789031744, -0.195746094, -0.737288594, -0.646142125),
    float4(-0.600302875, 0.046118792, 0.435882509, -0.587501466),
    float4(0.311843961, 0.229555234, -0.347491890, -0.145946071),
    float4(0.009561459, -0.459208190, 0.736723900, 0.021142924),
    float4(0.175059706, 0.691861987, 0.824216008, -0.017000724),
    float4(0.547766745, 0.373978168, -0.503097653, -0.090789184),
    float4(-0.323113024, 0.993536174, 0.120587423, 0.773548186),
    float4(0.269746661, -0.397482157, 0.399009675, 3.502569199),
    float4(-0.435575575, -0.646290779, 0.176628053, -3.576125145),
    float4(-0.345212072, 0.522237957, -0.069926046, -1.903903365),
    float4(-0.351149797, 0.594739795, -0.068887308, 0.360132426),
    float4(0.535218537, 0.025757920, -0.459538639, 0.198079765),
    float4(0.165266067, -0.195548356, -0.235718101, 0.526902139),
    float4(-0.848744035, -0.241835013, -0.068041831, -0.782595038),
    float4(-0.879847527, 0.880598426, -1.479037523, -0.044767305),
    float4(-0.176006883, -0.002988719, 0.832145870, 0.274749726),
    float4(-0.411046326, -0.018310603, 0.489419967, -0.265991926),
    float4(-0.665538311, -0.007513349, -0.313240737, -0.339458108),
    float4(0.112935498, -0.615179121, -0.769895375, -0.700175762),
    float4(-0.025276352, -0.236055046, 0.435621321, -1.177589297),
    float4(-0.114177100, -1.120269060, -0.591441274, -2.321050406),
    float4(0.182142422, -0.052835885, -0.442020327, -1.241039634),
    float4(-0.475019783, 0.175065041, 0.764335096, -0.890451252),
    float4(0.582823992, -0.069464915, -0.495929092, 0.401011854),
    float4(0.399218798, 0.489937872, -0.335512757, -0.769207895),
    float4(-0.224198133, -0.468186438, 0.389229238, 0.309193313),
    float4(0.180819586, -0.296335191, -0.511490107, 0.079025216),
    float4(0.271378279, 0.528470814, 0.503377557, 0.125316218),
    float4(0.378717065, 0.111551061, 0.790561855, -0.215333298),
    float4(-0.264145225, 0.326991796, -0.568252385, 0.097437136),
    float4(-0.549045265, 0.121311098, -0.427249581, 0.354185373),
    float4(0.272755206, -0.557049394, -0.097753078, -2.256846189),
    float4(-0.553730965, -0.940150917, 0.235617280, 1.546710849),
    float4(0.228109702, 0.454378009, -0.301024586, -3.406354189)
};
static const float4 B_Mix_B[3] = {
    float4(0.017644040, -0.120309018, -0.029212048, 0.068806812),
    float4(0.053569581, -0.141184986, 0.036499061, -0.027951837),
    float4(0.221204460, -0.030860605, 0.061900575, 0.100701667)
};
static const float4 B_Mix_A[3] = {
    float4(-1.701528668, -2.895941973, -0.477188736, -0.332290858),
    float4(0.147276983, -0.674965382, 0.295771748, -0.229368791),
    float4(-0.975452960, -3.820577860, -1.948044658, -1.310231686)
};
static const float4 B_Out_W[3] = {
    float4(-0.875323415, 1.467422843, -1.980630398, -0.803634167),
    float4(-1.479813099, -1.135522127, -0.968504190, 0.906366646),
    float4(-1.197628736, 1.004730344, 1.426059246, -2.517558813)
};
static const float B_Out_Bias = 0.065220654;
// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
)"
                                           R"(

// BT.601 weights, paired with RGBToYCbCr / YCbCrToRGB below (same as the ReShade version)
static const float3 kLumaCoeff = float3(0.299, 0.587, 0.114);

// 16x16 thread group + 1-pixel border on each side for the 3x3 luma taps
groupshared float s_luma[18][18];

float GetLuma(float3 rgb)
{
    return dot(rgb, kLumaCoeff);
}

float3 RGBToYCbCr(float3 rgb)
{
    float y = GetLuma(rgb);
    float cb = (rgb.z - y) * 0.564 + 0.5;
    float cr = (rgb.x - y) * 0.713 + 0.5;
    return float3(y, cb, cr);
}

float3 YCbCrToRGB(float3 ycbcr)
{
    float y = ycbcr.x;
    float cb = ycbcr.y - 0.5;
    float cr = ycbcr.z - 0.5;
    float r = y + 1.403 * cr;
    float g = y - 0.344 * cb - 0.714 * cr;
    float b = y + 1.770 * cb;
    return float3(r, g, b);
}

// The network and the artifact thresholds are made for display referred [0, 1] luma (ReShade runs after
// tonemapping). Like rcas.hlsl, neighbourhoods brighter than 1 are scaled down to [0, 1] (dark ones are not
// scaled up), also for SDR flagged games, some send pre-tonemap HDR color anyway. With the HDR flag the input is
// linear, gamma 2.2 makes it perceptual. No Reinhard here, its inverse has a pole and a small residual on a
// highlight would explode.
float EncodeLuma(float l, float localScale)
{
    float n = l / localScale;
    return IsHdr > 0 ? pow(max(n, 0.0), 1.0 / 2.2) : n;
}

// HDR only, the SDR path adds the change directly
float DecodeLuma(float e, float localScale)
{
    return pow(max(e, 0.0), 2.2) * localScale;
}

// Bit test, fxc can optimize isfinite / isnan away
bool IsFinite(float x)
{
    return (asuint(x) & 0x7f800000) != 0x7f800000;
}

int2 ClampCoord(int2 p)
{
    return int2(clamp(p.x, 0, DisplayWidth - 1), clamp(p.y, 0, DisplayHeight - 1));
}

int2 ClampMotionCoord(int2 p)
{
    return int2(clamp(p.x, 0, MotionWidth - 1), clamp(p.y, 0, MotionHeight - 1));
}

float3 LoadColor(int2 p)
{
    return Source.Load(int3(ClampCoord(p), 0)).rgb;
}

float2 LoadMotion(int2 p)
{
    return Motion.Load(int3(ClampMotionCoord(p), 0)).rg;
}

// Same motion adaptive logic as the depth aware shaders, result is the final strength in [0, 2]
float ComputeAdaptiveSharpness(int2 pixelCoord)
{
    float setSharpness = Sharpness;

    if (DynamicSharpenEnabled > 0)
    {
        float2 mv;

        if (DisplaySizeMV > 0)
        {
            mv = LoadMotion(pixelCoord);
        }
        else
        {
            float2 mvf = (float2(pixelCoord) + 0.5) * (float2(MotionWidth, MotionHeight) / float2(DisplayWidth, DisplayHeight));
            mv = LoadMotion(int2(mvf));
        }

        float motion = max(abs(mv.x * MvScaleX), abs(mv.y * MvScaleY));
        float add = 0.0;

        if (motion > MotionThreshold)
        {
            float denom = max(MotionScaleLimit - MotionThreshold, 1e-6);
            add = ((motion - MotionThreshold) / denom) * MotionSharpness;
        }

        add = clamp(add, min(0.0, MotionSharpness), max(0.0, MotionSharpness));
        setSharpness += add;
    }

    return clamp(setSharpness, 0.0, 2.0);
}

// Reddish for sharpness added by motion, greenish for removed
float3 ApplyMasDebugTint(float3 color, float baseSharpness, float adaptiveSharpness)
{
    float motionBoost = max(adaptiveSharpness - baseSharpness, 0.0);
    float motionReduce = max(baseSharpness - adaptiveSharpness, 0.0);

    color.x *= 1.0 + 12.0 * motionBoost;
    color.y *= 1.0 + 12.0 * motionReduce;

    return color;
}

void WritePassthrough(int2 p, float3 c, float alpha, float intensity)
{
    if (Debug > 0 && DynamicSharpenEnabled > 0)
        c = ApplyMasDebugTint(c, Sharpness, intensity);

    if (ClampOutput > 0)
        c = saturate(c);

    Dest[p] = float4(c, alpha);
}

// -----------------------------------------------------------------------------
// Neural network, one copy per model so the weights stay compile time constants
// taps: luma of the 3x3 neighbourhood, row major (0 1 2 / 3 4 5 / 6 7 8)
// -----------------------------------------------------------------------------

float RunNetA(float taps[9])
{
    // LAYER 1: 3x3 convolution, 12 channels stored as 3 float4 groups
    float4 L1_0 = A_Local_B[0];
    float4 L1_1 = A_Local_B[1];
    float4 L1_2 = A_Local_B[2];
    L1_0 += taps[0] * A_Local_W[0];
    L1_1 += taps[0] * A_Local_W[1];
    L1_2 += taps[0] * A_Local_W[2];
    L1_0 += taps[1] * A_Local_W[3];
    L1_1 += taps[1] * A_Local_W[4];
    L1_2 += taps[1] * A_Local_W[5];
    L1_0 += taps[2] * A_Local_W[6];
    L1_1 += taps[2] * A_Local_W[7];
    L1_2 += taps[2] * A_Local_W[8];
    L1_0 += taps[3] * A_Local_W[9];
    L1_1 += taps[3] * A_Local_W[10];
    L1_2 += taps[3] * A_Local_W[11];
    L1_0 += taps[4] * A_Local_W[12];
    L1_1 += taps[4] * A_Local_W[13];
    L1_2 += taps[4] * A_Local_W[14];
    L1_0 += taps[5] * A_Local_W[15];
    L1_1 += taps[5] * A_Local_W[16];
    L1_2 += taps[5] * A_Local_W[17];
    L1_0 += taps[6] * A_Local_W[18];
    L1_1 += taps[6] * A_Local_W[19];
    L1_2 += taps[6] * A_Local_W[20];
    L1_0 += taps[7] * A_Local_W[21];
    L1_1 += taps[7] * A_Local_W[22];
    L1_2 += taps[7] * A_Local_W[23];
    L1_0 += taps[8] * A_Local_W[24];
    L1_1 += taps[8] * A_Local_W[25];
    L1_2 += taps[8] * A_Local_W[26];

    // PReLU 1
    L1_0 = max(0.0, L1_0) + min(0.0, L1_0) * A_Local_A[0];
    L1_1 = max(0.0, L1_1) + min(0.0, L1_1) * A_Local_A[1];
    L1_2 = max(0.0, L1_2) + min(0.0, L1_2) * A_Local_A[2];

    // LAYER 2: 1x1 dense, 12 -> 12
    float4 L2_0 = A_Mix_B[0];
    float4 L2_1 = A_Mix_B[1];
    float4 L2_2 = A_Mix_B[2];
    L2_0.x += dot(L1_0, A_Mix_W[0]);
    L2_0.y += dot(L1_0, A_Mix_W[1]);
    L2_0.z += dot(L1_0, A_Mix_W[2]);
    L2_0.w += dot(L1_0, A_Mix_W[3]);
    L2_0.x += dot(L1_1, A_Mix_W[4]);
    L2_0.y += dot(L1_1, A_Mix_W[5]);
    L2_0.z += dot(L1_1, A_Mix_W[6]);
    L2_0.w += dot(L1_1, A_Mix_W[7]);
    L2_0.x += dot(L1_2, A_Mix_W[8]);
    L2_0.y += dot(L1_2, A_Mix_W[9]);
    L2_0.z += dot(L1_2, A_Mix_W[10]);
    L2_0.w += dot(L1_2, A_Mix_W[11]);
    L2_1.x += dot(L1_0, A_Mix_W[12]);
    L2_1.y += dot(L1_0, A_Mix_W[13]);
    L2_1.z += dot(L1_0, A_Mix_W[14]);
    L2_1.w += dot(L1_0, A_Mix_W[15]);
    L2_1.x += dot(L1_1, A_Mix_W[16]);
    L2_1.y += dot(L1_1, A_Mix_W[17]);
    L2_1.z += dot(L1_1, A_Mix_W[18]);
    L2_1.w += dot(L1_1, A_Mix_W[19]);
    L2_1.x += dot(L1_2, A_Mix_W[20]);
    L2_1.y += dot(L1_2, A_Mix_W[21]);
    L2_1.z += dot(L1_2, A_Mix_W[22]);
    L2_1.w += dot(L1_2, A_Mix_W[23]);
    L2_2.x += dot(L1_0, A_Mix_W[24]);
    L2_2.y += dot(L1_0, A_Mix_W[25]);
    L2_2.z += dot(L1_0, A_Mix_W[26]);
    L2_2.w += dot(L1_0, A_Mix_W[27]);
    L2_2.x += dot(L1_1, A_Mix_W[28]);
    L2_2.y += dot(L1_1, A_Mix_W[29]);
    L2_2.z += dot(L1_1, A_Mix_W[30]);
    L2_2.w += dot(L1_1, A_Mix_W[31]);
    L2_2.x += dot(L1_2, A_Mix_W[32]);
    L2_2.y += dot(L1_2, A_Mix_W[33]);
    L2_2.z += dot(L1_2, A_Mix_W[34]);
    L2_2.w += dot(L1_2, A_Mix_W[35]);

    // PReLU 2
    L2_0 = max(0.0, L2_0) + min(0.0, L2_0) * A_Mix_A[0];
    L2_1 = max(0.0, L2_1) + min(0.0, L2_1) * A_Mix_A[1];
    L2_2 = max(0.0, L2_2) + min(0.0, L2_2) * A_Mix_A[2];

    // LAYER 3: 1x1 output, 12 -> 1 (luma residual)
    float res = A_Out_Bias;
    res += dot(L2_0, A_Out_W[0]);
    res += dot(L2_1, A_Out_W[1]);
    res += dot(L2_2, A_Out_W[2]);
    return res;
}

float RunNetB(float taps[9])
{
    // LAYER 1: 3x3 convolution, 12 channels stored as 3 float4 groups
    float4 L1_0 = B_Local_B[0];
    float4 L1_1 = B_Local_B[1];
    float4 L1_2 = B_Local_B[2];
    L1_0 += taps[0] * B_Local_W[0];
    L1_1 += taps[0] * B_Local_W[1];
    L1_2 += taps[0] * B_Local_W[2];
    L1_0 += taps[1] * B_Local_W[3];
    L1_1 += taps[1] * B_Local_W[4];
    L1_2 += taps[1] * B_Local_W[5];
    L1_0 += taps[2] * B_Local_W[6];
    L1_1 += taps[2] * B_Local_W[7];
    L1_2 += taps[2] * B_Local_W[8];
    L1_0 += taps[3] * B_Local_W[9];
    L1_1 += taps[3] * B_Local_W[10];
    L1_2 += taps[3] * B_Local_W[11];
    L1_0 += taps[4] * B_Local_W[12];
    L1_1 += taps[4] * B_Local_W[13];
    L1_2 += taps[4] * B_Local_W[14];
    L1_0 += taps[5] * B_Local_W[15];
    L1_1 += taps[5] * B_Local_W[16];
    L1_2 += taps[5] * B_Local_W[17];
    L1_0 += taps[6] * B_Local_W[18];
    L1_1 += taps[6] * B_Local_W[19];
    L1_2 += taps[6] * B_Local_W[20];
    L1_0 += taps[7] * B_Local_W[21];
    L1_1 += taps[7] * B_Local_W[22];
    L1_2 += taps[7] * B_Local_W[23];
    L1_0 += taps[8] * B_Local_W[24];
    L1_1 += taps[8] * B_Local_W[25];
    L1_2 += taps[8] * B_Local_W[26];

    // PReLU 1
    L1_0 = max(0.0, L1_0) + min(0.0, L1_0) * B_Local_A[0];
    L1_1 = max(0.0, L1_1) + min(0.0, L1_1) * B_Local_A[1];
    L1_2 = max(0.0, L1_2) + min(0.0, L1_2) * B_Local_A[2];

    // LAYER 2: 1x1 dense, 12 -> 12
    float4 L2_0 = B_Mix_B[0];
    float4 L2_1 = B_Mix_B[1];
    float4 L2_2 = B_Mix_B[2];
    L2_0.x += dot(L1_0, B_Mix_W[0]);
    L2_0.y += dot(L1_0, B_Mix_W[1]);
    L2_0.z += dot(L1_0, B_Mix_W[2]);
    L2_0.w += dot(L1_0, B_Mix_W[3]);
    L2_0.x += dot(L1_1, B_Mix_W[4]);
    L2_0.y += dot(L1_1, B_Mix_W[5]);
    L2_0.z += dot(L1_1, B_Mix_W[6]);
    L2_0.w += dot(L1_1, B_Mix_W[7]);
    L2_0.x += dot(L1_2, B_Mix_W[8]);
    L2_0.y += dot(L1_2, B_Mix_W[9]);
    L2_0.z += dot(L1_2, B_Mix_W[10]);
    L2_0.w += dot(L1_2, B_Mix_W[11]);
    L2_1.x += dot(L1_0, B_Mix_W[12]);
    L2_1.y += dot(L1_0, B_Mix_W[13]);
    L2_1.z += dot(L1_0, B_Mix_W[14]);
    L2_1.w += dot(L1_0, B_Mix_W[15]);
    L2_1.x += dot(L1_1, B_Mix_W[16]);
    L2_1.y += dot(L1_1, B_Mix_W[17]);
    L2_1.z += dot(L1_1, B_Mix_W[18]);
    L2_1.w += dot(L1_1, B_Mix_W[19]);
    L2_1.x += dot(L1_2, B_Mix_W[20]);
    L2_1.y += dot(L1_2, B_Mix_W[21]);
    L2_1.z += dot(L1_2, B_Mix_W[22]);
    L2_1.w += dot(L1_2, B_Mix_W[23]);
    L2_2.x += dot(L1_0, B_Mix_W[24]);
    L2_2.y += dot(L1_0, B_Mix_W[25]);
    L2_2.z += dot(L1_0, B_Mix_W[26]);
    L2_2.w += dot(L1_0, B_Mix_W[27]);
    L2_2.x += dot(L1_1, B_Mix_W[28]);
    L2_2.y += dot(L1_1, B_Mix_W[29]);
    L2_2.z += dot(L1_1, B_Mix_W[30]);
    L2_2.w += dot(L1_1, B_Mix_W[31]);
    L2_2.x += dot(L1_2, B_Mix_W[32]);
    L2_2.y += dot(L1_2, B_Mix_W[33]);
    L2_2.z += dot(L1_2, B_Mix_W[34]);
    L2_2.w += dot(L1_2, B_Mix_W[35]);

    // PReLU 2
    L2_0 = max(0.0, L2_0) + min(0.0, L2_0) * B_Mix_A[0];
    L2_1 = max(0.0, L2_1) + min(0.0, L2_1) * B_Mix_A[1];
    L2_2 = max(0.0, L2_2) + min(0.0, L2_2) * B_Mix_A[2];

    // LAYER 3: 1x1 output, 12 -> 1 (luma residual)
    float res = B_Out_Bias;
    res += dot(L2_0, B_Out_W[0]);
    res += dot(L2_1, B_Out_W[1]);
    res += dot(L2_2, B_Out_W[2]);
    return res;
}

// Network answer for a flat patch (all 9 taps = mean). With one input and PReLU activations it is piecewise
// linear in mean, written here as hinges at its breakpoints, the same function as RunNetA/B on a flat patch
// (float32 differs ~1e-6). Generated from the weights above, regenerate if they change. Breakpoints above 1 are
// left out: encoded taps are never above 1, localScale is at least the largest tap
float RunNetFlatA(float mean)
{
    float flat = 0.0060600224405 + 0.175067294912 * mean;

    // Only one side's hinges can be non zero, negative means are rare (SDR with negative luma)
    [branch]
    if (mean >= 0.0)
    {
        flat += -0.150968173604 * max(mean - 0.014251068354, 0.0);
        flat += -0.0572397387027 * max(mean - 0.0247515683503, 0.0);
        flat += 0.000115763534968 * max(mean - 0.0550066773131, 0.0);
        flat += 0.0305009873447 * max(mean - 0.097019030578, 0.0);
        flat += 0.0681568495192 * max(mean - 0.135350654766, 0.0);
        flat += -0.225701032239 * max(mean - 0.167670870173, 0.0);
        flat += 0.138205237234 * max(mean - 0.176185385576, 0.0);
        flat += 0.0379811555884 * max(mean - 0.614819644624, 0.0);
        flat += 0.312393273336 * max(mean - 0.878628609894, 0.0);
        flat += -0.325835521453 * max(mean - 0.879908311442, 0.0);
    }
    else
    {
        flat += -0.320186759593 * max(-0.0153030932581 - mean, 0.0);
        flat += 0.248268476742 * max(-0.0182633030934 - mean, 0.0);
        flat += 0.131543843716 * max(-0.024893074335 - mean, 0.0);
        flat += -0.828078985856 * max(-0.0286011565566 - mean, 0.0);
        flat += 0.378372720001 * max(-0.029243185007 - mean, 0.0);
        flat += -0.127722375726 * max(-0.116665666306 - mean, 0.0);
        flat += 0.291334879569 * max(-0.1342146258 - mean, 0.0);
        flat += -1.06977892019 * max(-0.166516498241 - mean, 0.0);
        flat += -0.00634175929432 * max(-0.188603808329 - mean, 0.0);
        flat += -0.218456127884 * max(-0.288253670903 - mean, 0.0);
        flat += -0.0126240685325 * max(-0.342916063483 - mean, 0.0);
        flat += 0.986959893291 * max(-0.39312024127 - mean, 0.0);
        flat += 0.0909796853916 * max(-0.431024404817 - mean, 0.0);
        flat += 0.236969792167 * max(-0.707216963513 - mean, 0.0);
        flat += -0.161559871524 * max(-2.51984802432 - mean, 0.0);
        flat += 0.00874072097725 * max(-6.13224086942 - mean, 0.0);
    }
)"
                                           R"(

    return flat;
}

float RunNetFlatB(float mean)
{
    float flat = 0.0050881055069 + 0.751365926683 * mean;

    [branch]
    if (mean >= 0.0)
    {
        flat += -3.1911015516 * max(mean - 0.00632336345482, 0.0);
        flat += 2.92045777063 * max(mean - 0.00779963788163, 0.0);
        flat += -0.545743344435 * max(mean - 0.0141646869386, 0.0);
        flat += 0.481058718233 * max(mean - 0.0357847241578, 0.0);
        flat += -0.746653119581 * max(mean - 0.0409117461946, 0.0);
        flat += 0.248145308176 * max(mean - 0.0582469815953, 0.0);
        flat += -0.287864643495 * max(mean - 0.0862950277974, 0.0);
        flat += -0.287965471802 * max(mean - 0.0877899551077, 0.0);
        flat += 0.666144589111 * max(mean - 0.0947852013482, 0.0);
        flat += -0.00776714226758 * max(mean - 0.807396931514, 0.0);
    }
    else
    {
        flat += -0.395228421239 * max(-0.0118680346466 - mean, 0.0);
        flat += -1.11449681372 * max(-0.200443822499 - mean, 0.0);
        flat += 1.34960286286 * max(-0.409868462457 - mean, 0.0);
        flat += -0.889477289545 * max(-0.4784245775 - mean, 0.0);
        flat += 1.09211718264 * max(-0.480572785318 - mean, 0.0);
        flat += 0.0136815823277 * max(-0.670213587711 - mean, 0.0);
        flat += -0.418580677883 * max(-0.794003817545 - mean, 0.0);
        flat += 0.243976450468 * max(-0.853032168865 - mean, 0.0);
        flat += -0.1066883019 * max(-1.547628992 - mean, 0.0);
        flat += -0.011665346854 * max(-6.33907306161 - mean, 0.0);
    }

    return flat;
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID,
            uint GIndex : SV_GroupIndex)
{
    const int2 groupOrigin = int2(Gid.xy) * 16 - 1;

    // Luma tile of the group, borders are clamped like the clamped sampler of the ReShade version.
    // Luma of an Inf / NaN color is not finite either, the finite check below catches it
    for (uint ti = GIndex; ti < 18 * 18; ti += 256)
    {
        const int2 local = int2(ti % 18, ti / 18);
        s_luma[local.y][local.x] = GetLuma(LoadColor(groupOrigin + local));
    }

    GroupMemoryBarrierWithGroupSync();

    int2 p = int2(DTid.x, DTid.y);

    // Guard against oversized dispatch
    if (p.x >= DisplayWidth || p.y >= DisplayHeight)
        return;

    float4 source = Source.Load(int3(p, 0));
    float3 c = source.rgb;
    float intensity = ComputeAdaptiveSharpness(p);

    // Nothing to add, the debug views still need the network so they skip this
    if (intensity <= 0.0 && ViewMode == 0)
    {
        WritePassthrough(p, c, source.a, intensity);
        return;
    }

    // Luma of the 3x3 neighbourhood from shared memory
    const int2 s = int2(GTid.xy) + 1;
    float taps[9];
    taps[0] = s_luma[s.y - 1][s.x - 1];
    taps[1] = s_luma[s.y - 1][s.x];
    taps[2] = s_luma[s.y - 1][s.x + 1];
    taps[3] = s_luma[s.y][s.x - 1];
    taps[4] = s_luma[s.y][s.x];
    taps[5] = s_luma[s.y][s.x + 1];
    taps[6] = s_luma[s.y + 1][s.x - 1];
    taps[7] = s_luma[s.y + 1][s.x];
    taps[8] = s_luma[s.y + 1][s.x + 1];

    float lumaLinear = taps[4];
    float localScale = 1.0;
    bool finite = true;

    [unroll]
    for (int i = 0; i < 9; i++)
    {
        localScale = max(localScale, taps[i]);
        finite = finite && IsFinite(taps[i]);
    }

    // An Inf / NaN would set the scale of the whole neighbourhood, leave the pixel as it is
    if (!finite)
    {
        WritePassthrough(p, c, source.a, intensity);
        return;
    }

    [unroll]
    for (int j = 0; j < 9; j++)
        taps[j] = EncodeLuma(taps[j], localScale);

    // The network gives a small positive residual even on a flat patch (~+0.007). Invisible after tonemapping,
    // but it doubles the brightness of dark linear values (pre-tonemap input). Its answer for a flat patch at the
    // local mean is subtracted, so only the detail is sharpened
    float mean = 0.0;

    [unroll]
    for (int k = 0; k < 9; k++)
        mean += taps[k];

    mean /= 9.0;

    float rawResidual;

    [branch]
    if (Model == 0)
        rawResidual = RunNetA(taps) - RunNetFlatA(mean);
    else
        rawResidual = RunNetB(taps) - RunNetFlatB(mean);

    float normalizedResidual = saturate(abs(rawResidual) * 2.63); // 2.63 ~ 1/0.38

    // Debug: residual map
    if (ViewMode == 2)
    {
        Dest[p] = float4(normalizedResidual, normalizedResidual, normalizedResidual, source.a);
        return;
    }

    float residual = clamp(rawResidual, -0.5, 0.5) * 0.22;

    // Edge response: negative smooths edges, positive enhances them
    // (lerp(1, 1 - r, |e|) for e < 0 and lerp(1, 1 + r, e) for e > 0 are both 1 + e * r)
    if (abs(EdgeResponse) > 0.001)
        residual *= 1.0 + EdgeResponse * normalizedResidual;

    // Debug: sharpening map
    if (ViewMode == 1)
    {
        float debugRes = residual * intensity;
        Dest[p] = float4(0.5 + debugRes, 0.5 + debugRes, 0.5 + debugRes, source.a);
        return;
    }

    // Suppress the effect on aliased edges and fireflies, cross neighbourhood of the 3x3 block
    float lumaC = taps[4];
    float lumaN = taps[1];
    float lumaS = taps[7];
    float lumaE = taps[5];
    float lumaW = taps[3];

    float maxNeighbors = max(max(lumaN, lumaS), max(lumaE, lumaW));
    float minNeighbors = min(min(lumaN, lumaS), min(lumaE, lumaW));

    float localContrast = maxNeighbors - minNeighbors;
    float aliasingSignal = saturate((localContrast - 0.25) * 4.0);

    float brightSpike = max(0.0, lumaC - maxNeighbors);
    float darkSpike = max(0.0, minNeighbors - lumaC);
    float spikeIntensity = max(brightSpike, darkSpike);
    float fireflySignal = saturate((spikeIntensity - 0.1) * 6.0);

    float artifactSignal = saturate(aliasingSignal + fireflySignal);
    residual *= 1.0 - artifactSignal;

    // Luma only, chroma is kept
    float3 outColor;

    // Nothing below goes under the input's own negative (wide gamut) values, or under 0 for the rest
    float3 floorC = min(c, 0.0);

    if (IsHdr == 0)
    {
        // The change follows the pixel's own brightness, not the brightest neighbour's: a pixel at 1 next to a
        // highlight at 20 would get 20x the change otherwise. Exactly the ReShade version while localScale is 1
        float delta = residual * intensity * min(localScale, max(lumaLinear, 1.0));

        float3 ycbcr = RGBToYCbCr(c);
        ycbcr.x = max(min(lumaLinear, 0.0), lumaLinear + delta);
        outColor = max(floorC, YCbCrToRGB(ycbcr));
    }
    else
    {
        float lumaOut = DecodeLuma(lumaC + residual * intensity, localScale);

        // Back to linear, scaling rgb keeps the hue. Only when luma is ~0 next to a large channel (black, or wide
        // gamut with negative scRGB channels) the ratio would explode, those get the luma change added instead,
        // blended over 0.05 - 0.1 so there is no step. Non negative pixels always have luma >= 0.114 * max
        // channel, so they all take the ratio
        float3 absC = abs(c);
        float maxC = max(absC.r, max(absC.g, absC.b));
        float ratioWeight = saturate((lumaLinear - 0.05 * maxC) / max(0.05 * maxC, 1e-6));
        float ratio = lumaLinear > 0.05 * maxC ? lumaOut / lumaLinear : 1.0;

        // DecodeLuma(lumaC) and not lumaLinear, a negative luma is encoded as 0
        float3 added = c + (lumaOut - DecodeLuma(lumaC, localScale));
        outColor = max(floorC, lerp(added, c * ratio, ratioWeight));
    }

    if (Debug > 0 && DynamicSharpenEnabled > 0)
        outColor = ApplyMasDebugTint(outColor, Sharpness, intensity);

    if (ClampOutput > 0)
        outColor = saturate(outColor);

    Dest[p] = float4(outColor, source.a);
}
)";
