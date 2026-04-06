#pragma once

// Core dependencies
#include "ws/core/profile.hpp"
#include "ws/core/runtime.hpp"

// Standard library
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ws::app {

// =============================================================================
// Launch Configuration
// =============================================================================

// Configuration for launching a simulation.
struct LaunchConfig {
    std::uint64_t seed = 42;
    GridSpec grid{128, 128};
    ModelTier tier = ModelTier::A;
    TemporalPolicy temporalPolicy = TemporalPolicy::UniformA;
    InitialConditionConfig initialConditions{};
};

// =============================================================================
// Launch Preset
// =============================================================================

// A named preset for launching simulations.
struct LaunchPreset {
    std::string name;
    LaunchConfig config;
    std::string description;
};

// =============================================================================
// Model Catalog Entry
// =============================================================================

// Entry in the model catalog.
struct ModelCatalogEntry {
    std::string key;
    std::filesystem::path path;
    bool isDirectory = false;
};

// =============================================================================
// Field Summary
// =============================================================================

// Summary statistics for a simulation field.
struct FieldSummary {
    std::size_t validCount = 0;
    std::size_t invalidCount = 0;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    double average = 0.0;
};

// Converts a string to lowercase.
[[nodiscard]] std::string toLower(std::string value);
// Removes leading and trailing whitespace from a string.
[[nodiscard]] std::string trim(std::string value);
// Converts TemporalPolicy to string.
[[nodiscard]] std::string temporalPolicyToString(TemporalPolicy policy);
// Parses a string into TemporalPolicy.
[[nodiscard]] std::optional<TemporalPolicy> parseTemporalPolicy(const std::string& token);
// Converts InitialConditionType to string.
[[nodiscard]] std::string initialConditionTypeToString(InitialConditionType type);
// Parses a string into InitialConditionType.
[[nodiscard]] std::optional<InitialConditionType> parseInitialConditionType(const std::string& token);
// Parses a string into ModelTier.
[[nodiscard]] std::optional<ModelTier> parseTier(const std::string& token);
// Parses a string into uint64_t.
[[nodiscard]] std::optional<std::uint64_t> parseU64(const std::string& token);
// Parses a string into uint32_t.
[[nodiscard]] std::optional<std::uint32_t> parseU32(const std::string& token);
[[nodiscard]] std::optional<float> parseFloat(const std::string& token);
[[nodiscard]] std::string normalizeModelKey(std::string value);
[[nodiscard]] std::vector<ModelCatalogEntry> listAvailableModels(
    const std::filesystem::path& modelsRoot = std::filesystem::path("models"));

[[nodiscard]] ProfileResolverInput buildProfileInput(ModelTier tier);
[[nodiscard]] RuntimeConfig makeRuntimeConfig(const LaunchConfig& launchConfig);

[[nodiscard]] const std::vector<LaunchPreset>& allPresets();
[[nodiscard]] std::optional<LaunchPreset> presetByName(const std::string& name);

[[nodiscard]] FieldSummary summarizeField(const StateStoreSnapshot::FieldPayload& field);
[[nodiscard]] char heatmapGlyph(float value, float minValue, float maxValue);

} // namespace ws::app
