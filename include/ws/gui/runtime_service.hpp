// =============================================================================
// Public API Note
// =============================================================================
// RuntimeService is the primary GUI-facing interface for simulation control,
// state queries, checkpoint management, and world storage.
//
// Public contract: The types, enums, and method signatures in this header are
// stable for external use. Internal implementation may change without notice,
// but the public method contracts are maintained.
//
// The data structures below (ModelScopeContext, StoredWorldInfo, CheckpointInfo)
// represent query results and are designed to be stable for query-based access.
// =============================================================================

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

struct ModelScopeContext {
    std::string modelId;      ///< Unique model identifier.
    std::string modelName;    ///< Display name for the model.
    std::string modelPath;    ///< Filesystem path to the model package.
    std::string modelIdentityHash; ///< Deterministic hash of model structure.
};

/// Information about a stored world (profile/checkpoint).
/// Aggregates filesystem metadata and checkpoint diagnostics for UI queries.
/// Safe to query concurrently; instances are snapshot copies.
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
/// Represents a named checkpoint saved in the runtime's in-memory checkpoint map.
/// Immutable query result; safe for concurrent reads.
struct CheckpointInfo {
    std::string label;
    std::uint64_t stepIndex = 0;
    std::uint64_t timestampTicks = 0;
    std::uint64_t stateHash = 0;
    std::uint64_t payloadBytes = 0;
    std::uint64_t runIdentityHash = 0;
    std::uint64_t profileFingerprint = 0;
};

/// Central service managing runtime lifecycle, checkpoints, and world storage.
///
/// ## Public API Contract
/// This class is the primary GUI interface for simulation management. All public
/// methods and types in this header are stable; callers may depend on them without
/// breaking on patch releases.
///
/// ## Threading Model
/// - **Mutation/control methods** (start, stop, step, checkpoint writes, world operations)
///   are serialized internally via a recursive mutex and are intended to be called
///   by a single owner thread (typically the GUI event loop).
/// - **Query/read methods** are lock-backed snapshot providers (e.g., config(), modelScope())
///   and safe to call concurrently from multiple threads.
/// - **State caches** (isRunning(), isPaused()) are low-latency reads backed by a
///   single published atomic snapshot with acquire/release semantics, guaranteeing
///   that isRunning/isPaused pairs always come from the same publication moment
///   (no mixed-observation anomalies).
///
/// ## Method Patterns
/// - Most methods return `bool` and populate an output `message` parameter with
///   diagnostic text. `true` indicates success; `false` indicates failure.
/// - All mutation operations (step, checkpoint, world operations) are best-effort;
///   callers should check the return code and message.
/// - Query methods marked `[[nodiscard]]` return snapshot copies; modifying them
///   does not affect runtime state.
///
/// ## Failure Semantics
/// - Methods return `false` on failure, populating `message` with a formatted
///   error string (typically "operation_name error=detail_text").
/// - Operations failing due to no active runtime return message containing
///   "runtime_not_active" or "runtime is not ready".
/// - All exceptions are caught and converted to false returns with descriptive messages.
///
/// ## Ownership and Initialization
/// - Instances may be created freely; the service owns a unique_ptr to the Runtime.
/// - setModelScope() must be called before start() to configure which model to use.
/// - setConfig() can be called anytime; changes are applied to the next start() or restart().
class RuntimeService {
public:
    /// Constructs a new RuntimeService in stopped state with default configuration.
    /// No runtime is active until start() succeeds.
    RuntimeService();

    // =========================================================================
    // Configuration Access (thread-safe snapshots)
    // =========================================================================

    /// Returns a snapshot copy of the current launch configuration.
    /// Thread-safe; does not modify runtime state.
    [[nodiscard]] app::LaunchConfig config() const;

    /// Sets the launch configuration to be used by the next start() call.
    /// Does not affect a currently running simulation.
    void setConfig(const app::LaunchConfig& config);

    /// Sets the model scope (model identity, name, path) for the next start().
    /// Must be called before start() to establish which model to execute.
    void setModelScope(ModelScopeContext context);

    /// Returns a snapshot copy of the current model scope context.
    /// Thread-safe; does not modify runtime state.
    [[nodiscard]] ModelScopeContext modelScope() const;

    // =========================================================================
    // Runtime State (low-latency cache reads)
    // =========================================================================

    /// Returns true if a runtime is currently active and stepping.
    /// Low-latency cached read; safe to call frequently from UI.
    /// Never observes mixed running/paused states from different moments.
    [[nodiscard]] bool isRunning() const;

    /// Returns true if the runtime is active but paused (suspended stepping).
    /// Low-latency cached read; paired with isRunning() from same snapshot.
    [[nodiscard]] bool isPaused() const;

    // =========================================================================
    // Lifecycle Control
    // =========================================================================

    /// Starts a new simulation instance using the current config and model scope.
    /// - Precondition: modelScope() must have been set and point to a valid model.
    /// - On success: runtime becomes active and returns true; isRunning() returns true.
    /// - On failure: returns false with diagnostic message; no state change.
    /// - Postcondition: Any existing runtime is destroyed and replaced.
    bool start(std::string& message);

    /// Stops the current runtime and restarts using the current config.
    /// Shorthand for stop() followed by start().
    bool restart(std::string& message);

    /// Stops the active runtime gracefully.
    /// - Returns true on success; idempotent (safe to call when already stopped).
    /// - Returns false with diagnostic on error (e.g., I/O failure during cleanup).
    bool stop(std::string& message);

    // =========================================================================
    // Stepping Control
    // =========================================================================

    /// Advances the simulation by the specified number of steps.
    /// - Precondition: runtime must be active and not paused.
    /// - count: number of steps to advance (must be > 0).
    /// - Returns true on success; false on failure or invalid step count.
    bool step(std::uint32_t count, std::string& message);

    /// Rewinds the simulation by undoing the specified number of steps.
    /// - Precondition: runtime must be active and have undo history available.
    /// - count: number of steps to rewind (must be > 0).
    /// - Returns true on success; false if history unavailable or invalid count.
    bool stepBackward(std::uint32_t count, std::string& message);

    /// Runs simulation forward until reaching the target step index.
    /// - Precondition: runtime must be active and not paused.
    /// - targetStep: absolute step index to reach (must be >= current step).
    /// - Returns true on success; false if target < current or on I/O error.
    bool runUntil(std::uint64_t targetStep, std::string& message);

    /// Seeks the simulation to an arbitrary step index (undo/redo).
    /// - Precondition: runtime must be active; checkpoint at targetStep must exist.
    /// - targetStep: absolute step to restore to.
    /// - Returns true on seek success; false if no checkpoint or seek fails.
    bool seekStep(std::uint64_t targetStep, std::string& message);

    /// Pauses stepping without stopping the runtime.
    /// - After pause(), isPaused() returns true and step() calls fail.
    /// - Returns true on success; false if not running or pause fails.
    bool pause(std::string& message);

    /// Resumes stepping from paused state.
    /// - After resume(), isPaused() returns false.
    /// - Returns true on success; false if not paused or resume fails.
    bool resume(std::string& message);

    /// Sets the playback speed multiplier for automated stepping.
    /// - speed: must be in [0.1, 1.0]; values outside this range are rejected.
    /// - Does not validate immediately; takes effect on next step/runUntil.
    /// - Returns true on success; false if speed is out of range.
    bool setPlaybackSpeed(float speed, std::string& message);

    /// Returns the current playback speed multiplier.
    [[nodiscard]] float playbackSpeed() const;

    /// Returns canonical ids of all currently registered time integrators.
    [[nodiscard]] std::vector<std::string> availableTimeIntegrators() const;

    /// Returns the currently selected time integrator id.
    [[nodiscard]] std::string currentTimeIntegrator() const;

    /// Switches the active time integrator without restarting the runtime.
    /// - integratorId: canonical id or alias known to the integrator registry.
    /// - Returns true on success, false when the id cannot be resolved.
    bool setTimeIntegrator(const std::string& integratorId, std::string& message);

    /// Configures automatic checkpointing behavior.
    /// - intervalSteps: checkpoint every N steps (must be > 0).
    /// - retention: maximum number of checkpoints to keep in memory.
    /// - Returns true on success; false if interval is zero or retention invalid.
    bool configureCheckpointTimeline(std::uint32_t intervalSteps, std::size_t retention, std::string& message);

    // =========================================================================
    // Diagnostics and Introspection
    // =========================================================================

    /// Returns a formatted summary of the current runtime status (step, time, state).
    /// Thread-safe; read-only operation.
    bool status(std::string& message) const;

    /// Returns a formatted report of runtime performance metrics (allocations, step time, etc.).
    /// Thread-safe; read-only operation.
    bool metrics(std::string& message) const;

    /// Returns a formatted list of all field (variable) names defined in the model.
    /// Thread-safe; read-only operation.
    bool listFields(std::string& message) const;

    /// Computes and returns statistical summary (min, max, mean, std dev) for a named field.
    /// - variableName: must exist in the model; empty name is rejected.
    /// - Returns true on success; false if variable unknown or computation fails.
    bool summarizeField(const std::string& variableName, std::string& message) const;

    /// Captures the current runtime state into a RuntimeCheckpoint structure.
    /// - checkpoint: output buffer to receive the snapshot (must not be null).
    /// - computeHash: if true, computes a hash of the state (slower but verifiable).
    /// - Returns true on success; false if no runtime or capture fails.
    bool captureCheckpoint(RuntimeCheckpoint& checkpoint, std::string& message, bool computeHash = false) const;

    /// Populates a vector with the names of all fields in the current model.
    /// - names: cleared and refilled with field name strings.
    /// - Returns true on success; false if no runtime or query fails.
    bool fieldNames(std::vector<std::string>& names, std::string& message) const;

    /// Populates a map of field names to their display tags (UI hints).
    /// - tags: map from field name to list of tag strings (e.g., "scalar", "vector", "temporal").
    /// - Returns true on success; false if no runtime or query fails.
    bool fieldDisplayTags(std::unordered_map<std::string, std::vector<std::string>>& tags, std::string& message) const;

    /// Returns a list of model-defined parameter controls (sliders, dropdowns, etc.).
    /// - controls: cleared and refilled with ParameterControl instances.
    /// - Returns true on success; false if no runtime or query fails.
    bool parameterControls(std::vector<ParameterControl>& controls, std::string& message) const;

    // =========================================================================
    // Probe Management (diagnostic sensors)
    // =========================================================================

    /// Adds a diagnostic probe to the simulation.
    /// - definition: must have unique probeId and valid variable references.
    /// - Probes sample named variables at each step for time-series analysis.
    /// - Returns true on success; false if probe already exists or definition invalid.
    bool addProbe(const ProbeDefinition& definition, std::string& message);

    /// Removes a named probe and discards its accumulated series.
    /// - probeId: must match an existing probe; unknown IDs are silently ignored.
    /// - Returns true on success; false if probe not found or removal fails.
    bool removeProbe(const std::string& probeId, std::string& message);

    /// Removes all probes and clears all probe data.
    /// - Faster than removing probes individually.
    /// - Returns true on success; false only on severe internal errors.
    bool clearProbes(std::string& message);

    /// Returns a list of all currently active probe definitions.
    /// - definitions: cleared and refilled with ProbeDefinition instances.
    /// - Returns true on success; false if no runtime or query fails.
    bool probeDefinitions(std::vector<ProbeDefinition>& definitions, std::string& message) const;

    /// Retrieves the accumulated time-series data for a named probe.
    /// - probeId: must exist; unknown IDs return false with diagnostic.
    /// - series: populated with ProbeSeries data (timestamps and values).
    /// - Returns true on success; false if probe unknown or retrieval fails.
    bool probeSeries(const std::string& probeId, ProbeSeries& series, std::string& message) const;

    /// Captures diagnostic information (effect ledger state) from the most recent step.
    /// - diagnostics: output buffer for StepDiagnostics.
    /// - Returns true on success; false if no runtime or diagnostics unavailable.
    bool lastStepDiagnostics(StepDiagnostics& diagnostics, std::string& message) const;

    // =========================================================================
    // Parameter and State Modification
    // =========================================================================

    /// Sets a named model parameter to a new value.
    /// - parameterName: must exist in the model's parameter set; unknown names rejected.
    /// - value: new value to apply (validated by the model).
    /// - note: optional string describing why the change was made (for logging).
    /// - Returns true on success; false if parameter unknown or out of range.
    bool setParameterValue(const std::string& parameterName, float value, const std::string& note, std::string& message);

    /// Manually overwrites a grid cell value (perturbation by direct assignment).
    /// - variableName: must exist; unknown names rejected.
    /// - cell: if set, applies to specific cell; if empty, applies to entire grid.
    /// - newValue: new value to assign (validated by field constraints).
    /// - note: optional description for logging.
    /// - Returns true on success; false if variable unknown or cell out of bounds.
    bool applyManualPatch(const std::string& variableName, std::optional<Cell> cell, float newValue, const std::string& note, std::string& message);

    /// Undoes the most recent manual patch (restores overwritten values).
    /// - Returns true on success; false if no patch history or undo fails.
    bool undoLastManualPatch(std::string& message);

    /// Enqueues a perturbation event to be applied at a future step.
    /// - perturbation: must define trigger and action; validated by the model.
    /// - Returns true on success; false if perturbation invalid or queue full.
    bool enqueuePerturbation(const PerturbationSpec& perturbation, std::string& message);

    /// Returns a log of all manual events (patches, parameter changes) applied in this session.
    /// - events: cleared and refilled with ManualEventRecord entries.
    /// - Returns true on success; false if no runtime or query fails.
    bool manualEventLog(std::vector<ManualEventRecord>& events, std::string& message) const;

    /// Returns counts of pending effects awaiting application (for diagnostics).
    /// - Returns true on success; false if no runtime or query fails.
    bool effectLedgerCounts(
        std::size_t& pendingImmediateWrites,
        std::size_t& queuedDeferredEvents,
        std::size_t& pendingScheduledPerturbations,
        std::size_t& runtimeManualEventCount,
        std::string& message) const;

    /// Returns a list of step indices where automatic checkpoints are available.
    /// - steps: cleared and refilled with checkpoint step indices.
    /// - Returns true on success; false if no runtime or query fails.
    bool timelineCheckpointSteps(std::vector<std::uint64_t>& steps, std::string& message) const;

    // =========================================================================
    // Checkpoint Management
    // =========================================================================

    /// Creates a named checkpoint of the current runtime state.
    /// - label: user-provided name; must be non-empty and unique (rejected if exists).
    /// - On success: checkpoint is stored in memory and persisted to disk.
    /// - Returns true on success; false if label invalid or I/O fails.
    bool createCheckpoint(const std::string& label, std::string& message);

    /// Restores the runtime to a previously saved checkpoint.
    /// - label: must name an existing checkpoint; unknown labels rejected.
    /// - On success: runtime state is reverted to the saved point.
    /// - Returns true on success; false if checkpoint unknown or restore fails.
    bool restoreCheckpoint(const std::string& label, std::string& message);

    /// Deletes a named checkpoint (in-memory and on-disk).
    /// - label: must name an existing checkpoint; unknown labels rejected.
    /// - Returns true on success; false if checkpoint unknown or deletion fails.
    bool deleteCheckpoint(const std::string& label, std::string& message);

    /// Renames a checkpoint (both in-memory and persistent storage).
    /// - fromLabel: must exist; unknown names rejected.
    /// - toLabel: must not exist (duplicate rejection).
    /// - Returns true on success; false if source missing, target exists, or rename fails.
    bool renameCheckpoint(const std::string& fromLabel, const std::string& toLabel, std::string& message);

    /// Returns metadata for all in-memory checkpoints.
    /// - records: cleared and refilled with CheckpointInfo entries.
    /// - Returns true on success; false if no runtime or query fails.
    bool checkpointRecords(std::vector<CheckpointInfo>& records, std::string& message) const;

    /// Returns a formatted list of all saved checkpoint names and metadata.
    /// - Thread-safe; read-only operation.
    bool listCheckpoints(std::string& message) const;

    // =========================================================================
    // Profile Management
    // =========================================================================

    /// Saves the current runtime configuration and state as a named profile.
    /// - name: must be non-empty and unique; duplicates rejected.
    /// - Profiles capture parameter values, initial conditions, and display settings.
    /// - Returns true on success; false if name invalid or save fails.
    bool saveProfile(const std::string& name, std::string& message);

    /// Loads a named profile, applying its parameters and settings to the runtime.
    /// - name: must exist; unknown profiles rejected.
    /// - Returns true on success; false if profile unknown or load fails.
    bool loadProfile(const std::string& name, std::string& message);

    /// Returns a formatted list of all saved profile names.
    /// - Thread-safe; read-only operation.
    bool listProfiles(std::string& message) const;

    // =========================================================================
    // World Management
    // =========================================================================

    /// Returns metadata for all stored worlds across all models.
    /// - message: populated with diagnostic info if query fails.
    /// - Returns snapshot list of StoredWorldInfo; safe for concurrent reads.
    std::vector<StoredWorldInfo> listStoredWorlds(std::string& message) const;

    /// Returns metadata for all stored worlds of a specific model.
    /// - modelKey: must be valid; unknown keys return empty list with diagnostic.
    /// - message: populated with diagnostic info if query fails.
    /// - Returns snapshot list of StoredWorldInfo filtered by model.
    std::vector<StoredWorldInfo> listStoredWorldsForModel(const std::string& modelKey, std::string& message) const;

    /// Generates a unique world name not used in any stored world.
    /// - Used by UI when creating a new world without user-provided name.
    std::string suggestNextWorldName() const;

    /// Generates a unique world name derived from a user-provided hint string.
    /// - hint: base name to derive from (e.g., "test" -> "test_1", "test_2", ...).
    /// - Returns unique name not present in stored world list.
    std::string suggestWorldNameFromHint(const std::string& hint) const;

    /// Normalizes a user-entered world name for display and storage.
    /// - Applies case rules, special-character escaping, and length constraints.
    /// - Does not check uniqueness; use in combination with world list queries.
    std::string normalizeWorldNameForUi(const std::string& worldName) const;

    /// Creates a new world with the given name and launch configuration.
    /// - worldName: must be non-empty and not already exist (duplicates rejected).
    /// - config: launch settings (seed, grid, initial condition, etc.).
    /// - Returns true on success; false if name invalid, duplicate, or creation fails.
    bool createWorld(const std::string& worldName, const app::LaunchConfig& config, std::string& message);

    /// Opens an existing stored world for simulation.
    /// - worldName: must exist in the stored world list; unknown names rejected.
    /// - Precondition: a model scope must be set before calling.
    /// - On success: a new runtime is created and the world is loaded and ready.
    /// - Returns true on success; false if world unknown or load fails.
    bool openWorld(const std::string& worldName, std::string& message);

    /// Opens a checkpoint file directly (used for file-based checkpoint restoration).
    /// - checkpointPath: filesystem path to a checkpoint file.
    /// - Precondition: a model scope must be set; checkpoint must be valid.
    /// - Returns true on success; false if file invalid or load fails.
    bool openCheckpointFile(const std::filesystem::path& checkpointPath, std::string& message);

    /// Persists the currently active world and its runtime state to storage.
    /// - Precondition: a runtime must be active.
    /// - Saves the world profile, current state, and display preferences.
    /// - Returns true on success; false if not running or save fails.
    bool saveActiveWorld(std::string& message);

    /// Deletes a stored world (profile, checkpoint, and display settings).
    /// - worldName: must exist; unknown worlds rejected.
    /// - Returns true on success; false if world unknown or deletion fails.
    bool deleteWorld(const std::string& worldName, std::string& message);

    /// Renames a stored world (on-disk only; does not affect runtime if world is active).
    /// - fromWorldName: must exist; unknown names rejected.
    /// - toWorldName: must not exist (duplicates rejected).
    /// - Returns true on success; false if source missing, target exists, or rename fails.
    bool renameWorld(const std::string& fromWorldName, const std::string& toWorldName, std::string& message);

    /// Creates a new world as a copy of an existing one.
    /// - fromWorldName: source world (must exist).
    /// - toWorldName: new world name (must not exist).
    /// - Returns true on success; false if source missing, target exists, or copy fails.
    bool duplicateWorld(const std::string& fromWorldName, const std::string& toWorldName, std::string& message);

    /// Exports a stored world to a file (typically as a distributable archive).
    /// - worldName: must exist in stored worlds.
    /// - outputPath: filesystem destination for the export file.
    /// - Returns true on success; false if world unknown or export fails.
    bool exportWorld(const std::string& worldName, const std::filesystem::path& outputPath, std::string& message);

    /// Imports a world from a file and makes it available for opening.
    /// - inputPath: filesystem path to the world file to import.
    /// - importedWorldName: output variable set to the imported world's name.
    /// - Returns true on success; false if file invalid or import fails.
    bool importWorld(const std::filesystem::path& inputPath, std::string& importedWorldName, std::string& message);

    /// Returns the name of the currently active world (if any running runtime has one).
    /// - Returns empty string if no world is active.
    std::string activeWorldName() const;

    /// Returns the model key (identifier) of the currently active runtime (if any).
    /// - Returns empty string if no runtime is active.
    std::string activeModelKey() const;

    /// Applies any pending configuration changes and restarts the simulation.
    /// - Convenience method that combines the effect of stop() and start() with config changes.
    /// - Returns true on success; false if restart fails.
    bool applySettings(std::string& message);

private:
    enum class CachedRuntimeState : std::uint8_t {
        Stopped = 0,
        Running = 1,
        RunningPaused = 2,
    };

    [[nodiscard]] static CachedRuntimeState makeCachedRuntimeState(bool running, bool paused) noexcept;
    [[nodiscard]] static bool cachedStateRunning(CachedRuntimeState state) noexcept;
    [[nodiscard]] static bool cachedStatePaused(CachedRuntimeState state) noexcept;

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
    mutable std::atomic<std::uint8_t> cachedRuntimeState_{
        static_cast<std::uint8_t>(CachedRuntimeState::Stopped)};
    mutable std::recursive_mutex mutex_;
};

} // namespace ws::gui
