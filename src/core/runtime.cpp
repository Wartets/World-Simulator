#include "ws/core/runtime.hpp"

#include "ws/core/determinism.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <utility>

namespace ws {

namespace {

std::string tierToString(const ModelTier tier) {
    return toString(tier);
}

} // namespace

Runtime::Runtime(RuntimeConfig config)
    : config_(std::move(config)),
    stateStore_(config_.grid, config_.boundaryMode, config_.topologyBackend, config_.memoryLayoutPolicy),
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
}

void Runtime::registerSubsystem(std::shared_ptr<ISubsystem> subsystem) {
    if (status_ != RuntimeStatus::Created) {
        throw std::runtime_error("Subsystem registration is only allowed before runtime start");
    }
    scheduler_.registerSubsystem(std::move(subsystem));
}

void Runtime::start() {
    if (status_ != RuntimeStatus::Created) {
        throw std::runtime_error("Runtime start can only be called from Created state");
    }

    try {
        resolvedProfile_ = profileResolver_.resolve(config_.profileInput);

        admissionReport_ = interactionCoordinator_.buildAdmissionReport(
            resolvedProfile_,
            config_.temporalPolicy,
            scheduler_.registeredSubsystems());
        if (!admissionReport_.admitted) {
            throw std::runtime_error("Profile admission failed: " + admissionReport_.diagnosticsText());
        }
        scheduler_.setAdmissionReport(admissionReport_);

        allocateCanonicalFields();

        DeterministicRngFactory rngFactory(config_.seed);
        auto bootstrapStream = rngFactory.createStream("runtime.seed.bootstrap_marker");
        std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
        {
            StateStore::WriteSession seedWriter(stateStore_, "runtime_seed_pipeline", {"seed_probe"});
            for (std::uint32_t y = 0; y < config_.grid.height; ++y) {
                for (std::uint32_t x = 0; x < config_.grid.width; ++x) {
                    seedWriter.setScalar("seed_probe", Cell{x, y}, distribution(bootstrapStream));
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

    StepDiagnostics diagnostics;
    diagnostics.orderingLog.push_back("pipeline:input_ingest");

    while (!pendingInputs_.empty()) {
        diagnostics.inputPatchesApplied += applyInputFrame(pendingInputs_.front());
        pendingInputs_.pop_front();
    }

    diagnostics.orderingLog.push_back("pipeline:event_queue_apply");
    std::uint64_t eventOrdinal = 0;
    while (!pendingEvents_.empty()) {
        diagnostics.eventPatchesApplied += applyEvent(pendingEvents_.front(), eventOrdinal);
        diagnostics.eventsApplied += 1;
        pendingEvents_.pop_front();
        eventOrdinal += 1;
    }

    StepDiagnostics schedulerDiagnostics = scheduler_.step(
        stateStore_,
        resolvedProfile_,
        config_.temporalPolicy,
        config_.guardrailPolicy,
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
}

void Runtime::queueInput(RuntimeInputFrame inputFrame) {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime input queue requires Running state");
    }
    pendingInputs_.push_back(std::move(inputFrame));
}

void Runtime::enqueueEvent(RuntimeEvent event) {
    if (status_ != RuntimeStatus::Running) {
        throw std::runtime_error("Runtime event queue requires Running state");
    }
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
    status_ = RuntimeStatus::Terminated;
}

RuntimeCheckpoint Runtime::createCheckpoint(const std::string& label) const {
    if (status_ != RuntimeStatus::Running && status_ != RuntimeStatus::Initialized) {
        throw std::runtime_error("Runtime checkpoint creation requires Initialized or Running state");
    }

    const std::uint64_t profileFingerprint = resolvedProfile_.fingerprint();
    return RuntimeCheckpoint{
        snapshot_.runSignature,
        profileFingerprint,
        stateStore_.createSnapshot(
            snapshot_.runSignature.identityHash(),
            profileFingerprint,
            label)};
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

} // namespace ws
