#include "ws/gui/runtime_service.hpp"

#include "ws/core/subsystems/subsystems.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ws::gui {

RuntimeService::RuntimeService() = default;

bool RuntimeService::isRunning() const {
    return runtime_ && runtime_->status() == RuntimeStatus::Running;
}

bool RuntimeService::isPaused() const {
    return isRunning() && runtime_->paused();
}

bool RuntimeService::start(std::string& message) {
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
    if (runtime_ && runtime_->status() == RuntimeStatus::Running) {
        std::string ignored;
        stop(ignored);
    }
    return start(message);
}

bool RuntimeService::stop(std::string& message) {
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
}

bool RuntimeService::step(const std::uint32_t count, std::string& message) {
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
}

bool RuntimeService::runUntil(const std::uint64_t targetStep, std::string& message) {
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
}

bool RuntimeService::pause(std::string& message) {
    if (!requireRuntime("pause", message)) {
        return false;
    }
    runtime_->pause();
    message = "runtime paused";
    return true;
}

bool RuntimeService::resume(std::string& message) {
    if (!requireRuntime("resume", message)) {
        return false;
    }
    runtime_->resume();
    message = "runtime resumed";
    return true;
}

bool RuntimeService::status(std::string& message) const {
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
}

bool RuntimeService::metrics(std::string& message) const {
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
}

bool RuntimeService::listFields(std::string& message) const {
    if (!requireRuntime("fields", message)) {
        return false;
    }

    const auto checkpoint = runtime_->createCheckpoint("fields_probe");
    std::ostringstream output;
    output << "fields count=" << checkpoint.stateSnapshot.fields.size();
    for (const auto& field : checkpoint.stateSnapshot.fields) {
        output << "\n  - " << field.spec.name;
    }
    message = output.str();
    return true;
}

bool RuntimeService::summarizeField(const std::string& variableName, std::string& message) const {
    if (!requireRuntime("summary", message)) {
        return false;
    }
    if (variableName.empty()) {
        message = "summary requires a variable name";
        return false;
    }

    const auto checkpoint = runtime_->createCheckpoint("summary_probe");
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
}

bool RuntimeService::createCheckpoint(const std::string& label, std::string& message) {
    if (!requireRuntime("checkpoint", message)) {
        return false;
    }
    if (label.empty()) {
        message = "checkpoint label is required";
        return false;
    }

    checkpoints_[label] = runtime_->createCheckpoint(label);
    std::ostringstream output;
    output << "checkpoint_saved=" << label
           << " step_index=" << runtime_->snapshot().stateHeader.stepIndex;
    message = output.str();
    return true;
}

bool RuntimeService::restoreCheckpoint(const std::string& label, std::string& message) {
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
}

bool RuntimeService::listCheckpoints(std::string& message) const {
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
    if (!runtime_ || runtime_->status() != RuntimeStatus::Running) {
        message = std::string("runtime is not running; operation unavailable: ") + operation;
        return false;
    }
    return true;
}

} // namespace ws::gui
