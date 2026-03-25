#pragma once

#include "ws/core/profile.hpp"
#include "ws/core/types.hpp"

#include <cstdint>
#include <string>

namespace ws {

struct RunIdentityInput {
    std::uint64_t globalSeed = 0;
    std::string initializationParameterHash;
    GridSpec grid;
    BoundaryMode boundaryMode = BoundaryMode::Clamp;
    UnitRegime unitRegime = UnitRegime::Normalized;
    TemporalPolicy temporalPolicy = TemporalPolicy::UniformA;
    std::string eventTimelineHash;
    std::string activeSubsystemSetHash;
    ModelProfile profile;
};

class RunSignature {
public:
    RunSignature(
        std::uint64_t globalSeed,
        std::string initializationParameterHash,
        GridSpec grid,
        BoundaryMode boundaryMode,
        UnitRegime unitRegime,
        TemporalPolicy temporalPolicy,
        std::string eventTimelineHash,
        std::string activeSubsystemSetHash,
        std::uint64_t profileFingerprint,
        std::uint64_t compatibilityFingerprint,
        std::uint64_t identityHash);

    [[nodiscard]] std::uint64_t globalSeed() const noexcept { return globalSeed_; }
    [[nodiscard]] const std::string& initializationParameterHash() const noexcept { return initializationParameterHash_; }
    [[nodiscard]] const GridSpec& grid() const noexcept { return grid_; }
    [[nodiscard]] BoundaryMode boundaryMode() const noexcept { return boundaryMode_; }
    [[nodiscard]] UnitRegime unitRegime() const noexcept { return unitRegime_; }
    [[nodiscard]] TemporalPolicy temporalPolicy() const noexcept { return temporalPolicy_; }
    [[nodiscard]] const std::string& eventTimelineHash() const noexcept { return eventTimelineHash_; }
    [[nodiscard]] const std::string& activeSubsystemSetHash() const noexcept { return activeSubsystemSetHash_; }
    [[nodiscard]] std::uint64_t profileFingerprint() const noexcept { return profileFingerprint_; }
    [[nodiscard]] std::uint64_t compatibilityFingerprint() const noexcept { return compatibilityFingerprint_; }
    [[nodiscard]] std::uint64_t identityHash() const noexcept { return identityHash_; }

private:
    std::uint64_t globalSeed_;
    std::string initializationParameterHash_;
    GridSpec grid_;
    BoundaryMode boundaryMode_;
    UnitRegime unitRegime_;
    TemporalPolicy temporalPolicy_;
    std::string eventTimelineHash_;
    std::string activeSubsystemSetHash_;
    std::uint64_t profileFingerprint_;
    std::uint64_t compatibilityFingerprint_;
    std::uint64_t identityHash_;
};

class RunSignatureService {
public:
    [[nodiscard]] RunSignature create(const RunIdentityInput& input) const;
};

} // namespace ws
