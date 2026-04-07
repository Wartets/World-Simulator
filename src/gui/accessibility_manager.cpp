#include "ws/gui/accessibility_manager.hpp"
#include <vector>
#include <set>
#include <algorithm>
#include <cctype>

namespace ws::gui {

// Constructs tooltip manager with default configuration.
// Sets default delay to 500ms, max width to 300px, enables multiline and persistence.
TooltipManager::TooltipManager() {
    defaultConfig_.delayMs = 500.0f;
    defaultConfig_.maxWidth = 300.0f;
    defaultConfig_.multiline = true;
    defaultConfig_.persistent = true;
    defaultConfig_.colorScheme = "default";
}

// Registers a tooltip for a UI element.
// @param elementId Unique identifier for the UI element
// @param text Tooltip text content
// @param config Optional custom tooltip configuration
void TooltipManager::registerTooltip(const std::string& elementId,
                                     const std::string& text,
                                     const TooltipConfig& config) {
    tooltips_[elementId] = {text, config};
}

// Retrieves tooltip text for a UI element.
// @param elementId Unique identifier for the UI element
// @return Tooltip text content, empty string if not registered
std::string TooltipManager::getTooltip(const std::string& elementId) const {
    auto it = tooltips_.find(elementId);
    if (it != tooltips_.end()) {
        return it->second.text;
    }
    return "";
}

// Determines if tooltip should be displayed based on hover state.
// @param elementId Unique identifier for the UI element
// @param isHovered Whether the element is currently hovered
// @param timeSinceHover Time elapsed since hover started in milliseconds
// @return true if tooltip should be displayed
bool TooltipManager::shouldShowTooltip(const std::string& elementId,
                                       bool isHovered,
                                       float timeSinceHover) const {
    if (!isHovered) return false;
    
    auto it = tooltips_.find(elementId);
    if (it == tooltips_.end()) return false;
    
    return timeSinceHover >= it->second.config.delayMs;
}

// Renders tooltip for a UI element using ImGui.
// @param elementId Unique identifier for the UI element
void TooltipManager::renderTooltip(const std::string& elementId) {
    // TODO: Implement ImGui tooltip rendering
}

// Sets default tooltip configuration for all tooltips.
// @param config Default tooltip configuration
void TooltipManager::setDefaultConfig(const TooltipConfig& config) {
    defaultConfig_ = config;
}

// Builds searchable index of all registered tooltips.
// @return Map of element IDs to tooltip text for help system
std::map<std::string, std::string> TooltipManager::buildHelpIndex() const {
    std::map<std::string, std::string> index;
    for (const auto& [elementId, entry] : tooltips_) {
        // Extract keywords from tooltip text
        // For now, just map element ID
        index[elementId] = entry.text;
    }
    return index;
}

// Searches tooltips for text matching query.
// @param query Search string (case-insensitive)
// @return List of element IDs with matching tooltips
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

// Constructs accessibility manager with all features disabled by default.
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

// Enables or disables an accessibility feature.
// @param feature The accessibility feature to modify
// @param enabled Whether to enable (true) or disable (false) the feature
void AccessibilityManager::setFeatureEnabled(AccessibilityFeature feature, bool enabled) {
    features_[feature] = enabled;
}

// Checks if an accessibility feature is currently enabled.
// @param feature The accessibility feature to query
// @return true if the feature is enabled
bool AccessibilityManager::isFeatureEnabled(AccessibilityFeature feature) const {
    auto it = features_.find(feature);
    if (it != features_.end()) {
        return it->second;
    }
    return false;
}

// Gets set of all currently enabled accessibility features.
// @return Set of enabled accessibility features
std::set<AccessibilityFeature> AccessibilityManager::getEnabledFeatures() const {
    std::set<AccessibilityFeature> enabled;
    for (const auto& [feature, isEnabled] : features_) {
        if (isEnabled) {
            enabled.insert(feature);
        }
    }
    return enabled;
}

// Gets reference to the tooltip manager for this accessibility context.
// @return Reference to tooltip manager
TooltipManager& AccessibilityManager::getTooltipManager() {
    return tooltipManager_;
}

// Announces message to screen reader if enabled.
// @param elementId The element associated with the announcement
// @param announcement Text to announce
void AccessibilityManager::announceForScreenReader(const std::string& elementId,
                                                   const std::string& announcement) {
    if (isFeatureEnabled(AccessibilityFeature::ScreenReaderSupport)) {
        // TODO: Implement screen reader integration
    }
}

// Applies predefined accessibility preset configuration.
// @param preset Preset name: "minimal", "standard", or "full"
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

// Gets current focus element identifier.
// @return ID of currently focused element, empty if none
std::string AccessibilityManager::getCurrentFocusElement() const {
    return currentFocusElement_;
}

// Sets current focus element and announces change to screen reader.
// @param elementId ID of element to receive focus
void AccessibilityManager::setFocusElement(const std::string& elementId) {
    currentFocusElement_ = elementId;
    announceForScreenReader(elementId, "Focused on " + elementId);
}

// Saves accessibility settings to JSON file.
// @param filename Path to output file
void AccessibilityManager::saveSettings(const std::string& filename) {
    // TODO: Implement JSON serialization
}

// Loads accessibility settings from JSON file.
// @param filename Path to input file
void AccessibilityManager::loadSettings(const std::string& filename) {
    // TODO: Implement JSON deserialization
}

}  // namespace ws::gui
