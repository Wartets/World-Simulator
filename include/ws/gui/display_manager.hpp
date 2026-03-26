#pragma once

#include "ws/core/state_store.hpp"

#include <string>
#include <vector>

namespace ws::gui {

enum class DisplayType {
    ScalarField = 0,
    SurfaceCategory = 1,
    RelativeElevation = 2,
    WaterDepth = 3
};

struct DisplayManagerParams {
    bool autoWaterLevel = true;
    float waterLevel = 0.48f;
    float autoWaterQuantile = 0.58f;
    float lowlandThreshold = 0.58f;
    float highlandThreshold = 0.75f;
    float waterPresenceThreshold = 0.12f;
};

struct DisplayBuffer {
    std::vector<float> values;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float effectiveWaterLevel = 0.48f;
    std::string label;
};

[[nodiscard]] const char* displayTypeLabel(DisplayType type);

[[nodiscard]] DisplayBuffer buildDisplayBufferFromSnapshot(
    const StateStoreSnapshot& snapshot,
    int primaryFieldIndex,
    DisplayType displayType,
    bool includeSparseOverlay,
    const DisplayManagerParams& params);

[[nodiscard]] DisplayBuffer buildDisplayBufferFromTerrain(
    const std::vector<float>& terrain,
    const std::vector<float>& water,
    DisplayType displayType,
    const DisplayManagerParams& params,
    const char* label = "preview");

} // namespace ws::gui
