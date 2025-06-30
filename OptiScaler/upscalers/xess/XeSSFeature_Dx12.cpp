#pragma once
#include <pch.h>
#include <Config.h>

#include "XeSSFeature_Dx12.h"

bool XeSSFeatureDx12::Init(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCommandList,
                           NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    Device = InDevice;

    if (InitXeSS(InDevice, InParameters))
    {
        if (!Config::Instance()->OverlayMenu.value_or(true) && (Imgui == nullptr || Imgui.get() == nullptr))
            Imgui = std::make_unique<Menu_Dx12>(Util::GetProcessWindow(), InDevice);

        OutputScaler = std::make_unique<OS_Dx12>("Output Scaling", InDevice, (TargetWidth() < DisplayWidth()));
        RCAS = std::make_unique<RCAS_Dx12>("RCAS", InDevice);
        Bias = std::make_unique<Bias_Dx12>("Bias", InDevice);

        return true;
    }

    return false;
}

bool XeSSFeatureDx12::Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!IsInited() || !_xessContext || !ModuleLoaded())
    {
        LOG_ERROR("Not inited!");
        return false;
    }

    if (!RCAS->IsInit())
        Config::Instance()->RcasEnabled = false;

    if (!OutputScaler->IsInit())
        Config::Instance()->OutputScalingEnabled = false;

    xess_result_t xessResult;

    if (State::Instance().xessDebug)
    {
        LOG_ERROR("xessDebug");

        xess_dump_parameters_t dumpParams {};
        dumpParams.frame_count = State::Instance().xessDebugFrames;
        dumpParams.frame_idx = dumpCount;
        dumpParams.path = Util::DllPath().string().c_str();
        dumpParams.dump_elements_mask = XESS_DUMP_INPUT_COLOR | XESS_DUMP_INPUT_VELOCITY | XESS_DUMP_INPUT_DEPTH |
                                        XESS_DUMP_OUTPUT | XESS_DUMP_EXECUTION_PARAMETERS | XESS_DUMP_HISTORY;

        // if (!Config::Instance()->DisableReactiveMask.value_or(true))
        //     dumpParams.dump_elements_mask |= XESS_DUMP_INPUT_RESPONSIVE_PIXEL_MASK;

        xessResult = XeSSProxy::StartDump()(_xessContext, &dumpParams);
        LOG_DEBUG("Dump result: {}", ResultToString(xessResult));
        State::Instance().xessDebug = false;
        dumpCount += State::Instance().xessDebugFrames;
    }

    xess_d3d12_execute_params_t params {};

    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &params.jitterOffsetX);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &params.jitterOffsetY);

    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, &params.exposureScale) != NVSDK_NGX_Result_Success)
        params.exposureScale = 1.0f;

    InParameters->Get(NVSDK_NGX_Parameter_Reset, &params.resetHistory);

    GetRenderResolution(InParameters, &params.inputWidth, &params.inputHeight);

    _sharpness = GetSharpness(InParameters);

    float ssMulti = Config::Instance()->OutputScalingMultiplier.value_or(1.5f);

    bool useSS = Config::Instance()->OutputScalingEnabled.value_or(false) && LowResMV();

    LOG_DEBUG("Input Resolution: {0}x{1}", params.inputWidth, params.inputHeight);

    ID3D12Resource* paramColor;
    if (InParameters->Get(NVSDK_NGX_Parameter_Color, &paramColor) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Color, (void**) &paramColor);

    if (paramColor)
    {
        LOG_DEBUG("Color exist..");
        paramColor->SetName(L"paramColor");

        if (Config::Instance()->ColorResourceBarrier.has_value())
        {
            ResourceBarrier(InCommandList, paramColor,
                            (D3D12_RESOURCE_STATES) Config::Instance()->ColorResourceBarrier.value(),
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        else if (State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
                 State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine)
        {
            Config::Instance()->ColorResourceBarrier = (int) D3D12_RESOURCE_STATE_RENDER_TARGET;
            ResourceBarrier(InCommandList, paramColor, D3D12_RESOURCE_STATE_RENDER_TARGET,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        params.pColorTexture = paramColor;
    }
    else
    {
        LOG_ERROR("Color not exist!!");
        return false;
    }

    if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &params.pVelocityTexture) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &params.pVelocityTexture);

    if (params.pVelocityTexture)
    {
        LOG_DEBUG("MotionVectors exist..");
        params.pVelocityTexture->SetName(L"pVelocityTexture");

        if (Config::Instance()->MVResourceBarrier.has_value())
        {
            ResourceBarrier(InCommandList, params.pVelocityTexture,
                            (D3D12_RESOURCE_STATES) Config::Instance()->MVResourceBarrier.value(),
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        else if (State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
                 State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine)
        {
            Config::Instance()->MVResourceBarrier = (int) D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            ResourceBarrier(InCommandList, params.pVelocityTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
    }
    else
    {
        LOG_ERROR("MotionVectors not exist!!");
        return false;
    }

    ID3D12Resource* paramOutput;

    if (InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput);

    if (paramOutput)
    {
        LOG_DEBUG("Output exist..");
        paramOutput->SetName(L"paramOutput");

        if (Config::Instance()->OutputResourceBarrier.has_value())
        {
            ResourceBarrier(InCommandList, paramOutput,
                            (D3D12_RESOURCE_STATES) Config::Instance()->OutputResourceBarrier.value(),
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        if (useSS)
        {
            if (OutputScaler->CreateBufferResource(Device, paramOutput, TargetWidth(), TargetHeight(),
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            {
                OutputScaler->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                params.pOutputTexture = OutputScaler->Buffer();
            }
            else
                params.pOutputTexture = paramOutput;
        }
        else
            params.pOutputTexture = paramOutput;

        if (Config::Instance()->RcasEnabled.value_or(true) &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or(false) &&
                                   Config::Instance()->MotionSharpness.value_or(0.4) > 0.0f)) &&
            RCAS->IsInit() &&
            RCAS->CreateBufferResource(Device, params.pOutputTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
        {
            RCAS->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            params.pOutputTexture = RCAS->Buffer();
        }
    }
    else
    {
        LOG_ERROR("Output not exist!!");
        return false;
    }

    if (InParameters->Get(NVSDK_NGX_Parameter_Depth, &params.pDepthTexture) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &params.pDepthTexture);

    if (params.pDepthTexture)
    {
        LOG_DEBUG("Depth exist..");
        params.pDepthTexture->SetName(L"params.pDepthTexture");

        if (Config::Instance()->DepthResourceBarrier.has_value())
            ResourceBarrier(InCommandList, params.pDepthTexture,
                            (D3D12_RESOURCE_STATES) Config::Instance()->DepthResourceBarrier.value(),
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    else
    {
        if (LowResMV())
        {
            LOG_ERROR("Depth not exist!!");
            return false;
        }

        params.pDepthTexture = nullptr;
    }

    if (!AutoExposure())
    {
        if (InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, &params.pExposureScaleTexture) !=
            NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, (void**) &params.pExposureScaleTexture);

        if (params.pExposureScaleTexture)
        {
            LOG_DEBUG("ExposureTexture exist..");

            if (Config::Instance()->ExposureResourceBarrier.has_value())
                ResourceBarrier(InCommandList, params.pExposureScaleTexture,
                                (D3D12_RESOURCE_STATES) Config::Instance()->ExposureResourceBarrier.value(),
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        else
        {
            LOG_WARN("AutoExposure disabled but ExposureTexture is not exist, it may cause problems!!");
            State::Instance().AutoExposure = true;
            State::Instance().changeBackend[_handle->Id] = true;
            return true;
        }
    }
    else
        LOG_DEBUG("AutoExposure enabled!");

    bool supportsFloatResponsivePixelMask = Version() >= feature_version { 2, 0, 1 };
    ID3D12Resource* paramReactiveMask = nullptr;

    if (supportsFloatResponsivePixelMask &&
        InParameters->Get("FSR.reactive", &paramReactiveMask) == NVSDK_NGX_Result_Success)
    {
        if (!Config::Instance()->DisableReactiveMask.value_or(!supportsFloatResponsivePixelMask))
            params.pResponsivePixelMaskTexture = paramReactiveMask;
    }
    else
    {
        if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &paramReactiveMask) !=
            NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactiveMask);

        if (!Config::Instance()->DisableReactiveMask.value_or(!supportsFloatResponsivePixelMask) && paramReactiveMask)
        {
            LOG_DEBUG("Input Bias mask exist..");
            Config::Instance()->DisableReactiveMask = false;

            if (Config::Instance()->MaskResourceBarrier.has_value())
                ResourceBarrier(InCommandList, params.pResponsivePixelMaskTexture,
                                (D3D12_RESOURCE_STATES) Config::Instance()->MaskResourceBarrier.value(),
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            if (Config::Instance()->DlssReactiveMaskBias.value_or(0.0f) > 0.0f && Bias->IsInit() &&
                Bias->CreateBufferResource(Device, paramReactiveMask, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
                Bias->CanRender())
            {
                Bias->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                if (Bias->Dispatch(Device, InCommandList, paramReactiveMask,
                                   Config::Instance()->DlssReactiveMaskBias.value_or(0.0f), Bias->Buffer()))
                {
                    Bias->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    params.pResponsivePixelMaskTexture = Bias->Buffer();
                }
            }
        }
        else
        {
            if (!Config::Instance()->DisableReactiveMask.value_or(true))
            {
                LOG_WARN("Bias mask not exist and its enabled in config, it may cause problems!!");
                Config::Instance()->DisableReactiveMask = true;
                State::Instance().changeBackend[_handle->Id] = true;
                return true;
            }
        }
    }

    _hasColor = params.pColorTexture != nullptr;
    _hasMV = params.pVelocityTexture != nullptr;
    _hasOutput = params.pOutputTexture != nullptr;
    _hasDepth = params.pDepthTexture != nullptr;
    _hasExposure = params.pExposureScaleTexture != nullptr;
    _accessToReactiveMask = paramReactiveMask != nullptr;

    float MVScaleX = 1.0f;
    float MVScaleY = 1.0f;

    if (InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &MVScaleX) == NVSDK_NGX_Result_Success &&
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &MVScaleY) == NVSDK_NGX_Result_Success)
    {
        xessResult = XeSSProxy::SetVelocityScale()(_xessContext, MVScaleX, MVScaleY);

        if (xessResult != XESS_RESULT_SUCCESS)
        {
            LOG_ERROR("xessSetVelocityScale: {0}", ResultToString(xessResult));
            return false;
        }
    }
    else
        LOG_WARN("Can't get motion vector scales!");

    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, &params.inputColorBase.x);
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, &params.inputColorBase.y);
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, &params.inputDepthBase.x);
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, &params.inputDepthBase.y);
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, &params.inputMotionVectorBase.x);
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, &params.inputMotionVectorBase.y);
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, &params.outputColorBase.x);
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y, &params.outputColorBase.y);
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_X,
                      &params.inputResponsiveMaskBase.x);
    InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_Y,
                      &params.inputResponsiveMaskBase.y);

    LOG_DEBUG("Executing!!");
    xessResult = XeSSProxy::D3D12Execute()(_xessContext, InCommandList, &params);

    if (xessResult != XESS_RESULT_SUCCESS)
    {
        LOG_ERROR("xessD3D12Execute error: {0}", ResultToString(xessResult));
        return false;
    }

    // Apply RCAS
    if (Config::Instance()->RcasEnabled.value_or(true) &&
        (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or(false) &&
                               Config::Instance()->MotionSharpness.value_or(0.4) > 0.0f)) &&
        RCAS->CanRender())
    {
        if (params.pOutputTexture != RCAS->Buffer())
            ResourceBarrier(InCommandList, params.pOutputTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        RCAS->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        RcasConstants rcasConstants {};

        rcasConstants.Sharpness = _sharpness;
        rcasConstants.DisplayWidth = TargetWidth();
        rcasConstants.DisplayHeight = TargetHeight();
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &rcasConstants.MvScaleX);
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &rcasConstants.MvScaleY);
        rcasConstants.DisplaySizeMV = !(GetFeatureFlags() & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes);
        rcasConstants.RenderHeight = RenderHeight();
        rcasConstants.RenderWidth = RenderWidth();

        if (useSS)
        {
            if (!RCAS->Dispatch(Device, InCommandList, params.pOutputTexture, params.pVelocityTexture, rcasConstants,
                                OutputScaler->Buffer()))
            {
                Config::Instance()->RcasEnabled = false;
                return true;
            }
        }
        else
        {
            if (!RCAS->Dispatch(Device, InCommandList, params.pOutputTexture, params.pVelocityTexture, rcasConstants,
                                paramOutput))
            {
                Config::Instance()->RcasEnabled = false;
                return true;
            }
        }
    }

    if (useSS)
    {
        LOG_DEBUG("scaling output...");
        OutputScaler->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        if (!OutputScaler->Dispatch(Device, InCommandList, OutputScaler->Buffer(), paramOutput))
        {
            Config::Instance()->OutputScalingEnabled = false;
            State::Instance().changeBackend[_handle->Id] = true;
            return true;
        }
    }

    // imgui
    if (!Config::Instance()->OverlayMenu.value_or(true) && _frameCount > 30)
    {
        if (Imgui != nullptr && Imgui.get() != nullptr)
        {
            if (Imgui->IsHandleDifferent())
            {
                Imgui.reset();
            }
            else
                Imgui->Render(InCommandList, paramOutput);
        }
        else
        {
            if (Imgui == nullptr || Imgui.get() == nullptr)
                Imgui = std::make_unique<Menu_Dx12>(GetForegroundWindow(), Device);
        }
    }

    // restore resource states
    if (params.pColorTexture && Config::Instance()->ColorResourceBarrier.has_value())
        ResourceBarrier(InCommandList, params.pColorTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        (D3D12_RESOURCE_STATES) Config::Instance()->ColorResourceBarrier.value());

    if (params.pVelocityTexture && Config::Instance()->MVResourceBarrier.has_value())
        ResourceBarrier(InCommandList, params.pVelocityTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        (D3D12_RESOURCE_STATES) Config::Instance()->MVResourceBarrier.value());

    if (paramOutput && Config::Instance()->OutputResourceBarrier.has_value())
        ResourceBarrier(InCommandList, paramOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        (D3D12_RESOURCE_STATES) Config::Instance()->OutputResourceBarrier.value());

    if (params.pDepthTexture && Config::Instance()->DepthResourceBarrier.has_value())
        ResourceBarrier(InCommandList, params.pDepthTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        (D3D12_RESOURCE_STATES) Config::Instance()->DepthResourceBarrier.value());

    if (params.pExposureScaleTexture && Config::Instance()->ExposureResourceBarrier.has_value())
        ResourceBarrier(InCommandList, params.pExposureScaleTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        (D3D12_RESOURCE_STATES) Config::Instance()->ExposureResourceBarrier.value());

    if (params.pResponsivePixelMaskTexture && Config::Instance()->MaskResourceBarrier.has_value())
        ResourceBarrier(InCommandList, params.pResponsivePixelMaskTexture,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        (D3D12_RESOURCE_STATES) Config::Instance()->MaskResourceBarrier.value());

    _frameCount++;

    return true;
}

XeSSFeatureDx12::~XeSSFeatureDx12() {}
