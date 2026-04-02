#pragma once

#include "ws/core/runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ws::initialization {

struct VariableDescriptor {
    std::string id;
    std::string role;
    std::string support;
    std::string type;
    std::string units;
    std::vector<std::string> tags;
    bool hasDomainMin = false;
    bool hasDomainMax = false;
    float domainMin = 0.0f;
    float domainMax = 0.0f;
};

struct ModelVariableCatalog {
    std::filesystem::path sourceModelPath;
    std::string modelId;
    std::vector<VariableDescriptor> variables;

    [[nodiscard]] std::vector<std::string> cellVariableIds() const;
    [[nodiscard]] std::vector<std::string> cellStateVariableIds() const;
};

struct InitializationRequest {
    InitialConditionType type = InitialConditionType::Terrain;
    std::optional<std::string> conwayTargetOverride;
    std::optional<std::string> grayTargetAOverride;
    std::optional<std::string> grayTargetBOverride;
    std::optional<std::string> wavesTargetOverride;
};

struct BindingDecision {
    std::string bindingKey;
    std::string variableId;
    float confidence = 0.0f;
    std::string rationale;
    bool resolved = false;
    bool required = true;
};

struct BindingIssue {
    std::string code;
    std::string message;
    bool blocking = true;
};

struct InitializationBindingPlan {
    InitialConditionType type = InitialConditionType::Terrain;
    std::vector<BindingDecision> decisions;
    std::vector<BindingIssue> issues;
    std::uint64_t fingerprint = 0;

    [[nodiscard]] bool hasBlockingIssues() const;
};

[[nodiscard]] bool loadModelVariableCatalog(
    const std::filesystem::path& modelPath,
    ModelVariableCatalog& outCatalog,
    std::string& message);

[[nodiscard]] bool loadModelParameterControls(
    const std::filesystem::path& modelPath,
    std::vector<ParameterControl>& controls,
    std::string& message);

[[nodiscard]] bool loadModelExecutionSpec(
    const std::filesystem::path& modelPath,
    ModelExecutionSpec& executionSpec,
    std::string& message);

[[nodiscard]] bool loadModelDisplaySpec(
    const std::filesystem::path& modelPath,
    ModelDisplaySpec& displaySpec,
    std::string& message);

[[nodiscard]] InitializationBindingPlan buildBindingPlan(
    const ModelVariableCatalog& catalog,
    const InitializationRequest& request);

} // namespace ws::initialization
