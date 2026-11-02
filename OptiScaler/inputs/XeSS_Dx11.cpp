#include "pch.h"
#include "XeSS_Dx11.h"

#include "NVNGX_Parameter.h"

#include <proxies/XeSS_Proxy.h>
#include "menu/menu_overlay_dx.h"

static UINT64 _handleCounter = 33370000;
static UINT64 _frameCounter = 0;
static xess_context_handle_t _currentContext = nullptr;
static ID3D11Device* _d3d11Device = nullptr;
// Device this input initialised NGX with
static std::atomic<ID3D11Device*> _nvngxDevice { nullptr };
static bool _nvngxInited = false;

static bool CreateDLSSContext(xess_context_handle_t handle, ID3D11DeviceContext* commandList,
                              const xess_d3d11_execute_params_t* pExecParams)
{
    LOG_DEBUG("");

    if (!_nvParams.contains(handle) || !_d3d11InitParams.contains(handle))
        return false;

    NVSDK_NGX_Handle* nvHandle = nullptr;
    auto params = _nvParams[handle];
    auto initParams = &_d3d11InitParams[handle];
    UINT initFlags = 0;

    if ((initParams->initFlags & XESS_INIT_FLAG_LDR_INPUT_COLOR) == 0)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;

    if (initParams->initFlags & XESS_INIT_FLAG_INVERTED_DEPTH)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;

    if (initParams->initFlags & XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

    if (initParams->initFlags & XESS_INIT_FLAG_JITTERED_MV)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;

    if ((initParams->initFlags & XESS_INIT_FLAG_HIGH_RES_MV) == 0)
        initFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

    if (initParams->initFlags & XESS_INIT_FLAG_EXPOSURE_SCALE_TEXTURE)
        params->Set("XeSS.ExposureScaleTexture", 1);

    if (initParams->initFlags & XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK)
        params->Set("XeSS.ResponsivePixelMask", 1);

    params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, initFlags);

    params->Set(NVSDK_NGX_Parameter_Width, pExecParams->inputWidth);
    params->Set(NVSDK_NGX_Parameter_Height, pExecParams->inputHeight);
    params->Set(NVSDK_NGX_Parameter_OutWidth, initParams->outputResolution.x);
    params->Set(NVSDK_NGX_Parameter_OutHeight, initParams->outputResolution.y);

    switch (initParams->qualitySetting)
    {
    case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_UltraPerformance);
        break;

    case XESS_QUALITY_SETTING_PERFORMANCE:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_MaxPerf);
        break;

    case XESS_QUALITY_SETTING_BALANCED:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_Balanced);
        break;

    case XESS_QUALITY_SETTING_QUALITY:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_MaxQuality);
        break;

    case XESS_QUALITY_SETTING_ULTRA_QUALITY:
    case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_UltraQuality);
        break;

    case XESS_QUALITY_SETTING_AA:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_DLAA);
        break;

    default:
        params->Set(NVSDK_NGX_Parameter_PerfQualityValue, NVSDK_NGX_PerfQuality_Value_Balanced);
        break;
    }

    if (NVSDK_NGX_D3D11_CreateFeature(commandList, NVSDK_NGX_Feature_SuperSampling, params, &nvHandle) !=
        NVSDK_NGX_Result_Success)
        return false;

    _contexts[handle] = nvHandle;

    return true;
}

xess_result_t hk_xessD3D11CreateContext(ID3D11Device* device, xess_context_handle_t* phContext)
{
    LOG_DEBUG("");

    if (device == nullptr)
        return XESS_RESULT_ERROR_DEVICE;

    // Re-initialise NGX when the device this input initialised it with was replaced
    if (!State::Instance().nvngxDx11Inited || (_nvngxInited && _nvngxDevice.load() != device))
    {
        NVSDK_NGX_FeatureCommonInfo fcInfo {};

        auto exePath = Util::ExePath().remove_filename();

        auto nvResult = NVSDK_NGX_D3D11_Init_with_ProjectID(
            OPTI_GUID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, OPTI_VERSION, exePath.c_str(), device, &fcInfo,
            State::Instance().NVNGX_Version == 0 ? NVSDK_NGX_Version_API : State::Instance().NVNGX_Version);

        if (nvResult != NVSDK_NGX_Result_Success)
            return XESS_RESULT_ERROR_UNINITIALIZED;

        _nvngxDevice = device;
        _nvngxInited = true;
    }

    _d3d11Device = device;
    *phContext = (xess_context_handle_t) ++_handleCounter;

    NVSDK_NGX_Parameter* params = nullptr;

    if (NVSDK_NGX_D3D12_GetCapabilityParameters(&params) != NVSDK_NGX_Result_Success)
        return XESS_RESULT_ERROR_INVALID_ARGUMENT;

    _nvParams[*phContext] = params;
    _motionScales[*phContext] = { 1.0, 1.0 };

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessD3D11Init(xess_context_handle_t hContext, const xess_d3d11_init_params_t* pInitParams)
{
    LOG_DEBUG("");

    xess_d3d11_init_params_t ip {};
    ip.initFlags = pInitParams->initFlags;
    ip.outputResolution = pInitParams->outputResolution;
    ip.qualitySetting = pInitParams->qualitySetting;

    _d3d11InitParams[hContext] = ip;

    if (!_contexts.contains(hContext))
        return XESS_RESULT_SUCCESS;

    NVSDK_NGX_D3D11_ReleaseFeature(_contexts[hContext]);
    _contexts.erase(hContext);

    return XESS_RESULT_SUCCESS;
}

xess_result_t hk_xessD3D11Execute(xess_context_handle_t hContext, const xess_d3d11_execute_params_t* pExecParams)
{
    LOG_DEBUG("");

    if (_d3d11Device == nullptr)
        return XESS_RESULT_ERROR_INVALID_ARGUMENT;

    ID3D11DeviceContext* pCommandList = nullptr;
    _d3d11Device->GetImmediateContext(&pCommandList);

    if (pCommandList == nullptr)
        return XESS_RESULT_ERROR_INVALID_ARGUMENT;

    pCommandList->Release();

    if (!_contexts.contains(hContext) && !CreateDLSSContext(hContext, pCommandList, pExecParams))
        return XESS_RESULT_ERROR_UNKNOWN;

    NVSDK_NGX_Parameter* params = _nvParams[hContext];
    NVSDK_NGX_Handle* handle = _contexts[hContext];
    xess_d3d11_init_params_t* initParams = &_d3d11InitParams[hContext];

    if (_motionScales.contains(hContext))
    {
        auto scales = &_motionScales[hContext];

        if ((initParams->initFlags & XESS_INIT_FLAG_USE_NDC_VELOCITY))
        {
            if (initParams->initFlags & XESS_INIT_FLAG_HIGH_RES_MV)
            {
                params->Set(NVSDK_NGX_Parameter_MV_Scale_X, initParams->outputResolution.x * 0.5 * scales->x);
                params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, initParams->outputResolution.y * -0.5 * scales->y);
            }
            else
            {
                params->Set(NVSDK_NGX_Parameter_MV_Scale_X, pExecParams->inputWidth * 0.5 * scales->x);
                params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, pExecParams->inputHeight * -0.5 * scales->y);
            }
        }
        else
        {
            params->Set(NVSDK_NGX_Parameter_MV_Scale_X, scales->x);
            params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, scales->y);
        }
    }

    float jitterScaleX = 1.0f;
    float jitterScaleY = 1.0f;

    if (_jitterScales.contains(hContext))
    {
        auto scales = &_jitterScales[hContext];
        jitterScaleX = scales->x;
        jitterScaleY = scales->y;
    }

    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, pExecParams->jitterOffsetX * jitterScaleX);
    params->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, pExecParams->jitterOffsetY * jitterScaleY);
    params->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, pExecParams->exposureScale);
    params->Set(NVSDK_NGX_Parameter_Reset, pExecParams->resetHistory);
    params->Set(NVSDK_NGX_Parameter_Width, pExecParams->inputWidth);
    params->Set(NVSDK_NGX_Parameter_Height, pExecParams->inputHeight);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, pExecParams->inputWidth);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, pExecParams->inputHeight);
    params->Set(NVSDK_NGX_Parameter_Depth, pExecParams->pDepthTexture);
    params->Set(NVSDK_NGX_Parameter_ExposureTexture, pExecParams->pExposureScaleTexture);

    if (feature_version { XeSSProxy::Version().major, XeSSProxy::Version().minor, XeSSProxy::Version().patch } <
        feature_version { 2, 0, 1 })
        params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, pExecParams->pResponsivePixelMaskTexture);
    else
        params->Set("FSR.reactive", pExecParams->pResponsivePixelMaskTexture);

    params->Set(NVSDK_NGX_Parameter_Color, pExecParams->pColorTexture);
    params->Set(NVSDK_NGX_Parameter_MotionVectors, pExecParams->pVelocityTexture);
    params->Set(NVSDK_NGX_Parameter_Output, pExecParams->pOutputTexture);

    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, pExecParams->inputColorBase.x);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, pExecParams->inputColorBase.y);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, pExecParams->inputDepthBase.x);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, pExecParams->inputDepthBase.y);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, pExecParams->inputMotionVectorBase.x);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, pExecParams->inputMotionVectorBase.y);
    params->Set(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, pExecParams->outputColorBase.x);
    params->Set(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y, pExecParams->outputColorBase.y);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_X,
                pExecParams->inputResponsiveMaskBase.x);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_Y,
                pExecParams->inputResponsiveMaskBase.y);

    State::Instance().setInputApiName = ApiUpscalerInput::XeSS_DX11;

    if (NVSDK_NGX_D3D11_EvaluateFeature(pCommandList, handle, params, nullptr) == NVSDK_NGX_Result_Success)
        return XESS_RESULT_SUCCESS;

    return XESS_RESULT_ERROR_UNKNOWN;
}

xess_result_t hk_xessD3D11GetInitParams(xess_context_handle_t hContext, xess_d3d11_init_params_t* pInitParams)
{
    LOG_DEBUG("");

    if (!_d3d11InitParams.contains(hContext))
        return XESS_RESULT_ERROR_INVALID_CONTEXT;

    auto ip = &_d3d11InitParams[hContext];

    pInitParams->initFlags = ip->initFlags;
    pInitParams->outputResolution = ip->outputResolution;
    pInitParams->qualitySetting = ip->qualitySetting;

    return XESS_RESULT_SUCCESS;
}

void XeSSDx11DeviceDestroyed(IUnknown* device)
{
    ID3D11Device* expected = (ID3D11Device*) device;
    _nvngxDevice.compare_exchange_strong(expected, nullptr);
}
