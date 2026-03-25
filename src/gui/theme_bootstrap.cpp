#include "ws/gui/theme_bootstrap.hpp"

#include <algorithm>

namespace ws::gui {

void ThemeBootstrap::applyBaseTheme(ImGuiStyle& style, const float effectiveScale) {
    ImGui::StyleColorsDark();

    style = ImGuiStyle{};
    ImGui::StyleColorsDark(&style);

    style.FrameRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.WindowRounding = 6.0f;
    style.TabRounding = 4.0f;
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 14.0f * effectiveScale;

    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.94f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.06f, 0.10f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.20f, 0.80f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.22f, 0.35f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.35f, 0.55f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.45f, 0.70f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.55f, 0.80f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.65f, 0.95f, 1.00f);

    style.Colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.45f, 0.50f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 1.0f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

    style.ScaleAllSizes(effectiveScale);
}

void ThemeBootstrap::applyAccessibility(ImGuiIO& io, ImGuiStyle& style, const AccessibilityConfig& cfg) {
    if (cfg.keyboardNav) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    }

    if (cfg.focusIndicators) {
        style.Colors[ImGuiCol_NavHighlight] = ImVec4(1.0f, 1.0f, 0.2f, 1.0f);
    } else {
        style.Colors[ImGuiCol_NavHighlight] = ImVec4(0, 0, 0, 0);
    }

    if (cfg.highContrast) {
        style.Colors[ImGuiCol_Text] = ImVec4(1, 1, 1, 1);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 1);
        style.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(1, 1, 0, 0.3f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(1, 1, 0, 1);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(1, 1, 0, 1);
        style.FrameBorderSize = 1.0f;
        style.WindowBorderSize = 1.0f;
    }
}

void ThemeBootstrap::configureFont(ImGuiIO& io, const float fontSizePx) {
    const float clampedSize = std::clamp(fontSizePx, 10.0f, 32.0f);

    io.Fonts->Clear();
    io.FontDefault = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", clampedSize);
    if (io.FontDefault == nullptr) {
        io.FontDefault = io.Fonts->AddFontDefault();
    }
}

} // namespace ws::gui
