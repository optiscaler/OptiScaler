#include "pch.h"

#include "DlssNr_MenuSections.h"
#include <Config.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>

namespace DlssNr::MenuSections
{

// Model tuning rebuilds the feature; commit slider changes only on release.
template <typename Option>
static bool DeferredSlider(const char* label, Option* opt, float mn, float mx, float def, const char* fmt = "%.2f",
                           bool inheritReset = false)
{
    static std::unordered_map<ImGuiID, float> pending;
    const ImGuiID id = ImGui::GetID(label);

    auto it = pending.find(id);
    float value = it != pending.end() ? it->second : (opt->has_value() ? opt->value() : def);
    bool changed = false;

    if (ImGui::SliderFloat(label, &value, mn, mx, fmt))
        pending[id] = value;

    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        auto committed = pending.find(id);

        if (committed != pending.end())
        {
            *opt = std::clamp(committed->second, mn, mx);
            pending.erase(committed);
            changed = true;
        }
    }

    ImGui::SameLine();

    const std::string resetId = std::string("Reset##") + label;
    if (ImGui::SmallButton(resetId.c_str()))
    {
        if (inheritReset)
            *opt = std::optional<float> {};
        else
            *opt = def;
        pending.erase(id);
        changed = true;
    }

    if (std::strcmp(label, "Intensity") == 0)
        HelpMarker("Enhancement strength. 1 = default.");
    else if (std::strcmp(label, "Local structure") == 0)
        HelpMarker("Fine detail and local contrast. 1 = default.");
    else if (std::strcmp(label, "Local tone") == 0)
        HelpMarker("Broad lighting changes. Later passes default to 0.");
    else if (std::strcmp(label, "Skin structure") == 0)
        HelpMarker("Skin detail. -1 follows Local structure.");
    return changed;
}

// An absent later-pass setting inherits pass 1. The first combo item represents that absence; the
// remaining items map directly to the model's zero-based profile values.
static bool InheritedProfileCombo(const char* label, CustomOptional<uint32_t, NoDefault>* opt, const char* const* names,
                                  int nameCount)
{
    int selected = 0;

    if (opt->has_value())
        selected = std::clamp((int) opt->value(), 0, nameCount - 2) + 1;

    if (!ImGui::Combo(label, &selected, names, nameCount))
        return false;

    if (selected == 0)
        *opt = std::optional<uint32_t> {};
    else
        *opt = (uint32_t) (selected - 1);

    return true;
}

void RenderModel(Config* config, float menuResScale)
{
    // Keep the UI simple; advanced INI pass settings remain available.
    constexpr int menuPassLimit = 2;
    {
        int passes = (int) std::clamp(config->DlssNrPasses.value_or_default(), 1u, (unsigned int) menuPassLimit);
        const auto text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        const float brightness = std::max({ text.x, text.y, text.z });
        const auto passColour = [&](int count)
        {
            return count == 1 ? ImVec4(brightness * 0.35f, brightness * 0.75f, brightness * 0.45f, text.w)
                              : ImVec4(brightness * 0.80f, brightness * 0.35f, brightness * 0.32f, text.w);
        };
        ImGui::PushStyleColor(ImGuiCol_Text, passColour(passes));
        const bool open = ImGui::BeginCombo("##Model passes", passes == 1 ? "1" : "2");
        ImGui::PopStyleColor();
        if (open)
        {
            for (int count = 1; count <= menuPassLimit; ++count)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, passColour(count));
                if (ImGui::Selectable(count == 1 ? "1" : "2", passes == count))
                    config->DlssNrPasses = (uint32_t) count;
                ImGui::PopStyleColor();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Model passes");
    }

    static const char* styles[] = { "Standard", "Natural", "Cinematic" };
    static const char* inheritedStyles[] = { "Auto", "Standard", "Natural", "Cinematic" };

    if (ImGui::TreeNodeEx("Pass 1", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int style = (int) std::min(config->DlssNrStyle.value_or_default(), 2u);
        if (ImGui::Combo("Style", &style, styles, IM_ARRAYSIZE(styles)))
            config->DlssNrStyle = (uint32_t) style;

        DeferredSlider("Intensity", &config->DlssNrIntensity, 0.0f, 2.0f, 1.0f);
        DeferredSlider("Local structure", &config->DlssNrLocalStructure, 0.0f, 2.0f, 1.0f);
        DeferredSlider("Local tone", &config->DlssNrLocalTone, 0.0f, 2.0f, 1.0f);
        DeferredSlider("Skin structure", &config->DlssNrSkinStructure, -1.0f, 2.0f, -1.0f);
        bool mask = config->DlssNrAutoMask.value_or_default();
        if (ImGui::Checkbox("Auto skin mask", &mask))
            config->DlssNrAutoMask = mask;
        HelpMarker("Model-based skin selection.");
        ImGui::TreePop();
    }

    if (config->DlssNrPasses.value_or_default() >= 2 && ImGui::TreeNodeEx("Pass 2", ImGuiTreeNodeFlags_DefaultOpen))
    {
        InheritedProfileCombo("Style", &config->DlssNrPass2Style, inheritedStyles, IM_ARRAYSIZE(inheritedStyles));
        DeferredSlider("Intensity", &config->DlssNrPass2Intensity, 0.0f, 2.0f,
                       config->DlssNrIntensity.value_or_default(), "%.2f", true);
        DeferredSlider("Local structure", &config->DlssNrPass2LocalStructure, 0.0f, 2.0f,
                       config->DlssNrLocalStructure.value_or_default(), "%.2f", true);
        DeferredSlider("Local tone", &config->DlssNrPass2LocalTone, 0.0f, 2.0f, 0.0f, "%.2f", true);
        DeferredSlider("Skin structure", &config->DlssNrPass2SkinStructure, -1.0f, 2.0f,
                       config->DlssNrSkinStructure.value_or_default(), "%.2f", true);
        bool mask = config->DlssNrPass2AutoMask.has_value() ? config->DlssNrPass2AutoMask.value()
                                                            : config->DlssNrAutoMask.value_or_default();
        if (ImGui::Checkbox("Auto skin mask", &mask))
            config->DlssNrPass2AutoMask = mask;
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##mask"))
            config->DlssNrPass2AutoMask = std::optional<bool> {};
        ImGui::TreePop();
    }

}

} // namespace DlssNr::MenuSections
