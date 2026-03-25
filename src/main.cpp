#include "ws/core/runtime.hpp"
#include "ws/core/subsystems/bootstrap_subsystem.hpp"

#include <exception>
#include <iostream>
#include <memory>

namespace {

ws::ProfileResolverInput defaultProfileInput() {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = ws::ModelTier::A;
    }
    input.compatibilityAssumptions = {
        "baseline_low_coupling",
        "single_rate_temporal_admissible"
    };
    return input;
}

} // namespace

int main() {
    try {
        ws::RuntimeConfig config;
        config.seed = 42;
        config.grid = ws::GridSpec{32, 16};
        config.boundaryMode = ws::BoundaryMode::Clamp;
        config.unitRegime = ws::UnitRegime::Normalized;
        config.temporalPolicy = ws::TemporalPolicy::UniformA;
        config.profileInput = defaultProfileInput();

        ws::Runtime runtime(config);
        runtime.registerSubsystem(std::make_shared<ws::BootstrapSubsystem>());

        runtime.start();
        runtime.step();
        runtime.stop();

        const auto& snapshot = runtime.snapshot();
        std::cout << "run_identity_hash=" << snapshot.runSignature.identityHash() << '\n';
        std::cout << "state_hash=" << snapshot.stateHash << '\n';
        std::cout << "step_index=" << snapshot.stateHeader.stepIndex << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "runtime_error=" << exception.what() << '\n';
        return 1;
    }
}
