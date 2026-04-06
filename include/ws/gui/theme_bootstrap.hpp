#pragma once

#include <imgui.h>

namespace ws::gui {

// =============================================================================
// Accessibility Config
// =============================================================================

// Configuration for accessibility features.
struct AccessibilityConfig {
    float uiScale = 1.0f;       // UI scale factor [0.75, 3.0].
    float fontSizePx = 16.0f;  // Font size in pixels [10, 32].
    bool highContrast = false;  // Enable high contrast mode.
    bool keyboardNav = true;    // Enable keyboard navigation.
    bool focusIndicators = true;  // Show focus indicators.
    bool reduceMotion = false;  // Reduce animations.
};

// =============================================================================
// Theme Bootstrap
// =============================================================================

// Applies base theme and accessibility settings to ImGui.
class ThemeBootstrap {
public:
    // Applies the base theme to an ImGui style.
    static void applyBaseTheme(ImGuiStyle& style, float effectiveScale);
    // Applies accessibility settings to ImGui.
    static void applyAccessibility(ImGuiIO& io, ImGuiStyle& style, const AccessibilityConfig& cfg);
    // Configures the font with specified size.
    static void configureFont(ImGuiIO& io, float fontSizePx);
};

} // namespace ws::gui
