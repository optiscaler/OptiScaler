#pragma once

#include <imgui/imgui.h>

class Config;

namespace DlssNr::MenuSections
{
// Compact contextual help, matching the rest of the menu.
inline void HelpMarker(const char* tip)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}


void RenderPlacement(Config* config, float menuResScale);
void RenderStatus(Config* config, float menuResScale);
void RenderInput(Config* config, float menuResScale);
void RenderModel(Config* config, float menuResScale);
void RenderBlend(Config* config, float menuResScale);
void RenderInspect(Config* config, float menuResScale);
} // namespace DlssNr::MenuSections
