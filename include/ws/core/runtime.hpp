#pragma once

#include "ws/core/event_queue.hpp"
#include "ws/core/interactions.hpp"
#include "ws/core/observability.hpp"
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

struct WorldGenerationParams {
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
    WorldGenerationParams worldGen{};
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
    [[nodiscard]] std::vector<ParameterControl> parameterControls() const;
    [[nodiscard]] const std::vector<TraceRecord>& traceRecords() const noexcept { return observability_.records(); }
    [[nodiscard]] RuntimeMetrics metrics() const noexcept { return observability_.metrics(); }
    [[nodiscard]] const AdmissionReport& admissionReport() const;

    bool setParameterValue(const std::string& parameterName, float value, std::string note, std::string& message);
    bool applyManualPatch(const std::string& variableName, std::optional<Cell> cell, float newValue, std::string note, std::string& message);
    bool undoLastManualPatch(std::string& message);
    bool enqueuePerturbation(const PerturbationSpec& perturbation, std::string& message);

private:
    void allocateCanonicalFields();
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
    ObservabilityPipeline observability_;
    StepDiagnostics lastStepDiagnostics_{};
};

} // namespace ws
