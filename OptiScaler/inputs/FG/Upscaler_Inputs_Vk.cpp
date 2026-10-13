#include "pch.h"
#include "Upscaler_Inputs_Vk.h"

#include <State.h>
#include <Config.h>
#include <MathUtils.h>

#include <nvsdk_ngx_vk.h>

using namespace OptiMath;

// Same camera values as the DX12 OptiFG
static void SetCameraValues(IFGFeature* fg, const NVSDK_NGX_Parameter& parameters, IFeature* feature)
{
    auto& cfg = *Config::Instance();
    const bool useFsrInputs = cfg.FsrUseFsrInputValues.value_or_default();

    float cameraNear = 0.0f;
    float cameraFar = 0.0f;
    float cameraVFov = 0.0f;
    float meterFactor = 0.0f;

    parameters.Get(OptiKeys::FSR_NearPlane, &cameraNear);
    parameters.Get(OptiKeys::FSR_FarPlane, &cameraFar);

    if (!useFsrInputs || (cameraNear == 0.0f && cameraFar == 0.0f))
    {
        cameraNear = cfg.FsrCameraNear.value_or_default();
        cameraFar = cfg.FsrCameraFar.value_or_default();

        if (feature->DepthInverted())
            std::swap(cameraNear, cameraFar);
    }

    if (!useFsrInputs || parameters.Get(OptiKeys::FSR_CameraFovVertical, &cameraVFov) != NVSDK_NGX_Result_Success)
    {
        if (cfg.FsrVerticalFov.has_value())
            cameraVFov = GetRadiansFromDeg(cfg.FsrVerticalFov.value());
        else if (cfg.FsrHorizontalFov.value_or_default() > 0.0f)
            cameraVFov = GetVerticalFovFromHorizontal(GetRadiansFromDeg(cfg.FsrHorizontalFov.value()),
                                                      (float) feature->TargetWidth(), (float) feature->TargetHeight());
        else
            cameraVFov = GetRadiansFromDeg(60);
    }

    if (!useFsrInputs)
        parameters.Get(OptiKeys::FSR_ViewSpaceToMetersFactor, &meterFactor);

    State::Instance().lastFsrCameraNear = cameraNear;
    State::Instance().lastFsrCameraFar = cameraFar;

    auto aspectRatio = (float) feature->DisplayWidth() / (float) feature->DisplayHeight();
    fg->SetCameraValues(cameraNear, cameraFar, cameraVFov, aspectRatio, meterFactor);
}

static void SetResource(IFGFeature_Vk* fg, NVSDK_NGX_Parameter* parameters, const char* name, FG_ResourceType type,
                        VkCommandBuffer cmdBuffer, uint32_t width, uint32_t height)
{
    NVSDK_NGX_Resource_VK* ngxResource = nullptr;

    if (parameters->Get(name, (void**) &ngxResource) != NVSDK_NGX_Result_Success || ngxResource == nullptr)
        return;

    auto& info = ngxResource->Resource.ImageViewInfo;

    VkFGResource resource {};
    resource.type = type;
    resource.image = info.Image;
    resource.format = info.Format;
    // Upscalers read their inputs as shader resources
    resource.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    resource.imageWidth = info.Width;
    resource.imageHeight = info.Height;
    resource.width = width;
    resource.height = height;
    resource.cmdBuffer = cmdBuffer;
    resource.validity = FG_ResourceValidity::ValidNow;

    fg->SetResource(&resource);
}

void UpscalerInputsVk::UpscaleStart(VkCommandBuffer cmdBuffer, NVSDK_NGX_Parameter* parameters, IFeature_Vk* feature)
{
    auto& state = State::Instance();
    auto fg = state.currentVkFG;

    if (fg == nullptr || state.activeFgInput != FGInput::Upscaler || !fg->HasSwapchain())
        return;

    auto config = Config::Instance();

    FG_Constants constants {};
    constants.displayWidth = feature->DisplayWidth();
    constants.displayHeight = feature->DisplayHeight();

    if (feature->IsHdr())
        constants.flags |= FG_Flags::Hdr;

    if (feature->DepthInverted())
        constants.flags |= FG_Flags::InvertedDepth;

    if (feature->JitteredMV())
        constants.flags |= FG_Flags::JitteredMVs;

    if (!feature->LowResMV())
        constants.flags |= FG_Flags::DisplayResolutionMVs;

    if (config->FGAsync.value_or_default())
        constants.flags |= FG_Flags::Async;

    fg->EvaluateState(constants);

    // Present dispatches the previous frame's data
    if (fg->Mutex.getOwner() == 2)
    {
        LOG_TRACE("Waiting for present!");
        fg->Mutex.lock(4);
        fg->Mutex.unlockThis(4);
    }

    fg->StartNewFrame();

    int reset = 0;
    float mvScaleX = 0.0f;
    float mvScaleY = 0.0f;
    float jitterX = 0.0f;
    float jitterY = 0.0f;

    parameters->Get(NVSDK_NGX_Parameter_Reset, &reset);
    parameters->Get(NVSDK_NGX_Parameter_MV_Scale_X, &mvScaleX);
    parameters->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &mvScaleY);
    parameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &jitterX);
    parameters->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &jitterY);

    SetCameraValues(fg, *parameters, feature);
    fg->SetFrameTimeDelta(state.lastFGFrameTime);
    fg->SetMVScale(mvScaleX, mvScaleY);
    fg->SetJitter(jitterX, jitterY);
    fg->SetReset(reset);
    fg->SetInterpolationRect(feature->DisplayWidth(), feature->DisplayHeight());

    if (!config->IsFGEnabled())
        return;

    if (!fg->IsActive() && !fg->IsPaused())
        fg->Activate();

    auto mvWidth = feature->LowResMV() ? feature->RenderWidth() : feature->TargetWidth();
    auto mvHeight = feature->LowResMV() ? feature->RenderHeight() : feature->TargetHeight();

    SetResource(fg, parameters, NVSDK_NGX_Parameter_MotionVectors, FG_ResourceType::Velocity, cmdBuffer, mvWidth,
                mvHeight);
    SetResource(fg, parameters, NVSDK_NGX_Parameter_Depth, FG_ResourceType::Depth, cmdBuffer, feature->RenderWidth(),
                feature->RenderHeight());
}
