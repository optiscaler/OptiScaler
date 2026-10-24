#include "pch.h"
#include "Upscaler_Inputs_VkwDx12.h"
#include "Upscaler_Inputs.h"
#include "VkwDx12_Inputs.h"

#include <State.h>
#include <Config.h>

#include <framegen/IFGFeature_Dx12.h>
#include <with_dx12/vk_with_dx12_sc.h>

// Tags the upscaler's input, copied to the frame's shared texture with the upscaler's command buffer. Upscalers read
// their inputs as shader resources.
static bool SetInput(VkCommandBuffer cmdBuffer, NVSDK_NGX_Parameter* parameters, const char* name, FG_ResourceType type,
                     uint32_t width, uint32_t height)
{
    NVSDK_NGX_Resource_VK* ngxResource = nullptr;

    if (parameters->Get(name, (void**) &ngxResource) != NVSDK_NGX_Result_Success || ngxResource == nullptr)
        return false;

    auto& info = ngxResource->Resource.ImageViewInfo;

    if (info.Image == VK_NULL_HANDLE || info.ImageView == VK_NULL_HANDLE || info.Width == 0 || info.Height == 0)
        return false;

    VkFGResource input {};
    input.type = type;
    input.image = info.Image;
    input.view = info.ImageView;
    input.format = info.Format;
    input.imageWidth = info.Width;
    input.imageHeight = info.Height;
    input.width = width;
    input.height = height;
    input.cmdBuffer = cmdBuffer;
    input.validity = FG_ResourceValidity::ValidNow;

    return VkwDx12Inputs::SetResource(input);
}

void UpscalerInputsVkwDx12::UpscaleStart(VkCommandBuffer cmdBuffer, NVSDK_NGX_Parameter* parameters,
                                         IFeature_Vk* feature)
{
    auto& state = State::Instance();
    auto fg = state.currentFG;

    if (fg == nullptr || state.activeFgInput != FGInput::Upscaler ||
        state.swapchainInteropApi != SwapchainInteropApi::VkwDx12)
    {
        return;
    }

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    ID3D12Device* dx12Device = nullptr;

    if (!VkwDx12SC::InteropDevices(device, physicalDevice, dx12Device))
        return;

    auto constants = UpscalerInputs::GetFGConstants(feature);
    fg->EvaluateState(dx12Device, constants);

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

    if (state.isShuttingDown || !fg->IsActive() || !Config::Instance()->IsFGEnabled() ||
        state.currentSwapchain == nullptr)
    {
        return;
    }

    const auto mvWidth = feature->LowResMV() ? feature->RenderWidth() : feature->TargetWidth();
    const auto mvHeight = feature->LowResMV() ? feature->RenderHeight() : feature->TargetHeight();

    SetInput(cmdBuffer, parameters, NVSDK_NGX_Parameter_MotionVectors, FG_ResourceType::Velocity, mvWidth, mvHeight);
    SetInput(cmdBuffer, parameters, NVSDK_NGX_Parameter_Depth, FG_ResourceType::Depth, feature->RenderWidth(),
             feature->RenderHeight());

    LOG_DEBUG("(FG VkwDx12) inputs copied to slot {}, frame: {}", fg->GetIndex(), fg->FrameCount());
}

void UpscalerInputsVkwDx12::Shutdown() { VkwDx12Inputs::Shutdown(); }
