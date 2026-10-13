#include "pch.h"
#include "Upscaler_Inputs_Dx12.h"
#include "Upscaler_Inputs.h"
#include <hudfix/Hudfix_Dx12.h>
#include <resource_tracking/ResTrack_dx12.h>
#include "shaders/depth_scale/DS_Dx12.h"
#include "MathUtils.h"

using namespace OptiMath;
static DS_Dx12* DepthScale = nullptr;

void UpscalerInputsDx12::Init(ID3D12Device* device)
{
    if (State::Instance().activeFgInput != FGInput::Upscaler)
        return;

    _device = device;
}

void UpscalerInputsDx12::Reset()
{
    if (State::Instance().currentFG == nullptr || State::Instance().activeFgInput != FGInput::Upscaler ||
        _device == nullptr)
        return;

    if (Config::Instance()->FGHUDFix.value_or_default())
        Hudfix_Dx12::ResetCounters();
}

void UpscalerInputsDx12::UpscaleStart(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Parameter* InParameters,
                                      IFeature_Dx12* feature)
{
    Hudfix_Dx12::SetSkipStatus(true);

    float mvScaleX = 0.0f;
    float mvScaleY = 0.0f;
    float jitterX = 0.0f;
    float jitterY = 0.0f;

    // FSR Camera values
    auto camera = UpscalerInputs::GetCameraValues(*InParameters, feature);

    auto fg = State::Instance().currentFG;

    if (fg == nullptr || State::Instance().activeFgInput != FGInput::Upscaler || _device == nullptr)
        return;

    auto fgConstants = UpscalerInputs::GetFGConstants(feature);

    fg->EvaluateState(_device, fgConstants);

    int reset = 0;
    InParameters->Get(NVSDK_NGX_Parameter_Reset, &reset);

    InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &mvScaleX);
    InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &mvScaleY);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &jitterX);
    InParameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &jitterY);

    fg->StartNewFrame();

    auto aspectRatio = (float) feature->DisplayWidth() / (float) feature->DisplayHeight();
    fg->SetCameraValues(camera.cameraNear, camera.cameraFar, camera.cameraVFov, aspectRatio, camera.meterFactor);
    fg->SetFrameTimeDelta(State::Instance().lastFGFrameTime);
    fg->SetMVScale(mvScaleX, mvScaleY);
    fg->SetJitter(jitterX, jitterY);
    fg->SetReset(reset);
    fg->SetInterpolationRect(feature->DisplayWidth(), feature->DisplayHeight());

    Hudfix_Dx12::UpscaleStart();

    // FG Prepare
    UINT frameIndex;
    if (!State::Instance().isShuttingDown && fg->IsActive() && Config::Instance()->IsFGEnabled() &&
        State::Instance().currentSwapchain != nullptr)
    {
        // Wait for present
        if (fg->Mutex.getOwner() == 2)
        {
            LOG_TRACE("Waiting for present!");
            fg->Mutex.lock(4);
            fg->Mutex.unlockThis(4);
        }

        bool allocatorReset = false;
        frameIndex = fg->GetIndex();

        ID3D12GraphicsCommandList* commandList = nullptr;
        commandList = InCmdList;

        LOG_DEBUG("(FG) copy buffers for fgUpscaledImage[{}], frame: {}", frameIndex, fg->FrameCount());

        ID3D12Resource* paramVelocity = nullptr;
        if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramVelocity) != NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramVelocity);

        if (paramVelocity != nullptr)
        {
            Dx12Resource setResource {};
            setResource.type = FG_ResourceType::Velocity;
            setResource.cmdList = commandList;
            setResource.resource = paramVelocity;
            setResource.state = (D3D12_RESOURCE_STATES) Config::Instance()->MVResourceBarrier.value_or(
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            setResource.validity = FG_ResourceValidity::ValidNow;

            if (feature->LowResMV())
            {
                setResource.width = feature->RenderWidth();
                setResource.height = feature->RenderHeight();
            }
            else
            {
                setResource.width = feature->TargetWidth();
                setResource.height = feature->TargetHeight();
            }

            fg->SetResource(&setResource);
        }

        ID3D12Resource* paramDepth = nullptr;
        if (InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth) != NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth);

        if (paramDepth != nullptr)
        {
            auto done = false;

            if (Config::Instance()->FGEnableDepthScale.value_or_default())
            {
                if (DepthScale == nullptr)
                    DepthScale = new DS_Dx12("Depth Scale", _device);

                if (DepthScale->CreateBufferResource(_device, paramDepth, feature->DisplayWidth(),
                                                     feature->DisplayHeight(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
                    DepthScale->Buffer() != nullptr)
                {
                    DepthScale->SetBufferState(InCmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                    if (DepthScale->Dispatch(InCmdList, paramDepth, DepthScale->Buffer()))
                    {
                        Dx12Resource setResource {};
                        setResource.type = FG_ResourceType::Depth;
                        setResource.cmdList = commandList;
                        setResource.resource = DepthScale->Buffer();
                        setResource.width = feature->RenderWidth();
                        setResource.height = feature->RenderHeight();
                        setResource.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                        setResource.validity = FG_ResourceValidity::UntilPresent;

                        fg->SetResource(&setResource);

                        done = true;
                    }
                }
            }

            if (!done)
            {
                Dx12Resource setResource {};
                setResource.type = FG_ResourceType::Depth;
                setResource.cmdList = commandList;
                setResource.resource = paramDepth;
                setResource.width = feature->RenderWidth();
                setResource.height = feature->RenderHeight();
                setResource.state = (D3D12_RESOURCE_STATES) Config::Instance()->DepthResourceBarrier.value_or(
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                setResource.validity = FG_ResourceValidity::ValidNow;

                fg->SetResource(&setResource);
            }
        }

        LOG_DEBUG("(FG) copy buffers done, frame: {0}", fg->FrameCount());
    }
}

void UpscalerInputsDx12::UpscaleEnd(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Parameter* InParameters,
                                    IFeature_Dx12* feature)
{
    Hudfix_Dx12::SetSkipStatus(false);

    auto fg = State::Instance().currentFG;

    if (fg == nullptr || State::Instance().activeFgInput != FGInput::Upscaler || _device == nullptr)
        return;

    // FG Dispatch
    if (fg->IsActive() && Config::Instance()->IsFGEnabled() && State::Instance().currentSwapchain != nullptr)
    {
        if (Config::Instance()->FGHUDFix.value_or_default())
        {
            // For signal after mv & depth copies
            Hudfix_Dx12::UpscaleEnd(feature->FrameCount(), State::Instance().lastFGFrameTime);

            ID3D12Resource* output = nullptr;
            if (InParameters->Get(NVSDK_NGX_Parameter_Output, &output) != NVSDK_NGX_Result_Success)
                InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &output);

            if (output == nullptr)
                return;

            ResourceInfo info {};
            auto desc = output->GetDesc();
            info.buffer = output;
            info.width = desc.Width;
            info.height = desc.Height;
            info.format = desc.Format;
            info.flags = desc.Flags;
            info.type = UAV;
            info.captureInfo = CaptureInfo::Upscaler;

            Hudfix_Dx12::CheckForHudless(InCmdList, &info,
                                         (D3D12_RESOURCE_STATES) Config::Instance()->OutputResourceBarrier.value_or(
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                                         true);
        }
        else
        {
            LOG_DEBUG("(FG) running, frame: {0}", feature->FrameCount());
        }
    }
}
