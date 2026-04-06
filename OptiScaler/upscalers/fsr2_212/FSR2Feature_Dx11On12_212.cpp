#include <pch.h>
#include <Config.h>
#include "FSR2Feature_Dx11On12_212.h"
#include "MathUtils.h"

using namespace OptiMath;

bool FSR2FeatureDx11on12_212::Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext,
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

bool FSR2FeatureDx11on12_212::Evaluate(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    auto& cfg = *Config::Instance();
    const auto& ngxParams = *InParameters;

    if (!_baseInit)
    {
        // to prevent creation dx12 device if we are going to recreate feature
        if (LowResMV())
        {
            ID3D11Resource* paramVelocity = nullptr;
            if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramVelocity) != NVSDK_NGX_Result_Success)
                InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity);
        }

        if (AutoExposure())
        {
            LOG_DEBUG("AutoExposure enabled!");
        }
        else
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
                Config::Instance()->DisableReactiveMask.set_volatile_value(true);
            }
        }

        if (!BaseInit(Device, InDeviceContext, InParameters))
        {
            LOG_DEBUG("FSR2FeatureDx11with12_212::Init BaseInit failed!");
            return false;
        }

        _baseInit = true;

        LOG_DEBUG("FSR2FeatureDx11with12_212::Init calling InitFSR2");

        if (_dx11on12Device == nullptr)
        {
            LOG_ERROR("FSR2FeatureDx11with12_212::Init Dx12on11Device is null!");
            return false;
        }

        if (!InitFSR2(InParameters))
        {
            LOG_ERROR("FSR2FeatureDx11with12_212::Init InitFSR2 fail!");
            return false;
        }

        if (!Config::Instance()->OverlayMenu.value_or_default() && (Imgui == nullptr || Imgui.get() == nullptr))
            Imgui = std::make_unique<Menu_Dx11>(GetForegroundWindow(), Device);

        if (Config::Instance()->Dx11DelayedInit.value_or_default())
        {
            LOG_TRACE("sleeping after FSRContext creation for 1500ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }

        OutputScaler = std::make_unique<OS_Dx12>("Output Scaling", _dx11on12Device, (TargetWidth() < DisplayWidth()));
        RCAS = std::make_unique<RCAS_Dx12>("RCAS", _dx11on12Device);
        Bias = std::make_unique<Bias_Dx12>("Bias", _dx11on12Device);
    }

    if (!IsInited())
        return false;

    if (!RCAS->IsInit())
        Config::Instance()->RcasEnabled.set_volatile_value(false);

    if (!OutputScaler->IsInit())
        Config::Instance()->OutputScalingEnabled.set_volatile_value(false);

    ID3D11DeviceContext4* dc;
    auto result = InDeviceContext->QueryInterface(IID_PPV_ARGS(&dc));

    if (result != S_OK)
    {
        LOG_ERROR("QueryInterface error: {0:x}", result);
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

    Fsr212::FfxFsr2DispatchDescription params {};

    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &params.jitterOffset.x);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &params.jitterOffset.y);

    if (Config::Instance()->OverrideSharpness.value_or_default())
        _sharpness = Config::Instance()->Sharpness.value_or_default();
    else
        _sharpness = GetSharpness(InParameters);

    if (Config::Instance()->RcasEnabled.value_or_default())
    {
        params.enableSharpening = false;
        params.sharpness = 0.0f;
    }
    else
    {
        if (_sharpness > 1.0f)
            _sharpness = 1.0f;

        params.enableSharpening = _sharpness > 0.0f;
        params.sharpness = _sharpness;
    }

    unsigned int reset;
    InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset);
    params.reset = (reset == 1);

    GetRenderResolution(InParameters, &params.renderSize.width, &params.renderSize.height);

    bool useSS =
        Config::Instance()->OutputScalingEnabled.value_or_default() && (LowResMV() || RenderWidth() == DisplayWidth());

    LOG_DEBUG("Input Resolution: {0}x{1}", params.renderSize.width, params.renderSize.height);

    auto frame = _frameCount % 2;
    auto cmdList = Dx12CommandList[frame];

    params.commandList = Fsr212::ffxGetCommandListDX12_212(cmdList);

    auto ffxresult = Fsr212::FFX_ERROR_INVALID_ALIGNMENT;

    uint8_t state = 0;

    do
    {
        if (!ProcessDx11Textures(InParameters))
        {
            LOG_ERROR("Can't process Dx11 textures!");
            break;
        }

        if (State::Instance().changeBackend[Handle()->Id])
        {
            break;
        }

        params.color = Fsr212::ffxGetResourceDX12_212(&_context, dx11Color.Dx12Resource, (wchar_t*) L"FSR2_Color",
                                                      Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
        params.motionVectors = Fsr212::ffxGetResourceDX12_212(&_context, dx11Mv.Dx12Resource, (wchar_t*) L"FSR2_Motion",
                                                              Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
        params.depth = Fsr212::ffxGetResourceDX12_212(&_context, dx11Depth.Dx12Resource, (wchar_t*) L"FSR2_Depth",
                                                      Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
        params.exposure = Fsr212::ffxGetResourceDX12_212(&_context, dx11Exp.Dx12Resource, (wchar_t*) L"FSR2_Exp",
                                                         Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);

        if (dx11Reactive.Dx12Resource != nullptr)
        {
            if (Config::Instance()->FsrUseMaskForTransparency.value_or_default())
                params.transparencyAndComposition = Fsr212::ffxGetResourceDX12_212(
                    &_context, dx11Reactive.Dx12Resource, (wchar_t*) L"FSR2_Transparency",
                    Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);

            if (Bias->IsInit() &&
                Bias->CreateBufferResource(_dx11on12Device, dx11Reactive.Dx12Resource,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
                Bias->CanRender())
            {
                state = 1;
                Bias->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                if (Config::Instance()->DlssReactiveMaskBias.value_or_default() > 0.0f &&
                    Bias->Dispatch(_dx11on12Device, cmdList, dx11Reactive.Dx12Resource,
                                   Config::Instance()->DlssReactiveMaskBias.value_or_default(), Bias->Buffer()))
                {
                    Bias->SetBufferState(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    params.reactive =
                        Fsr212::ffxGetResourceDX12_212(&_context, Bias->Buffer(), (wchar_t*) L"FSR2_Reactive",
                                                       Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
                }
            }
        }

        // OutputScaling
        if (useSS)
        {
            if (OutputScaler->CreateBufferResource(_dx11on12Device, dx11Out.Dx12Resource, TargetWidth(), TargetHeight(),
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            {
                state = 1;
                OutputScaler->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                params.output =
                    Fsr212::ffxGetResourceDX12_212(&_context, OutputScaler->Buffer(), (wchar_t*) L"FSR2_Out",
                                                   Fsr212::FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            else
                params.output = Fsr212::ffxGetResourceDX12_212(&_context, dx11Out.Dx12Resource, (wchar_t*) L"FSR2_Out",
                                                               Fsr212::FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        else
            params.output = Fsr212::ffxGetResourceDX12_212(&_context, dx11Out.Dx12Resource, (wchar_t*) L"FSR2_Out",
                                                           Fsr212::FFX_RESOURCE_STATE_UNORDERED_ACCESS);

        // RCAS
        if (Config::Instance()->RcasEnabled.value_or_default() &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                                   Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
            RCAS->IsInit() &&
            RCAS->CreateBufferResource(_dx11on12Device, (ID3D12Resource*) params.output.resource,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
        {
            state = 1;
            RCAS->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            params.output = Fsr212::ffxGetResourceDX12_212(&_context, RCAS->Buffer(), (wchar_t*) L"FSR2_Out",
                                                           Fsr212::FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        _hasColor = params.color.resource != nullptr;
        _hasDepth = params.depth.resource != nullptr;
        _hasMV = params.motionVectors.resource != nullptr;
        _hasExposure = params.exposure.resource != nullptr;
        _hasTM = params.transparencyAndComposition.resource != nullptr;
        _hasOutput = params.output.resource != nullptr;

        float MVScaleX = 1.0f;
        float MVScaleY = 1.0f;

        if (InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &MVScaleX) == NVSDK_NGX_Result_Success &&
            InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &MVScaleY) == NVSDK_NGX_Result_Success)
        {
            params.motionVectorScale.x = MVScaleX;
            params.motionVectorScale.y = MVScaleY;
        }
        else
        {
            LOG_WARN("Can't get motion vector scales!");

            params.motionVectorScale.x = MVScaleX;
            params.motionVectorScale.y = MVScaleY;
        }

        if (DepthInverted())
        {
            params.cameraFar = cfg.FsrCameraNear.value_or_default();
            params.cameraNear = cfg.FsrCameraFar.value_or_default();
        }
        else
        {
            params.cameraFar = cfg.FsrCameraFar.value_or_default();
            params.cameraNear = cfg.FsrCameraNear.value_or_default();
        }

        if (cfg.FsrVerticalFov.has_value())
            params.cameraFovAngleVertical = GetRadiansFromDeg(cfg.FsrVerticalFov.value());
        else if (cfg.FsrHorizontalFov.value_or_default() > 0.0f)
        {
            const float hFovRad = GetRadiansFromDeg(cfg.FsrHorizontalFov.value());
            params.cameraFovAngleVertical =
                GetVerticalFovFromHorizontal(hFovRad, (float) TargetWidth(), (float) TargetHeight());
        }
        else
            params.cameraFovAngleVertical = GetRadiansFromDeg(60);

        if (InParameters->Get(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, &params.frameTimeDelta) !=
                NVSDK_NGX_Result_Success ||
            params.frameTimeDelta < 1.0f)
            params.frameTimeDelta = (float) GetDeltaTime();

        if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &params.preExposure) != NVSDK_NGX_Result_Success)
            params.preExposure = 1.0f;

        LOG_DEBUG("Dispatch!!");
        ffxresult = Fsr212::ffxFsr2ContextDispatch212(&_context, &params);
        state = 1;

        if (ffxresult != Fsr212::FFX_OK)
        {
            LOG_ERROR("ffxFsr2ContextDispatch error: {0}", ResultToString212(ffxresult));
            break;
        }

        // apply rcas
        if (Config::Instance()->RcasEnabled.value_or_default() &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                                   Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
            RCAS->CanRender())
        {
            LOG_DEBUG("Apply RCAS");
            if (params.output.resource != RCAS->Buffer())
                ResourceBarrier(cmdList, (ID3D12Resource*) params.output.resource,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

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
            rcasConstants.DepthInverted = DepthInverted();

            if (useSS)
            {
                if (!RCAS->Dispatch(_dx11on12Device, cmdList, (ID3D12Resource*) params.output.resource,
                                    (ID3D12Resource*) params.motionVectors.resource,
                                    (ID3D12Resource*) params.depth.resource, rcasConstants, OutputScaler->Buffer()))
                {
                    Config::Instance()->RcasEnabled.set_volatile_value(false);
                    break;
                }
            }
            else
            {
                if (!RCAS->Dispatch(_dx11on12Device, cmdList, (ID3D12Resource*) params.output.resource,
                                    (ID3D12Resource*) params.motionVectors.resource,
                                    (ID3D12Resource*) params.depth.resource, rcasConstants, dx11Out.Dx12Resource))
                {
                    Config::Instance()->RcasEnabled.set_volatile_value(false);
                    break;
                }
            }
        }

        if (useSS)
        {
            LOG_DEBUG("scaling output...");
            OutputScaler->SetBufferState(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            if (!OutputScaler->Dispatch(_dx11on12Device, cmdList, OutputScaler->Buffer(), dx11Out.Dx12Resource))
            {
                Config::Instance()->OutputScalingEnabled.set_volatile_value(false);
                State::Instance().changeBackend[Handle()->Id] = true;

                break;
            }
        }

        state = 2;

    } while (false);

    if (state > 0)
    {
        cmdList->Close();
        ID3D12CommandList* ppCommandLists[] = { cmdList };
        Dx12CommandQueue->ExecuteCommandLists(1, ppCommandLists);
        Dx12CommandQueue->Signal(dx12FenceTextureCopy, _fenceValue);
    }

    auto evalResult = false;

    do
    {
        if (state != 2)
            break;

        if (ffxresult != Fsr212::FFX_OK)
            break;

        if (!CopyBackOutput())
        {
            LOG_ERROR("Can't copy output texture back!");
            break;
        }

        // imgui - legacy menu disabled for now
        // if (!Config::Instance()->OverlayMenu.value_or_default() && _frameCount > 30)
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

FSR2FeatureDx11on12_212::~FSR2FeatureDx11on12_212() {}

bool FSR2FeatureDx11on12_212::InitFSR2(const NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    if (Device == nullptr)
    {
        LOG_ERROR("D3D12Device is null!");
        return false;
    }

    {
        ScopedSkipSpoofing skipSpoofing {};

        const size_t scratchBufferSize = Fsr212::ffxFsr2GetScratchMemorySizeDX12_212();
        void* scratchBuffer = calloc(scratchBufferSize, 1);

        auto errorCode = Fsr212::ffxFsr2GetInterfaceDX12_212(&_contextDesc.callbacks, _dx11on12Device, scratchBuffer,
                                                             scratchBufferSize);

        if (errorCode != Fsr212::FFX_OK)
        {
            LOG_ERROR("ffxGetInterfaceDX12 error: {0}", ResultToString212(errorCode));
            free(scratchBuffer);
            return false;
        }

        _contextDesc.device = Fsr212::ffxGetDeviceDX12_212(_dx11on12Device);
        _contextDesc.flags = 0;

        if (DepthInverted())
            _contextDesc.flags |= Fsr212::FFX_FSR2_ENABLE_DEPTH_INVERTED;

        if (AutoExposure())
            _contextDesc.flags |= Fsr212::FFX_FSR2_ENABLE_AUTO_EXPOSURE;

        if (IsHdr())
            _contextDesc.flags |= Fsr212::FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;

        if (JitteredMV())
            _contextDesc.flags |= Fsr212::FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

        if (!LowResMV())
            _contextDesc.flags |= Fsr212::FFX_FSR2_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;

        if (Config::Instance()->OutputScalingEnabled.value_or_default() &&
            (LowResMV() || RenderWidth() == DisplayWidth()))
        {
            float ssMulti = Config::Instance()->OutputScalingMultiplier.value_or_default();

            if (ssMulti < 0.5f)
            {
                ssMulti = 0.5f;
                Config::Instance()->OutputScalingMultiplier.set_volatile_value(ssMulti);
            }
            else if (ssMulti > 3.0f)
            {
                ssMulti = 3.0f;
                Config::Instance()->OutputScalingMultiplier.set_volatile_value(ssMulti);
            }

            _targetWidth = static_cast<unsigned int>(DisplayWidth() * ssMulti);
            _targetHeight = static_cast<unsigned int>(DisplayHeight() * ssMulti);
        }
        else
        {
            _targetWidth = DisplayWidth();
            _targetHeight = DisplayHeight();
        }

        // extended limits changes how resolution
        if (Config::Instance()->ExtendedLimits.value_or_default() && RenderWidth() > DisplayWidth())
        {
            _contextDesc.maxRenderSize.width = RenderWidth();
            _contextDesc.maxRenderSize.height = RenderHeight();

            Config::Instance()->OutputScalingMultiplier.set_volatile_value(1.0f);

            // if output scaling active let it to handle downsampling
            if (Config::Instance()->OutputScalingEnabled.value_or_default() &&
                (LowResMV() || RenderWidth() == DisplayWidth()))
            {
                _contextDesc.displaySize.width = _contextDesc.maxRenderSize.width;
                _contextDesc.displaySize.height = _contextDesc.maxRenderSize.height;

                // update target res
                _targetWidth = _contextDesc.maxRenderSize.width;
                _targetHeight = _contextDesc.maxRenderSize.height;
            }
            else
            {
                _contextDesc.displaySize.width = DisplayWidth();
                _contextDesc.displaySize.height = DisplayHeight();
            }
        }
        else
        {
            _contextDesc.maxRenderSize.width = TargetWidth() > DisplayWidth() ? TargetWidth() : DisplayWidth();
            _contextDesc.maxRenderSize.height = TargetHeight() > DisplayHeight() ? TargetHeight() : DisplayHeight();
            _contextDesc.displaySize.width = TargetWidth();
            _contextDesc.displaySize.height = TargetHeight();
        }

        LOG_DEBUG("ffxFsr2ContextCreate!");

        ScopedSkipHeapCapture skipHeapCapture {};
        auto ret = Fsr212::ffxFsr2ContextCreate212(&_context, &_contextDesc);

        if (ret != Fsr212::FFX_OK)
        {
            LOG_ERROR("ffxFsr2ContextCreate error: {0}", ResultToString212(ret));
            return false;
        }

        LOG_TRACE("sleeping after ffxFsr2ContextCreate creation for 500ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    SetInit(true);

    return true;
}
