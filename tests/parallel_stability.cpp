#include "ws/core/interactions.hpp"
#include "ws/core/runtime.hpp"
#include "ws/core/scheduler.hpp"
#include "ws/core/subsystems/subsystems.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace {

class IndependentWriteSubsystem final : public ws::ISubsystem {
public:
    IndependentWriteSubsystem(std::string subsystemName, std::string outputVariable, float scale)
        : subsystemName_(std::move(subsystemName)),
          outputVariable_(std::move(outputVariable)),
          scale_(scale) {}

    [[nodiscard]] std::string name() const override {
        return subsystemName_;
    }

    [[nodiscard]] std::vector<std::string> declaredReadSet() const override {
        return {"seed_probe"};
    }

    [[nodiscard]] std::vector<std::string> declaredWriteSet() const override {
        return {outputVariable_};
    }

    void initialize(const ws::StateStore&, ws::StateStore::WriteSession& writeSession, const ws::ModelProfile&) override {
        writeSession.fillScalar(outputVariable_, 0.0f);
    }

    void step(const ws::StateStore& stateStore, ws::StateStore::WriteSession& writeSession, const ws::ModelProfile&, std::uint64_t stepIndex) override {
        for (std::uint32_t y = 0; y < stateStore.grid().height; ++y) {
            for (std::uint32_t x = 0; x < stateStore.grid().width; ++x) {
                const auto sample = stateStore.trySampleScalar("seed_probe", ws::CellSigned{static_cast<std::int64_t>(x), static_cast<std::int64_t>(y)});
                const float seedValue = sample.value_or(0.0f);
                const float value = scale_ * seedValue + static_cast<float>(stepIndex) * 0.01f;
                writeSession.setScalar(outputVariable_, ws::Cell{x, y}, value);
            }
        }
    }

private:
    std::string subsystemName_;
    std::string outputVariable_;
    float scale_ = 1.0f;
};

void primeSeedProbe(ws::StateStore& stateStore) {
    ws::StateStore::WriteSession seedWriter(stateStore, "seed_init", {"seed_probe"});
    for (std::uint32_t y = 0; y < stateStore.grid().height; ++y) {
        for (std::uint32_t x = 0; x < stateStore.grid().width; ++x) {
            const float value = static_cast<float>(x + y * stateStore.grid().width) * 0.001f;
            seedWriter.setScalar("seed_probe", ws::Cell{x, y}, value);
        }
    }
}

std::uint64_t runDeterministicReducedOnce() {
    ws::StateStore stateStore(ws::GridSpec{8, 8});
    stateStore.allocateScalarField(ws::VariableSpec{1, "seed_probe"});
    stateStore.allocateScalarField(ws::VariableSpec{2, "alpha_out"});
    stateStore.allocateScalarField(ws::VariableSpec{3, "beta_out"});
    primeSeedProbe(stateStore);

    ws::Scheduler scheduler;
    scheduler.setExecutionPolicyMode(ws::ExecutionPolicyMode::DeterministicReduced);
    scheduler.registerSubsystem(std::make_shared<IndependentWriteSubsystem>("alpha", "alpha_out", 1.0f));
    scheduler.registerSubsystem(std::make_shared<IndependentWriteSubsystem>("beta", "beta_out", 2.0f));

    ws::ModelProfile profile;
    profile.subsystemTiers["alpha"] = ws::ModelTier::Minimal;
    profile.subsystemTiers["beta"] = ws::ModelTier::Minimal;
    profile.subsystemTiers["temporal"] = ws::ModelTier::Minimal;
    profile.compatibilityAssumptions = {"parallel_independent_subsystems"};

    ws::InteractionCoordinator coordinator;
    const ws::AdmissionReport report = coordinator.buildAdmissionReport(
        profile,
        ws::TemporalPolicy::UniformA,
        scheduler.registeredSubsystems());
    assert(report.admitted);

    scheduler.setAdmissionReport(report);
    scheduler.initialize(stateStore, profile);

    ws::NumericGuardrailPolicy policy;
    policy.clampMin = -10000.0f;
    policy.clampMax = 10000.0f;
    policy.maxAbsDeltaPerStep = 10000.0f;

    const ws::StepDiagnostics diagnostics = scheduler.step(
        stateStore,
        profile,
        ws::TemporalPolicy::UniformA,
        policy,
        0);

    assert(diagnostics.executionPolicyMode == ws::ExecutionPolicyMode::DeterministicReduced);
    assert(diagnostics.parallelBatchesExecuted >= 1);
    assert(diagnostics.parallelTasksDispatched == 2);

    return stateStore.stateHash();
}

ws::ProfileResolverInput baselineProfileInput() {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = ws::ModelTier::Minimal;
    }
    input.compatibilityAssumptions = {
        "throughput_mode_recovery_validation",
        "checkpoint_reset_contract"
    };
    return input;
}

void verifyThroughputCheckpointRecovery() {
    ws::RuntimeConfig config;
    config.seed = 20260325;
    config.grid = ws::GridSpec{6, 6};
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    config.executionPolicyMode = ws::ExecutionPolicyMode::ThroughputPriority;
    config.profileInput = baselineProfileInput();

    ws::Runtime runtime(config);
    for (const auto& subsystem : ws::makePhase4Subsystems()) {
        runtime.registerSubsystem(subsystem);
    }

    runtime.start();
    runtime.step();
    runtime.step();
    runtime.step();

    const ws::RuntimeCheckpoint checkpoint = runtime.createCheckpoint("throughput_recovery_anchor");

    runtime.step();
    runtime.step();
    runtime.step();
    runtime.step();
    const std::uint64_t referenceHash = runtime.snapshot().stateHash;

    runtime.resetToCheckpoint(checkpoint);
    runtime.step();
    runtime.step();
    runtime.step();
    runtime.step();

    assert(runtime.snapshot().stateHash == referenceHash);
    runtime.stop();
}

} // namespace

int main() {
    const std::uint64_t hashA = runDeterministicReducedOnce();
    const std::uint64_t hashB = runDeterministicReducedOnce();
    assert(hashA == hashB);

    verifyThroughputCheckpointRecovery();
    return 0;
}
