#include "ws/core/profile.hpp"
#include "ws/core/initialization_binding.hpp"
#include "ws/core/runtime.hpp"
#include "ws/core/scheduler.hpp"
#include "ws/core/state_store.hpp"
#include "ws/core/subsystems/subsystems.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

namespace {

std::filesystem::path resolveModelsRoot() {
    const std::filesystem::path direct = "models";
    if (std::filesystem::exists(direct) && std::filesystem::is_directory(direct)) {
        return direct;
    }

    const std::filesystem::path parent = std::filesystem::path("..") / "models";
    if (std::filesystem::exists(parent) && std::filesystem::is_directory(parent)) {
        return parent;
    }

    return {};
}

std::optional<std::string> tryAliasTarget(const ws::ModelExecutionSpec& executionSpec, const std::string& semanticKey) {
    const auto it = executionSpec.semanticFieldAliases.find(semanticKey);
    if (it == executionSpec.semanticFieldAliases.end() || it->second.empty()) {
        return std::nullopt;
    }
    if (std::find(executionSpec.cellScalarVariableIds.begin(), executionSpec.cellScalarVariableIds.end(), it->second) ==
        executionSpec.cellScalarVariableIds.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool hasRequiredPhase4Aliases(const ws::ModelExecutionSpec& executionSpec) {
    std::set<std::string> requiredSemanticKeys;
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        if (!subsystem) {
            continue;
        }
        if (subsystem->name() == "automaton" || subsystem->name() == "fire_spread") {
            continue;
        }
        for (const auto& key : subsystem->declaredReadSet()) {
            if (!key.empty()) {
                requiredSemanticKeys.insert(key);
            }
        }
        for (const auto& key : subsystem->declaredWriteSet()) {
            if (!key.empty()) {
                requiredSemanticKeys.insert(key);
            }
        }
    }

    for (const auto& semanticKey : requiredSemanticKeys) {
        if (!tryAliasTarget(executionSpec, semanticKey).has_value()) {
            return false;
        }
    }
    return true;
}

std::optional<ws::ModelExecutionSpec> selectExecutionSpec() {
    const auto modelsRoot = resolveModelsRoot();
    if (modelsRoot.empty()) {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(modelsRoot)) {
        if (!entry.is_directory() || entry.path().extension() != ".simmodel") {
            continue;
        }
        candidates.push_back(entry.path());
    }
    std::sort(candidates.begin(), candidates.end());

    for (const auto& modelPath : candidates) {
        ws::ModelExecutionSpec executionSpec;
        std::string executionMessage;
        const bool executionOk = ws::initialization::loadModelExecutionSpec(modelPath, executionSpec, executionMessage);
        if (!executionOk || executionSpec.cellScalarVariableIds.empty() || !hasRequiredPhase4Aliases(executionSpec)) {
            continue;
        }
        return executionSpec;
    }
    return std::nullopt;
}

std::vector<std::shared_ptr<ws::ISubsystem>> compatiblePhase4Subsystems(const ws::ModelExecutionSpec& executionSpec) {
    std::vector<std::shared_ptr<ws::ISubsystem>> result;
    result.reserve(ws::makePhase4Subsystems().size());

    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        if (!subsystem) {
            continue;
        }

        if (subsystem->name() == "automaton" && executionSpec.semanticFieldAliases.find("automaton.state") == executionSpec.semanticFieldAliases.end()) {
            continue;
        }
        if (subsystem->name() == "fire_spread" && executionSpec.semanticFieldAliases.find("fire.state") == executionSpec.semanticFieldAliases.end()) {
            continue;
        }

        result.push_back(subsystem);
    }

    return result;
}

ws::ProfileResolverInput profileInputForTier(const ws::ModelTier tier) {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        if (subsystem) {
            input.requestedSubsystemTiers[subsystem->name()] = tier;
        }
    }
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = tier;
    }
    input.compatibilityAssumptions = {
        "module_runtime",
        "profile_ab_only"
    };
    return input;
}

float fieldAverage(const ws::StateStoreSnapshot& snapshot, const std::string& variableName) {
    const auto it = std::find_if(
        snapshot.fields.begin(),
        snapshot.fields.end(),
        [&](const ws::StateStoreSnapshot::FieldPayload& field) {
            return field.spec.name == variableName;
        });

    if (it == snapshot.fields.end()) {
        throw std::runtime_error("Missing field in snapshot: " + variableName);
    }

    float total = 0.0f;
    for (const float value : it->values) {
        total += value;
    }

    if (it->values.empty()) {
        return 0.0f;
    }
    return total / static_cast<float>(it->values.size());
}

struct ScenarioResult {
    std::uint64_t identityHash = 0;
    std::uint64_t stateHash = 0;
    float avgWater = 0.0f;
    float avgTemperature = 0.0f;
    float avgResources = 0.0f;
};

ScenarioResult runScenario(const ws::ModelTier tier, const std::uint64_t seed) {
    const auto executionSpec = selectExecutionSpec();
    assert(executionSpec.has_value());
    const auto subsystems = compatiblePhase4Subsystems(*executionSpec);

    const auto pickAliasOrFallback = [&](const std::string& semanticKey, std::size_t fallbackIndex = 0) {
        if (const auto alias = tryAliasTarget(*executionSpec, semanticKey); alias.has_value()) {
            return *alias;
        }
        const auto index = std::min(fallbackIndex, executionSpec->cellScalarVariableIds.size() - 1);
        return executionSpec->cellScalarVariableIds[index];
    };

    const std::string eventVariable = pickAliasOrFallback("events.signal", 0);
    const std::string waterVariable = pickAliasOrFallback("hydrology.water", 0);
    const std::string temperatureVariable = pickAliasOrFallback("temperature.current", 1);
    const std::string resourcesVariable = pickAliasOrFallback("resources.current", 2);

    ws::RuntimeConfig config;
    config.seed = seed;
    config.grid = ws::GridSpec{8, 8};
    config.temporalPolicy = (tier == ws::ModelTier::Standard) ? ws::TemporalPolicy::PhasedB : ws::TemporalPolicy::UniformA;
    config.profileInput = profileInputForTier(tier);
    config.modelExecutionSpec = *executionSpec;

    ws::Runtime runtime(config);
    for (const auto& subsystem : subsystems) {
        runtime.registerSubsystem(subsystem);
    }

    runtime.start();

    ws::RuntimeEvent rainPulse;
    rainPulse.eventName = "exogenous_rain";
    rainPulse.scalarPatches.push_back(ws::ScalarWritePatch{eventVariable, ws::Cell{3, 4}, 0.9f});
    runtime.enqueueEvent(std::move(rainPulse));

    for (std::uint64_t step = 0; step < 32; ++step) {
        runtime.step();
    }

    const ws::RuntimeCheckpoint checkpoint = runtime.createCheckpoint("validation");

    ScenarioResult result;
    result.identityHash = runtime.snapshot().runSignature.identityHash();
    result.stateHash = runtime.snapshot().stateHash;
    result.avgWater = fieldAverage(checkpoint.stateSnapshot, waterVariable);
    result.avgTemperature = fieldAverage(checkpoint.stateSnapshot, temperatureVariable);
    result.avgResources = fieldAverage(checkpoint.stateSnapshot, resourcesVariable);

    runtime.stop();
    return result;
}

void verifyOwnershipContracts() {
    const auto executionSpec = selectExecutionSpec();
    assert(executionSpec.has_value());
    const auto subsystems = compatiblePhase4Subsystems(*executionSpec);

    ws::Scheduler scheduler;
    for (const auto& subsystem : subsystems) {
        scheduler.registerSubsystem(subsystem);
    }

    const auto ownership = scheduler.writeOwnershipByVariable();
    const std::vector<std::string> expectedOwnedVariables = {
        "generation.elevation",
        "hydrology.water",
        "temperature.current",
        "humidity.current",
        "wind.vector.axis_x",
        "wind.vector.axis_y",
        "climate.current",
        "soil.fertility",
        "vegetation.current",
        "resources.current",
        "events.signal",
        "events.water_delta",
        "events.temperature_delta"
    };

    for (const auto& variable : expectedOwnedVariables) {
        assert(ownership.contains(variable));
    }

    assert(ownership.at("generation.elevation") == "generation");
    assert(ownership.at("hydrology.water") == "hydrology");
    assert(ownership.at("temperature.current") == "temperature");
    assert(ownership.at("events.signal") == "events");
}

void verifyInitializationAndStepping() {
    const ScenarioResult deterministicA1 = runScenario(ws::ModelTier::Minimal, 777);
    const ScenarioResult deterministicA2 = runScenario(ws::ModelTier::Minimal, 777);

    assert(deterministicA1.identityHash == deterministicA2.identityHash);
    assert(deterministicA1.stateHash == deterministicA2.stateHash);
    assert(deterministicA1.avgWater == deterministicA2.avgWater);
    assert(deterministicA1.avgTemperature == deterministicA2.avgTemperature);
    assert(deterministicA1.avgResources == deterministicA2.avgResources);

    const ScenarioResult tierB = runScenario(ws::ModelTier::Standard, 777);
    assert(deterministicA1.identityHash != tierB.identityHash);

    assert(deterministicA1.avgWater >= 0.0f);
    assert(std::isfinite(deterministicA1.avgTemperature));
    assert(std::isfinite(deterministicA1.avgResources));
}

void verifySubsystemReadWriteContractsExposeExplicitSets() {
    const auto executionSpec = selectExecutionSpec();
    assert(executionSpec.has_value());
    const auto subsystems = compatiblePhase4Subsystems(*executionSpec);

    for (const auto& subsystem : subsystems) {
        const auto readSet = subsystem->declaredReadSet();
        const auto writeSet = subsystem->declaredWriteSet();

        assert(!writeSet.empty());
        if (subsystem->name() != "generation") {
            assert(!readSet.empty());
        }
    }
}

} // namespace

int main() {
    verifyOwnershipContracts();
    verifyInitializationAndStepping();
    verifySubsystemReadWriteContractsExposeExplicitSets();
    return 0;
}
