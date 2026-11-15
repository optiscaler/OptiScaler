#pragma once

#include "SysUtils.h"

#include "shaders/BABA_NEURAL_Shader.h"
#include "shaders/DA_DAS_Shader.h"
#include "shaders/DA_RCAS_Shader.h"
#include "shaders/RCAS_Shader.h"

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
  protected:
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
        float MotionTextureScale;
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
        float MotionTextureScale;
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

    // Same order as the cbuffer of BABA_NEURAL_Shader.h / baba_neural_sharpen.hlsl
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
        float MotionTextureScale;
        float MvScaleX;
        float MvScaleY;

        float MotionThreshold;
        float MotionScaleLimit;
        int OutputWidth;
        int OutputHeight;

        int MotionWidth;
        int MotionHeight;
    };

    // Dx11 and Vulkan reuse the constant buffers of the other shaders
    static_assert(sizeof(InternalConstantsBaba) <= sizeof(InternalConstants));
    static_assert(sizeof(InternalConstantsBaba) <= sizeof(InternalConstantsDA));

    void FillMotionConstants(InternalConstants& OutConstants, const RcasConstants& InConstants);
    void FillMotionConstants(InternalConstantsDA& OutConstants, const RcasConstants& InConstants);
    void FillMotionConstants(InternalConstantsBaba& OutConstants, const RcasConstants& InConstants);
};
