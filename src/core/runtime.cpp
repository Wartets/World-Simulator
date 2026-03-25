#include "ws/core/runtime.hpp"

#include "ws/core/determinism.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

namespace ws {

namespace {

std::string tierToString(const ModelTier tier) {
    return toString(tier);
}

float smoothStep(const float t) {
    const float x = std::clamp(t, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

std::uint64_t mix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

float hash01(const std::uint64_t seed, const int x, const int y) {
    std::uint64_t h = DeterministicHash::combine(seed, DeterministicHash::hashPod(x));
    h = DeterministicHash::combine(h, DeterministicHash::hashPod(y));
    h = mix64(h);
    const std::uint32_t top24 = static_cast<std::uint32_t>((h >> 40u) & 0xFFFFFFu);
    return static_cast<float>(top24) / static_cast<float>(0xFFFFFFu);
}

float valueNoise2D(const std::uint64_t seed, const float x, const float y) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;

    const float tx = smoothStep(x - static_cast<float>(x0));
    const float ty = smoothStep(y - static_cast<float>(y0));

    const float n00 = hash01(seed, x0, y0);
    const float n10 = hash01(seed, x1, y0);
    const float n01 = hash01(seed, x0, y1);
    const float n11 = hash01(seed, x1, y1);

    const float nx0 = n00 + (n10 - n00) * tx;
    const float nx1 = n01 + (n11 - n01) * tx;
    return nx0 + (nx1 - nx0) * ty;
}

float fbm2D(
    const std::uint64_t seed,
    const float x,
    const float y,
    const int octaves,
    const float lacunarity,
    const float gain) {
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    float amplitudeSum = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        const std::uint64_t octaveSeed = DeterministicHash::combine(seed, static_cast<std::uint64_t>(i + 1));
        total += amplitude * valueNoise2D(octaveSeed, x * frequency, y * frequency);
        amplitudeSum += amplitude;
        frequency *= lacunarity;
        amplitude *= gain;
    }

    if (amplitudeSum <= 1e-6f) {
        return 0.5f;
    }
    return std::clamp(total / amplitudeSum, 0.0f, 1.0f);
}

} // namespace

Runtime::Runtime(RuntimeConfig config)
    : config_(std::move(config)),
    stateStore_(config_.grid, config_.boundaryMode, config_.topologyBackend, config_.memoryLayoutPolicy),
    runtimeGuardrailPolicy_(config_.guardrailPolicy),
      snapshot_{RunSignature(
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
                    0),
                0,
                StateHeader{},
                0} {
    config_.grid.validate();
    scheduler_.setExecutionPolicyMode(config_.executionPolicyMode);
}

void Runtime::registerSubsystem(std::shared_ptr<ISubsystem> subsystem) {
    if (status_ != RuntimeStatus::Created) {
        throw std::runtime_error("Subsystem registration is only allowed before runtime start");
    }
    scheduler_.registerSubsystem(std::move(subsystem));
}

void Runtime::selectProfile(ProfileResolverInput profileInput) {
    if (status_ != RuntimeStatus::Created && status_ != RuntimeStatus::Terminated) {
        throw std::runtime_error("Profile selection is only allowed in Created or Terminated state");
    }
    config_.profileInput = std::move(profileInput);
    trace(
        TraceChannel::Configuration,
        "runtime.config.profile_selected",
        "profile resolver input updated",
        DeterministicHash::hashPod(config_.profileInput.requestedSubsystemTiers.size()));
}

void Runtime::updateGuardrailPolicy(NumericGuardrailPolicy guardrailPolicy) {
    runtimeGuardrailPolicy_ = std::move(guardrailPolicy);
    trace(
        TraceChannel::Configuration,
        "runtime.config.guardrails_updated",
        "numeric guardrail policy updated",
        DeterministicHash::hashPod(runtimeGuardrailPolicy_.maxAbsDeltaPerStep));
}

void Runtime::start() {
    if (status_ != RuntimeStatus::Created) {
        throw std::runtime_error("Runtime start can only be called from Created state");
    }

    try {
        resolvedProfile_ = profileResolver_.resolve(config_.profileInput);
        paused_ = false;

        admissionReport_ = interactionCoordinator_.buildAdmissionReport(
            resolvedProfile_,
            config_.temporalPolicy,
            scheduler_.registeredSubsystems());
        if (!admissionReport_.admitted) {
            throw std::runtime_error("Profile admission failed: " + admissionReport_.diagnosticsText());
        }
        scheduler_.setAdmissionReport(admissionReport_);

        allocateCanonicalFields();

        {
            StateStore::WriteSession seedWriter(stateStore_, "runtime_seed_pipeline", {
                "terrain_elevation_h",
                "surface_water_w",
                "temperature_T",
                "humidity_q",
                "wind_u",
                "climate_index_c",
                "fertility_phi",
                "vegetation_v",
                "resource_stock_r",
                "event_signal_e",
                "event_water_delta",
                "event_temperature_delta",
                "bootstrap_marker",
                "seed_probe"});

            const float centerX = static_cast<float>(config_.grid.width - 1) * 0.5f;
            const float centerY = static_cast<float>(config_.grid.height - 1) * 0.5f;
            const float invW = 1.0f / static_cast<float>(std::max<std::uint32_t>(1, config_.grid.width - 1));
            const float invH = 1.0f / static_cast<float>(std::max<std::uint32_t>(1, config_.grid.height - 1));

            const float baseFreq = std::max(0.15f, config_.worldGen.terrainBaseFrequency);
            const float detailFreq = std::max(0.25f, config_.worldGen.terrainDetailFrequency);
            const float warpStrength = std::clamp(config_.worldGen.terrainWarpStrength, 0.0f, 2.0f);
            const float terrainAmplitude = std::clamp(config_.worldGen.terrainAmplitude, 0.1f, 3.0f);
            const float ridgeMix = std::clamp(config_.worldGen.terrainRidgeMix, 0.0f, 1.0f);
            const float seaLevel = std::clamp(config_.worldGen.seaLevel, 0.0f, 1.0f);
            const float polarCooling = std::clamp(config_.worldGen.polarCooling, 0.0f, 1.5f);
            const float humidityFromWater = std::clamp(config_.worldGen.humidityFromWater, 0.0f, 1.5f);
            const float biomeNoiseStrength = std::clamp(config_.worldGen.biomeNoiseStrength, 0.0f, 1.0f);

            const std::uint64_t noiseSeedA = DeterministicHash::combine(config_.seed, 0xA1C31A5DULL);
            const std::uint64_t noiseSeedB = DeterministicHash::combine(config_.seed, 0xB73F42D1ULL);
            const std::uint64_t noiseSeedC = DeterministicHash::combine(config_.seed, 0xCA1F0E91ULL);
            const std::uint64_t noiseSeedD = DeterministicHash::combine(config_.seed, 0xD4E8B8D3ULL);
            const std::uint64_t noiseSeedE = DeterministicHash::combine(config_.seed, 0xE713944BULL);

            for (std::uint32_t y = 0; y < config_.grid.height; ++y) {
                for (std::uint32_t x = 0; x < config_.grid.width; ++x) {
                    const float nx = static_cast<float>(x) * invW;
                    const float ny = static_cast<float>(y) * invH;

                    const float dx = static_cast<float>(x) - centerX;
                    const float dy = static_cast<float>(y) - centerY;
                    const float radius = std::sqrt((dx * dx) + (dy * dy)) /
                        std::max(1.0f, std::sqrt(centerX * centerX + centerY * centerY));

                    const float warpX = fbm2D(noiseSeedA, nx * detailFreq, ny * detailFreq, 3, 2.0f, 0.5f);
                    const float warpY = fbm2D(noiseSeedB, nx * detailFreq, ny * detailFreq, 3, 2.0f, 0.5f);
                    const float domainX = nx + (warpX - 0.5f) * warpStrength;
                    const float domainY = ny + (warpY - 0.5f) * warpStrength;

                    const float continental = fbm2D(noiseSeedC, domainX * baseFreq, domainY * baseFreq, 5, 2.0f, 0.54f);
                    const float detail = fbm2D(noiseSeedD, domainX * detailFreq, domainY * detailFreq, 4, 2.3f, 0.56f);
                    const float ridge = 1.0f - std::abs(2.0f * detail - 1.0f);
                    const float biomeNoise = fbm2D(noiseSeedE, nx * (detailFreq * 0.5f), ny * (detailFreq * 0.5f), 3, 2.0f, 0.6f);

                    const float macroShape = std::clamp(1.0f - radius * 0.85f, 0.0f, 1.0f);
                    const float elevationRaw = std::clamp(
                        (continental * 0.62f) +
                        (ridge * 0.28f * ridgeMix) +
                        (macroShape * 0.10f),
                        0.0f,
                        1.0f);
                    const float elevation = std::clamp(0.5f + (elevationRaw - 0.5f) * terrainAmplitude, 0.0f, 1.0f);

                    const float waterBasin = std::clamp((seaLevel - elevation) * 2.3f + 0.5f, 0.0f, 1.0f);
                    const float coastalNoise = (biomeNoise - 0.5f) * 0.25f;
                    const float water = std::clamp(waterBasin + coastalNoise, 0.0f, 1.0f);

                    const float latitudinal = 1.0f - std::abs(2.0f * ny - 1.0f);
                    const float heightCooling = std::clamp(elevation * 0.45f, 0.0f, 1.0f);
                    const float temperature = std::clamp(
                        0.25f +
                            0.55f * latitudinal * (1.0f - 0.35f * polarCooling) +
                            0.20f * (1.0f - heightCooling) +
                            (biomeNoise - 0.5f) * biomeNoiseStrength,
                        0.0f,
                        1.0f);

                    const float humidity = std::clamp(
                        0.20f +
                            humidityFromWater * 0.45f * water +
                            0.25f * (1.0f - std::abs(temperature - 0.55f)) +
                            (biomeNoise - 0.5f) * 0.2f,
                        0.0f,
                        1.0f);

                    const float wind = std::clamp(
                        0.25f +
                            0.45f * fbm2D(noiseSeedB, nx * 4.0f + 11.0f, ny * 4.0f + 7.0f, 3, 2.0f, 0.6f) +
                            0.30f * std::abs(temperature - 0.5f),
                        0.0f,
                        1.0f);

                    const float climate = std::clamp(0.45f * temperature + 0.55f * humidity, 0.0f, 1.0f);
                    const float fertility = std::clamp(0.25f + 0.55f * humidity + 0.20f * (1.0f - std::abs(elevation - seaLevel)), 0.0f, 1.0f);
                    const float vegetation = std::clamp(0.25f + 0.55f * fertility * humidity, 0.0f, 1.0f);
                    const float resources = std::clamp(0.15f + 0.55f * vegetation + 0.15f * water + 0.15f * biomeNoise, 0.0f, 1.0f);
                    const float eventSignal = std::clamp(0.15f + 0.85f * fbm2D(noiseSeedA, domainX * 10.0f + 3.0f, domainY * 10.0f + 5.0f, 3, 2.1f, 0.58f), 0.0f, 1.0f);

                    seedWriter.setScalar("terrain_elevation_h", Cell{x, y}, elevation);
                    seedWriter.setScalar("surface_water_w", Cell{x, y}, water);
                    seedWriter.setScalar("temperature_T", Cell{x, y}, temperature);
                    seedWriter.setScalar("humidity_q", Cell{x, y}, humidity);
                    seedWriter.setScalar("wind_u", Cell{x, y}, wind);
                    seedWriter.setScalar("climate_index_c", Cell{x, y}, climate);
                    seedWriter.setScalar("fertility_phi", Cell{x, y}, fertility);
                    seedWriter.setScalar("vegetation_v", Cell{x, y}, vegetation);
                    seedWriter.setScalar("resource_stock_r", Cell{x, y}, resources);
                    seedWriter.setScalar("event_signal_e", Cell{x, y}, eventSignal);
                    seedWriter.setScalar("event_water_delta", Cell{x, y}, 0.0f);
                    seedWriter.setScalar("event_temperature_delta", Cell{x, y}, 0.0f);
                    seedWriter.setScalar("bootstrap_marker", Cell{x, y}, biomeNoise);
                    seedWriter.setScalar("seed_probe", Cell{x, y}, continental);
                }
            }
        }

        StateHeader header{};
        header.stepIndex = 0;
        header.timestampTicks = 0;
        header.status = RuntimeStatus::Initialized;
        stateStore_.setHeader(header);

        scheduler_.initialize(stateStore_, resolvedProfile_);

        const auto activeSubsystems = scheduler_.activeSubsystemNames();
        const std::string subsystemHash = stableHashForStringSet(activeSubsystems);

        std::string profileDigestData;
        for (const auto& [subsystem, tier] : resolvedProfile_.subsystemTiers) {
            profileDigestData += subsystem + ":" + tierToString(tier) + ";";
        }

        const std::string initializationHash = std::to_string(DeterministicHash::hashString(profileDigestData));
        const std::string eventTimelineHash = std::to_string(DeterministicHash::hashString(
            "baseline:no-events;interaction_graph=" + std::to_string(DeterministicHash::hashString(admissionReport_.serializedGraph)) +
            ";admission_fingerprint=" + std::to_string(admissionReport_.fingerprint)));

        snapshot_.runSignature = runSignatureService_.create(RunIdentityInput{
            .globalSeed = config_.seed,
            .initializationParameterHash = initializationHash,
            .grid = config_.grid,
            .boundaryMode = config_.boundaryMode,
            .unitRegime = config_.unitRegime,
            .temporalPolicy = config_.temporalPolicy,
            .eventTimelineHash = eventTimelineHash,
            .activeSubsystemSetHash = subsystemHash,
            .profile = resolvedProfile_});

        trace(
            TraceChannel::Configuration,
            "runtime.start",
            "runtime admitted and started; execution_policy=" + toString(config_.executionPolicyMode),
            admissionReport_.fingerprint,
            0);

        header.status = RuntimeStatus::Running;
        stateStore_.setHeader(header);
        snapshot_.stateHeader = stateStore_.header();
        snapshot_.stateHash = stateStore_.stateHash();
        snapshot_.reproducibilityClass = admissionReport_.reproducibilityClass;
        snapshot_.payloadBytes = stateStore_.createSnapshot(
            snapshot_.runSignature.identityHash(),
            resolvedProfile_.fingerprint(),
            "runtime_start_baseline")
                                     .payloadBytes;
        status_ = RuntimeStatus::Running;
    } catch (...) {
        status_ = RuntimeStatus::Error;
        StateHeader header = stateStore_.header();
        header.status = RuntimeStatus::Error;
        stateStore_.setHeader(header);
        throw;
    }
}

void Runtime::step() {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime step requires Running state");
    }
    if (paused_) {
        throw std::runtime_error("Runtime is paused; use control surface stepping");
    }

    stepImpl(false);
}

void Runtime::pause() {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime pause requires Running state");
    }
    paused_ = true;
    trace(TraceChannel::Control, "control.pause", "runtime paused");
}

void Runtime::resume() {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime resume requires Running state");
    }
    paused_ = false;
    trace(TraceChannel::Control, "control.resume", "runtime resumed");
}

void Runtime::controlledStep(const std::uint32_t stepCount) {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Controlled stepping requires Running state");
    }
    if (stepCount == 0) {
        return;
    }

    trace(
        TraceChannel::Control,
        "control.step",
        "runtime controlled stepping",
        DeterministicHash::hashPod(stepCount));
    for (std::uint32_t i = 0; i < stepCount; ++i) {
        stepImpl(true);
    }
}

void Runtime::stepImpl(const bool controlledByRuntimeControl) {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime step requires Running state");
    }

    StepDiagnostics diagnostics;
    diagnostics.orderingLog.push_back("pipeline:input_ingest");

    while (!pendingInputs_.empty()) {
        diagnostics.inputPatchesApplied += applyInputFrame(pendingInputs_.front());
        pendingInputs_.pop_front();
    }

    diagnostics.orderingLog.push_back("pipeline:event_queue_apply");
    std::uint64_t eventOrdinal = 0;
    while (!pendingEvents_.empty()) {
        RuntimeEvent event = pendingEvents_.front();
        diagnostics.eventPatchesApplied += applyEvent(event, eventOrdinal);
        diagnostics.eventsApplied += 1;
        pendingEvents_.pop_front();
        eventChronology_.push_back(RuntimeEventRecord{
            stateStore_.header().stepIndex,
            eventOrdinal,
            std::move(event)});
        eventOrdinal += 1;
    }

    StepDiagnostics schedulerDiagnostics = scheduler_.step(
        stateStore_,
        resolvedProfile_,
        config_.temporalPolicy,
        runtimeGuardrailPolicy_,
        stateStore_.header().stepIndex);
    scheduler_.validateObservedDataFlow();

    diagnostics.orderingLog.insert(
        diagnostics.orderingLog.end(),
        schedulerDiagnostics.orderingLog.begin(),
        schedulerDiagnostics.orderingLog.end());
    diagnostics.stabilityAlerts = std::move(schedulerDiagnostics.stabilityAlerts);
    diagnostics.constraintViolations = std::move(schedulerDiagnostics.constraintViolations);
    diagnostics.reproducibilityClass = schedulerDiagnostics.reproducibilityClass;
    diagnostics.stability = schedulerDiagnostics.stability;
    lastStepDiagnostics_ = std::move(diagnostics);

    StateHeader header = stateStore_.header();
    header.stepIndex += 1;
    header.timestampTicks += 1;
    stateStore_.setHeader(header);

    snapshot_.stateHeader = header;
    snapshot_.stateHash = stateStore_.stateHash();
    snapshot_.reproducibilityClass = lastStepDiagnostics_.reproducibilityClass;
    snapshot_.stabilityDiagnostics = lastStepDiagnostics_.stability;
    snapshot_.payloadBytes = stateStore_.createSnapshot(
        snapshot_.runSignature.identityHash(),
        resolvedProfile_.fingerprint(),
        "runtime_step")
                                 .payloadBytes;

    trace(
        TraceChannel::Scheduler,
        "runtime.step.commit",
        controlledByRuntimeControl ? "controlled_step" : "free_step",
        snapshot_.stateHash,
        header.stepIndex);
}

void Runtime::queueInput(RuntimeInputFrame inputFrame) {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime input queue requires Running state");
    }
    for (const auto& patch : inputFrame.scalarPatches) {
        trace(
            TraceChannel::Input,
            "runtime.input.patch.queued",
            "input patch queued for variable=" + patch.variableName,
            DeterministicHash::hashString(patch.variableName));
    }
    pendingInputs_.push_back(std::move(inputFrame));
}

void Runtime::enqueueEvent(RuntimeEvent event) {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime event queue requires Running state");
    }
    trace(
        TraceChannel::Event,
        "runtime.event.queued",
        "runtime event queued name=" + event.eventName,
        DeterministicHash::hashString(event.eventName));
    pendingEvents_.push_back(std::move(event));
}

void Runtime::stop() {
    if (status_ == RuntimeStatus::Terminated) {
        return;
    }

    if (status_ != RuntimeStatus::Running && status_ != RuntimeStatus::Error && status_ != RuntimeStatus::Initialized) {
        throw std::runtime_error("Runtime stop called from invalid state");
    }

    StateHeader header = stateStore_.header();
    header.status = RuntimeStatus::Terminated;
    stateStore_.setHeader(header);

    snapshot_.stateHeader = header;
    snapshot_.stateHash = stateStore_.stateHash();
    snapshot_.reproducibilityClass = lastStepDiagnostics_.reproducibilityClass;
    snapshot_.stabilityDiagnostics = lastStepDiagnostics_.stability;
    snapshot_.payloadBytes = stateStore_.createSnapshot(
        snapshot_.runSignature.identityHash(),
        resolvedProfile_.fingerprint(),
        "runtime_stop")
                                 .payloadBytes;
    trace(TraceChannel::Control, "runtime.stop", "runtime terminated", snapshot_.stateHash);
    status_ = RuntimeStatus::Terminated;
}

RuntimeCheckpoint Runtime::createCheckpoint(const std::string& label) const {
    if (status_ != RuntimeStatus::Running && status_ != RuntimeStatus::Initialized) {
        throw std::runtime_error("Runtime checkpoint creation requires Initialized or Running state");
    }

    const std::uint64_t profileFingerprint = resolvedProfile_.fingerprint();
    RuntimeCheckpoint checkpoint{
        snapshot_.runSignature,
        profileFingerprint,
        stateStore_.createSnapshot(
            snapshot_.runSignature.identityHash(),
            profileFingerprint,
            label)};
    return checkpoint;
}

void Runtime::loadCheckpoint(const RuntimeCheckpoint& checkpoint) {
    if (status_ != RuntimeStatus::Running && status_ != RuntimeStatus::Initialized) {
        throw std::runtime_error("Runtime checkpoint load requires Initialized or Running state");
    }

    if (checkpoint.runSignature.identityHash() != snapshot_.runSignature.identityHash()) {
        throw std::invalid_argument("Checkpoint run signature identity hash does not match active runtime");
    }

    if (checkpoint.profileFingerprint != resolvedProfile_.fingerprint()) {
        throw std::invalid_argument("Checkpoint profile fingerprint does not match active runtime profile");
    }

    stateStore_.loadSnapshot(
        checkpoint.stateSnapshot,
        checkpoint.runSignature.identityHash(),
        checkpoint.profileFingerprint);

    snapshot_.stateHeader = stateStore_.header();
    snapshot_.stateHash = stateStore_.stateHash();
    snapshot_.reproducibilityClass = admissionReport_.reproducibilityClass;
    snapshot_.payloadBytes = checkpoint.stateSnapshot.payloadBytes;

    trace(
        TraceChannel::Replay,
        "runtime.checkpoint.loaded",
        "checkpoint loaded label=" + checkpoint.stateSnapshot.checkpointLabel,
        checkpoint.stateSnapshot.stateHash,
        checkpoint.stateSnapshot.header.stepIndex);
}

void Runtime::resetToCheckpoint(const RuntimeCheckpoint& checkpoint) {
    trace(
        TraceChannel::Control,
        "control.reset",
        "runtime reset requested",
        checkpoint.stateSnapshot.stateHash,
        checkpoint.stateSnapshot.header.stepIndex);
    loadCheckpoint(checkpoint);
    pendingInputs_.clear();
    pendingEvents_.clear();
}

void Runtime::allocateCanonicalFields() {
    const std::vector<VariableSpec> specs = {
        {0, "terrain_elevation_h"},
        {1, "surface_water_w"},
        {2, "temperature_T"},
        {3, "humidity_q"},
        {4, "wind_u"},
        {5, "climate_index_c"},
        {6, "fertility_phi"},
        {7, "vegetation_v"},
        {8, "resource_stock_r"},
        {9, "event_signal_e"},
        {10, "event_water_delta"},
        {11, "event_temperature_delta"},
        {12, "bootstrap_marker"},
        {13, "seed_probe"}
    };

    for (const auto& spec : specs) {
        if (!stateStore_.hasField(spec.name)) {
            stateStore_.allocateScalarField(spec);
        }
    }
}

std::uint64_t Runtime::applyInputFrame(const RuntimeInputFrame& inputFrame) {
    if (inputFrame.scalarPatches.empty()) {
        return 0;
    }

    StateStore::WriteSession inputWriter(
        stateStore_,
        "runtime.input_ingest",
        collectWritableVariables(inputFrame.scalarPatches));

    for (const auto& patch : inputFrame.scalarPatches) {
        inputWriter.setScalar(patch.variableName, patch.cell, patch.value);
    }

    return static_cast<std::uint64_t>(inputFrame.scalarPatches.size());
}

std::uint64_t Runtime::applyEvent(const RuntimeEvent& event, const std::uint64_t eventOrdinal) {
    if (event.scalarPatches.empty()) {
        return 0;
    }

    const std::string owner = event.eventName.empty()
        ? ("runtime.event_queue." + std::to_string(eventOrdinal))
        : ("runtime.event_queue." + event.eventName + "." + std::to_string(eventOrdinal));
    StateStore::WriteSession eventWriter(stateStore_, owner, collectWritableVariables(event.scalarPatches));

    for (const auto& patch : event.scalarPatches) {
        eventWriter.setScalar(patch.variableName, patch.cell, patch.value);
    }

    trace(
        TraceChannel::Event,
        "runtime.event.applied",
        owner,
        DeterministicHash::hashPod(eventOrdinal),
        stateStore_.header().stepIndex);

    return static_cast<std::uint64_t>(event.scalarPatches.size());
}

std::vector<std::string> Runtime::collectWritableVariables(const std::vector<ScalarWritePatch>& patches) {
    std::vector<std::string> variables;
    variables.reserve(patches.size());
    for (const auto& patch : patches) {
        variables.push_back(patch.variableName);
    }

    std::sort(variables.begin(), variables.end());
    variables.erase(std::unique(variables.begin(), variables.end()), variables.end());
    return variables;
}

std::string Runtime::stableHashForStringSet(const std::vector<std::string>& orderedValues) {
    std::vector<std::string> normalized = orderedValues;
    std::sort(normalized.begin(), normalized.end());
    std::string digest;
    for (const auto& value : normalized) {
        digest += value;
        digest += ';';
    }
    return std::to_string(DeterministicHash::hashString(digest));
}

const AdmissionReport& Runtime::admissionReport() const {
    if (!admissionReport_.admitted) {
        throw std::runtime_error("Runtime admission report is not available before successful start");
    }
    return admissionReport_;
}

void Runtime::trace(
    const TraceChannel channel,
    std::string name,
    std::string detail,
    const std::uint64_t payloadFingerprint,
    const std::uint64_t stepIndexOverride) {
    const std::uint64_t stepIndex =
        (stepIndexOverride == std::numeric_limits<std::uint64_t>::max())
            ? stateStore_.header().stepIndex
            : stepIndexOverride;
    observability_.record(TraceRecord{
        traceSequence_++,
        snapshot_.runSignature.identityHash(),
        resolvedProfile_.fingerprint(),
        stepIndex,
        channel,
        std::move(name),
        std::move(detail),
        payloadFingerprint});
}

} // namespace ws
