#include "ws/core/run_signature.hpp"

#include "ws/core/determinism.hpp"

#include <stdexcept>
#include <utility>

namespace ws {

RunSignature::RunSignature(
    const std::uint64_t globalSeed,
    std::string initializationParameterHash,
    const GridSpec grid,
    const BoundaryMode boundaryMode,
    const UnitRegime unitRegime,
    const TemporalPolicy temporalPolicy,
    std::string timeIntegratorId,
    std::string eventTimelineHash,
    std::string activeSubsystemSetHash,
    const std::uint64_t profileFingerprint,
    const std::uint64_t compatibilityFingerprint,
    const std::uint64_t identityHash)
    : globalSeed_(globalSeed),
      initializationParameterHash_(std::move(initializationParameterHash)),
      grid_(grid),
      boundaryMode_(boundaryMode),
      unitRegime_(unitRegime),
      temporalPolicy_(temporalPolicy),
    timeIntegratorId_(std::move(timeIntegratorId)),
      eventTimelineHash_(std::move(eventTimelineHash)),
      activeSubsystemSetHash_(std::move(activeSubsystemSetHash)),
      profileFingerprint_(profileFingerprint),
      compatibilityFingerprint_(compatibilityFingerprint),
      identityHash_(identityHash) {
    grid_.validate();

    if (initializationParameterHash_.empty() ||
        timeIntegratorId_.empty() ||
        eventTimelineHash_.empty() ||
        activeSubsystemSetHash_.empty()) {
        throw std::invalid_argument("RunSignature immutable identity fields must not be empty");
    }
}

RunSignature RunSignatureService::create(const RunIdentityInput& input) const {
    input.grid.validate();

    if (input.initializationParameterHash.empty()) {
        throw std::invalid_argument("RunIdentityInput.initializationParameterHash must not be empty");
    }

    if (input.eventTimelineHash.empty()) {
        throw std::invalid_argument("RunIdentityInput.eventTimelineHash must not be empty");
    }

    if (input.activeSubsystemSetHash.empty()) {
        throw std::invalid_argument("RunIdentityInput.activeSubsystemSetHash must not be empty");
    }

    if (input.timeIntegratorId.empty()) {
        throw std::invalid_argument("RunIdentityInput.timeIntegratorId must not be empty");
    }

    const std::uint64_t profileFingerprint = input.profile.fingerprint();

    std::uint64_t compatibilityFingerprint = DeterministicHash::offsetBasis;
    for (const auto& assumption : input.profile.compatibilityAssumptions) {
        compatibilityFingerprint = DeterministicHash::combine(
            compatibilityFingerprint,
            DeterministicHash::hashString(assumption));
    }

    std::uint64_t identityHash = DeterministicHash::offsetBasis;
    identityHash = DeterministicHash::combine(identityHash, DeterministicHash::hashPod(input.globalSeed));
    identityHash = DeterministicHash::combine(identityHash, DeterministicHash::hashString(input.initializationParameterHash));
    identityHash = DeterministicHash::combine(identityHash, DeterministicHash::hashPod(input.grid.width));
    identityHash = DeterministicHash::combine(identityHash, DeterministicHash::hashPod(input.grid.height));
    identityHash = DeterministicHash::combine(identityHash, static_cast<std::uint64_t>(input.boundaryMode));
    identityHash = DeterministicHash::combine(identityHash, static_cast<std::uint64_t>(input.unitRegime));
    identityHash = DeterministicHash::combine(identityHash, static_cast<std::uint64_t>(input.temporalPolicy));
    identityHash = DeterministicHash::combine(identityHash, DeterministicHash::hashString(input.timeIntegratorId));
    identityHash = DeterministicHash::combine(identityHash, DeterministicHash::hashString(input.eventTimelineHash));
    identityHash = DeterministicHash::combine(identityHash, DeterministicHash::hashString(input.activeSubsystemSetHash));
    identityHash = DeterministicHash::combine(identityHash, profileFingerprint);
    identityHash = DeterministicHash::combine(identityHash, compatibilityFingerprint);

    return RunSignature(
        input.globalSeed,
        input.initializationParameterHash,
        input.grid,
        input.boundaryMode,
        input.unitRegime,
        input.temporalPolicy,
        input.timeIntegratorId,
        input.eventTimelineHash,
        input.activeSubsystemSetHash,
        profileFingerprint,
        compatibilityFingerprint,
        identityHash);
}

} // namespace ws
