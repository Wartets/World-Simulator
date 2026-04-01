#pragma once

#include <string>
#include <map>
#include <memory>

/**
 * @file accessibility_manager.hpp
 * @brief Accessibility and tooltip infrastructure for GUI
 * 
 * Provides context-aware tooltips, screen reader integration,
 * keyboard navigation, and accessibility features.
 */

namespace ws::gui {

/**
 * @enum AccessibilityFeature
 * @brief Supported accessibility features
 */
enum class AccessibilityFeature {
    ScreenReaderSupport,      ///< ARIA/NVDA compatibility
    HighContrast,             ///< High-contrast mode
    LargeText,                ///< Enlarged fonts
    HighlightFocus,           ///< Visible focus indicators
    ReducedMotion,            ///< Disable animations
    KeyboardNavigation,       ///< Full keyboard control
    IncreaseClickTargets,     ///< Larger clickable areas
    SimpleLanguage            ///< Simplified UI text
};

/**
 * @struct TooltipConfig
 * @brief Tooltip display configuration
 */
struct TooltipConfig {
    float delayMs;            ///< Delay before showing tooltip (default: 500ms)
    float maxWidth;           ///< Max tooltip width in pixels
    bool multiline;           ///< Allow line wrapping
    bool persistent;          ///< Keep showing while hovering
    std::string colorScheme;  ///< Color scheme (default, dark, high_contrast)
};

/**
 * @class TooltipManager
 * @brief Manages context-sensitive help tooltips
 * 
 * Features:
 * - Lazy loading of help text
 * - Multiple languages (if needed)
 * - Accessibility-compatible formatting
 * - Help index and search
 */
class TooltipManager {
public:
    TooltipManager();
    
    /**
     * @brief Register a tooltip for a UI element
     * @param elementId Unique element identifier
     * @param text Tooltip text
     * @param config Tooltip display config
     */
    void registerTooltip(const std::string& elementId,
                        const std::string& text,
                        const TooltipConfig& config = {});
    
    /**
     * @brief Get tooltip text for element
     * @param elementId Element identifier
     * @return Tooltip text or empty string if not found
     */
    std::string getTooltip(const std::string& elementId) const;
    
    /**
     * @brief Show tooltip if conditions met
     * @param elementId Element identifier
     * @param isHovered Element is currently hovered
     * @param timeSinceHover Time hovered in milliseconds
     * @return true if tooltip should be displayed
     */
    bool shouldShowTooltip(const std::string& elementId,
                          bool isHovered,
                          float timeSinceHover) const;
    
    /**
     * @brief Render tooltip at cursor position
     * @param elementId Element identifier
     * 
     * Should be called from ImGui drawing context
     */
    void renderTooltip(const std::string& elementId);
    
    /**
     * @brief Set tooltip configuration for all tooltips
     * @param config Default configuration
     */
    void setDefaultConfig(const TooltipConfig& config);
    
    /**
     * @brief Build help index for search
     * @return Map of keywords to element IDs
     */
    std::map<std::string, std::string> buildHelpIndex() const;
    
    /**
     * @brief Search tooltips by keyword
     * @param query Search query
     * @return Vector of matching element IDs
     */
    std::vector<std::string> searchTooltips(const std::string& query) const;

private:
    struct TooltipEntry {
        std::string text;
        TooltipConfig config;
    };
    
    std::map<std::string, TooltipEntry> tooltips_;
    TooltipConfig defaultConfig_;
};

/**
 * @class AccessibilityManager
 * @brief Centralized accessibility feature management
 * 
 * Manages all accessibility-related settings and configurations.
 * Coordinates with ThemeManager and KeyboardShortcutManager.
 */
class AccessibilityManager {
public:
    AccessibilityManager();
    
    /**
     * @brief Enable/disable accessibility feature
     * @param feature Feature to modify
     * @param enabled Enable state
     */
    void setFeatureEnabled(AccessibilityFeature feature, bool enabled);
    
    /**
     * @brief Check if feature is enabled
     * @param feature Feature to check
     * @return true if enabled
     */
    bool isFeatureEnabled(AccessibilityFeature feature) const;
    
    /**
     * @brief Get all enabled features
     * @return Set of enabled features
     */
    std::set<AccessibilityFeature> getEnabledFeatures() const;
    
    /**
     * @brief Get tooltip manager
     * @return Reference to TooltipManager
     */
    TooltipManager& getTooltipManager();
    
    /**
     * @brief Set screen reader focus
     * @param elementId Element to announce
     * @param announcement Text for screen reader
     * 
     * Broadcasts announcement to connected screen readers (if any)
     */
    void announceForScreenReader(const std::string& elementId,
                                 const std::string& announcement);
    
    /**
     * @brief Apply accessibility preset
     * @param preset Accessibility profile
     * 
     * Presets: minimal, standard, full (all features)
     */
    void applyAccessibilityPreset(const std::string& preset);
    
    /**
     * @brief Get current focus element
     * @return Element ID or empty string
     */
    std::string getCurrentFocusElement() const;
    
    /**
     * @brief Set focus to element
     * @param elementId Element to focus
     */
    void setFocusElement(const std::string& elementId);
    
    /**
     * @brief Persist accessibility settings
     * @param filename Target file path
     */
    void saveSettings(const std::string& filename);
    
    /**
     * @brief Load accessibility settings
     * @param filename Source file path
     */
    void loadSettings(const std::string& filename);

private:
    std::map<AccessibilityFeature, bool> features_;
    TooltipManager tooltipManager_;
    std::string currentFocusElement_;
};

}  // namespace ws::gui
