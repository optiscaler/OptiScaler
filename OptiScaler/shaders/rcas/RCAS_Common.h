#pragma once

#include "SysUtils.h"

#include "shaders/BABA_NEURAL_Shader.h"
#include "shaders/DA_DAS_Shader.h"
#include "shaders/DA_RCAS_Shader.h"
#include "shaders/OPTI_SHARPEN_Shader.h"
#include "shaders/RCAS_Shader.h"

#include <atomic>

struct RcasConstants
{
    float Sharpness;

    float MvScaleX;
    float MvScaleY;

    float CameraNear;
    float CameraFar;

    bool DepthIsLinear;
    bool DepthIsReversed;

    bool IsHdr;
};

class RCAS_Common
{
  public:
    // The sharpening shader that ran last, its GPU time is shown under this name
    const char* TimedShaderName() const { return _timedShaderName; }

  protected:
    // Called by Dispatch with the shader it runs, after the fallbacks
    void SetTimedShader(SharpenShader InShader);

    struct alignas(256) InternalConstants
    {
        float Sharpness;
        float Contrast;

        // Motion Vector Stuff
        int DynamicSharpenEnabled;
        int DisplaySizeMV;
        int Debug;
        int MotionWidth;
        int MotionHeight;

        float MotionSharpness;
        float MvScaleX;
        float MvScaleY;
        float Threshold;
        float ScaleLimit;

        int OutputWidth;
        int OutputHeight;
    };

    struct alignas(256) InternalConstantsDA
    {
        float Sharpness;

        int DepthIsLinear;
        int DepthIsReversed;

        float DepthScale;
        float DepthBias;

        float DepthLinearA;
        float DepthLinearB;
        float DepthLinearC;

        int DynamicSharpenEnabled;
        int DisplaySizeMV;
        int Debug;

        float MotionSharpness;
        float MvScaleX;
        float MvScaleY;
        float MotionThreshold;
        float MotionScaleLimit;

        float DepthTextureScale;

        int ClampOutput;

        int OutputWidth;
        int OutputHeight;
        int MotionWidth;
        int MotionHeight;
        int DepthWidth;
        int DepthHeight;
    };

    // Same order as the cbuffer of baba_neural_sharpen.hlsl and opti_sharpen.hlsl
    struct alignas(256) InternalConstantsBaba
    {
        float Sharpness;
        float EdgeResponse;
        int Model;
        int ViewMode;

        int ClampOutput;
        int DynamicSharpenEnabled;
        int DisplaySizeMV;
        int Debug;

        float MotionSharpness;
        float MvScaleX;
        float MvScaleY;

        float MotionThreshold;
        float MotionScaleLimit;
        int OutputWidth;
        int OutputHeight;

        int MotionWidth;
        int MotionHeight;
        int IsHdr;
    };

    // Dx11 and Vulkan reuse the constant buffers of the other shaders
    static_assert(sizeof(InternalConstantsBaba) <= sizeof(InternalConstants));
    static_assert(sizeof(InternalConstantsBaba) <= sizeof(InternalConstantsDA));

    void FillMotionConstants(InternalConstants& OutConstants, const RcasConstants& InConstants);
    void FillMotionConstants(InternalConstantsDA& OutConstants, const RcasConstants& InConstants);
    // InShader is BabaNeural or OptiSharpen, they share the constants layout
    void FillMotionConstants(InternalConstantsBaba& OutConstants, const RcasConstants& InConstants,
                             SharpenShader InShader);

  private:
    // Written by Dispatch, read by the present hooks
    std::atomic<const char*> _timedShaderName { "RCAS" };
};
