#pragma once

#include "ws/app/world_store.hpp"
#include "ws/app/profile_store.hpp"
#include "ws/app/shell_support.hpp"
#include "ws/core/runtime.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ws::gui {

struct StoredWorldInfo {
    std::string worldName;
    std::filesystem::path profilePath;
    std::filesystem::path checkpointPath;
    bool hasProfile = false;
    bool hasCheckpoint = false;
    std::uint32_t gridWidth = 0;
    std::uint32_t gridHeight = 0;
    std::uint64_t seed = 0;
    std::string tier;
    std::string temporalPolicy;
    std::uintmax_t profileBytes = 0;
    std::uintmax_t checkpointBytes = 0;
    std::filesystem::file_time_type profileLastWrite{};
    std::filesystem::file_time_type checkpointLastWrite{};
    bool hasProfileTimestamp = false;
    bool hasCheckpointTimestamp = false;
    std::uint64_t stepIndex = 0;
    std::uint64_t runIdentityHash = 0;
};

class RuntimeService {
public:
    RuntimeService();

    [[nodiscard]] const app::LaunchConfig& config() const noexcept { return config_; }
    void setConfig(const app::LaunchConfig& config) { config_ = config; }

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isPaused() const;

    bool start(std::string& message);
    bool restart(std::string& message);
    bool stop(std::string& message);

    bool step(std::uint32_t count, std::string& message);
    bool runUntil(std::uint64_t targetStep, std::string& message);
    bool pause(std::string& message);
    bool resume(std::string& message);

    bool status(std::string& message) const;
    bool metrics(std::string& message) const;
    bool listFields(std::string& message) const;
    bool summarizeField(const std::string& variableName, std::string& message) const;
    bool captureCheckpoint(RuntimeCheckpoint& checkpoint, std::string& message, bool computeHash = false) const;
    bool fieldNames(std::vector<std::string>& names, std::string& message) const;
    bool parameterControls(std::vector<ParameterControl>& controls, std::string& message) const;
    bool addProbe(const ProbeDefinition& definition, std::string& message);
    bool removeProbe(const std::string& probeId, std::string& message);
    bool clearProbes(std::string& message);
    bool probeDefinitions(std::vector<ProbeDefinition>& definitions, std::string& message) const;
    bool probeSeries(const std::string& probeId, ProbeSeries& series, std::string& message) const;
    bool lastStepDiagnostics(StepDiagnostics& diagnostics, std::string& message) const;
    bool setParameterValue(const std::string& parameterName, float value, const std::string& note, std::string& message);
    bool applyManualPatch(const std::string& variableName, std::optional<Cell> cell, float newValue, const std::string& note, std::string& message);
    bool undoLastManualPatch(std::string& message);
    bool enqueuePerturbation(const PerturbationSpec& perturbation, std::string& message);
    bool manualEventLog(std::vector<ManualEventRecord>& events, std::string& message) const;

    bool createCheckpoint(const std::string& label, std::string& message);
    bool restoreCheckpoint(const std::string& label, std::string& message);
    bool listCheckpoints(std::string& message) const;

    bool saveProfile(const std::string& name, std::string& message);
    bool loadProfile(const std::string& name, std::string& message);
    bool listProfiles(std::string& message) const;

    [[nodiscard]] std::vector<StoredWorldInfo> listStoredWorlds(std::string& message) const;
    [[nodiscard]] std::string suggestNextWorldName() const;
    bool createWorld(const std::string& worldName, const app::LaunchConfig& config, std::string& message);
    bool openWorld(const std::string& worldName, std::string& message);
    bool saveActiveWorld(std::string& message);
    bool deleteWorld(const std::string& worldName, std::string& message);
    bool renameWorld(const std::string& fromWorldName, const std::string& toWorldName, std::string& message);
    bool duplicateWorld(const std::string& fromWorldName, const std::string& toWorldName, std::string& message);
    bool exportWorld(const std::string& worldName, const std::filesystem::path& outputPath, std::string& message);
    bool importWorld(const std::filesystem::path& inputPath, std::string& importedWorldName, std::string& message);

    [[nodiscard]] const std::string& activeWorldName() const noexcept { return activeWorldName_; }

    bool applySettings(std::string& message);

private:
    void refreshCachedStateNoLock() const;
    bool requireRuntime(const char* operation, std::string& message) const;
    [[nodiscard]] static std::filesystem::path worldProfileRoot();
    [[nodiscard]] static std::filesystem::path worldCheckpointRoot();

    app::LaunchConfig config_{};
    std::unique_ptr<Runtime> runtime_;
    std::map<std::string, RuntimeCheckpoint, std::less<>> checkpoints_;
    app::ProfileStore profileStore_{};
    app::ProfileStore worldProfileStore_{worldProfileRoot()};
    app::WorldStore worldStore_{worldProfileRoot(), worldCheckpointRoot()};
    std::string activeWorldName_;
    mutable std::atomic<bool> cachedRunning_{false};
    mutable std::atomic<bool> cachedPaused_{false};
    mutable std::recursive_mutex mutex_;
};

} // namespace ws::gui
