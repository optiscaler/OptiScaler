#include "pch.h"

#include "DlssNr.h"
#include "DlssNrFeature_Vk.h"
#include "DlssNrFinished_Vk.h"
#include "DlssNr_MenuSections.h"
#include "DlssNr_Placement.h"
#include "DlssNr_PipelineUi.h"
#include <Config.h>
#include <menu/menu_common.h>

namespace DlssNr::MenuSections
{

void RenderPlacement(Config* config, float menuResScale)
{
    const bool enabled = config->DlssNrEnabled.value_or_default();
    const bool finishedPicture = config->DlssNrFinishedPicture.value_or_default();
    if (finishedPicture && enabled)
    {
        const auto feature = State::Instance().currentFeature;
        if (State::Instance().swapchainApi == API::Vulkan)
            ImGui::TextWrapped("%s", DlssNr::FinishedVkStatus().c_str());
        else if (feature && (feature->Api() != API::DX12 ||
                             (feature->IsWithDx12() && State::Instance().swapchainApi != API::DX11 &&
                              State::Instance().swapchainInteropApi != SwapchainInteropApi::Dx11wDx12)))
            ImGui::TextWrapped("This option needs DirectX 12 or a DirectX 11 upscaler marked w/Dx12.");
        else
            ImGui::TextWrapped("%s", DlssNr::FinishedPictureStatus().c_str());
    }

    const auto placement = ResolvePlacement(config->DlssNrRunBeforeSr.value_or_default(),
                                            config->DlssNrDeferredDlss.value_or_default(),
                                            config->DlssNrResidualAcrossRr.value_or_default(), finishedPicture);
    if (placement.deferred)
    {
        ImGui::TextWrapped("Private upscale: %s", DlssNr::DeferredDlssStatus().c_str());
        ImGui::TextWrapped(finishedPicture
            ? "The game processes clean input through SR/RR and its effects. The separately upscaled NR edit is applied to the finished picture."
            : "The game processes clean input through SR/RR. The separately upscaled NR edit is applied after upscale.");
    }

}

void RenderStatus(Config* config, float menuResScale)
{
    const bool enabled = config->DlssNrEnabled.value_or_default();
    const bool finishedPicture = config->DlssNrFinishedPicture.value_or_default();
    // Either backend. The two keep separate state, and on a native Vulkan game the D3D12 side
    // is never touched -- so asking only that one reports "waiting for the upscaler" over a pass
    // that is demonstrably running.
    const bool vulkan = DlssNr::IsRunningVk();

    // Turning the pass off does not release the model, so the feature handle stays alive and
    // IsRunning keeps answering yes. Reporting a cost from that was wrong in the way that matters
    // most: the toggle is how anyone A/Bs this, so the one moment the number is read is the one
    // moment it describes the frame before last.
    if (!enabled)
    {
        ImGui::TextDisabled("NR off.");
    }
    else if (!DlssNr::IsRunning() && !vulkan)
    {
        const auto feature = State::Instance().currentFeature;
        const bool nativeVk = feature && feature->Api() == API::Vulkan && !feature->IsWithDx12();
        const char* reason = nativeVk ? DlssNr::FailureReasonVk() : DlssNr::FailureReason();

        if (reason[0] != 0)
        {
            ImGui::TextWrapped("%s", reason);
            ImGui::SameLine();

            if (nativeVk)
                ImGui::TextUnformatted("Restart the game to retry native Vulkan NR.");
            else if (ImGui::SmallButton("Retry"))
                DlssNr::RetryAfterFailure();
        }
        else if (feature && feature->Api() == API::DX11 && !feature->IsWithDx12())
        {
            ImGui::TextWrapped("NR needs the D3D12 bridge on D3D11. Choose an upscaler marked w/Dx12 and restart.");
        }
        else if (nativeVk && ResolvePlacement(config->DlssNrRunBeforeSr.value_or_default(),
                     config->DlssNrDeferredDlss.value_or_default(),
                     config->DlssNrResidualAcrossRr.value_or_default(), finishedPicture).deferred)
        {
            ImGui::TextWrapped("The private edit-upscale path requires DirectX 12 or its bridge. Disable separate edit upscaling to use native Vulkan NR.");
        }
        else if (enabled)
            ImGui::TextUnformatted("Waiting for the upscaler to run.");
    }
    else
    {
        // The elapsed time belongs here rather than only in the upscaler's breakdown: that tooltip needs
        // OptiScaler's own upscaler to have run, and with native DLSS passing through there is
        // nothing in it to hang this off.
        // Either backend's timer. They measure the same thing by different means, and only one
        // of them is running.
        const auto ms = vulkan ? DlssNr::LastGpuTimeVk() : DlssNr::LastGpuTime();

        // With "Apply the model" off the pass STILL RUNS (so Hold-frame A/B can toggle its edit on
        // a frozen frame) -- it only outputs the clean frame.
        // Enable Neural Rendering off stops the work.
        const char* runSuffix = !config->DlssNrApplyModel.value_or_default() ? "  (model running, edit hidden)" : "";

        // Keep the running indicator green, using the theme's HDR-adjusted text brightness.
        const auto textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(textColor.x * 0.55f, textColor.y * 0.80f, textColor.z * 0.55f, textColor.w));
        if (ms.has_value())
            ImGui::Text("Running%s - %.2f ms elapsed%s", vulkan ? " natively on Vulkan" : "", ms.value(), runSuffix);
        else if (vulkan)
            // Measured but not yet read: the first few frames are still in the query ring.
            ImGui::Text("Running natively on Vulkan - %llu frames%s", DlssNr::FramesVk(), runSuffix);
        else
            ImGui::Text("Running.%s", runSuffix);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Time between the start and end of NR on the GPU, including delays while other work "
                              "runs.\nCompare FPS to check the effect on game performance.");

        if (ms.has_value())
        {
            const auto& state = State::Instance();
            // The FG swapchain interval is between real game frames, not interpolated presents.
            // Native Vulkan has no DXGI timing, so use the existing overlay frame interval there.
            const double frameMs = state.swapchainApi == API::Vulkan
                                       ? (state.frameTimes.empty() ? 0.0 : state.frameTimes.back())
                                   : state.currentFG ? state.lastFGFrameTime
                                                     : state.presentFrameTime;
            PipelineUi::DrawTimingBar(ms.value(), frameMs);
        }
    }
}

} // namespace DlssNr::MenuSections
