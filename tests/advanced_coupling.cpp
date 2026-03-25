#include "ws/core/runtime.hpp"
#include "ws/core/scheduler.hpp"
#include "ws/core/subsystems/subsystems.hpp"

#include <cassert>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

ws::ProfileResolverInput fullTierProfileInput(const ws::ModelTier tier) {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = tier;
    }
    input.compatibilityAssumptions = {
        "advanced_coupling_runtime",
        "temporal_multirate_stability_controls"
    };
    return input;
}

void verifyCTierAdmissionAndExecution() {
    ws::RuntimeConfig config;
    config.seed = 20260325;
    config.grid = ws::GridSpec{6, 6};
    config.temporalPolicy = ws::TemporalPolicy::MultiRateC;
    config.guardrailPolicy.multiRateMicroStepCount = 3;
    config.profileInput = fullTierProfileInput(ws::ModelTier::C);

    ws::Runtime runtime(config);
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        runtime.registerSubsystem(subsystem);
    }

    runtime.start();
    runtime.step();

    const auto& report = runtime.admissionReport();
    assert(report.admitted);
    assert(report.reproducibilityClass == ws::ReproducibilityClass::Exploratory);

    const auto& diagnostics = runtime.lastStepDiagnostics();
    assert(diagnostics.reproducibilityClass == ws::ReproducibilityClass::Exploratory);
    assert(diagnostics.stability.microStepsExecuted == 3);
    assert(std::isfinite(diagnostics.stability.driftMetric));

    runtime.stop();
}

ws::RuntimeSnapshot runBoundedScenario() {
    ws::RuntimeConfig config;
    config.seed = 314159;
    config.grid = ws::GridSpec{5, 5};
    config.temporalPolicy = ws::TemporalPolicy::MultiRateC;
    config.profileInput = fullTierProfileInput(ws::ModelTier::B);
    config.profileInput.requestedSubsystemTiers["hydrology"] = ws::ModelTier::C;
    config.profileInput.requestedSubsystemTiers["temperature"] = ws::ModelTier::C;
    config.profileInput.requestedSubsystemTiers["temporal"] = ws::ModelTier::C;

    ws::Runtime runtime(config);
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        runtime.registerSubsystem(subsystem);
    }

    runtime.start();
    for (std::uint64_t step = 0; step < 8; ++step) {
        runtime.step();
    }
    runtime.stop();
    return runtime.snapshot();
}

void verifyBoundedReproducibilityClassIsDeterministic() {
    const ws::RuntimeSnapshot runA = runBoundedScenario();
    const ws::RuntimeSnapshot runB = runBoundedScenario();

    assert(runA.runSignature.identityHash() == runB.runSignature.identityHash());
    assert(runA.reproducibilityClass == ws::ReproducibilityClass::BoundedDivergence);
    assert(runB.reproducibilityClass == ws::ReproducibilityClass::BoundedDivergence);
    assert(runA.stateHash == runB.stateHash);
}

class SpikeSubsystem final : public ws::ISubsystem {
public:
    [[nodiscard]] std::string name() const override {
        return "spike";
    }

    [[nodiscard]] std::vector<std::string> declaredReadSet() const override {
        return {"signal"};
    }

    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override {
        return {"signal"};
    }

    void initialize(const ws::StateStore&, ws::StateStore::WriteSession& writeSession, const ws::ModelProfile&) override {
        writeSession.fillScalar("signal", 0.0f);
    }

    void step(const ws::StateStore&, ws::StateStore::WriteSession& writeSession, const ws::ModelProfile&, std::uint64_t) override {
        writeSession.fillScalar("signal", 50.0f);
    }
};

void verifyEscalationFallbackPath() {
    ws::StateStore store(ws::GridSpec{2, 2});
    store.allocateScalarField(ws::VariableSpec{900, "signal"});

    ws::Scheduler scheduler;
    scheduler.registerSubsystem(std::make_shared<SpikeSubsystem>());

    ws::ModelProfile profile;
    profile.subsystemTiers["spike"] = ws::ModelTier::C;
    profile.subsystemTiers["temporal"] = ws::ModelTier::C;
    profile.compatibilityAssumptions = {"escalation_path_test"};

    scheduler.initialize(store, profile);

    ws::NumericGuardrailPolicy policy;
    policy.multiRateMicroStepCount = 1;
    policy.minAdaptiveSubIterations = 1;
    policy.maxAdaptiveSubIterations = 1;
    policy.divergenceSoftLimit = 0.001f;
    policy.divergenceHardLimit = 0.002f;
    policy.enableControlledFallback = true;

    const ws::StepDiagnostics diagnostics = scheduler.step(
        store,
        profile,
        ws::TemporalPolicy::MultiRateC,
        policy,
        0);

    assert(diagnostics.stability.dampingApplications >= 1);
    assert(diagnostics.stability.fallbackApplications == 1);
    assert(diagnostics.stability.finalEscalationAction == ws::EscalationAction::ControlledFallback);
}

} // namespace

int main() {
    verifyCTierAdmissionAndExecution();
    verifyBoundedReproducibilityClassIsDeterministic();
    verifyEscalationFallbackPath();
    return 0;
}
