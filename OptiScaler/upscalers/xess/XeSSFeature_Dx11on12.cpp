#pragma once
#include <pch.h>
#include <Config.h>

#include "XeSSFeature_Dx11on12.h"

#if _DEBUG
#include <include/d3dx/D3DX11tex.h>
#endif

XeSSFeatureDx11on12::XeSSFeatureDx11on12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature_Dx11wDx12(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters),
      IFeature(InHandleId, InParameters), XeSSFeature(InHandleId, InParameters)
{
    _moduleLoaded = XeSSProxy::InitXeSS() && XeSSProxy::D3D12CreateContext() != nullptr;
}

bool XeSSFeatureDx11on12::Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext,
                               NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    Device = InDevice;
    DeviceContext = InContext;

    _baseInit = false;

    return true;
}

bool XeSSFeatureDx11on12::Evaluate(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!_baseInit)
    {
        // to prevent creation dx12 device if we are going to recreate feature
        ID3D11Resource* paramVelocity = nullptr;
        if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramVelocity) != NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity);

        if (!AutoExposure())
        {
            ID3D11Resource* paramExpo = nullptr;
            if (InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, &paramExpo) != NVSDK_NGX_Result_Success)
                InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, (void**) &paramExpo);

            if (paramExpo == nullptr)
            {
                LOG_WARN("ExposureTexture is not exist, enabling AutoExposure!!");
                State::Instance().AutoExposure = true;
            }
        }

        ID3D11Resource* paramReactiveMask = nullptr;
        if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &paramReactiveMask) !=
            NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactiveMask);
        _accessToReactiveMask = paramReactiveMask != nullptr;

        if (!Config::Instance()->DisableReactiveMask.has_value())
        {
            if (!paramReactiveMask)
            {
                LOG_WARN("Bias mask not exist, enabling DisableReactiveMask!!");
                Config::Instance()->DisableReactiveMask = true;
            }
        }

        if (!BaseInit(Device, InDeviceContext, InParameters))
        {
            LOG_ERROR("BaseInit failed!");
            return false;
        }

        _baseInit = true;

        if (Dx12Device == nullptr)
        {
            LOG_ERROR("Dx12on11Device is null!");
            return false;
        }

        LOG_DEBUG("calling InitXeSS");

        if (!InitXeSS(Dx12Device, InParameters))
        {
            LOG_ERROR("InitXeSS fail!");
            return false;
        }

        if (!Config::Instance()->OverlayMenu.value_or(true) && (Imgui == nullptr || Imgui.get() == nullptr))
        {
            LOG_DEBUG("Create Imgui!");
            Imgui = std::make_unique<Menu_Dx11>(GetForegroundWindow(), Device);
        }

        if (Config::Instance()->Dx11DelayedInit.value_or(false))
        {
            LOG_TRACE("sleeping after XeSSContext creation for 1500ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }

        OutputScaler = std::make_unique<OS_Dx12>("Output Scaling", Dx12Device, (TargetWidth() < DisplayWidth()));
        RCAS = std::make_unique<RCAS_Dx12>("RCAS", Dx12Device);
        Bias = std::make_unique<Bias_Dx12>("Bias", Dx12Device);
    }

    if (!IsInited() || !_xessContext || !ModuleLoaded())
    {
        LOG_ERROR("Not inited!");
        return false;
    }

    if (!RCAS->IsInit())
        Config::Instance()->RcasEnabled = false;

    if (!OutputScaler->IsInit())
        Config::Instance()->OutputScalingEnabled = false;

    ID3D11DeviceContext4* dc;
    auto result = InDeviceContext->QueryInterface(IID_PPV_ARGS(&dc));

    if (result != S_OK)
    {
        LOG_ERROR("CreateFence d3d12fence error: {0:x}", result);
        return false;
    }

    if (dc != Dx11DeviceContext)
    {
        LOG_WARN("Dx11DeviceContext changed!");
        ReleaseSharedResources();
        Dx11DeviceContext = dc;
    }

    if (dc != nullptr)
        dc->Release();

    if (State::Instance().xessDebug)
    {
        LOG_ERROR("xessDebug");

        xess_dump_parameters_t dumpParams {};
        dumpParams.frame_count = State::Instance().xessDebugFrames;
        dumpParams.frame_idx = dumpCount;
        dumpParams.path = ".";
        dumpParams.dump_elements_mask = XESS_DUMP_INPUT_COLOR | XESS_DUMP_INPUT_VELOCITY | XESS_DUMP_INPUT_DEPTH |
                                        XESS_DUMP_OUTPUT | XESS_DUMP_EXECUTION_PARAMETERS | XESS_DUMP_HISTORY |
                                        XESS_DUMP_INPUT_RESPONSIVE_PIXEL_MASK;

        if (!Config::Instance()->DisableReactiveMask.value_or(true))
            dumpParams.dump_elements_mask |= XESS_DUMP_INPUT_RESPONSIVE_PIXEL_MASK;

        XeSSProxy::StartDump()(_xessContext, &dumpParams);
        State::Instance().xessDebug = false;
        dumpCount += State::Instance().xessDebugFrames;
    }

    // creatimg params for XeSS
    xess_result_t xessResult;
    xess_d3d12_execute_params_t params {};

    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &params.jitterOffsetX);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &params.jitterOffsetY);

    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, &params.exposureScale) != NVSDK_NGX_Result_Success)
        params.exposureScale = 1.0f;

    InParameters->Get(NVSDK_NGX_Parameter_Reset, &params.resetHistory);

    GetRenderResolution(InParameters, &params.inputWidth, &params.inputHeight);

    _sharpness = GetSharpness(InParameters);

    bool useSS = Config::Instance()->OutputScalingEnabled.value_or(false) && LowResMV();

    LOG_DEBUG("Input Resolution: {0}x{1}", params.inputWidth, params.inputHeight);

    auto frame = _frameCount % 2;
    auto cmdList = Dx12CommandList[frame];

    do
    {
        if (!ProcessDx11Textures(InParameters))
        {
            LOG_ERROR("Can't process Dx11 textures!");
            break;
        }

        if (State::Instance().changeBackend[_handle->Id])
        {
            break;
        }

        params.pColorTexture = dx11Color.Dx12Resource;
        _hasColor = params.pColorTexture != nullptr;
        params.pVelocityTexture = dx11Mv.Dx12Resource;
        _hasMV = params.pVelocityTexture != nullptr;

        if (useSS)
        {
            if (OutputScaler->CreateBufferResource(Dx12Device, dx11Out.Dx12Resource, TargetWidth(), TargetHeight(),
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            {
                OutputScaler->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                params.pOutputTexture = OutputScaler->Buffer();
            }
            else
                params.pOutputTexture = dx11Out.Dx12Resource;
        }
        else
        {
            params.pOutputTexture = dx11Out.Dx12Resource;
        }

        // RCAS
        if (Config::Instance()->RcasEnabled.value_or(true) &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or(false) &&
                                   Config::Instance()->MotionSharpness.value_or(0.4) > 0.0f)) &&
            RCAS->IsInit() &&
            RCAS->CreateBufferResource(Dx12Device, params.pOutputTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
        {
            RCAS->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            params.pOutputTexture = RCAS->Buffer();
        }

        _hasOutput = params.pOutputTexture != nullptr;
        params.pDepthTexture = dx11Depth.Dx12Resource;
        _hasDepth = params.pDepthTexture != nullptr;
        params.pExposureScaleTexture = dx11Exp.Dx12Resource;
        _hasExposure = params.pExposureScaleTexture != nullptr;

        if (dx11Reactive.Dx12Resource != nullptr)
        {
            if (Config::Instance()->DlssReactiveMaskBias.value_or(0.0f) > 0.0f && Bias->IsInit() &&
                Bias->CreateBufferResource(Dx12Device, dx11Reactive.Dx12Resource,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
                Bias->CanRender())
            {
                Bias->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                if (Bias->Dispatch(Dx12Device, cmdList, dx11Reactive.Dx12Resource,
                                   Config::Instance()->DlssReactiveMaskBias.value_or(0.0f), Bias->Buffer()))
                {
                    Bias->SetBufferState(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    params.pResponsivePixelMaskTexture = Bias->Buffer();
                }
            }
        }

        float MVScaleX;
        float MVScaleY;

        if (InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &MVScaleX) == NVSDK_NGX_Result_Success &&
            InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &MVScaleY) == NVSDK_NGX_Result_Success)
        {
            xessResult = XeSSProxy::SetVelocityScale()(_xessContext, MVScaleX, MVScaleY);

            if (xessResult != XESS_RESULT_SUCCESS)
            {
                LOG_ERROR("xessSetVelocityScale error: {0}", ResultToString(xessResult));
                break;
            }
        }
        else
        {
            LOG_WARN("Can't get motion vector scales!");
        }

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

        // Execute xess
        LOG_DEBUG("Executing!!");
        xessResult = XeSSProxy::D3D12Execute()(_xessContext, cmdList, &params);

        if (xessResult != XESS_RESULT_SUCCESS)
        {
            LOG_ERROR("xessD3D12Execute error: {0}", ResultToString(xessResult));
            break;
        }

        // apply rcas
        if (Config::Instance()->RcasEnabled.value_or(true) &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or(false) &&
                                   Config::Instance()->MotionSharpness.value_or(0.4) > 0.0f)) &&
            RCAS->CanRender())
        {
            LOG_DEBUG("Apply RCAS");

            if (params.pOutputTexture != RCAS->Buffer())
                ResourceBarrier(cmdList, params.pOutputTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            RCAS->SetBufferState(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

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
                if (!RCAS->Dispatch(Dx12Device, cmdList, params.pOutputTexture, params.pVelocityTexture, rcasConstants,
                                    OutputScaler->Buffer()))
                {
                    Config::Instance()->RcasEnabled = false;
                    break;
                }
            }
            else
            {
                if (!RCAS->Dispatch(Dx12Device, cmdList, params.pOutputTexture, params.pVelocityTexture, rcasConstants,
                                    dx11Out.Dx12Resource))
                {
                    Config::Instance()->RcasEnabled = false;
                    break;
                }
            }
        }

        if (useSS)
        {
            LOG_DEBUG("scaling output...");
            OutputScaler->SetBufferState(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            if (!OutputScaler->Dispatch(Dx12Device, cmdList, OutputScaler->Buffer(), dx11Out.Dx12Resource))
            {
                Config::Instance()->OutputScalingEnabled = false;
                State::Instance().changeBackend[_handle->Id] = true;

                break;
            }
        }

    } while (false);

    cmdList->Close();
    ID3D12CommandList* ppCommandLists[] = { cmdList };
    Dx12CommandQueue->ExecuteCommandLists(1, ppCommandLists);
    Dx12CommandQueue->Signal(dx12FenceTextureCopy, _fenceValue);

    auto evalResult = false;

    do
    {
        if (xessResult != XESS_RESULT_SUCCESS)
            break;

        if (!CopyBackOutput())
        {
            LOG_ERROR("Can't copy output texture back!");
            break;
        }

        // imgui - legacy menu disabled for now
        // if (!Config::Instance()->OverlayMenu.value_or(true) && _frameCount > 30)
        //{
        //    if (Imgui != nullptr && Imgui.get() != nullptr)
        //    {
        //        LOG_DEBUG("Apply Imgui");

        //        if (Imgui->IsHandleDifferent())
        //        {
        //            LOG_DEBUG("Imgui handle different, reset()!");
        //            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        //            Imgui.reset();
        //        }
        //        else
        //            Imgui->Render(InDeviceContext, paramOutput[_frameCount % 2]);
        //    }
        //    else
        //    {
        //        if (Imgui == nullptr || Imgui.get() == nullptr)
        //            Imgui = std::make_unique<Menu_Dx11>(GetForegroundWindow(), Device);
        //    }
        //}

        evalResult = true;

    } while (false);

    _frameCount++;
    Dx12CommandQueue->Signal(Dx12Fence, _frameCount);

    return evalResult;
}

XeSSFeatureDx11on12::~XeSSFeatureDx11on12() {}

// TODO
bool XeSSFeatureDx11on12::CheckInitializationContext(ApiContext* context) { return false; }

// TODO
xess_result_t XeSSFeatureDx11on12::CreateXessContext(ApiContext* context, xess_context_handle_t* pXessContext)
{
    return xess_result_t();
}

// TODO
xess_result_t XeSSFeatureDx11on12::ApiInit(ApiContext* context, XessInitParams* xessInitParams)
{
    return xess_result_t();
}

XessInitParams XeSSFeatureDx11on12::CreateInitParams(xess_2d_t outputResolution, xess_quality_settings_t qualitySetting,
                                                     uint32_t initFlags)
{
    return XessInitParams();
}

void XeSSFeatureDx11on12::InitMenuAndOutput(ApiContext* context) {}
