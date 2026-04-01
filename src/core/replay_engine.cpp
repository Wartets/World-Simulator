#include "ws/core/replay_engine.hpp"

#include <algorithm>
#include <exception>

namespace ws {

ReplayExecutionResult ReplayEngine::replayToStep(
    Runtime& runtime,
    const RuntimeCheckpoint& checkpoint,
    const std::uint64_t targetStep,
    const std::optional<std::uint64_t> expectedStateHash,
    std::string& message) {
    ReplayExecutionResult result;

    try {
        runtime.resetToCheckpoint(checkpoint);

        const std::uint64_t startStep = runtime.snapshot().stateHeader.stepIndex;
        if (targetStep < startStep) {
            message = "replay_failed reason=target_before_checkpoint target_step=" + std::to_string(targetStep) +
                " checkpoint_step=" + std::to_string(startStep);
            return result;
        }

        std::uint64_t remaining = targetStep - startStep;
        while (remaining > 0u) {
            const std::uint32_t chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, 10000u));
            runtime.controlledStep(chunk);
            result.trajectoryStateHashes.push_back(runtime.snapshot().stateHash);
            remaining -= chunk;
        }

        result.success = true;
        result.finalStateHash = runtime.snapshot().stateHash;
        result.deterministicMatch = expectedStateHash.has_value()
            ? (result.finalStateHash == *expectedStateHash)
            : true;
        message = "replay_complete step=" + std::to_string(runtime.snapshot().stateHeader.stepIndex) +
            " state_hash=" + std::to_string(result.finalStateHash);
        return result;
    } catch (const std::exception& exception) {
        message = std::string("replay_failed error=") + exception.what();
        return result;
    }
}

} // namespace ws
