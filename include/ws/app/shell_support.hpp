#pragma once

#include "ws/core/profile.hpp"
#include "ws/core/runtime.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ws::app {

struct LaunchConfig {
    std::uint64_t seed = 42;
    GridSpec grid{128, 128};
    ModelTier tier = ModelTier::A;
    TemporalPolicy temporalPolicy = TemporalPolicy::UniformA;
    InitialConditionConfig initialConditions{};
};

struct LaunchPreset {
    std::string name;
    LaunchConfig config;
    std::string description;
};

struct FieldSummary {
    std::size_t validCount = 0;
    std::size_t invalidCount = 0;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    double average = 0.0;
};

[[nodiscard]] std::string toLower(std::string value);
[[nodiscard]] std::string trim(std::string value);
[[nodiscard]] std::string temporalPolicyToString(TemporalPolicy policy);
[[nodiscard]] std::optional<TemporalPolicy> parseTemporalPolicy(const std::string& token);
[[nodiscard]] std::optional<ModelTier> parseTier(const std::string& token);
[[nodiscard]] std::optional<std::uint64_t> parseU64(const std::string& token);
[[nodiscard]] std::optional<std::uint32_t> parseU32(const std::string& token);
[[nodiscard]] std::optional<float> parseFloat(const std::string& token);

[[nodiscard]] ProfileResolverInput buildProfileInput(ModelTier tier);
[[nodiscard]] RuntimeConfig makeRuntimeConfig(const LaunchConfig& launchConfig);

[[nodiscard]] const std::vector<LaunchPreset>& allPresets();
[[nodiscard]] std::optional<LaunchPreset> presetByName(const std::string& name);

[[nodiscard]] FieldSummary summarizeField(const StateStoreSnapshot::FieldPayload& field);
[[nodiscard]] char heatmapGlyph(float value, float minValue, float maxValue);

} // namespace ws::app
