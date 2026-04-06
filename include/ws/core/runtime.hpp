#pragma once

#include "ws/core/event_queue.hpp"
#include "ws/core/interactions.hpp"
#include "ws/core/observability.hpp"
#include "ws/core/probe.hpp"
#include "ws/core/profile.hpp"
#include "ws/core/run_signature.hpp"
#include "ws/core/scheduler.hpp"
#include "ws/core/state_store.hpp"

#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ws {

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

struct ConwayParams {
    std::string targetVariable = "initialization.conway.target";
    float aliveProbability = 0.5f;
    float aliveValue = 1.0f;
    float deadValue = 0.0f;
    int smoothingPasses = 0;
};

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

struct WaveParams {
    std::string targetVariable = "initialization.waves.target";
    float baseline = 0.0f;
    float dropAmplitude = 1.0f;
    float dropRadius = 5.0f;
    int dropCount = 1;
    float dropJitter = 0.35f;
    float ringFrequency = 1.0f;
};

struct VoronoiParams {
    std::string targetVariable = "initialization.voronoi.target";
    int seedCount = 12;
    float smoothing = 0.3f;
    float colorScale = 1.0f;
    float jitter = 0.5f;
};

struct ClusteringParams {
    std::string targetVariable = "initialization.clustering.target";
    int clusterCount = 8;
    float clusterRadius = 20.0f;
    float clusterIntensity = 0.8f;
    float clusterDecay = 0.6f;
    float clusterSpread = 0.4f;
};

struct SparseRandomParams {
    std::string targetVariable = "initialization.sparse.target";
    float fillDensity = 0.15f;
    float minValue = 0.2f;
    float maxValue = 0.9f;
    bool clusterSparse = false;
    float clusterRadius = 8.0f;
};

struct GradientFieldParams {
    std::string targetVariable = "initialization.gradient.target";
    int directionMode = 0;
    float gradientScale = 1.0f;
    float centerX = 0.5f;
    float centerY = 0.5f;
    float perturbation = 0.1f;
};

struct CheckerboardParams {
    std::string targetVariable = "initialization.checkerboard.target";
    int cellSize = 4;
    float darkValue = 0.2f;
    float lightValue = 0.8f;
    float blurRadius = 0.0f;
    bool diagonal = false;
};

struct RadialPatternParams {
    std::string targetVariable = "initialization.radial.target";
    float centerX = 0.5f;
    float centerY = 0.5f;
    int ringCount = 6;
    float innerValue = 0.3f;
    float outerValue = 0.7f;
    float falloff = 1.0f;
};

struct MultiScaleParams {
    std::string targetVariable = "initialization.multiscale.target";
    int scaleCount = 3;
    float baseFrequency = 0.5f;
    float frequencyScale = 2.0f;
    float amplitudeScale = 0.5f;
    float blendMode = 0.5f;
};

struct DiffusionLimitParams {
    std::string targetVariable = "initialization.diffusion.target";
    int seedCount = 5;
    float growthRate = 0.2f;
    float anisotropy = 0.0f;
    float colorVariance = 0.3f;
    float randomWalk = 0.15f;
};

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

struct ParameterControl {
    std::string name;
    std::string targetVariable;
    float value = 0.0f;
    float minValue = -1.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    std::string units = "1";
};

struct ModelExecutionSpec {
    std::vector<std::string> cellScalarVariableIds;
    std::vector<std::string> stageOrder;
    std::vector<std::string> conservedVariables;
    std::unordered_map<std::string, std::string> semanticFieldAliases;
    std::optional<BoundaryMode> preferredBoundaryMode;
};

struct ModelDisplaySpec {
    std::unordered_map<std::string, std::vector<std::string>> fieldTags;
};

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

enum class PerturbationType : std::uint8_t {
    Gaussian = 0,
    Rectangle = 1,
    Sine = 2,
    WhiteNoise = 3,
    Gradient = 4
};

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

struct RuntimeSnapshot {
    RunSignature runSignature;
    std::uint64_t stateHash = 0;
    StateHeader stateHeader{};
    std::uint64_t payloadBytes = 0;
    ReproducibilityClass reproducibilityClass = ReproducibilityClass::Strict;
    StabilityDiagnostics stabilityDiagnostics{};
};

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

class Runtime {
public:
    explicit Runtime(RuntimeConfig config);

    void registerSubsystem(std::shared_ptr<ISubsystem> subsystem);
    void selectProfile(ProfileResolverInput profileInput);
    void updateGuardrailPolicy(NumericGuardrailPolicy guardrailPolicy);
    void start();
    void pause();
    void resume();
    void step();
    void controlledStep(std::uint32_t stepCount);
    void stop();
    void queueInput(RuntimeInputFrame inputFrame);
    void enqueueEvent(RuntimeEvent event);
    [[nodiscard]] RuntimeCheckpoint createCheckpoint(const std::string& label, bool computeHash = true) const;
    void loadCheckpoint(const RuntimeCheckpoint& checkpoint);
    void resetToCheckpoint(const RuntimeCheckpoint& checkpoint);
    [[nodiscard]] std::uint64_t computeStateHash() const noexcept;
    [[nodiscard]] bool validateDeterminism(const std::vector<std::uint64_t>& referenceHashes) const noexcept;
    [[nodiscard]] const std::vector<std::uint64_t>& stateHashHistory() const noexcept { return stateHashHistory_; }

    [[nodiscard]] RuntimeStatus status() const noexcept { return status_; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] const RuntimeSnapshot& snapshot() const noexcept { return snapshot_; }
    [[nodiscard]] const StepDiagnostics& lastStepDiagnostics() const noexcept { return lastStepDiagnostics_; }
    [[nodiscard]] const std::vector<RuntimeEventRecord>& eventChronology() const noexcept { return eventChronology_; }
    [[nodiscard]] const std::vector<ManualEventRecord>& manualEventLog() const noexcept { return eventQueue_.manualEvents(); }
    [[nodiscard]] const ProbeManager& probes() const noexcept { return probeManager_; }
    [[nodiscard]] std::vector<ParameterControl> parameterControls() const;
    [[nodiscard]] const std::vector<TraceRecord>& traceRecords() const noexcept { return observability_.records(); }
    [[nodiscard]] RuntimeMetrics metrics() const noexcept { return observability_.metrics(); }
    [[nodiscard]] const AdmissionReport& admissionReport() const;

    bool setParameterValue(const std::string& parameterName, float value, std::string note, std::string& message);
    bool applyManualPatch(const std::string& variableName, std::optional<Cell> cell, float newValue, std::string note, std::string& message);
    bool undoLastManualPatch(std::string& message);
    bool enqueuePerturbation(const PerturbationSpec& perturbation, std::string& message);
    bool addProbe(const ProbeDefinition& definition, std::string& message);
    bool removeProbe(const std::string& probeId, std::string& message);
    void clearProbes() noexcept;

private:
    void allocateRuntimeFieldsFromModelSpec();
    void initializeParameterControls();
    void stepImpl(bool controlledByRuntimeControl);
    void trace(
        TraceChannel channel,
        std::string name,
        std::string detail,
        std::uint64_t payloadFingerprint = 0,
        std::uint64_t stepIndexOverride = std::numeric_limits<std::uint64_t>::max());
    [[nodiscard]] std::uint64_t applyInputFrame(const RuntimeInputFrame& inputFrame);
    [[nodiscard]] std::uint64_t applyEvent(const RuntimeEvent& event, std::uint64_t eventOrdinal);
    [[nodiscard]] RuntimeEvent buildUndoEvent(const ManualEventRecord& manualEvent) const;
    [[nodiscard]] RuntimeEvent buildPerturbationEvent(const PerturbationSpec& perturbation, std::uint64_t appliedStep) const;
    [[nodiscard]] bool sampleCurrentValue(const std::string& variableName, std::optional<Cell> cell, float& outValue, std::string& message) const;
    [[nodiscard]] static std::vector<std::string> collectWritableVariables(const std::vector<ScalarWritePatch>& patches);
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
