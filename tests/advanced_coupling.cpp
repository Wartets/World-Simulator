#include "ws/core/runtime.hpp"
#include "ws/core/scheduler.hpp"
#include "ws/core/initialization_binding.hpp"
#include "ws/core/subsystems/subsystems.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <optional>
#include <memory>
#include <stdexcept>
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

std::optional<ws::ModelExecutionSpec> loadCompatibleExecutionSpec(const std::filesystem::path& modelPath) {
    ws::ModelExecutionSpec executionSpec;
    std::string message;
    if (!ws::initialization::loadModelExecutionSpec(modelPath, executionSpec, message)) {
        return std::nullopt;
    }
    if (executionSpec.cellScalarVariableIds.empty()) {
        return std::nullopt;
    }
    return executionSpec;
}

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
    for (const auto& candidate : candidates) {
        const auto executionSpec = loadCompatibleExecutionSpec(candidate);
        if (executionSpec.has_value()) {
            return executionSpec;
        }
    }
    return std::nullopt;
}

void verifyCTierAdmissionAndExecution() {
    const auto executionSpec = selectExecutionSpec();
    assert(executionSpec.has_value());

    ws::RuntimeConfig config;
    config.seed = 20260325;
    config.grid = ws::GridSpec{6, 6};
    config.temporalPolicy = ws::TemporalPolicy::MultiRateC;
    config.guardrailPolicy.multiRateMicroStepCount = 3;
    config.profileInput = fullTierProfileInput(ws::ModelTier::Advanced);
    config.modelExecutionSpec = *executionSpec;

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
    const auto executionSpec = selectExecutionSpec();
    assert(executionSpec.has_value());

    ws::RuntimeConfig config;
    config.seed = 314159;
    config.grid = ws::GridSpec{5, 5};
    config.temporalPolicy = ws::TemporalPolicy::MultiRateC;
    config.profileInput = fullTierProfileInput(ws::ModelTier::Standard);
    config.profileInput.requestedSubsystemTiers["hydrology"] = ws::ModelTier::Advanced;
    config.profileInput.requestedSubsystemTiers["temperature"] = ws::ModelTier::Advanced;
    config.profileInput.requestedSubsystemTiers["temporal"] = ws::ModelTier::Advanced;
    config.modelExecutionSpec = *executionSpec;

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
    const auto executionSpec = selectExecutionSpec();
    assert(executionSpec.has_value());

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
    const auto executionSpec = selectExecutionSpec();
    assert(executionSpec.has_value());

    ws::StateStore store(ws::GridSpec{2, 2});
    store.allocateScalarField(ws::VariableSpec{900, "signal"});

    ws::Scheduler scheduler;
    scheduler.registerSubsystem(std::make_shared<SpikeSubsystem>());

    ws::ModelProfile profile;
    profile.subsystemTiers["spike"] = ws::ModelTier::Advanced;
    profile.subsystemTiers["temporal"] = ws::ModelTier::Advanced;
    profile.compatibilityAssumptions = {"escalation_path_test"};
    profile.modelExecutionSpec = *executionSpec;

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
