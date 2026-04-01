#include "ws/gui/theme_manager.hpp"

namespace ws::gui {

ThemeManager::ThemeManager() : currentTheme_(nullptr), accessibilityMode_(false) {
    initializeBuiltInThemes();
    setTheme(ThemePreset::Light);
}

const std::vector<std::string>& ThemeManager::getAvailableThemes() const {
    static std::vector<std::string> names;
    if (names.empty()) {
        for (const auto& [name, _] : themes_) {
            names.push_back(name);
        }
    }
    return names;
}

const Theme& ThemeManager::getCurrentTheme() const {
    return *currentTheme_;
}

void ThemeManager::setTheme(ThemePreset preset) {
    std::string name;
    switch (preset) {
        case ThemePreset::Light:
            name = "Light";
            break;
        case ThemePreset::Dark:
            name = "Dark";
            break;
        case ThemePreset::HighContrast:
            name = "HighContrast";
            break;
        case ThemePreset::Solarized:
            name = "Solarized";
            break;
        case ThemePreset::Monokai:
            name = "Monokai";
            break;
        case ThemePreset::Custom:
            name = "Custom";
            break;
    }
    setTheme(name);
}

void ThemeManager::setTheme(const std::string& name) {
    auto it = themes_.find(name);
    if (it != themes_.end()) {
        currentTheme_ = &it->second;
        applyToImGui();
    }
}

void ThemeManager::createTheme(const std::string& name, const ColorPalette& palette) {
    Theme theme;
    theme.name = name;
    theme.preset = ThemePreset::Custom;
    theme.palette = palette;
    theme.fontScale = 1.0f;
    theme.windowRounding = 5.0f;
    theme.darkMode = (palette.backgroundColor.x < 0.5f);
    
    themes_[name] = theme;
}

void ThemeManager::applyToImGui() {
    // TODO: Apply theme to ImGui style
    // This requires ImGui context and style modification
}

ImVec4 ThemeManager::getColor(const std::string& colorName) const {
    if (!currentTheme_) return ImVec4(0, 0, 0, 0);
    
    const auto& p = currentTheme_->palette;
    
    if (colorName == "background") return p.backgroundColor;
    if (colorName == "foreground") return p.foregroundColor;
    if (colorName == "accent") return p.accentColor;
    if (colorName == "button") return p.buttonColor;
    if (colorName == "text") return p.textColor;
    if (colorName == "success") return p.successColor;
    if (colorName == "warning") return p.warningColor;
    if (colorName == "error") return p.errorColor;
    if (colorName == "info") return p.infoColor;
    
    return ImVec4(0, 0, 0, 0);
}

void ThemeManager::setAccessibilityMode(bool enable) {
    accessibilityMode_ = enable;
    if (enable) {
        setTheme("HighContrast");
        currentTheme_->fontScale = 1.5f;
    }
}

void ThemeManager::setFontScale(float scale) {
    if (currentTheme_) {
        currentTheme_->fontScale = scale;
        applyToImGui();
    }
}

void ThemeManager::saveThemePreference(const std::string& filename) {
    // TODO: Implement JSON serialization
}

void ThemeManager::loadThemePreference(const std::string& filename) {
    // TODO: Implement JSON deserialization
}

void ThemeManager::exportTheme(const std::string& filename) {
    // TODO: Export current theme to JSON
}

void ThemeManager::importTheme(const std::string& filename) {
    // TODO: Import theme from JSON
}

void ThemeManager::initializeBuiltInThemes() {
    themes_["Light"] = createLightTheme();
    themes_["Dark"] = createDarkTheme();
    themes_["HighContrast"] = createHighContrastTheme();
}

Theme ThemeManager::createLightTheme() const {
    Theme theme;
    theme.name = "Light";
    theme.description = "Light theme with bright backgrounds";
    theme.preset = ThemePreset::Light;
    theme.darkMode = false;
    theme.fontScale = 1.0f;
    theme.windowRounding = 5.0f;
    
    theme.palette.backgroundColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    theme.palette.foregroundColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    theme.palette.accentColor = ImVec4(0.2f, 0.5f, 1.0f, 1.0f);
    theme.palette.textColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    theme.palette.buttonColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
    theme.palette.buttonHoverColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    theme.palette.buttonActiveColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    theme.palette.successColor = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);
    theme.palette.warningColor = ImVec4(1.0f, 0.7f, 0.0f, 1.0f);
    theme.palette.errorColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    theme.palette.infoColor = ImVec4(0.0f, 0.5f, 1.0f, 1.0f);
    
    return theme;
}

Theme ThemeManager::createDarkTheme() const {
    Theme theme;
    theme.name = "Dark";
    theme.description = "Dark theme with dark backgrounds";
    theme.preset = ThemePreset::Dark;
    theme.darkMode = true;
    theme.fontScale = 1.0f;
    theme.windowRounding = 5.0f;
    
    theme.palette.backgroundColor = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    theme.palette.foregroundColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    theme.palette.accentColor = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);
    theme.palette.textColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    theme.palette.buttonColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    theme.palette.buttonHoverColor = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    theme.palette.buttonActiveColor = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    theme.palette.successColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
    theme.palette.warningColor = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
    theme.palette.errorColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
    theme.palette.infoColor = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);
    
    return theme;
}

Theme ThemeManager::createHighContrastTheme() const {
    Theme theme;
    theme.name = "HighContrast";
    theme.description = "High-contrast theme for accessibility";
    theme.preset = ThemePreset::HighContrast;
    theme.darkMode = false;
    theme.fontScale = 1.2f;
    theme.windowRounding = 2.0f;
    
    theme.palette.backgroundColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    theme.palette.foregroundColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    theme.palette.accentColor = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);  // Pure blue
    theme.palette.textColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    theme.palette.buttonColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    theme.palette.buttonHoverColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    theme.palette.buttonActiveColor = ImVec4(0.0f, 0.0f, 0.5f, 1.0f);
    theme.palette.successColor = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);
    theme.palette.warningColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    theme.palette.errorColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    theme.palette.infoColor = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
    
    return theme;
}

}  // namespace ws::gui
