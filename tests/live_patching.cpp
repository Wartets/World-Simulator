#include "ws/app/checkpoint_io.hpp"
#include "ws/core/initialization_binding.hpp"
#include "ws/core/runtime.hpp"
#include "ws/core/subsystems/subsystems.hpp"
#include "ws/core/subsystems/bootstrap_subsystem.hpp"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

ws::ProfileResolverInput baselineProfileInput() {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        input.requestedSubsystemTiers[subsystem->name()] = ws::ModelTier::Minimal;
    }
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = ws::ModelTier::Minimal;
    }
    input.requestedSubsystemTiers["bootstrap"] = ws::ModelTier::Minimal;
    input.compatibilityAssumptions = {
        "phase6_live_patching_test",
        "deterministic_manual_event_pipeline"
    };
    return input;
}

struct ModelFixture {
    std::filesystem::path modelPath;
    ws::ModelExecutionSpec executionSpec;
    std::string patchVariable;
    std::string perturbationVariable;
};

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

std::optional<std::string> tryAliasTarget(
    const ws::ModelExecutionSpec& executionSpec,
    const std::string& semanticKey) {
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

std::optional<std::string> tryAnyAliasTarget(
    const ws::ModelExecutionSpec& executionSpec,
    const std::vector<std::string>& semanticKeys) {
    for (const auto& semanticKey : semanticKeys) {
        const auto alias = tryAliasTarget(executionSpec, semanticKey);
        if (alias.has_value()) {
            return alias;
        }
    }
    return std::nullopt;
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
        const auto alias = tryAliasTarget(executionSpec, semanticKey);
        if (!alias.has_value()) {
            return false;
        }
    }
    return true;
}

std::vector<std::shared_ptr<ws::ISubsystem>> compatiblePhase4Subsystems(const ws::ModelExecutionSpec& executionSpec) {
    std::vector<std::shared_ptr<ws::ISubsystem>> result;
    result.reserve(ws::makePhase4Subsystems().size());

    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        if (!subsystem) {
            continue;
        }
        if (subsystem->name() == "automaton" &&
            executionSpec.semanticFieldAliases.find("automaton.state") == executionSpec.semanticFieldAliases.end()) {
            continue;
        }
        if (subsystem->name() == "fire_spread" &&
            executionSpec.semanticFieldAliases.find("fire.state") == executionSpec.semanticFieldAliases.end()) {
            continue;
        }
        result.push_back(subsystem);
    }

    return result;
}

ModelFixture selectModelFixture() {
    const auto modelsRoot = resolveModelsRoot();
    assert(!modelsRoot.empty());

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

        const std::string fallback = executionSpec.cellScalarVariableIds.front();
        const std::string patchVariable = tryAnyAliasTarget(
            executionSpec,
            {
                "temperature.current",
                "hydrology.water",
                "resources.current",
                "climate.current"
            }).value_or(fallback);
        const std::string perturbationVariable = tryAnyAliasTarget(
            executionSpec,
            {
                "initialization.waves.target",
                "hydrology.water",
                "temperature.current"
            }).value_or(fallback);
        return ModelFixture{modelPath, executionSpec, patchVariable, perturbationVariable};
    }

    assert(false && "No compatible .simmodel fixture found for live patching test.");
    return {};
}

struct RuntimeFixture {
    ws::Runtime runtime;
    std::string patchVariable;
    std::string perturbationVariable;
};

RuntimeFixture makeRuntimeFixture() {
    const auto fixture = selectModelFixture();

    ws::RuntimeConfig config;
    config.seed = 777;
    config.grid = ws::GridSpec{8, 8};
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    config.profileInput = baselineProfileInput();

    config.modelExecutionSpec = fixture.executionSpec;

    ws::Runtime runtime(config);
    runtime.registerSubsystem(std::make_shared<ws::BootstrapSubsystem>());
    for (const auto& subsystem : compatiblePhase4Subsystems(fixture.executionSpec)) {
        runtime.registerSubsystem(subsystem);
    }
    runtime.start();
    runtime.pause();
    return RuntimeFixture{std::move(runtime), fixture.patchVariable, fixture.perturbationVariable};
}

void verifyParameterControlAndManualPatch() {
    auto fixture = makeRuntimeFixture();
    auto& runtime = fixture.runtime;

    std::string message;
    const bool manualPatchOk = runtime.applyManualPatch(fixture.patchVariable, ws::Cell{2u, 3u}, 0.91f, "manual_probe", message);
    assert(manualPatchOk);

    runtime.controlledStep(1);
    const auto sample = runtime.createCheckpoint("phase6_probe", true).stateSnapshot;
    const std::string patchedVariable = fixture.patchVariable;
    const auto it = std::find_if(sample.fields.begin(), sample.fields.end(), [&](const auto& f) {
        return f.spec.name == patchedVariable;
    });
    assert(it != sample.fields.end());
    const auto idx = static_cast<std::size_t>(3u * 8u + 2u);
    assert(idx < it->values.size());
    assert(it->values[idx] > 0.0f);

    const auto& manualEvents = runtime.manualEventLog();
    assert(manualEvents.size() >= 1);
}

void verifyGlobalManualPatchAppliesImmediately() {
    auto fixture = makeRuntimeFixture();
    auto& runtime = fixture.runtime;

    std::string message;
    const bool globalPatchOk = runtime.applyManualPatch(
        fixture.patchVariable,
        std::nullopt,
        0.42f,
        "global_init",
        message);
    assert(globalPatchOk);

    const auto checkpoint = runtime.createCheckpoint("global_patch_probe", true).stateSnapshot;
    const auto it = std::find_if(checkpoint.fields.begin(), checkpoint.fields.end(), [&](const auto& f) {
        return f.spec.name == fixture.patchVariable;
    });
    assert(it != checkpoint.fields.end());
    assert(!it->values.empty());
    for (const float v : it->values) {
        assert(std::isfinite(v));
        assert(v == 0.42f);
    }
}

void verifyPerturbationAndCheckpointPersistence() {
    auto fixture = makeRuntimeFixture();
    auto& runtime = fixture.runtime;

    ws::PerturbationSpec perturbation;
    perturbation.type = ws::PerturbationType::Gaussian;
    perturbation.targetVariable = fixture.perturbationVariable;
    perturbation.amplitude = 0.15f;
    perturbation.startStep = 0;
    perturbation.durationSteps = 2;
    perturbation.origin = ws::Cell{4u, 4u};
    perturbation.sigma = 2.0f;
    perturbation.description = "gaussian_test";

    std::string message;
    const bool perturbationOk = runtime.enqueuePerturbation(perturbation, message);
    assert(perturbationOk);

    runtime.controlledStep(1);

    const auto checkpoint = runtime.createCheckpoint("phase6_event_checkpoint", true);
    assert(!checkpoint.manualEventLog.empty());

    const auto tmpPath = std::filesystem::temp_directory_path() / "phase6_event_checkpoint.wscp";
    ws::app::writeCheckpointFile(checkpoint, tmpPath);
    const auto restored = ws::app::readCheckpointFile(tmpPath);
    assert(restored.manualEventLog.size() == checkpoint.manualEventLog.size());

    std::filesystem::remove(tmpPath);
}

} // namespace

int main() {
    verifyParameterControlAndManualPatch();
    verifyGlobalManualPatchAppliesImmediately();
    verifyPerturbationAndCheckpointPersistence();
    return 0;
}
