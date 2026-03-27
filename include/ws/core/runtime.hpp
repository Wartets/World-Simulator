#pragma once

#include "ws/core/interactions.hpp"
#include "ws/core/observability.hpp"
#include "ws/core/profile.hpp"
#include "ws/core/run_signature.hpp"
#include "ws/core/scheduler.hpp"
#include "ws/core/state_store.hpp"

#include <deque>
#include <limits>
#include <memory>
#include <string>
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

struct ScalarWritePatch {
    std::string variableName;
    Cell cell;
    float value = 0.0f;
};

struct RuntimeInputFrame {
    std::vector<ScalarWritePatch> scalarPatches;
};

struct RuntimeEvent {
    std::string eventName;
    std::vector<ScalarWritePatch> scalarPatches;
};

struct RuntimeEventRecord {
    std::uint64_t stepIndex = 0;
    std::uint64_t ordinalInStep = 0;
    RuntimeEvent event;
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

    [[nodiscard]] RuntimeStatus status() const noexcept { return status_; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] const RuntimeSnapshot& snapshot() const noexcept { return snapshot_; }
    [[nodiscard]] const StepDiagnostics& lastStepDiagnostics() const noexcept { return lastStepDiagnostics_; }
    [[nodiscard]] const std::vector<RuntimeEventRecord>& eventChronology() const noexcept { return eventChronology_; }
    [[nodiscard]] const std::vector<TraceRecord>& traceRecords() const noexcept { return observability_.records(); }
    [[nodiscard]] RuntimeMetrics metrics() const noexcept { return observability_.metrics(); }
    [[nodiscard]] const AdmissionReport& admissionReport() const;

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
    std::deque<RuntimeInputFrame> pendingInputs_;
    std::deque<RuntimeEvent> pendingEvents_;
    std::vector<RuntimeEventRecord> eventChronology_;
    ObservabilityPipeline observability_;
    StepDiagnostics lastStepDiagnostics_{};
};

} // namespace ws
