#include "ws/core/replay.hpp"

#include <map>
#include <stdexcept>
#include <utility>

namespace ws {

// Constructs ReplayRunner with configuration and subsystem factory.
ReplayRunner::ReplayRunner(RuntimeConfig config, std::function<std::vector<std::shared_ptr<ISubsystem>>()> subsystemFactory)
    : config_(std::move(config)),
      subsystemFactory_(std::move(subsystemFactory)) {
    if (!subsystemFactory_) {
        throw std::invalid_argument("ReplayRunner requires a non-empty subsystem factory");
    }
}

// Runs simulation replay according to the provided plan.
// Validates checkpoint compatibility and steps through replay frames.
ReplayResult ReplayRunner::run(const ReplayPlan& plan) const {
    Runtime runtime(config_);
    for (auto& subsystem : subsystemFactory_()) {
        runtime.registerSubsystem(subsystem);
    }

    runtime.start();
    runtime.resetToCheckpoint(plan.checkpoint);

    std::map<std::uint64_t, ReplayFrame> frameByStep;
    for (const auto& frame : plan.frames) {
        if (frameByStep.contains(frame.stepIndex)) {
            throw std::invalid_argument("ReplayPlan contains duplicate frame step index");
        }
        frameByStep.emplace(frame.stepIndex, frame);
    }

    ReplayResult result;
    result.runIdentityMatch = runtime.snapshot().runSignature.identityHash() == plan.checkpoint.runSignature.identityHash();

    for (std::uint64_t i = 0; i < plan.stepCount; ++i) {
        const std::uint64_t activeStep = runtime.snapshot().stateHeader.stepIndex;
        const auto frameIt = frameByStep.find(activeStep);
        if (frameIt != frameByStep.end()) {
            for (const auto& input : frameIt->second.inputs) {
                runtime.queueInput(input);
            }
            for (const auto& event : frameIt->second.events) {
                runtime.enqueueEvent(event);
            }
        }

        runtime.controlledStep(1);
        result.trajectoryStateHashes.push_back(runtime.snapshot().stateHash);
    }

    result.replayStateHash = runtime.snapshot().stateHash;
    result.finalSnapshot = runtime.snapshot();
    result.deterministicMatch = (plan.expectedFinalStateHash == 0)
        ? true
        : result.replayStateHash == plan.expectedFinalStateHash;

    runtime.stop();
    return result;
}

RunComparison ReplayRunner::compareSnapshots(const RuntimeSnapshot& lhs, const RuntimeSnapshot& rhs) {
    return RunComparison{
        lhs.runSignature.identityHash() == rhs.runSignature.identityHash(),
        lhs.stateHash == rhs.stateHash,
        static_cast<std::int64_t>(lhs.stateHeader.stepIndex) - static_cast<std::int64_t>(rhs.stateHeader.stepIndex)};
}

} // namespace ws
