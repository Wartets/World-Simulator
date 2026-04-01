#pragma once

#include "ws/core/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ws {

struct CheckpointManagerConfig {
    bool enabled = true;
    std::uint32_t intervalSteps = 100;
    std::size_t maxInMemoryCheckpoints = 64;
};

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
    bool seek(Runtime& runtime, std::uint64_t targetStep, std::string& message);

private:
    void pruneOldestIfNeeded();

    CheckpointManagerConfig config_{};
    std::map<std::uint64_t, RuntimeCheckpoint> checkpointsByStep_;
};

} // namespace ws
