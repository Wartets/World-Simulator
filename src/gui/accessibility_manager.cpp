#include "ws/gui/accessibility_manager.hpp"
#include <vector>
#include <set>
#include <algorithm>
#include <cctype>

namespace ws::gui {

TooltipManager::TooltipManager() {
    defaultConfig_.delayMs = 500.0f;
    defaultConfig_.maxWidth = 300.0f;
    defaultConfig_.multiline = true;
    defaultConfig_.persistent = true;
    defaultConfig_.colorScheme = "default";
}

void TooltipManager::registerTooltip(const std::string& elementId,
                                     const std::string& text,
                                     const TooltipConfig& config) {
    tooltips_[elementId] = {text, config};
}

std::string TooltipManager::getTooltip(const std::string& elementId) const {
    auto it = tooltips_.find(elementId);
    if (it != tooltips_.end()) {
        return it->second.text;
    }
    return "";
}

bool TooltipManager::shouldShowTooltip(const std::string& elementId,
                                       bool isHovered,
                                       float timeSinceHover) const {
    if (!isHovered) return false;
    
    auto it = tooltips_.find(elementId);
    if (it == tooltips_.end()) return false;
    
    return timeSinceHover >= it->second.config.delayMs;
}

void TooltipManager::renderTooltip(const std::string& elementId) {
    // TODO: Implement ImGui tooltip rendering
}

void TooltipManager::setDefaultConfig(const TooltipConfig& config) {
    defaultConfig_ = config;
}

std::map<std::string, std::string> TooltipManager::buildHelpIndex() const {
    std::map<std::string, std::string> index;
    for (const auto& [elementId, entry] : tooltips_) {
        // Extract keywords from tooltip text
        // For now, just map element ID
        index[elementId] = entry.text;
    }
    return index;
}

std::vector<std::string> TooltipManager::searchTooltips(const std::string& query) const {
    std::vector<std::string> results;
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
    
    for (const auto& [elementId, entry] : tooltips_) {
        std::string lowerText = entry.text;
        std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
        
        if (lowerText.find(lowerQuery) != std::string::npos) {
            results.push_back(elementId);
        }
    }
    return results;
}

AccessibilityManager::AccessibilityManager() {
    // Initialize all features as disabled
    features_[AccessibilityFeature::ScreenReaderSupport] = false;
    features_[AccessibilityFeature::HighContrast] = false;
    features_[AccessibilityFeature::LargeText] = false;
    features_[AccessibilityFeature::HighlightFocus] = false;
    features_[AccessibilityFeature::ReducedMotion] = false;
    features_[AccessibilityFeature::KeyboardNavigation] = false;
    features_[AccessibilityFeature::IncreaseClickTargets] = false;
    features_[AccessibilityFeature::SimpleLanguage] = false;
}

void AccessibilityManager::setFeatureEnabled(AccessibilityFeature feature, bool enabled) {
    features_[feature] = enabled;
}

bool AccessibilityManager::isFeatureEnabled(AccessibilityFeature feature) const {
    auto it = features_.find(feature);
    if (it != features_.end()) {
        return it->second;
    }
    return false;
}

std::set<AccessibilityFeature> AccessibilityManager::getEnabledFeatures() const {
    std::set<AccessibilityFeature> enabled;
    for (const auto& [feature, isEnabled] : features_) {
        if (isEnabled) {
            enabled.insert(feature);
        }
    }
    return enabled;
}

TooltipManager& AccessibilityManager::getTooltipManager() {
    return tooltipManager_;
}

void AccessibilityManager::announceForScreenReader(const std::string& elementId,
                                                   const std::string& announcement) {
    if (isFeatureEnabled(AccessibilityFeature::ScreenReaderSupport)) {
        // TODO: Implement screen reader integration
    }
}

void AccessibilityManager::applyAccessibilityPreset(const std::string& preset) {
    // Disable all features
    for (auto& [feature, _] : features_) {
        features_[feature] = false;
    }
    
    if (preset == "minimal") {
        // No extra features
    } else if (preset == "standard") {
        features_[AccessibilityFeature::HighlightFocus] = true;
        features_[AccessibilityFeature::KeyboardNavigation] = true;
        features_[AccessibilityFeature::LargeText] = true;
    } else if (preset == "full") {
        for (auto& [feature, _] : features_) {
            features_[feature] = true;
        }
    }
}

std::string AccessibilityManager::getCurrentFocusElement() const {
    return currentFocusElement_;
}

void AccessibilityManager::setFocusElement(const std::string& elementId) {
    currentFocusElement_ = elementId;
    announceForScreenReader(elementId, "Focused on " + elementId);
}

void AccessibilityManager::saveSettings(const std::string& filename) {
    // TODO: Implement JSON serialization
}

void AccessibilityManager::loadSettings(const std::string& filename) {
    // TODO: Implement JSON deserialization
}

}  // namespace ws::gui
