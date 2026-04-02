#include "ws/core/initialization_binding.hpp"

#include "ws/core/determinism.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
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

std::optional<std::size_t> findVariableIndexById(const ModelVariableCatalog& catalog, const std::string& id) {
    for (std::size_t i = 0; i < catalog.variables.size(); ++i) {
        if (catalog.variables[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

float scoreCandidate(const VariableDescriptor& variable, const std::vector<std::string>& keywords) {
    if (variable.support != "cell") {
        return -std::numeric_limits<float>::infinity();
    }

    float score = 0.0f;
    if (variable.role == "state") {
        score += 2.5f;
    } else if (variable.role == "derived") {
        score += 0.5f;
    }

    const std::string idLower = toLowerCopy(variable.id);
    for (const auto& keyword : keywords) {
        if (idLower.find(keyword) != std::string::npos) {
            score += 1.0f;
        }
    }

    if (variable.type == "u32" || variable.type == "i32") {
        score += 0.2f;
    }

    if (variable.units == "1") {
        score += 0.1f;
    }

    return score;
}

std::optional<std::string> pickBestCandidate(
    const ModelVariableCatalog& catalog,
    const std::vector<std::string>& keywords,
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
        scored.emplace_back(scoreCandidate(variable, keywords), variable.id);
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

    return scored.front().second;
}

void addDecisionFromOverrideOrResolver(
    InitializationBindingPlan& plan,
    const ModelVariableCatalog& catalog,
    const std::string& key,
    const std::optional<std::string>& overrideValue,
    const std::vector<std::string>& keywords,
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

    const auto picked = pickBestCandidate(catalog, keywords, exclude);
    if (picked.has_value()) {
        decision.variableId = *picked;
        decision.resolved = true;
        decision.confidence = 0.75f;
        decision.rationale = "auto_resolved_from_model_catalog";
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

    std::filesystem::path modelJsonPath = modelPath;
    if (std::filesystem::is_directory(modelJsonPath)) {
        modelJsonPath /= "model.json";
    }

    if (!std::filesystem::exists(modelJsonPath)) {
        message = "model_catalog_load_failed reason=model_json_missing path=" + modelJsonPath.string();
        return false;
    }

    try {
        std::ifstream in(modelJsonPath);
        if (!in) {
            message = "model_catalog_load_failed reason=file_open_failed path=" + modelJsonPath.string();
            return false;
        }

        json parsed = json::parse(in);
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
        message = std::string("model_catalog_load_failed error=") + exception.what();
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
                {"living", "alive", "state", "cell", "binary", "veg", "bio"});
            break;

        case InitialConditionType::GrayScott: {
            addDecisionFromOverrideOrResolver(
                plan,
                catalog,
                "gray_scott.target_variable_a",
                request.grayTargetAOverride,
                {"u_", "ucon", "resource", "stock", "conc", "density", "nitrate"});

            std::optional<std::string> exclude;
            if (!plan.decisions.empty() && plan.decisions.front().resolved) {
                exclude = plan.decisions.front().variableId;
            }

            addDecisionFromOverrideOrResolver(
                plan,
                catalog,
                "gray_scott.target_variable_b",
                request.grayTargetBOverride,
                {"v_", "vcon", "vegetation", "phyto", "bio", "oxygen", "detritus"},
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
                {"water", "height", "surface", "moisture", "salinity", "level"});
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
