#pragma once

#include "ws/core/runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ws::initialization {

// Describes a single variable in the model.
struct VariableDescriptor {
    std::string id;
    std::string role;
    std::string support;
    std::string type;
    std::string units;
    std::vector<std::string> tags;
    std::optional<std::string> displayType;
    std::optional<std::string> vectorComponent;
    std::vector<std::string> visualizationRoles;
    std::vector<std::string> initializationHints;
    bool hasDomainMin = false;
    bool hasDomainMax = false;
    float domainMin = 0.0f;
    float domainMax = 0.0f;
};

// Default initialization settings for a variable.
struct VariableInitializationDefault {
    bool enabled = false;
    bool hasBaseValue = false;
    float baseValue = 0.0f;
    bool hasRestrictionMode = false;
    int restrictionMode = 0;
    bool hasClampMin = false;
    float clampMin = 0.0f;
    bool hasClampMax = false;
    float clampMax = 1.0f;
};

// Complete catalog of model variables and initialization metadata.
struct ModelVariableCatalog {
    std::filesystem::path sourceModelPath;
    std::string modelId;
    std::optional<InitialConditionType> preferredInitializationMode;
    std::string expectedStartDataType;
    std::vector<InitialConditionType> supportedInitializationModes;
    std::string preferredDisplayVariable;
    std::unordered_map<std::string, VariableInitializationDefault> variableInitializationDefaults;
    std::unordered_map<std::string, float> generationParameterOverrides;
    std::vector<VariableDescriptor> variables;

    [[nodiscard]] std::vector<std::string> cellVariableIds() const;
    [[nodiscard]] std::vector<std::string> cellStateVariableIds() const;
};

// Request for initializing a model with specific conditions.
struct InitializationRequest {
    InitialConditionType type = InitialConditionType::Terrain;
    bool requireMetadataHints = true;
    std::optional<std::string> conwayTargetOverride;
    std::optional<std::string> grayTargetAOverride;
    std::optional<std::string> grayTargetBOverride;
    std::optional<std::string> wavesTargetOverride;
};

// A single binding decision linking initialization to a variable.
struct BindingDecision {
    std::string bindingKey;
    std::string variableId;
    float confidence = 0.0f;
    std::string rationale;
    bool resolved = false;
    bool required = true;
};

// An issue encountered during binding resolution.
struct BindingIssue {
    std::string code;
    std::string message;
    bool blocking = true;
};

// Complete initialization binding plan for a model.
struct InitializationBindingPlan {
    InitialConditionType type = InitialConditionType::Terrain;
    std::vector<BindingDecision> decisions;
    std::vector<BindingIssue> issues;
    std::uint64_t fingerprint = 0;

    [[nodiscard]] bool hasBlockingIssues() const;
};

// Loads the variable catalog from a model file.
[[nodiscard]] bool loadModelVariableCatalog(
    const std::filesystem::path& modelPath,
    ModelVariableCatalog& outCatalog,
    std::string& message);

// Loads parameter controls from a model file.
[[nodiscard]] bool loadModelParameterControls(
    const std::filesystem::path& modelPath,
    std::vector<ParameterControl>& controls,
    std::string& message);

// Loads execution specification from a model file.
[[nodiscard]] bool loadModelExecutionSpec(
    const std::filesystem::path& modelPath,
    ModelExecutionSpec& executionSpec,
    std::string& message);

// Loads display specification from a model file.
[[nodiscard]] bool loadModelDisplaySpec(
    const std::filesystem::path& modelPath,
    ModelDisplaySpec& displaySpec,
    std::string& message);

// Builds an initialization binding plan from catalog and request.
[[nodiscard]] InitializationBindingPlan buildBindingPlan(
    const ModelVariableCatalog& catalog,
    const InitializationRequest& request);

} // namespace ws::initialization
