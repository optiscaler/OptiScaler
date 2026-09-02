#include <pch.h>
#include "IFeature_Dx11.h"
#include <State.h>

using Microsoft::WRL::ComPtr;

bool IFeature_Dx11::Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("ID3D11Device is null!");
        return false;
    }

    if (InContext == nullptr)
    {
        LOG_ERROR("ID3D11DeviceContext is null!");
        return false;
    }

    Device = InDevice;
    DeviceContext = InContext;

    auto result = InitInternal(InContext, InParameters);

    if (result)
    {
        if (!Config::Instance()->OverlayMenu.value_or_default() && (Imgui == nullptr || Imgui.get() == nullptr))
            Imgui = std::make_unique<Menu_Dx11>(Util::GetProcessWindow(), InDevice);

        OutputScaler = std::make_unique<OS_Dx11>("Output Scaling", InDevice, (TargetWidth() < DisplayWidth()));
        RCAS = std::make_unique<RCAS_Dx11>("RCAS", InDevice);
        Bias = std::make_unique<Bias_Dx11>("Bias", InDevice); // TODO: not needed on DLSS/DLSSD
        Magnifier = std::make_unique<Magnifier_Dx11>("Magnifier", InDevice);

        UpscalerTime = std::make_unique<GpuTime_Dx11>(InDevice);
    }

    return result;
}

bool IFeature_Dx11::Evaluate(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters)
{
    auto result = true;

    ComPtr<ID3D11ShaderResourceView> restoreSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ComPtr<ID3D11SamplerState> restoreSamplerStates[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    ComPtr<ID3D11Buffer> restoreCBVs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ComPtr<ID3D11UnorderedAccessView> restoreUAVs[D3D11_1_UAV_SLOT_COUNT] = {};
    ComPtr<ID3D11RenderTargetView> restoreRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11RenderTargetView* rawRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ComPtr<ID3D11DepthStencilView> restoreDSV = nullptr;

    // backup compute shader resources
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
    {
        InDeviceContext->CSGetShaderResources(i, 1, restoreSRVs[i].GetAddressOf());
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
    {
        InDeviceContext->CSGetSamplers(i, 1, restoreSamplerStates[i].GetAddressOf());
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
    {
        InDeviceContext->CSGetConstantBuffers(i, 1, restoreCBVs[i].GetAddressOf());
    }

    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
    {
        InDeviceContext->CSGetUnorderedAccessViews(i, 1, restoreUAVs[i].GetAddressOf());
    }

    InDeviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rawRTVs, restoreDSV.GetAddressOf());

    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        restoreRTVs[i].Attach(rawRTVs[i]);
    }

    // Unbind RenderTargets
    ID3D11RenderTargetView* nullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    InDeviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, nullRTVs, nullptr);

    if (Config::Instance()->OverrideSharpness.value_or_default())
        _sharpness = Config::Instance()->Sharpness.value_or_default();
    else
        _sharpness = GetSharpness(InParameters);

    if (_sharpness > 1.0f)
        _sharpness = 1.0f;

    // Those upcalers don't have their own sharpness so always need to use RCAS when sharpness is set
    auto upscaler = GetUpscalerType();
    bool useRcas = upscaler == Upscaler::XeSS ||
                   (upscaler == Upscaler::DLSS && Version() >= feature_version(2, 5, 1)) || upscaler == Upscaler::DLSSD;

    if (!useRcas)
        useRcas = Config::Instance()->RcasEnabled.value_or_default();

    if (_sharpness == 0.0f)
        useRcas = false;

    // Need RCAS for MAS
    if (!useRcas && (Config::Instance()->MotionSharpnessEnabled.value_or_default() &&
                     Config::Instance()->MotionSharpness.value_or_default() > 0.0f))
    {
        useRcas = true;
    }

    if (!RCAS->IsInit())
        useRcas = false;

    bool useOutputScaling =
        Config::Instance()->OutputScalingEnabled.value_or_default() && (LowResMV() || RenderWidth() == DisplayWidth());

    if (!OutputScaler->IsInit())
        useOutputScaling = false;

    ID3D11Resource* paramOutput = nullptr;
    ID3D11Resource* paramMotion = nullptr;
    ID3D11Resource* paramDepth = nullptr;

    InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput);
    InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramMotion);
    InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth);

    // Order is important as that's the order of shader dispatch
    std::vector<ShaderPass> pipeline;

    if (useOutputScaling)
    {
        pipeline.push_back(
            { // Setup
              [&](ID3D11Resource* nextOutput) -> ID3D11Resource*
              {
                  if (OutputScaler->CreateBufferResource(Device, nextOutput, TargetWidth(), TargetHeight()))
                  {
                      return OutputScaler->Buffer();
                  }
                  return nullptr;
              },

              // Dispatch
              [&](ID3D11Resource* input, ID3D11Resource* output) -> bool
              {
                  LOG_DEBUG("Scaling output...");

                  if (!OutputScaler->Dispatch(Device, InDeviceContext, (ID3D11Texture2D*) input,
                                              (ID3D11Texture2D*) output))
                  {
                      Config::Instance()->OutputScalingEnabled.set_volatile_value(false);
                      State::Instance().changeBackend[Handle()->Id] = true;
                      return false;
                  }
                  return true;
              } });
    }

    _actualSharpness = _sharpness;
    if (useRcas)
    {
        pipeline.push_back({ // Setup
                             [&](ID3D11Resource* nextOutput) -> ID3D11Resource*
                             {
                                 // Disable any built-in sharpness shaders
                                 InParameters->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
                                 _sharpness = 0.0f;

                                 if (RCAS->CreateBufferResource(Device, nextOutput))
                                 {
                                     return RCAS->Buffer();
                                 }
                                 return nullptr;
                             },

                             // Dispatch
                             [&](ID3D11Resource* input, ID3D11Resource* output) -> bool
                             {
                                 if (!RCAS->CanRender() || !paramMotion || !paramOutput)
                                     return true;

                                 RcasConstants rcasConstants {};

                                 rcasConstants.Sharpness = _actualSharpness.value_or(_sharpness);
                                 rcasConstants.DepthIsLinear = DepthLinear();
                                 rcasConstants.DepthIsReversed = DepthInverted();
                                 rcasConstants.IsHdr = IsHdr();

                                 // Restore value
                                 _sharpness = _actualSharpness.value_or(_sharpness);
                                 _actualSharpness.reset();

                                 InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &rcasConstants.MvScaleX);
                                 InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &rcasConstants.MvScaleY);

                                 float nearPlane = 0.0f;
                                 float farPlane = 0.0f;

                                 // We need camera near and far for DLSSD
                                 // We passthrough those values from the DLSSG params onto the upscaler's params
                                 if (InParameters->Get("DLSSG.CameraNear", &nearPlane) == NVSDK_NGX_Result_Success &&
                                     InParameters->Get("DLSSG.CameraFar", &farPlane) == NVSDK_NGX_Result_Success)
                                 {
                                     rcasConstants.CameraNear = nearPlane;
                                     rcasConstants.CameraFar = farPlane;
                                 }
                                 else
                                 {
                                     rcasConstants.CameraNear = Config::Instance()->FsrCameraNear.value_or_default();
                                     rcasConstants.CameraFar = Config::Instance()->FsrCameraFar.value_or_default();
                                 }

                                 if (!RCAS->Dispatch(Device, InDeviceContext, (ID3D11Texture2D*) input,
                                                     (ID3D11Texture2D*) paramMotion, rcasConstants,
                                                     (ID3D11Texture2D*) output, (ID3D11Texture2D*) paramDepth))
                                 {
                                     Config::Instance()->RcasEnabled.set_volatile_value(false);
                                     return false;
                                 }
                                 return true;
                             } });
    }

    if (Magnifier->ShouldRun())
    {
        pipeline.push_back({ // Setup
                             [&](ID3D11Resource* nextOutput) -> ID3D11Resource*
                             {
                                 if (Magnifier->CreateBufferResource(Device, nextOutput))
                                     return Magnifier->Buffer();

                                 return nullptr;
                             },

                             // Dispatch
                             [&](ID3D11Resource* input, ID3D11Resource* output) -> bool
                             {
                                 if (!Magnifier->CanRender() || !paramMotion || !paramOutput)
                                     return true;

                                 return Magnifier->Dispatch(Device, InDeviceContext, (ID3D11Texture2D*) input,
                                                            (ID3D11Texture2D*) output);
                             } });
    }

    // Iterate BACKWARDS to establish where each shader needs to pull its input from
    ID3D11Resource* currentTarget = paramOutput;
    for (auto it = pipeline.rbegin(); it != pipeline.rend(); ++it)
    {
        ID3D11Resource* requiredInput = it->Setup(currentTarget);
        if (requiredInput)
        {
            it->outputBuffer = currentTarget;
            it->inputBuffer = requiredInput;
            currentTarget = requiredInput; // Shift the target back for the next previous stage
        }
    }

    // Upscaler will write to the first active shader, or just output
    InParameters->Set(NVSDK_NGX_Parameter_Output, currentTarget);

    UpscalerTime->Start(InDeviceContext);

    auto evalResult = EvaluateInternal(InDeviceContext, InParameters);

    UpscalerTime->End(InDeviceContext);

    if (!evalResult)
        result = false;

    bool pipelineFailed = false;

    // Iterate FORWARDS to execute the shaders in the defined order
    for (auto& pass : pipeline)
    {
        if (pass.inputBuffer && pass.outputBuffer)
        {
            if (!pass.Dispatch(pass.inputBuffer, pass.outputBuffer))
            {
                pipelineFailed = true;
                break;
            }
        }
    }

    // imgui
    if (!pipelineFailed && !Config::Instance()->OverlayMenu.value_or_default() && _frameCount > 30)
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
                Imgui = std::make_unique<Menu_Dx11>(GetForegroundWindow(), Device);
        }
    }

    if (evalResult && !pipelineFailed)
        InParameters->Set(NVSDK_NGX_Parameter_Output, paramOutput);

    // restore compute shader resources
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
    {
        auto raw = restoreSRVs[i].Get();
        InDeviceContext->CSSetShaderResources(i, 1, &raw);
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
    {
        auto raw = restoreSamplerStates[i].Get();
        InDeviceContext->CSSetSamplers(i, 1, &raw);
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
    {
        auto raw = restoreCBVs[i].Get();
        InDeviceContext->CSSetConstantBuffers(i, 1, &raw);
    }

    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
    {
        auto raw = restoreUAVs[i].Get();
        InDeviceContext->CSSetUnorderedAccessViews(i, 1, &raw, 0);
    }

    InDeviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rawRTVs, restoreDSV.Get());

    _frameCount++;

    return result;
}

std::optional<double> IFeature_Dx11::ReadUpscalerTime(void* deviceContextVoid)
{
    auto* deviceContext = (ID3D11DeviceContext*) deviceContextVoid;

    lastUpscalerTime = UpscalerTime->ReadGpuTime(deviceContext);
    lastRcasTime = RCAS->ReadGpuTime(deviceContext);
    lastOutputScalingTime = OutputScaler->ReadGpuTime(deviceContext);

    return sumOpts(lastUpscalerTime, lastRcasTime, lastOutputScalingTime);
}

void IFeature_Dx11::ReadDetailedGpuTimes(void* deviceContextVoid, std::vector<DetailedGpuTime>& detailedGpuTimes)
{
    auto* deviceContext = (ID3D11DeviceContext*) deviceContextVoid;

    detailedGpuTimes.clear();

    // Do not call ReadGpuTime twice for shaders
    if (lastUpscalerTime)
        detailedGpuTimes.emplace_back(DetailedGpuTime { ShortName(), lastUpscalerTime.value(), true });

    if (lastRcasTime)
        detailedGpuTimes.emplace_back(DetailedGpuTime { RCAS->Name(), lastRcasTime.value(), true });

    if (lastOutputScalingTime)
        detailedGpuTimes.emplace_back(DetailedGpuTime { OutputScaler->Name(), lastOutputScalingTime.value(), true });

    auto magnifierTime = Magnifier->ReadGpuTime(deviceContext);

    if (magnifierTime)
        detailedGpuTimes.emplace_back(DetailedGpuTime { Magnifier->Name(), magnifierTime.value(), false });
}

IFeature_Dx11::~IFeature_Dx11()
{
    if (State::Instance().isShuttingDown)
        return;

    Imgui.reset();
    OutputScaler.reset();
    RCAS.reset();
    Bias.reset();
}
