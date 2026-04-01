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

struct CheckpointStoragePolicy {
    bool enabled = true;
    std::uint32_t intervalSteps = 100;
    std::size_t maxRetainedFiles = 64;
    std::filesystem::path directory = std::filesystem::path("checkpoints") / "timeline";
};

class CheckpointStorage {
public:
    explicit CheckpointStorage(CheckpointStoragePolicy policy = {});

    void configure(CheckpointStoragePolicy policy);
    [[nodiscard]] const CheckpointStoragePolicy& policy() const noexcept { return policy_; }

    bool store(const RuntimeCheckpoint& checkpoint, std::string& message);
    bool load(std::uint64_t step, RuntimeCheckpoint& checkpoint, std::string& message) const;
    [[nodiscard]] std::optional<std::uint64_t> nearestStepAtOrBefore(std::uint64_t step) const;
    [[nodiscard]] std::vector<std::uint64_t> listSteps() const;
    void clearIndex();

private:
    void enforceRetention();
    [[nodiscard]] std::filesystem::path pathForStep(std::uint64_t step) const;

    CheckpointStoragePolicy policy_{};
    std::map<std::uint64_t, std::filesystem::path> stepToPath_;
};

} // namespace ws::app
