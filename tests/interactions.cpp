#include "ws/core/interactions.hpp"
#include "ws/core/runtime.hpp"
#include "ws/core/scheduler.hpp"
#include "ws/core/subsystems/subsystems.hpp"

#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

ws::ProfileResolverInput makeProfileInput(const ws::ModelTier tier, const ws::ModelTier temporalTier) {
    ws::ProfileResolverInput input;
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
    ws::RuntimeConfig config;
    config.seed = 99;
    config.grid = ws::GridSpec{4, 4};
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    config.profileInput = makeProfileInput(ws::ModelTier::A, ws::ModelTier::B);

    ws::Runtime runtime(config);
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
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

void verifyKnownIncompatibleCombinationBlocked() {
    ws::RuntimeConfig config;
    config.seed = 101;
    config.grid = ws::GridSpec{4, 4};
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    config.profileInput = makeProfileInput(ws::ModelTier::A, ws::ModelTier::A);
    config.profileInput.requestedSubsystemTiers["events"] = ws::ModelTier::B;

    ws::Runtime runtime(config);
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
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

void verifyDeterministicAdmissionGraph() {
    ws::RuntimeConfig config;
    config.seed = 555;
    config.grid = ws::GridSpec{6, 6};
    config.temporalPolicy = ws::TemporalPolicy::PhasedB;
    config.profileInput = makeProfileInput(ws::ModelTier::B, ws::ModelTier::B);

    ws::Runtime runA(config);
    ws::Runtime runB(config);
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
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
    profile.subsystemTiers["undeclared_reader"] = ws::ModelTier::A;
    profile.subsystemTiers["temporal"] = ws::ModelTier::A;
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
    verifyKnownIncompatibleCombinationBlocked();
    verifyDeterministicAdmissionGraph();
    verifyObservedDataFlowEnforcement();
    return 0;
}
