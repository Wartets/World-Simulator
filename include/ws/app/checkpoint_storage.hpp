#pragma once

#include "ws/core/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ws::app {

// =============================================================================
// Checkpoint Storage Policy
// =============================================================================

// Configuration for automatic checkpoint persistence.
struct CheckpointStoragePolicy {
    bool enabled = true;                           // Whether checkpoint storage is active.
    std::uint32_t intervalSteps = 100;             // Number of simulation steps between checkpoints.
    std::size_t maxRetainedFiles = 64;             // Maximum number of checkpoint files to retain.
    std::filesystem::path directory = std::filesystem::path("checkpoints") / "timeline";  // Storage directory.
};

// =============================================================================
// Checkpoint Storage
// =============================================================================

// Manages automatic storage and retrieval of simulation checkpoints.
class CheckpointStorage {
public:
    // Constructs a checkpoint storage with the given policy.
    explicit CheckpointStorage(CheckpointStoragePolicy policy = {});

    // Updates the storage policy.
    void configure(CheckpointStoragePolicy policy);
    // Returns the current storage policy.
    [[nodiscard]] const CheckpointStoragePolicy& policy() const noexcept { return policy_; }

    // Stores a checkpoint at the current simulation step.
    bool store(const RuntimeCheckpoint& checkpoint, std::string& message);
    // Loads the checkpoint nearest to the given step.
    bool load(std::uint64_t step, RuntimeCheckpoint& checkpoint, std::string& message) const;
    // Finds the nearest step that has a stored checkpoint at or before the given step.
    [[nodiscard]] std::optional<std::uint64_t> nearestStepAtOrBefore(std::uint64_t step) const;
    // Lists all stored checkpoint steps.
    [[nodiscard]] std::vector<std::uint64_t> listSteps() const;
    // Clears the checkpoint index.
    void clearIndex();

private:
    // Enforces the retention limit by removing old checkpoints.
    void enforceRetention();
    // Returns the file path for a checkpoint at a given step.
    [[nodiscard]] std::filesystem::path pathForStep(std::uint64_t step) const;

    CheckpointStoragePolicy policy_{};
    std::map<std::uint64_t, std::filesystem::path> stepToPath_;
};

} // namespace ws::app
