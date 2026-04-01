#include "ws/gui/runtime_service.hpp"

#include "ws/app/checkpoint_io.hpp"
#include "ws/core/subsystems/subsystems.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace ws::gui {

RuntimeService::RuntimeService() = default;

std::filesystem::path RuntimeService::worldProfileRoot() {
    return std::filesystem::path("checkpoints") / "world_profiles";
}

std::filesystem::path RuntimeService::worldCheckpointRoot() {
    return std::filesystem::path("checkpoints") / "worlds";
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
        auto runtime = std::make_unique<Runtime>(app::makeRuntimeConfig(config_));
        for (const auto& subsystem : makePhase4Subsystems()) {
            runtime->registerSubsystem(subsystem);
        }
        runtime->start();
        runtime_ = std::move(runtime);

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
    std::vector<StoredWorldInfo> worlds;

    const auto records = worldStore_.list(message);
    worlds.reserve(records.size());
    for (const auto& record : records) {
        StoredWorldInfo info;
        info.worldName = record.worldName;
        info.profilePath = record.profilePath;
        info.checkpointPath = record.checkpointPath;
        info.hasProfile = record.hasProfile;
        info.hasCheckpoint = record.hasCheckpoint;
        info.gridWidth = record.gridWidth;
        info.gridHeight = record.gridHeight;
        info.seed = record.seed;
        info.tier = record.tier;
        info.temporalPolicy = record.temporalPolicy;
        info.profileBytes = record.profileBytes;
        info.checkpointBytes = record.checkpointBytes;
        info.profileLastWrite = record.profileLastWrite;
        info.checkpointLastWrite = record.checkpointLastWrite;
        info.hasProfileTimestamp = record.hasProfileTimestamp;
        info.hasCheckpointTimestamp = record.hasCheckpointTimestamp;
        info.stepIndex = record.stepIndex;
        info.runIdentityHash = record.runIdentityHash;
        worlds.push_back(std::move(info));
    }

    return worlds;
}

std::string RuntimeService::suggestNextWorldName() const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return worldStore_.suggestNextWorldName();
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
        config_ = worldProfileStore_.load(normalized);
    } catch (const std::exception& exception) {
        message = std::string("world_open_failed error=") + exception.what();
        return false;
    }

    if (!start(message)) {
        return false;
    }

    const auto checkpointPath = worldStore_.checkpointPathFor(normalized);
    if (std::filesystem::exists(checkpointPath)) {
        try {
            const auto checkpoint = app::readCheckpointFile(checkpointPath);
            runtime_->resetToCheckpoint(checkpoint);
        } catch (const std::exception& exception) {
            message = std::string("world_open_failed checkpoint_restore_error=") + exception.what();
            return false;
        }
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
        worldProfileStore_.save(activeWorldName_, config_);
        const auto checkpoint = runtime_->createCheckpoint(activeWorldName_, true /* computeHash */);
        const auto checkpointPath = worldStore_.checkpointPathFor(activeWorldName_);
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

    const bool ok = worldStore_.erase(worldName, message);
    if (ok && activeWorldName_ == app::trim(worldName)) {
        activeWorldName_.clear();
    }
    return ok;
}

bool RuntimeService::renameWorld(const std::string& fromWorldName, const std::string& toWorldName, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    const bool ok = worldStore_.rename(fromWorldName, toWorldName, message);
    if (ok && activeWorldName_ == app::trim(fromWorldName)) {
        activeWorldName_ = app::trim(toWorldName);
    }
    return ok;
}

bool RuntimeService::duplicateWorld(const std::string& fromWorldName, const std::string& toWorldName, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return worldStore_.duplicate(fromWorldName, toWorldName, message);
}

bool RuntimeService::exportWorld(const std::string& worldName, const std::filesystem::path& outputPath, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return worldStore_.exportWorld(worldName, outputPath, message);
}

bool RuntimeService::importWorld(const std::filesystem::path& inputPath, std::string& importedWorldName, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return worldStore_.importWorld(inputPath, importedWorldName, message);
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

bool RuntimeService::listCheckpoints(std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (checkpoints_.empty()) {
        message = "checkpoints=empty";
        return true;
    }

    std::ostringstream output;
    for (const auto& [label, checkpoint] : checkpoints_) {
        output << "checkpoint label=" << label
               << " step_index=" << checkpoint.stateSnapshot.header.stepIndex
               << " state_hash=" << checkpoint.stateSnapshot.stateHash
               << " payload_bytes=" << checkpoint.stateSnapshot.payloadBytes
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

} // namespace ws::gui
