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

struct ParsedDomainDefinition {
    std::string id;
    std::string kind = "interval";
    std::string declaredType;
    std::optional<float> minValue;
    std::optional<float> maxValue;
    std::vector<int> allowedValues;
};

bool isCommentKey(const std::string& key) {
    return key.rfind("__comment", 0) == 0;
}

bool isCategoricalCompatibleType(const std::string& rawType) {
    const std::string type = toLowerCopy(rawType);
    return type == "u32" || type == "i32" || type == "bool";
}

bool isCellOrBoundarySupport(const std::string& rawSupport) {
    const std::string support = toLowerCopy(rawSupport);
    return support == "cell" || support == "boundary";
}

bool parseDomainDefinitions(
    const json& parsed,
    std::unordered_map<std::string, ParsedDomainDefinition>& outDomains,
    std::string& message) {
    outDomains.clear();
    if (!parsed.contains("domains") || !parsed["domains"].is_object()) {
        return true;
    }

    for (auto it = parsed["domains"].begin(); it != parsed["domains"].end(); ++it) {
        const std::string domainId = it.key();
        if (domainId.empty() || isCommentKey(domainId)) {
            continue;
        }
        if (!it.value().is_object()) {
            message = "model_catalog_load_failed reason=domain_not_object id=" + domainId;
            return false;
        }

        ParsedDomainDefinition definition;
        definition.id = domainId;
        if (it.value().contains("kind") && it.value()["kind"].is_string()) {
            definition.kind = toLowerCopy(it.value()["kind"].get<std::string>());
        }
        if (it.value().contains("type") && it.value()["type"].is_string()) {
            definition.declaredType = toLowerCopy(it.value()["type"].get<std::string>());
        }

        if (definition.kind == "categorical") {
            if (!it.value().contains("allowed_values") || !it.value()["allowed_values"].is_array()) {
                message = "model_catalog_load_failed reason=domain_categorical_missing_values id=" + domainId;
                return false;
            }

            for (const auto& allowedValue : it.value()["allowed_values"]) {
                if (allowedValue.is_boolean()) {
                    definition.allowedValues.push_back(allowedValue.get<bool>() ? 1 : 0);
                    continue;
                }
                if (allowedValue.is_number_integer() || allowedValue.is_number_unsigned()) {
                    definition.allowedValues.push_back(allowedValue.get<int>());
                    continue;
                }

                message = "model_catalog_load_failed reason=domain_categorical_non_integer id=" + domainId;
                return false;
            }

            std::sort(definition.allowedValues.begin(), definition.allowedValues.end());
            definition.allowedValues.erase(
                std::unique(definition.allowedValues.begin(), definition.allowedValues.end()),
                definition.allowedValues.end());
            if (definition.allowedValues.empty()) {
                message = "model_catalog_load_failed reason=domain_categorical_empty id=" + domainId;
                return false;
            }
        } else if (definition.kind == "interval") {
            definition.minValue = readDomainBound(it.value(), "min");
            definition.maxValue = readDomainBound(it.value(), "max");
            if (definition.minValue.has_value() && definition.maxValue.has_value() &&
                (*definition.minValue > *definition.maxValue)) {
                std::swap(*definition.minValue, *definition.maxValue);
            }
        } else {
            message = "model_catalog_load_failed reason=domain_kind_unsupported id=" + domainId + " kind=" + definition.kind;
            return false;
        }

        outDomains.insert_or_assign(domainId, std::move(definition));
    }

    return true;
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

void appendNormalizedStrings(const json& source, std::vector<std::string>& destination) {
    if (!source.is_array()) {
        return;
    }
    for (const auto& value : source) {
        if (value.is_string()) {
            destination.push_back(value.get<std::string>());
        }
    }
}

std::optional<InitialConditionType> parseInitialConditionType(const std::string& rawMode) {
    const std::string mode = toLowerCopy(rawMode);
    if (mode == "blank" || mode == "zero") {
        return InitialConditionType::Blank;
    }
    if (mode == "terrain") {
        return InitialConditionType::Terrain;
    }
    if (mode == "conway" || mode == "game_of_life") {
        return InitialConditionType::Conway;
    }
    if (mode == "gray_scott" || mode == "grayscott" || mode == "reaction_diffusion") {
        return InitialConditionType::GrayScott;
    }
    if (mode == "waves" || mode == "wave") {
        return InitialConditionType::Waves;
    }
    if (mode == "voronoi") {
        return InitialConditionType::Voronoi;
    }
    if (mode == "clustering" || mode == "cluster") {
        return InitialConditionType::Clustering;
    }
    if (mode == "sparse_random" || mode == "sparserandom") {
        return InitialConditionType::SparseRandom;
    }
    if (mode == "gradient_field" || mode == "gradient") {
        return InitialConditionType::GradientField;
    }
    if (mode == "checkerboard") {
        return InitialConditionType::Checkerboard;
    }
    if (mode == "radial_pattern" || mode == "radial") {
        return InitialConditionType::RadialPattern;
    }
    if (mode == "multi_scale" || mode == "multiscale") {
        return InitialConditionType::MultiScale;
    }
    if (mode == "diffusion_limit" || mode == "diffusion") {
        return InitialConditionType::DiffusionLimit;
    }
    return std::nullopt;
}

std::optional<CrossVariableRelation> parseCrossVariableRelationToken(const std::string& rawToken) {
    const std::string token = toLowerCopy(rawToken);
    if (token == "<=" || token == "le" || token == "less_equal" || token == "lte") {
        return CrossVariableRelation::LessEqual;
    }
    if (token == ">=" || token == "ge" || token == "greater_equal" || token == "gte") {
        return CrossVariableRelation::GreaterEqual;
    }
    if (token == "==" || token == "eq" || token == "equal") {
        return CrossVariableRelation::Equal;
    }
    return std::nullopt;
}

bool appendCrossVariableConstraintsFromModel(
    const json& parsed,
    ModelExecutionSpec& executionSpec,
    std::string& message) {
    if (!parsed.contains("cross_variable_constraints")) {
        return true;
    }
    if (!parsed["cross_variable_constraints"].is_array()) {
        message = "model_execution_spec_failed reason=cross_variable_constraints_not_array";
        return false;
    }

    auto readConstraintString = [](const json& object, std::initializer_list<const char*> keys) -> std::optional<std::string> {
        for (const char* key : keys) {
            if (object.contains(key) && object[key].is_string()) {
                const std::string value = object[key].get<std::string>();
                if (!value.empty()) {
                    return value;
                }
            }
        }
        return std::nullopt;
    };

    for (const auto& rawConstraint : parsed["cross_variable_constraints"]) {
        if (!rawConstraint.is_object()) {
            message = "model_execution_spec_failed reason=cross_variable_constraint_not_object";
            return false;
        }

        const auto lhs = readConstraintString(rawConstraint, {"lhs", "lhs_variable", "left"});
        const auto rhs = readConstraintString(rawConstraint, {"rhs", "rhs_variable", "right"});
        const auto relationToken = readConstraintString(rawConstraint, {"op", "operator", "relation"});

        if (!lhs.has_value() || !rhs.has_value() || !relationToken.has_value()) {
            message = "model_execution_spec_failed reason=cross_variable_constraint_missing_fields";
            return false;
        }

        const auto relation = parseCrossVariableRelationToken(*relationToken);
        if (!relation.has_value()) {
            message = "model_execution_spec_failed reason=cross_variable_constraint_bad_relation value=" + *relationToken;
            return false;
        }

        CrossVariableConstraint constraint;
        constraint.id = readConstraintString(rawConstraint, {"id"}).value_or(*lhs + "_" + *rhs);
        constraint.lhsVariable = *lhs;
        constraint.rhsVariable = *rhs;
        constraint.relation = *relation;

        if (rawConstraint.contains("offset") &&
            (rawConstraint["offset"].is_number_float() || rawConstraint["offset"].is_number_integer() || rawConstraint["offset"].is_number_unsigned())) {
            constraint.offset = rawConstraint["offset"].get<float>();
        }
        if (rawConstraint.contains("tolerance") &&
            (rawConstraint["tolerance"].is_number_float() || rawConstraint["tolerance"].is_number_integer() || rawConstraint["tolerance"].is_number_unsigned())) {
            constraint.tolerance = std::max(0.0f, rawConstraint["tolerance"].get<float>());
        }

        if (const auto action = readConstraintString(rawConstraint, {"action", "enforcement"}); action.has_value()) {
            const std::string normalizedAction = toLowerCopy(*action);
            if (normalizedAction == "report_only") {
                constraint.autoClamp = false;
            } else if (normalizedAction == "clamp_lhs" || normalizedAction == "clamp" || normalizedAction == "enforce") {
                constraint.autoClamp = true;
            } else {
                message = "model_execution_spec_failed reason=cross_variable_constraint_bad_action value=" + *action;
                return false;
            }
        }

        executionSpec.crossVariableConstraints.push_back(std::move(constraint));
    }

    return true;
}

int parseRestrictionMode(const json& value, const int fallback = 0) {
    if (value.is_number_integer()) {
        return std::clamp(value.get<int>(), 0, 5);
    }
    if (!value.is_string()) {
        return fallback;
    }

    const std::string token = toLowerCopy(value.get<std::string>());
    if (token == "none") {
        return 0;
    }
    if (token == "clamp" || token == "clamp01" || token == "clamp_min_max") {
        return 1;
    }
    if (token == "non_negative" || token == "nonnegative") {
        return 2;
    }
    if (token == "clamp_signed" || token == "clamp[-1,1]" || token == "signed") {
        return 3;
    }
    if (token == "tanh") {
        return 4;
    }
    if (token == "sigmoid") {
        return 5;
    }
    return fallback;
}

void normalizeInitialConditionTypeVector(std::vector<InitialConditionType>& values) {
    std::sort(values.begin(), values.end(), [](const InitialConditionType lhs, const InitialConditionType rhs) {
        return static_cast<int>(lhs) < static_cast<int>(rhs);
    });
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

void populateCatalogFromMetadataJson(const std::string& metadataJson, ModelVariableCatalog& outCatalog) {
    if (metadataJson.empty()) {
        return;
    }

    const json parsed = json::parse(metadataJson);
    if (!parsed.is_object()) {
        return;
    }

    if ((outCatalog.modelId.empty()) && parsed.contains("id") && parsed["id"].is_string()) {
        outCatalog.modelId = parsed["id"].get<std::string>();
    }

    if (!parsed.contains("initialization_guidance") || !parsed["initialization_guidance"].is_object()) {
        return;
    }

    const auto& guidance = parsed["initialization_guidance"];

    if (guidance.contains("preferred_mode") && guidance["preferred_mode"].is_string()) {
        outCatalog.preferredInitializationMode = parseInitialConditionType(
            guidance["preferred_mode"].get<std::string>());
    }

    if (guidance.contains("expected_start_data_type") && guidance["expected_start_data_type"].is_string()) {
        outCatalog.expectedStartDataType = toLowerCopy(guidance["expected_start_data_type"].get<std::string>());
    }

    if (guidance.contains("supported_modes") && guidance["supported_modes"].is_array()) {
        outCatalog.supportedInitializationModes.clear();
        for (const auto& value : guidance["supported_modes"]) {
            if (!value.is_string()) {
                continue;
            }
            if (const auto parsedMode = parseInitialConditionType(value.get<std::string>()); parsedMode.has_value()) {
                outCatalog.supportedInitializationModes.push_back(*parsedMode);
            }
        }
        normalizeInitialConditionTypeVector(outCatalog.supportedInitializationModes);
    }

    if (guidance.contains("preferred_display_variable") && guidance["preferred_display_variable"].is_string()) {
        outCatalog.preferredDisplayVariable = guidance["preferred_display_variable"].get<std::string>();
    }

    if (guidance.contains("variable_defaults") && guidance["variable_defaults"].is_object()) {
        outCatalog.variableInitializationDefaults.clear();
        for (auto it = guidance["variable_defaults"].begin(); it != guidance["variable_defaults"].end(); ++it) {
            if (!it.value().is_object()) {
                continue;
            }

            VariableInitializationDefault defaults;
            if (it.value().contains("enabled") && it.value()["enabled"].is_boolean()) {
                defaults.enabled = it.value()["enabled"].get<bool>();
            }

            if (it.value().contains("base_value") &&
                (it.value()["base_value"].is_number_float() ||
                 it.value()["base_value"].is_number_integer() ||
                 it.value()["base_value"].is_number_unsigned())) {
                defaults.hasBaseValue = true;
                defaults.baseValue = it.value()["base_value"].get<float>();
            }

            if (it.value().contains("restriction_mode")) {
                defaults.hasRestrictionMode = true;
                defaults.restrictionMode = parseRestrictionMode(it.value()["restriction_mode"], defaults.restrictionMode);
            }

            if (it.value().contains("min") &&
                (it.value()["min"].is_number_float() ||
                 it.value()["min"].is_number_integer() ||
                 it.value()["min"].is_number_unsigned())) {
                defaults.hasClampMin = true;
                defaults.clampMin = it.value()["min"].get<float>();
            }
            if (it.value().contains("max") &&
                (it.value()["max"].is_number_float() ||
                 it.value()["max"].is_number_integer() ||
                 it.value()["max"].is_number_unsigned())) {
                defaults.hasClampMax = true;
                defaults.clampMax = it.value()["max"].get<float>();
            }

            outCatalog.variableInitializationDefaults.insert_or_assign(it.key(), defaults);
        }
    }

    if (guidance.contains("parameter_overrides") && guidance["parameter_overrides"].is_object()) {
        outCatalog.generationParameterOverrides.clear();
        for (auto it = guidance["parameter_overrides"].begin(); it != guidance["parameter_overrides"].end(); ++it) {
            if (!it.value().is_number_float() && !it.value().is_number_integer() && !it.value().is_number_unsigned()) {
                continue;
            }
            outCatalog.generationParameterOverrides.insert_or_assign(it.key(), it.value().get<float>());
        }
    }
}

void applyMetadataParameterOverrides(
    const json& metadataParsed,
    std::vector<ParameterControl>& controls) {
    if (!metadataParsed.is_object() ||
        !metadataParsed.contains("initialization_guidance") ||
        !metadataParsed["initialization_guidance"].is_object()) {
        return;
    }

    const auto& guidance = metadataParsed["initialization_guidance"];
    if (!guidance.contains("parameter_overrides") || !guidance["parameter_overrides"].is_object()) {
        return;
    }

    std::unordered_map<std::string, float> overrides;
    for (auto it = guidance["parameter_overrides"].begin(); it != guidance["parameter_overrides"].end(); ++it) {
        if (!it.value().is_number_float() && !it.value().is_number_integer() && !it.value().is_number_unsigned()) {
            continue;
        }
        overrides.insert_or_assign(it.key(), it.value().get<float>());
    }

    if (overrides.empty()) {
        return;
    }

    for (auto& control : controls) {
        const auto byVariable = overrides.find(control.targetVariable);
        if (byVariable != overrides.end()) {
            const float v = std::clamp(byVariable->second, control.minValue, control.maxValue);
            control.defaultValue = v;
            control.value = v;
            continue;
        }

        const auto byName = overrides.find(control.name);
        if (byName != overrides.end()) {
            const float v = std::clamp(byName->second, control.minValue, control.maxValue);
            control.defaultValue = v;
            control.value = v;
        }
    }
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

        std::unordered_map<std::string, ParsedDomainDefinition> domainDefinitions;
        if (!parseDomainDefinitions(parsed, domainDefinitions, message)) {
            return false;
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

            if (variable.contains("display_type") && variable["display_type"].is_string()) {
                descriptor.displayType = toLowerCopy(variable["display_type"].get<std::string>());
            }
            if (variable.contains("vector_component") && variable["vector_component"].is_string()) {
                descriptor.vectorComponent = toLowerCopy(variable["vector_component"].get<std::string>());
            }

            if (variable.contains("visualization_roles")) {
                appendNormalizedStrings(variable["visualization_roles"], descriptor.visualizationRoles);
            }
            if (variable.contains("initialization_hints")) {
                appendNormalizedStrings(variable["initialization_hints"], descriptor.initializationHints);
            }
            if (variable.contains("initialization_modes")) {
                appendNormalizedStrings(variable["initialization_modes"], descriptor.initializationHints);
            }

            normalizeStringVector(descriptor.visualizationRoles);
            normalizeStringVector(descriptor.initializationHints);

            if (descriptor.displayType.has_value() && !descriptor.displayType->empty()) {
                descriptor.tags.push_back(*descriptor.displayType);
            }
            if (descriptor.vectorComponent.has_value() && !descriptor.vectorComponent->empty()) {
                descriptor.tags.push_back(*descriptor.vectorComponent);
            }
            descriptor.tags.insert(
                descriptor.tags.end(),
                descriptor.visualizationRoles.begin(),
                descriptor.visualizationRoles.end());
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

            if (variable.contains("domain") && variable["domain"].is_string()) {
                const std::string domainId = variable["domain"].get<std::string>();
                if (!domainId.empty() && !isCommentKey(domainId)) {
                    const auto domainIt = domainDefinitions.find(domainId);
                    if (domainIt == domainDefinitions.end()) {
                        message = "model_catalog_load_failed reason=domain_missing id=" + descriptor.id + " domain=" + domainId;
                        return false;
                    }

                    const ParsedDomainDefinition& domain = domainIt->second;
                    if (domain.kind == "interval") {
                        if (!descriptor.hasDomainMin && domain.minValue.has_value()) {
                            descriptor.hasDomainMin = true;
                            descriptor.domainMin = *domain.minValue;
                        }
                        if (!descriptor.hasDomainMax && domain.maxValue.has_value()) {
                            descriptor.hasDomainMax = true;
                            descriptor.domainMax = *domain.maxValue;
                        }
                    } else if (domain.kind == "categorical") {
                        if (!isCategoricalCompatibleType(descriptor.type)) {
                            message = "model_catalog_load_failed reason=domain_categorical_type_mismatch id=" +
                                descriptor.id + " type=" + descriptor.type;
                            return false;
                        }
                        descriptor.hasCategoricalDomain = true;
                        descriptor.categoricalAllowedValues = domain.allowedValues;

                        if (variable.contains("initial_value")) {
                            std::optional<int> initialValue;
                            const auto& rawInitial = variable["initial_value"];
                            if (rawInitial.is_boolean()) {
                                initialValue = rawInitial.get<bool>() ? 1 : 0;
                            } else if (rawInitial.is_number_integer() || rawInitial.is_number_unsigned()) {
                                initialValue = rawInitial.get<int>();
                            }

                            if (initialValue.has_value() &&
                                !std::binary_search(domain.allowedValues.begin(), domain.allowedValues.end(), *initialValue)) {
                                message = "model_catalog_load_failed reason=domain_categorical_initial_out_of_range id=" +
                                    descriptor.id;
                                return false;
                            }
                        }
                    }
                }
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

bool variableHasAnyInitializationHint(
    const VariableDescriptor& variable,
    const std::vector<std::string>& requiredHints) {
    if (requiredHints.empty()) {
        return true;
    }
    if (variable.initializationHints.empty()) {
        return false;
    }

    for (const auto& required : requiredHints) {
        const std::string requiredLower = toLowerCopy(required);
        if (requiredLower.empty()) {
            continue;
        }
        for (const auto& variableHint : variable.initializationHints) {
            if (toLowerCopy(variableHint) == requiredLower) {
                return true;
            }
        }
    }
    return false;
}

std::optional<std::string> pickBestCandidateByInitializationHints(
    const ModelVariableCatalog& catalog,
    const std::vector<std::string>& initializationHints,
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
        if (!variableHasAnyInitializationHint(variable, initializationHints)) {
            continue;
        }
        scored.emplace_back(scoreCandidate(variable, semanticHints), variable.id);
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
    const bool requireMetadataHints,
    const std::vector<std::string>& initializationHints,
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
    std::optional<std::string> picked;
    if (requireMetadataHints) {
        picked = pickBestCandidateByInitializationHints(
            catalog,
            initializationHints,
            semanticHints,
            minimumScore,
            exclude);
    } else {
        picked = pickBestCandidate(
            catalog,
            semanticHints,
            minimumScore,
            exclude);
    }

    if (picked.has_value()) {
        decision.variableId = *picked;
        decision.resolved = true;
        decision.confidence = 0.85f;
        decision.rationale = requireMetadataHints
            ? "auto_resolved_from_initialization_hints"
            : "auto_resolved_from_catalog_metadata";
    } else {
        decision.resolved = false;
        decision.confidence = 0.0f;
        decision.rationale = requireMetadataHints
            ? "initialization_hint_resolution_failed"
            : "no_candidate_available";
        plan.issues.push_back(BindingIssue{
            "binding.missing_candidate",
            requireMetadataHints
                ? "No metadata-hinted candidate available for binding '" + key + "'."
                : "No candidate variable available for binding '" + key + "'.",
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
            if (!ctx.metadata_json.empty()) {
                populateCatalogFromMetadataJson(ctx.metadata_json, outCatalog);
            }
            return true;
        }

        // Preserve the package loader fallback for incomplete model packages.
        if (std::filesystem::is_directory(modelPath)) {
            const auto fallbackJsonPath = modelPath / "model.json";
            if (std::filesystem::exists(fallbackJsonPath)) {
                std::ifstream in(fallbackJsonPath);
                if (!in) {
                    message = "model_catalog_load_failed reason=file_open_failed path=" + fallbackJsonPath.string();
                    return false;
                }
                const std::string modelJson((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (!populateCatalogFromModelJson(fallbackJsonPath, modelJson, outCatalog, message)) {
                    return false;
                }

                const auto metadataPath = modelPath / "metadata.json";
                if (std::filesystem::exists(metadataPath)) {
                    std::ifstream metadataIn(metadataPath);
                    if (metadataIn) {
                        const std::string metadataJson((std::istreambuf_iterator<char>(metadataIn)), std::istreambuf_iterator<char>());
                        if (!metadataJson.empty()) {
                            populateCatalogFromMetadataJson(metadataJson, outCatalog);
                        }
                    }
                }
                return true;
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
                    if (!populateCatalogFromModelJson(modelJsonPath, modelJson, outCatalog, message)) {
                        return false;
                    }

                    const auto metadataPath = modelPath / "metadata.json";
                    if (std::filesystem::exists(metadataPath)) {
                        std::ifstream metadataIn(metadataPath);
                        if (metadataIn) {
                            const std::string metadataJson((std::istreambuf_iterator<char>(metadataIn)), std::istreambuf_iterator<char>());
                            if (!metadataJson.empty()) {
                                populateCatalogFromMetadataJson(metadataJson, outCatalog);
                            }
                        }
                    }
                    return true;
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
        json metadataParsed;
        if (!ctx.metadata_json.empty()) {
            metadataParsed = json::parse(ctx.metadata_json);
        }

        std::unordered_map<std::string, ParsedDomainDefinition> domainDefinitions;
        if (!parseDomainDefinitions(parsed, domainDefinitions, message)) {
            return false;
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

            if (role != "parameter" || !isCellOrBoundarySupport(support)) {
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
                const auto domainIt = domainDefinitions.find(variable["domain"].get<std::string>());
                if (domainIt != domainDefinitions.end()) {
                    const auto& domain = domainIt->second;
                    if (domain.kind == "interval") {
                        if (!minBound.has_value()) {
                            minBound = domain.minValue;
                        }
                        if (!maxBound.has_value()) {
                            maxBound = domain.maxValue;
                        }
                    } else if (domain.kind == "categorical") {
                        const std::string type = variable.contains("type") && variable["type"].is_string()
                            ? variable["type"].get<std::string>()
                            : std::string{};
                        if (!isCategoricalCompatibleType(type)) {
                            message = "model_parameter_controls_failed reason=domain_categorical_type_mismatch id=" + id;
                            return false;
                        }
                        if (!domain.allowedValues.empty()) {
                            if (!minBound.has_value()) {
                                minBound = static_cast<float>(domain.allowedValues.front());
                            }
                            if (!maxBound.has_value()) {
                                maxBound = static_cast<float>(domain.allowedValues.back());
                            }
                        }
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

        applyMetadataParameterOverrides(metadataParsed, controls);

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
                if (!isCellOrBoundarySupport(support)) {
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

        if (parsed.contains("grid") && parsed["grid"].is_object()) {
            const auto& grid = parsed["grid"];
            if (grid.contains("boundary_conditions")) {
                const auto& boundary = grid["boundary_conditions"];
                auto parseBoundaryToken = [](const std::string& raw) {
                    const std::string token = toLowerCopy(raw);
                    if (token == "periodic" || token == "wrap" || token == "wrapped") {
                        return std::optional<BoundaryMode>(BoundaryMode::Wrap);
                    }
                    if (token == "reflect" || token == "reflecting" || token == "mirror" || token == "mirrored") {
                        return std::optional<BoundaryMode>(BoundaryMode::Reflect);
                    }
                    if (token == "clamp" || token == "fixed" || token == "dirichlet" || token == "neumann" || token == "absorbing") {
                        return std::optional<BoundaryMode>(BoundaryMode::Clamp);
                    }
                    return std::optional<BoundaryMode>{};
                };

                if (boundary.is_string()) {
                    executionSpec.preferredBoundaryMode = parseBoundaryToken(boundary.get<std::string>());
                } else if (boundary.is_object()) {
                    bool sawWrap = false;
                    bool sawClamp = false;
                    bool sawReflect = false;
                    for (auto it = boundary.begin(); it != boundary.end(); ++it) {
                        if (!it.value().is_string()) {
                            continue;
                        }
                        const auto parsedMode = parseBoundaryToken(it.value().get<std::string>());
                        if (!parsedMode.has_value()) {
                            continue;
                        }
                        if (*parsedMode == BoundaryMode::Wrap) {
                            sawWrap = true;
                        } else if (*parsedMode == BoundaryMode::Reflect) {
                            sawReflect = true;
                        } else {
                            sawClamp = true;
                        }
                    }
                    const int modeCount = static_cast<int>(sawWrap) + static_cast<int>(sawClamp) + static_cast<int>(sawReflect);
                    if (modeCount == 1 && sawWrap) {
                        executionSpec.preferredBoundaryMode = BoundaryMode::Wrap;
                    } else if (modeCount == 1 && sawClamp) {
                        executionSpec.preferredBoundaryMode = BoundaryMode::Clamp;
                    } else if (modeCount == 1 && sawReflect) {
                        executionSpec.preferredBoundaryMode = BoundaryMode::Reflect;
                    }
                }
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

        if (!appendCrossVariableConstraintsFromModel(parsed, executionSpec, message)) {
            return false;
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
                    if (!isCellOrBoundarySupport(support)) {
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

            if (parsed.contains("grid") && parsed["grid"].is_object()) {
                const auto& grid = parsed["grid"];
                if (grid.contains("boundary_conditions")) {
                    const auto& boundary = grid["boundary_conditions"];
                    auto parseBoundaryToken = [](const std::string& raw) {
                        const std::string token = toLowerCopy(raw);
                        if (token == "periodic" || token == "wrap" || token == "wrapped") {
                            return std::optional<BoundaryMode>(BoundaryMode::Wrap);
                        }
                        if (token == "reflect" || token == "reflecting" || token == "mirror" || token == "mirrored") {
                            return std::optional<BoundaryMode>(BoundaryMode::Reflect);
                        }
                        if (token == "clamp" || token == "fixed" || token == "dirichlet" || token == "neumann" || token == "absorbing") {
                            return std::optional<BoundaryMode>(BoundaryMode::Clamp);
                        }
                        return std::optional<BoundaryMode>{};
                    };

                    if (boundary.is_string()) {
                        executionSpec.preferredBoundaryMode = parseBoundaryToken(boundary.get<std::string>());
                    } else if (boundary.is_object()) {
                        bool sawWrap = false;
                        bool sawClamp = false;
                        bool sawReflect = false;
                        for (auto it = boundary.begin(); it != boundary.end(); ++it) {
                            if (!it.value().is_string()) {
                                continue;
                            }
                            const auto parsedMode = parseBoundaryToken(it.value().get<std::string>());
                            if (!parsedMode.has_value()) {
                                continue;
                            }
                            if (*parsedMode == BoundaryMode::Wrap) {
                                sawWrap = true;
                            } else if (*parsedMode == BoundaryMode::Reflect) {
                                sawReflect = true;
                            } else {
                                sawClamp = true;
                            }
                        }
                        const int modeCount = static_cast<int>(sawWrap) + static_cast<int>(sawClamp) + static_cast<int>(sawReflect);
                        if (modeCount == 1 && sawWrap) {
                            executionSpec.preferredBoundaryMode = BoundaryMode::Wrap;
                        } else if (modeCount == 1 && sawClamp) {
                            executionSpec.preferredBoundaryMode = BoundaryMode::Clamp;
                        } else if (modeCount == 1 && sawReflect) {
                            executionSpec.preferredBoundaryMode = BoundaryMode::Reflect;
                        }
                    }
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

            if (!appendCrossVariableConstraintsFromModel(parsed, executionSpec, message)) {
                return false;
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

                if (variable.contains("display_type") && variable["display_type"].is_string()) {
                    fieldTags[fieldId].push_back(variable["display_type"].get<std::string>());
                }

                if (variable.contains("vector_component") && variable["vector_component"].is_string()) {
                    fieldTags[fieldId].push_back(variable["vector_component"].get<std::string>());
                }

                if (variable.contains("visualization_roles") && variable["visualization_roles"].is_array()) {
                    for (const auto& roleValue : variable["visualization_roles"]) {
                        if (roleValue.is_string()) {
                            fieldTags[fieldId].push_back(roleValue.get<std::string>());
                        }
                    }
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
                request.requireMetadataHints,
                {"conway", "conway_target", "binary_seed", "alive_seed"},
                {"alive", "binary", "state", "vegetation", "biomass"});
            break;

        case InitialConditionType::GrayScott: {
            addDecisionFromOverrideOrResolver(
                plan,
                catalog,
                "gray_scott.target_variable_a",
                request.grayTargetAOverride,
                request.requireMetadataHints,
                {"gray_scott", "gray_scott_a", "reaction_a", "u_component"},
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
                request.requireMetadataHints,
                {"gray_scott", "gray_scott_b", "reaction_b", "v_component"},
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
                request.requireMetadataHints,
                {"waves", "waves_target", "surface_waves", "height_field"},
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
