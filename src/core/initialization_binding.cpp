#include "ws/core/initialization_binding.hpp"

#include "ws/core/model_parser.hpp"

#include "ws/core/determinism.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>

namespace ws::initialization {

namespace {

using json = nlohmann::json;

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::optional<float> readDomainBound(const json& domain, const char* key) {
    if (!domain.contains(key)) {
        return std::nullopt;
    }
    const auto& value = domain.at(key);
    if (value.is_number_float() || value.is_number_integer() || value.is_number_unsigned()) {
        return value.get<float>();
    }
    return std::nullopt;
}

void normalizeStringVector(std::vector<std::string>& values) {
    values.erase(
        std::remove_if(values.begin(), values.end(), [](const std::string& value) { return value.empty(); }),
        values.end());
    for (auto& value : values) {
        value = toLowerCopy(value);
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

bool populateCatalogFromModelJson(
    const std::filesystem::path& modelPath,
    const std::string& modelJson,
    ModelVariableCatalog& outCatalog,
    std::string& message) {
    try {
        const json parsed = json::parse(modelJson);
        if (parsed.contains("id") && parsed["id"].is_string()) {
            outCatalog.modelId = parsed["id"].get<std::string>();
        }

        if (!parsed.contains("variables") || !parsed["variables"].is_array()) {
            message = "model_catalog_load_failed reason=variables_missing";
            return false;
        }

        for (const auto& variable : parsed["variables"]) {
            if (!variable.is_object()) {
                continue;
            }
            if (!variable.contains("id") || !variable["id"].is_string()) {
                continue;
            }

            VariableDescriptor descriptor;
            descriptor.id = variable["id"].get<std::string>();
            descriptor.role = variable.contains("role") && variable["role"].is_string() ? variable["role"].get<std::string>() : std::string();
            descriptor.support = variable.contains("support") && variable["support"].is_string() ? variable["support"].get<std::string>() : std::string();
            descriptor.type = variable.contains("type") && variable["type"].is_string() ? variable["type"].get<std::string>() : std::string();
            descriptor.units = variable.contains("units") && variable["units"].is_string() ? variable["units"].get<std::string>() : std::string();

            if (variable.contains("display_tags") && variable["display_tags"].is_array()) {
                for (const auto& tagValue : variable["display_tags"]) {
                    if (tagValue.is_string()) {
                        descriptor.tags.push_back(tagValue.get<std::string>());
                    }
                }
            }
            if (variable.contains("display_channel") && variable["display_channel"].is_string()) {
                descriptor.tags.push_back(variable["display_channel"].get<std::string>());
            }
            normalizeStringVector(descriptor.tags);

            const auto minBound = readDomainBound(variable, "min");
            const auto maxBound = readDomainBound(variable, "max");
            if (minBound.has_value()) {
                descriptor.hasDomainMin = true;
                descriptor.domainMin = *minBound;
            }
            if (maxBound.has_value()) {
                descriptor.hasDomainMax = true;
                descriptor.domainMax = *maxBound;
            }

            outCatalog.variables.push_back(std::move(descriptor));
        }

        std::sort(outCatalog.variables.begin(), outCatalog.variables.end(), [](const VariableDescriptor& lhs, const VariableDescriptor& rhs) {
            return lhs.id < rhs.id;
        });
        outCatalog.variables.erase(
            std::unique(outCatalog.variables.begin(), outCatalog.variables.end(), [](const VariableDescriptor& lhs, const VariableDescriptor& rhs) {
                return lhs.id == rhs.id;
            }),
            outCatalog.variables.end());

        message = "model_catalog_ready variables=" + std::to_string(outCatalog.variables.size());
        return true;
    } catch (const std::exception& exception) {
        outCatalog = ModelVariableCatalog{};
        outCatalog.sourceModelPath = modelPath;
        message = std::string("model_catalog_load_failed error=") + exception.what();
        return false;
    }
}

std::optional<std::size_t> findVariableIndexById(const ModelVariableCatalog& catalog, const std::string& id) {
    for (std::size_t i = 0; i < catalog.variables.size(); ++i) {
        if (catalog.variables[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

float scoreCandidate(
    const VariableDescriptor& variable,
    const std::vector<std::string>& semanticHints) {
    if (variable.support != "cell") {
        return -std::numeric_limits<float>::infinity();
    }

    float score = 0.0f;
    bool hasSemanticHit = false;

    if (variable.role == "state") {
        score += 2.0f;
    } else if (variable.role == "derived") {
        score += 0.3f;
    } else if (variable.role == "parameter") {
        score -= 1.5f;
    }

    const std::string idLower = toLowerCopy(variable.id);

    for (const auto& semanticHint : semanticHints) {
        if (semanticHint.empty()) {
            continue;
        }

        const bool roleMatch = (toLowerCopy(variable.role) == semanticHint);
        const bool tagMatch = std::binary_search(variable.tags.begin(), variable.tags.end(), semanticHint);
        const bool idExactMatch = (idLower == semanticHint);
        const bool idContainsMatch = (idLower.find(semanticHint) != std::string::npos);

        if (roleMatch || tagMatch || idExactMatch) {
            hasSemanticHit = true;
            score += 2.2f;
            continue;
        }
        if (idContainsMatch) {
            hasSemanticHit = true;
            score += 0.8f;
        }
    }

    const std::string typeLower = toLowerCopy(variable.type);
    if (typeLower == "u32" || typeLower == "i32") {
        score += 0.2f;
    }

    if (variable.units == "1") {
        score += 0.1f;
    }

    return score;
}

std::optional<std::string> pickBestCandidate(
    const ModelVariableCatalog& catalog,
    const std::vector<std::string>& semanticHints,
    const float minimumScore,
    const std::optional<std::string>& exclude = std::nullopt) {
    std::vector<std::pair<float, std::string>> scored;
    scored.reserve(catalog.variables.size());

    for (const auto& variable : catalog.variables) {
        if (variable.support != "cell") {
            continue;
        }
        if (exclude.has_value() && variable.id == *exclude) {
            continue;
        }
        scored.emplace_back(
            scoreCandidate(variable, semanticHints),
            variable.id);
    }

    if (scored.empty()) {
        return std::nullopt;
    }

    std::sort(scored.begin(), scored.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.first != rhs.first) {
            return lhs.first > rhs.first;
        }
        return lhs.second < rhs.second;
    });

    if (scored.front().first < minimumScore) {
        return std::nullopt;
    }

    return scored.front().second;
}

void addDecisionFromOverrideOrResolver(
    InitializationBindingPlan& plan,
    const ModelVariableCatalog& catalog,
    const std::string& key,
    const std::optional<std::string>& overrideValue,
    const std::vector<std::string>& semanticHints,
    const std::optional<std::string>& exclude = std::nullopt) {
    BindingDecision decision;
    decision.bindingKey = key;

    if (overrideValue.has_value() && !overrideValue->empty()) {
        decision.variableId = *overrideValue;
        const auto idx = findVariableIndexById(catalog, *overrideValue);
        if (idx.has_value()) {
            decision.resolved = true;
            decision.confidence = 1.0f;
            decision.rationale = "explicit_override_valid";
        } else if (catalog.variables.empty()) {
            decision.resolved = true;
            decision.confidence = 0.4f;
            decision.rationale = "explicit_override_unverified_catalog_unavailable";
            plan.issues.push_back(BindingIssue{
                "binding.catalog_missing",
                "Model catalog unavailable; binding override cannot be verified.",
                false});
        } else {
            decision.resolved = false;
            decision.confidence = 0.0f;
            decision.rationale = "explicit_override_unknown_variable";
            plan.issues.push_back(BindingIssue{
                "binding.override_unknown_variable",
                "Override variable '" + *overrideValue + "' was not found in model variables.",
                true});
        }

        plan.decisions.push_back(std::move(decision));
        return;
    }

    const float minimumScore = 0.75f;
    const auto picked = pickBestCandidate(
        catalog,
        semanticHints,
        minimumScore,
        exclude);
    if (picked.has_value()) {
        decision.variableId = *picked;
        decision.resolved = true;
        decision.confidence = 0.85f;
        decision.rationale = "auto_resolved_from_catalog_metadata";
    } else {
        decision.resolved = false;
        decision.confidence = 0.0f;
        decision.rationale = "no_candidate_available";
        plan.issues.push_back(BindingIssue{
            "binding.missing_candidate",
            "No candidate variable available for binding '" + key + "'.",
            true});
    }

    plan.decisions.push_back(std::move(decision));
}

std::uint64_t computePlanFingerprint(const InitializationBindingPlan& plan) {
    std::ostringstream digest;
    digest << "type=" << static_cast<int>(plan.type) << ';';
    for (const auto& decision : plan.decisions) {
        digest << decision.bindingKey << '=' << decision.variableId << ':'
               << decision.resolved << ':' << decision.confidence << ';';
    }
    for (const auto& issue : plan.issues) {
        digest << issue.code << ':' << issue.blocking << ';';
    }
    return DeterministicHash::hashString(digest.str());
}

} // namespace

std::vector<std::string> ModelVariableCatalog::cellVariableIds() const {
    std::vector<std::string> out;
    out.reserve(variables.size());
    for (const auto& variable : variables) {
        if (variable.support == "cell") {
            out.push_back(variable.id);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::string> ModelVariableCatalog::cellStateVariableIds() const {
    std::vector<std::string> primary;
    std::vector<std::string> fallback;
    primary.reserve(variables.size());
    fallback.reserve(variables.size());

    for (const auto& variable : variables) {
        if (variable.support != "cell") {
            continue;
        }
        fallback.push_back(variable.id);
        if (variable.role == "state") {
            primary.push_back(variable.id);
        }
    }

    auto& selected = !primary.empty() ? primary : fallback;
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    return selected;
}

bool InitializationBindingPlan::hasBlockingIssues() const {
    return std::any_of(issues.begin(), issues.end(), [](const BindingIssue& issue) {
        return issue.blocking;
    });
}

bool loadModelVariableCatalog(
    const std::filesystem::path& modelPath,
    ModelVariableCatalog& outCatalog,
    std::string& message) {
    outCatalog = ModelVariableCatalog{};
    outCatalog.sourceModelPath = modelPath;
    try {
        const auto ctx = ws::ModelParser::load(modelPath);
        if (populateCatalogFromModelJson(modelPath, ctx.model_json, outCatalog, message)) {
            return true;
        }

        // Preserve the legacy loader behavior as a fallback for incomplete model packages.
        if (std::filesystem::is_directory(modelPath)) {
            const auto legacyJsonPath = modelPath / "model.json";
            if (std::filesystem::exists(legacyJsonPath)) {
                std::ifstream in(legacyJsonPath);
                if (!in) {
                    message = "model_catalog_load_failed reason=file_open_failed path=" + legacyJsonPath.string();
                    return false;
                }
                const std::string modelJson((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                return populateCatalogFromModelJson(legacyJsonPath, modelJson, outCatalog, message);
            }
        }

        message = "model_catalog_load_failed reason=unsupported_model_package path=" + modelPath.string();
        return false;
    } catch (const std::exception& exception) {
        if (std::filesystem::is_directory(modelPath)) {
            const std::filesystem::path modelJsonPath = modelPath / "model.json";
            if (std::filesystem::exists(modelJsonPath)) {
                try {
                    std::ifstream in(modelJsonPath);
                    if (!in) {
                        message = "model_catalog_load_failed reason=file_open_failed path=" + modelJsonPath.string();
                        return false;
                    }

                    const std::string modelJson((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    return populateCatalogFromModelJson(modelJsonPath, modelJson, outCatalog, message);
                } catch (const std::exception& fallbackException) {
                    outCatalog = ModelVariableCatalog{};
                    outCatalog.sourceModelPath = modelPath;
                    message = std::string("model_catalog_load_failed error=") + fallbackException.what();
                    return false;
                }
            }
        }

        outCatalog = ModelVariableCatalog{};
        outCatalog.sourceModelPath = modelPath;
        message = std::string("model_catalog_load_failed error=") + exception.what();
        return false;
    }
}

bool loadModelParameterControls(
    const std::filesystem::path& modelPath,
    std::vector<ParameterControl>& controls,
    std::string& message) {
    controls.clear();

    try {
        const auto ctx = ws::ModelParser::load(modelPath);
        const json parsed = json::parse(ctx.model_json);

        std::map<std::string, std::pair<std::optional<float>, std::optional<float>>, std::less<>> domainBounds;
        if (parsed.contains("domains") && parsed["domains"].is_object()) {
            for (auto it = parsed["domains"].begin(); it != parsed["domains"].end(); ++it) {
                if (!it.value().is_object()) {
                    continue;
                }
                domainBounds.insert_or_assign(it.key(), std::make_pair(
                    readDomainBound(it.value(), "min"),
                    readDomainBound(it.value(), "max")));
            }
        }

        if (!parsed.contains("variables") || !parsed["variables"].is_array()) {
            message = "model_parameter_controls_failed reason=variables_missing";
            return false;
        }

        for (const auto& variable : parsed["variables"]) {
            if (!variable.is_object()) {
                continue;
            }
            if (!variable.contains("id") || !variable["id"].is_string()) {
                continue;
            }

            const std::string id = variable["id"].get<std::string>();
            const std::string role = variable.contains("role") && variable["role"].is_string()
                ? variable["role"].get<std::string>()
                : std::string{};
            const std::string support = variable.contains("support") && variable["support"].is_string()
                ? variable["support"].get<std::string>()
                : std::string{};

            if (role != "parameter" || support != "cell") {
                continue;
            }

            ParameterControl control;
            control.name = "model.parameter." + id;
            control.targetVariable = id;
            control.units = variable.contains("units") && variable["units"].is_string()
                ? variable["units"].get<std::string>()
                : std::string{"1"};

            if (variable.contains("initial_value") &&
                (variable["initial_value"].is_number_float() || variable["initial_value"].is_number_integer() || variable["initial_value"].is_number_unsigned())) {
                control.defaultValue = variable["initial_value"].get<float>();
                control.value = control.defaultValue;
            }

            auto minBound = readDomainBound(variable, "min");
            auto maxBound = readDomainBound(variable, "max");

            if (variable.contains("domain") && variable["domain"].is_string()) {
                const auto domainIt = domainBounds.find(variable["domain"].get<std::string>());
                if (domainIt != domainBounds.end()) {
                    if (!minBound.has_value()) {
                        minBound = domainIt->second.first;
                    }
                    if (!maxBound.has_value()) {
                        maxBound = domainIt->second.second;
                    }
                }
            }

            if (minBound.has_value()) {
                control.minValue = *minBound;
            }
            if (maxBound.has_value()) {
                control.maxValue = *maxBound;
            }
            if (control.minValue > control.maxValue) {
                std::swap(control.minValue, control.maxValue);
            }
            control.defaultValue = std::clamp(control.defaultValue, control.minValue, control.maxValue);
            control.value = std::clamp(control.value, control.minValue, control.maxValue);

            controls.push_back(std::move(control));
        }

        std::sort(controls.begin(), controls.end(), [](const ParameterControl& lhs, const ParameterControl& rhs) {
            return lhs.name < rhs.name;
        });

        message = "model_parameter_controls_ready count=" + std::to_string(controls.size());
        return true;
    } catch (const std::exception& exception) {
        controls.clear();
        message = std::string("model_parameter_controls_failed error=") + exception.what();
        return false;
    }
}

bool loadModelExecutionSpec(
    const std::filesystem::path& modelPath,
    ModelExecutionSpec& executionSpec,
    std::string& message) {
    executionSpec = ModelExecutionSpec{};

    try {
        const auto ctx = ws::ModelParser::load(modelPath);
        const json parsed = json::parse(ctx.model_json);
        json metadataParsed;
        if (!ctx.metadata_json.empty()) {
            metadataParsed = json::parse(ctx.metadata_json);
        }

        if (parsed.contains("variables") && parsed["variables"].is_array()) {
            for (const auto& variable : parsed["variables"]) {
                if (!variable.is_object()) {
                    continue;
                }
                if (!variable.contains("id") || !variable["id"].is_string()) {
                    continue;
                }

                const std::string support = variable.contains("support") && variable["support"].is_string()
                    ? variable["support"].get<std::string>()
                    : std::string{};
                if (support != "cell") {
                    continue;
                }

                const std::string type = variable.contains("type") && variable["type"].is_string()
                    ? variable["type"].get<std::string>()
                    : std::string{"f32"};
                const bool scalarType =
                    (type == "f32") || (type == "f64") ||
                    (type == "i32") || (type == "u32") ||
                    (type == "bool");
                if (!scalarType) {
                    continue;
                }

                executionSpec.cellScalarVariableIds.push_back(variable["id"].get<std::string>());
            }
        }

        if (parsed.contains("stages") && parsed["stages"].is_array()) {
            for (const auto& stage : parsed["stages"]) {
                if (!stage.is_object() || !stage.contains("id") || !stage["id"].is_string()) {
                    continue;
                }
                executionSpec.stageOrder.push_back(stage["id"].get<std::string>());
            }
        }

        if (parsed.contains("diagnostics") && parsed["diagnostics"].is_object()) {
            const auto& diagnostics = parsed["diagnostics"];
            if (diagnostics.contains("conserved_variables") && diagnostics["conserved_variables"].is_array()) {
                for (const auto& name : diagnostics["conserved_variables"]) {
                    if (name.is_string()) {
                        executionSpec.conservedVariables.push_back(name.get<std::string>());
                    }
                }
            }
            if (executionSpec.conservedVariables.empty() &&
                diagnostics.contains("conservation") && diagnostics["conservation"].is_array()) {
                for (const auto& name : diagnostics["conservation"]) {
                    if (name.is_string()) {
                        executionSpec.conservedVariables.push_back(name.get<std::string>());
                    }
                }
            }
        }

        if (metadataParsed.is_object() &&
            metadataParsed.contains("runtime_field_aliases") &&
            metadataParsed["runtime_field_aliases"].is_object()) {
            for (auto it = metadataParsed["runtime_field_aliases"].begin();
                 it != metadataParsed["runtime_field_aliases"].end();
                 ++it) {
                if (!it.value().is_string()) {
                    continue;
                }
                const std::string semanticKey = it.key();
                const std::string variableId = it.value().get<std::string>();
                if (semanticKey.empty() || variableId.empty()) {
                    continue;
                }
                executionSpec.semanticFieldAliases.insert_or_assign(semanticKey, variableId);
            }
        }

        auto normalize = [](std::vector<std::string>& values) {
            values.erase(
                std::remove_if(values.begin(), values.end(), [](const std::string& value) { return value.empty(); }),
                values.end());
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
        };

        normalize(executionSpec.cellScalarVariableIds);
        normalize(executionSpec.conservedVariables);

        executionSpec.stageOrder.erase(
            std::remove_if(executionSpec.stageOrder.begin(), executionSpec.stageOrder.end(), [](const std::string& stage) {
                return stage.empty();
            }),
            executionSpec.stageOrder.end());
        executionSpec.stageOrder.erase(
            std::unique(executionSpec.stageOrder.begin(), executionSpec.stageOrder.end()),
            executionSpec.stageOrder.end());

        message = "model_execution_spec_ready variables=" + std::to_string(executionSpec.cellScalarVariableIds.size()) +
            " stages=" + std::to_string(executionSpec.stageOrder.size()) +
            " conserved=" + std::to_string(executionSpec.conservedVariables.size()) +
            " aliases=" + std::to_string(executionSpec.semanticFieldAliases.size());
        return true;
    } catch (const std::exception& exception) {
        // Fallback path for model packages where IR parsing fails but model.json / metadata.json are valid.
        try {
            if (!std::filesystem::is_directory(modelPath)) {
                executionSpec = ModelExecutionSpec{};
                message = std::string("model_execution_spec_failed error=") + exception.what();
                return false;
            }

            const auto modelJsonPath = modelPath / "model.json";
            if (!std::filesystem::exists(modelJsonPath)) {
                executionSpec = ModelExecutionSpec{};
                message = std::string("model_execution_spec_failed error=") + exception.what();
                return false;
            }

            std::ifstream modelIn(modelJsonPath);
            if (!modelIn) {
                executionSpec = ModelExecutionSpec{};
                message = "model_execution_spec_failed reason=file_open_failed path=" + modelJsonPath.string();
                return false;
            }

            const std::string modelJson((std::istreambuf_iterator<char>(modelIn)), std::istreambuf_iterator<char>());
            const json parsed = json::parse(modelJson);

            json metadataParsed;
            const auto metadataJsonPath = modelPath / "metadata.json";
            if (std::filesystem::exists(metadataJsonPath)) {
                std::ifstream metadataIn(metadataJsonPath);
                if (metadataIn) {
                    const std::string metadataJson((std::istreambuf_iterator<char>(metadataIn)), std::istreambuf_iterator<char>());
                    if (!metadataJson.empty()) {
                        metadataParsed = json::parse(metadataJson);
                    }
                }
            }

            executionSpec = ModelExecutionSpec{};
            if (parsed.contains("variables") && parsed["variables"].is_array()) {
                for (const auto& variable : parsed["variables"]) {
                    if (!variable.is_object()) {
                        continue;
                    }
                    if (!variable.contains("id") || !variable["id"].is_string()) {
                        continue;
                    }

                    const std::string support = variable.contains("support") && variable["support"].is_string()
                        ? variable["support"].get<std::string>()
                        : std::string{};
                    if (support != "cell") {
                        continue;
                    }

                    const std::string type = variable.contains("type") && variable["type"].is_string()
                        ? variable["type"].get<std::string>()
                        : std::string{"f32"};
                    const bool scalarType =
                        (type == "f32") || (type == "f64") ||
                        (type == "i32") || (type == "u32") ||
                        (type == "bool");
                    if (!scalarType) {
                        continue;
                    }

                    executionSpec.cellScalarVariableIds.push_back(variable["id"].get<std::string>());
                }
            }

            if (parsed.contains("stages") && parsed["stages"].is_array()) {
                for (const auto& stage : parsed["stages"]) {
                    if (!stage.is_object() || !stage.contains("id") || !stage["id"].is_string()) {
                        continue;
                    }
                    executionSpec.stageOrder.push_back(stage["id"].get<std::string>());
                }
            }

            if (parsed.contains("diagnostics") && parsed["diagnostics"].is_object()) {
                const auto& diagnostics = parsed["diagnostics"];
                if (diagnostics.contains("conserved_variables") && diagnostics["conserved_variables"].is_array()) {
                    for (const auto& name : diagnostics["conserved_variables"]) {
                        if (name.is_string()) {
                            executionSpec.conservedVariables.push_back(name.get<std::string>());
                        }
                    }
                }
                if (executionSpec.conservedVariables.empty() &&
                    diagnostics.contains("conservation") && diagnostics["conservation"].is_array()) {
                    for (const auto& name : diagnostics["conservation"]) {
                        if (name.is_string()) {
                            executionSpec.conservedVariables.push_back(name.get<std::string>());
                        }
                    }
                }
            }

            if (metadataParsed.is_object() &&
                metadataParsed.contains("runtime_field_aliases") &&
                metadataParsed["runtime_field_aliases"].is_object()) {
                for (auto it = metadataParsed["runtime_field_aliases"].begin();
                     it != metadataParsed["runtime_field_aliases"].end();
                     ++it) {
                    if (!it.value().is_string()) {
                        continue;
                    }
                    const std::string semanticKey = it.key();
                    const std::string variableId = it.value().get<std::string>();
                    if (semanticKey.empty() || variableId.empty()) {
                        continue;
                    }
                    executionSpec.semanticFieldAliases.insert_or_assign(semanticKey, variableId);
                }
            }

            auto normalize = [](std::vector<std::string>& values) {
                values.erase(
                    std::remove_if(values.begin(), values.end(), [](const std::string& value) { return value.empty(); }),
                    values.end());
                std::sort(values.begin(), values.end());
                values.erase(std::unique(values.begin(), values.end()), values.end());
            };

            normalize(executionSpec.cellScalarVariableIds);
            normalize(executionSpec.conservedVariables);
            executionSpec.stageOrder.erase(
                std::remove_if(executionSpec.stageOrder.begin(), executionSpec.stageOrder.end(), [](const std::string& stage) {
                    return stage.empty();
                }),
                executionSpec.stageOrder.end());
            executionSpec.stageOrder.erase(
                std::unique(executionSpec.stageOrder.begin(), executionSpec.stageOrder.end()),
                executionSpec.stageOrder.end());

            message = "model_execution_spec_ready_fallback variables=" + std::to_string(executionSpec.cellScalarVariableIds.size()) +
                " stages=" + std::to_string(executionSpec.stageOrder.size()) +
                " conserved=" + std::to_string(executionSpec.conservedVariables.size()) +
                " aliases=" + std::to_string(executionSpec.semanticFieldAliases.size());
            return true;
        } catch (const std::exception& fallbackException) {
            executionSpec = ModelExecutionSpec{};
            message = std::string("model_execution_spec_failed error=") + exception.what() +
                " fallback_error=" + fallbackException.what();
            return false;
        }
    }
}

bool loadModelDisplaySpec(
    const std::filesystem::path& modelPath,
    ModelDisplaySpec& displaySpec,
    std::string& message) {
    displaySpec = ModelDisplaySpec{};

    try {
        const auto ctx = ws::ModelParser::load(modelPath);
        const json parsed = json::parse(ctx.model_json);

        auto normalizeTags = [](std::vector<std::string>& tags) {
            tags.erase(
                std::remove_if(tags.begin(), tags.end(), [](const std::string& tag) { return tag.empty(); }),
                tags.end());
            std::sort(tags.begin(), tags.end());
            tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
        };

        std::unordered_map<std::string, std::vector<std::string>> fieldTags;

        if (parsed.contains("display_channels") && parsed["display_channels"].is_object()) {
            for (const auto& [channelName, channelValue] : parsed["display_channels"].items()) {
                if (channelName.empty()) {
                    continue;
                }
                if (channelValue.is_array()) {
                    for (const auto& fieldNameValue : channelValue) {
                        if (!fieldNameValue.is_string()) {
                            continue;
                        }
                        const std::string fieldName = fieldNameValue.get<std::string>();
                        if (fieldName.empty()) {
                            continue;
                        }
                        fieldTags[fieldName].push_back(channelName);
                    }
                }
            }
        }

        if (parsed.contains("variables") && parsed["variables"].is_array()) {
            for (const auto& variable : parsed["variables"]) {
                if (!variable.is_object()) {
                    continue;
                }
                if (!variable.contains("id") || !variable["id"].is_string()) {
                    continue;
                }

                const std::string fieldId = variable["id"].get<std::string>();
                if (fieldId.empty()) {
                    continue;
                }

                if (variable.contains("display_tags") && variable["display_tags"].is_array()) {
                    for (const auto& tagValue : variable["display_tags"]) {
                        if (tagValue.is_string()) {
                            fieldTags[fieldId].push_back(tagValue.get<std::string>());
                        }
                    }
                }

                if (variable.contains("display_channel") && variable["display_channel"].is_string()) {
                    fieldTags[fieldId].push_back(variable["display_channel"].get<std::string>());
                }
            }
        }

        for (auto& [fieldId, tags] : fieldTags) {
            normalizeTags(tags);
        }

        displaySpec.fieldTags = std::move(fieldTags);
        message = "model_display_spec_ready fields=" + std::to_string(displaySpec.fieldTags.size());
        return true;
    } catch (const std::exception& exception) {
        displaySpec = ModelDisplaySpec{};
        message = std::string("model_display_spec_failed error=") + exception.what();
        return false;
    }
}

InitializationBindingPlan buildBindingPlan(
    const ModelVariableCatalog& catalog,
    const InitializationRequest& request) {
    InitializationBindingPlan plan;
    plan.type = request.type;

    switch (request.type) {
        case InitialConditionType::Conway:
            addDecisionFromOverrideOrResolver(
                plan,
                catalog,
                "conway.target_variable",
                request.conwayTargetOverride,
                {"alive", "binary", "state", "vegetation", "biomass"});
            break;

        case InitialConditionType::GrayScott: {
            addDecisionFromOverrideOrResolver(
                plan,
                catalog,
                "gray_scott.target_variable_a",
                request.grayTargetAOverride,
                {"gray_scott_u", "reactant_u", "resource", "concentration"});

            std::optional<std::string> exclude;
            if (!plan.decisions.empty() && plan.decisions.front().resolved) {
                exclude = plan.decisions.front().variableId;
            }

            addDecisionFromOverrideOrResolver(
                plan,
                catalog,
                "gray_scott.target_variable_b",
                request.grayTargetBOverride,
                {"gray_scott_v", "reactant_v", "biomass", "vegetation", "concentration"},
                exclude);

            if (plan.decisions.size() >= 2 &&
                plan.decisions[0].resolved &&
                plan.decisions[1].resolved &&
                plan.decisions[0].variableId == plan.decisions[1].variableId) {
                plan.issues.push_back(BindingIssue{
                    "binding.gray_scott_same_variable",
                    "Gray-Scott requires distinct target A and B variables.",
                    true});
            }
            break;
        }

        case InitialConditionType::Waves:
            addDecisionFromOverrideOrResolver(
                plan,
                catalog,
                "waves.target_variable",
                request.wavesTargetOverride,
                {"water", "height", "surface", "wave", "elevation"});
            break;

        case InitialConditionType::Terrain:
        case InitialConditionType::Blank:
        default:
            break;
    }

    plan.fingerprint = computePlanFingerprint(plan);
    return plan;
}

} // namespace ws::initialization
