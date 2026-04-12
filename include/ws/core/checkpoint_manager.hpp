#pragma once

#include "ws/core/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ws {

// Configuration for checkpoint management behavior.
struct CheckpointManagerConfig {
    bool enabled = true;
    std::uint32_t intervalSteps = 100;
    std::size_t maxInMemoryCheckpoints = 64;
    std::unordered_map<std::string, std::uint32_t> variableIntervalSteps;
    bool includeUnspecifiedVariables = true;
};

// Manages automatic and manual checkpoints during simulation.
// Controls capture intervals, retention limits, and state restoration.
class CheckpointManager {
public:
    explicit CheckpointManager(CheckpointManagerConfig config = {});

    void configure(CheckpointManagerConfig config);
    [[nodiscard]] const CheckpointManagerConfig& config() const noexcept { return config_; }

    void clear();
    bool captureBaseline(Runtime& runtime, std::string& message);
    bool captureIfDue(Runtime& runtime, std::string& message);
    bool captureNow(Runtime& runtime, std::string label, std::string& message);
    [[nodiscard]] std::vector<std::uint64_t> listSteps() const;
    [[nodiscard]] std::optional<std::uint64_t> nearestStepAtOrBefore(std::uint64_t step) const;
    [[nodiscard]] std::optional<RuntimeCheckpoint> checkpointAtStep(std::uint64_t step) const;
    bool seek(Runtime& runtime, std::uint64_t targetStep, std::string& message);

private:
    void applyVariableCadence(RuntimeCheckpoint& checkpoint);
    void pruneOldestIfNeeded();

    CheckpointManagerConfig config_{};
    std::map<std::uint64_t, RuntimeCheckpoint> checkpointsByStep_;
    std::unordered_map<std::string, StateStoreSnapshot::FieldPayload> lastPersistedFieldByName_;
};

} // namespace ws
