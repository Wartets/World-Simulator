#include "ws/core/checkpoint_manager.hpp"

#include "ws/core/determinism.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ws {

namespace {

std::uint64_t computeSnapshotStateHash(const StateStoreSnapshot& snapshot) {
    std::uint64_t hash = DeterministicHash::offsetBasis;
    hash = DeterministicHash::combine(hash, DeterministicHash::hashPod(snapshot.header.stepIndex));
    hash = DeterministicHash::combine(hash, DeterministicHash::hashPod(snapshot.header.timestampTicks));
    hash = DeterministicHash::combine(hash, static_cast<std::uint64_t>(snapshot.header.status));

    for (const auto& field : snapshot.fields) {
        hash = DeterministicHash::combine(hash, DeterministicHash::hashString(field.spec.name));
        for (const float value : field.values) {
            hash = DeterministicHash::combine(hash, DeterministicHash::hashPod(value));
        }
        for (const std::uint8_t validity : field.validityMask) {
            hash = DeterministicHash::combine(hash, DeterministicHash::hashPod(validity));
        }

        std::vector<std::pair<std::uint64_t, float>> sortedOverlay(field.sparseOverlay.begin(), field.sparseOverlay.end());
        std::sort(sortedOverlay.begin(), sortedOverlay.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });
        for (const auto& [index, value] : sortedOverlay) {
            hash = DeterministicHash::combine(hash, DeterministicHash::hashPod(index));
            hash = DeterministicHash::combine(hash, DeterministicHash::hashPod(value));
        }
    }

    return hash;
}

} // namespace

CheckpointManager::CheckpointManager(CheckpointManagerConfig config)
    : config_(std::move(config)) {
}

void CheckpointManager::configure(CheckpointManagerConfig config) {
    config_ = std::move(config);
    if (config_.intervalSteps == 0u) {
        config_.intervalSteps = 1u;
    }
    lastPersistedFieldByName_.clear();
    pruneOldestIfNeeded();
}

void CheckpointManager::clear() {
    checkpointsByStep_.clear();
    lastPersistedFieldByName_.clear();
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
        RuntimeCheckpoint checkpoint = runtime.createCheckpoint(std::move(label), true /* computeHash */);
        checkpoint.variableCheckpointIntervalSteps = config_.variableIntervalSteps;
        checkpoint.checkpointIncludeUnspecifiedVariables = config_.includeUnspecifiedVariables;
        applyVariableCadence(checkpoint);

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

std::optional<RuntimeCheckpoint> CheckpointManager::checkpointAtStep(const std::uint64_t step) const {
    const auto it = checkpointsByStep_.find(step);
    if (it == checkpointsByStep_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void CheckpointManager::applyVariableCadence(RuntimeCheckpoint& checkpoint) {
    auto& snapshot = checkpoint.stateSnapshot;
    const std::uint64_t step = snapshot.header.stepIndex;

    for (auto& field : snapshot.fields) {
        const auto policyIt = config_.variableIntervalSteps.find(field.spec.name);
        std::uint32_t interval = 1u;
        if (policyIt != config_.variableIntervalSteps.end()) {
            interval = policyIt->second;
        } else if (!config_.includeUnspecifiedVariables) {
            interval = 0u;
        }

        const bool due = (step == 0u) || (interval > 0u && (step % static_cast<std::uint64_t>(interval) == 0u));
        if (due) {
            lastPersistedFieldByName_[field.spec.name] = field;
            continue;
        }

        const auto cachedIt = lastPersistedFieldByName_.find(field.spec.name);
        if (cachedIt != lastPersistedFieldByName_.end()) {
            field = cachedIt->second;
        } else {
            lastPersistedFieldByName_[field.spec.name] = field;
        }
    }

    std::uint64_t payloadBytes = 0;
    for (const auto& field : snapshot.fields) {
        payloadBytes += static_cast<std::uint64_t>(field.values.size()) * sizeof(float);
        payloadBytes += static_cast<std::uint64_t>(field.validityMask.size()) * sizeof(std::uint8_t);
        payloadBytes += static_cast<std::uint64_t>(field.sparseOverlay.size()) * (sizeof(std::uint64_t) + sizeof(float));
    }
    snapshot.payloadBytes = payloadBytes;
    snapshot.stateHash = computeSnapshotStateHash(snapshot);
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
