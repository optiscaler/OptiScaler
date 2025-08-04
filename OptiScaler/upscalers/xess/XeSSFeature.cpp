#pragma once
#include "XeSSFeature.h"

#include <pch.h>
#include <Config.h>
#include <Util.h>

#include <include/detours/detours.h>
#include <include/d3dx/d3dx12.h>

inline void XeSSLogCallback(const char* Message, xess_logging_level_t Level)
{
    auto logLevel = (int) Level + 1;
    spdlog::log((spdlog::level::level_enum) logLevel, "XeSSFeature::LogCallback XeSS Runtime ({0})", Message);
}

bool XeSSFeature::InitXeSS(ID3D12Device* device, const NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (!_moduleLoaded)
    {
        LOG_ERROR("libxess.dll not loaded!");
        return false;
    }

    if (IsInited())
        return true;

    if (device == nullptr)
    {
        LOG_ERROR("D3D12Device is null!");
        return false;
    }

    State::Instance().skipSpoofing = true;

    auto ret = XeSSProxy::D3D12CreateContext()(device, &_xessContext);

    if (ret != XESS_RESULT_SUCCESS)
    {
        LOG_ERROR("xessD3D12CreateContext error: {0}", ResultToString(ret));
        return false;
    }

    if (XeSSProxy::IsOptimalDriver() != nullptr)
    {
        ret = XeSSProxy::IsOptimalDriver()(_xessContext);
        LOG_DEBUG("xessIsOptimalDriver : {0}", ResultToString(ret));
    }

    if (XeSSProxy::SetLoggingCallback() != nullptr)
    {
        ret = XeSSProxy::SetLoggingCallback()(_xessContext, XESS_LOGGING_LEVEL_DEBUG, XeSSLogCallback);
        LOG_DEBUG("xessSetLoggingCallback : {0}", ResultToString(ret));
    }

    xess_d3d12_init_params_t xessParams {};

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

    // create heaps to prevent create heap errors of xess
    if (Config::Instance()->CreateHeaps.value_or(true))
    {
        HRESULT hr;
        xess_properties_t xessProps {};
        ret = XeSSProxy::GetProperties()(_xessContext, &xessParams.outputResolution, &xessProps);

        if (ret == XESS_RESULT_SUCCESS)
        {
            CD3DX12_HEAP_DESC bufferHeapDesc(xessProps.tempBufferHeapSize, D3D12_HEAP_TYPE_DEFAULT);
            State::Instance().skipHeapCapture = true;
            hr = device->CreateHeap(&bufferHeapDesc, IID_PPV_ARGS(&_localBufferHeap));
            State::Instance().skipHeapCapture = false;

            if (SUCCEEDED(hr))
            {
                D3D12_HEAP_DESC textureHeapDesc { xessProps.tempTextureHeapSize,
                                                  { D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                                    D3D12_MEMORY_POOL_UNKNOWN, 0, 0 },
                                                  0,
                                                  D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES };

                State::Instance().skipHeapCapture = true;
                hr = device->CreateHeap(&textureHeapDesc, IID_PPV_ARGS(&_localTextureHeap));
                State::Instance().skipHeapCapture = false;

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
                    LOG_ERROR("CreateHeap textureHeapDesc failed {0:x}!", (UINT) hr);
                }
            }
            else
            {
                LOG_ERROR("CreateHeap bufferHeapDesc failed {0:x}!", (UINT) hr);
            }
        }
        else
        {
            LOG_ERROR("xessGetProperties failed {0}!", ResultToString(ret));
        }
    }

    // try to build pipelines with local pipeline object
    if (Config::Instance()->BuildPipelines.value_or(true) && Version() > feature_version { 1, 2, 0 })
    {
        LOG_DEBUG("xessD3D12BuildPipelines!");
        State::Instance().skipHeapCapture = true;

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
                LOG_ERROR("CreatePipelineLibrary failed {0:x}!", (UINT) hr);
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

        State::Instance().skipHeapCapture = false;

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
                                              (xess_network_model_t) Config::Instance()->NetworkModel.value());
        LOG_ERROR("xessSelectNetworkModel result: {0}", ResultToString(ret));
    }

    State::Instance().skipHeapCapture = true;
    ret = XeSSProxy::D3D12Init()(_xessContext, &xessParams);
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

XeSSFeature::XeSSFeature(unsigned int handleId, NVSDK_NGX_Parameter* InParameters) : IFeature(handleId, InParameters)
{
    _initParameters = SetInitParameters(InParameters);
}

XeSSFeature::~XeSSFeature()
{
    if (State::Instance().isShuttingDown || _xessContext == nullptr)
        return;

    if (_xessContext)
    {
        XeSSProxy::DestroyContext()(_xessContext);
        _xessContext = nullptr;
    }

    if (_localPipeline != nullptr)
    {
        _localPipeline->Release();
        _localPipeline = nullptr;
    }

    if (_localBufferHeap != nullptr)
    {
        _localBufferHeap->Release();
        _localBufferHeap = nullptr;
    }

    if (_localTextureHeap != nullptr)
    {
        _localTextureHeap->Release();
        _localTextureHeap = nullptr;
    }
}
