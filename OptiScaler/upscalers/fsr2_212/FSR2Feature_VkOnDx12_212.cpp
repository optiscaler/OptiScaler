#include <pch.h>
#include <Config.h>

#include "FSR2Feature_VkOnDx12_212.h"

#include "nvsdk_ngx_vk.h"

bool FSR2FeatureVkOnDx12_212::InitFSR2(const NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    if (_dx11on12Device == nullptr)
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
    }

    SetInit(true);

    return true;
}

bool FSR2FeatureVkOnDx12_212::Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                                   VkCommandBuffer InCmdList, PFN_vkGetInstanceProcAddr InGIPA,
                                   PFN_vkGetDeviceProcAddr InGDPA, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    VulkanInstance = InInstance;
    VulkanPhysicalDevice = InPD;
    VulkanDevice = InDevice;
    VulkanGIPA = InGIPA;
    VulkanGDPA = InGDPA;

    return true;
}

bool FSR2FeatureVkOnDx12_212::Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!_baseInit)
    {
        // Check for motion vectors parameter - use void** since we're checking existence, not using the resource
        void* paramVelocity = nullptr;
        if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramVelocity) != NVSDK_NGX_Result_Success)
        {
            LOG_WARN("MotionVectors parameter not found!");
        }

        // Check auto exposure
        if (AutoExposure())
        {
            LOG_DEBUG("AutoExposure enabled!");
        }
        else
        {
            void* paramExpo = nullptr;
            if (InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, &paramExpo) != NVSDK_NGX_Result_Success)
            {
                LOG_WARN("ExposureTexture does not exist, enabling AutoExposure!!");
                State::Instance().AutoExposure = true;
            }
        }

        // Check reactive mask
        void* paramReactiveMask = nullptr;
        if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &paramReactiveMask) !=
            NVSDK_NGX_Result_Success)
        {
            LOG_DEBUG("Reactive mask not found");
        }
        _accessToReactiveMask = paramReactiveMask != nullptr;

        if (!Config::Instance()->DisableReactiveMask.has_value())
        {
            if (!paramReactiveMask)
            {
                LOG_WARN("Bias mask does not exist, enabling DisableReactiveMask!!");
                Config::Instance()->DisableReactiveMask.set_volatile_value(true);
            }
        }

        // Initialize base Vulkan-D3D12 interop using stored Vulkan context
        if (!BaseInit(VulkanInstance, VulkanPhysicalDevice, VulkanDevice, InCmdBuffer, VulkanGIPA, VulkanGDPA,
                      InParameters))
        {
            LOG_ERROR("BaseInit failed!");
            return false;
        }

        _baseInit = true;

        LOG_DEBUG("calling InitFSR2");

        if (_dx11on12Device == nullptr)
        {
            LOG_ERROR("D3D12 device is null!");
            return false;
        }

        if (!InitFSR2(InParameters))
        {
            LOG_ERROR("InitFSR2 failed!");
            return false;
        }

        // Create D3D12 post-processing shaders
        OutputScaler = std::make_unique<OS_Dx12>("Output Scaling", _dx11on12Device, (TargetWidth() < DisplayWidth()));
        RCAS = std::make_unique<RCAS_Dx12>("RCAS", _dx11on12Device);
        Bias = std::make_unique<Bias_Dx12>("Bias", _dx11on12Device);

        if (Config::Instance()->Dx11DelayedInit.value_or_default())
        {
            LOG_TRACE("sleeping after FSRContext creation for 1500ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
    }

    if (!IsInited())
        return false;

    if (!RCAS->IsInit())
        Config::Instance()->RcasEnabled.set_volatile_value(false);

    if (!OutputScaler->IsInit())
        Config::Instance()->OutputScalingEnabled.set_volatile_value(false);

    // Set up dispatch parameters
    Fsr212::FfxFsr2DispatchDescription params = {};

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

    // Force enable RCAS when in FSR4 debug view mode
    if (Version() >= feature_version { 4, 0, 2 } && Config::Instance()->FsrDebugView.value_or_default() &&
        Config::Instance()->Fsr4EnableDebugView.value_or_default() && !params.enableSharpening)
    {
        params.enableSharpening = true;
        params.sharpness = 0.01f;
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

    params.commandList = cmdList;

    Fsr212::FfxErrorCode ffxresult = Fsr212::FFX_ERROR_NULL_DEVICE;

    uint8_t state = 0;

    do
    {
        // Process Vulkan textures and copy to D3D12
        if (!ProcessVulkanTextures(InCmdBuffer, InParameters))
        {
            LOG_ERROR("Can't process Vulkan textures!");
            break;
        }

        if (State::Instance().changeBackend[Handle()->Id])
        {
            break;
        }

        // Set up FSR3 input resources from shared D3D12 resources

        params.color = Fsr212::ffxGetResourceDX12_212(&_context, vkColor.Dx12Resource, (wchar_t*) L"FSR2_Color",
                                                      Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
        params.motionVectors = Fsr212::ffxGetResourceDX12_212(
            &_context, vkMv.Dx12Resource, (wchar_t*) L"FSR2_MotionVectors", Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
        params.depth = Fsr212::ffxGetResourceDX12_212(&_context, vkDepth.Dx12Resource, (wchar_t*) L"FSR2_Depth",
                                                      Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
        params.output = Fsr212::ffxGetResourceDX12_212(&_context, vkOut.Dx12Resource, (wchar_t*) L"FSR2_Output",
                                                       Fsr212::FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        params.exposure = Fsr212::ffxGetResourceDX12_212(&_context, vkExp.Dx12Resource, (wchar_t*) L"FSR2_Exposure",
                                                         Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);

        // Handle reactive mask
        if (vkReactive.Dx12Resource != nullptr)
        {
            if (Config::Instance()->FsrUseMaskForTransparency.value_or_default())
                params.transparencyAndComposition =
                    Fsr212::ffxGetResourceDX12_212(&_context, vkReactive.Dx12Resource, (wchar_t*) L"FSR2_Reactive",
                                                   Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);

            if (Config::Instance()->DlssReactiveMaskBias.value_or_default() > 0.0f && Bias->IsInit() &&
                Bias->CreateBufferResource(_dx11on12Device, vkReactive.Dx12Resource,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
                Bias->CanRender())
            {
                state = 1;
                Bias->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                if (Bias->Dispatch(_dx11on12Device, cmdList, vkReactive.Dx12Resource,
                                   Config::Instance()->DlssReactiveMaskBias.value_or_default(), Bias->Buffer()))
                {
                    Bias->SetBufferState(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    params.reactive =
                        Fsr212::ffxGetResourceDX12_212(&_context, Bias->Buffer(), (wchar_t*) L"FSR2_Reactive",
                                                       Fsr212::FFX_RESOURCE_STATE_COMPUTE_READ);
                }
            }
        }

        // Output Scaling
        if (useSS)
        {
            if (OutputScaler->CreateBufferResource(_dx11on12Device, vkOut.Dx12Resource, TargetWidth(), TargetHeight(),
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            {
                state = 1;

                OutputScaler->SetBufferState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                params.output =
                    Fsr212::ffxGetResourceDX12_212(&_context, OutputScaler->Buffer(), (wchar_t*) L"FSR2_Output",
                                                   Fsr212::FFX_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            else
                params.output = Fsr212::ffxGetResourceDX12_212(&_context, vkOut.Dx12Resource, (wchar_t*) L"FSR2_Output",
                                                               Fsr212::FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        else
            params.output = Fsr212::ffxGetResourceDX12_212(&_context, vkOut.Dx12Resource, (wchar_t*) L"FSR2_Output",
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
            params.output = Fsr212::ffxGetResourceDX12_212(&_context, RCAS->Buffer(), (wchar_t*) L"FSR2_Output",
                                                           Fsr212::FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        _hasColor = params.color.resource != nullptr;
        _hasDepth = params.depth.resource != nullptr;
        _hasMV = params.motionVectors.resource != nullptr;
        _hasExposure = params.exposure.resource != nullptr;
        _hasTM = params.transparencyAndComposition.resource != nullptr;
        _hasOutput = params.output.resource != nullptr;

        // For FSR 4 - resolve typeless formats
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

        if (Config::Instance()->FsrCameraNear.has_value() ||
            !Config::Instance()->FsrUseFsrInputValues.value_or_default() ||
            InParameters->Get("FSR.cameraNear", &params.cameraNear) != NVSDK_NGX_Result_Success)
        {
            if (DepthInverted())
                params.cameraFar = Config::Instance()->FsrCameraNear.value_or_default();
            else
                params.cameraNear = Config::Instance()->FsrCameraNear.value_or_default();
        }

        if (!Config::Instance()->FsrUseFsrInputValues.value_or_default() ||
            InParameters->Get("FSR.cameraFar", &params.cameraFar) != NVSDK_NGX_Result_Success)
        {
            if (DepthInverted())
                params.cameraNear = Config::Instance()->FsrCameraFar.value_or_default();
            else
                params.cameraFar = Config::Instance()->FsrCameraFar.value_or_default();
        }

        if (InParameters->Get("FSR.cameraFovAngleVertical", &params.cameraFovAngleVertical) != NVSDK_NGX_Result_Success)
        {
            if (Config::Instance()->FsrVerticalFov.has_value())
                params.cameraFovAngleVertical = Config::Instance()->FsrVerticalFov.value() * 0.0174532925199433f;
            else if (Config::Instance()->FsrHorizontalFov.value_or_default() > 0.0f)
                params.cameraFovAngleVertical =
                    2.0f * atan((tan(Config::Instance()->FsrHorizontalFov.value() * 0.0174532925199433f) * 0.5f) /
                                (float) TargetHeight() * (float) TargetWidth());
            else
                params.cameraFovAngleVertical = 1.0471975511966f;
        }

        if (!Config::Instance()->FsrUseFsrInputValues.value_or_default() ||
            InParameters->Get("FSR.frameTimeDelta", &params.frameTimeDelta) != NVSDK_NGX_Result_Success)
        {
            if (InParameters->Get(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, &params.frameTimeDelta) !=
                    NVSDK_NGX_Result_Success ||
                params.frameTimeDelta < 1.0f)
                params.frameTimeDelta = (float) GetDeltaTime();
        }

        LOG_DEBUG("FrameTimeDeltaInMsec: {0}", params.frameTimeDelta);
        State::Instance().lastFsrCameraFar = params.cameraFar;
        State::Instance().lastFsrCameraNear = params.cameraNear;

        if (Config::Instance()->FsrVerticalFov.has_value())
            params.cameraFovAngleVertical = Config::Instance()->FsrVerticalFov.value() * 0.0174532925199433f;
        else if (Config::Instance()->FsrHorizontalFov.value_or_default() > 0.0f)
            params.cameraFovAngleVertical =
                2.0f * atan((tan(Config::Instance()->FsrHorizontalFov.value() * 0.0174532925199433f) * 0.5f) /
                            (float) TargetHeight() * (float) TargetWidth());
        else
            params.cameraFovAngleVertical = 1.0471975511966f;

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

        // Apply RCAS
        if (Config::Instance()->RcasEnabled.value_or_default() &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                                   Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
            RCAS->CanRender())
        {
            LOG_DEBUG("Apply CAS");
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
                                    (ID3D12Resource*) params.depth.resource, rcasConstants, vkOut.Dx12Resource))
                {
                    Config::Instance()->RcasEnabled.set_volatile_value(false);
                    break;
                }
            }
        }

        if (useSS)
        {
            LOG_DEBUG("downscaling output...");
            OutputScaler->SetBufferState(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            if (!OutputScaler->Dispatch(_dx11on12Device, cmdList, OutputScaler->Buffer(), vkOut.Dx12Resource))
            {
                Config::Instance()->OutputScalingEnabled.set_volatile_value(false);
                State::Instance().changeBackend[Handle()->Id] = true;

                break;
            }
        }

        state = 2;

    } while (false);

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

        evalResult = true;

    } while (false);

    _frameCount++;

    return evalResult;
}

NVSDK_NGX_Parameter* FSR2FeatureVkOnDx12_212::SetParameters(NVSDK_NGX_Parameter* InParameters)
{
    InParameters->Set("OptiScaler.SupportsUpscaleSize", false);
    return InParameters;
}
