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

namespace {

std::string normalizeWorldName(std::string worldName) {
    worldName = app::trim(std::move(worldName));
    if (worldName.empty()) {
        return {};
    }

    for (char& ch : worldName) {
        const bool allowed =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-';
        if (!allowed) {
            ch = '_';
        }
    }
    return worldName;
}

} // namespace

RuntimeService::RuntimeService() = default;

std::filesystem::path RuntimeService::worldProfileRoot() {
    return std::filesystem::path("checkpoints") / "world_profiles";
}

std::filesystem::path RuntimeService::worldCheckpointRoot() {
    return std::filesystem::path("checkpoints") / "worlds";
}

std::filesystem::path RuntimeService::checkpointPathForWorld(const std::string& worldName) {
    return worldCheckpointRoot() / (worldName + ".wscp");
}

bool RuntimeService::isDefaultWorldName(const std::string& name, int& outIndex) {
    if (name.size() != 10 || !name.starts_with("world_")) {
        return false;
    }

    int value = 0;
    for (std::size_t i = 6; i < name.size(); ++i) {
        const char ch = name[i];
        if (ch < '0' || ch > '9') {
            return false;
        }
        value = (value * 10) + static_cast<int>(ch - '0');
    }
    outIndex = value;
    return true;
}

bool RuntimeService::isRunning() const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return runtime_ && runtime_->status() == RuntimeStatus::Running;
}

bool RuntimeService::isPaused() const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return isRunning() && runtime_->paused();
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
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        runtime_.reset();
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

bool RuntimeService::stop(std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        if (!isRunning()) {
            message = "runtime already stopped";
            runtime_.reset();
            return true;
        }

        runtime_->stop();
        std::ostringstream output;
        output << "runtime_stopped step_index=" << runtime_->snapshot().stateHeader.stepIndex;
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
        message = std::string("stop_failed error=") + exception.what();
        return false;
    }
}

std::vector<StoredWorldInfo> RuntimeService::listStoredWorlds(std::string& message) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<StoredWorldInfo> worlds;

    try {
        const auto names = worldProfileStore_.list();
        worlds.reserve(names.size());

        for (const auto& worldName : names) {
            StoredWorldInfo info;
            info.worldName = worldName;
            info.profilePath = worldProfileStore_.pathFor(worldName);
            info.checkpointPath = checkpointPathForWorld(worldName);
            info.hasProfile = std::filesystem::exists(info.profilePath);
            info.hasCheckpoint = std::filesystem::exists(info.checkpointPath);

            if (info.hasProfile) {
                info.profileBytes = std::filesystem::file_size(info.profilePath);
                info.profileLastWrite = std::filesystem::last_write_time(info.profilePath);
                info.hasProfileTimestamp = true;

                try {
                    const auto launch = worldProfileStore_.load(worldName);
                    info.gridWidth = launch.grid.width;
                    info.gridHeight = launch.grid.height;
                    info.seed = launch.seed;
                    info.tier = toString(launch.tier);
                    info.temporalPolicy = app::temporalPolicyToString(launch.temporalPolicy);
                } catch (...) {
                    // Keep list resilient when one profile entry is malformed.
                }
            }

            if (info.hasCheckpoint) {
                info.checkpointBytes = std::filesystem::file_size(info.checkpointPath);
                info.checkpointLastWrite = std::filesystem::last_write_time(info.checkpointPath);
                info.hasCheckpointTimestamp = true;
                const auto checkpoint = app::readCheckpointFile(info.checkpointPath);
                info.stepIndex = checkpoint.stateSnapshot.header.stepIndex;
                info.runIdentityHash = checkpoint.runSignature.identityHash();
            }
            worlds.push_back(std::move(info));
        }

        message = "world_list_ready count=" + std::to_string(worlds.size());
    } catch (const std::exception& exception) {
        message = std::string("world_list_failed error=") + exception.what();
        worlds.clear();
    }

    return worlds;
}

std::string RuntimeService::suggestNextWorldName() const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);

    const auto names = worldProfileStore_.list();
    int maxIndex = 0;
    for (const auto& name : names) {
        int value = 0;
        if (isDefaultWorldName(name, value)) {
            maxIndex = std::max(maxIndex, value);
        }
    }

    for (int i = maxIndex + 1; i < 100000; ++i) {
        std::ostringstream candidate;
        candidate << "world_" << std::setw(4) << std::setfill('0') << i;
        const std::string name = candidate.str();
        if (!std::filesystem::exists(worldProfileStore_.pathFor(name))) {
            return name;
        }
    }

    return "world_99999";
}

bool RuntimeService::createWorld(const std::string& worldName, const app::LaunchConfig& config, std::string& message) {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);

    const std::string normalized = normalizeWorldName(worldName);
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

    const std::string normalized = normalizeWorldName(worldName);
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

    const auto checkpointPath = checkpointPathForWorld(normalized);
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
        const auto checkpointPath = checkpointPathForWorld(activeWorldName_);
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

    const std::string normalized = normalizeWorldName(worldName);
    if (normalized.empty()) {
        message = "world name is required";
        return false;
    }

    const auto profilePath = worldProfileStore_.pathFor(normalized);
    const auto checkpointPath = checkpointPathForWorld(normalized);
    bool removedAny = false;

    try {
        if (std::filesystem::exists(profilePath)) {
            removedAny = std::filesystem::remove(profilePath) || removedAny;
        }
        if (std::filesystem::exists(checkpointPath)) {
            removedAny = std::filesystem::remove(checkpointPath) || removedAny;
        }

        const auto displayPath = worldCheckpointRoot() / (normalized + ".displayprefs");
        if (std::filesystem::exists(displayPath)) {
            removedAny = std::filesystem::remove(displayPath) || removedAny;
        }

        if (activeWorldName_ == normalized) {
            activeWorldName_.clear();
        }

        if (!removedAny) {
            message = "world_delete_noop name=" + normalized;
            return false;
        }

        message = "world_deleted name=" + normalized;
        return true;
    } catch (const std::exception& exception) {
        message = std::string("world_delete_failed error=") + exception.what();
        return false;
    }
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
        message = output.str();
        return true;
    } catch (const std::exception& exception) {
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
        message = "runtime paused";
        return true;
    } catch (const std::exception& exception) {
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
        message = "runtime resumed";
        return true;
    } catch (const std::exception& exception) {
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

