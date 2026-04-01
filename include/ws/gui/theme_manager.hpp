#pragma once

#include <string>
#include <map>
#include <memory>
#include <glm/glm.hpp>

/**
 * @file theme_manager.hpp
 * @brief Theme and color scheme management for GUI
 * 
 * Supports multiple built-in themes with dynamic switching.
 * Colors are applied at ImGui level for consistent appearance.
 */

namespace ws::gui {

/**
 * @struct ColorPalette
 * @brief Complete color palette for a theme
 */
struct ColorPalette {
    // Base colors
    glm::vec4 backgroundColor;
    glm::vec4 foregroundColor;
    glm::vec4 accentColor;
    
    // UI elements
    glm::vec4 buttonColor;
    glm::vec4 buttonHoverColor;
    glm::vec4 buttonActiveColor;
    glm::vec4 windowBgColor;
    glm::vec4 windowBorderColor;
    
    // Text
    glm::vec4 textColor;
    glm::vec4 textDisabledColor;
    glm::vec4 textHintColor;
    
    // Status/feedback
    glm::vec4 successColor;
    glm::vec4 warningColor;
    glm::vec4 errorColor;
    glm::vec4 infoColor;
    
    // Viewport
    glm::vec4 viewportBgColor;
    glm::vec4 gridColor;
    glm::vec4 selectionColor;
};

/**
 * @enum ThemePreset
 * @brief Built-in theme presets
 */
enum class ThemePreset {
    Light,           ///< Light mode (default)
    Dark,            ///< Dark mode
    HighContrast,    ///< High-contrast accessibility mode
    Solarized,       ///< Solarized color scheme
    Monokai,         ///< Monokai color scheme
    Custom           ///< User-defined theme
};

/**
 * @struct Theme
 * @brief Complete theme definition
 */
struct Theme {
    std::string name;
    std::string description;
    ThemePreset preset;
    ColorPalette palette;
    float fontScale;       ///< Font scaling factor
    float windowRounding;  ///< UI element corner radius
    bool darkMode;         ///< Dark color mode flag
};

/**
 * @class ThemeManager
 * @brief Manages themes and applies them to ImGui
 * 
 * Features:
 * - Load predefined themes
 * - Create and customize themes
 * - Apply themes dynamically without restart
 * - Save/load user theme preferences
 * - Accessibility mode (high contrast, larger fonts)
 */
class ThemeManager {
public:
    /// Initialize with default themes
    ThemeManager();
    
    /**
     * @brief Get all available themes
     * @return Vector of theme names
     */
    const std::vector<std::string>& getAvailableThemes() const;
    
    /**
     * @brief Get current theme
     * @return Current Theme object
     */
    const Theme& getCurrentTheme() const;
    
    /**
     * @brief Set theme by preset
     * @param preset Theme preset
     */
    void setTheme(ThemePreset preset);
    
    /**
     * @brief Set theme by name
     * @param name Theme name (e.g., "Dark", "Light")
     */
    void setTheme(const std::string& name);
    
    /**
     * @brief Create custom theme
     * @param name Theme name
     * @param palette Color palette
     */
    void createTheme(const std::string& name, const ColorPalette& palette);
    
    /**
     * @brief Apply current theme to ImGui
     * 
     * This should be called once during UI initialization
     * and whenever theme changes.
     */
    void applyToImGui();
    
    /**
     * @brief Get color by semantic name
     * @param colorName Color identifier (e.g., "accent", "error")
     * @return Color as glm::vec4 (RGBA)
     */
    glm::vec4 getColor(const std::string& colorName) const;
    
    /**
     * @brief Enable accessibility mode
     * @param enable Enable/disable high-contrast mode
     */
    void setAccessibilityMode(bool enable);
    
    /**
     * @brief Set font scale
     * @param scale Scale factor (1.0 = normal, 1.5 = 50% larger)
     */
    void setFontScale(float scale);
    
    /**
     * @brief Persist theme preference to file
     * @param filename Target file path
     */
    void saveThemePreference(const std::string& filename);
    
    /**
     * @brief Load theme preference from file
     * @param filename Source file path
     */
    void loadThemePreference(const std::string& filename);
    
    /**
     * @brief Export theme definition
     * @param filename Target JSON file
     */
    void exportTheme(const std::string& filename);
    
    /**
     * @brief Import theme definition
     * @param filename Source JSON file
     */
    void importTheme(const std::string& filename);

private:
    std::map<std::string, Theme> themes_;
    Theme* currentTheme_;
    bool accessibilityMode_;
    
    /// Initialize built-in themes
    void initializeBuiltInThemes();
    
    /// Create light theme
    Theme createLightTheme() const;
    
    /// Create dark theme
    Theme createDarkTheme() const;
    
    /// Create high-contrast theme
    Theme createHighContrastTheme() const;
};

}  // namespace ws::gui
