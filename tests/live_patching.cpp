#include "ws/app/checkpoint_io.hpp"
#include "ws/core/initialization_binding.hpp"
#include "ws/core/runtime.hpp"
#include "ws/core/subsystems/subsystems.hpp"
#include "ws/core/subsystems/bootstrap_subsystem.hpp"

#include <cassert>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

ws::ProfileResolverInput baselineProfileInput() {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        input.requestedSubsystemTiers[subsystem->name()] = ws::ModelTier::A;
    }
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = ws::ModelTier::A;
    }
    input.requestedSubsystemTiers["bootstrap"] = ws::ModelTier::A;
    input.compatibilityAssumptions = {
        "phase6_live_patching_test",
        "deterministic_manual_event_pipeline"
    };
    return input;
}

ws::Runtime makeRuntime() {
    ws::RuntimeConfig config;
    config.seed = 777;
    config.grid = ws::GridSpec{8, 8};
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    config.profileInput = baselineProfileInput();

    const std::filesystem::path modelPath = std::filesystem::path("..") / "models" / "environmental_model_2d.simmodel";
    ws::ModelExecutionSpec executionSpec;
    std::string executionMessage;
    const bool executionOk = ws::initialization::loadModelExecutionSpec(modelPath, executionSpec, executionMessage);
    assert(executionOk);
    config.modelExecutionSpec = executionSpec;

    ws::Runtime runtime(config);
    runtime.registerSubsystem(std::make_shared<ws::BootstrapSubsystem>());
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        runtime.registerSubsystem(subsystem);
    }
    runtime.start();
    runtime.pause();
    return runtime;
}

void verifyParameterControlAndManualPatch() {
    auto runtime = makeRuntime();

    std::string message;
    const bool manualPatchOk = runtime.applyManualPatch("temperature", ws::Cell{2u, 3u}, 0.91f, "manual_probe", message);
    assert(manualPatchOk);

    runtime.controlledStep(1);
    const auto sample = runtime.createCheckpoint("phase6_probe", true).stateSnapshot;
    const auto it = std::find_if(sample.fields.begin(), sample.fields.end(), [](const auto& f) {
        return f.spec.name == "temperature";
    });
    assert(it != sample.fields.end());
    const auto idx = static_cast<std::size_t>(3u * 8u + 2u);
    assert(idx < it->values.size());
    assert(it->values[idx] > 0.0f);

    const auto& manualEvents = runtime.manualEventLog();
    assert(manualEvents.size() >= 1);
}

void verifyPerturbationAndCheckpointPersistence() {
    auto runtime = makeRuntime();

    ws::PerturbationSpec perturbation;
    perturbation.type = ws::PerturbationType::Gaussian;
    perturbation.targetVariable = "water_height";
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

    const auto tmpPath = std::filesystem::path("build") / "phase6_event_checkpoint.wscp";
    ws::app::writeCheckpointFile(checkpoint, tmpPath);
    const auto restored = ws::app::readCheckpointFile(tmpPath);
    assert(restored.manualEventLog.size() == checkpoint.manualEventLog.size());

    std::filesystem::remove(tmpPath);
}

} // namespace

int main() {
    verifyParameterControlAndManualPatch();
    verifyPerturbationAndCheckpointPersistence();
    return 0;
}
