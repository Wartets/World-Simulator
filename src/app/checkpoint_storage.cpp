#include "ws/app/checkpoint_storage.hpp"

#include "ws/app/checkpoint_io.hpp"

#include <algorithm>
#include <system_error>

namespace ws::app {

CheckpointStorage::CheckpointStorage(CheckpointStoragePolicy policy)
    : policy_(std::move(policy)) {
}

void CheckpointStorage::configure(CheckpointStoragePolicy policy) {
    policy_ = std::move(policy);
    if (policy_.intervalSteps == 0u) {
        policy_.intervalSteps = 1u;
    }
    enforceRetention();
}

bool CheckpointStorage::store(const RuntimeCheckpoint& checkpoint, std::string& message) {
    if (!policy_.enabled) {
        message = "checkpoint_storage_skip reason=disabled";
        return true;
    }

    try {
        const std::uint64_t step = checkpoint.stateSnapshot.header.stepIndex;
        const auto path = pathForStep(step);
        writeCheckpointFile(checkpoint, path);
        stepToPath_[step] = path;
        enforceRetention();
        message = "checkpoint_storage_saved step=" + std::to_string(step) + " path=" + path.string();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("checkpoint_storage_failed error=") + exception.what();
        return false;
    }
}

bool CheckpointStorage::load(const std::uint64_t step, RuntimeCheckpoint& checkpoint, std::string& message) const {
    const auto it = stepToPath_.find(step);
    if (it == stepToPath_.end()) {
        message = "checkpoint_storage_load_failed reason=unknown_step step=" + std::to_string(step);
        return false;
    }

    try {
        checkpoint = readCheckpointFile(it->second);
        message = "checkpoint_storage_loaded step=" + std::to_string(step) + " path=" + it->second.string();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("checkpoint_storage_load_failed error=") + exception.what();
        return false;
    }
}

std::optional<std::uint64_t> CheckpointStorage::nearestStepAtOrBefore(const std::uint64_t step) const {
    if (stepToPath_.empty()) {
        return std::nullopt;
    }

    auto it = stepToPath_.upper_bound(step);
    if (it == stepToPath_.begin()) {
        return std::nullopt;
    }

    --it;
    return it->first;
}

std::vector<std::uint64_t> CheckpointStorage::listSteps() const {
    std::vector<std::uint64_t> steps;
    steps.reserve(stepToPath_.size());
    for (const auto& [step, _] : stepToPath_) {
        steps.push_back(step);
    }
    return steps;
}

void CheckpointStorage::clearIndex() {
    stepToPath_.clear();
}

void CheckpointStorage::enforceRetention() {
    if (policy_.maxRetainedFiles == 0u) {
        for (const auto& [_, path] : stepToPath_) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
        stepToPath_.clear();
        return;
    }

    while (stepToPath_.size() > policy_.maxRetainedFiles) {
        const auto it = stepToPath_.begin();
        std::error_code ec;
        std::filesystem::remove(it->second, ec);
        stepToPath_.erase(it);
    }
}

std::filesystem::path CheckpointStorage::pathForStep(const std::uint64_t step) const {
    return policy_.directory / ("step_" + std::to_string(step) + ".wscp");
}

} // namespace ws::app
