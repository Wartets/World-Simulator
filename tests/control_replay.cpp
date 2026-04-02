#include "ws/core/control_surface.hpp"
#include "ws/core/initialization_binding.hpp"
#include "ws/core/replay.hpp"
#include "ws/core/runtime.hpp"
#include "ws/core/subsystems/subsystems.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
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
    input.compatibilityAssumptions = {
        "control_surface_runtime_contract",
        "replay_observability_trace_contract"
    };
    return input;
}

ws::RuntimeConfig baselineConfig() {
struct ReplayFixture {
    ws::RuntimeConfig config;
    std::string patchVariable;
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

ReplayFixture baselineFixture() {
    ws::RuntimeConfig config;
    config.seed = 20260325;
    config.grid = ws::GridSpec{6, 6};
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    config.profileInput = baselineProfileInput();

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

        config.modelExecutionSpec = executionSpec;
        const std::string fallback = executionSpec.cellScalarVariableIds.front();
        const std::string patchVariable = tryAnyAliasTarget(
            executionSpec,
            {
                "temperature.current",
                "hydrology.water",
                "resources.current",
                "climate.current"
            }).value_or(fallback);
        return ReplayFixture{config, patchVariable};
    }

    assert(false && "No compatible .simmodel fixture found for control replay test.");
    return ReplayFixture{config, ""};
}

void verifyControlActionsAreTracedAndDeterministic() {
    const auto fixture = baselineFixture();
    ws::Runtime runtime(fixture.config);
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        runtime.registerSubsystem(subsystem);
    }

    ws::RuntimeControlSurface control(runtime);
    control.selectProfile(baselineProfileInput());

    runtime.start();
    const ws::RuntimeCheckpoint checkpoint = runtime.createCheckpoint("control_surface_begin");

    control.pause();

    ws::RuntimeInputFrame inputFrame;
    inputFrame.scalarPatches.push_back(ws::ScalarWritePatch{fixture.patchVariable, ws::Cell{1, 1}, 292.0f});
    control.queueInput(std::move(inputFrame));

    ws::RuntimeEvent event;
    event.eventName = "operator_event";
    event.scalarPatches.push_back(ws::ScalarWritePatch{fixture.patchVariable, ws::Cell{1, 1}, 0.8f});
    control.enqueueEvent(std::move(event));

    control.step(1);
    control.resume();
    runtime.step();
    control.reset(checkpoint);
    control.step(1);
    runtime.stop();

    const auto records = runtime.traceRecords();
    const auto containsName = [&](const std::string& value) {
        return std::find_if(records.begin(), records.end(), [&](const ws::TraceRecord& record) {
            return record.name == value;
        }) != records.end();
    };

    assert(containsName("runtime.config.profile_selected"));
    assert(containsName("control.pause"));
    assert(containsName("control.step"));
    assert(containsName("control.reset"));
    assert(containsName("runtime.input.patch.queued"));
    assert(containsName("runtime.event.queued"));
    assert(containsName("runtime.event.applied"));

    const ws::RuntimeMetrics metrics = runtime.metrics();
    assert(metrics.configurationTransactions >= 1);
    assert(metrics.controlTransactions >= 4);
    assert(metrics.inputPatches >= 1);
    assert(metrics.eventsQueued >= 1);
    assert(metrics.eventsApplied >= 1);
    assert(metrics.stepsExecuted >= 3);

    for (const auto& record : records) {
        if (record.name == "runtime.config.profile_selected") {
            continue;
        }
        assert(record.runIdentityHash == runtime.snapshot().runSignature.identityHash());
    }
}

void verifyReplayFromCheckpointAndEventChronology() {
    const auto fixture = baselineFixture();
    ws::Runtime runtime(fixture.config);
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        runtime.registerSubsystem(subsystem);
    }

    runtime.start();
    runtime.step();
    runtime.step();

    const ws::RuntimeCheckpoint checkpoint = runtime.createCheckpoint("replay_anchor");
    const std::uint64_t checkpointStep = checkpoint.stateSnapshot.header.stepIndex;

    for (std::uint64_t i = 0; i < 4; ++i) {
        ws::RuntimeEvent event;
        event.eventName = "scheduled_event_" + std::to_string(i);
        event.scalarPatches.push_back(ws::ScalarWritePatch{
            fixture.patchVariable,
            ws::Cell{static_cast<std::uint32_t>(i % 3), static_cast<std::uint32_t>((i + 1) % 3)},
            0.2f + 0.1f * static_cast<float>(i)});
        runtime.enqueueEvent(std::move(event));
        runtime.step();
    }

    const ws::RuntimeSnapshot referenceSnapshot = runtime.snapshot();
    const auto chronology = runtime.eventChronology();
    runtime.stop();

    std::map<std::uint64_t, ws::ReplayFrame> frameByStep;
    for (const auto& record : chronology) {
        if (record.stepIndex < checkpointStep) {
            continue;
        }
        auto& frame = frameByStep[record.stepIndex];
        frame.stepIndex = record.stepIndex;
        frame.events.push_back(record.event);
    }

    ws::ReplayPlan plan;
    plan.checkpoint = checkpoint;
    for (const auto& [stepIndex, frame] : frameByStep) {
        (void)stepIndex;
        plan.frames.push_back(frame);
    }
    plan.stepCount = referenceSnapshot.stateHeader.stepIndex - checkpointStep;
    plan.expectedFinalStateHash = referenceSnapshot.stateHash;

    ws::ReplayRunner replayRunner(
        fixture.config,
        []() {
            return ws::makePhase4Subsystems();
        });

    const ws::ReplayResult replayResult = replayRunner.run(plan);
    assert(replayResult.finalSnapshot.has_value());
    const ws::RunComparison comparison = ws::ReplayRunner::compareSnapshots(referenceSnapshot, replayResult.finalSnapshot.value());

    assert(replayResult.runIdentityMatch);
    assert(replayResult.deterministicMatch);
    assert(replayResult.replayStateHash == referenceSnapshot.stateHash);
    assert(comparison.runIdentityEqual);
    assert(comparison.stateHashEqual);
    assert(comparison.stepIndexDelta == 0);
}

} // namespace

int main() {
    verifyControlActionsAreTracedAndDeterministic();
    verifyReplayFromCheckpointAndEventChronology();
    return 0;
}
