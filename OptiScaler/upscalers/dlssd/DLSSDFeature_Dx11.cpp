#include <pch.h>
#include "DLSSDFeature_Dx11.h"
#include <dxgi.h>
#include <Config.h>

bool DLSSDFeatureDx11::Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters)
{
    if (NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_ERROR("nvngx.dll not loaded!");

        SetInit(false);
        return false;
    }

    NVSDK_NGX_Result nvResult;
    bool initResult = false;

    Device = InDevice;
    DeviceContext = InContext;

    do
    {
        if (!_dlssdInited)
        {
            _dlssdInited = NVNGXProxy::InitDx11(InDevice);

            if (!_dlssdInited)
                return false;

            _moduleLoaded =
                (NVNGXProxy::D3D11_Init_ProjectID() != nullptr || NVNGXProxy::D3D11_Init_Ext() != nullptr) &&
                (NVNGXProxy::D3D11_Shutdown() != nullptr || NVNGXProxy::D3D11_Shutdown1() != nullptr) &&
                (NVNGXProxy::D3D11_GetParameters() != nullptr || NVNGXProxy::D3D11_AllocateParameters() != nullptr) &&
                NVNGXProxy::D3D11_DestroyParameters() != nullptr && NVNGXProxy::D3D11_CreateFeature() != nullptr &&
                NVNGXProxy::D3D11_ReleaseFeature() != nullptr && NVNGXProxy::D3D11_EvaluateFeature() != nullptr;

            // delay between init and create feature
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        LOG_INFO("Creating DLSSD feature");

        if (NVNGXProxy::D3D11_CreateFeature() != nullptr)
        {
            ProcessInitParams(InParameters);

            _p_dlssdHandle = &_dlssdHandle;
            nvResult = NVNGXProxy::D3D11_CreateFeature()(InContext, NVSDK_NGX_Feature_RayReconstruction, InParameters,
                                                         &_p_dlssdHandle);

            if (nvResult != NVSDK_NGX_Result_Success)
            {
                LOG_ERROR("_CreateFeature result: {0:X}", (unsigned int) nvResult);
                break;
            }
            else
            {
                LOG_INFO("_CreateFeature result: NVSDK_NGX_Result_Success, HandleId: {0}", _p_dlssdHandle->Id);
            }
        }
        else
        {
            LOG_ERROR("_CreateFeature is nullptr");
            break;
        }

        ReadVersion();

        initResult = true;

    } while (false);

    if (initResult)
    {
        if (!Config::Instance()->OverlayMenu.value_or_default() && (Imgui == nullptr || Imgui.get() == nullptr))
            Imgui = std::make_unique<Menu_Dx11>(GetForegroundWindow(), InDevice);

        OutputScaler = std::make_unique<OS_Dx11>("Output Scaling", InDevice, (TargetWidth() < DisplayWidth()));
        RCAS = std::make_unique<RCAS_Dx11>("RCAS", InDevice);
    }

    SetInit(initResult);

    return initResult;
}

bool DLSSDFeatureDx11::Evaluate(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters)
{
    if (!_moduleLoaded)
    {
        LOG_ERROR("nvngx.dll or _nvngx.dll is not loaded!");
        return false;
    }

    NVSDK_NGX_Result nvResult;

    bool rcasEnabled = true;

    if (Config::Instance()->RcasEnabled.value_or(rcasEnabled) &&
        (RCAS == nullptr || RCAS.get() == nullptr || !RCAS->IsInit()))
        Config::Instance()->RcasEnabled.set_volatile_value(false);

    if (!OutputScaler->IsInit())
        Config::Instance()->OutputScalingEnabled.set_volatile_value(false);

    if (NVNGXProxy::D3D11_EvaluateFeature() != nullptr)
    {
        ID3D11ShaderResourceView* restoreSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        ID3D11SamplerState* restoreSamplerStates[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
        ID3D11Buffer* restoreCBVs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        ID3D11UnorderedAccessView* restoreUAVs[D3D11_1_UAV_SLOT_COUNT] = {};

        // backup compute shader resources
        for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
        {
            restoreSRVs[i] = nullptr;
            InDeviceContext->CSGetShaderResources(i, 1, &restoreSRVs[i]);
        }

        for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
        {
            restoreSamplerStates[i] = nullptr;
            InDeviceContext->CSGetSamplers(i, 1, &restoreSamplerStates[i]);
        }

        for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
        {
            restoreCBVs[i] = nullptr;
            InDeviceContext->CSGetConstantBuffers(i, 1, &restoreCBVs[i]);
        }

        for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
        {
            restoreUAVs[i] = nullptr;
            InDeviceContext->CSGetUnorderedAccessViews(i, 1, &restoreUAVs[i]);
        }

        ProcessEvaluateParams(InParameters);

        ID3D11Resource* paramOutput = nullptr;
        ID3D11Resource* paramMotion = nullptr;
        ID3D11Resource* paramDepth = nullptr;
        ID3D11Resource* setBuffer = nullptr;

        bool useSS = Config::Instance()->OutputScalingEnabled.value_or_default() &&
                     (LowResMV() || RenderWidth() == DisplayWidth());

        InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput);
        InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramMotion);
        InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth);

        // supersampling
        if (useSS)
        {
            if (OutputScaler->CreateBufferResource(Device, paramOutput, TargetWidth(), TargetHeight()))
            {
                setBuffer = OutputScaler->Buffer();
            }
            else
                setBuffer = paramOutput;
        }
        else
            setBuffer = paramOutput;

        // RCAS sharpness & preperation
        _sharpness = GetSharpness(InParameters);

        if (Config::Instance()->RcasEnabled.value_or(rcasEnabled) &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                                   Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
            RCAS->IsInit() && RCAS->CreateBufferResource(Device, setBuffer))
        {
            // Disable DLSS sharpness
            InParameters->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
            setBuffer = RCAS->Buffer();
        }

        InParameters->Set(NVSDK_NGX_Parameter_Output, setBuffer);

        nvResult = NVNGXProxy::D3D11_EvaluateFeature()(InDeviceContext, _p_dlssdHandle, InParameters, NULL);

        if (nvResult != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("_EvaluateFeature result: {0:X}", (unsigned int) nvResult);
            return false;
        }

        LOG_TRACE("_EvaluateFeature ok!");

        // Apply CAS
        if (Config::Instance()->RcasEnabled.value_or(rcasEnabled) &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                                   Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
            RCAS->CanRender())
        {
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
                if (!RCAS->Dispatch(Device, InDeviceContext, (ID3D11Texture2D*) setBuffer,
                                    (ID3D11Texture2D*) paramMotion, (ID3D11Texture2D*) paramDepth, rcasConstants,
                                    OutputScaler->Buffer()))
                {
                    Config::Instance()->RcasEnabled.set_volatile_value(false);
                    return true;
                }
            }
            else
            {
                if (!RCAS->Dispatch(Device, InDeviceContext, (ID3D11Texture2D*) setBuffer,
                                    (ID3D11Texture2D*) paramMotion, (ID3D11Texture2D*) paramDepth, rcasConstants,
                                    (ID3D11Texture2D*) paramOutput))
                {
                    Config::Instance()->RcasEnabled.set_volatile_value(false);
                    return true;
                }
            }
        }

        // Downsampling
        if (useSS)
        {
            LOG_DEBUG("downscaling output...");

            if (!OutputScaler->Dispatch(Device, InDeviceContext, OutputScaler->Buffer(),
                                        (ID3D11Texture2D*) paramOutput))
            {
                Config::Instance()->OutputScalingEnabled.set_volatile_value(false);
                State::Instance().changeBackend[Handle()->Id] = true;
                return true;
            }
        }

        // imgui
        if (!Config::Instance()->OverlayMenu.value_or_default() && _frameCount > 30 && paramOutput != nullptr)
        {
            if (Imgui != nullptr && Imgui.get() != nullptr)
            {
                if (Imgui->IsHandleDifferent())
                {
                    Imgui.reset();
                }
                else
                    Imgui->Render(InDeviceContext, paramOutput);
            }
            else
            {
                if (Imgui == nullptr || Imgui.get() == nullptr)
                    Imgui = std::make_unique<Menu_Dx11>(Util::GetProcessWindow(), Device);
            }
        }

        // set original output texture back
        InParameters->Set(NVSDK_NGX_Parameter_Output, paramOutput);

        // restore compute shader resources
        for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
        {
            if (restoreSRVs[i] != nullptr)
                InDeviceContext->CSSetShaderResources(i, 1, &restoreSRVs[i]);
        }

        for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
        {
            if (restoreSamplerStates[i] != nullptr)
                InDeviceContext->CSSetSamplers(i, 1, &restoreSamplerStates[i]);
        }

        for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
        {
            if (restoreCBVs[i] != nullptr)
                InDeviceContext->CSSetConstantBuffers(i, 1, &restoreCBVs[i]);
        }

        for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
        {
            if (restoreUAVs[i] != nullptr)
                InDeviceContext->CSSetUnorderedAccessViews(i, 1, &restoreUAVs[i], 0);
        }
    }
    else
    {
        LOG_ERROR("_EvaluateFeature is nullptr");
        return false;
    }

    _frameCount++;

    return true;
}

DLSSDFeatureDx11::DLSSDFeatureDx11(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters),
      DLSSDFeature(InHandleId, InParameters)
{
    if (NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_INFO("nvngx.dll not loaded, now loading");
        NVNGXProxy::InitNVNGX();
    }

    LOG_INFO("binding complete!");
}

DLSSDFeatureDx11::~DLSSDFeatureDx11()
{
    if (State::Instance().isShuttingDown)
        return;

    if (NVNGXProxy::D3D11_ReleaseFeature() != nullptr && _p_dlssdHandle != nullptr)
        NVNGXProxy::D3D11_ReleaseFeature()(_p_dlssdHandle);
}
