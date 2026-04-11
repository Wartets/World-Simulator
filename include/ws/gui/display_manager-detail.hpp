#pragma once

// Private display manager implementation details.
// This header is for internal use only and should not be included in public headers.

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace ws::gui::detail {

// Internal display type enumeration kept in detail to reduce public API.
// The public API exposes DisplayType directly for now.
// This namespace reserves the right to reorganize display types in the future.

// Parameters for display configuration - kept as internal details
// to allow for future refactoring without breaking the public API.
struct DisplayConfigurationDetail {
    bool autoWaterLevel = true;
    float waterLevel = 0.48f;
    float autoWaterQuantile = 0.58f;
    float lowlandThreshold = 0.58f;
    float highlandThreshold = 0.75f;
    float waterPresenceThreshold = 0.12f;
    float shallowWaterDepth = 0.05f;
    float highMoistureThreshold = 0.65f;

    // Returns a clamped/safe configuration suitable for rendering.
    [[nodiscard]] DisplayConfigurationDetail sanitized() const;

    // Validates the configuration.
    [[nodiscard]] bool validate(std::string& errorMessage) const;
};

} // namespace ws::gui::detail

