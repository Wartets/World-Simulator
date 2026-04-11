#include "ws/app/shell_support.hpp"
#include "ws/core/runtime.hpp"
#include "ws/core/subsystems/bootstrap_subsystem.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace {

ws::RuntimeConfig baselineRuntimeConfig() {
    ws::RuntimeConfig config;
    config.seed = 20260411ULL;
    config.grid = ws::GridSpec{24, 16};
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    config.timeIntegratorId = "explicit_euler";
    config.initialConditions.type = ws::InitialConditionType::Blank;
    config.profileInput.requestedSubsystemTiers["bootstrap"] = ws::ModelTier::A;
    config.profileInput.compatibilityAssumptions = {
        "runtime_integrator_switch_test"
    };
    return config;
}

void registerDefaultSubsystems(ws::Runtime& runtime) {
    runtime.registerSubsystem(std::make_shared<ws::BootstrapSubsystem>());
}

void testPreStartIntegratorSelection() {
    ws::Runtime runtime(baselineRuntimeConfig());

    std::string message;
    assert(runtime.setTimeIntegratorId("Verlet", message));
    assert(runtime.timeIntegratorId() == "velocity_verlet");

    registerDefaultSubsystems(runtime);
    runtime.start();
    assert(runtime.snapshot().runSignature.timeIntegratorId() == "velocity_verlet");
    runtime.stop();
}

void testLiveIntegratorSwitching() {
    ws::Runtime runtime(baselineRuntimeConfig());
    registerDefaultSubsystems(runtime);
    runtime.start();

    const std::uint64_t baselineIdentity = runtime.snapshot().runSignature.identityHash();

    std::string message;
    assert(runtime.setTimeIntegratorId("RK4", message));
    assert(runtime.timeIntegratorId() == "rk4");
    assert(runtime.snapshot().runSignature.timeIntegratorId() == "rk4");
    assert(runtime.snapshot().runSignature.identityHash() != baselineIdentity);

    runtime.controlledStep(1);

    assert(runtime.setTimeIntegratorId("Semi-Implicit Euler", message));
    assert(runtime.timeIntegratorId() == "semi_implicit_euler");

    assert(!runtime.setTimeIntegratorId("definitely_not_an_integrator", message));

    runtime.stop();
}

} // namespace

int main() {
    testPreStartIntegratorSelection();
    testLiveIntegratorSwitching();
    return 0;
}
