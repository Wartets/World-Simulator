#include "ws/core/runtime.hpp"
#include "ws/core/scheduler.hpp"
#include "ws/core/subsystems/bootstrap_subsystem.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct ContractCounters {
    std::uint64_t preCalls = 0;
    std::uint64_t postCalls = 0;
};

class ConstantWriteSubsystem final : public ws::ISubsystem {
public:
    ConstantWriteSubsystem(
        std::string subsystemName,
        std::string variableName,
        const float value,
        ContractCounters* counters)
        : subsystemName_(std::move(subsystemName)),
          variableName_(std::move(variableName)),
          value_(value),
          counters_(counters) {}

    [[nodiscard]] std::string name() const override {
        return subsystemName_;
    }

    [[nodiscard]] std::vector<std::string> declaredReadSet() const override {
        return {};
    }

    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override {
        return {variableName_};
    }

    void initialize(const ws::StateStore&, ws::StateStore::WriteSession& writeSession, const ws::ModelProfile&) override {
        writeSession.fillScalar(variableName_, 0.0f);
    }

    void preStep(const ws::ModelProfile&, const std::uint64_t) override {
        if (counters_ != nullptr) {
            counters_->preCalls += 1;
        }
    }

    void step(const ws::StateStore&, ws::StateStore::WriteSession& writeSession, const ws::ModelProfile&, const std::uint64_t) override {
        writeSession.fillScalar(variableName_, value_);
    }

    void postStep(const ws::ModelProfile&, const std::uint64_t) override {
        if (counters_ != nullptr) {
            counters_->postCalls += 1;
        }
    }

private:
    std::string subsystemName_;
    std::string variableName_;
    float value_ = 0.0f;
    ContractCounters* counters_ = nullptr;
};

ws::ProfileResolverInput baselineProfileInput() {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = ws::ModelTier::Minimal;
    }
    input.requestedSubsystemTiers["bootstrap"] = ws::ModelTier::Minimal;
    input.compatibilityAssumptions = {
        "execution_test",
        "deterministic_scheduler_order"
    };
    return input;
}

void verifyTemporalPoliciesAndContracts() {
    ws::StateStore stateStore(ws::GridSpec{1, 1});
    stateStore.allocateScalarField(ws::VariableSpec{100, "alpha_v"});
    stateStore.allocateScalarField(ws::VariableSpec{101, "beta_v"});

    ContractCounters alphaCounters{};
    ContractCounters betaCounters{};

    ws::Scheduler scheduler;
    scheduler.registerSubsystem(std::make_shared<ConstantWriteSubsystem>("beta", "beta_v", 2.0f, &betaCounters));
    scheduler.registerSubsystem(std::make_shared<ConstantWriteSubsystem>("alpha", "alpha_v", 1.0f, &alphaCounters));

    ws::ModelProfile profile;
    profile.subsystemTiers["alpha"] = ws::ModelTier::Minimal;
    profile.subsystemTiers["beta"] = ws::ModelTier::Standard;
    profile.compatibilityAssumptions.insert("phase3_test_profile");

    scheduler.initialize(stateStore, profile);

    ws::NumericGuardrailPolicy relaxedPolicy;
    relaxedPolicy.clampMin = -1000.0f;
    relaxedPolicy.clampMax = 1000.0f;
    relaxedPolicy.maxAbsDeltaPerStep = 1000.0f;

    const ws::StepDiagnostics uniformDiagnostics = scheduler.step(
        stateStore,
        profile,
        ws::TemporalPolicy::UniformA,
        relaxedPolicy,
        0);

    const auto alphaUpdateIt = std::find(uniformDiagnostics.orderingLog.begin(), uniformDiagnostics.orderingLog.end(), "update:alpha");
    const auto betaUpdateIt = std::find(uniformDiagnostics.orderingLog.begin(), uniformDiagnostics.orderingLog.end(), "update:beta");
    assert(alphaUpdateIt != uniformDiagnostics.orderingLog.end());
    assert(betaUpdateIt != uniformDiagnostics.orderingLog.end());
    assert(alphaUpdateIt < betaUpdateIt);

    assert(alphaCounters.preCalls == 1);
    assert(alphaCounters.postCalls == 1);
    assert(betaCounters.preCalls == 1);
    assert(betaCounters.postCalls == 1);

    const ws::StepDiagnostics phasedDiagnostics = scheduler.step(
        stateStore,
        profile,
        ws::TemporalPolicy::PhasedB,
        relaxedPolicy,
        1);

    const auto phaseAlphaIt = std::find(phasedDiagnostics.orderingLog.begin(), phasedDiagnostics.orderingLog.end(), "phase0:update:alpha");
    const auto phaseBetaIt = std::find(phasedDiagnostics.orderingLog.begin(), phasedDiagnostics.orderingLog.end(), "phase0:update:beta");
    assert(phaseAlphaIt != phasedDiagnostics.orderingLog.end());
    assert(phaseBetaIt != phasedDiagnostics.orderingLog.end());
    assert(phaseAlphaIt < phaseBetaIt);
}

void verifyNumericalGuardrails() {
    ws::StateStore stateStore(ws::GridSpec{1, 1});
    stateStore.allocateScalarField(ws::VariableSpec{200, "signal"});

    ws::Scheduler scheduler;
    scheduler.registerSubsystem(std::make_shared<ConstantWriteSubsystem>("signal_writer", "signal", 500.0f, nullptr));

    ws::ModelProfile profile;
    profile.subsystemTiers["signal_writer"] = ws::ModelTier::Minimal;
    profile.compatibilityAssumptions.insert("guardrail_test");
    profile.conservedVariables = {"signal"};

    scheduler.initialize(stateStore, profile);

    ws::NumericGuardrailPolicy policy;
    policy.clampMin = -100.0f;
    policy.clampMax = 100.0f;
    policy.maxAbsDeltaPerStep = 10.0f;

    const ws::StepDiagnostics diagnostics = scheduler.step(
        stateStore,
        profile,
        ws::TemporalPolicy::UniformA,
        policy,
        0);

    const auto value = stateStore.trySampleScalar("signal", ws::CellSigned{0, 0});
    assert(value.has_value());
    assert(*value == 10.0f);
    assert(!diagnostics.constraintViolations.empty());
    assert(!diagnostics.stabilityAlerts.empty());
    assert(!diagnostics.stability.conservationResiduals.empty());
    assert(diagnostics.stability.conservationResiduals.front().variableName == "signal");
}

void verifyNaNFailFast() {
    ws::StateStore stateStore(ws::GridSpec{1, 1});
    stateStore.allocateScalarField(ws::VariableSpec{300, "nan_probe"});

    ws::Scheduler scheduler;
    scheduler.registerSubsystem(std::make_shared<ConstantWriteSubsystem>(
        "nan_writer",
        "nan_probe",
        std::numeric_limits<float>::quiet_NaN(),
        nullptr));

    ws::ModelProfile profile;
    profile.subsystemTiers["nan_writer"] = ws::ModelTier::Minimal;
    profile.compatibilityAssumptions.insert("nan_failfast_test");

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
    } catch (const std::runtime_error&) {
        threw = true;
    }

    assert(threw);
}

void verifyCrossVariableConstraintEnforcement() {
    ws::StateStore stateStore(ws::GridSpec{1, 1});
    stateStore.allocateScalarField(ws::VariableSpec{400, "lhs_value"});
    stateStore.allocateScalarField(ws::VariableSpec{401, "rhs_value"});

    ws::Scheduler scheduler;
    scheduler.registerSubsystem(std::make_shared<ConstantWriteSubsystem>("lhs_writer", "lhs_value", 5.0f, nullptr));
    scheduler.registerSubsystem(std::make_shared<ConstantWriteSubsystem>("rhs_writer", "rhs_value", 2.0f, nullptr));

    ws::ModelProfile profile;
    profile.subsystemTiers["lhs_writer"] = ws::ModelTier::Minimal;
    profile.subsystemTiers["rhs_writer"] = ws::ModelTier::Minimal;
    ws::CrossVariableConstraint constraint;
    constraint.id = "lhs_lte_rhs";
    constraint.lhsVariable = "lhs_value";
    constraint.rhsVariable = "rhs_value";
    constraint.relation = ws::CrossVariableRelation::LessEqual;
    constraint.offset = 0.0f;
    constraint.tolerance = 0.0f;
    constraint.autoClamp = true;
    profile.crossVariableConstraints.push_back(constraint);

    scheduler.initialize(stateStore, profile);

    ws::NumericGuardrailPolicy relaxedPolicy;
    relaxedPolicy.clampEnabled = false;
    relaxedPolicy.boundedIncrementEnabled = false;

    const ws::StepDiagnostics diagnostics = scheduler.step(
        stateStore,
        profile,
        ws::TemporalPolicy::UniformA,
        relaxedPolicy,
        0);

    const auto lhsValue = stateStore.trySampleScalar("lhs_value", ws::CellSigned{0, 0});
    const auto rhsValue = stateStore.trySampleScalar("rhs_value", ws::CellSigned{0, 0});
    assert(lhsValue.has_value());
    assert(rhsValue.has_value());
    assert(*lhsValue <= *rhsValue);
    assert(!diagnostics.constraintViolations.empty());
    assert(std::any_of(
        diagnostics.constraintViolations.begin(),
        diagnostics.constraintViolations.end(),
        [](const std::string& violation) {
            return violation.find("cross_constraint:id=lhs_lte_rhs") != std::string::npos;
        }));
}

struct ScenarioResult {
    std::uint64_t runIdentityHash = 0;
    std::uint64_t stateHash = 0;
    std::vector<std::string> orderingLog;
};

ScenarioResult runBaselineScenario(const ws::TemporalPolicy temporalPolicy, const std::uint64_t seed) {
    ws::RuntimeConfig config;
    config.seed = seed;
    config.grid = ws::GridSpec{4, 4};
    config.temporalPolicy = temporalPolicy;
    config.profileInput = baselineProfileInput();
    if (temporalPolicy == ws::TemporalPolicy::PhasedB) {
        config.profileInput.requestedSubsystemTiers["temporal"] = ws::ModelTier::Standard;
    }

    ws::Runtime runtime(config);
    runtime.registerSubsystem(std::make_shared<ws::BootstrapSubsystem>());
    runtime.start();

    for (std::uint64_t step = 0; step < 10000; ++step) {
        runtime.step();
    }

    const ws::StepDiagnostics diagnostics = runtime.lastStepDiagnostics();
    assert(runtime.validateDeterminism(runtime.stateHashHistory()));
    assert(runtime.computeStateHash() == runtime.snapshot().stateHash);
    runtime.stop();

    return ScenarioResult{
        runtime.snapshot().runSignature.identityHash(),
        runtime.snapshot().stateHash,
        diagnostics.orderingLog};
}

void verifyDeterministicReplayAndTemporalDistinction() {
    const ScenarioResult uniformRunA = runBaselineScenario(ws::TemporalPolicy::UniformA, 424242);
    const ScenarioResult uniformRunB = runBaselineScenario(ws::TemporalPolicy::UniformA, 424242);

    assert(uniformRunA.runIdentityHash == uniformRunB.runIdentityHash);
    assert(uniformRunA.stateHash == uniformRunB.stateHash);
    assert(uniformRunA.orderingLog == uniformRunB.orderingLog);

    const ScenarioResult phasedRun = runBaselineScenario(ws::TemporalPolicy::PhasedB, 424242);
    assert(uniformRunA.orderingLog != phasedRun.orderingLog);

    const auto phasedBootstrapIt = std::find(
        phasedRun.orderingLog.begin(),
        phasedRun.orderingLog.end(),
        "phase0:update:bootstrap");
    assert(phasedBootstrapIt != phasedRun.orderingLog.end());
}

void verifyRuntimeInputAndEventPipeline() {
    ws::RuntimeConfig config;
    config.seed = 101;
    config.grid = ws::GridSpec{2, 2};
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    config.profileInput = baselineProfileInput();

    ws::Runtime runtime(config);
    runtime.registerSubsystem(std::make_shared<ws::BootstrapSubsystem>());
    runtime.start();

    ws::RuntimeInputFrame inputFrame;
    inputFrame.scalarPatches.push_back(ws::ScalarWritePatch{"bootstrap_marker", ws::Cell{0, 0}, 290.0f});
    runtime.queueInput(std::move(inputFrame));

    ws::RuntimeEvent event;
    event.eventName = "humidity_injection";
    event.scalarPatches.push_back(ws::ScalarWritePatch{"bootstrap_marker", ws::Cell{0, 0}, 0.7f});
    runtime.enqueueEvent(std::move(event));

    runtime.step();
    const ws::StepDiagnostics diagnostics = runtime.lastStepDiagnostics();
    runtime.stop();

    assert(diagnostics.inputPatchesApplied == 1);
    assert(diagnostics.eventPatchesApplied == 1);
    assert(diagnostics.eventsApplied == 1);
    assert(!diagnostics.orderingLog.empty());
    assert(diagnostics.orderingLog.front() == "pipeline:input_ingest");
}

} // namespace

int main() {
    verifyTemporalPoliciesAndContracts();
    verifyNumericalGuardrails();
    verifyNaNFailFast();
    verifyCrossVariableConstraintEnforcement();
    verifyDeterministicReplayAndTemporalDistinction();
    verifyRuntimeInputAndEventPipeline();
    return 0;
}
