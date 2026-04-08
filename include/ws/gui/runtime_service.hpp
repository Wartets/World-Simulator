#pragma once

#include "ws/app/world_store.hpp"
#include "ws/app/profile_store.hpp"
#include "ws/app/checkpoint_storage.hpp"
#include "ws/app/shell_support.hpp"
#include "ws/core/checkpoint_manager.hpp"
#include "ws/core/runtime.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ws::gui {

// Context information about the currently loaded model.
struct ModelScopeContext {
    std::string modelId;
    std::string modelName;
    std::string modelPath;
    std::string modelIdentityHash;
};

// Information about a stored world (profile/checkpoint).
struct StoredWorldInfo {
    std::string modelKey;
    std::string worldName;
    std::filesystem::path profilePath;
    std::filesystem::path checkpointPath;
    std::filesystem::path displayPrefsPath;
    bool hasProfile = false;
    bool hasCheckpoint = false;
    bool hasDisplayPrefs = false;
    std::uint32_t gridWidth = 0;
    std::uint32_t gridHeight = 0;
    std::uint64_t seed = 0;
    std::string temporalPolicy;
    std::string initialConditionMode;
    std::uintmax_t profileBytes = 0;
    std::uintmax_t checkpointBytes = 0;
    std::uintmax_t displayPrefsBytes = 0;
    std::filesystem::file_time_type profileLastWrite{};
    std::filesystem::file_time_type checkpointLastWrite{};
    std::filesystem::file_time_type displayPrefsLastWrite{};
    bool hasProfileTimestamp = false;
    bool hasCheckpointTimestamp = false;
    bool hasDisplayPrefsTimestamp = false;
    std::uint64_t stepIndex = 0;
    std::uint64_t runIdentityHash = 0;
    bool profileUsesFallback = false;
    bool checkpointUsesFallback = false;
    bool displayPrefsUsesFallback = false;

    [[nodiscard]] bool usesLegacyFallback() const noexcept {
        return profileUsesFallback || checkpointUsesFallback || displayPrefsUsesFallback;
    }
};

// Information about an in-memory checkpoint exposed to the GUI.
struct CheckpointInfo {
    std::string label;
    std::uint64_t stepIndex = 0;
    std::uint64_t timestampTicks = 0;
    std::uint64_t stateHash = 0;
    std::uint64_t payloadBytes = 0;
    std::uint64_t runIdentityHash = 0;
    std::uint64_t profileFingerprint = 0;
};

// Central service managing runtime lifecycle, checkpoints, and world storage.
class RuntimeService {
public:
    RuntimeService();

    // Configuration access
    [[nodiscard]] const app::LaunchConfig& config() const noexcept { return config_; }
    void setConfig(const app::LaunchConfig& config) { config_ = config; }
    void setModelScope(ModelScopeContext context);
    [[nodiscard]] const ModelScopeContext& modelScope() const noexcept { return modelScope_; }

    // Runtime state
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isPaused() const;

    // Lifecycle control
    bool start(std::string& message);
    bool restart(std::string& message);
    bool stop(std::string& message);

    // Stepping control
    bool step(std::uint32_t count, std::string& message);
    bool stepBackward(std::uint32_t count, std::string& message);
    bool runUntil(std::uint64_t targetStep, std::string& message);
    bool seekStep(std::uint64_t targetStep, std::string& message);
    bool pause(std::string& message);
    bool resume(std::string& message);
    bool setPlaybackSpeed(float speed, std::string& message);
    [[nodiscard]] float playbackSpeed() const noexcept { return playbackSpeed_; }
    bool configureCheckpointTimeline(std::uint32_t intervalSteps, std::size_t retention, std::string& message);

    // Diagnostics and introspection
    bool status(std::string& message) const;
    bool metrics(std::string& message) const;
    bool listFields(std::string& message) const;
    bool summarizeField(const std::string& variableName, std::string& message) const;
    bool captureCheckpoint(RuntimeCheckpoint& checkpoint, std::string& message, bool computeHash = false) const;
    bool fieldNames(std::vector<std::string>& names, std::string& message) const;
    bool fieldDisplayTags(std::unordered_map<std::string, std::vector<std::string>>& tags, std::string& message) const;
    bool parameterControls(std::vector<ParameterControl>& controls, std::string& message) const;

    // Probe management
    bool addProbe(const ProbeDefinition& definition, std::string& message);
    bool removeProbe(const std::string& probeId, std::string& message);
    bool clearProbes(std::string& message);
    bool probeDefinitions(std::vector<ProbeDefinition>& definitions, std::string& message) const;
    bool probeSeries(const std::string& probeId, ProbeSeries& series, std::string& message) const;
    bool lastStepDiagnostics(StepDiagnostics& diagnostics, std::string& message) const;

    // Parameter and state modification
    bool setParameterValue(const std::string& parameterName, float value, const std::string& note, std::string& message);
    bool applyManualPatch(const std::string& variableName, std::optional<Cell> cell, float newValue, const std::string& note, std::string& message);
    bool undoLastManualPatch(std::string& message);
    bool enqueuePerturbation(const PerturbationSpec& perturbation, std::string& message);
    bool manualEventLog(std::vector<ManualEventRecord>& events, std::string& message) const;
    bool timelineCheckpointSteps(std::vector<std::uint64_t>& steps, std::string& message) const;

    // Checkpoint management
    bool createCheckpoint(const std::string& label, std::string& message);
    bool restoreCheckpoint(const std::string& label, std::string& message);
    bool deleteCheckpoint(const std::string& label, std::string& message);
    bool renameCheckpoint(const std::string& fromLabel, const std::string& toLabel, std::string& message);
    bool checkpointRecords(std::vector<CheckpointInfo>& records, std::string& message) const;
    bool listCheckpoints(std::string& message) const;

    // Profile management
    bool saveProfile(const std::string& name, std::string& message);
    bool loadProfile(const std::string& name, std::string& message);
    bool listProfiles(std::string& message) const;

    // World management
    [[nodiscard]] std::vector<StoredWorldInfo> listStoredWorlds(std::string& message) const;
    [[nodiscard]] std::vector<StoredWorldInfo> listStoredWorldsForModel(const std::string& modelKey, std::string& message) const;
    [[nodiscard]] std::string suggestNextWorldName() const;
    [[nodiscard]] std::string suggestWorldNameFromHint(const std::string& hint) const;
    [[nodiscard]] std::string normalizeWorldNameForUi(const std::string& worldName) const;
    bool createWorld(const std::string& worldName, const app::LaunchConfig& config, std::string& message);
    bool openWorld(const std::string& worldName, std::string& message);
    bool saveActiveWorld(std::string& message);
    bool deleteWorld(const std::string& worldName, std::string& message);
    bool renameWorld(const std::string& fromWorldName, const std::string& toWorldName, std::string& message);
    bool duplicateWorld(const std::string& fromWorldName, const std::string& toWorldName, std::string& message);
    bool exportWorld(const std::string& worldName, const std::filesystem::path& outputPath, std::string& message);
    bool importWorld(const std::filesystem::path& inputPath, std::string& importedWorldName, std::string& message);

    [[nodiscard]] const std::string& activeWorldName() const noexcept { return activeWorldName_; }
    [[nodiscard]] std::string activeModelKey() const;

    bool applySettings(std::string& message);

private:
    [[nodiscard]] app::WorldModelMetadata currentWorldModelMetadata() const;
    void refreshCachedStateNoLock() const;
    bool requireRuntime(const char* operation, std::string& message) const;
    bool captureTimelineCheckpointNoLock(const char* context, std::string& message);
    [[nodiscard]] static std::filesystem::path worldProfileRoot();
    [[nodiscard]] static std::filesystem::path worldCheckpointRoot();
    [[nodiscard]] std::string currentModelKey() const;

    app::LaunchConfig config_{};
    ModelScopeContext modelScope_{};
    std::unique_ptr<Runtime> runtime_;
    std::map<std::string, RuntimeCheckpoint, std::less<>> checkpoints_;
    app::ProfileStore profileStore_{};
    app::ProfileStore worldProfileStore_{worldProfileRoot()};
    app::WorldStore worldStore_{worldProfileRoot(), worldCheckpointRoot()};
    CheckpointManager checkpointManager_{};
    app::CheckpointStorage checkpointStorage_{};
    float playbackSpeed_ = 1.0f;
    std::string activeWorldName_;
    std::unordered_map<std::string, std::vector<std::string>> activeFieldDisplayTags_;
    mutable std::atomic<bool> cachedRunning_{false};
    mutable std::atomic<bool> cachedPaused_{false};
    mutable std::recursive_mutex mutex_;
};

} // namespace ws::gui
