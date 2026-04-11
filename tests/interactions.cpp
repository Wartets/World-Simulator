#include "ws/core/interactions.hpp"
#include "ws/core/initialization_binding.hpp"
#include "ws/core/runtime.hpp"
#include "ws/core/scheduler.hpp"
#include "ws/core/subsystems/subsystems.hpp"

#include <algorithm>
#include <cassert>
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

ws::ProfileResolverInput makeProfileInput(const ws::ModelTier tier, const ws::ModelTier temporalTier) {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        if (subsystem) {
            input.requestedSubsystemTiers[subsystem->name()] = tier;
        }
    }
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = tier;
    }
    input.requestedSubsystemTiers["temporal"] = temporalTier;
    input.compatibilityAssumptions = {
        "interaction_graph_certified",
        "deterministic_admission_gate"
    };
    return input;
}

void verifyTemporalAdmissionBlocksMismatch() {
    const auto executionSpec = selectExecutionSpec();
    assert(executionSpec.has_value());

    ws::RuntimeConfig config;
    config.seed = 99;
    config.grid = ws::GridSpec{4, 4};
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    config.profileInput = makeProfileInput(ws::ModelTier::Minimal, ws::ModelTier::Standard);
    config.modelExecutionSpec = *executionSpec;

    ws::Runtime runtime(config);
    for (const auto& subsystem : compatiblePhase4Subsystems(*executionSpec)) {
        runtime.registerSubsystem(subsystem);
    }

    bool threw = false;
    try {
        runtime.start();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void verifyNoHardcodedHydrologyEventsGate() {
    const auto executionSpec = selectExecutionSpec();
    assert(executionSpec.has_value());

    ws::RuntimeConfig config;
    config.seed = 101;
    config.grid = ws::GridSpec{4, 4};
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    config.profileInput = makeProfileInput(ws::ModelTier::Minimal, ws::ModelTier::Minimal);
    config.profileInput.requestedSubsystemTiers["events"] = ws::ModelTier::Standard;
    config.modelExecutionSpec = *executionSpec;

    ws::Runtime runtime(config);
    for (const auto& subsystem : compatiblePhase4Subsystems(*executionSpec)) {
        runtime.registerSubsystem(subsystem);
    }

    runtime.start();
    assert(runtime.admissionReport().admitted);
    runtime.stop();
}

void verifyDeterministicAdmissionGraph() {
    const auto executionSpec = selectExecutionSpec();
    assert(executionSpec.has_value());

    ws::RuntimeConfig config;
    config.seed = 555;
    config.grid = ws::GridSpec{6, 6};
    config.temporalPolicy = ws::TemporalPolicy::PhasedB;
    config.profileInput = makeProfileInput(ws::ModelTier::Standard, ws::ModelTier::Standard);
    config.modelExecutionSpec = *executionSpec;

    ws::Runtime runA(config);
    ws::Runtime runB(config);
    for (const auto& subsystem : compatiblePhase4Subsystems(*executionSpec)) {
        runA.registerSubsystem(subsystem);
        runB.registerSubsystem(subsystem);
    }

    runA.start();
    runB.start();

    assert(runA.admissionReport().admitted);
    assert(runA.admissionReport().fingerprint == runB.admissionReport().fingerprint);
    assert(runA.admissionReport().serializedGraph == runB.admissionReport().serializedGraph);
    assert(runA.snapshot().runSignature.identityHash() == runB.snapshot().runSignature.identityHash());

    runA.stop();
    runB.stop();
}

class UndeclaredReadSubsystem final : public ws::ISubsystem {
public:
    [[nodiscard]] std::string name() const override {
        return "undeclared_reader";
    }

    [[nodiscard]] std::vector<std::string> declaredReadSet() const override {
        return {};
    }

    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override {
        return {"signal_b"};
    }

    void initialize(const ws::StateStore&, ws::StateStore::WriteSession& writeSession, const ws::ModelProfile&) override {
        writeSession.fillScalar("signal_b", 0.0f);
    }

    void step(const ws::StateStore& stateStore, ws::StateStore::WriteSession& writeSession, const ws::ModelProfile&, std::uint64_t) override {
        const auto sample = stateStore.trySampleScalar("signal_a", ws::CellSigned{0, 0});
        writeSession.setScalar("signal_b", ws::Cell{0, 0}, sample.value_or(0.0f));
    }
};

void verifyObservedDataFlowEnforcement() {
    ws::Scheduler scheduler;
    scheduler.registerSubsystem(std::make_shared<UndeclaredReadSubsystem>());

    ws::StateStore stateStore(ws::GridSpec{1, 1});
    stateStore.allocateScalarField(ws::VariableSpec{1, "signal_a"});
    stateStore.allocateScalarField(ws::VariableSpec{2, "signal_b"});

    ws::ModelProfile profile;
    profile.subsystemTiers["undeclared_reader"] = ws::ModelTier::Minimal;
    profile.subsystemTiers["temporal"] = ws::ModelTier::Minimal;
    profile.compatibilityAssumptions = {"data_flow_enforcement"};

    ws::InteractionCoordinator coordinator;
    const ws::AdmissionReport report = coordinator.buildAdmissionReport(
        profile,
        ws::TemporalPolicy::UniformA,
        scheduler.registeredSubsystems());
    assert(report.admitted);

    scheduler.setAdmissionReport(report);
    scheduler.initialize(stateStore, profile);

    bool threw = false;
    try {
        const ws::StepDiagnostics diagnostics = scheduler.step(
            stateStore,
            profile,
            ws::TemporalPolicy::UniformA,
            ws::NumericGuardrailPolicy{},
            0);
        (void)diagnostics;
        scheduler.validateObservedDataFlow();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

} // namespace

int main() {
    verifyTemporalAdmissionBlocksMismatch();
    verifyNoHardcodedHydrologyEventsGate();
    verifyDeterministicAdmissionGraph();
    verifyObservedDataFlowEnforcement();
    return 0;
}
