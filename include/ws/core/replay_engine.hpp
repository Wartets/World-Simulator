#pragma once

#include "ws/core/runtime.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ws {

// =============================================================================
// Replay Execution Result
// =============================================================================

// Result of a replay execution including success status and determinism verification.
struct ReplayExecutionResult {
    bool success = false;                        // Whether replay completed successfully.
    bool deterministicMatch = false;              // Whether state hashes match expected.
    std::uint64_t finalStateHash = 0;            // Hash of final state after replay.
    std::vector<std::uint64_t> trajectoryStateHashes;  // Hash at each step of the trajectory.
};

// =============================================================================
// Replay Engine
// =============================================================================

// Replays a simulation from a checkpoint to a target step and optionally
// verifies determinism against expected state hashes.
class ReplayEngine {
public:
    // Replays the simulation from checkpoint to target step.
    static ReplayExecutionResult replayToStep(
        Runtime& runtime,
        const RuntimeCheckpoint& checkpoint,
        std::uint64_t targetStep,
        std::optional<std::uint64_t> expectedStateHash,
        std::string& message);
};

} // namespace ws
