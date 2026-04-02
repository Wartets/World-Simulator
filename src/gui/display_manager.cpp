#include "ws/gui/display_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <string>

namespace ws::gui {
namespace {

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

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

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

int findFieldIndexByTagOrKeyword(
    const std::vector<StateStoreSnapshot::FieldPayload>& fields,
    const std::unordered_map<std::string, std::vector<std::string>>& fieldDisplayTags,
    const std::initializer_list<const char*> preferredTags,
    const std::initializer_list<const char*> keywords) {
    for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
        std::string name = fields[static_cast<std::size_t>(i)].spec.name;
        const std::string nameLower = toLowerCopy(name);
        if (fieldHasTag(name, preferredTags, fieldDisplayTags)) {
            return i;
        }
        for (const char* keyword : keywords) {
            if (nameLower.find(keyword) != std::string::npos) {
                return i;
            }
        }
    }
    return -1;
}

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

    const int terrainIdx = findFieldIndexByTagOrKeyword(
        fields,
        fieldDisplayTags,
        {"terrain", "elevation", "surface", "height", "altitude"},
        {"terrain_elevation", "elevation", "terrain", "height", "altitude"});
    const int waterIdx = findFieldIndexByTagOrKeyword(
        fields,
        fieldDisplayTags,
        {"water", "moisture", "hydro", "water_depth"},
        {"surface_water", "water", "hydro"});
    const int humidityIdx = findFieldIndexByTagOrKeyword(
        fields,
        fieldDisplayTags,
        {"moisture", "humidity", "humid"},
        {"humidity_q", "humidity", "humid", "moisture"});
    const int windUIdx = findFieldIndexByTagOrKeyword(
        fields,
        fieldDisplayTags,
        {"vector_x", "wind_u", "wind"},
        {"wind_u", "wind"});
    const int windVIdx = findFieldIndexByTagOrKeyword(
        fields,
        fieldDisplayTags,
        {"vector_y", "wind_v", "wind"},
        {"wind_v"});

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

} // namespace ws::gui
