#pragma once

// Core dependencies
#include "ws/core/profile.hpp"
#include "ws/core/types.hpp"

// Standard library
#include <cstdint>
#include <string>

namespace ws {

// =============================================================================
// Run Identity Input
// =============================================================================

// Input parameters for computing a run signature.
struct RunIdentityInput {
    std::uint64_t globalSeed = 0;
    std::string initializationParameterHash;
    GridSpec grid;
    BoundaryMode boundaryMode = BoundaryMode::Clamp;
    UnitRegime unitRegime = UnitRegime::Normalized;
    TemporalPolicy temporalPolicy = TemporalPolicy::UniformA;
    std::string timeIntegratorId = "explicit_euler";
    std::string eventTimelineHash;
    std::string activeSubsystemSetHash;
    ModelProfile profile;
};

// =============================================================================
// Run Signature
// =============================================================================

// Immutable identifier for a specific run configuration.
// Used for reproducibility tracking and checkpoint identification.
class RunSignature {
public:
    // Constructs a run signature from its components.
    RunSignature(
        std::uint64_t globalSeed,
        std::string initializationParameterHash,
        GridSpec grid,
        BoundaryMode boundaryMode,
        UnitRegime unitRegime,
        TemporalPolicy temporalPolicy,
        std::string timeIntegratorId,
        std::string eventTimelineHash,
        std::string activeSubsystemSetHash,
        std::uint64_t profileFingerprint,
        std::uint64_t compatibilityFingerprint,
        std::uint64_t identityHash);

    // Returns the global seed used for random number generation.
    [[nodiscard]] std::uint64_t globalSeed() const noexcept { return globalSeed_; }
    // Returns the hash of initialization parameters.
    [[nodiscard]] const std::string& initializationParameterHash() const noexcept { return initializationParameterHash_; }
    // Returns the grid specification.
    [[nodiscard]] const GridSpec& grid() const noexcept { return grid_; }
    // Returns the boundary mode.
    [[nodiscard]] BoundaryMode boundaryMode() const noexcept { return boundaryMode_; }
    // Returns the unit regime.
    [[nodiscard]] UnitRegime unitRegime() const noexcept { return unitRegime_; }
    // Returns the temporal policy.
    [[nodiscard]] TemporalPolicy temporalPolicy() const noexcept { return temporalPolicy_; }
    // Returns the selected time integrator id.
    [[nodiscard]] const std::string& timeIntegratorId() const noexcept { return timeIntegratorId_; }
    // Returns the hash of the event timeline.
    [[nodiscard]] const std::string& eventTimelineHash() const noexcept { return eventTimelineHash_; }
    // Returns the hash of the active subsystem set.
    [[nodiscard]] const std::string& activeSubsystemSetHash() const noexcept { return activeSubsystemSetHash_; }
    // Returns the profile fingerprint.
    [[nodiscard]] std::uint64_t profileFingerprint() const noexcept { return profileFingerprint_; }
    // Returns the compatibility fingerprint.
    [[nodiscard]] std::uint64_t compatibilityFingerprint() const noexcept { return compatibilityFingerprint_; }
    // Returns the overall identity hash.
    [[nodiscard]] std::uint64_t identityHash() const noexcept { return identityHash_; }

private:
    std::uint64_t globalSeed_;
    std::string initializationParameterHash_;
    GridSpec grid_;
    BoundaryMode boundaryMode_;
    UnitRegime unitRegime_;
    TemporalPolicy temporalPolicy_;
    std::string timeIntegratorId_;
    std::string eventTimelineHash_;
    std::string activeSubsystemSetHash_;
    std::uint64_t profileFingerprint_;
    std::uint64_t compatibilityFingerprint_;
    std::uint64_t identityHash_;
};

// =============================================================================
// Run Signature Service
// =============================================================================

// Service for creating run signatures from input parameters.
class RunSignatureService {
public:
    // Creates a run signature from the given input.
    [[nodiscard]] RunSignature create(const RunIdentityInput& input) const;
};

} // namespace ws
