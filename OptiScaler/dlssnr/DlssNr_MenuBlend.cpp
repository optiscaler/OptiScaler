#include "pch.h"

#include "DlssNr_MenuSections.h"
#include <Config.h>
#include <menu/menu_common.h>
#include <algorithm>

namespace DlssNr::MenuSections
{

void RenderBlend(Config* config, float menuResScale)
{
    if (config->DlssNrFinishedPicture.value_or_default() &&
        (config->DlssNrRunBeforeSr.value_or_default() || config->DlssNrDeferredDlss.value_or_default()))
    {
        const auto feature = State::Instance().currentFeature;
        ImGui::BeginDisabled(State::Instance().swapchainApi == API::Vulkan ||
                             (feature && feature->GetUpscalerType() == Upscaler::DLSSD));
        bool hdrTransfer = config->DlssNrHdrTransfer.value_or_default();
        if (ImGui::Checkbox("Match HDR brightness response (experimental)", &hdrTransfer))
            config->DlssNrHdrTransfer = hdrTransfer;
        ImGui::EndDisabled();
        HelpMarker("Match early NR brightness changes to the finished HDR image. Adds GPU work; unreliable fits fall back.");
    }
    float transfer = config->DlssNrTransferStrength.value_or_default();
    if (ImGui::SliderFloat("Detail strength", &transfer, 0.0f, 2.0f, "%.2f"))
        config->DlssNrTransferStrength = transfer;

    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##detail"))
        config->DlssNrTransferStrength = 1.0f;

    HelpMarker("0 = no detail change. 1 = normal.");

    float colour = config->DlssNrColourStrength.value_or_default();
    if (ImGui::SliderFloat("Colour strength", &colour, 0.0f, 4.0f, "%.2f"))
        config->DlssNrColourStrength = colour;

    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##colour"))
        config->DlssNrColourStrength = 1.0f;

    HelpMarker("0 = game colours. 1 = model colours. Above 1 boosts saturation.");

    if (ImGui::TreeNode("Skin and environment (final edit)"))
    {
        bool filter = config->DlssNrSkinProtection.value_or_default();
        if (ImGui::Checkbox("Separate skin / environment controls", &filter))
            config->DlssNrSkinProtection = filter;
        ImGui::BeginDisabled(!filter);
        bool tone = config->DlssNrSkinToneEnabled.value_or_default();
        if (ImGui::Checkbox("Allow skin tone / colour changes", &tone))
            config->DlssNrSkinToneEnabled = tone;
        HelpMarker("Allow skin colour changes while retaining separate detail control.");
        const auto slider = [](const char* label, auto& option)
        {
            float v = option.value_or_default();
            if (ImGui::SliderFloat(label, &v, 0.0f, 1.0f, "%.2f"))
                option = v;
            HelpMarker("0 = unchanged. 1 = full effect.");
        };
        slider("Skin detail / lighting", config->DlssNrSkinDetail);
        ImGui::BeginDisabled(!tone);
        slider("Skin colour", config->DlssNrSkinColour);
        ImGui::EndDisabled();
        slider("Environment detail / lighting", config->DlssNrEnvironmentDetail);
        slider("Environment colour", config->DlssNrEnvironmentColour);
        bool preview = config->DlssNrShowSkinMask.value_or_default();
        if (ImGui::Checkbox("Preview colour-based mask", &preview))
            config->DlssNrShowSkinMask = preview;
        ImGui::EndDisabled();
        ImGui::TreePop();
    }

    // Highlight guard, directly under the white point / trim -- it bounds the model's edit and
    // belongs with the exposure controls it works alongside.
    float maxRatio = config->DlssNrMaxRatio.value_or_default();
    if (ImGui::SliderFloat("Highlight guard", &maxRatio, 1.0f, 8.0f, "%.1fx"))
        config->DlssNrMaxRatio = maxRatio;

    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##guard"))
        config->DlssNrMaxRatio = 2.0f;

    HelpMarker("Limit pixel brightening and darkening.");
}

void RenderInspect(Config* config, float menuResScale)
{
    bool held = config->DlssNrHoldFrame.value_or_default();
    if (ImGui::Checkbox("Hold frame", &held))
        config->DlssNrHoldFrame = held;

    HelpMarker("Freeze a frame for NR tuning. Later game effects may update; temporal behaviour is not representative.");

    static const char* compareNames[] = { "Off", "Side by side", "Wipe" };
    int compare = (int) config->DlssNrCompare.value_or_default();
    if (ImGui::Combo("Compare", &compare, compareNames, IM_ARRAYSIZE(compareNames)))
        config->DlssNrCompare = (uint32_t) compare;

    HelpMarker("Compare the original and NR output.");

    if (compare != 0)
    {
        bool swap = config->DlssNrCompareSwap.value_or_default();
        if (ImGui::Checkbox("Swap sides", &swap))
            config->DlssNrCompareSwap = swap;


        bool tags = config->DlssNrCompareTags.value_or_default();
        if (ImGui::Checkbox("Label the sides", &tags))
            config->DlssNrCompareTags = tags;



        if (tags)
        {
            float tagScale = config->DlssNrTagScale.value_or_default();
            if (ImGui::SliderFloat("Label size", &tagScale, 0.5f, 5.0f, "%.1fx"))
                config->DlssNrTagScale = std::clamp(tagScale, 0.5f, 5.0f);
        }
    }

    if (compare == 1)
    {
        float zoom = config->DlssNrCompareZoom.value_or_default();
        if (ImGui::SliderFloat("Zoom", &zoom, 1.0f, 2.0f, "%.2f"))
            config->DlssNrCompareZoom = std::clamp(zoom, 1.0f, 2.0f);

        HelpMarker("1 = fit. 2 = crop and enlarge.");
    }

    if (compare == 2)
    {
        float split = config->DlssNrCompareSplit.value_or_default();
        if (ImGui::SliderFloat("Split", &split, 0.0f, 1.0f, "%.2f"))
            config->DlssNrCompareSplit = std::clamp(split, 0.0f, 1.0f);

        HelpMarker("Move the comparison boundary.");
    }

    static const char* debugNames[] = { "Off", "Proxy (what the model sees)", "Model output (raw)",
                                        "Difference (amplified)" };
    int debugView = (int) config->DlssNrDebugView.value_or_default();
    if (ImGui::Combo("Debug view", &debugView, debugNames, IM_ARRAYSIZE(debugNames)))
        config->DlssNrDebugView = (uint32_t) debugView;

    HelpMarker("Difference is amplified 20x. Grey means unchanged.");
}

} // namespace DlssNr::MenuSections
