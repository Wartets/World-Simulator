#pragma once

// Core dependencies
#include "ws/core/runtime.hpp"

// Standard library
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace ws {

// =============================================================================
// Replay Frame
// =============================================================================

// Single frame of input/event data for replay.
struct ReplayFrame {
    std::uint64_t stepIndex = 0;
    std::vector<RuntimeInputFrame> inputs;
    std::vector<RuntimeEvent> events;
};

// =============================================================================
// Replay Plan
// =============================================================================

// Complete plan for replaying a simulation run.
struct ReplayPlan {
    RuntimeCheckpoint checkpoint{};
    std::vector<ReplayFrame> frames;
    std::uint64_t stepCount = 0;
    std::uint64_t expectedFinalStateHash = 0;
};

// =============================================================================
// Replay Result
// =============================================================================

// Result of a replay execution for validation.
struct ReplayResult {
    bool runIdentityMatch = false;
    bool deterministicMatch = false;
    std::uint64_t replayStateHash = 0;
    std::vector<std::uint64_t> trajectoryStateHashes;
    std::optional<RuntimeSnapshot> finalSnapshot;
};

// =============================================================================
// Run Comparison
// =============================================================================

// Comparison between two run snapshots.
struct RunComparison {
    bool runIdentityEqual = false;
    bool stateHashEqual = false;
    std::int64_t stepIndexDelta = 0;
};

// =============================================================================
// Replay Runner
// =============================================================================

// Executes replay plans for determinism verification.
class ReplayRunner {
public:
    // Constructs a replay runner with the given configuration and subsystem factory.
    ReplayRunner(RuntimeConfig config, std::function<std::vector<std::shared_ptr<ISubsystem>>()> subsystemFactory);

    // Executes the replay plan and returns the result.
    [[nodiscard]] ReplayResult run(const ReplayPlan& plan) const;
    // Compares two runtime snapshots.
    [[nodiscard]] static RunComparison compareSnapshots(const RuntimeSnapshot& lhs, const RuntimeSnapshot& rhs);

private:
    RuntimeConfig config_;
    std::function<std::vector<std::shared_ptr<ISubsystem>>()> subsystemFactory_;
};

} // namespace ws
