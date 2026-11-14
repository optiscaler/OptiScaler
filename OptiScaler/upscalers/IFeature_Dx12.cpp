#include <pch.h>

#include <functional>
#include <vector>

#include "IFeature_Dx12.h"
#include "State.h"
#include <misc/DeferredRelease.h>
#include <shaders/resource_copy/RC_Common.h>

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

void IFeature_Dx12::ForwardBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                   D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState)
{
    // Backends get our padded copies in place of the game's inputs, they track their own state
    if (IsPadBuffer(InResource))
        return;

    ResourceBarrier(InCommandList, InResource, InBeforeState, InAfterState);
    _barrierTransitions.emplace_back(InResource, InBeforeState, InAfterState);
}

void IFeature_Dx12::RestoreBarriers(ID3D12GraphicsCommandList* InCommandList)
{
    for (const auto& [resource, before, after] : _barrierTransitions | std::views::reverse)
        ResourceBarrier(InCommandList, resource, after, before);

    _barrierTransitions.clear();
}

bool IFeature_Dx12::CheckSubrect(SubrectSlot InSlot, const Subrect& InRect, ID3D12Resource* InResource)
{
    const auto desc = InResource->GetDesc();

    if (!SubrectFits(InSlot, InRect, desc.Width, desc.Height))
        return false;

    auto& copy = SubrectCopy[(size_t) InSlot];

    if (copy == nullptr)
        copy = std::make_unique<RC_Dx12>("Subrect Copy", Device, InSlot == SubrectSlot::Depth);

    return copy->IsInit();
}

ID3D12Resource* IFeature_Dx12::SubrectInput(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                            SubrectSlot InSlot)
{
    const auto& rect = GetSubrect(InSlot);

    if (InResource == nullptr || !rect.HasOffset() || !CheckSubrect(InSlot, rect, InResource))
        return InResource;

    auto& copy = SubrectCopy[(size_t) InSlot];

    if (!copy->CreateBufferResource(Device, InResource, rect.Width, rect.Height, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
    {
        return InResource;
    }

    copy->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (!copy->Dispatch(InCommandList, InResource, copy->Buffer(), rect.X, rect.Y, 0, 0, rect.Width, rect.Height))
        return InResource;

    copy->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    _subrectCopied[(size_t) InSlot] = copy->Buffer();
    return copy->Buffer();
}

ID3D12Resource* IFeature_Dx12::MaskInput(ID3D12Resource* InResource)
{
    if (InResource == nullptr || !RC_Formats::IsInteger(InResource->GetDesc().Format))
        return InResource;

    WarnIntegerMask(InResource->GetDesc().Format);
    return nullptr;
}

bool IFeature_Dx12::IsPadBuffer(ID3D12Resource* InResource) const
{
    return InResource != nullptr && std::ranges::any_of(PadCopy, [InResource](const auto& copy)
                                                        { return copy != nullptr && copy->Buffer() == InResource; });
}

ID3D12Resource* IFeature_Dx12::PadInput(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                        PadSlot InSlot, uint32_t InWidth, uint32_t InHeight, uint32_t InPadWidth,
                                        uint32_t InPadHeight)
{
    auto& copy = PadCopy[InSlot];

    if (copy == nullptr)
        copy = std::make_unique<RC_Dx12>("Pad Copy", Device, InSlot == PadDepth);

    // Grows with dynamic resolution, the upscaler reads only the padded size from (0, 0)
    if (!copy->IsInit() ||
        !copy->CreateBufferResource(Device, InResource, InPadWidth, InPadHeight, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
    {
        return nullptr;
    }

    copy->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Repeating the edges keeps the padding out of the view's border pixels
    if (!copy->Dispatch(InCommandList, InResource, copy->Buffer(), 0, 0, 0, 0, InWidth, InHeight, InPadWidth,
                        InPadHeight))
    {
        return nullptr;
    }

    copy->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return copy->Buffer();
}

bool IFeature_Dx12::CheckPadRecreation(NVSDK_NGX_Parameter* InParameters, uint32_t InUpscaleWidth,
                                       uint32_t InUpscaleHeight)
{
    if (InUpscaleWidth == 0 || InUpscaleHeight == 0)
        return false;

    // Padding only shrinks, a bigger upscale size needs the max size upscaler back
    unsigned int maxWidth = 0;
    unsigned int maxHeight = 0;
    InParameters->Get("FSR.maxUpscaleSize.width", &maxWidth);
    InParameters->Get("FSR.maxUpscaleSize.height", &maxHeight);

    if ((InUpscaleWidth > DisplayWidth() || InUpscaleHeight > DisplayHeight()) &&
        (DisplayWidth() < maxWidth || DisplayHeight() < maxHeight))
    {
        LOG_INFO("Upscale size {}x{} is bigger than {}x{}, recreating the upscaler for the max size", InUpscaleWidth,
                 InUpscaleHeight, DisplayWidth(), DisplayHeight());
        InParameters->Set("OptiScaler.PadExactUpscaleSize", 0);
        State::Instance().changeBackend[Handle()->Id] = true;
        return true;
    }

    if (InUpscaleWidth == DisplayWidth() && InUpscaleHeight == DisplayHeight())
    {
        _padStableFrames = 0;
        return false;
    }

    if (InUpscaleWidth != _padLastUpscaleWidth || InUpscaleHeight != _padLastUpscaleHeight)
    {
        _padLastUpscaleWidth = InUpscaleWidth;
        _padLastUpscaleHeight = InUpscaleHeight;
        _padStableFrames = 0;
        return false;
    }

    // Padded this frame too, the new upscaler takes over on the next one
    if (++_padStableFrames == PadStableFrames)
    {
        LOG_INFO("Upscale size {}x{} stayed the same for {} frames, recreating the upscaler for it", InUpscaleWidth,
                 InUpscaleHeight, PadStableFrames);
        InParameters->Set("OptiScaler.PadExactUpscaleSize", 1);
        InParameters->Set("OptiScaler.PadExactWidth", InUpscaleWidth);
        InParameters->Set("OptiScaler.PadExactHeight", InUpscaleHeight);
        State::Instance().changeBackend[Handle()->Id] = true;
    }

    return false;
}

bool IFeature_Dx12::PadInputs(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters,
                              uint32_t InUpscaleWidth, uint32_t InUpscaleHeight, PadRestore& OutRestore)
{
    if (InUpscaleWidth == 0 || InUpscaleHeight == 0 || InUpscaleWidth > DisplayWidth() ||
        InUpscaleHeight > DisplayHeight() || (InUpscaleWidth == DisplayWidth() && InUpscaleHeight == DisplayHeight()))
    {
        return false;
    }

    auto disablePadding = [&](const char* reason)
    {
        LOG_ERROR("Can't pad {}, recreating the upscaler for the upscale size", reason);
        RestorePadding(InParameters, OutRestore);
        OutRestore = {};

        // Also for the next upscaler, the FFX inputs set it on the game's parameters
        _padToMaxUpscaleSize = false;
        InParameters->Set("OptiScaler.PadToMaxUpscaleSize", 0);
        State::Instance().changeBackend[Handle()->Id] = true;
        return false;
    };

    unsigned int renderWidth = 0;
    unsigned int renderHeight = 0;

    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &renderWidth) !=
            NVSDK_NGX_Result_Success ||
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &renderHeight) !=
            NVSDK_NGX_Result_Success ||
        renderWidth == 0 || renderHeight == 0)
    {
        return disablePadding("without a render size");
    }

    // Same pixel size as the upscale size's render, the upscaler can't take more than it was created with
    auto padWidth = (uint32_t) ((uint64_t) renderWidth * DisplayWidth() / InUpscaleWidth);
    auto padHeight = (uint32_t) ((uint64_t) renderHeight * DisplayHeight() / InUpscaleHeight);

    if (padWidth > _maxRenderWidth || padHeight > _maxRenderHeight)
    {
        if (!_padWarned)
        {
            LOG_WARN("Render {}x{} for upscale size {}x{} needs {}x{}, more than {}x{}, the view will be scaled",
                     renderWidth, renderHeight, InUpscaleWidth, InUpscaleHeight, padWidth, padHeight, _maxRenderWidth,
                     _maxRenderHeight);
            _padWarned = true;
        }

        padWidth = std::min(padWidth, _maxRenderWidth);
        padHeight = std::min(padHeight, _maxRenderHeight);
    }

    // Checked first, a missing crop would leave the max size output in the game's
    auto& outputCopy = PadCopy[PadOutput];

    if (outputCopy == nullptr)
        outputCopy = std::make_unique<RC_Dx12>("Pad Copy", Device);

    if (!outputCopy->IsInit())
        return disablePadding("the output");

    struct PadInputDesc
    {
        PadSlot Slot;
        const char* Name;
        std::optional<int32_t> Barrier;
    };

    // XeSS and the FSR 2 backends read the FFX masks
    const PadInputDesc inputs[] = {
        { PadColor, NVSDK_NGX_Parameter_Color, Config::Instance()->ColorResourceBarrier },
        { PadDepth, NVSDK_NGX_Parameter_Depth, Config::Instance()->DepthResourceBarrier },
        { PadMV, NVSDK_NGX_Parameter_MotionVectors, Config::Instance()->MVResourceBarrier },
        { PadMask, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, Config::Instance()->MaskResourceBarrier },
        { PadReactive, "FSR.reactive", Config::Instance()->MaskResourceBarrier },
        { PadTransparency, "FSR.transparencyAndComposition", Config::Instance()->MaskResourceBarrier },
    };

    for (const auto& input : inputs)
    {
        ID3D12Resource* resource = nullptr;
        if (InParameters->Get(input.Name, &resource) != NVSDK_NGX_Result_Success)
            InParameters->Get(input.Name, (void**) &resource);

        if (resource == nullptr || (input.Slot >= PadMask && MaskInput(resource) == nullptr))
            continue;

        // The FFX inputs pass the reactive mask as the bias mask too
        ID3D12Resource* padded = nullptr;
        for (const auto& [name, game, copy] : OutRestore.Resources)
        {
            if (game == resource)
                padded = copy;
        }

        if (padded == nullptr)
        {
            // The backends' barrier forwarding skips our copies, the game's resource gets it here
            if (input.Barrier.has_value())
            {
                ForwardBarrier(InCommandList, resource, (D3D12_RESOURCE_STATES) input.Barrier.value(),
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }

            // Display resolution motion vectors pad the upscale size to the max one
            padded = input.Slot == PadMV && !LowResMV() ? PadInput(InCommandList, resource, input.Slot, InUpscaleWidth,
                                                                   InUpscaleHeight, DisplayWidth(), DisplayHeight())
                                                        : PadInput(InCommandList, resource, input.Slot, renderWidth,
                                                                   renderHeight, padWidth, padHeight);
        }

        // A missing input would mix padded and unpadded sizes
        if (padded == nullptr)
            return disablePadding(input.Name);

        OutRestore.Resources.emplace_back(input.Name, resource, padded);
        InParameters->Set(input.Name, (void*) padded);
    }

    OutRestore.RenderWidth = renderWidth;
    OutRestore.RenderHeight = renderHeight;
    InParameters->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, padWidth);
    InParameters->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, padHeight);

    return true;
}

void IFeature_Dx12::RestorePadding(NVSDK_NGX_Parameter* InParameters, const PadRestore& InRestore)
{
    for (const auto& [name, resource, padded] : InRestore.Resources)
        InParameters->Set(name, (void*) resource);

    if (InRestore.RenderWidth != 0)
    {
        InParameters->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, InRestore.RenderWidth);
        InParameters->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, InRestore.RenderHeight);

        // The backend read the padded size, frame generation describes the game's resources with it
        _renderWidth = InRestore.RenderWidth;
        _renderHeight = InRestore.RenderHeight;
    }
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
        // DLSS/DLSSD take the bias mask as is
        if (GetUpscalerType() != Upscaler::DLSS && GetUpscalerType() != Upscaler::DLSSD)
            Bias = std::make_unique<Bias_Dx12>("Bias", InDevice);
        Magnifier = std::make_unique<Magnifier_Dx12>("Magnifier", InDevice);

        UpscalerTime = std::make_unique<GpuTime_Dx12>(InDevice);
    }

    return result;
}

bool IFeature_Dx12::Evaluate(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    // Frees deferred objects even when presents aren't hooked
    DeferredRelease::Collect();

    _subrectCopied = {};

    // The padding forwards barriers before the backend
    _barrierTransitions.clear();

    if (!IsInited())
    {
        LOG_ERROR("Not inited!");
        return false;
    }

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

    // Smaller upscale sizes run at the max one, padded inputs and the upscale size cropped from the output
    PadRestore padRestore;
    unsigned int padUpscaleWidth = 0;
    unsigned int padUpscaleHeight = 0;
    const bool padding = _padToMaxUpscaleSize;

    if (padding)
    {
        InParameters->Get("FSR.upscaleSize.width", &padUpscaleWidth);
        InParameters->Get("FSR.upscaleSize.height", &padUpscaleHeight);

        if (CheckPadRecreation(InParameters, padUpscaleWidth, padUpscaleHeight))
            return true;
    }

    const bool padded =
        padding && PadInputs(InCommandList, InParameters, padUpscaleWidth, padUpscaleHeight, padRestore);

    // Recreated for the upscale size, this frame would be stretched
    if (padding && !_padToMaxUpscaleSize)
    {
        RestoreBarriers(InCommandList);
        return true;
    }

    ID3D12Resource* paramOutput = nullptr;
    ID3D12Resource* paramMotion = nullptr;
    ID3D12Resource* paramDepth = nullptr;

    InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput);
    InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramMotion);
    InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth);

    // The output base is needed before EvaluateInternal, which reads them again with this frame's render size.
    // Kept as a copy, the backend reads the zeroed base when we place the output.
    ReadSubrects(InParameters);
    const Subrect outputRect = GetSubrect(SubrectSlot::Output);

    // Order is important as that's the order of shader dispatch
    std::vector<ShaderPass> pipeline;

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

    _actualSharpness.reset();
    if (useRcas)
    {
        pipeline.push_back(
            { // Setup
              [&](ID3D12Resource* nextOutput) -> ID3D12Resource*
              {
                  // Same checks as Dispatch, a skipped pass must not take over the output
                  if (paramMotion && paramOutput &&
                      RCAS->CreateBufferResource(Device, nextOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
                      RCAS->CanRender())
                  {
                      // Disable any built-in sharpness shaders
                      InParameters->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
                      _actualSharpness = _sharpness;
                      _sharpness = 0.0f;

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

                  // RCAS works on the view at (0, 0), reuse the backend's copies or crop for native subrect backends
                  auto viewInput = [&](SubrectSlot slot, ID3D12Resource* resource)
                  {
                      auto copied = _subrectCopied[(size_t) slot];
                      return copied != nullptr ? copied : SubrectInput(InCommandList, resource, slot);
                  };

                  if (!RCAS->Dispatch(InCommandList, input, viewInput(SubrectSlot::MV, paramMotion), rcasConstants,
                                      output, viewInput(SubrectSlot::Depth, paramDepth)))
                  {
                      Config::Instance()->RcasEnabled.set_volatile_value(false);
                      return false;
                  }
                  return true;
              } });
    }

    if (Magnifier->ShouldRun())
    {
        pipeline.push_back(
            { // Setup
              [&](ID3D12Resource* nextOutput) -> ID3D12Resource*
              {
                  // Same checks as Dispatch, a skipped pass must not take over the output
                  if (paramMotion && paramOutput &&
                      Magnifier->CreateBufferResource(Device, nextOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
                      Magnifier->CanRender())
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

    bool placeOutput = false;

    if (paramOutput && PlacesOutput(outputRect, !pipeline.empty()) &&
        CheckSubrect(SubrectSlot::Output, outputRect, paramOutput))
    {
        // By value, the lambdas run after this scope
        RC_Dx12* copy = SubrectCopy[(size_t) SubrectSlot::Output].get();

        pipeline.push_back({ // Setup
                             [&, copy](ID3D12Resource* nextOutput) -> ID3D12Resource*
                             {
                                 // Exact, output scaling and RCAS size their work by it
                                 if (!copy->CreateBufferResource(Device, nextOutput, outputRect.Width,
                                                                 outputRect.Height,
                                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false))
                                 {
                                     return nullptr;
                                 }

                                 copy->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                                 placeOutput = true;
                                 return copy->Buffer();
                             },

                             // Dispatch
                             [&, copy](ID3D12Resource* input, ID3D12Resource* output) -> bool
                             {
                                 copy->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                                 return copy->Dispatch(InCommandList, input, output, 0, 0, outputRect.X, outputRect.Y,
                                                       outputRect.Width, outputRect.Height);
                             } });
    }

    if (padded && paramOutput)
    {
        // By value, the lambdas run after this scope. PadInputs checked it's ready.
        RC_Dx12* copy = PadCopy[PadOutput].get();

        pipeline.push_back({ // Setup
                             [&, copy](ID3D12Resource* nextOutput) -> ID3D12Resource*
                             {
                                 // Exact, output scaling and RCAS size their work by it
                                 if (!copy->CreateBufferResource(Device, nextOutput, DisplayWidth(), DisplayHeight(),
                                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, false))
                                 {
                                     return nullptr;
                                 }

                                 copy->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                                 return copy->Buffer();
                             },

                             // Dispatch
                             [&, copy](ID3D12Resource* input, ID3D12Resource* output) -> bool
                             {
                                 copy->SetBufferState(InCommandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                                 return copy->Dispatch(InCommandList, input, output, 0, 0, 0, 0, padUpscaleWidth,
                                                       padUpscaleHeight);
                             } });
    }

    // RCAS setup and DLSS zero the game's sharpness, restored after EvaluateInternal
    float originalSharpness = 0.0f;
    bool hasSharpness =
        InParameters->Get(NVSDK_NGX_Parameter_Sharpness, &originalSharpness) == NVSDK_NGX_Result_Success;

    // Iterate BACKWARDS to establish where each shader needs to pull its input from
    ID3D12Resource* currentTarget = paramOutput;
    for (auto it = pipeline.rbegin(); it != pipeline.rend(); ++it)
    {
        ID3D12Resource* requiredInput = it->Setup(currentTarget);
        if (requiredInput)
        {
            it->outputBuffer = currentTarget;
            it->inputBuffer = requiredInput;
            currentTarget = requiredInput; // Shift the target back for the next previous stage
        }
    }

    // Upscaler will write to the first active shader, or just output
    InParameters->Set(NVSDK_NGX_Parameter_Output, currentTarget);

    if (placeOutput)
        SetSubrectBase(InParameters, SubrectSlot::Output, 0, 0);

    UpscalerTime->Start(InCommandList);

    // Game's real output, our pass buffers track their own state
    std::optional<int32_t> outputBarrier = Config::Instance()->OutputResourceBarrier;
    if (paramOutput && outputBarrier.has_value() && upscaler != Upscaler::DLSS && upscaler != Upscaler::DLSSD)
    {
        ForwardBarrier(InCommandList, paramOutput, (D3D12_RESOURCE_STATES) outputBarrier.value(),
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    auto evalResult = EvaluateInternal(InCommandList, InParameters);

    UpscalerTime->End(InCommandList);

    InParameters->Set(NVSDK_NGX_Parameter_Output, paramOutput);

    if (padded)
        RestorePadding(InParameters, padRestore);

    if (placeOutput)
        SetSubrectBase(InParameters, SubrectSlot::Output, outputRect.X, outputRect.Y);

    if (hasSharpness)
        InParameters->Set(NVSDK_NGX_Parameter_Sharpness, originalSharpness);

    if (!evalResult)
    {
        RestoreBarriers(InCommandList);
        return false;
    }

    // Iterate FORWARDS to execute the shaders in the defined order
    for (auto& pass : pipeline)
    {
        if (pass.inputBuffer && pass.outputBuffer)
        {
            if (!pass.Dispatch(pass.inputBuffer, pass.outputBuffer))
            {
                RestoreBarriers(InCommandList);
                return true;
            }
        }
    }

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

    // After Imgui, which expects the output in UAV
    RestoreBarriers(InCommandList);

    return evalResult;
}

std::optional<double> IFeature_Dx12::ReadUpscalerTime(void* commandQueueVoid)
{
    if (!UpscalerTime || !RCAS || !OutputScaler)
        return std::nullopt;

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
    if (State::Instance().isShuttingDown)
        return;

    Imgui.reset();
    OutputScaler.reset();
    RCAS.reset();
    Bias.reset();
}
