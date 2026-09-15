#include <pch.h>

#include <functional>
#include <vector>

#include "IFeature_Dx12.h"
#include "State.h"
#include <dlssnr/DlssNr_ExposureScan.h>
#include <dlssnr/DlssNr_Pipeline_Dx12.h>

void IFeature_Dx12::ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                    D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState) const
{
    if (InBeforeState == InAfterState)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = InResource;
    barrier.Transition.StateBefore = InBeforeState;
    barrier.Transition.StateAfter = InAfterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    InCommandList->ResourceBarrier(1, &barrier);
}

bool IFeature_Dx12::Init(ID3D12Device* InDevice, ID3D12GraphicsCommandList* InCommandList,
                         NVSDK_NGX_Parameter* InParameters)
{
    Device = InDevice;

    auto result = InitInternal(InCommandList, InParameters);

    if (result)
    {
        if (!Config::Instance()->OverlayMenu.value_or_default() && (Imgui == nullptr || Imgui.get() == nullptr))
            Imgui = std::make_unique<Menu_Dx12>(Util::GetProcessWindow(), InDevice);

        OutputScaler = std::make_unique<OS_Dx12>("Output Scaling", InDevice, (TargetWidth() < DisplayWidth()));
        RCAS = std::make_unique<RCAS_Dx12>("RCAS", InDevice);
        Bias = std::make_unique<Bias_Dx12>("Bias", InDevice); // TODO: not needed on DLSS/DLSSD
        Magnifier = std::make_unique<Magnifier_Dx12>("Magnifier", InDevice);

        UpscalerTime = std::make_unique<GpuTime_Dx12>(InDevice);
    }

    return result;
}

bool IFeature_Dx12::Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters,
                             ID3D12CommandQueue* timingQueue, uint64_t submissionEpoch, bool sourceRayReconstruction)
{
    const bool interop = timingQueue != nullptr;
    if (!interop)
        submissionEpoch = State::Instance().frameCount;
    if (timingQueue == nullptr)
        timingQueue = State::Instance().currentCommandQueue;
    if (!IsInited())
    {
        LOG_ERROR("Not inited!");
        return false;
    }

    if (!NeuralRendering && Config::Instance()->DlssNrEnabled.value_or_default())
        NeuralRendering = std::make_unique<DlssNr_Dx12>("Neural Rendering", Device);

    // Hold the inputs shared by NR and SR, not just NR's colour. Restore temporary
    // jitter/exposure/reset parameters even when evaluation exits early.
    const auto holdStates = DlssNr::ResolveInputStates_Dx12(interop);
    const D3D12_RESOURCE_STATES holdInputStates[] = {
        holdStates.color, holdStates.depth, holdStates.motion, holdStates.exposure
    };
    if (NeuralRendering)
        NeuralRendering->BeginInputHold(InCommandList, InParameters, holdInputStates);
    struct RestoreHoldParameters
    {
        DlssNr_Dx12* shader;
        NVSDK_NGX_Parameter* params;
        ~RestoreHoldParameters()
        {
            if (shader)
                shader->EndInputHold(params);
        }
    } restoreHold { NeuralRendering.get(), InParameters };

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

    auto* paramOutput = GetUpscalerResource_Dx12(InParameters, NVSDK_NGX_Parameter_Output);
    auto* paramMotion = GetUpscalerResource_Dx12(InParameters, NVSDK_NGX_Parameter_MotionVectors);
    auto* paramDepth = GetUpscalerResource_Dx12(InParameters, NVSDK_NGX_Parameter_Depth);

    RestoreUpscalerResources_Dx12 restoreResources(InParameters);

    const bool rayReconstruction = sourceRayReconstruction || upscaler == Upscaler::DLSSD;
    // Specialized schedules own the two seams but keep the same per-feature shader/history lifetime.
    const bool specializedNr = NeuralRendering && NeuralRendering->ProcessSeam(
        InCommandList, InParameters, true, timingQueue, rayReconstruction, submissionEpoch, interop, GetFeatureFlags());
    const bool nrBeforeUpscale = NeuralRendering && !specializedNr &&
                                 Config::Instance()->DlssNrEnabled.value_or_default() &&
                                 Config::Instance()->DlssNrRunBeforeSr.value_or_default() &&
                                 DlssNr::CanRunBeforeUpscale_Dx12(InParameters);

    // Order is important as that's the order of shader dispatch
    ShaderPipeline_Dx12 pipeline;

    if (useOutputScaling)
    {
        pipeline.push_back(
            { // Setup
              [&](ID3D12Resource* nextOutput) -> ID3D12Resource*
              {
                  if (OutputScaler->CreateBufferResource(Device, nextOutput, TargetWidth(), TargetHeight(),
                                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
                  {
                      OutputScaler->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                      return OutputScaler->Buffer();
                  }
                  return nullptr;
              },

              // Dispatch
              [&](ID3D12Resource* input, ID3D12Resource* output) -> bool
              {
                  LOG_DEBUG("Scaling output...");
                  OutputScaler->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                  if (!OutputScaler->Dispatch(InCommandList, input, output))
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
        pipeline.push_back(
            { // Setup
              [&](ID3D12Resource* nextOutput) -> ID3D12Resource*
              {
                  // Disable any built-in sharpness shaders
                  InParameters->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
                  _sharpness = 0.0f;

                  if (RCAS->CreateBufferResource(Device, nextOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
                  {
                      RCAS->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                      return RCAS->Buffer();
                  }
                  return nullptr;
              },

              // Dispatch
              [&](ID3D12Resource* input, ID3D12Resource* output) -> bool
              {
                  if (!RCAS->CanRender() || !paramMotion || !paramOutput)
                      return true;

                  RCAS->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

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

                  if (!RCAS->Dispatch(InCommandList, input, paramMotion, rcasConstants, output, paramDepth))
                  {
                      Config::Instance()->RcasEnabled.set_volatile_value(false);
                      return false;
                  }
                  return true;
              } });
    }

    if (NeuralRendering && !specializedNr && !nrBeforeUpscale && Config::Instance()->DlssNrEnabled.value_or_default())
    {
        pipeline.push_back(MakeDlssNrPass(*NeuralRendering, Device, InCommandList, InParameters, false,
                                          GetFeatureFlags(), timingQueue, interop, rayReconstruction, submissionEpoch));
    }

    if (Magnifier->ShouldRun())
    {
        pipeline.push_back(
            { // Setup
              [&](ID3D12Resource* nextOutput) -> ID3D12Resource*
              {
                  if (Magnifier->CreateBufferResource(Device, nextOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
                  {
                      Magnifier->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                      return Magnifier->Buffer();
                  }

                  return nullptr;
              },

              // Dispatch
              [&](ID3D12Resource* input, ID3D12Resource* output) -> bool
              {
                  if (!Magnifier->CanRender() || !paramMotion || !paramOutput)
                      return true;

                  Magnifier->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                  return Magnifier->Dispatch(InCommandList, input, output);
              } });
    }

    // Post-seam scheduling sees the same final output identity as the pre-seam, after all ordinary passes.
    if (NeuralRendering)
        pipeline.push_back({ [](ID3D12Resource* output) { return output; },
                         [&](ID3D12Resource*, ID3D12Resource* output)
                         {
                             auto* previousOutput = GetUpscalerResource_Dx12(InParameters, NVSDK_NGX_Parameter_Output);
                             SetUpscalerResource_Dx12(InParameters, NVSDK_NGX_Parameter_Output, output);
                             NeuralRendering->ProcessSeam(InCommandList, InParameters, false, timingQueue,
                                                          rayReconstruction, submissionEpoch, interop,
                                                          GetFeatureFlags());
                             SetUpscalerResource_Dx12(InParameters, NVSDK_NGX_Parameter_Output, previousOutput);
                             return true;
                         } });

    // Upscaler will write to the first active shader, or just output
    auto* currentTarget = SetupShaderPipeline(pipeline, paramOutput);
    SetUpscalerResource_Dx12(InParameters, NVSDK_NGX_Parameter_Output, currentTarget);
    auto* originalColor = GetUpscalerResource_Dx12(InParameters, NVSDK_NGX_Parameter_Color);
    const bool diagnoseNr = nrBeforeUpscale && !interop;
    if (diagnoseNr)
        NeuralRendering->DiagnosePipeline(0, InCommandList, InParameters, originalColor, GetFeatureFlags(),
                                         rayReconstruction);
    if (nrBeforeUpscale)
    {
        if (auto* nrInput = PrepareDlssNrInput(*NeuralRendering, Device, InCommandList, InParameters, GetFeatureFlags(),
                                               timingQueue, interop, rayReconstruction, submissionEpoch))
            SetUpscalerResource_Dx12(InParameters, NVSDK_NGX_Parameter_Color, nrInput);
    }
    if (diagnoseNr)
    {
        auto* edited = GetUpscalerResource_Dx12(InParameters, NVSDK_NGX_Parameter_Color);
        NeuralRendering->DiagnosePipeline(1, InCommandList, InParameters, edited, GetFeatureFlags(),
                                         rayReconstruction, edited != originalColor);
    }
    UpscalerTime->Start(InCommandList);
    const bool evalResult = EvaluateInternal(InCommandList, InParameters);
    UpscalerTime->End(InCommandList);
    if (diagnoseNr)
        NeuralRendering->DiagnosePipeline(2, InCommandList, InParameters, currentTarget, GetFeatureFlags(),
                                         rayReconstruction, evalResult);
    SetUpscalerResource_Dx12(InParameters, NVSDK_NGX_Parameter_Color, originalColor);

    if (!evalResult)
        return false;

    if (!DispatchShaderPipeline(pipeline))
        return true;

    // imgui
    if (!Config::Instance()->OverlayMenu.value_or_default() && _frameCount > 30)
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
                Imgui = std::make_unique<Menu_Dx12>(GetForegroundWindow(), Device);
        }
    }

    return evalResult;
}

std::optional<double> IFeature_Dx12::ReadUpscalerTime(void* commandQueueVoid)
{
    ID3D12CommandQueue* commandQueue = (ID3D12CommandQueue*) commandQueueVoid;

    lastUpscalerTime = UpscalerTime->ReadGpuTime(commandQueue);
    lastRcasTime = RCAS->ReadGpuTime(commandQueue);
    lastOutputScalingTime = OutputScaler->ReadGpuTime(commandQueue);

    return sumOpts(lastUpscalerTime, lastRcasTime, lastOutputScalingTime);
}

void IFeature_Dx12::ReadDetailedGpuTimes(void* commandQueueVoid, std::vector<DetailedGpuTime>& detailedGpuTimes)
{
    ID3D12CommandQueue* commandQueue = (ID3D12CommandQueue*) commandQueueVoid;

    detailedGpuTimes.clear();

    // Do not call ReadGpuTime twice for shaders
    if (lastUpscalerTime)
        detailedGpuTimes.emplace_back(DetailedGpuTime { ShortName(), lastUpscalerTime.value(), true });

    if (lastRcasTime)
        detailedGpuTimes.emplace_back(DetailedGpuTime { RCAS->Name(), lastRcasTime.value(), true });

    if (lastOutputScalingTime)
        detailedGpuTimes.emplace_back(DetailedGpuTime { OutputScaler->Name(), lastOutputScalingTime.value(), true });

    auto magnifierTime = Magnifier->ReadGpuTime(commandQueue);

    if (magnifierTime)
        detailedGpuTimes.emplace_back(DetailedGpuTime { Magnifier->Name(), magnifierTime.value(), false });
}

IFeature_Dx12::IFeature_Dx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters) {}

IFeature_Dx12::~IFeature_Dx12()
{
    DlssNr::ExposureScan::ReleaseTrackedResources();
    if (State::Instance().isShuttingDown)
        return;

    Imgui.reset();
    OutputScaler.reset();
    RCAS.reset();
    Bias.reset();
    DlssNr_Dx12::Retire(std::move(NeuralRendering));
}
