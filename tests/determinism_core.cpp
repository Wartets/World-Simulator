#include "ws/core/runtime.hpp"
#include "ws/core/subsystems/bootstrap_subsystem.hpp"

#include <cassert>
#include <memory>

namespace {

ws::ProfileResolverInput baselineProfileInput() {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = ws::ModelTier::A;
    }
    input.requestedSubsystemTiers["bootstrap"] = ws::ModelTier::A;
    input.compatibilityAssumptions = {
        "determinism_test_assumption",
        "deterministic_serial_scheduler"
    };
    return input;
}

ws::RuntimeSnapshot runOnce(const std::uint64_t seed) {
    ws::RuntimeConfig config;
    config.seed = seed;
    config.grid = ws::GridSpec{16, 16};
    config.profileInput = baselineProfileInput();

    ws::Runtime runtime(config);
    runtime.registerSubsystem(std::make_shared<ws::BootstrapSubsystem>());
    runtime.start();
    runtime.step();
    runtime.stop();
    return runtime.snapshot();
}

ws::RuntimeSnapshot runOnceWithInitConfig(
    const std::uint64_t seed,
    const ws::InitialConditionConfig& initialConditions) {
    ws::RuntimeConfig config;
    config.seed = seed;
    config.grid = ws::GridSpec{16, 16};
    config.profileInput = baselineProfileInput();
    config.initialConditions = initialConditions;

    ws::Runtime runtime(config);
    runtime.registerSubsystem(std::make_shared<ws::BootstrapSubsystem>());
    runtime.start();
    runtime.step();
    runtime.stop();
    return runtime.snapshot();
}

} // namespace

int main() {
    const ws::RuntimeSnapshot runA = runOnce(12345);
    const ws::RuntimeSnapshot runB = runOnce(12345);
    const ws::RuntimeSnapshot runC = runOnce(12346);

    ws::InitialConditionConfig conwayLow;
    conwayLow.type = ws::InitialConditionType::Conway;
    conwayLow.conway.targetVariable = "initialization.conway.target";
    conwayLow.conway.aliveProbability = 0.15f;

    ws::InitialConditionConfig conwayHigh = conwayLow;
    conwayHigh.conway.aliveProbability = 0.75f;

    const ws::RuntimeSnapshot initRunA = runOnceWithInitConfig(2026, conwayLow);
    const ws::RuntimeSnapshot initRunB = runOnceWithInitConfig(2026, conwayLow);
    const ws::RuntimeSnapshot initRunC = runOnceWithInitConfig(2026, conwayHigh);

    assert(runA.runSignature.identityHash() == runB.runSignature.identityHash());
    assert(runA.stateHash == runB.stateHash);

    assert(runA.runSignature.identityHash() != runC.runSignature.identityHash());
    assert(runA.stateHash != runC.stateHash);

    assert(initRunA.runSignature.identityHash() == initRunB.runSignature.identityHash());
    assert(initRunA.stateHash == initRunB.stateHash);

    assert(initRunA.runSignature.identityHash() != initRunC.runSignature.identityHash());
    assert(initRunA.stateHash != initRunC.stateHash);

    assert(runA.stateHeader.status == ws::RuntimeStatus::Terminated);
    assert(runA.stateHeader.stepIndex == 1);

    return 0;
}
