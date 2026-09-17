#pragma once

#include <imgui/imgui.h>
#include <algorithm>
#include <string>
#include <vector>
#include <utility>
#include <cmath>

namespace DlssNr::PipelineUi
{
enum class Section
{
    Placement,
    Input,
    Model,
    Blend
};
enum class Route
{
    Before,
    After,
    Deferred,
    Finished,
    FinishedBefore
};

struct View
{
    Route route = Route::After;
    bool enabled = true;
    bool rayReconstruction = false;
    bool applyModel = true;
    const char* privateUpscaler = "DLSS";
    unsigned int passes = 1;
    int scalePercent = 100;
};

inline const char* SectionName(Section section)
{
    switch (section)
    {
    case Section::Placement:
        return "Placement";
    case Section::Input:
        return "Input";
    case Section::Model:
        return "Model passes";
    case Section::Blend:
        return "Apply NR edit";
    }
    return "";
}

// Keep both top-level toggles on one row, wrapping their clickable labels in narrow overlays.
inline bool CheckboxWrapped(const char* label, bool* value, float width)
{
    ImGui::PushID(label);
    ImGui::BeginGroup();
    const float right = ImGui::GetCursorPosX() + width;
    bool changed = ImGui::Checkbox("##toggle", value);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::PushTextWrapPos(right);
    ImGui::TextUnformatted(label);
    ImGui::PopTextWrapPos();
    if (ImGui::IsItemClicked())
    {
        *value = !*value;
        changed = true;
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

inline void DrawTimingBar(double nrMs, double frameMs)
{
    if (!std::isfinite(nrMs) || nrMs < 0.0 || !std::isfinite(frameMs) || frameMs <= 0.0)
    {
        ImGui::TextDisabled("Waiting for frame timing.");
        return;
    }
    const double remaining = std::max(frameMs - nrMs, 0.0);
    const bool overlapping = nrMs > frameMs;
    ImGui::TextWrapped("NR %.2f ms  |  Rest of frame ~%.2f ms", nrMs, remaining);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 1.0f), ImGui::GetFontSize());
    const float split = size.x * static_cast<float>(nrMs / std::max(frameMs, nrMs));
    const auto text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
    if (split > 0.0f)
        draw->AddRectFilled(at, ImVec2(at.x + split, at.y + size.y),
                            ImGui::GetColorU32(ImVec4(text.x * 0.20f, text.y * 0.55f, text.z * 0.25f, text.w)));
    if (split < size.x)
        draw->AddRectFilled(ImVec2(at.x + split, at.y), ImVec2(at.x + size.x, at.y + size.y),
                            ImGui::GetColorU32(ImGuiCol_Button));
    ImGui::Dummy(size);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("NR GPU time versus frame interval. Work can overlap.");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Rendered frame: %.2f ms%s", frameMs,
                        overlapping ? " (NR overlaps/exceeds this interval)" : "");
    ImGui::PopTextWrapPos();
}

// This describes the configured colour/edit flow. It never changes a rendering option.
inline void Draw(const View& view, Section& selected)
{
    ImGui::PushID("NR pipeline chart");
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    const float gap = ImGui::GetFontSize() * 1.5f;
    const bool split = view.enabled && (view.route == Route::Deferred ||
                                        view.route == Route::FinishedBefore);
    const bool finished = view.route == Route::Finished || view.route == Route::FinishedBefore;
    const float nodeWidth =
        split ? std::max((width - gap) * 0.5f, 1.0f) : std::min(width, ImGui::GetFontSize() * 27.0f);
    struct Node
    {
        int lane, row; // -1: left branch, 0: main path, 1: right branch
        const char* title;
        std::string detail;
        int section;
    };
    struct Edge
    {
        int from, to;
    };
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    const auto add = [&](int lane, int row, const char* title, std::string detail, int section = -1)
    {
        nodes.push_back({ lane, row, title, std::move(detail), section });
        return (int) nodes.size() - 1;
    };
    const auto connect = [&](int from, int to) { edges.push_back({ from, to }); };
    const auto prepare = [&](int lane, int row)
    {
        return add(lane, row, "Prepare NR input", "HDR / exposure / " + std::to_string(view.scalePercent) + "%",
                   (int) Section::Input);
    };
    const auto model = [&](int lane, int row)
    {
        return add(lane, row, "NR model",
                   std::to_string(view.passes) + (view.passes == 1 ? " pass" : " passes"),
                   (int) Section::Model);
    };
    const auto apply = [&](int row)
    {
        return add(0, row, "Apply NR edit", view.applyModel ? "Strength / skin" : "Edit hidden; model runs",
                   (int) Section::Blend);
    };
    const auto upscale = [&](int lane, int row)
    {
        return add(lane, row, view.rayReconstruction ? "RR + Super Resolution" : "Super Resolution",
                   split ? "Clean game image" : "Game upscaler");
    };
    const auto effects = [&](int lane, int row) { return add(lane, row, "Game effects + HUD", "Game rendering"); };
    const int input = add(0, 0, "Game input", "Placement / routing", (int) Section::Placement);
    int last = input, lastRow = 0;
    if (split)
    {
        // Only a separately carried edit branches. Both paths reunite at its application point.
        const int prep = prepare(-1, 1), nr = model(-1, 2);
        const int edit = add(-1, 3, "Upscale NR edit", std::string("Separate ") + view.privateUpscaler + " pass (no RR)");
        int game = upscale(1, 1);
        connect(input, prep);
        connect(prep, nr);
        connect(nr, edit);
        connect(input, game);
        if (finished)
        {
            const int fx = effects(1, 2);
            connect(game, fx);
            game = fx;
        }
        last = apply(4);
        connect(edit, last);
        connect(game, last);
        lastRow = 4;
    }
    else
    {
        const auto append = [&](int next)
        {
            connect(last, next);
            last = next;
        };
        if (!view.enabled || view.route != Route::Before)
            append(upscale(0, ++lastRow));
        if (!view.enabled || finished)
            append(effects(0, ++lastRow));
        if (view.enabled)
        {
            append(prepare(0, ++lastRow));
            append(model(0, ++lastRow));
            append(apply(++lastRow));
            if (view.route == Route::Before)
                append(upscale(0, ++lastRow));
        }
    }
    if (view.enabled && !finished)
    {
        const int fx = effects(0, ++lastRow);
        connect(last, fx);
        last = fx;
    }
    connect(last, add(0, ++lastRow, "Game output", "FG / presentation"));

    // Wrap labels inside their nodes so a narrow overlay does not crop either branch.
    const float padding = ImGui::GetStyle().FramePadding.x + 4.0f;
    const float wrapWidth = std::max(nodeWidth - padding * 2.0f, 1.0f);
    float height = 0.0f;
    for (const auto& node : nodes)
        height = std::max(height, ImGui::CalcTextSize(node.title, nullptr, false, wrapWidth).y +
                                      ImGui::CalcTextSize(node.detail.c_str(), nullptr, false, wrapWidth).y + 12.0f);
    const float step = height + gap;
    const auto topLeft = [&](int index)
    {
        const auto& node = nodes[index];
        const float x = node.lane < 0 ? 0.0f : node.lane > 0 ? width - nodeWidth : (width - nodeWidth) * 0.5f;
        return ImVec2(origin.x + x, origin.y + node.row * step);
    };
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 lineColour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    for (const auto& edge : edges)
    {
        const auto a = topLeft(edge.from), b = topLeft(edge.to);
        const ImVec2 start(a.x + nodeWidth * 0.5f, a.y + height), end(b.x + nodeWidth * 0.5f, b.y);
        const float bend = end.y - gap * 0.5f;
        draw->AddLine(start, ImVec2(start.x, bend), lineColour, 1.5f);
        draw->AddLine(ImVec2(start.x, bend), ImVec2(end.x, bend), lineColour, 1.5f);
        draw->AddLine(ImVec2(end.x, bend), end, lineColour, 1.5f);
        draw->AddTriangleFilled(end, ImVec2(end.x - 3, end.y - 5), ImVec2(end.x + 3, end.y - 5), lineColour);
    }
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const auto& node = nodes[i];
        const auto at = topLeft(i);
        ImGui::SetCursorScreenPos(at);
        ImGui::PushID(i);
        const bool editable = node.section >= 0;
        const bool chosen = editable && node.section == (int) selected;
        bool hovered = false;
        if (editable)
        {
            if (ImGui::InvisibleButton("stage", ImVec2(nodeWidth, height)))
                selected = (Section) node.section;
            hovered = ImGui::IsItemHovered();
        }
        else
            ImGui::Dummy(ImVec2(nodeWidth, height));
        const auto background = chosen     ? ImGuiCol_HeaderActive
                                : hovered  ? ImGuiCol_ButtonHovered
                                : editable ? ImGuiCol_Button
                                           : ImGuiCol_FrameBg;
        auto fill = ImGui::GetStyleColorVec4(background);
        if (node.section == (int) Section::Model)
        {
            // Preserve the theme's HDR brightness and interaction states, changing only the hue.
            const float brightness = std::max({ fill.x, fill.y, fill.z });
            fill = ImVec4(brightness * 0.20f, brightness * 0.65f, brightness * 0.30f, fill.w);
        }
        draw->AddRectFilled(at, ImVec2(at.x + nodeWidth, at.y + height), ImGui::GetColorU32(fill),
                            ImGui::GetStyle().FrameRounding);
        const auto titleSize = ImGui::CalcTextSize(node.title, nullptr, false, wrapWidth);
        const auto detailSize = ImGui::CalcTextSize(node.detail.c_str(), nullptr, false, wrapWidth);
        const float y = at.y + (height - titleSize.y - detailSize.y) * 0.5f;
        draw->AddText(nullptr, 0.0f, ImVec2(at.x + (nodeWidth - titleSize.x) * 0.5f, y),
                      ImGui::GetColorU32(ImGuiCol_Text), node.title, nullptr, wrapWidth);
        draw->AddText(nullptr, 0.0f, ImVec2(at.x + (nodeWidth - detailSize.x) * 0.5f, y + titleSize.y),
                      ImGui::GetColorU32(editable ? ImGuiCol_Text : ImGuiCol_TextDisabled), node.detail.c_str(),
                      nullptr, wrapWidth);
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + (lastRow + 1) * step));
    ImGui::Dummy(ImVec2(width, 0));
    // Inspection is a tool, not a colour path or a processing stage.
    const auto tool = [&](const char* label, Section section)
    {
        const bool chosen = selected == section;
        if (chosen)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        if (ImGui::Button(label))
            selected = section;
        if (chosen)
            ImGui::PopStyleColor();
    };
    if (!view.enabled)
    {
        tool("Prepare input", Section::Input);
        ImGui::SameLine();
        tool("Model passes", Section::Model);
        ImGui::SameLine();
        tool("Apply edit", Section::Blend);
    }
    ImGui::PopID();
}
} // namespace DlssNr::PipelineUi
