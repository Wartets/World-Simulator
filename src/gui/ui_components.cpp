#include "ws/gui/ui_components.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace ws::gui {
namespace {

constexpr std::array<ImVec4, 10> kSectionTints = {
    ImVec4(0.25f, 0.35f, 0.55f, 0.12f),
    ImVec4(0.20f, 0.45f, 0.45f, 0.12f),
    ImVec4(0.35f, 0.30f, 0.55f, 0.12f),
    ImVec4(0.40f, 0.35f, 0.25f, 0.12f),
    ImVec4(0.25f, 0.40f, 0.30f, 0.12f),
    ImVec4(0.35f, 0.25f, 0.35f, 0.12f),
    ImVec4(0.25f, 0.45f, 0.60f, 0.12f),
    ImVec4(0.35f, 0.45f, 0.25f, 0.12f),
    ImVec4(0.22f, 0.32f, 0.52f, 0.12f),
    ImVec4(0.42f, 0.28f, 0.40f, 0.12f),
};

ImVec4 addColors(const ImVec4& base, const ImVec4& tint) {
    return ImVec4(
        std::clamp(base.x + tint.x, 0.0f, 1.0f),
        std::clamp(base.y + tint.y, 0.0f, 1.0f),
        std::clamp(base.z + tint.z, 0.0f, 1.0f),
        std::clamp(base.w + tint.w, 0.0f, 1.0f));
}

} // namespace

bool PrimaryButton(const char* label, const ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(56, 96, 172, 220));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(72, 118, 205, 240));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(42, 78, 148, 255));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

bool SecondaryButton(const char* label, const ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(52, 58, 74, 180));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(68, 74, 94, 220));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(44, 50, 66, 245));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

bool NumericSliderPair(const char* label, float* value, const float minValue, const float maxValue, const char* format, const float inputWidth) {
    const float totalWidth = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float sliderWidth = std::clamp(totalWidth - inputWidth - spacing, 50.0f, 300.0f);

    bool changed = false;
    ImGui::PushItemWidth(sliderWidth);
    changed |= ImGui::SliderFloat(label, value, minValue, maxValue, format);
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::PushItemWidth(inputWidth);
    changed |= ImGui::InputFloat((std::string("##inp_") + label).c_str(), value, 0, 0, format);
    ImGui::PopItemWidth();

    *value = std::clamp(*value, minValue, maxValue);
    return changed;
}

bool NumericSliderPairInt(const char* label, int* value, const int minValue, const int maxValue, const char* format, const float inputWidth) {
    const float totalWidth = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float sliderWidth = std::clamp(totalWidth - inputWidth - spacing, 50.0f, 300.0f);

    bool changed = false;
    ImGui::PushItemWidth(sliderWidth);
    changed |= ImGui::SliderInt(label, value, minValue, maxValue, format);
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::PushItemWidth(inputWidth);
    changed |= ImGui::InputInt((std::string("##inpi_") + label).c_str(), value);
    ImGui::PopItemWidth();

    *value = std::clamp(*value, minValue, maxValue);
    return changed;
}

void DelayedTooltip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(360.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void SectionHeader(const char* title, const char* subtitle) {
    if (title != nullptr && title[0] != '\0') {
        ImGui::TextUnformatted(title);
    }
    if (subtitle != nullptr && subtitle[0] != '\0') {
        ImGui::TextDisabled("%s", subtitle);
    }
}

void EmptyStateCard(const char* title, const char* body) {
    ImGui::BeginChild("##empty_state", ImVec2(-1.0f, 0.0f), true);
    ImGui::Spacing();
    if (title != nullptr && title[0] != '\0') {
        ImGui::TextWrapped("%s", title);
    }
    if (body != nullptr && body[0] != '\0') {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", body);
    }
    ImGui::Spacing();
    ImGui::EndChild();
}

void LabeledHint(const char* text) {
    if (text != nullptr && text[0] != '\0') {
        ImGui::TextDisabled("%s", text);
    }
}

void PushSectionTint(const int index) {
    const ImVec4 tint = kSectionTints[static_cast<std::size_t>(std::abs(index) % static_cast<int>(kSectionTints.size()))];
    const auto& colors = ImGui::GetStyle().Colors;

    ImGui::PushStyleColor(ImGuiCol_Header, addColors(colors[ImGuiCol_Header], tint));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, addColors(colors[ImGuiCol_HeaderHovered], tint));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, addColors(colors[ImGuiCol_HeaderActive], tint));
}

void PopSectionTint() {
    ImGui::PopStyleColor(3);
}

void StatusBadge(const char* label, const bool active) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 pad(8.0f, 4.0f);
    const ImVec2 max(min.x + textSize.x + (2.0f * pad.x), min.y + textSize.y + (2.0f * pad.y));

    const ImU32 bg = active ? IM_COL32(40, 120, 80, 220) : IM_COL32(110, 70, 70, 220);
    const ImU32 border = active ? IM_COL32(90, 190, 130, 255) : IM_COL32(170, 100, 100, 255);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(min, max, bg, 6.0f);
    dl->AddRect(min, max, border, 6.0f);
    dl->AddText(ImVec2(min.x + pad.x, min.y + pad.y), IM_COL32(245, 245, 255, 255), label);

    ImGui::Dummy(ImVec2(max.x - min.x, max.y - min.y));
}

} // namespace ws::gui
