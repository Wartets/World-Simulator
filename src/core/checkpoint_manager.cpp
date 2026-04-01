#include "ws/core/checkpoint_manager.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ws {

CheckpointManager::CheckpointManager(CheckpointManagerConfig config)
    : config_(std::move(config)) {
}

void CheckpointManager::configure(CheckpointManagerConfig config) {
    config_ = std::move(config);
    if (config_.intervalSteps == 0u) {
        config_.intervalSteps = 1u;
    }
    pruneOldestIfNeeded();
}

void CheckpointManager::clear() {
    checkpointsByStep_.clear();
}

bool CheckpointManager::captureBaseline(Runtime& runtime, std::string& message) {
    return captureNow(runtime, "timeline_step_0", message);
}

bool CheckpointManager::captureIfDue(Runtime& runtime, std::string& message) {
    if (!config_.enabled) {
        message = "timeline_checkpoint_skip reason=disabled";
        return true;
    }

    if (config_.intervalSteps == 0u) {
        config_.intervalSteps = 1u;
    }

    const std::uint64_t step = runtime.snapshot().stateHeader.stepIndex;
    if (step % static_cast<std::uint64_t>(config_.intervalSteps) != 0u) {
        message = "timeline_checkpoint_skip reason=not_due step=" + std::to_string(step);
        return true;
    }

    if (checkpointsByStep_.contains(step)) {
        message = "timeline_checkpoint_skip reason=already_exists step=" + std::to_string(step);
        return true;
    }

    std::ostringstream label;
    label << "timeline_step_" << step;
    return captureNow(runtime, label.str(), message);
}

bool CheckpointManager::captureNow(Runtime& runtime, std::string label, std::string& message) {
    try {
        const RuntimeCheckpoint checkpoint = runtime.createCheckpoint(std::move(label), true /* computeHash */);
        const std::uint64_t step = checkpoint.stateSnapshot.header.stepIndex;
        checkpointsByStep_[step] = checkpoint;
        pruneOldestIfNeeded();

        message = "timeline_checkpoint_saved step=" + std::to_string(step) +
            " state_hash=" + std::to_string(checkpoint.stateSnapshot.stateHash);
        return true;
    } catch (const std::exception& exception) {
        message = std::string("timeline_checkpoint_failed error=") + exception.what();
        return false;
    }
}

std::vector<std::uint64_t> CheckpointManager::listSteps() const {
    std::vector<std::uint64_t> steps;
    steps.reserve(checkpointsByStep_.size());
    for (const auto& [step, _] : checkpointsByStep_) {
        steps.push_back(step);
    }
    return steps;
}

std::optional<std::uint64_t> CheckpointManager::nearestStepAtOrBefore(const std::uint64_t step) const {
    if (checkpointsByStep_.empty()) {
        return std::nullopt;
    }

    auto it = checkpointsByStep_.upper_bound(step);
    if (it == checkpointsByStep_.begin()) {
        return std::nullopt;
    }

    --it;
    return it->first;
}

bool CheckpointManager::seek(Runtime& runtime, const std::uint64_t targetStep, std::string& message) {
    try {
        const std::uint64_t currentStep = runtime.snapshot().stateHeader.stepIndex;
        if (targetStep == currentStep) {
            message = "time_seek_noop step=" + std::to_string(currentStep);
            return true;
        }

        if (targetStep > currentStep) {
            std::uint64_t remaining = targetStep - currentStep;
            while (remaining > 0u) {
                const std::uint32_t chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, 10000u));
                runtime.controlledStep(chunk);
                remaining -= chunk;
            }
            message = "time_seek_forward from=" + std::to_string(currentStep) + " to=" + std::to_string(targetStep);
            return true;
        }

        const auto nearest = nearestStepAtOrBefore(targetStep);
        if (!nearest.has_value()) {
            message = "time_seek_failed reason=no_checkpoint target=" + std::to_string(targetStep);
            return false;
        }

        const auto checkpointIt = checkpointsByStep_.find(*nearest);
        if (checkpointIt == checkpointsByStep_.end()) {
            message = "time_seek_failed reason=checkpoint_lookup_inconsistent";
            return false;
        }

        runtime.resetToCheckpoint(checkpointIt->second);
        if (targetStep > *nearest) {
            std::uint64_t remaining = targetStep - *nearest;
            while (remaining > 0u) {
                const std::uint32_t chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, 10000u));
                runtime.controlledStep(chunk);
                remaining -= chunk;
            }
        }

        message = "time_seek_backward from=" + std::to_string(currentStep) +
            " restored=" + std::to_string(*nearest) +
            " to=" + std::to_string(targetStep);
        return true;
    } catch (const std::exception& exception) {
        message = std::string("time_seek_failed error=") + exception.what();
        return false;
    }
}

void CheckpointManager::pruneOldestIfNeeded() {
    if (config_.maxInMemoryCheckpoints == 0u) {
        checkpointsByStep_.clear();
        return;
    }

    while (checkpointsByStep_.size() > config_.maxInMemoryCheckpoints) {
        checkpointsByStep_.erase(checkpointsByStep_.begin());
    }
}

} // namespace ws
