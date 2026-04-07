#include "ws/gui/display_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <string>

namespace ws::gui {
namespace {

// Merges field values with optional sparse overlay into unified vector.
// @param field Source field payload
// @param includeSparseOverlay Whether to include sparse overlay values
// @return Merged vector with NaN for invalid cells
std::vector<float> mergedFieldValues(const StateStoreSnapshot::FieldPayload& field, const bool includeSparseOverlay) {
    const std::size_t n = field.values.size();
    std::vector<float> merged(n, std::numeric_limits<float>::quiet_NaN());

    for (std::size_t i = 0; i < n; ++i) {
        if (i < field.validityMask.size() && field.validityMask[i] != 0u) {
            merged[i] = field.values[i];
        }
    }

    if (includeSparseOverlay) {
        for (const auto& [idx, value] : field.sparseOverlay) {
            if (idx < merged.size()) {
                merged[static_cast<std::size_t>(idx)] = value;
            }
        }
    }

    return merged;
}

// Computes min and max of finite values in vector.
// Falls back to [0, 1] if no finite values found.
// @param values Input vector
// @param outMin Output minimum
// @param outMax Output maximum
void minMaxFinite(const std::vector<float>& values, float& outMin, float& outMax) {
    outMin = std::numeric_limits<float>::infinity();
    outMax = -std::numeric_limits<float>::infinity();
    for (const float value : values) {
        if (std::isfinite(value)) {
            outMin = std::min(outMin, value);
            outMax = std::max(outMax, value);
        }
    }
    if (!std::isfinite(outMin) || !std::isfinite(outMax)) {
        outMin = 0.0f;
        outMax = 1.0f;
    }
    if (std::abs(outMax - outMin) < 1e-12f) {
        outMax = outMin + 1.0f;
    }
}

// Computes quantile of finite values in vector.
// @param values Input vector
// @param q Quantile to compute [0, 1]
// @return Quantile value, 0.5 if no finite values
float quantileFinite(const std::vector<float>& values, const float q) {
    std::vector<float> finite;
    finite.reserve(values.size());
    for (const float v : values) {
        if (std::isfinite(v)) {
            finite.push_back(v);
        }
    }
    if (finite.empty()) {
        return 0.5f;
    }

    const float qq = std::clamp(q, 0.0f, 1.0f);
    const std::size_t idx = static_cast<std::size_t>(std::floor(qq * static_cast<float>(finite.size() - 1)));
    std::nth_element(finite.begin(), finite.begin() + static_cast<std::ptrdiff_t>(idx), finite.end());
    return finite[idx];
}

// Creates lowercase copy of string.
// @param value Input string
// @return Lowercase version
std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

// Checks if field has any of the specified tags.
// @param fieldName Field name to check
// @param desiredTags Tags to match against
// @param fieldDisplayTags Map of field names to tag lists
// @return true if field has matching tag
bool fieldHasTag(
    const std::string& fieldName,
    const std::initializer_list<const char*> desiredTags,
    const std::unordered_map<std::string, std::vector<std::string>>& fieldDisplayTags) {
    const auto it = fieldDisplayTags.find(fieldName);
    if (it == fieldDisplayTags.end()) {
        return false;
    }

    for (const auto& tag : it->second) {
        const std::string tagLower = toLowerCopy(tag);
        for (const char* desired : desiredTags) {
            if (tagLower == desired || tagLower.find(desired) != std::string::npos) {
                return true;
            }
        }
    }

    return false;
}

// Finds field index by matching tags from fieldDisplayTags.
// @param fields Vector of field payloads
// @param fieldDisplayTags Map of field names to tag lists
// @param preferredTags Tags to search for in order
// @return Index of first matching field, -1 if not found
int findFieldIndexByTag(
    const std::vector<StateStoreSnapshot::FieldPayload>& fields,
    const std::unordered_map<std::string, std::vector<std::string>>& fieldDisplayTags,
    const std::initializer_list<const char*> preferredTags) {
    for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
        std::string name = fields[static_cast<std::size_t>(i)].spec.name;
        if (fieldHasTag(name, preferredTags, fieldDisplayTags)) {
            return i;
        }
    }
    return -1;
}

// Core display buffer builder for all display types.
// Handles scalar fields and composite terrain/water/humidity visualizations.
// @param primary Primary field values
// @param terrain Terrain/elevation field
// @param water Water/moisture field
// @param humidity Humidity field
// @param windU Wind U component
// @param windV Wind V component
// @param displayType Display type selector
// @param params Display manager parameters
// @param label Field label
// @return Configured display buffer
DisplayBuffer buildDisplayBufferCore(
    const std::vector<float>& primary,
    const std::vector<float>& terrain,
    const std::vector<float>& water,
    const std::vector<float>& humidity,
    const std::vector<float>& windU,
    const std::vector<float>& windV,
    DisplayType displayType,
    const DisplayManagerParams& params,
    const std::string& label) {
    DisplayBuffer result;
    result.values = primary;
    result.label = label;

    DisplayManagerParams tuned = params;
    tuned.lowlandThreshold = std::clamp(tuned.lowlandThreshold, 0.0f, 1.0f);
    tuned.highlandThreshold = std::clamp(tuned.highlandThreshold, tuned.lowlandThreshold + 0.01f, 1.0f);
    tuned.waterPresenceThreshold = std::clamp(tuned.waterPresenceThreshold, 0.0f, 1.0f);
    tuned.shallowWaterDepth = std::clamp(tuned.shallowWaterDepth, 0.0f, 0.5f);
    tuned.highMoistureThreshold = std::clamp(tuned.highMoistureThreshold, 0.0f, 1.0f);

    result.effectiveWaterLevel = tuned.autoWaterLevel
        ? quantileFinite(terrain, tuned.autoWaterQuantile)
        : std::clamp(tuned.waterLevel, 0.0f, 1.0f);

    if (displayType == DisplayType::ScalarField) {
        minMaxFinite(result.values, result.minValue, result.maxValue);
        return result;
    }

    result.values.assign(primary.size(), 0.0f);

    if (displayType == DisplayType::SurfaceCategory) {
        result.minValue = 0.0f;
        result.maxValue = 6.0f;
        result.label = "Surface Category";
        for (std::size_t i = 0; i < result.values.size(); ++i) {
            const float t = (i < terrain.size() && std::isfinite(terrain[i])) ? terrain[i] : 0.0f;
            const float w = (i < water.size() && std::isfinite(water[i])) ? water[i] : 0.0f;
            const float q = (i < humidity.size() && std::isfinite(humidity[i])) ? humidity[i] : 0.0f;
            const float depth = std::max(0.0f, result.effectiveWaterLevel - t);
            const float wetness = std::clamp(w * 0.5f, 0.0f, 1.0f);

            if (depth > tuned.shallowWaterDepth) {
                result.values[i] = 0.0f; // deep water
            } else if (depth > 0.02f) {
                result.values[i] = 1.0f; // shallow water
            } else if (wetness > tuned.waterPresenceThreshold || q > tuned.highMoistureThreshold) {
                result.values[i] = 2.0f; // shoreline / wet
            } else if (t < tuned.lowlandThreshold) {
                result.values[i] = 3.0f; // lowland
            } else if (t < tuned.highlandThreshold) {
                result.values[i] = 4.0f; // inland / midland
            } else if (t < tuned.highlandThreshold + 0.15f) {
                result.values[i] = 5.0f; // highland
            } else {
                result.values[i] = 6.0f; // mountain
            }
        }
        return result;
    }

    if (displayType == DisplayType::RelativeElevation) {
        result.minValue = 0.0f;
        result.maxValue = 5.0f;
        result.label = "Relative Elevation";
        for (std::size_t i = 0; i < result.values.size(); ++i) {
            const float t = (i < terrain.size() && std::isfinite(terrain[i])) ? terrain[i] : 0.0f;
            const float rel = t - result.effectiveWaterLevel;
            if (rel < -0.08f) result.values[i] = 0.0f;
            else if (rel < -0.01f) result.values[i] = 1.0f;
            else if (rel < 0.07f) result.values[i] = 2.0f;
            else if (rel < 0.18f) result.values[i] = 3.0f;
            else if (rel < 0.33f) result.values[i] = 4.0f;
            else result.values[i] = 5.0f;
        }
        return result;
    }

    if (displayType == DisplayType::MoistureMap) {
        result.minValue = 0.0f;
        result.maxValue = 1.0f;
        result.label = "Moisture Map";
        for (std::size_t i = 0; i < result.values.size(); ++i) {
            const float w = (i < water.size() && std::isfinite(water[i])) ? water[i] : 0.0f;
            const float q = (i < humidity.size() && std::isfinite(humidity[i])) ? humidity[i] : 0.0f;
            const float wetness = std::clamp(w * 0.5f, 0.0f, 1.0f);
            result.values[i] = std::clamp(0.7f * q + 0.3f * wetness, 0.0f, 1.0f);
        }
        return result;
    }

    if (displayType == DisplayType::WindField) {
        constexpr float kMaxWindMagnitude = 11.313708f;
        result.minValue = 0.0f;
        result.maxValue = 1.0f;
        result.label = "Wind Field";
        for (std::size_t i = 0; i < result.values.size(); ++i) {
            const float u = (i < windU.size() && std::isfinite(windU[i])) ? windU[i] : 0.0f;
            const float v = (i < windV.size() && std::isfinite(windV[i])) ? windV[i] : 0.0f;
            const float magnitude = std::sqrt(u * u + v * v);
            result.values[i] = std::clamp(magnitude / kMaxWindMagnitude, 0.0f, 1.0f);
        }
        return result;
    }

    result.minValue = 0.0f;
    result.maxValue = 1.0f;
    result.label = "Surface Water";
    for (std::size_t i = 0; i < result.values.size(); ++i) {
        const float t = (i < terrain.size() && std::isfinite(terrain[i])) ? terrain[i] : 0.0f;
        const float w = (i < water.size() && std::isfinite(water[i])) ? water[i] : 0.0f;
        const float depth = std::clamp((result.effectiveWaterLevel - t) * 2.0f + w * 0.9f, 0.0f, 1.0f);
        result.values[i] = depth;
    }

    return result;
}

} // namespace

// Returns human-readable label for display type.
// @param type Display type enum
// @return Static string label
const char* displayTypeLabel(const DisplayType type) {
    switch (type) {
        case DisplayType::ScalarField: return "Scalar Field";
        case DisplayType::SurfaceCategory: return "Surface Category";
        case DisplayType::RelativeElevation: return "Relative Elevation";
        case DisplayType::WaterDepth: return "Surface Water";
        case DisplayType::MoistureMap: return "Moisture Map";
        case DisplayType::WindField: return "Wind Field";
        default: return "Scalar Field";
    }
}

// Builds display buffer from state store snapshot.
// Automatically detects terrain, water, humidity, and wind fields by tags.
// @param snapshot State store snapshot with fields
// @param primaryFieldIndex Index of primary field to display
// @param displayType Visualization type
// @param includeSparseOverlay Include sparse overlay in display
// @param params Display parameters
// @param fieldDisplayTags Map of field names to display tags
// @return Display buffer ready for rendering
DisplayBuffer buildDisplayBufferFromSnapshot(
    const StateStoreSnapshot& snapshot,
    const int primaryFieldIndex,
    const DisplayType displayType,
    const bool includeSparseOverlay,
    const DisplayManagerParams& params,
    const std::unordered_map<std::string, std::vector<std::string>>& fieldDisplayTags) {
    DisplayBuffer result;
    if (snapshot.fields.empty()) {
        result.label = "No data";
        return result;
    }

    const auto& fields = snapshot.fields;
    const int clampedPrimary = std::clamp(primaryFieldIndex, 0, static_cast<int>(fields.size()) - 1);

    const auto primary = mergedFieldValues(fields[static_cast<std::size_t>(clampedPrimary)], includeSparseOverlay);

    const int terrainIdx = findFieldIndexByTag(
        fields,
        fieldDisplayTags,
        {"terrain", "elevation", "surface", "height", "altitude"});
    const int waterIdx = findFieldIndexByTag(
        fields,
        fieldDisplayTags,
        {"water", "moisture", "hydro", "water_depth"});
    const int humidityIdx = findFieldIndexByTag(
        fields,
        fieldDisplayTags,
        {"moisture", "humidity", "humid"});
    const int windUIdx = findFieldIndexByTag(
        fields,
        fieldDisplayTags,
        {"vector_x", "axis_x", "flow_x", "transport_x", "flow"});
    const int windVIdx = findFieldIndexByTag(
        fields,
        fieldDisplayTags,
        {"vector_y", "axis_y", "flow_y", "transport_y", "flow"});

    const std::vector<float> terrain = (terrainIdx >= 0)
        ? mergedFieldValues(fields[static_cast<std::size_t>(terrainIdx)], includeSparseOverlay)
        : primary;
    const std::vector<float> water = (waterIdx >= 0)
        ? mergedFieldValues(fields[static_cast<std::size_t>(waterIdx)], includeSparseOverlay)
        : std::vector<float>(primary.size(), 0.0f);
    const std::vector<float> humidity = (humidityIdx >= 0)
        ? mergedFieldValues(fields[static_cast<std::size_t>(humidityIdx)], includeSparseOverlay)
        : std::vector<float>(primary.size(), 0.0f);
    const std::vector<float> windU = (windUIdx >= 0)
        ? mergedFieldValues(fields[static_cast<std::size_t>(windUIdx)], includeSparseOverlay)
        : std::vector<float>(primary.size(), 0.0f);
    const std::vector<float> windV = (windVIdx >= 0)
        ? mergedFieldValues(fields[static_cast<std::size_t>(windVIdx)], includeSparseOverlay)
        : std::vector<float>(primary.size(), 0.0f);

    return buildDisplayBufferCore(primary, terrain, water, humidity, windU, windV, displayType, params, fields[static_cast<std::size_t>(clampedPrimary)].spec.name);
}

// Builds display buffer from terrain and water inputs for preview.
// Synthesizes humidity from water and elevation.
// @param terrain Terrain elevation values
// @param water Water/moisture values
// @param displayType Visualization type
// @param params Display parameters
// @param label Field label
// @return Display buffer for preview
DisplayBuffer buildDisplayBufferFromTerrain(
    const std::vector<float>& terrain,
    const std::vector<float>& water,
    const DisplayType displayType,
    const DisplayManagerParams& params,
    const char* label) {
    const std::vector<float> safeTerrain = terrain.empty() ? std::vector<float>(64u, 0.5f) : terrain;
    std::vector<float> safeWater;
    if (water.empty()) {
        safeWater.assign(safeTerrain.size(), 0.0f);
    } else {
        safeWater = water;
        if (safeWater.size() < safeTerrain.size()) {
            safeWater.resize(safeTerrain.size(), 0.0f);
        }
    }

    std::vector<float> safeHumidity(safeTerrain.size(), 0.0f);
    for (std::size_t i = 0; i < safeHumidity.size(); ++i) {
        const float wetness = (i < safeWater.size()) ? std::clamp(safeWater[i] * 0.5f, 0.0f, 1.0f) : 0.0f;
        const float elevation = (i < safeTerrain.size() && std::isfinite(safeTerrain[i])) ? safeTerrain[i] : 0.0f;
        safeHumidity[i] = std::clamp(0.65f * wetness + 0.35f * (1.0f - std::abs(elevation - 0.5f)), 0.0f, 1.0f);
    }

    return buildDisplayBufferCore(
        safeTerrain,
        safeTerrain,
        safeWater,
        safeHumidity,
        std::vector<float>(safeTerrain.size(), 0.0f),
        std::vector<float>(safeTerrain.size(), 0.0f),
        displayType,
        params,
        label == nullptr ? "preview" : label);
}

// Builds display buffer from fully specified preview components.
// Provides complete control over all input fields.
// @param primary Primary display field
// @param terrain Terrain/elevation field
// @param water Water field
// @param humidity Humidity field
// @param windU Wind U component
// @param windV Wind V component
// @param displayType Visualization type
// @param params Display parameters
// @param label Field label
// @return Display buffer
DisplayBuffer buildDisplayBufferFromPreviewComponents(
    const std::vector<float>& primary,
    const std::vector<float>& terrain,
    const std::vector<float>& water,
    const std::vector<float>& humidity,
    const std::vector<float>& windU,
    const std::vector<float>& windV,
    const DisplayType displayType,
    const DisplayManagerParams& params,
    const char* label) {
    const std::size_t minSize = std::max<std::size_t>(1u, primary.size());

    std::vector<float> safePrimary = primary;
    if (safePrimary.empty()) {
        safePrimary.assign(minSize, 0.0f);
    }

    auto fitToPrimarySize = [&](const std::vector<float>& source, const float fallback) {
        std::vector<float> out = source;
        if (out.empty()) {
            out.assign(safePrimary.size(), fallback);
        }
        if (out.size() < safePrimary.size()) {
            out.resize(safePrimary.size(), fallback);
        }
        return out;
    };

    std::vector<float> safeTerrain = fitToPrimarySize(terrain, 0.0f);
    std::vector<float> safeWater = fitToPrimarySize(water, 0.0f);
    std::vector<float> safeHumidity = fitToPrimarySize(humidity, 0.0f);
    std::vector<float> safeWindU = fitToPrimarySize(windU, 0.0f);
    std::vector<float> safeWindV = fitToPrimarySize(windV, 0.0f);

    return buildDisplayBufferCore(
        safePrimary,
        safeTerrain,
        safeWater,
        safeHumidity,
        safeWindU,
        safeWindV,
        displayType,
        params,
        label == nullptr ? "preview" : label);
}

} // namespace ws::gui
