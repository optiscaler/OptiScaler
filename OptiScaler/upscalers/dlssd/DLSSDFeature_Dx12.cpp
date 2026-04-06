#include <pch.h>
#include "DLSSDFeature_Dx12.h"
#include <dxgi1_4.h>
#include <Config.h>

bool DLSSDFeatureDx12::Init(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCommandList,
                            NVSDK_NGX_Parameter* InParameters)
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

    do
    {
        if (!_dlssdInited)
        {
            _dlssdInited = NVNGXProxy::InitDx12(InDevice);

            if (!_dlssdInited)
                return false;

            _moduleLoaded =
                (NVNGXProxy::D3D12_Init_ProjectID() != nullptr || NVNGXProxy::D3D12_Init_Ext() != nullptr) &&
                (NVNGXProxy::D3D12_Shutdown() != nullptr || NVNGXProxy::D3D12_Shutdown1() != nullptr) &&
                (NVNGXProxy::D3D12_GetParameters() != nullptr || NVNGXProxy::D3D12_AllocateParameters() != nullptr) &&
                NVNGXProxy::D3D12_DestroyParameters() != nullptr && NVNGXProxy::D3D12_CreateFeature() != nullptr &&
                NVNGXProxy::D3D12_ReleaseFeature() != nullptr && NVNGXProxy::D3D12_EvaluateFeature() != nullptr;

            // delay between init and create feature
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        LOG_INFO("Creating DLSSD feature");

        if (NVNGXProxy::D3D12_CreateFeature() != nullptr)
        {
            ProcessInitParams(InParameters);

            _p_dlssdHandle = &_dlssdHandle;

            {
                ScopedSkipHeapCapture skipHeapCapture {};

                nvResult = NVNGXProxy::D3D12_CreateFeature()(InCommandList, NVSDK_NGX_Feature_RayReconstruction,
                                                             InParameters, &_p_dlssdHandle);
            }

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
        RCAS = std::make_unique<RCAS_Dx12>("RCAS", InDevice);

        if (!Config::Instance()->OverlayMenu.value_or_default() && (Imgui == nullptr || Imgui.get() == nullptr))
            Imgui = std::make_unique<Menu_Dx12>(Util::GetProcessWindow(), InDevice);

        OutputScaler = std::make_unique<OS_Dx12>("OutputScaling", InDevice, (TargetWidth() < DisplayWidth()));
    }

    SetInit(initResult);

    return initResult;
}

bool DLSSDFeatureDx12::Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    if (!_moduleLoaded)
    {
        LOG_ERROR("nvngx.dll or _nvngx.dll is not loaded!");
        return false;
    }

    if (!IsInited())
    {
        LOG_ERROR("Not inited!");
        return false;
    }

    bool rcasEnabled = true;

    if (Config::Instance()->RcasEnabled.value_or(rcasEnabled) &&
        (RCAS == nullptr || RCAS.get() == nullptr || !RCAS->IsInit()))
        Config::Instance()->RcasEnabled.set_volatile_value(false);

    if (!OutputScaler->IsInit())
        Config::Instance()->OutputScalingEnabled.set_volatile_value(false);

    NVSDK_NGX_Result nvResult;

    if (NVNGXProxy::D3D12_EvaluateFeature() != nullptr)
    {
        ProcessEvaluateParams(InParameters);

        ID3D12Resource* paramOutput = nullptr;
        ID3D12Resource* paramDepth = nullptr;
        ID3D12Resource* paramMotion = nullptr;
        ID3D12Resource* setBuffer = nullptr;

        bool useSS = Config::Instance()->OutputScalingEnabled.value_or_default() &&
                     (LowResMV() || RenderWidth() == DisplayWidth());

        InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput);
        InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramMotion);
        InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth);

        if (paramDepth != nullptr)
            LOG_DEBUG("Depth exist, {:X}", (size_t) paramDepth);

        if (paramMotion != nullptr)
            LOG_DEBUG("Velocity exist, {:X}", (size_t) paramMotion);

        if (paramOutput != nullptr)
            LOG_DEBUG("Output exist, {:X}", (size_t) paramOutput);

        // supersampling
        if (useSS)
        {
            if (OutputScaler->CreateBufferResource(Device, paramOutput, TargetWidth(), TargetHeight(),
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            {
                OutputScaler->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                setBuffer = OutputScaler->Buffer();
            }
            else
                setBuffer = paramOutput;
        }
        else
        {
            setBuffer = paramOutput;
        }

        // RCAS sharpness & preperation
        _sharpness = GetSharpness(InParameters);

        if (Config::Instance()->RcasEnabled.value_or(rcasEnabled) &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                                   Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
            RCAS->IsInit() && RCAS->CreateBufferResource(Device, setBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
        {
            // Disable DLSS sharpness
            InParameters->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);

            RCAS->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            setBuffer = RCAS->Buffer();
        }

        InParameters->Set(NVSDK_NGX_Parameter_Output, setBuffer);

        nvResult = NVNGXProxy::D3D12_EvaluateFeature()(InCommandList, _p_dlssdHandle, InParameters, NULL);

        if (nvResult != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("_EvaluateFeature result: {0:X}", (unsigned int) nvResult);
            return false;
        }

        // Apply CAS
        if (Config::Instance()->RcasEnabled.value_or(rcasEnabled) &&
            (_sharpness > 0.0f || (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                                   Config::Instance()->MotionSharpness.value_or_default() > 0.0f)) &&
            RCAS->CanRender())
        {
            if (setBuffer != RCAS->Buffer())
                ResourceBarrier(InCommandList, setBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
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
            rcasConstants.DepthInverted = DepthInverted();

            if (useSS)
            {
                if (!RCAS->Dispatch(Device, InCommandList, setBuffer, paramMotion, paramDepth, rcasConstants,
                                    OutputScaler->Buffer()))
                {
                    Config::Instance()->RcasEnabled.set_volatile_value(false);
                    return true;
                }
            }
            else
            {
                if (!RCAS->Dispatch(Device, InCommandList, setBuffer, paramMotion, paramDepth, rcasConstants,
                                    paramOutput))
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
            OutputScaler->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            if (!OutputScaler->Dispatch(Device, InCommandList, OutputScaler->Buffer(), paramOutput))
            {
                Config::Instance()->OutputScalingEnabled.set_volatile_value(false);
                State::Instance().changeBackend[Handle()->Id] = true;
                return true;
            }
        }

        // imgui
        if (!Config::Instance()->OverlayMenu.value_or_default() && _frameCount > 30 && paramOutput)
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
                    Imgui = std::make_unique<Menu_Dx12>(Util::GetProcessWindow(), Device);
            }
        }

        // set original output texture back
        InParameters->Set(NVSDK_NGX_Parameter_Output, paramOutput);
    }
    else
    {
        LOG_ERROR("_EvaluateFeature is nullptr");
        return false;
    }

    _frameCount++;

    return true;
}

DLSSDFeatureDx12::DLSSDFeatureDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(InHandleId, InParameters), IFeature_Dx12(InHandleId, InParameters),
      DLSSDFeature(InHandleId, InParameters)
{
    if (NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_INFO("nvngx.dll not loaded, now loading");
        NVNGXProxy::InitNVNGX();
    }

    LOG_INFO("binding complete!");
}

DLSSDFeatureDx12::~DLSSDFeatureDx12()
{
    if (State::Instance().isShuttingDown)
        return;

    if (NVNGXProxy::D3D12_ReleaseFeature() != nullptr && _p_dlssdHandle != nullptr)
        NVNGXProxy::D3D12_ReleaseFeature()(_p_dlssdHandle);
}
