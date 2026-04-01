#include "ws/app/checkpoint_storage.hpp"
#include "ws/core/checkpoint_manager.hpp"
#include "ws/core/replay_engine.hpp"
#include "ws/core/runtime.hpp"
#include "ws/core/subsystems/bootstrap_subsystem.hpp"

#include <cassert>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace {

ws::ProfileResolverInput baselineProfileInput() {
    ws::ProfileResolverInput input;
    for (const auto& subsystem : ws::ProfileResolver::requiredSubsystems()) {
        input.requestedSubsystemTiers[subsystem] = ws::ModelTier::A;
    }
    input.requestedSubsystemTiers["bootstrap"] = ws::ModelTier::A;
    input.compatibilityAssumptions = {
        "phase8_time_control_scrubbing",
        "deterministic_seek_replay"
    };
    return input;
}

ws::Runtime makeRuntime() {
    ws::RuntimeConfig config;
    config.seed = 20260401;
    config.grid = ws::GridSpec{10, 10};
    config.temporalPolicy = ws::TemporalPolicy::UniformA;
    config.profileInput = baselineProfileInput();

    ws::Runtime runtime(config);
    runtime.registerSubsystem(std::make_shared<ws::BootstrapSubsystem>());
    runtime.start();
    runtime.pause();
    return runtime;
}

void verifyCheckpointManagerSeekDeterminism() {
    ws::Runtime runtime = makeRuntime();
    ws::CheckpointManager manager(ws::CheckpointManagerConfig{true, 5u, 128u});

    std::string message;
    assert(manager.captureBaseline(runtime, message));

    std::unordered_map<std::uint64_t, std::uint64_t> hashesByStep;
    hashesByStep[runtime.snapshot().stateHeader.stepIndex] = runtime.snapshot().stateHash;

    for (int i = 0; i < 30; ++i) {
        runtime.controlledStep(1);
        hashesByStep[runtime.snapshot().stateHeader.stepIndex] = runtime.snapshot().stateHash;
        assert(manager.captureIfDue(runtime, message));
    }

    assert(manager.seek(runtime, 12u, message));
    assert(runtime.snapshot().stateHeader.stepIndex == 12u);

    assert(manager.seek(runtime, 23u, message));
    assert(runtime.snapshot().stateHeader.stepIndex == 23u);
    assert(runtime.snapshot().stateHash == hashesByStep[23u]);

    assert(manager.seek(runtime, 0u, message));
    assert(runtime.snapshot().stateHeader.stepIndex == 0u);
    assert(runtime.snapshot().stateHash == hashesByStep[0u]);
}

void verifyReplayEngineAndStorage() {
    ws::Runtime runtime = makeRuntime();

    ws::app::CheckpointStorage storage(ws::app::CheckpointStoragePolicy{
        true,
        5u,
        16u,
        std::filesystem::path("build") / "phase8_time_control"});

    std::string message;
    ws::RuntimeCheckpoint step10Checkpoint;
    for (int i = 0; i < 15; ++i) {
        runtime.controlledStep(1);
        if (runtime.snapshot().stateHeader.stepIndex == 10u) {
            step10Checkpoint = runtime.createCheckpoint("phase8_step10", true);
            assert(storage.store(step10Checkpoint, message));
        }
    }

    const std::uint64_t finalHash = runtime.snapshot().stateHash;

    ws::RuntimeCheckpoint loaded;
    assert(storage.load(10u, loaded, message));

    const auto replayResult = ws::ReplayEngine::replayToStep(
        runtime,
        loaded,
        15u,
        finalHash,
        message);

    assert(replayResult.success);
    assert(replayResult.deterministicMatch);
    assert(replayResult.finalStateHash == finalHash);

    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path("build") / "phase8_time_control", ec);
}

} // namespace

int main() {
    verifyCheckpointManagerSeekDeterminism();
    verifyReplayEngineAndStorage();
    return 0;
}
