#pragma once

#include "ws/core/state_store.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ws::gui {

// =============================================================================
// Display Type
// =============================================================================

// Types of visualization displays available.
enum class DisplayType {
    ScalarField = 0,       // Raw scalar field visualization.
    SurfaceCategory = 1,   // Categorized surface type visualization.
    RelativeElevation = 2, // Relative elevation from water level.
    WaterDepth = 3,        // Water depth visualization.
    MoistureMap = 4,       // Soil moisture map visualization.
    WindField = 5          // Wind vector field visualization.
};

// =============================================================================
// Display Manager Params
// =============================================================================

// Parameters for display buffer construction.
struct DisplayManagerParams {
    bool autoWaterLevel = true;            // Whether to auto-calculate water level.
    float waterLevel = 0.48f;              // Manual water level threshold.
    float autoWaterQuantile = 0.58f;       // Quantile for auto water level.
    float lowlandThreshold = 0.58f;        // Threshold for lowland classification.
    float highlandThreshold = 0.75f;       // Threshold for highland classification.
    float waterPresenceThreshold = 0.12f;  // Minimum value for water presence.
    float shallowWaterDepth = 0.05f;       // Depth threshold for shallow water.
    float highMoistureThreshold = 0.65f;   // Threshold for high moisture.

    // Returns a clamped/safe parameter set suitable for rendering.
    [[nodiscard]] DisplayManagerParams sanitized() const;

    // Validates the parameter set.
    // Returns false and sets errorMessage when values are non-finite.
    [[nodiscard]] bool validate(std::string& errorMessage) const;
};

// Returns an empty, reusable display-tag map.
[[nodiscard]] const std::unordered_map<std::string, std::vector<std::string>>& emptyDisplayFieldTags();

// Typed request for snapshot-driven display generation.
struct DisplaySnapshotRequest {
    const StateStoreSnapshot& snapshot;
    int primaryFieldIndex = 0;
    DisplayType displayType = DisplayType::ScalarField;
    bool includeSparseOverlay = false;
    std::reference_wrapper<const std::unordered_map<std::string, std::vector<std::string>>> fieldDisplayTags =
        std::cref(emptyDisplayFieldTags());

    [[nodiscard]] DisplaySnapshotRequest sanitized() const;
    [[nodiscard]] bool validate(std::string& errorMessage) const;
};

// Typed request for terrain/water preview display generation.
struct DisplayTerrainPreviewRequest {
    std::reference_wrapper<const std::vector<float>> terrain;
    std::reference_wrapper<const std::vector<float>> water;
    DisplayType displayType = DisplayType::ScalarField;
    std::string label = "preview";

    [[nodiscard]] DisplayTerrainPreviewRequest sanitized() const;
    [[nodiscard]] bool validate(std::string& errorMessage) const;
};

// Typed request for full-component preview display generation.
struct DisplayPreviewComponentsRequest {
    std::reference_wrapper<const std::vector<float>> primary;
    std::reference_wrapper<const std::vector<float>> terrain;
    std::reference_wrapper<const std::vector<float>> water;
    std::reference_wrapper<const std::vector<float>> humidity;
    std::reference_wrapper<const std::vector<float>> windU;
    std::reference_wrapper<const std::vector<float>> windV;
    DisplayType displayType = DisplayType::ScalarField;
    std::string label = "preview";

    [[nodiscard]] DisplayPreviewComponentsRequest sanitized() const;
    [[nodiscard]] bool validate(std::string& errorMessage) const;
};

// =============================================================================
// Display Buffer
// =============================================================================

// Pre-computed buffer for rendering a display type.
struct DisplayBuffer {
    std::vector<float> values;          // Pre-computed display values.
    float minValue = 0.0f;              // Minimum value in the buffer.
    float maxValue = 1.0f;              // Maximum value in the buffer.
    float effectiveWaterLevel = 0.48f;  // Effective water level used.
    std::string label;                  // Label for the buffer.
};

// Returns a human-readable label for a display type.
[[nodiscard]] const char* displayTypeLabel(DisplayType type);

// Builds a display buffer from a state store snapshot.
[[nodiscard]] DisplayBuffer buildDisplayBufferFromSnapshot(
    const DisplaySnapshotRequest& request,
    const DisplayManagerParams& params);

// Builds a display buffer from terrain and water arrays.
[[nodiscard]] DisplayBuffer buildDisplayBufferFromTerrain(
    const DisplayTerrainPreviewRequest& request,
    const DisplayManagerParams& params);

// Builds a display buffer from preview component arrays.
[[nodiscard]] DisplayBuffer buildDisplayBufferFromPreviewComponents(
    const DisplayPreviewComponentsRequest& request,
    const DisplayManagerParams& params);

// Backward-compatible overloads.
[[nodiscard]] DisplayBuffer buildDisplayBufferFromSnapshot(
    const StateStoreSnapshot& snapshot,
    int primaryFieldIndex,
    DisplayType displayType,
    bool includeSparseOverlay,
    const DisplayManagerParams& params,
    const std::unordered_map<std::string, std::vector<std::string>>& fieldDisplayTags = {});

// Builds a display buffer from terrain and water arrays.
[[nodiscard]] DisplayBuffer buildDisplayBufferFromTerrain(
    const std::vector<float>& terrain,
    const std::vector<float>& water,
    DisplayType displayType,
    const DisplayManagerParams& params,
    const char* label = "preview");

// Builds a display buffer from preview component arrays.
[[nodiscard]] DisplayBuffer buildDisplayBufferFromPreviewComponents(
    const std::vector<float>& primary,
    const std::vector<float>& terrain,
    const std::vector<float>& water,
    const std::vector<float>& humidity,
    const std::vector<float>& windU,
    const std::vector<float>& windV,
    DisplayType displayType,
    const DisplayManagerParams& params,
    const char* label = "preview");

} // namespace ws::gui
