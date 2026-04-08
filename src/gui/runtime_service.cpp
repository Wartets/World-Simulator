#include "ws/gui/runtime_service.hpp"

#include "ws/app/checkpoint_io.hpp"
#include "ws/core/initialization_binding.hpp"
#include "ws/core/subsystems/subsystems.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace ws::gui {

namespace {

// Checks if subsystem key can be resolved given available variables and aliases.
// Returns true if key is empty, found in variableIds, resolves via alias, or is internal.
// @param key Subsystem key to check
// @param variableIds Set of available variable IDs
// @param aliases Map of alias to canonical variable names
// @return true if key can be resolved
bool canResolveSubsystemKey(
    const std::string& key,
    const std::unordered_set<std::string>& variableIds,
    const std::unordered_map<std::string, std::string>& aliases) {
    if (key.empty()) {
        return true;
    }
    if (variableIds.contains(key)) {
        return true;
    }

    const auto aliasIt = aliases.find(key);
    if (aliasIt != aliases.end() && variableIds.contains(aliasIt->second)) {
        return true;
    }

    // Internal runtime-owned writes (without semantic namespace) are allowed.
    return key.find('.') == std::string::npos;
}

// Selects subsystems compatible with model's variable requirements.
// Returns all subsystems if no execution spec, otherwise filters by variable needs.
// @param executionSpec Optional model execution specification
// @return Vector of compatible subsystem instances
// Selects subsystems compatible with model's variable requirements.
// Returns all subsystems if no execution spec, otherwise filters by variable needs.
// @param executionSpec Optional model execution specification
// @return Vector of compatible subsystem instances
std::vector<std::shared_ptr<ISubsystem>> selectCompatibleSubsystems(const std::optional<ModelExecutionSpec>& executionSpec) {
    const auto all = makePhase4Subsystems();
    if (!executionSpec.has_value()) {
        return all;
    }

    const std::unordered_set<std::string> variableIds(
        executionSpec->cellScalarVariableIds.begin(),
        executionSpec->cellScalarVariableIds.end());

    std::vector<std::shared_ptr<ISubsystem>> selected;
    selected.reserve(all.size());
    for (const auto& subsystem : all) {
        if (!subsystem) {
            continue;
        }

        bool compatible = true;
        for (const auto& readKey : subsystem->declaredReadSet()) {
            if (!canResolveSubsystemKey(readKey, variableIds, executionSpec->semanticFieldAliases)) {
                compatible = false;
                break;
            }
        }
        if (!compatible) {
            continue;
        }
        for (const auto& writeKey : subsystem->declaredWriteSet()) {
            if (!canResolveSubsystemKey(writeKey, variableIds, executionSpec->semanticFieldAliases)) {
                compatible = false;
                break;
            }
        }
        if (compatible) {
            selected.push_back(subsystem);
        }
    }

    return selected;
}

} // namespace

RuntimeService::RuntimeService() = default;

void RuntimeService::setModelScope(ModelScopeContext context) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    modelScope_ = std::move(context);
}

std::filesystem::path RuntimeService::worldProfileRoot() {
    return std::filesystem::path("checkpoints") / "world_profiles";
}

std::filesystem::path RuntimeService::worldCheckpointRoot() {
    return std::filesystem::path("checkpoints") / "worlds";
}

std::string RuntimeService::currentModelKey() const {
    const auto pick = [](const std::string& value) -> std::string {
        const auto trimmed = app::trim(value);
        return trimmed;
    };

    if (!pick(modelScope_.modelId).empty()) {
        return pick(modelScope_.modelId);
    }
    if (!pick(modelScope_.modelIdentityHash).empty()) {
        return pick(modelScope_.modelIdentityHash);
    }
    if (!pick(modelScope_.modelName).empty()) {
        return pick(modelScope_.modelName);
    }
    if (!pick(modelScope_.modelPath).empty()) {
        return pick(modelScope_.modelPath);
    }
    return "default";
}

std::string RuntimeService::activeModelKey() const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return currentModelKey();
}

app::WorldModelMetadata RuntimeService::currentWorldModelMetadata() const {
    app::WorldModelMetadata metadata;
    metadata.modelKey = currentModelKey();
    metadata.modelId = app::trim(modelScope_.modelId);
    metadata.modelName = app::trim(modelScope_.modelName);
    metadata.modelPath = app::trim(modelScope_.modelPath);
    metadata.modelIdentityHash = app::trim(modelScope_.modelIdentityHash);
    return metadata;
}

void RuntimeService::refreshCachedStateNoLock() const {
    const bool running = runtime_ && runtime_->status() == RuntimeStatus::Running;
    const bool paused = running && runtime_->paused();
    cachedRunning_.store(running, std::memory_order_relaxed);
    cachedPaused_.store(paused, std::memory_order_relaxed);
}

bool RuntimeService::isRunning() const {
    if (mutex_.try_lock()) {
        const std::lock_guard<std::recursive_mutex> lock(mutex_, std::adopt_lock);
        refreshCachedStateNoLock();
        return cachedRunning_.load(std::memory_order_relaxed);
    }
    return cachedRunning_.load(std::memory_order_relaxed);
}

bool RuntimeService::isPaused() const {
    if (mutex_.try_lock()) {
        const std::lock_guard<std::recursive_mutex> lock(mutex_, std::adopt_lock);
        refreshCachedStateNoLock();
        return cachedPaused_.load(std::memory_order_relaxed);
    }
    return cachedPaused_.load(std::memory_order_relaxed);
}

bool RuntimeService::start(std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    checkpoints_.clear();

    try {
        auto runtimeConfig = app::makeRuntimeConfig(config_);
        if (!modelScope_.modelPath.empty()) {
            ModelExecutionSpec modelExecutionSpec;
            std::string executionSpecMessage;
            if (initialization::loadModelExecutionSpec(
                    std::filesystem::path(modelScope_.modelPath),
                    modelExecutionSpec,
                    executionSpecMessage)) {
                if (!modelExecutionSpec.conservedVariables.empty()) {
                    runtimeConfig.profileInput.conservedVariables = modelExecutionSpec.conservedVariables;
                }
                if (modelExecutionSpec.preferredBoundaryMode.has_value()) {
                    runtimeConfig.boundaryMode = *modelExecutionSpec.preferredBoundaryMode;
                }
                runtimeConfig.modelExecutionSpec = std::move(modelExecutionSpec);
            }

                ModelDisplaySpec modelDisplaySpec;
                std::string displaySpecMessage;
                if (initialization::loadModelDisplaySpec(
                        std::filesystem::path(modelScope_.modelPath),
                        modelDisplaySpec,
                        displaySpecMessage)) {
                    runtimeConfig.modelDisplaySpec = modelDisplaySpec;
                    activeFieldDisplayTags_ = std::move(modelDisplaySpec.fieldTags);
                } else {
                    activeFieldDisplayTags_.clear();
                }

            std::vector<ParameterControl> modelParameterControls;
            std::string parameterControlMessage;
            if (initialization::loadModelParameterControls(
                    std::filesystem::path(modelScope_.modelPath),
                    modelParameterControls,
                    parameterControlMessage)) {
                runtimeConfig.modelParameterControls = std::move(modelParameterControls);
            }
        }

        const auto compatibleSubsystems = selectCompatibleSubsystems(runtimeConfig.modelExecutionSpec);

        auto runtime = std::make_unique<Runtime>(std::move(runtimeConfig));
        for (const auto& subsystem : compatibleSubsystems) {
            runtime->registerSubsystem(subsystem);
        }
        runtime->start();
        runtime_ = std::move(runtime);
        checkpointManager_.clear();
        checkpointStorage_.clearIndex();

        std::string checkpointMessage;
        captureTimelineCheckpointNoLock("start", checkpointMessage);

        const auto& snapshot = runtime_->snapshot();
        std::ostringstream output;
        output << "session_started run_identity_hash=" << snapshot.runSignature.identityHash()
               << " grid=" << config_.grid.width << 'x' << config_.grid.height
               << " tier=" << toString(config_.tier)
               << " temporal=" << app::temporalPolicyToString(config_.temporalPolicy);
        refreshCachedStateNoLock();
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        runtime_.reset();
        refreshCachedStateNoLock();
        message = std::string("start_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::restart(std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (runtime_ && runtime_->status() == RuntimeStatus::Running) {
        std::string ignored;
        stop(ignored);
    }
    return start(message);
}

bool RuntimeService::applySettings(std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!runtime_ || runtime_->status() != RuntimeStatus::Running) {
        message = "apply_settings_failed runtime_not_active";
        return false;
    }
    
    try {
        std::string restartMessage;
        if (!restart(restartMessage)) {
            message = std::string("apply_settings_failed restart_error=") + restartMessage;
            return false;
        }

        message = "settings_applied " + restartMessage;
        return true;
    } catch (const std::exception& exception) {
        message = std::string("apply_settings_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::stop(std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!isRunning()) {
            message = "runtime already stopped";
            runtime_.reset();
            refreshCachedStateNoLock();
            return true;
        }

        runtime_->stop();
        std::ostringstream output;
        output << "runtime_stopped step_index=" << runtime_->snapshot().stateHeader.stepIndex;
        refreshCachedStateNoLock();
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        refreshCachedStateNoLock();
        message = std::string("stop_failed error=") + exception.what();
        return false;
    }
}

std::vector<StoredWorldInfo> RuntimeService::listStoredWorlds(std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string modelKey = currentModelKey();
    return listStoredWorldsForModel(modelKey, message);
}

std::vector<StoredWorldInfo> RuntimeService::listStoredWorldsForModel(const std::string& modelKey, std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<StoredWorldInfo> worlds;

    const auto records = worldStore_.list(modelKey, message);
    worlds.reserve(records.size());
    for (const auto& record : records) {
        StoredWorldInfo info;
        info.modelKey = record.modelKey;
        info.worldName = record.worldName;
        info.profilePath = record.profilePath;
        info.checkpointPath = record.checkpointPath;
        info.displayPrefsPath = record.displayPrefsPath;
        info.hasProfile = record.hasProfile;
        info.hasCheckpoint = record.hasCheckpoint;
        info.hasDisplayPrefs = record.hasDisplayPrefs;
        info.gridWidth = record.gridWidth;
        info.gridHeight = record.gridHeight;
        info.seed = record.seed;
        info.temporalPolicy = record.temporalPolicy;
        info.initialConditionMode = record.initialConditionMode;
        info.profileBytes = record.profileBytes;
        info.checkpointBytes = record.checkpointBytes;
        info.displayPrefsBytes = record.displayPrefsBytes;
        info.profileLastWrite = record.profileLastWrite;
        info.checkpointLastWrite = record.checkpointLastWrite;
        info.displayPrefsLastWrite = record.displayPrefsLastWrite;
        info.hasProfileTimestamp = record.hasProfileTimestamp;
        info.hasCheckpointTimestamp = record.hasCheckpointTimestamp;
        info.hasDisplayPrefsTimestamp = record.hasDisplayPrefsTimestamp;
        info.stepIndex = record.stepIndex;
        info.runIdentityHash = record.runIdentityHash;
        info.profileUsesFallback = record.profileUsesFallback;
        info.checkpointUsesFallback = record.checkpointUsesFallback;
        info.displayPrefsUsesFallback = record.displayPrefsUsesFallback;
        worlds.push_back(std::move(info));
    }

    return worlds;
}

std::string RuntimeService::suggestNextWorldName() const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return worldStore_.suggestNextWorldName(currentModelKey());
}

std::string RuntimeService::suggestWorldNameFromHint(const std::string& hint) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return worldStore_.suggestWorldNameFromHint(hint, currentModelKey());
}

std::string RuntimeService::normalizeWorldNameForUi(const std::string& worldName) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return worldStore_.normalizeNameForUi(worldName);
}

bool RuntimeService::createWorld(const std::string& worldName, const app::LaunchConfig& config, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);

    const std::string normalized = app::trim(worldName);
    if (normalized.empty()) {
        message = "world name is required";
        return false;
    }

    config_ = config;
    if (!start(message)) {
        return false;
    }

    activeWorldName_ = normalized;
    const std::string modelKey = currentModelKey();
    if (!saveActiveWorld(message)) {
        return false;
    }

    std::ostringstream output;
    output << "world_created name=" << activeWorldName_
           << " grid=" << config_.grid.width << 'x' << config_.grid.height
           << " seed=" << config_.seed;
    message = output.str();
    return true;
}

bool RuntimeService::openWorld(const std::string& worldName, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);

    const std::string normalized = app::trim(worldName);
    if (normalized.empty()) {
        message = "world name is required";
        return false;
    }

    try {
        config_ = worldProfileStore_.load(normalized, currentModelKey());
    } catch (const std::exception& exception) {
        message = std::string("world_open_failed error=") + exception.what();
        return false;
    }

    if (!start(message)) {
        return false;
    }

    const auto checkpointPath = worldStore_.checkpointPathFor(normalized, currentModelKey());
    if (std::filesystem::exists(checkpointPath)) {
        try {
            const auto checkpoint = app::readCheckpointFile(checkpointPath);
            runtime_->resetToCheckpoint(checkpoint);
            checkpointManager_.clear();
            checkpointStorage_.clearIndex();
            checkpointManager_.captureNow(*runtime_, "timeline_world_open", message);
            checkpointStorage_.store(checkpoint, message);
        } catch (const std::exception& exception) {
            message = std::string("world_open_failed checkpoint_restore_error=") + exception.what();
            return false;
        }
    } else {
        checkpointManager_.clear();
        checkpointStorage_.clearIndex();
        std::string checkpointMessage;
        captureTimelineCheckpointNoLock("world_open_profile", checkpointMessage);
    }

    activeWorldName_ = normalized;
    std::ostringstream output;
    output << "world_opened name=" << activeWorldName_;
    if (std::filesystem::exists(checkpointPath)) {
        output << " source=checkpoint";
    } else {
        output << " source=profile";
    }
    message = output.str();
    return true;
}

bool RuntimeService::saveActiveWorld(std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (activeWorldName_.empty()) {
        message = "no active world selected";
        return false;
    }
    if (!runtime_ || runtime_->status() != RuntimeStatus::Running) {
        message = "world save requires an active running runtime";
        return false;
    }

    try {
        const std::string modelKey = currentModelKey();
        worldProfileStore_.save(activeWorldName_, config_, modelKey);
        const auto checkpoint = runtime_->createCheckpoint(activeWorldName_, true /* computeHash */);
        const auto checkpointPath = worldStore_.writeCheckpointPathFor(activeWorldName_, modelKey);
        app::writeCheckpointFile(checkpoint, checkpointPath);
        std::ostringstream output;
        output << "world_saved name=" << activeWorldName_
               << " step=" << checkpoint.stateSnapshot.header.stepIndex;
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("world_save_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::deleteWorld(const std::string& worldName, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);

    const bool ok = worldStore_.erase(worldName, currentModelKey(), message);
    if (ok && activeWorldName_ == app::trim(worldName)) {
        activeWorldName_.clear();
    }
    return ok;
}

bool RuntimeService::renameWorld(const std::string& fromWorldName, const std::string& toWorldName, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    const bool ok = worldStore_.rename(fromWorldName, toWorldName, currentModelKey(), message);
    if (ok && activeWorldName_ == app::trim(fromWorldName)) {
        activeWorldName_ = app::trim(toWorldName);
    }
    return ok;
}

bool RuntimeService::duplicateWorld(const std::string& fromWorldName, const std::string& toWorldName, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return worldStore_.duplicate(fromWorldName, toWorldName, currentModelKey(), message);
}

bool RuntimeService::exportWorld(const std::string& worldName, const std::filesystem::path& outputPath, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto modelMetadata = currentWorldModelMetadata();
    return worldStore_.exportWorld(worldName, outputPath, modelMetadata.modelKey, modelMetadata, message);
}

bool RuntimeService::importWorld(const std::filesystem::path& inputPath, std::string& importedWorldName, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto modelMetadata = currentWorldModelMetadata();
    return worldStore_.importWorld(inputPath, modelMetadata.modelKey, modelMetadata, importedWorldName, message);
}

bool RuntimeService::step(const std::uint32_t count, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("step", message)) {
            return false;
        }
        if (count == 0) {
            message = "step count must be positive";
            return false;
        }

        runtime_->controlledStep(count);
        std::string checkpointMessage;
        captureTimelineCheckpointNoLock("step", checkpointMessage);
        const auto& snapshot = runtime_->snapshot();
        const auto& diagnostics = runtime_->lastStepDiagnostics();

        std::ostringstream output;
        output << "stepped=" << count
               << " step_index=" << snapshot.stateHeader.stepIndex
               << " state_hash=" << snapshot.stateHash
               << " events_applied=" << diagnostics.eventsApplied
               << " reproducibility=" << toString(snapshot.reproducibilityClass);
        refreshCachedStateNoLock();
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        refreshCachedStateNoLock();
        message = std::string("step_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::stepBackward(const std::uint32_t count, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("step_backward", message)) {
            return false;
        }
        if (count == 0u) {
            message = "step_backward_failed reason=zero_count";
            return false;
        }

        const std::uint64_t currentStep = runtime_->snapshot().stateHeader.stepIndex;
        const std::uint64_t targetStep = (count >= currentStep) ? 0u : (currentStep - count);
        return seekStep(targetStep, message);
    } catch (const std::exception& exception) {
        message = std::string("step_backward_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::runUntil(const std::uint64_t targetStep, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("rununtil", message)) {
            return false;
        }

        const auto current = runtime_->snapshot().stateHeader.stepIndex;
        if (targetStep <= current) {
            std::ostringstream output;
            output << "rununtil_noop current_step=" << current << " target_step=" << targetStep;
            message = output.str();
            return true;
        }

        std::uint64_t remaining = targetStep - current;
        while (remaining > 0) {
            const std::uint32_t chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, 10000));
            runtime_->controlledStep(chunk);
            remaining -= chunk;
            std::string checkpointMessage;
            captureTimelineCheckpointNoLock("run_until", checkpointMessage);
        }

        const auto& snapshot = runtime_->snapshot();
        std::ostringstream output;
        output << "rununtil_complete step_index=" << snapshot.stateHeader.stepIndex
               << " state_hash=" << snapshot.stateHash;
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("rununtil_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::seekStep(const std::uint64_t targetStep, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("seek", message)) {
            return false;
        }

        const std::uint64_t currentStep = runtime_->snapshot().stateHeader.stepIndex;
        if (targetStep == currentStep) {
            message = "seek_noop step=" + std::to_string(currentStep);
            return true;
        }

        if (targetStep > currentStep) {
            std::uint64_t remaining = targetStep - currentStep;
            while (remaining > 0u) {
                const auto chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, 10000u));
                runtime_->controlledStep(chunk);
                remaining -= chunk;
                std::string checkpointMessage;
                captureTimelineCheckpointNoLock("seek_forward", checkpointMessage);
            }
            message = "seek_complete direction=forward step=" + std::to_string(targetStep);
            return true;
        }

        std::string seekMessage;
        if (checkpointManager_.seek(*runtime_, targetStep, seekMessage)) {
            message = "seek_complete direction=backward step=" + std::to_string(targetStep) + " detail=" + seekMessage;
            return true;
        }

        const auto nearestDiskStep = checkpointStorage_.nearestStepAtOrBefore(targetStep);
        if (!nearestDiskStep.has_value()) {
            message = "seek_failed reason=no_checkpoint_for_target target=" + std::to_string(targetStep);
            return false;
        }

        RuntimeCheckpoint checkpoint;
        std::string loadMessage;
        if (!checkpointStorage_.load(*nearestDiskStep, checkpoint, loadMessage)) {
            message = loadMessage;
            return false;
        }

        runtime_->resetToCheckpoint(checkpoint);
        checkpointManager_.captureNow(*runtime_, "timeline_seek_restore", loadMessage);

        std::uint64_t remaining = targetStep - *nearestDiskStep;
        while (remaining > 0u) {
            const auto chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, 10000u));
            runtime_->controlledStep(chunk);
            remaining -= chunk;
            std::string checkpointMessage;
            captureTimelineCheckpointNoLock("seek_restore_replay", checkpointMessage);
        }

        message = "seek_complete direction=backward step=" + std::to_string(targetStep) +
            " restored_step=" + std::to_string(*nearestDiskStep);
        return true;
    } catch (const std::exception& exception) {
        message = std::string("seek_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::pause(std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("pause", message)) {
            return false;
        }
        runtime_->pause();
        refreshCachedStateNoLock();
        message = "runtime paused";
        return true;
    } catch (const std::exception& exception) {
        refreshCachedStateNoLock();
        message = std::string("pause_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::resume(std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("resume", message)) {
            return false;
        }
        runtime_->resume();
        refreshCachedStateNoLock();
        message = "runtime resumed";
        return true;
    } catch (const std::exception& exception) {
        refreshCachedStateNoLock();
        message = std::string("resume_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::setPlaybackSpeed(const float speed, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (speed < 0.1f || speed > 8.0f) {
        message = "playback_speed_failed reason=out_of_range min=0.1 max=8.0";
        return false;
    }

    playbackSpeed_ = speed;
    std::ostringstream output;
    output << std::fixed << std::setprecision(2)
           << "playback_speed_updated value=" << playbackSpeed_;
    message = output.str();
    return true;
}

bool RuntimeService::configureCheckpointTimeline(
    const std::uint32_t intervalSteps,
    const std::size_t retention,
    std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (intervalSteps == 0u) {
        message = "timeline_config_failed reason=interval_zero";
        return false;
    }

    checkpointManager_.configure(CheckpointManagerConfig{
        true,
        intervalSteps,
        std::max<std::size_t>(retention, 1u)});
    checkpointStorage_.configure(app::CheckpointStoragePolicy{
        true,
        intervalSteps,
        std::max<std::size_t>(retention, 1u),
        std::filesystem::path("checkpoints") / "timeline"});

    message = "timeline_configured interval_steps=" + std::to_string(intervalSteps) +
        " retention=" + std::to_string(std::max<std::size_t>(retention, 1u));
    return true;
}

bool RuntimeService::status(std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("status", message)) {
            return false;
        }

        const auto& snapshot = runtime_->snapshot();
        const auto& diagnostics = runtime_->lastStepDiagnostics();

        std::ostringstream output;
        output << "status=running"
               << " paused=" << (runtime_->paused() ? "yes" : "no")
               << " step_index=" << snapshot.stateHeader.stepIndex
               << " state_hash=" << snapshot.stateHash
               << " run_identity_hash=" << snapshot.runSignature.identityHash()
               << " reproducibility=" << toString(snapshot.reproducibilityClass)
               << " constraints=" << diagnostics.constraintViolations.size()
               << " stability_alerts=" << diagnostics.stabilityAlerts.size();
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("status_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::metrics(std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("metrics", message)) {
            return false;
        }

        const auto metrics = runtime_->metrics();
        std::ostringstream output;
        output << "metrics"
               << " steps_executed=" << metrics.stepsExecuted
               << " events_applied=" << metrics.eventsApplied
               << " events_queued=" << metrics.eventsQueued
               << " input_patches=" << metrics.inputPatches
               << " checkpoints_created=" << metrics.checkpointsCreated
               << " checkpoints_loaded=" << metrics.checkpointsLoaded;
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("metrics_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::listFields(std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("fields", message)) {
            return false;
        }

        const auto checkpoint = runtime_->createCheckpoint("fields_probe", false /* computeHash */);
        std::ostringstream output;
        output << "fields count=" << checkpoint.stateSnapshot.fields.size();
        for (const auto& field : checkpoint.stateSnapshot.fields) {
            output << "\n  - " << field.spec.name;
        }
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("fields_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::summarizeField(const std::string& variableName, std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("summary", message)) {
            return false;
        }
        if (variableName.empty()) {
            message = "summary requires a variable name";
            return false;
        }

        const auto checkpoint = runtime_->createCheckpoint("summary_probe", false /* computeHash */);
        const auto& fields = checkpoint.stateSnapshot.fields;
        const auto it = std::find_if(fields.begin(), fields.end(), [&](const auto& field) {
            return field.spec.name == variableName;
        });
        if (it == fields.end()) {
            message = "unknown variable: " + variableName;
            return false;
        }

        const auto summary = app::summarizeField(*it);
        std::ostringstream output;
        output << std::fixed << std::setprecision(6)
               << "summary variable=" << variableName
               << " valid_count=" << summary.validCount
               << " invalid_count=" << summary.invalidCount
               << " min=" << summary.minValue
               << " max=" << summary.maxValue
               << " avg=" << summary.average;
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("summary_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::captureCheckpoint(RuntimeCheckpoint& checkpoint, std::string& message, const bool computeHash) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("visualization", message)) {
            return false;
        }

        checkpoint = runtime_->createCheckpoint("visualization_probe", computeHash);
        message = "visualization_snapshot_ready";
        return true;
    } catch (const std::exception& exception) {
        message = std::string("visualization_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::fieldNames(std::vector<std::string>& names, std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("fieldnames", message)) {
            return false;
        }

        const auto checkpoint = runtime_->createCheckpoint("field_name_probe", false /* computeHash */);
        names.clear();
        names.reserve(checkpoint.stateSnapshot.fields.size());
        for (const auto& field : checkpoint.stateSnapshot.fields) {
            names.push_back(field.spec.name);
        }
        message = "field_names_ready";
        return true;
    } catch (const std::exception& exception) {
        message = std::string("field_names_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::fieldDisplayTags(
    std::unordered_map<std::string, std::vector<std::string>>& tags,
    std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!runtime_) {
        message = "field_display_tags_failed runtime_not_active";
        return false;
    }

    tags = activeFieldDisplayTags_;
    message = "field_display_tags_ready count=" + std::to_string(tags.size());
    return true;
}

bool RuntimeService::parameterControls(std::vector<ParameterControl>& controls, std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("parameter_controls", message)) {
            return false;
        }

        controls = runtime_->parameterControls();
        message = "parameter_controls_ready count=" + std::to_string(controls.size());
        return true;
    } catch (const std::exception& exception) {
        message = std::string("parameter_controls_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::addProbe(const ProbeDefinition& definition, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("add_probe", message)) {
            return false;
        }
        return runtime_->addProbe(definition, message);
    } catch (const std::exception& exception) {
        message = std::string("add_probe_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::removeProbe(const std::string& probeId, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("remove_probe", message)) {
            return false;
        }
        return runtime_->removeProbe(probeId, message);
    } catch (const std::exception& exception) {
        message = std::string("remove_probe_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::clearProbes(std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("clear_probes", message)) {
            return false;
        }
        runtime_->clearProbes();
        message = "probes_cleared";
        return true;
    } catch (const std::exception& exception) {
        message = std::string("clear_probes_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::probeDefinitions(std::vector<ProbeDefinition>& definitions, std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("probe_definitions", message)) {
            return false;
        }
        definitions = runtime_->probes().definitions();
        message = "probe_definitions_ready count=" + std::to_string(definitions.size());
        return true;
    } catch (const std::exception& exception) {
        message = std::string("probe_definitions_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::probeSeries(const std::string& probeId, ProbeSeries& series, std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("probe_series", message)) {
            return false;
        }
        return runtime_->probes().getSeries(probeId, series, message);
    } catch (const std::exception& exception) {
        message = std::string("probe_series_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::lastStepDiagnostics(StepDiagnostics& diagnostics, std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("step_diagnostics", message)) {
            return false;
        }
        diagnostics = runtime_->lastStepDiagnostics();
        message = "step_diagnostics_ready";
        return true;
    } catch (const std::exception& exception) {
        message = std::string("step_diagnostics_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::setParameterValue(const std::string& parameterName, const float value, const std::string& note, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("set_parameter", message)) {
            return false;
        }
        return runtime_->setParameterValue(parameterName, value, note, message);
    } catch (const std::exception& exception) {
        message = std::string("set_parameter_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::applyManualPatch(
    const std::string& variableName,
    const std::optional<Cell> cell,
    const float newValue,
    const std::string& note,
    std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("manual_patch", message)) {
            return false;
        }
        return runtime_->applyManualPatch(variableName, cell, newValue, note, message);
    } catch (const std::exception& exception) {
        message = std::string("manual_patch_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::undoLastManualPatch(std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("manual_patch_undo", message)) {
            return false;
        }
        return runtime_->undoLastManualPatch(message);
    } catch (const std::exception& exception) {
        message = std::string("manual_patch_undo_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::enqueuePerturbation(const PerturbationSpec& perturbation, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("enqueue_perturbation", message)) {
            return false;
        }
        return runtime_->enqueuePerturbation(perturbation, message);
    } catch (const std::exception& exception) {
        message = std::string("enqueue_perturbation_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::manualEventLog(std::vector<ManualEventRecord>& events, std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("manual_event_log", message)) {
            return false;
        }
        events = runtime_->manualEventLog();
        message = "manual_event_log_ready count=" + std::to_string(events.size());
        return true;
    } catch (const std::exception& exception) {
        message = std::string("manual_event_log_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::effectLedgerCounts(
    std::size_t& pendingImmediateWrites,
    std::size_t& queuedDeferredEvents,
    std::size_t& pendingScheduledPerturbations,
    std::size_t& runtimeManualEventCount,
    std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("effect_ledger_counts", message)) {
            return false;
        }

        pendingImmediateWrites = runtime_->pendingInputPatchCount();
        queuedDeferredEvents = runtime_->pendingEventCount();
        pendingScheduledPerturbations = runtime_->pendingPerturbationCount();
        runtimeManualEventCount = runtime_->manualEventCount();

        message = "effect_ledger_counts_ready immediate_writes=" + std::to_string(pendingImmediateWrites) +
            " queued_events=" + std::to_string(queuedDeferredEvents) +
            " scheduled_perturbations=" + std::to_string(pendingScheduledPerturbations) +
            " manual_events=" + std::to_string(runtimeManualEventCount);
        return true;
    } catch (const std::exception& exception) {
        message = std::string("effect_ledger_counts_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::timelineCheckpointSteps(std::vector<std::uint64_t>& steps, std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("timeline_checkpoint_steps", message)) {
            return false;
        }
        steps = checkpointStorage_.listSteps();
        message = "timeline_checkpoint_steps_ready count=" + std::to_string(steps.size());
        return true;
    } catch (const std::exception& exception) {
        message = std::string("timeline_checkpoint_steps_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::checkpointRecords(std::vector<CheckpointInfo>& records, std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("checkpoint_records", message)) {
            return false;
        }

        records.clear();
        records.reserve(checkpoints_.size());
        for (const auto& [label, checkpoint] : checkpoints_) {
            records.push_back(CheckpointInfo{
                label,
                checkpoint.stateSnapshot.header.stepIndex,
                checkpoint.stateSnapshot.header.timestampTicks,
                checkpoint.stateSnapshot.stateHash,
                checkpoint.stateSnapshot.payloadBytes,
                checkpoint.runSignature.identityHash(),
                checkpoint.profileFingerprint});
        }

        std::stable_sort(records.begin(), records.end(), [](const CheckpointInfo& lhs, const CheckpointInfo& rhs) {
            if (lhs.stepIndex != rhs.stepIndex) {
                return lhs.stepIndex > rhs.stepIndex;
            }
            if (lhs.timestampTicks != rhs.timestampTicks) {
                return lhs.timestampTicks > rhs.timestampTicks;
            }
            return lhs.label < rhs.label;
        });

        message = "checkpoint_records_ready count=" + std::to_string(records.size());
        return true;
    } catch (const std::exception& exception) {
        message = std::string("checkpoint_records_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::createCheckpoint(const std::string& label, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("checkpoint", message)) {
            return false;
        }
        if (label.empty()) {
            message = "checkpoint label is required";
            return false;
        }

        checkpoints_[label] = runtime_->createCheckpoint(label, true /* computeHash */);
        std::ostringstream output;
        output << "checkpoint_saved=" << label
               << " step_index=" << runtime_->snapshot().stateHeader.stepIndex;
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("checkpoint_save_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::restoreCheckpoint(const std::string& label, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("restore", message)) {
            return false;
        }

        const auto it = checkpoints_.find(label);
        if (it == checkpoints_.end()) {
            message = "unknown checkpoint label: " + label;
            return false;
        }

        runtime_->resetToCheckpoint(it->second);
        std::ostringstream output;
        output << "checkpoint_restored=" << label
               << " step_index=" << runtime_->snapshot().stateHeader.stepIndex;
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("checkpoint_restore_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::deleteCheckpoint(const std::string& label, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("checkpoint_delete", message)) {
            return false;
        }

        const auto erased = checkpoints_.erase(label);
        if (erased == 0u) {
            message = "unknown checkpoint label: " + label;
            return false;
        }

        message = "checkpoint_deleted label=" + label;
        return true;
    } catch (const std::exception& exception) {
        message = std::string("checkpoint_delete_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::renameCheckpoint(const std::string& fromLabel, const std::string& toLabel, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!requireRuntime("checkpoint_rename", message)) {
            return false;
        }

        if (fromLabel.empty() || toLabel.empty()) {
            message = "checkpoint rename requires source and target labels";
            return false;
        }
        if (fromLabel == toLabel) {
            message = "checkpoint rename target matches source";
            return false;
        }

        const auto sourceIt = checkpoints_.find(fromLabel);
        if (sourceIt == checkpoints_.end()) {
            message = "unknown checkpoint label: " + fromLabel;
            return false;
        }
        if (checkpoints_.contains(toLabel)) {
            message = "checkpoint label already exists: " + toLabel;
            return false;
        }

        RuntimeCheckpoint checkpoint = sourceIt->second;
        checkpoint.stateSnapshot.checkpointLabel = toLabel;
        checkpoints_.erase(sourceIt);
        checkpoints_.emplace(toLabel, std::move(checkpoint));
        message = "checkpoint_renamed from=" + fromLabel + " to=" + toLabel;
        return true;
    } catch (const std::exception& exception) {
        message = std::string("checkpoint_rename_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::listCheckpoints(std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<CheckpointInfo> records;
    if (!checkpointRecords(records, message)) {
        return false;
    }
    if (records.empty()) {
        message = "checkpoints=empty";
        return true;
    }

    std::ostringstream output;
    for (const auto& record : records) {
        output << "checkpoint label=" << record.label
               << " step_index=" << record.stepIndex
               << " timestamp_ticks=" << record.timestampTicks
               << " state_hash=" << record.stateHash
               << " payload_bytes=" << record.payloadBytes
               << '\n';
    }

    message = output.str();
    return true;
}

bool RuntimeService::saveProfile(const std::string& name, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (name.empty()) {
        message = "profile name is required";
        return false;
    }

    try {
        profileStore_.save(name, config_);
        message = "profile_saved name=" + name + " path=" + profileStore_.pathFor(name).string();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("profile_save_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::loadProfile(const std::string& name, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (name.empty()) {
        message = "profile name is required";
        return false;
    }

    try {
        config_ = profileStore_.load(name);
        std::ostringstream output;
        output << "profile_loaded name=" << name
               << " seed=" << config_.seed
               << " grid=" << config_.grid.width << 'x' << config_.grid.height
               << " tier=" << toString(config_.tier)
               << " temporal=" << app::temporalPolicyToString(config_.temporalPolicy);
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("profile_load_failed error=") + exception.what();
        return false;
    }
}

bool RuntimeService::listProfiles(std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto names = profileStore_.list();
    if (names.empty()) {
        message = "profiles=empty";
        return true;
    }

    std::ostringstream output;
    output << "profiles:";
    for (const auto& name : names) {
        output << "\n  - " << name;
    }
    message = output.str();
    return true;
}

bool RuntimeService::requireRuntime(const char* operation, std::string& message) const {
    if (!runtime_ || runtime_->status() == RuntimeStatus::Created || runtime_->status() == RuntimeStatus::Error) {
        message = std::string("runtime is not ready; operation unavailable: ") + operation;
        return false;
    }
    return true;
}

bool RuntimeService::captureTimelineCheckpointNoLock(const char* context, std::string& message) {
    if (!runtime_ || runtime_->status() != RuntimeStatus::Running) {
        message = std::string("timeline_capture_skip reason=runtime_inactive context=") + context;
        return true;
    }

    const auto interval = checkpointManager_.config().intervalSteps;
    const std::uint64_t step = runtime_->snapshot().stateHeader.stepIndex;
    if (interval == 0u || (step % static_cast<std::uint64_t>(interval)) != 0u) {
        message = std::string("timeline_capture_skip reason=not_due context=") + context;
        return true;
    }

    std::string managerMessage;
    if (!checkpointManager_.captureNow(*runtime_, "timeline_step_" + std::to_string(step), managerMessage)) {
        message = managerMessage;
        return false;
    }

    const auto diskCheckpoint = runtime_->createCheckpoint("timeline_step_" + std::to_string(step), true /* computeHash */);
    std::string storageMessage;
    if (!checkpointStorage_.store(diskCheckpoint, storageMessage)) {
        message = storageMessage;
        return false;
    }

    message = std::string("timeline_capture_ok context=") + context +
        " step=" + std::to_string(step);
    return true;
}

} // namespace ws::gui
