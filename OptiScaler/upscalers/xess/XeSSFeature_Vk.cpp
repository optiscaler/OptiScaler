#include "XeSSFeature_Vk.h"
#include <nvsdk_ngx_vk.h>

static std::string ResultToString(xess_result_t result)
{
    switch (result)
    {
    case XESS_RESULT_WARNING_NONEXISTING_FOLDER:
        return "Warning Nonexistent Folder";
    case XESS_RESULT_WARNING_OLD_DRIVER:
        return "Warning Old Driver";
    case XESS_RESULT_SUCCESS:
        return "Success";
    case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE:
        return "Unsupported Device";
    case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER:
        return "Unsupported Driver";
    case XESS_RESULT_ERROR_UNINITIALIZED:
        return "Uninitialized";
    case XESS_RESULT_ERROR_INVALID_ARGUMENT:
        return "Invalid Argument";
    case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY:
        return "Device Out of Memory";
    case XESS_RESULT_ERROR_DEVICE:
        return "Device Error";
    case XESS_RESULT_ERROR_NOT_IMPLEMENTED:
        return "Not Implemented";
    case XESS_RESULT_ERROR_INVALID_CONTEXT:
        return "Invalid Context";
    case XESS_RESULT_ERROR_OPERATION_IN_PROGRESS:
        return "Operation in Progress";
    case XESS_RESULT_ERROR_UNSUPPORTED:
        return "Unsupported";
    case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY:
        return "Cannot Load Library";
    case XESS_RESULT_ERROR_UNKNOWN:
    default:
        return "Unknown";
    }
}

static xess_vk_image_view_info NV_to_XeSS(NVSDK_NGX_Resource_VK* nvResource)
{
    xess_vk_image_view_info xessResource {};

    xessResource.format = nvResource->Resource.ImageViewInfo.Format;
    xessResource.height = nvResource->Resource.ImageViewInfo.Height;
    xessResource.image = nvResource->Resource.ImageViewInfo.Image;
    xessResource.imageView = nvResource->Resource.ImageViewInfo.ImageView;
    xessResource.subresourceRange = nvResource->Resource.ImageViewInfo.SubresourceRange;
    xessResource.width = nvResource->Resource.ImageViewInfo.Width;
    xessResource.subresourceRange.layerCount = 1;
    xessResource.subresourceRange.levelCount = 1;

    // LOG_DEBUG("{}x{}, format: {}, image: {:X}, imageView: {:X}", xessResource.width, xessResource.height,
    //           (UINT) xessResource.format, (size_t) xessResource.image, (size_t) xessResource.imageView);

    // LOG_DEBUG("aspectMask: {:X}, baseArrayLayer: {}, baseMipLevel: {}, layerCount: {}, levelCount: {}",
    //           (UINT) xessResource.subresourceRange.aspectMask, xessResource.subresourceRange.baseArrayLayer,
    //           xessResource.subresourceRange.baseMipLevel, xessResource.subresourceRange.layerCount,
    //           xessResource.subresourceRange.levelCount);

    return xessResource;
}

static void XeSSLogCallback(const char* Message, xess_logging_level_t Level)
{
    auto logLevel = (int) Level + 1;
    spdlog::log((spdlog::level::level_enum) logLevel, "XeSSFeature::LogCallback XeSS Runtime ({0})", Message);
}

bool XeSSFeature_Vk::Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdList,
                          PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                          NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!_moduleLoaded)
    {
        LOG_ERROR("libxess.dll not loaded!");
        return false;
    }

    if (IsInited())
        return true;

    if (InInstance == nullptr)
    {
        LOG_ERROR("VkInstance is null!");
        return false;
    }

    if (InPD == nullptr)
    {
        LOG_ERROR("VkPhysicalDevice is null!");
        return false;
    }

    if (InDevice == nullptr)
    {
        LOG_ERROR("VkDevice is null!");
        return false;
    }

    if (InCmdList == nullptr)
    {
        LOG_ERROR("VkCommandBuffer is null!");
        return false;
    }

    State::Instance().skipSpoofing = true;

    auto ret = XeSSProxy::VKCreateContext()(InInstance, InPD, InDevice, &_xessContext);

    if (ret != XESS_RESULT_SUCCESS)
    {
        LOG_ERROR("xessD3D12CreateContext error: {0}", ResultToString(ret));
        return false;
    }

    ret = XeSSProxy::IsOptimalDriver()(_xessContext);
    LOG_DEBUG("xessIsOptimalDriver : {0}", ResultToString(ret));

    ret = XeSSProxy::SetLoggingCallback()(_xessContext, XESS_LOGGING_LEVEL_DEBUG, XeSSLogCallback);
    LOG_DEBUG("xessSetLoggingCallback : {0}", ResultToString(ret));

    xess_vk_init_params_t xessParams {};

    xessParams.initFlags = XESS_INIT_FLAG_NONE;

    if (DepthInverted())
        xessParams.initFlags |= XESS_INIT_FLAG_INVERTED_DEPTH;

    if (AutoExposure())
        xessParams.initFlags |= XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
    else
        xessParams.initFlags |= XESS_INIT_FLAG_EXPOSURE_SCALE_TEXTURE;

    if (!IsHdr())
        xessParams.initFlags |= XESS_INIT_FLAG_LDR_INPUT_COLOR;

    if (JitteredMV())
        xessParams.initFlags |= XESS_INIT_FLAG_JITTERED_MV;

    if (!LowResMV())
        xessParams.initFlags |= XESS_INIT_FLAG_HIGH_RES_MV;

    int responsiveMask = 0;
    if (InParameters->Get("XeSS.ResponsivePixelMask", &responsiveMask) == NVSDK_NGX_Result_Success &&
        responsiveMask > 0)
        xessParams.initFlags |= XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;

    if (!Config::Instance()->DisableReactiveMask.value_or(true))
    {
        Config::Instance()->DisableReactiveMask = false;
        xessParams.initFlags |= XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;
        LOG_DEBUG("xessParams.initFlags (ReactiveMaskActive) {0:b}", xessParams.initFlags);
    }

    switch (PerfQualityValue())
    {
    case NVSDK_NGX_PerfQuality_Value_UltraPerformance:
        if (Version().major >= 1 && Version().minor >= 3)
            xessParams.qualitySetting = XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
        else
            xessParams.qualitySetting = XESS_QUALITY_SETTING_PERFORMANCE;

        break;

    case NVSDK_NGX_PerfQuality_Value_MaxPerf:
        if (Version().major >= 1 && Version().minor >= 3)
            xessParams.qualitySetting = XESS_QUALITY_SETTING_BALANCED;
        else
            xessParams.qualitySetting = XESS_QUALITY_SETTING_PERFORMANCE;

        break;

    case NVSDK_NGX_PerfQuality_Value_Balanced:
        if (Version().major >= 1 && Version().minor >= 3)
            xessParams.qualitySetting = XESS_QUALITY_SETTING_QUALITY;
        else
            xessParams.qualitySetting = XESS_QUALITY_SETTING_BALANCED;

        break;

    case NVSDK_NGX_PerfQuality_Value_MaxQuality:
        if (Version().major >= 1 && Version().minor >= 3)
            xessParams.qualitySetting = XESS_QUALITY_SETTING_ULTRA_QUALITY;
        else
            xessParams.qualitySetting = XESS_QUALITY_SETTING_QUALITY;

        break;

    case NVSDK_NGX_PerfQuality_Value_UltraQuality:
        if (Version().major >= 1 && Version().minor >= 3)
            xessParams.qualitySetting = XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS;
        else
            xessParams.qualitySetting = XESS_QUALITY_SETTING_ULTRA_QUALITY;

        break;

    case NVSDK_NGX_PerfQuality_Value_DLAA:
        if (Version().major >= 1 && Version().minor >= 3)
            xessParams.qualitySetting = XESS_QUALITY_SETTING_AA;
        else
            xessParams.qualitySetting = XESS_QUALITY_SETTING_ULTRA_QUALITY;

        break;

    default:
        xessParams.qualitySetting =
            XESS_QUALITY_SETTING_BALANCED; // Set out-of-range value for non-existing XESS_QUALITY_SETTING_BALANCED mode
        break;
    }

    if (Config::Instance()->OutputScalingEnabled.value_or(false) && LowResMV())
    {
        float ssMulti = Config::Instance()->OutputScalingMultiplier.value_or(1.5f);

        if (ssMulti < 0.5f)
        {
            ssMulti = 0.5f;
            Config::Instance()->OutputScalingMultiplier = ssMulti;
        }
        else if (ssMulti > 3.0f)
        {
            ssMulti = 3.0f;
            Config::Instance()->OutputScalingMultiplier = ssMulti;
        }

        _targetWidth = DisplayWidth() * ssMulti;
        _targetHeight = DisplayHeight() * ssMulti;
    }
    else
    {
        _targetWidth = DisplayWidth();
        _targetHeight = DisplayHeight();
    }

    if (Config::Instance()->ExtendedLimits.value_or(false) && RenderWidth() > DisplayWidth())
    {
        _targetWidth = RenderWidth();
        _targetHeight = RenderHeight();

        // enable output scaling to restore image
        if (LowResMV())
        {
            Config::Instance()->OutputScalingMultiplier = 1.0f;
            Config::Instance()->OutputScalingEnabled = true;
        }
    }

    xessParams.outputResolution.x = TargetWidth();
    xessParams.outputResolution.y = TargetHeight();

    /*
    // create heaps to prevent create heap errors of xess
    if (Config::Instance()->CreateHeaps.value_or(true))
    {
        HRESULT hr;
        xess_properties_t xessProps{};
        ret = XeSSProxy::GetProperties()(_xessContext, &xessParams.outputResolution, &xessProps);

        if (ret == XESS_RESULT_SUCCESS)
        {
            CD3DX12_HEAP_DESC bufferHeapDesc(xessProps.tempBufferHeapSize, D3D12_HEAP_TYPE_DEFAULT);
            Config::Instance()->SkipHeapCapture = true;
            hr = device->CreateHeap(&bufferHeapDesc, IID_PPV_ARGS(&_localBufferHeap));
            Config::Instance()->SkipHeapCapture = false;

            if (SUCCEEDED(hr))
            {
                D3D12_HEAP_DESC textureHeapDesc{ xessProps.tempTextureHeapSize,
                    {D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0},
                    0, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES };

                Config::Instance()->SkipHeapCapture = true;
                hr = device->CreateHeap(&textureHeapDesc, IID_PPV_ARGS(&_localTextureHeap));
                Config::Instance()->SkipHeapCapture = false;

                if (SUCCEEDED(hr))
                {
                    Config::Instance()->CreateHeaps = true;

                    LOG_DEBUG("using _localBufferHeap & _localTextureHeap!");

                    xessParams.bufferHeapOffset = 0;
                    xessParams.textureHeapOffset = 0;
                    xessParams.pTempBufferHeap = _localBufferHeap;
                    xessParams.pTempTextureHeap = _localTextureHeap;
                }
                else
                {
                    _localBufferHeap->Release();
                    LOG_ERROR("CreateHeap textureHeapDesc failed {0:x}!", (UINT)hr);
                }
            }
            else
            {
                LOG_ERROR("CreateHeap bufferHeapDesc failed {0:x}!", (UINT)hr);
            }

        }
        else
        {
            LOG_ERROR("xessGetProperties failed {0}!", ResultToString(ret));
        }
    }

    // try to build pipelines with local pipeline object
    if (Config::Instance()->BuildPipelines.value_or(true))
    {
        LOG_DEBUG("xessD3D12BuildPipelines!");
        Config::Instance()->SkipHeapCapture = true;

        ID3D12Device1* device1;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1))))
        {
            LOG_ERROR("QueryInterface device1 failed!");
            ret = XeSSProxy::D3D12BuildPipelines()(_xessContext, NULL, false, xessParams.initFlags);
        }
        else
        {
            HRESULT hr = device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&_localPipeline));

            if (FAILED(hr) || !_localPipeline)
            {
                LOG_ERROR("CreatePipelineLibrary failed {0:x}!", (UINT)hr);
                ret = XeSSProxy::D3D12BuildPipelines()(_xessContext, NULL, false, xessParams.initFlags);
            }
            else
            {
                ret = XeSSProxy::D3D12BuildPipelines()(_xessContext, _localPipeline, false, xessParams.initFlags);

                if (ret != XESS_RESULT_SUCCESS)
                {
                    LOG_ERROR("xessD3D12BuildPipelines error with _localPipeline: {0}", ResultToString(ret));
                    ret = XeSSProxy::D3D12BuildPipelines()(_xessContext, NULL, false, xessParams.initFlags);
                }
                else
                {
                    LOG_DEBUG("using _localPipelines!");
                    xessParams.pPipelineLibrary = _localPipeline;
                }
            }
        }

        if (device1 != nullptr)
            device1->Release();

        Config::Instance()->SkipHeapCapture = false;

        if (ret != XESS_RESULT_SUCCESS)
        {
            LOG_ERROR("xessD3D12BuildPipelines error: {0}", ResultToString(ret));
            return false;
        }
    }

    LOG_DEBUG("xessD3D12Init!");

    if (Config::Instance()->NetworkModel.has_value() && Config::Instance()->NetworkModel.value() >= 0 &&
    Config::Instance()->NetworkModel.value() <= 5)
    {
        ret = XeSSProxy::SelectNetworkModel()(_xessContext,
    (xess_network_model_t)Config::Instance()->NetworkModel.value()); LOG_ERROR("xessSelectNetworkModel result: {0}",
    ResultToString(ret));
    }
    */

    State::Instance().skipHeapCapture = true;
    ret = XeSSProxy::VKInit()(_xessContext, &xessParams);
    State::Instance().skipHeapCapture = false;

    State::Instance().skipSpoofing = false;

    if (ret != XESS_RESULT_SUCCESS)
    {
        LOG_ERROR("xessD3D12Init error: {0}", ResultToString(ret));
        return false;
    }

    SetInit(true);

    return true;
}

bool XeSSFeature_Vk::Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!IsInited() || !_xessContext || !ModuleLoaded())
    {
        LOG_ERROR("Not inited!");
        return false;
    }

    if (State::Instance().xessDebug)
    {
        LOG_ERROR("xessDebug");

        xess_dump_parameters_t dumpParams {};
        dumpParams.frame_count = State::Instance().xessDebugFrames;
        dumpParams.frame_idx = dumpCount;
        dumpParams.path = ".";
        dumpParams.dump_elements_mask = XESS_DUMP_INPUT_COLOR | XESS_DUMP_INPUT_VELOCITY | XESS_DUMP_INPUT_DEPTH |
                                        XESS_DUMP_OUTPUT | XESS_DUMP_EXECUTION_PARAMETERS | XESS_DUMP_HISTORY;

        if (!Config::Instance()->DisableReactiveMask.value_or(true))
            dumpParams.dump_elements_mask |= XESS_DUMP_INPUT_RESPONSIVE_PIXEL_MASK;

        XeSSProxy::StartDump()(_xessContext, &dumpParams);
        State::Instance().xessDebug = false;
        dumpCount += State::Instance().xessDebugFrames;
    }

    xess_result_t xessResult;
    xess_vk_execute_params_t params {};

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

    NVSDK_NGX_Resource_VK* paramColor = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_Color, (void**) &paramColor) == NVSDK_NGX_Result_Success &&
        paramColor != nullptr)
    {
        LOG_DEBUG("Color exist..");
        params.colorTexture = NV_to_XeSS(paramColor);
    }
    else
    {
        LOG_ERROR("Color not exist!!");
        return false;
    }

    NVSDK_NGX_Resource_VK* paramVelocity = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity) == NVSDK_NGX_Result_Success &&
        paramVelocity != nullptr)
    {
        LOG_DEBUG("MotionVectors exist..");
        params.velocityTexture = NV_to_XeSS(paramVelocity);
    }
    else
    {
        LOG_ERROR("MotionVectors not exist!!");
        return false;
    }

    NVSDK_NGX_Resource_VK* paramOutput = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput) == NVSDK_NGX_Result_Success &&
        paramOutput != nullptr)
    {
        LOG_DEBUG("Output exist..");
        params.outputTexture = NV_to_XeSS(paramOutput);
    }
    else
    {
        LOG_ERROR("Output not exist!!");
        return false;
    }

    NVSDK_NGX_Resource_VK* paramDepth = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth) == NVSDK_NGX_Result_Success &&
        paramDepth != nullptr)
    {
        LOG_DEBUG("Depth exist..");
        params.depthTexture = NV_to_XeSS(paramDepth);
    }
    else
    {
        if (LowResMV())
        {
            LOG_ERROR("Depth not exist!!");
            return false;
        }
    }

    if (!AutoExposure())
    {
        NVSDK_NGX_Resource_VK* paramExp = nullptr;
        if (InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, (void**) &paramExp) == NVSDK_NGX_Result_Success &&
            paramExp != nullptr)
        {
            LOG_DEBUG("ExposureTexture exist..");
            params.exposureScaleTexture = NV_to_XeSS(paramExp);
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
    NVSDK_NGX_Resource_VK* paramReactiveMask = nullptr;

    if (supportsFloatResponsivePixelMask &&
        InParameters->Get("FSR.reactive", (void**) &paramReactiveMask) == NVSDK_NGX_Result_Success)
    {
        if (!Config::Instance()->DisableReactiveMask.value_or(true))
            params.responsivePixelMaskTexture = NV_to_XeSS(paramReactiveMask);
    }
    else
    {
        if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactiveMask) ==
                NVSDK_NGX_Result_Success &&
            paramReactiveMask != nullptr)
        {
            LOG_DEBUG("Input Bias mask exist..");
            Config::Instance()->DisableReactiveMask = false;

            if (!Config::Instance()->DisableReactiveMask.value_or(!supportsFloatResponsivePixelMask))
                params.responsivePixelMaskTexture = NV_to_XeSS(paramReactiveMask);
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

    _hasColor = params.colorTexture.image != VK_NULL_HANDLE;
    _hasMV = params.velocityTexture.image != VK_NULL_HANDLE;
    _hasOutput = params.outputTexture.image != VK_NULL_HANDLE;
    _hasDepth = params.depthTexture.image != VK_NULL_HANDLE;
    _hasExposure = params.exposureScaleTexture.image != VK_NULL_HANDLE;
    _accessToReactiveMask = params.responsivePixelMaskTexture.image != VK_NULL_HANDLE;

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
    xessResult = XeSSProxy::VKExecute()(_xessContext, InCmdBuffer, &params);

    if (xessResult != XESS_RESULT_SUCCESS)
    {
        LOG_ERROR("xessVKExecute error: {0}", ResultToString(xessResult));
        return false;
    }

    _frameCount++;

    return true;
}

XeSSFeature_Vk::XeSSFeature_Vk(unsigned int handleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(handleId, InParameters), IFeature_Vk(handleId, InParameters)
{
    _initParameters = SetInitParameters(InParameters);

    if (XeSSProxy::Module() == nullptr && XeSSProxy::InitXeSS())
        XeSSProxy::HookXeSS();

    _moduleLoaded = XeSSProxy::Module() != nullptr && XeSSProxy::VKCreateContext() != nullptr;
}

XeSSFeature_Vk::~XeSSFeature_Vk() {}
