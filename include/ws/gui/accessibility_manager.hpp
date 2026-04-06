#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>

namespace ws::gui {

// =============================================================================
// Accessibility Feature
// =============================================================================

// Supported accessibility features.
enum class AccessibilityFeature {
    ScreenReaderSupport,      // ARIA/NVDA compatibility.
    HighContrast,             // High-contrast mode.
    LargeText,                // Enlarged fonts.
    HighlightFocus,           // Visible focus indicators.
    ReducedMotion,            // Disable animations.
    KeyboardNavigation,       // Full keyboard control.
    IncreaseClickTargets,     // Larger clickable areas.
    SimpleLanguage            // Simplified UI text.
};

// =============================================================================
// Tooltip Config
// =============================================================================

// Configuration for tooltip display.
struct TooltipConfig {
    float delayMs;            // Delay before showing tooltip (default: 500ms).
    float maxWidth;           // Max tooltip width in pixels.
    bool multiline;           // Allow line wrapping.
    bool persistent;          // Keep showing while hovering.
    std::string colorScheme;  // Color scheme (default, dark, high_contrast).
};

// =============================================================================
// Tooltip Manager
// =============================================================================

// Manages context-sensitive help tooltips.
class TooltipManager {
public:
    TooltipManager();
    
    // Registers a tooltip for a UI element.
    void registerTooltip(const std::string& elementId,
                        const std::string& text,
                        const TooltipConfig& config = {});
    
    // Gets tooltip text for an element.
    std::string getTooltip(const std::string& elementId) const;
    
    // Determines if tooltip should be shown.
    bool shouldShowTooltip(const std::string& elementId,
                          bool isHovered,
                          float timeSinceHover) const;
    
    // Renders tooltip at cursor position.
    void renderTooltip(const std::string& elementId);
    
    // Sets default tooltip configuration.
    void setDefaultConfig(const TooltipConfig& config);
    
    // Builds help index for search.
    std::map<std::string, std::string> buildHelpIndex() const;
    
    // Searches tooltips by keyword.
    std::vector<std::string> searchTooltips(const std::string& query) const;

private:
    struct TooltipEntry {
        std::string text;
        TooltipConfig config;
    };
    
    std::map<std::string, TooltipEntry> tooltips_;
    TooltipConfig defaultConfig_;
};

// =============================================================================
// Accessibility Manager
// =============================================================================

// Centralized accessibility feature management.
class AccessibilityManager {
public:
    AccessibilityManager();
    
    // Enables or disables an accessibility feature.
    void setFeatureEnabled(AccessibilityFeature feature, bool enabled);
    
    // Checks if a feature is enabled.
    bool isFeatureEnabled(AccessibilityFeature feature) const;
    
    // Gets all enabled features.
    std::set<AccessibilityFeature> getEnabledFeatures() const;
    
    // Gets the tooltip manager.
    TooltipManager& getTooltipManager();
    
    // Announces text for screen readers.
    void announceForScreenReader(const std::string& elementId,
                                 const std::string& announcement);
    
    // Applies an accessibility preset (minimal, standard, full).
    void applyAccessibilityPreset(const std::string& preset);
    
    // Gets the current focus element.
    std::string getCurrentFocusElement() const;
    
    // Sets focus to an element.
    void setFocusElement(const std::string& elementId);
    
    // Saves accessibility settings to a file.
    void saveSettings(const std::string& filename);
    
    // Loads accessibility settings from a file.
    void loadSettings(const std::string& filename);

private:
    std::map<AccessibilityFeature, bool> features_;
    TooltipManager tooltipManager_;
    std::string currentFocusElement_;
};

}  // namespace ws::gui
