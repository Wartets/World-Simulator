#pragma once

#include <imgui.h>

namespace ws::gui {

struct AccessibilityConfig {
    float uiScale = 1.0f;      // [0.75, 3.0]
    float fontSizePx = 16.0f;  // [10, 32]
    bool highContrast = false;
    bool keyboardNav = true;
    bool focusIndicators = true;
    bool reduceMotion = false;
};

class ThemeBootstrap {
public:
    static void applyBaseTheme(ImGuiStyle& style, float effectiveScale);
    static void applyAccessibility(ImGuiIO& io, ImGuiStyle& style, const AccessibilityConfig& cfg);
    static void configureFont(ImGuiIO& io, float fontSizePx);
};

} // namespace ws::gui
