#include "pch.h"
#include "Upscaler_Inputs_Vk.h"
#include "Upscaler_Inputs.h"

#include <State.h>
#include <Config.h>
#include <MathUtils.h>
#include <hudfix/Hudfix_Vk.h>
#include <hooks/VulkanwDx12_Hooks.h>

#include <nvsdk_ngx_vk.h>

using namespace OptiMath;

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

    auto constants = UpscalerInputs::GetFGConstants(feature);
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

    auto camera = UpscalerInputs::GetCameraValues(*parameters, feature);
    auto aspectRatio = (float) feature->DisplayWidth() / (float) feature->DisplayHeight();
    fg->SetCameraValues(camera.cameraNear, camera.cameraFar, camera.cameraVFov, aspectRatio, camera.meterFactor);
    fg->SetFrameTimeDelta(state.lastFGFrameTime);
    fg->SetMVScale(mvScaleX, mvScaleY);
    fg->SetJitter(jitterX, jitterY);
    fg->SetReset(reset);
    fg->SetInterpolationRect(feature->DisplayWidth(), feature->DisplayHeight());

    if (!Config::Instance()->IsFGEnabled())
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

void UpscalerInputsVk::UpscaleEnd(VkCommandBuffer cmdBuffer, const NVSDK_NGX_Resource_VK* output)
{
    auto& state = State::Instance();

    if (state.currentVkFG == nullptr || state.activeFgInput != FGInput::Upscaler)
        return;

    Hudfix_Vk::UpscaleEnd(cmdBuffer);

    // The upscaled image is the first hudless candidate, NGX outputs are storage images
    if (output != nullptr)
    {
        // Upscaling through DX12 continues the game's recording in a command buffer submitted after the upscale
        if (auto virtualCmdBuffer = Vulkan_wDx12::GetVirtualCommandBuffer(cmdBuffer);
            virtualCmdBuffer != VK_NULL_HANDLE)
            cmdBuffer = virtualCmdBuffer;

        auto& info = output->Resource.ImageViewInfo;
        Hudfix_Vk::CheckImage(cmdBuffer, info.Image, info.Format, { info.Width, info.Height }, VK_IMAGE_LAYOUT_GENERAL);
    }
}
