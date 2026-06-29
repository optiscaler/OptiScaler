#include <pch.h>

#include "FSR31Feature_VkOn12.h"

#include <Config.h>
#include <Util.h>

#include <proxies/FfxApi_Proxy.h>

NVSDK_NGX_Parameter* FSR31FeatureVkOn12::SetParameters(NVSDK_NGX_Parameter* InParameters)
{
    InParameters->Set("OptiScaler.SupportsUpscaleSize", true);
    return InParameters;
}

FSR31FeatureVkOn12::FSR31FeatureVkOn12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : FSR31Feature(InHandleId, InParameters), IFeature_VkwDx12(InHandleId, InParameters),
      IFeature_Vk(InHandleId, InParameters), IFeature(InHandleId, SetParameters(InParameters))
{
    FfxApiProxy::InitFfxDx12();

    _moduleLoaded = FfxApiProxy::IsSRReady();

    if (_moduleLoaded)
        LOG_INFO("amd_fidelityfx_dx12.dll methods loaded!");
    else
        LOG_ERROR("can't load amd_fidelityfx_dx12.dll methods!");
}

bool FSR31FeatureVkOn12::Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice,
                              VkCommandBuffer InCmdList, PFN_vkGetInstanceProcAddr InGIPA,
                              PFN_vkGetDeviceProcAddr InGDPA, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    // Store Vulkan context for later use
    VulkanInstance = InInstance;
    VulkanPhysicalDevice = InPD;
    VulkanDevice = InDevice;
    VulkanGIPA = InGIPA;
    VulkanGDPA = InGDPA;

    _baseInit = false;
    return _moduleLoaded;
}

bool FSR31FeatureVkOn12::Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
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

        LOG_DEBUG("calling InitFSR3");

        if (_dx11on12Device == nullptr)
        {
            LOG_ERROR("D3D12 device is null!");
            return false;
        }

        if (!InitFSR3(InParameters))
        {
            LOG_ERROR("InitFSR3 failed!");
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

    if (Config::Instance()->DADepthIsLinear.value_for_config_ignore_default() == std::nullopt)
        Config::Instance()->DADepthIsLinear.set_volatile_value(false);

    // Set up dispatch parameters
    struct ffxDispatchDescUpscale params = { 0 };
    params.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;

    params.flags = 0;

    if (Config::Instance()->FsrDebugView.value_or_default() &&
        (Version() < feature_version { 4, 0, 0 } || Config::Instance()->Fsr4EnableDebugView.value_or_default()))
    {
        params.flags |= FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW;
    }

    if (Config::Instance()->FsrNonLinearPQ.value_or_default())
        params.flags |= FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ;
    else if (Config::Instance()->FsrNonLinearSRGB.value_or_default())
        params.flags |= FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;

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

    ffxReturnCode_t ffxresult = FFX_API_RETURN_ERROR;

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
        params.color = ffxApiGetResourceDX12(vkColor.Dx12Resource, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        params.motionVectors = ffxApiGetResourceDX12(vkMv.Dx12Resource, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        params.depth = ffxApiGetResourceDX12(vkDepth.Dx12Resource, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        params.output = ffxApiGetResourceDX12(vkOut.Dx12Resource, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        params.exposure = ffxApiGetResourceDX12(vkExp.Dx12Resource, FFX_API_RESOURCE_STATE_COMPUTE_READ);

        // Handle reactive mask
        if (vkReactive.Dx12Resource != nullptr)
        {
            if (Config::Instance()->FsrUseMaskForTransparency.value_or_default())
                params.transparencyAndComposition =
                    ffxApiGetResourceDX12(vkReactive.Dx12Resource, FFX_API_RESOURCE_STATE_COMPUTE_READ);

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
                    params.reactive = ffxApiGetResourceDX12(Bias->Buffer(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
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
                params.output = ffxApiGetResourceDX12(OutputScaler->Buffer(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            else
                params.output = ffxApiGetResourceDX12(vkOut.Dx12Resource, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        else
            params.output = ffxApiGetResourceDX12(vkOut.Dx12Resource, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

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
            params.output = ffxApiGetResourceDX12(RCAS->Buffer(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        _hasColor = params.color.resource != nullptr;
        _hasDepth = params.depth.resource != nullptr;
        _hasMV = params.motionVectors.resource != nullptr;
        _hasExposure = params.exposure.resource != nullptr;
        _hasTM = params.transparencyAndComposition.resource != nullptr;
        _hasOutput = params.output.resource != nullptr;

        // For FSR 4 as it seems to be missing some conversions from typeless
        // transparencyAndComposition and exposure might be unnecessary here
        if (Version().major >= 4)
        {
            params.color.description.format = ffxResolveTypelessFormat(params.color.description.format);
            params.depth.description.format = ffxResolveTypelessFormat(params.depth.description.format);
            params.motionVectors.description.format = ffxResolveTypelessFormat(params.motionVectors.description.format);
            params.exposure.description.format = ffxResolveTypelessFormat(params.exposure.description.format);
            params.transparencyAndComposition.description.format =
                ffxResolveTypelessFormat(params.transparencyAndComposition.description.format);
            params.output.description.format = ffxResolveTypelessFormat(params.output.description.format);
        }

        params.motionVectorScale.x = 1.0f;
        params.motionVectorScale.y = 1.0f;

        if (InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &params.motionVectorScale.x) !=
                NVSDK_NGX_Result_Success ||
            InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &params.motionVectorScale.y) != NVSDK_NGX_Result_Success)
        {
            LOG_WARN("Can't get motion vector scales!");
        }

        if (DepthInverted())
        {
            params.cameraFar = Config::Instance()->FsrCameraNear.value_or_default();
            params.cameraNear = Config::Instance()->FsrCameraFar.value_or_default();
        }
        else
        {
            params.cameraFar = Config::Instance()->FsrCameraFar.value_or_default();
            params.cameraNear = Config::Instance()->FsrCameraNear.value_or_default();
        }

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

        params.viewSpaceToMetersFactor = 1.0f;

        // Version 3.1.1 check - dynamic configuration
        if (Version() >= feature_version { 3, 1, 1 } && _velocity != Config::Instance()->FsrVelocity.value_or_default())
        {
            _velocity = Config::Instance()->FsrVelocity.value_or_default();
            ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
            m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
            m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FVELOCITYFACTOR;
            m_upscalerKeyValueConfig.ptr = &_velocity;
            auto result = FfxApiProxy::D3D12_Configure(&_context, &m_upscalerKeyValueConfig.header);

            if (result != FFX_API_RETURN_OK)
                LOG_WARN("Velocity configure result: {}", (UINT) result);
        }

        if (Version() >= feature_version { 3, 1, 4 })
        {
            if (_reactiveScale != Config::Instance()->FsrReactiveScale.value_or_default())
            {
                _reactiveScale = Config::Instance()->FsrReactiveScale.value_or_default();
                ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
                m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
                m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FREACTIVENESSSCALE;
                m_upscalerKeyValueConfig.ptr = &_reactiveScale;
                auto result = FfxApiProxy::D3D12_Configure(&_context, &m_upscalerKeyValueConfig.header);

                if (result != FFX_API_RETURN_OK)
                    LOG_WARN("Reactive Scale configure result: {}", (UINT) result);
            }

            if (_shadingScale != Config::Instance()->FsrShadingScale.value_or_default())
            {
                _shadingScale = Config::Instance()->FsrShadingScale.value_or_default();
                ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
                m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
                m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FSHADINGCHANGESCALE;
                m_upscalerKeyValueConfig.ptr = &_shadingScale;
                auto result = FfxApiProxy::D3D12_Configure(&_context, &m_upscalerKeyValueConfig.header);

                if (result != FFX_API_RETURN_OK)
                    LOG_WARN("Shading Scale configure result: {}", (UINT) result);
            }

            if (_accAddPerFrame != Config::Instance()->FsrAccAddPerFrame.value_or_default())
            {
                _accAddPerFrame = Config::Instance()->FsrAccAddPerFrame.value_or_default();
                ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
                m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
                m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FACCUMULATIONADDEDPERFRAME;
                m_upscalerKeyValueConfig.ptr = &_accAddPerFrame;
                auto result = FfxApiProxy::D3D12_Configure(&_context, &m_upscalerKeyValueConfig.header);

                if (result != FFX_API_RETURN_OK)
                    LOG_WARN("Acc. Add Per Frame configure result: {}", (UINT) result);
            }

            if (_minDisOccAcc != Config::Instance()->FsrMinDisOccAcc.value_or_default())
            {
                _minDisOccAcc = Config::Instance()->FsrMinDisOccAcc.value_or_default();
                ffxConfigureDescUpscaleKeyValue m_upscalerKeyValueConfig {};
                m_upscalerKeyValueConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_UPSCALE_KEYVALUE;
                m_upscalerKeyValueConfig.key = FFX_API_CONFIGURE_UPSCALE_KEY_FMINDISOCCLUSIONACCUMULATION;
                m_upscalerKeyValueConfig.ptr = &_minDisOccAcc;
                auto result = FfxApiProxy::D3D12_Configure(&_context, &m_upscalerKeyValueConfig.header);

                if (result != FFX_API_RETURN_OK)
                    LOG_WARN("Minimum Disocclusion Acc. configure result: {}", (UINT) result);
            }
        }

        if (InParameters->Get("FSR.upscaleSize.width", &params.upscaleSize.width) == NVSDK_NGX_Result_Success &&
            Config::Instance()->OutputScalingEnabled.value_or_default())
        {
            auto originalWidth = static_cast<float>(params.upscaleSize.width);
            params.upscaleSize.width =
                static_cast<uint32_t>(originalWidth * Config::Instance()->OutputScalingMultiplier.value_or_default());
        }
        else if (params.upscaleSize.width == 0)
        {
            params.upscaleSize.width = TargetWidth();
        }

        if (InParameters->Get("FSR.upscaleSize.height", &params.upscaleSize.height) == NVSDK_NGX_Result_Success &&
            Config::Instance()->OutputScalingEnabled.value_or_default())
        {
            auto originalHeight = static_cast<float>(params.upscaleSize.height);
            params.upscaleSize.height =
                static_cast<uint32_t>(originalHeight * Config::Instance()->OutputScalingMultiplier.value_or_default());
        }
        else if (params.upscaleSize.height == 0)
        {
            params.upscaleSize.height = TargetHeight();
        }

        LOG_DEBUG("Dispatch!!");
        ffxresult = FfxApiProxy::D3D12_Dispatch(&_context, &params.header);
        state = 1;

        if (ffxresult != FFX_API_RETURN_OK)
        {
            LOG_ERROR("ffxFsr2ContextDispatch error: {0}", FfxApiProxy::ReturnCodeToString(ffxresult));
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
            InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &rcasConstants.MvScaleX);
            InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &rcasConstants.MvScaleY);

            if (DepthInverted())
            {
                rcasConstants.CameraNear = params.cameraFar;
                rcasConstants.CameraFar = params.cameraNear;
            }
            else
            {
                rcasConstants.CameraNear = params.cameraNear;
                rcasConstants.CameraFar = params.cameraFar;
            }

            if (useSS)
            {
                if (!RCAS->Dispatch(_dx11on12Device, cmdList, (ID3D12Resource*) params.output.resource,
                                    (ID3D12Resource*) params.motionVectors.resource, rcasConstants,
                                    OutputScaler->Buffer(), (ID3D12Resource*) params.depth.resource))
                {
                    Config::Instance()->RcasEnabled.set_volatile_value(false);
                    break;
                }
            }
            else
            {
                if (!RCAS->Dispatch(_dx11on12Device, cmdList, (ID3D12Resource*) params.output.resource,
                                    (ID3D12Resource*) params.motionVectors.resource, rcasConstants, vkOut.Dx12Resource,
                                    (ID3D12Resource*) params.depth.resource))
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

        if (ffxresult != FFX_API_RETURN_OK)
            break;

        if (!CopyBackOutput())
        {
            LOG_ERROR("Can't copy output texture back!");
            break;
        }

        evalResult = true;

    } while (false);

    _frameCount++;
    // Dx12CommandQueue->Signal(Dx12Fence, _frameCount);

    return evalResult;
}

bool FSR31FeatureVkOn12::InitFSR3(const NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!ModuleLoaded())
        return false;

    if (IsInited())
        return true;

    if (_dx11on12Device == nullptr)
    {
        LOG_ERROR("D3D12Device is null!");
        return false;
    }

    {
        ScopedSkipSpoofing skipSpoofing {};

        ffxQueryDescGetVersions versionQuery {};
        versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        versionQuery.device = _dx11on12Device;
        uint64_t versionCount = 0;
        versionQuery.outputCount = &versionCount;

        // Get number of versions for allocation
        FfxApiProxy::D3D12_Query(nullptr, &versionQuery.header);

        State::Instance().ffxUpscalerVersionIds.resize(versionCount);
        State::Instance().ffxUpscalerVersionNames.resize(versionCount);
        versionQuery.versionIds = State::Instance().ffxUpscalerVersionIds.data();
        versionQuery.versionNames = State::Instance().ffxUpscalerVersionNames.data();

        // Fill version ids and names arrays
        FfxApiProxy::D3D12_Query(nullptr, &versionQuery.header);

        _contextDesc.flags = 0;
        _contextDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;

#ifdef _DEBUG
        LOG_INFO("Debug checking enabled!");
        _contextDesc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
        _contextDesc.fpMessage = FfxLogCallback;
#endif

        if (DepthInverted())
            _contextDesc.flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED;

        if (AutoExposure())
            _contextDesc.flags |= FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;

        if (IsHdr())
            _contextDesc.flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;

        if (JitteredMV())
            _contextDesc.flags |= FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

        if (!LowResMV())
            _contextDesc.flags |= FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;

        if (Config::Instance()->Fsr4EnableDebugView.value_or_default())
        {
            LOG_INFO("Debug view enabled!");
            _contextDesc.flags |= 512; // FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION
        }

        if (Config::Instance()->FsrNonLinearColorSpace.value_or_default())
        {
            _contextDesc.flags |= FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
            LOG_INFO("contextDesc.initFlags (NonLinearColorSpace) {0:b}", _contextDesc.flags);
        }

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

        // Extended limits changes resolution handling
        if (Config::Instance()->ExtendedLimits.value_or_default() && RenderWidth() > DisplayWidth())
        {
            _contextDesc.maxRenderSize.width = RenderWidth();
            _contextDesc.maxRenderSize.height = RenderHeight();

            Config::Instance()->OutputScalingMultiplier.set_volatile_value(1.0f);

            // If output scaling is active, let it handle downsampling
            if (Config::Instance()->OutputScalingEnabled.value_or_default() &&
                (LowResMV() || RenderWidth() == DisplayWidth()))
            {
                _contextDesc.maxUpscaleSize.width = _contextDesc.maxRenderSize.width;
                _contextDesc.maxUpscaleSize.height = _contextDesc.maxRenderSize.height;

                // Update target resolution
                _targetWidth = _contextDesc.maxRenderSize.width;
                _targetHeight = _contextDesc.maxRenderSize.height;
            }
            else
            {
                _contextDesc.maxUpscaleSize.width = DisplayWidth();
                _contextDesc.maxUpscaleSize.height = DisplayHeight();
            }
        }
        else
        {
            _contextDesc.maxRenderSize.width = TargetWidth() > DisplayWidth() ? TargetWidth() : DisplayWidth();
            _contextDesc.maxRenderSize.height = TargetHeight() > DisplayHeight() ? TargetHeight() : DisplayHeight();
            _contextDesc.maxUpscaleSize.width = TargetWidth();
            _contextDesc.maxUpscaleSize.height = TargetHeight();
        }

        // Set stability values as default if not set by user
        {
            auto config = Config::Instance();
            auto const scaleRatioX = (float) TargetWidth() / (float) RenderWidth();
            auto const scaleRatioY = (float) TargetHeight() / (float) RenderHeight();
            auto const scaleRatio = std::max(scaleRatioX, scaleRatioY);

            if (scaleRatio > 0.0f && !std::isinf(scaleRatio))
            {
                if (config->FsrVelocity.value_for_config() == std::nullopt)
                    config->FsrVelocity.set_volatile_value(0.5f);

                if (config->FsrReactiveScale.value_for_config() == std::nullopt)
                    config->FsrReactiveScale.set_volatile_value(0.25f);

                if (config->FsrShadingScale.value_for_config() == std::nullopt)
                    config->FsrShadingScale.set_volatile_value(0.5f / scaleRatio);

                if (config->FsrAccAddPerFrame.value_for_config() == std::nullopt)
                    config->FsrAccAddPerFrame.set_volatile_value(scaleRatio / 10.0f);

                if (config->FsrMinDisOccAcc.value_for_config() == std::nullopt)
                    config->FsrMinDisOccAcc.set_volatile_value(scaleRatio / 20.0f);
            }
        }

        ffxCreateBackendDX12Desc backendDesc = { 0 };
        backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        backendDesc.device = _dx11on12Device;
        _contextDesc.header.pNext = &backendDesc.header;

        if (Config::Instance()->FfxUpscalerIndex.value_or_default() < 0 ||
            Config::Instance()->FfxUpscalerIndex.value_or_default() >= State::Instance().ffxUpscalerVersionIds.size())
            Config::Instance()->FfxUpscalerIndex.set_volatile_value(0);

        ffxOverrideVersion ov = { 0 };
        ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
        ov.versionId = State::Instance().ffxUpscalerVersionIds[Config::Instance()->FfxUpscalerIndex.value_or_default()];
        backendDesc.header.pNext = &ov.header;

        LOG_DEBUG("_createContext!");

        {
            ScopedSkipHeapCapture skipHeapCapture {};

            auto ret = FfxApiProxy::D3D12_CreateContext(&_context, &_contextDesc.header, NULL);

            if (ret != FFX_API_RETURN_OK)
            {
                LOG_ERROR("_createContext error: {0}", FfxApiProxy::ReturnCodeToString(ret));
                return false;
            }
        }

        auto version =
            State::Instance().ffxUpscalerVersionNames[Config::Instance()->FfxUpscalerIndex.value_or_default()];
        _name = "FSR";
        parse_version(version);

        LOG_TRACE("sleeping after _createContext creation for 500ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    SetInit(true);

    return true;
}
