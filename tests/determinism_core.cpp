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

} // namespace

int main() {
    const ws::RuntimeSnapshot runA = runOnce(12345);
    const ws::RuntimeSnapshot runB = runOnce(12345);
    const ws::RuntimeSnapshot runC = runOnce(12346);

    assert(runA.runSignature.identityHash() == runB.runSignature.identityHash());
    assert(runA.stateHash == runB.stateHash);

    assert(runA.runSignature.identityHash() != runC.runSignature.identityHash());
    assert(runA.stateHash != runC.stateHash);

    assert(runA.stateHeader.status == ws::RuntimeStatus::Terminated);
    assert(runA.stateHeader.stepIndex == 1);

    return 0;
}
