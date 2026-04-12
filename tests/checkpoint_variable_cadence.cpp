#include "ws/core/checkpoint_manager.hpp"
#include "ws/core/runtime.hpp"
#include "ws/core/subsystems/bootstrap_subsystem.hpp"

#include <cassert>
#include <memory>
#include <optional>
#include <string>

namespace {

ws::ProfileResolverInput baselineProfileInput() {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = ws::ModelTier::Minimal;
    }
    input.requestedSubsystemTiers["bootstrap"] = ws::ModelTier::Minimal;
    input.compatibilityAssumptions = {
        "checkpoint_variable_cadence_test",
        "deterministic_serial_scheduler"
    };
    return input;
}

const ws::StateStoreSnapshot::FieldPayload* findField(
    const ws::RuntimeCheckpoint& checkpoint,
    const std::string& variableName) {
    for (const auto& field : checkpoint.stateSnapshot.fields) {
        if (field.spec.name == variableName) {
            return &field;
        }
    }
    return nullptr;
}

void verifyVariableCadenceReusesLastPersistedPayload() {
    ws::RuntimeConfig config;
    config.seed = 9001;
    config.grid = ws::GridSpec{8, 8};
    config.profileInput = baselineProfileInput();

    ws::Runtime runtime(config);
    runtime.registerSubsystem(std::make_shared<ws::BootstrapSubsystem>());
    runtime.start();

    ws::CheckpointManager manager(ws::CheckpointManagerConfig{
        true,
        1u,
        16u,
        {{"resource_stock_r", 2u}},
        false});

    std::string message;
    assert(manager.captureNow(runtime, "baseline", message));

    const auto baselineCheckpoint = manager.checkpointAtStep(0u);
    assert(baselineCheckpoint.has_value());
    assert(!baselineCheckpoint->stateSnapshot.fields.empty());

    std::string trackedVariable;
    ws::RuntimeCheckpoint rawStep1;
    bool foundMutableField = false;
    for (std::size_t fieldIndex = 0; fieldIndex < baselineCheckpoint->stateSnapshot.fields.size(); ++fieldIndex) {
        const auto& baselineFieldCandidate = baselineCheckpoint->stateSnapshot.fields[fieldIndex];
        if (baselineFieldCandidate.values.empty()) {
            continue;
        }

        runtime.resetToCheckpoint(*baselineCheckpoint);
        ws::RuntimeInputFrame inputFrame;
        inputFrame.scalarPatches.push_back(ws::ScalarWritePatch{
            baselineFieldCandidate.spec.name,
            ws::Cell{0u, 0u},
            static_cast<float>(1000.0 + static_cast<double>(fieldIndex))});
        runtime.queueInput(std::move(inputFrame));
        runtime.controlledStep(1u);

        const auto probeCheckpoint = runtime.createCheckpoint("raw_step_1_probe", true);
        const auto* probeField = findField(probeCheckpoint, baselineFieldCandidate.spec.name);
        if (probeField == nullptr || probeField->values.empty()) {
            continue;
        }

        if (probeField->values.front() != baselineFieldCandidate.values.front()) {
            trackedVariable = baselineFieldCandidate.spec.name;
            rawStep1 = probeCheckpoint;
            foundMutableField = true;
            break;
        }
    }
    assert(foundMutableField);
    assert(manager.captureNow(runtime, "cadence_step_1", message));

    const auto cadenceStep1 = manager.checkpointAtStep(1u);
    assert(cadenceStep1.has_value());

    const auto* baselineField = findField(*baselineCheckpoint, trackedVariable);
    const auto* rawStep1Field = findField(rawStep1, trackedVariable);
    const auto* cadenceStep1Field = findField(*cadenceStep1, trackedVariable);
    assert(baselineField != nullptr);
    assert(rawStep1Field != nullptr);
    assert(cadenceStep1Field != nullptr);

    assert(!baselineField->values.empty());
    assert(!rawStep1Field->values.empty());
    assert(!cadenceStep1Field->values.empty());

    const float baselineValue = baselineField->values.front();
    const float rawStep1Value = rawStep1Field->values.front();
    const float cadenceStep1Value = cadenceStep1Field->values.front();

    assert(rawStep1Value != baselineValue);
    assert(cadenceStep1Value == baselineValue);

    assert(cadenceStep1->variableCheckpointIntervalSteps.contains("resource_stock_r"));
    assert(cadenceStep1->checkpointIncludeUnspecifiedVariables == false);
}

} // namespace

int main() {
    verifyVariableCadenceReusesLastPersistedPayload();
    return 0;
}
