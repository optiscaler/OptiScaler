#include <pch.h>

#include "IFeature_Vk.h"
#include "State.h"
#include "nvsdk_ngx_vk.h"
#include <misc/DeferredRelease.h>

NVSDK_NGX_Resource_VK* IFeature_Vk::MaskInput(NVSDK_NGX_Resource_VK* InResource)
{
    if (InResource == nullptr || !ResourceCopy_Vk::IsInteger(InResource->Resource.ImageViewInfo.Format))
        return InResource;

    WarnIntegerMask(InResource->Resource.ImageViewInfo.Format);
    return nullptr;
}

NVSDK_NGX_Resource_VK* IFeature_Vk::SubrectInput(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Resource_VK* InResource,
                                                 SubrectSlot InSlot)
{
    const auto& rect = GetSubrect(InSlot);

    if (InResource == nullptr || !rect.HasOffset())
        return InResource;

    const auto& info = InResource->Resource.ImageViewInfo;

    if (!SubrectFits(InSlot, rect, info.Width, info.Height))
        return InResource;

    auto& copy = SubrectCopy[(size_t) InSlot];

    if (copy == nullptr)
        copy = std::make_unique<ResourceCopy_Vk>("Subrect Copy", Device, PhysicalDevice);

    const auto format = copy->BufferFormat(info.Format, InSlot == SubrectSlot::Depth);

    if (!copy->IsInit() || !copy->CreateGrowingImage(rect.Width, rect.Height, format))
        return InResource;

    const VkImageSubresourceRange range { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    copy->SetImageLayout(InCmdBuffer, copy->GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, range);

    if (!copy->Dispatch(Device, InCmdBuffer, info.ImageView, copy->GetImageView(), { rect.Width, rect.Height },
                        { (int32_t) rect.X, (int32_t) rect.Y }))
    {
        return InResource;
    }

    copy->SetImageLayout(InCmdBuffer, copy->GetImage(), VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);

    auto& view = _subrectViews[(size_t) InSlot];
    view = *InResource;
    view.ReadWrite = false;

    auto& viewInfo = view.Resource.ImageViewInfo;
    viewInfo.ImageView = copy->GetImageView();
    viewInfo.Image = copy->GetImage();
    viewInfo.SubresourceRange = range;
    viewInfo.Format = format;
    viewInfo.Width = copy->ImageExtent().width;
    viewInfo.Height = copy->ImageExtent().height;

    _subrectCopied |= 1u << (uint32_t) InSlot;
    return &view;
}

bool IFeature_Vk::Init(VkInstance InInstance, VkPhysicalDevice InPD, VkDevice InDevice, VkCommandBuffer InCmdBuffer,
                       PFN_vkGetInstanceProcAddr InGIPA, PFN_vkGetDeviceProcAddr InGDPA,
                       NVSDK_NGX_Parameter* InParameters)
{
    Instance = InInstance;
    PhysicalDevice = InPD;
    Device = InDevice;
    GIPA = InGIPA;
    GDPA = InGDPA;

    auto result = InitInternal(InCmdBuffer, InParameters);

    if (result)
    {
        OutputScaler = std::make_unique<OS_Vk>("Output Scaling", InDevice, InPD, (TargetWidth() < DisplayWidth()));
        RCAS = std::make_unique<RCAS_Vk>("RCAS", InDevice, InPD);
        Magnifier = std::make_unique<Magnifier_Vk>("Magnifier", InDevice, InPD);
        UpscalerTime = std::make_unique<GpuTime_Vk>(InDevice, InPD);
    }

    return result;
}

bool IFeature_Vk::Evaluate(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters)
{
    // Frees deferred objects even when presents aren't hooked
    DeferredRelease::Collect();

    _subrectCopied = 0;

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

    auto upscaler = GetUpscalerType();
    bool useRcas = upscaler == Upscaler::XeSS ||
                   (upscaler == Upscaler::DLSS && Version() >= feature_version(2, 5, 1)) || upscaler == Upscaler::DLSSD;

    if (!useRcas)
        useRcas = Config::Instance()->RcasEnabled.value_or_default();

    if (_sharpness == 0.0f)
        useRcas = false;

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

    NVSDK_NGX_Resource_VK* paramOutput = nullptr;
    NVSDK_NGX_Resource_VK* paramMotion = nullptr;
    NVSDK_NGX_Resource_VK* paramDepth = nullptr;

    InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput);
    InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramMotion);
    InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth);

    // Save the original output so we can restore it later
    VkImageInfo originalOutput {};
    if (paramOutput)
    {
        originalOutput.Image = paramOutput->Resource.ImageViewInfo.Image;
        originalOutput.ImageView = paramOutput->Resource.ImageViewInfo.ImageView;
        originalOutput.SubresourceRange = paramOutput->Resource.ImageViewInfo.SubresourceRange;
        originalOutput.Format = paramOutput->Resource.ImageViewInfo.Format;
        originalOutput.Width = paramOutput->Resource.ImageViewInfo.Width;
        originalOutput.Height = paramOutput->Resource.ImageViewInfo.Height;
    }

    // The output base is needed before EvaluateInternal, which reads them again with this frame's render size.
    // Kept as a copy, the backend reads the zeroed base when we place the output.
    ReadSubrects(InParameters);
    const Subrect outputRect = GetSubrect(SubrectSlot::Output);

    const VkImageUsageFlags intermediateUsage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // Order is important as that's the order of shader dispatch
    std::vector<ShaderPass> pipeline;

    if (useOutputScaling)
    {
        pipeline.push_back(
            { // Setup
              [&](const VkImageInfo& nextOutput) -> VkImageInfo
              {
                  if (OutputScaler->CreateImageResource(Device, PhysicalDevice, TargetWidth(), TargetHeight(),
                                                        nextOutput.Format, intermediateUsage))
                  {
                      OutputScaler->SetImageLayout(InCmdBuffer, OutputScaler->GetImage(), VK_IMAGE_LAYOUT_UNDEFINED,
                                                   VK_IMAGE_LAYOUT_GENERAL, nextOutput.SubresourceRange);

                      // TODO: improve query of the output info
                      VkImageInfo info = nextOutput;
                      info.Image = OutputScaler->GetImage();
                      info.ImageView = OutputScaler->GetImageView();
                      info.Width = TargetWidth();
                      info.Height = TargetHeight();
                      return info;
                  }
                  return VkImageInfo {}; // Returns null handle
              },

              // Dispatch
              [&](const VkImageInfo& input, const VkImageInfo& output) -> bool
              {
                  LOG_DEBUG("Scaling output...");
                  OutputScaler->SetImageLayout(InCmdBuffer, input.Image, VK_IMAGE_LAYOUT_GENERAL,
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, input.SubresourceRange);

                  if (!OutputScaler->Dispatch(InCmdBuffer, input, output))
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
              [&](const VkImageInfo& nextOutput) -> VkImageInfo
              {
                  // Same checks as Dispatch, a skipped pass must not take over the output
                  if (paramMotion && paramOutput && RCAS->CanRender() &&
                      RCAS->CreateImageResource(Device, PhysicalDevice, nextOutput.Width, nextOutput.Height,
                                                nextOutput.Format, intermediateUsage))
                  {
                      // Disable any built-in sharpness shaders
                      InParameters->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
                      _actualSharpness = _sharpness;
                      _sharpness = 0.0f;

                      RCAS->SetImageLayout(InCmdBuffer, RCAS->GetImage(), VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_GENERAL, nextOutput.SubresourceRange);

                      VkImageInfo info = nextOutput;
                      info.Image = RCAS->GetImage();
                      info.ImageView = RCAS->GetImageView();
                      return info;
                  }
                  return VkImageInfo {};
              },

              // Dispatch
              [&](const VkImageInfo& input, const VkImageInfo& output) -> bool
              {
                  if (!RCAS->CanRender() || !paramMotion || !paramOutput)
                      return true;

                  RCAS->SetImageLayout(InCmdBuffer, input.Image, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, input.SubresourceRange);

                  RcasConstants rcasConstants {};
                  rcasConstants.Sharpness = _actualSharpness.value_or(_sharpness);
                  rcasConstants.DepthIsLinear = DepthLinear();
                  rcasConstants.DepthIsReversed = DepthInverted();
                  rcasConstants.IsHdr = IsHdr();

                  _sharpness = _actualSharpness.value_or(_sharpness);
                  _actualSharpness.reset();

                  InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &rcasConstants.MvScaleX);
                  InParameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &rcasConstants.MvScaleY);

                  float nearPlane = 0.0f;
                  float farPlane = 0.0f;

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
                  auto viewInput = [&](SubrectSlot slot, NVSDK_NGX_Resource_VK* resource)
                  {
                      return (_subrectCopied & (1u << (uint32_t) slot)) ? &_subrectViews[(size_t) slot]
                                                                        : SubrectInput(InCmdBuffer, resource, slot);
                  };

                  auto motion = viewInput(SubrectSlot::MV, paramMotion);
                  auto depth = paramDepth ? viewInput(SubrectSlot::Depth, paramDepth) : nullptr;

                  // In Vulkan we pass the Info structs instead of raw resources
                  VkImageInfo mvInfo = *(VkImageInfo*) &motion->Resource.ImageViewInfo;
                  VkImageInfo* depthInfo = depth ? (VkImageInfo*) &depth->Resource.ImageViewInfo : nullptr;

                  if (!RCAS->Dispatch(Device, InCmdBuffer, rcasConstants, input, mvInfo, output, depthInfo))
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
              [&](const VkImageInfo& nextOutput) -> VkImageInfo
              {
                  // Same checks as Dispatch, a skipped pass must not take over the output
                  if (paramMotion && paramOutput && Magnifier->CanRender() &&
                      Magnifier->CreateImageResource(Device, PhysicalDevice, nextOutput.Width, nextOutput.Height,
                                                     nextOutput.Format, intermediateUsage))
                  {
                      Magnifier->SetImageLayout(InCmdBuffer, Magnifier->GetImage(), VK_IMAGE_LAYOUT_UNDEFINED,
                                                VK_IMAGE_LAYOUT_GENERAL, nextOutput.SubresourceRange);

                      VkImageInfo info = nextOutput;
                      info.Image = Magnifier->GetImage();
                      info.ImageView = Magnifier->GetImageView();
                      return info;
                  }
                  return VkImageInfo {};
              },

              // Dispatch
              [&](const VkImageInfo& input, const VkImageInfo& output) -> bool
              {
                  if (!Magnifier->CanRender() || !paramMotion || !paramOutput)
                      return true;

                  Magnifier->SetImageLayout(InCmdBuffer, input.Image, VK_IMAGE_LAYOUT_GENERAL,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, input.SubresourceRange);

                  return Magnifier->Dispatch(InCmdBuffer, input, output);
              } });
    }

    bool placeOutput = false;

    if (paramOutput && PlacesOutput(outputRect, !pipeline.empty()) &&
        SubrectFits(SubrectSlot::Output, outputRect, originalOutput.Width, originalOutput.Height))
    {
        auto& slotCopy = SubrectCopy[(size_t) SubrectSlot::Output];

        if (slotCopy == nullptr)
            slotCopy = std::make_unique<ResourceCopy_Vk>("Subrect Copy", Device, PhysicalDevice);

        // By value, the lambdas run after this scope
        ResourceCopy_Vk* copy = slotCopy.get();

        if (copy->IsInit())
        {
            pipeline.push_back({ // Setup
                                 [&, copy](const VkImageInfo& nextOutput) -> VkImageInfo
                                 {
                                     const auto format = copy->BufferFormat(nextOutput.Format, false);

                                     // Exact, output scaling and RCAS size their work by it
                                     if (!copy->CreateImageResource(Device, PhysicalDevice, outputRect.Width,
                                                                    outputRect.Height, format, 0))
                                         return VkImageInfo {};

                                     VkImageInfo info = nextOutput;
                                     info.Image = copy->GetImage();
                                     info.ImageView = copy->GetImageView();
                                     info.SubresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                                     info.Format = format;
                                     info.Width = copy->ImageExtent().width;
                                     info.Height = copy->ImageExtent().height;

                                     copy->SetImageLayout(InCmdBuffer, info.Image, VK_IMAGE_LAYOUT_UNDEFINED,
                                                          VK_IMAGE_LAYOUT_GENERAL, info.SubresourceRange);

                                     placeOutput = true;
                                     return info;
                                 },

                                 // Dispatch
                                 [&, copy](const VkImageInfo& input, const VkImageInfo& output) -> bool
                                 {
                                     copy->SetImageLayout(InCmdBuffer, input.Image, VK_IMAGE_LAYOUT_GENERAL,
                                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                          input.SubresourceRange);

                                     return copy->Dispatch(Device, InCmdBuffer, input.ImageView, output.ImageView,
                                                           { outputRect.Width, outputRect.Height }, {},
                                                           { (int32_t) outputRect.X, (int32_t) outputRect.Y });
                                 } });
        }
    }

    // RCAS setup and DLSS zero the game's sharpness, restored after EvaluateInternal
    float originalSharpness = 0.0f;
    bool hasSharpness =
        InParameters->Get(NVSDK_NGX_Parameter_Sharpness, &originalSharpness) == NVSDK_NGX_Result_Success;

    // Iterate BACKWARDS to establish where each shader needs to pull its input from
    VkImageInfo currentTarget = originalOutput;
    for (auto it = pipeline.rbegin(); it != pipeline.rend(); ++it)
    {
        VkImageInfo requiredInput = it->Setup(currentTarget);
        if (requiredInput.Image != VK_NULL_HANDLE)
        {
            it->outputBuffer = currentTarget;
            it->inputBuffer = requiredInput;
            currentTarget = requiredInput; // Shift the target back for the next previous stage
        }
    }

    // Write target back into the params
    // In DX11/DX12 we set ngx param but in Vulkan we can set just the resource info
    if (paramOutput)
    {
        paramOutput->Resource.ImageViewInfo.Image = currentTarget.Image;
        paramOutput->Resource.ImageViewInfo.ImageView = currentTarget.ImageView;
        paramOutput->Resource.ImageViewInfo.SubresourceRange = currentTarget.SubresourceRange;
        paramOutput->Resource.ImageViewInfo.Format = currentTarget.Format;
        paramOutput->Resource.ImageViewInfo.Width = currentTarget.Width;
        paramOutput->Resource.ImageViewInfo.Height = currentTarget.Height;
    }

    if (placeOutput)
        SetSubrectBase(InParameters, SubrectSlot::Output, 0, 0);

    UpscalerTime->Start(InCmdBuffer);

    auto evalResult = EvaluateInternal(InCmdBuffer, InParameters);

    UpscalerTime->End(InCmdBuffer);

    // Restore original output pointer
    if (paramOutput)
    {
        paramOutput->Resource.ImageViewInfo.Image = originalOutput.Image;
        paramOutput->Resource.ImageViewInfo.ImageView = originalOutput.ImageView;
        paramOutput->Resource.ImageViewInfo.SubresourceRange = originalOutput.SubresourceRange;
        paramOutput->Resource.ImageViewInfo.Format = originalOutput.Format;
        paramOutput->Resource.ImageViewInfo.Width = originalOutput.Width;
        paramOutput->Resource.ImageViewInfo.Height = originalOutput.Height;
    }

    if (placeOutput)
        SetSubrectBase(InParameters, SubrectSlot::Output, outputRect.X, outputRect.Y);

    if (hasSharpness)
        InParameters->Set(NVSDK_NGX_Parameter_Sharpness, originalSharpness);

    if (!evalResult)
        return false;

    // Iterate FORWARDS to execute the shaders in the defined order
    for (auto& pass : pipeline)
    {
        if (pass.inputBuffer.Image != VK_NULL_HANDLE && pass.outputBuffer.Image != VK_NULL_HANDLE)
        {
            // Upscale succeeded, a failed pass only drops the remaining passes
            if (!pass.Dispatch(pass.inputBuffer, pass.outputBuffer))
                break;
        }
    }

    _frameCount++;

    return evalResult;
}

std::optional<double> IFeature_Vk::ReadUpscalerTime(void* unused)
{
    if (!UpscalerTime || !RCAS || !OutputScaler)
        return std::nullopt;

    lastUpscalerTime = UpscalerTime->ReadGpuTime();
    lastRcasTime = RCAS->ReadGpuTime();
    lastOutputScalingTime = OutputScaler->ReadGpuTime();

    return sumOpts(lastUpscalerTime, lastRcasTime, lastOutputScalingTime);
}

void IFeature_Vk::ReadDetailedGpuTimes(void* unused, std::vector<DetailedGpuTime>& detailedGpuTimes)
{
    detailedGpuTimes.clear();

    // Do not call ReadGpuTime twice for shaders
    if (lastUpscalerTime)
        detailedGpuTimes.emplace_back(DetailedGpuTime { ShortName(), lastUpscalerTime.value(), true });

    if (lastRcasTime)
        detailedGpuTimes.emplace_back(DetailedGpuTime { RCAS->Name(), lastRcasTime.value(), true });

    if (lastOutputScalingTime)
        detailedGpuTimes.emplace_back(DetailedGpuTime { OutputScaler->Name(), lastOutputScalingTime.value(), true });

    if (!Magnifier)
        return;

    auto magnifierTime = Magnifier->ReadGpuTime();

    if (magnifierTime)
        detailedGpuTimes.emplace_back(DetailedGpuTime { Magnifier->Name(), magnifierTime.value(), false });
}
