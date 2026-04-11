// =============================================================================
// Runtime Public API
// =============================================================================
// Core simulation runtime interface including configuration, execution control,
// and state management.
//
// Public contract: RuntimeConfig, the Grid/Memory/Temporal/Boundary enums,
// Probe types, and runtime execution methods are stable for external use.
//
// Note on initialization types: The 13 initialization condition types and their
// parameter structures are currently exposed but should be considered internal
// implementation details. Future versions may refactor this API to expose only
// the active configuration. Client code should not depend on the full set of
// initialization types.
// =============================================================================

#pragma once

// Core simulation headers
#include "ws/core/event_queue.hpp"
#include "ws/core/interactions.hpp"
#include "ws/core/observability.hpp"
#include "ws/core/probe.hpp"
#include "ws/core/profile.hpp"
#include "ws/core/run_signature.hpp"
#include "ws/core/scheduler.hpp"
#include "ws/core/state_store.hpp"

// Standard library containers
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ws {

// =============================================================================
// Initial Condition Types
// =============================================================================

// Available initial condition generators for field initialization.
// Each type corresponds to a different algorithm for setting initial values.
enum class InitialConditionType : std::uint8_t {
    Terrain = 0,
    Conway = 1,
    GrayScott = 2,
    Waves = 3,
    Blank = 4,
    Voronoi = 5,
    Clustering = 6,
    SparseRandom = 7,
    GradientField = 8,
    Checkerboard = 9,
    RadialPattern = 10,
    MultiScale = 11,
    DiffusionLimit = 12
};

// =============================================================================
// Terrain Initialization Parameters
// =============================================================================

// Parameters for terrain-based initial conditions using multi-octave
// fractal noise with ridge, island falloff, and erosion simulation.
struct TerrainParams {
    float terrainBaseFrequency = 2.2f;
    float terrainDetailFrequency = 7.5f;
    float terrainWarpStrength = 0.55f;
    float terrainAmplitude = 1.0f;
    float terrainRidgeMix = 0.28f;
    int terrainOctaves = 5;
    float terrainLacunarity = 2.0f;
    float terrainGain = 0.5f;
    float seaLevel = 0.48f;
    float polarCooling = 0.62f;
    float latitudeBanding = 1.0f;
    float humidityFromWater = 0.52f;
    float biomeNoiseStrength = 0.20f;
    float islandDensity = 0.58f;
    float islandFalloff = 1.35f;
    float coastlineSharpness = 1.10f;
    float archipelagoJitter = 0.85f;
    float erosionStrength = 0.32f;
    float shelfDepth = 0.20f;
};

// =============================================================================
// Conway's Game of Life Parameters
// =============================================================================

// Parameters for Conway's Game of Life cellular automaton initialization.
struct ConwayParams {
    std::string targetVariable = "initialization.conway.target";
    float aliveProbability = 0.5f;
    float aliveValue = 1.0f;
    float deadValue = 0.0f;
    int smoothingPasses = 0;
};

// =============================================================================
// Gray-Scott Reaction-Diffusion Parameters
// =============================================================================

// Parameters for Gray-Scott reaction-diffusion system initialization.
struct GrayScottParams {
    std::string targetVariableA = "initialization.gray_scott.target_a";
    std::string targetVariableB = "initialization.gray_scott.target_b";
    float backgroundA = 1.0f;
    float backgroundB = 0.0f;
    float spotValueA = 0.0f;
    float spotValueB = 1.0f;
    int spotCount = 4;
    float spotRadius = 15.0f;
    float spotJitter = 0.35f;
};

// =============================================================================
// Wave Propagation Parameters
// =============================================================================

// Parameters for wave-based initial conditions with radial drops.
struct WaveParams {
    std::string targetVariable = "initialization.waves.target";
    float baseline = 0.0f;
    float dropAmplitude = 1.0f;
    float dropRadius = 5.0f;
    int dropCount = 1;
    float dropJitter = 0.35f;
    float ringFrequency = 1.0f;
};

// =============================================================================
// Voronoi Diagram Parameters
// =============================================================================

// Parameters for Voronoi tessellation-based initialization.
struct VoronoiParams {
    std::string targetVariable = "initialization.voronoi.target";
    int seedCount = 12;
    float smoothing = 0.3f;
    float colorScale = 1.0f;
    float jitter = 0.5f;
};

// =============================================================================
// Clustering Initialization Parameters
// =============================================================================

// Parameters for cluster-based field initialization with multiple
// circular clusters and exponential falloff.
struct ClusteringParams {
    std::string targetVariable = "initialization.clustering.target";
    int clusterCount = 8;
    float clusterRadius = 20.0f;
    float clusterIntensity = 0.8f;
    float clusterDecay = 0.6f;
    float clusterSpread = 0.4f;
};

// =============================================================================
// Sparse Random Initialization Parameters
// =============================================================================

// Parameters for sparse random field initialization with optional clustering.
struct SparseRandomParams {
    std::string targetVariable = "initialization.sparse.target";
    float fillDensity = 0.15f;
    float minValue = 0.2f;
    float maxValue = 0.9f;
    bool clusterSparse = false;
    float clusterRadius = 8.0f;
};

// =============================================================================
// Gradient Field Parameters
// =============================================================================

// Parameters for gradient-based field initialization from a center point.
struct GradientFieldParams {
    std::string targetVariable = "initialization.gradient.target";
    int directionMode = 0;
    float gradientScale = 1.0f;
    float centerX = 0.5f;
    float centerY = 0.5f;
    float perturbation = 0.1f;
};

// =============================================================================
// Checkerboard Pattern Parameters
// =============================================================================

// Parameters for checkerboard pattern initialization.
struct CheckerboardParams {
    std::string targetVariable = "initialization.checkerboard.target";
    int cellSize = 4;
    float darkValue = 0.2f;
    float lightValue = 0.8f;
    float blurRadius = 0.0f;
    bool diagonal = false;
};

// =============================================================================
// Radial Pattern Parameters
// =============================================================================

// Parameters for radial ring pattern initialization.
struct RadialPatternParams {
    std::string targetVariable = "initialization.radial.target";
    float centerX = 0.5f;
    float centerY = 0.5f;
    int ringCount = 6;
    float innerValue = 0.3f;
    float outerValue = 0.7f;
    float falloff = 1.0f;
};

// =============================================================================
// Multi-Scale Pattern Parameters
// =============================================================================

// Parameters for multi-scale layered pattern initialization.
struct MultiScaleParams {
    std::string targetVariable = "initialization.multiscale.target";
    int scaleCount = 3;
    float baseFrequency = 0.5f;
    float frequencyScale = 2.0f;
    float amplitudeScale = 0.5f;
    float blendMode = 0.5f;
};

// =============================================================================
// Diffusion-Limited Aggregation Parameters
// =============================================================================

// Parameters for diffusion-limited pattern initialization.
struct DiffusionLimitParams {
    std::string targetVariable = "initialization.diffusion.target";
    int seedCount = 5;
    float growthRate = 0.2f;
    float anisotropy = 0.0f;
    float colorVariance = 0.3f;
    float randomWalk = 0.15f;
};

// =============================================================================
// Initial Condition Configuration
// =============================================================================

// Complete configuration for field initialization. Contains the type
// selector and parameters for all available initial condition generators.
struct InitialConditionConfig {
    InitialConditionType type = InitialConditionType::Terrain;
    TerrainParams terrain;
    ConwayParams conway;
    GrayScottParams grayScott;
    WaveParams waves;
    VoronoiParams voronoi;
    ClusteringParams clustering;
    SparseRandomParams sparseRandom;
    GradientFieldParams gradientField;
    CheckerboardParams checkerboard;
    RadialPatternParams radialPattern;
    MultiScaleParams multiScale;
    DiffusionLimitParams diffusionLimit;
};

// =============================================================================
// Parameter Control Specification
// =============================================================================

// Runtime-adjustable model parameter with bounds and units.
struct ParameterControl {
    std::string name;
    std::string targetVariable;
    float value = 0.0f;
    float minValue = -1.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    std::string units = "1";
};

// =============================================================================
// Model Execution Specification
// =============================================================================

// Model-specific execution configuration including variable ordering
// and boundary handling preferences.
struct ModelExecutionSpec {
    std::vector<std::string> cellScalarVariableIds;
    std::vector<std::string> stageOrder;
    std::vector<std::string> conservedVariables;
    std::vector<CrossVariableConstraint> crossVariableConstraints;
    std::unordered_map<std::string, std::string> semanticFieldAliases;
    std::optional<BoundaryMode> preferredBoundaryMode;
};

// =============================================================================
// Model Display Specification
// =============================================================================

// Display configuration including field tags for visualization.
struct ModelDisplaySpec {
    std::unordered_map<std::string, std::vector<std::string>> fieldTags;
};

// =============================================================================
// Runtime Configuration
// =============================================================================

// Complete configuration for simulation runtime instantiation.
// Contains all parameters needed to initialize the grid, fields, scheduler,
// and execution policies.
struct RuntimeConfig {
    std::uint64_t seed = 1;
    GridSpec grid{16, 16};
    BoundaryMode boundaryMode = BoundaryMode::Clamp;
    GridTopologyBackend topologyBackend = GridTopologyBackend::Cartesian2D;
    MemoryLayoutPolicy memoryLayoutPolicy{};
    UnitRegime unitRegime = UnitRegime::Normalized;
    TemporalPolicy temporalPolicy = TemporalPolicy::UniformA;
    ExecutionPolicyMode executionPolicyMode = ExecutionPolicyMode::StrictDeterministic;
    NumericGuardrailPolicy guardrailPolicy{};
    ProfileResolverInput profileInput{};
    InitialConditionConfig initialConditions{};
    std::vector<ParameterControl> modelParameterControls{};
    std::optional<ModelExecutionSpec> modelExecutionSpec{};
    std::optional<ModelDisplaySpec> modelDisplaySpec{};
};

// =============================================================================
// Perturbation Types
// =============================================================================

// Types of perturbations that can be applied to running simulations.
enum class PerturbationType : std::uint8_t {
    Gaussian = 0,
    Rectangle = 1,
    Sine = 2,
    WhiteNoise = 3,
    Gradient = 4
};

// =============================================================================
// Perturbation Specification
// =============================================================================

// Defines a perturbation to apply to the simulation state during execution.
// Perturbations are time-based and can be spatially localized.
struct PerturbationSpec {
    PerturbationType type = PerturbationType::Gaussian;
    std::string targetVariable;
    float amplitude = 0.0f;
    std::uint32_t startStep = 0;
    std::uint32_t durationSteps = 1;
    Cell origin{0, 0};
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    float sigma = 1.0f;
    float frequency = 0.1f;
    std::uint64_t noiseSeed = 0;
    std::string description;
};

// =============================================================================
// Runtime Snapshot
// =============================================================================

// Immutable snapshot of runtime state at a specific point in time.
// Used for visualization and checkpointing.
struct RuntimeSnapshot {
    RunSignature runSignature;
    std::uint64_t stateHash = 0;
    StateHeader stateHeader{};
    std::uint64_t payloadBytes = 0;
    ReproducibilityClass reproducibilityClass = ReproducibilityClass::Strict;
    StabilityDiagnostics stabilityDiagnostics{};
};

// =============================================================================
// Runtime Checkpoint
// =============================================================================

// Complete checkpoint data for saving and restoring runtime state.
// Includes signature, profile fingerprint, state data, and event log.
struct RuntimeCheckpoint {
    RunSignature runSignature = RunSignature(
        0,
        "placeholder",
        GridSpec{1, 1},
        BoundaryMode::Clamp,
        UnitRegime::Normalized,
        TemporalPolicy::UniformA,
        "none",
        "none",
        0,
        0,
        0);
    std::uint64_t profileFingerprint = 0;
    StateStoreSnapshot stateSnapshot{};
    std::vector<ManualEventRecord> manualEventLog;
};

// =============================================================================
// Runtime Class
// =============================================================================

/// Main simulation runtime controller managing the simulation lifecycle,
/// state storage, scheduling, event processing, and observability.
///
/// ## Public API Contract
/// All public methods and the RuntimeConfig/RuntimeCheckpoint structures are
/// stable for external use. The detailed initialization type parameters are
/// currently exposed but should be considered implementation details and may
/// be refactored in future versions.
///
/// ## Runtime Lifecycle
/// 1. **Created**: After constructor; runtime is ready but not stepping.
/// 2. **Running**: After start(); simulation is stepping forward.
/// 3. **Paused**: After pause() called while running; stepping is suspended.
/// 4. **Terminated**: After stop(); simulation has ended.
///
/// ## Step Contract (Deterministic Stepping)
/// Each step follows this order and semantics are preserved for identical
/// seed + config inputs:
/// 1. Input patch ingestion (queueInput frames are applied to state).
/// 2. Event queue apply (enqueued RuntimeEvents are processed).
/// 3. Scheduler execution (subsystems run in order).
/// 4. State metadata/hash commit (snapshot is updated, probes sample).
///
/// ## Thread Safety
/// - **Not thread-safe for mutations**: All state modifications (start, pause,
///   resume, stop, step, queueInput, enqueueEvent, etc.) must be called from
///   a single owner thread.
/// - **Safe for queries**: snapshot(), status(), metrics(), paused() can be
///   called concurrently from other threads.
/// - **Safe for read-only checkpoint inspection**: RuntimeCheckpoint instances
///   can be read from multiple threads once created.
///
/// ## Subsystem Registration and Execution Order
/// Subsystems are executed in the order they are registered via registerSubsystem().
/// Each subsystem is called once per step during the scheduler execution phase.
/// Subsystems must be registered before start() is called.
///
/// ## Checkpoint Semantics
/// - Checkpoints capture the complete runtime state at a snapshot moment.
/// - createCheckpoint() generates a new checkpoint with optional hash computation.
/// - loadCheckpoint() and resetToCheckpoint() restore the runtime to a saved state.
/// - Checkpoints are immutable after creation.
///
/// ## Determinism Guarantees
/// - For identical (seed, config, subsystem set), stepping produces the same state.
/// - State hashes are recorded per step and can be validated against reference hashes.
/// - If numeric guardrail policy changes, determinism may break and is logged.
///
/// ## Error Handling
/// - Methods returning `[[nodiscard]] bool` should have their return value checked.
/// - Operations that fail return false and populate a message parameter with diagnostics.
/// - Exceptions are not thrown; failures are reported via return codes and messages.
///
/// ## Initialization Type Notes
/// The 13 initialization condition types exposed (Terrain, Conway, GrayScott, etc.)
/// are currently visible in the public API but should be treated as internal details.
/// Future versions may refactor this to expose only the active configuration and
/// hide unused types. Client code should not rely on the full set of initialization types.
class Runtime {
public:
    /// Constructs a runtime with the given configuration.
    /// The runtime will be in Created state until start() is called.
    /// All subsystems and event handlers may be registered after construction.
    explicit Runtime(RuntimeConfig config);

    /// Registers a subsystem to be called during simulation steps.
    /// Subsystems are executed in the order they are registered.
    /// Must be called before start().
    void registerSubsystem(std::shared_ptr<ISubsystem> subsystem);

    /// Selects the execution profile based on model characteristics.
    /// Affects performance optimizations; called during initialization.
    void selectProfile(ProfileResolverInput profileInput);

    /// Updates the numerical guardrail policy for stability monitoring.
    /// Applies to all subsequent steps.
    void updateGuardrailPolicy(NumericGuardrailPolicy guardrailPolicy);

    /// Starts the simulation from the initial state.
    /// Transitions from Created to Running state.
    /// Precondition: Runtime must be in Created state.
    void start();

    /// Pauses the simulation at the current step.
    /// Transitions from Running to Paused state; step() calls fail while paused.
    /// Precondition: Runtime must be in Running state.
    void pause();

    /// Resumes a paused simulation from the current step.
    /// Transitions from Paused to Running state.
    /// Precondition: Runtime must be in Paused state.
    void resume();

    /// Executes a single simulation step following the deterministic step contract.
    /// - Ingests any queued input patches.
    /// - Applies any queued runtime events.
    /// - Executes all registered subsystems in order.
    /// - Commits state metadata and samples all probes.
    /// Precondition: Runtime must be in Running state (not paused or terminated).
    void step();

    /// Executes a controlled number of steps in sequence.
    /// - stepCount: number of steps to advance (must be > 0).
    /// Each step follows the deterministic step contract.
    /// Precondition: Runtime must be in Running state.
    void controlledStep(std::uint32_t stepCount);

    /// Stops the simulation and transitions to Terminated state.
    /// No further stepping is possible after stop().
    void stop();

    /// Queues an input frame for processing in the next step.
    /// Input patches are ingested during the first phase of step().
    /// - inputFrame: contains cell updates to apply.
    void queueInput(RuntimeInputFrame inputFrame);

    /// Enqueues a runtime event to be processed in the next step.
    /// Events are applied during the second phase of step().
    /// - event: specifies the event type and parameters.
    void enqueueEvent(RuntimeEvent event);

    /// Creates a checkpoint of the current runtime state.
    /// - label: user-provided name for the checkpoint.
    /// - computeHash: if true, computes and stores the state hash (slower).
    /// Returns a RuntimeCheckpoint that can be restored later.
    [[nodiscard]] RuntimeCheckpoint createCheckpoint(const std::string& label, bool computeHash = true) const;

    /// Loads a checkpoint, restoring runtime to that state.
    /// - checkpoint: must be a checkpoint previously created by createCheckpoint().
    /// Precondition: checkpoint must be from the same model/config family.
    void loadCheckpoint(const RuntimeCheckpoint& checkpoint);

    /// Resets to a previous checkpoint, discarding all state since then.
    /// Equivalent to loadCheckpoint() but emphasizes the discarding semantics.
    void resetToCheckpoint(const RuntimeCheckpoint& checkpoint);

    /// Computes a hash of the current simulation state.
    /// Used for determinism validation and replay verification.
    /// Returns a 64-bit deterministic hash of the full state.
    [[nodiscard]] std::uint64_t computeStateHash() const noexcept;

    /// Validates determinism by comparing state hashes to reference hashes.
    /// - referenceHashes: vector of step-by-step hashes from a reference run.
    /// Returns true if all recorded hashes match the reference (perfect replay).
    [[nodiscard]] bool validateDeterminism(const std::vector<std::uint64_t>& referenceHashes) const noexcept;

    /// Returns the history of state hashes recorded each step.
    /// Chronologically ordered from first step (index 0) to current step.
    [[nodiscard]] const std::vector<std::uint64_t>& stateHashHistory() const noexcept { return stateHashHistory_; }

    // =========================================================================
    // State and Diagnostics Queries
    // =========================================================================

    /// Returns the current runtime status (Created, Running, Paused, Terminated).
    [[nodiscard]] RuntimeStatus status() const noexcept { return status_; }

    /// Returns whether the simulation is currently paused.
    /// Only meaningful if status() returns Running.
    [[nodiscard]] bool paused() const noexcept { return paused_; }

    /// Returns the current runtime snapshot.
    /// Immutable snapshot copy; safe for concurrent reads.
    /// Contains current step index, temporal metadata, and grid dimensions.
    [[nodiscard]] const RuntimeSnapshot& snapshot() const noexcept { return snapshot_; }

    /// Returns diagnostics from the most recent step (timing, effect counts, etc.).
    /// Populated after each step() call.
    [[nodiscard]] const StepDiagnostics& lastStepDiagnostics() const noexcept { return lastStepDiagnostics_; }

    /// Returns the chronological record of all runtime events that occurred.
    /// Useful for replay, debugging, and audit logging.
    [[nodiscard]] const std::vector<RuntimeEventRecord>& eventChronology() const noexcept { return eventChronology_; }

    /// Returns the manually recorded events (patches, parameter changes, etc.).
    /// Subset of eventChronology() containing only user-initiated changes.
    [[nodiscard]] const std::vector<ManualEventRecord>& manualEventLog() const noexcept { return eventQueue_.manualEvents(); }

    /// Returns count of queued scalar input patches waiting for ingestion.
    /// Used for diagnostics; count decreases by 1 at the start of each step.
    [[nodiscard]] std::size_t pendingInputPatchCount() const noexcept { return eventQueue_.pendingInputPatchCount(); }

    /// Returns count of queued runtime events waiting for event queue apply.
    /// Used for diagnostics; count decreases as events are processed.
    [[nodiscard]] std::size_t pendingEventCount() const noexcept { return eventQueue_.pendingEventCount(); }

    /// Returns count of scheduled perturbations that have not fully completed.
    /// Includes perturbations currently executing and waiting to execute.
    [[nodiscard]] std::size_t pendingPerturbationCount() const noexcept { return pendingPerturbations_.size(); }

    /// Returns count of recorded manual events in current runtime session/checkpoint state.
    /// Incremented when parameters are set or patches are applied.
    [[nodiscard]] std::size_t manualEventCount() const noexcept { return eventQueue_.manualEvents().size(); }

    /// Returns the probe manager for data collection and time-series queries.
    /// Thread-safe for read-only queries; modifications must be serialized.
    [[nodiscard]] const ProbeManager& probes() const noexcept { return probeManager_; }

    /// Returns the list of runtime-adjustable parameter controls defined by the model.
    /// Each parameter has name, bounds, units, and default value metadata.
    [[nodiscard]] std::vector<ParameterControl> parameterControls() const;

    /// Returns trace records for debugging and analysis (one record per step).
    /// Includes timing, event counts, and subsystem-specific diagnostics.
    [[nodiscard]] const std::vector<TraceRecord>& traceRecords() const noexcept { return observability_.records(); }

    /// Returns runtime performance metrics.
    /// Includes total execution time, steps completed, allocations, etc.
    [[nodiscard]] RuntimeMetrics metrics() const noexcept { return observability_.metrics(); }

    /// Returns the admission report from the last profile resolution.
    /// Indicates model compatibility and performance class.
    [[nodiscard]] const AdmissionReport& admissionReport() const;

    // =========================================================================
    // State Modification
    // =========================================================================

    /// Sets a runtime parameter value by name.
    /// - parameterName: must exist in parameterControls().
    /// - value: must be within the parameter's bounds (validated).
    /// - note: optional description for logging.
    /// - message: output for diagnostic information.
    /// Returns true on success; false if parameter unknown or out of range.
    [[nodiscard]] bool setParameterValue(const std::string& parameterName, float value, std::string note, std::string& message);

    /// Applies a manual patch to a specific cell in a variable.
    /// - variableName: must exist in the model.
    /// - cell: if provided, patches a single cell; if empty, patches entire grid.
    /// - newValue: new value to assign (validated by field constraints).
    /// - note: optional description for logging.
    /// - message: output for diagnostic information.
    /// Returns true on success; false if variable unknown or cell out of bounds.
    [[nodiscard]] bool applyManualPatch(const std::string& variableName, std::optional<Cell> cell, float newValue, std::string note, std::string& message);

    /// Undoes the most recent manual patch.
    /// Restores values overwritten by the most recent applyManualPatch().
    /// - message: output for diagnostic information.
    /// Returns true on success; false if no patch history or undo fails.
    [[nodiscard]] bool undoLastManualPatch(std::string& message);

    /// Enqueues a perturbation to be applied during simulation.
    /// Perturbations are scheduled events that modify state based on triggers.
    /// - perturbation: must define trigger and action (validated).
    /// - message: output for diagnostic information.
    /// Returns true on success; false if perturbation invalid or queue full.
    [[nodiscard]] bool enqueuePerturbation(const PerturbationSpec& perturbation, std::string& message);

    /// Adds a data collection probe.
    /// Probes sample named variables at each step for time-series analysis.
    /// - definition: must have unique probeId and valid variable references.
    /// - message: output for diagnostic information.
    /// Returns true on success; false if probe already exists or definition invalid.
    [[nodiscard]] bool addProbe(const ProbeDefinition& definition, std::string& message);

    /// Removes a probe by identifier.
    /// - probeId: must match an existing probe.
    /// - message: output for diagnostic information.
    /// Returns true on success; false if probe not found.
    [[nodiscard]] bool removeProbe(const std::string& probeId, std::string& message);

    /// Clears all registered probes and discards their data.
    /// Equivalent to removing probes individually but faster.
    void clearProbes() noexcept;

private:
    // Allocates runtime fields based on the loaded model specification.
    void allocateRuntimeFieldsFromModelSpec();
    // Initializes the parameter control list from model specification.
    void initializeParameterControls();
    // Internal step implementation with runtime control flag.
    void stepImpl(bool controlledByRuntimeControl);
    // Writes a trace record to the observability pipeline.
    void trace(
        TraceChannel channel,
        std::string name,
        std::string detail,
        std::uint64_t payloadFingerprint = 0,
        std::uint64_t stepIndexOverride = std::numeric_limits<std::uint64_t>::max());
    // Applies an input frame to the simulation state.
    [[nodiscard]] std::uint64_t applyInputFrame(const RuntimeInputFrame& inputFrame);
    // Applies an event to the simulation state.
    [[nodiscard]] std::uint64_t applyEvent(const RuntimeEvent& event, std::uint64_t eventOrdinal);
    // Builds an undo event for a manual patch.
    [[nodiscard]] RuntimeEvent buildUndoEvent(const ManualEventRecord& manualEvent) const;
    // Builds an event for applying a perturbation.
    [[nodiscard]] RuntimeEvent buildPerturbationEvent(const PerturbationSpec& perturbation, std::uint64_t appliedStep) const;
    // Samples the current value of a variable at a cell.
    [[nodiscard]] bool sampleCurrentValue(const std::string& variableName, std::optional<Cell> cell, float& outValue, std::string& message) const;
    // Collects the set of writable variables from patch specifications.
    [[nodiscard]] static std::vector<std::string> collectWritableVariables(const std::vector<ScalarWritePatch>& patches);
    // Computes a stable hash for a set of ordered strings.
    [[nodiscard]] static std::string stableHashForStringSet(const std::vector<std::string>& orderedValues);

    RuntimeConfig config_;
    RuntimeStatus status_ = RuntimeStatus::Created;
    ProfileResolver profileResolver_;
    RunSignatureService runSignatureService_;
    InteractionCoordinator interactionCoordinator_;
    AdmissionReport admissionReport_{};
    ModelProfile resolvedProfile_;
    StateStore stateStore_;
    Scheduler scheduler_;
    NumericGuardrailPolicy runtimeGuardrailPolicy_{};
    RuntimeSnapshot snapshot_;
    bool paused_ = false;
    std::uint64_t traceSequence_ = 0;
    EventQueue eventQueue_;
    std::unordered_map<std::string, ParameterControl> parameterControls_;
    std::vector<PerturbationSpec> pendingPerturbations_;
    std::vector<RuntimeEventRecord> eventChronology_;
    std::vector<std::uint64_t> stateHashHistory_;
    ProbeManager probeManager_;
    ObservabilityPipeline observability_;
    StepDiagnostics lastStepDiagnostics_{};
};

} // namespace ws
