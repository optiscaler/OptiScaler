#include <pch.h>
#include "XeSSFeature_Dx11.h"
#include <imgui/ImGuiNotify.hpp>

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

static void XeSSLogCallback(const char* Message, xess_logging_level_t Level)
{
    auto logLevel = (int) Level + 1;
    spdlog::log((spdlog::level::level_enum) logLevel, "XeSSFeature::LogCallback XeSS Runtime ({0})", Message);
}

bool XeSSFeature_Dx11::InitInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!_moduleLoaded)
    {
        ImGui::InsertNotification(
            { ImGuiToastType::Warning, 10000, "Couldn't load libxess_dx11.dll\nCheck if the dll is present" });
        LOG_ERROR("libxess_dx11.dll not loaded!");
        return false;
    }

    if (IsInited())
        return true;

    {
#ifndef DONT_USE_XMX
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
#endif // !DONT_USE_XMX

        auto ret = XeSSProxy::D3D11CreateContext()(Device, &_xessContext);

        if (ret != XESS_RESULT_SUCCESS)
        {
            auto str = ResultToString(ret);
            ImGui::InsertNotification(
                { ImGuiToastType::Error, 10000, std::format("Couldn't create XeSS context\n{}", str).c_str() });
            LOG_ERROR("xessD3D11CreateContext error: {0}", str);
            return false;
        }

        ret = XeSSProxy::D3D11IsOptimalDriver()(_xessContext);
        LOG_DEBUG("xessIsOptimalDriver : {0}", ResultToString(ret));

        ret = XeSSProxy::D3D11SetLoggingCallback()(_xessContext, XESS_LOGGING_LEVEL_DEBUG, XeSSLogCallback);
        LOG_DEBUG("xessSetLoggingCallback : {0}", ResultToString(ret));

        xess_d3d11_init_params_t xessParams {};

        xessParams.initFlags = XESS_INIT_FLAG_NONE;

        if (DepthInverted())
            xessParams.initFlags |= XESS_INIT_FLAG_INVERTED_DEPTH;

        // Autoexposure is always enabled for XeSS Dx11
        LOG_INFO("AutoExposure is always enabled for XeSS Dx11");
        xessParams.initFlags |= XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;

        // if (AutoExposure())
        //     xessParams.initFlags |= XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
        // else
        //     xessParams.initFlags |= XESS_INIT_FLAG_EXPOSURE_SCALE_TEXTURE;

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

        _xessInitFlags = xessParams.initFlags;

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
            xessParams.qualitySetting = XESS_QUALITY_SETTING_BALANCED; // Set out-of-range value for non-existing
                                                                       // XESS_QUALITY_SETTING_BALANCED mode
            break;
        }

        if (Config::Instance()->OutputScalingEnabled.value_or_default() &&
            (LowResMV() || RenderWidth() == DisplayWidth()))
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

            _targetWidth = static_cast<unsigned int>(DisplayWidth() * ssMulti);
            _targetHeight = static_cast<unsigned int>(DisplayHeight() * ssMulti);
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

        {
            ScopedSkipHeapCapture skipHeapCapture {};
            ret = XeSSProxy::D3D11Init()(_xessContext, &xessParams);
        }

        if (ret != XESS_RESULT_SUCCESS)
        {
            LOG_ERROR("xessD3D12Init error: {0}", ResultToString(ret));
            return false;
        }
    }

    SetInit(true);

    return true;
}

bool XeSSFeature_Dx11::EvaluateInternal(ID3D11DeviceContext* DeviceContext, NVSDK_NGX_Parameter* InParameters)
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

        XeSSProxy::D3D11StartDump()(_xessContext, &dumpParams);
        State::Instance().xessDebug = false;
        dumpCount += State::Instance().xessDebugFrames;
    }

    xess_result_t xessResult;
    xess_d3d11_execute_params_t params {};

    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &params.jitterOffsetX);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &params.jitterOffsetY);

    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, &params.exposureScale) != NVSDK_NGX_Result_Success ||
        params.exposureScale <= 0.0f)
        params.exposureScale = 1.0f;

    InParameters->Get(NVSDK_NGX_Parameter_Reset, &params.resetHistory);

    GetRenderResolution(InParameters, &params.inputWidth, &params.inputHeight);

    LOG_DEBUG("Input Resolution: {0}x{1}", params.inputWidth, params.inputHeight);

    ID3D11Resource* paramColor = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_Color, &paramColor) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Color, (void**) &paramColor);

    if (paramColor)
    {
        LOG_DEBUG("Color exist..");
        params.pColorTexture = paramColor;
    }
    else
    {
        LOG_ERROR("Color not exist!!");
        return false;
    }

    ID3D11Resource* paramVelocity = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramVelocity) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity);

    if (paramVelocity)
    {
        LOG_DEBUG("MotionVectors exist..");
        params.pVelocityTexture = paramVelocity;
    }
    else
    {
        LOG_ERROR("MotionVectors not exist!!");
        return false;
    }

    ID3D11Resource* paramOutput = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput);

    if (paramOutput)
    {
        LOG_DEBUG("Output exist..");
        params.pOutputTexture = paramOutput;
    }
    else
    {
        LOG_ERROR("Output not exist!!");
        return false;
    }

    if (LowResMV())
    {
        ID3D11Resource* paramDepth = nullptr;
        if (InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth) != NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth);

        if (paramDepth)
        {
            LOG_DEBUG("Depth exist..");
            params.pDepthTexture = paramDepth;
        }
        else
        {
            LOG_ERROR("Depth not exist!!");
            return false;
        }
    }

    // if (!AutoExposure())
    //{
    //     ID3D11Resource* paramExp = nullptr;
    //     if (InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, &paramExp) != NVSDK_NGX_Result_Success)
    //         InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, (void**) &paramExp);

    //    if (paramExp)
    //    {
    //        LOG_DEBUG("ExposureTexture exist..");
    //        params.pExposureScaleTexture = paramExp;
    //    }
    //    else
    //    {
    //        LOG_WARN("AutoExposure disabled but ExposureTexture is not exist, it may cause problems!!");
    //        State::Instance().AutoExposure = true;
    //        State::Instance().changeBackend[_handle->Id] = true;
    //        return true;
    //    }
    //}
    // else
    //{
    //    LOG_DEBUG("AutoExposure is always enabled for XeSS Dx11!");
    //}

    ID3D11Resource* paramReactiveMask = nullptr;
    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &paramReactiveMask) !=
        NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactiveMask);

    bool supportsFloatResponsivePixelMask = Version() >= feature_version { 2, 0, 1 };

    if (!Config::Instance()->DisableReactiveMask.value_or(true) && supportsFloatResponsivePixelMask)
    {
        if (paramReactiveMask)
        {
            LOG_DEBUG("Input Bias mask exist..");
            params.pResponsivePixelMaskTexture = paramReactiveMask;
        }
    }

    if ((_xessInitFlags & XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK) > 0 && params.pResponsivePixelMaskTexture == nullptr)
    {
        LOG_WARN("Bias mask not exist and its enabled in config, it may cause problems!!");
        Config::Instance()->DisableReactiveMask = true;
        State::Instance().changeBackend[_handle->Id] = true;
        return true;
    }

    _hasColor = params.pColorTexture != nullptr;
    _hasMV = params.pVelocityTexture != nullptr;
    _hasOutput = params.pOutputTexture != nullptr;
    _hasDepth = params.pDepthTexture != nullptr;
    _hasExposure = params.pExposureScaleTexture != nullptr;
    _accessToReactiveMask = params.pResponsivePixelMaskTexture != nullptr;

    float MVScaleX = 1.0f;
    float MVScaleY = 1.0f;

    if (InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &MVScaleX) == NVSDK_NGX_Result_Success &&
        InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &MVScaleY) == NVSDK_NGX_Result_Success)
    {
        xessResult = XeSSProxy::D3D11SetVelocityScale()(_xessContext, MVScaleX, MVScaleY);

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
    xessResult = XeSSProxy::D3D11Execute()(_xessContext, &params);

    if (xessResult != XESS_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D11Execute error: {0}", ResultToString(xessResult));
        return false;
    }

    return true;
}

XeSSFeature_Dx11::XeSSFeature_Dx11(unsigned int handleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(handleId, InParameters), IFeature_Dx11(handleId, InParameters)
{
    _initParameters = SetInitParameters(InParameters);

    if (XeSSProxy::ModuleDx11() == nullptr)
        XeSSProxy::InitXeSSDx11();

    _moduleLoaded = XeSSProxy::ModuleDx11() != nullptr && XeSSProxy::D3D11CreateContext() != nullptr;
}

XeSSFeature_Dx11::~XeSSFeature_Dx11()
{
    if (State::Instance().isShuttingDown)
        return;

    if (_xessContext)
    {
        XeSSProxy::D3D11DestroyContext()(_xessContext);
        _xessContext = nullptr;
    }
}
