#include "pch.h"

#include "DlssNr.h"
#include "DlssNr_PipelineUi.h"
#include "DlssNr_Upscaler.h"
#include "DlssNr_MenuSections.h"
#include "DlssNr_Placement.h"
#include <Config.h>
#include <menu/menu_common.h>
#include <algorithm>
#include <cmath>

namespace DlssNr
{

void RenderMenu(Config* config, float menuResScale)
{
    using namespace MenuSections;
    ImGui::Spacing();
    if (auto header = ScopedCollapsingHeader("DLSS Neural Rendering"); header.IsHeaderOpen())
    {
        ScopedIndent indent {};
        const float toggleGap = ImGui::GetStyle().ItemSpacing.x;
        const float toggleWidth = (ImGui::GetContentRegionAvail().x - toggleGap) * 0.5f;
        const float toggleRight = ImGui::GetCursorPosX() + toggleWidth + toggleGap;
        bool enabled = config->DlssNrEnabled.value_or_default();
        if (PipelineUi::CheckboxWrapped("Enable Neural Rendering", &enabled, toggleWidth))
            config->DlssNrEnabled = enabled;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Enable NR processing.");

        bool applyModel = config->DlssNrApplyModel.value_or_default();
        if (PipelineUi::CheckboxWrapped("Apply model", &applyModel, toggleWidth))
            config->DlssNrApplyModel = applyModel;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show or hide the NR effect. The model still runs when hidden.\nDisable Enable Neural "
                              "Rendering to stop its GPU cost.");

        const auto feature = State::Instance().currentFeature;
        const bool rayReconstruction = feature && feature->GetUpscalerType() == Upscaler::DLSSD;
        bool finished = config->DlssNrFinishedPicture.value_or_default();
        auto placement = ResolvePlacement(config->DlssNrRunBeforeSr.value_or_default(),
                                          config->DlssNrDeferredDlss.value_or_default(),
                                          config->DlssNrResidualAcrossRr.value_or_default(), finished);
        bool generateBefore = placement.beforeUpscale;
        ImGui::SameLine(toggleRight);
        ImGui::BeginDisabled(placement.deferred);
        if (PipelineUi::CheckboxWrapped("Generate model before upscale", &generateBefore, toggleWidth))
        {
            config->DlssNrRunBeforeSr = generateBefore;
            config->DlssNrResidualAcrossRr = false;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(placement.deferred ? "The separate-edit path always generates before upscale."
                                               : "Run NR before the game's upscaler, including RR.");

        if (PipelineUi::CheckboxWrapped("Apply NR to the finished picture", &finished, toggleWidth))
        {
            config->DlssNrFinishedPicture = finished;
            DlssNr::RetryAfterFailure();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Apply NR after game effects and HUD. Early generation carries the edit through a separate upscaler.");

        placement = ResolvePlacement(config->DlssNrRunBeforeSr.value_or_default(),
                                     config->DlssNrDeferredDlss.value_or_default(),
                                     config->DlssNrResidualAcrossRr.value_or_default(), finished);
        ImGui::SameLine(toggleRight);
        bool deferred = placement.deferred;
        if (PipelineUi::CheckboxWrapped("Generate before upscale, apply after upscale", &deferred, toggleWidth))
        {
            config->DlssNrDeferredDlss = deferred;
            config->DlssNrResidualAcrossRr = false; // Clear the legacy alias when the unified option changes.
            if (deferred || finished)
                config->DlssNrRunBeforeSr = deferred;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Keep the game's SR/RR input clean and upscale only the NR edit with a separate non-RR backend."
                              "\nApply after upscale, or at presentation when finished-picture mode is enabled.");
        ImGui::Spacing();

        placement = ResolvePlacement(config->DlssNrRunBeforeSr.value_or_default(),
                                     config->DlssNrDeferredDlss.value_or_default(),
                                     config->DlssNrResidualAcrossRr.value_or_default(), finished);
        const bool nativePrivateVk = feature && feature->Api() == API::Vulkan && !feature->IsWithDx12();
        if (placement.deferred && !nativePrivateVk)
        {
            int backend = (int) GetPrivateUpscaler(config->DlssNrPrivateUpscaler.value_or_default());
            if (ImGui::Combo("Private NR upscaler", &backend, "DLSS\0FSR 2.2\0FSR (FidelityFX)\0XeSS\0"))
                config->DlssNrPrivateUpscaler = backend;
            HelpMarker("Upscales only the NR edit, with or without game RR. FSR (FidelityFX) and XeSS need their runtimes.");
        }

        PipelineUi::View view;
        view.privateUpscaler = PrivateUpscalerName(GetPrivateUpscaler(config->DlssNrPrivateUpscaler.value_or_default()));
        view.enabled = enabled;
        view.applyModel = config->DlssNrApplyModel.value_or_default();
        view.passes = config->DlssNrPasses.value_or_default();
        view.scalePercent = (int) lroundf(config->DlssNrWorkingScale.value_or_default() * 100.0f);
        view.rayReconstruction = rayReconstruction;
        if (finished)
            view.route = placement.deferred ? PipelineUi::Route::FinishedBefore : PipelineUi::Route::Finished;
        else if (placement.deferred)
            view.route = PipelineUi::Route::Deferred;
        else
            view.route = placement.beforeUpscale ? PipelineUi::Route::Before : PipelineUi::Route::After;

        static PipelineUi::Section selected = PipelineUi::Section::Placement;
        ImGui::Separator();
        PipelineUi::Draw(view, selected);
        ImGui::Separator();
        ImGui::Spacing();
        RenderStatus(config, menuResScale);
        ImGui::SeparatorText(PipelineUi::SectionName(selected));
        ImGui::PushItemWidth(std::min(220.0f * menuResScale, ImGui::GetContentRegionAvail().x * 0.42f));
        switch (selected)
        {
        case PipelineUi::Section::Placement:
            RenderPlacement(config, menuResScale);
            break;
        case PipelineUi::Section::Input:
            RenderInput(config, menuResScale);
            break;
        case PipelineUi::Section::Model:
            RenderModel(config, menuResScale);
            break;
        case PipelineUi::Section::Blend:
            RenderBlend(config, menuResScale);
            break;
        }
        ImGui::PopItemWidth();
        if (ImGui::CollapsingHeader("Inspect NR"))
        {
            ImGui::PushItemWidth(std::min(220.0f * menuResScale, ImGui::GetContentRegionAvail().x * 0.42f));
            RenderInspect(config, menuResScale);
            ImGui::PopItemWidth();
        }
    }
}

} // namespace DlssNr
