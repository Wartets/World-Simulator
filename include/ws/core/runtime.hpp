#pragma once

#include "ws/core/profile.hpp"
#include "ws/core/run_signature.hpp"
#include "ws/core/scheduler.hpp"
#include "ws/core/state_store.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ws {

struct RuntimeConfig {
    std::uint64_t seed = 1;
    GridSpec grid{16, 16};
    BoundaryMode boundaryMode = BoundaryMode::Clamp;
    GridTopologyBackend topologyBackend = GridTopologyBackend::Cartesian2D;
    MemoryLayoutPolicy memoryLayoutPolicy{};
    UnitRegime unitRegime = UnitRegime::Normalized;
    TemporalPolicy temporalPolicy = TemporalPolicy::UniformA;
    ProfileResolverInput profileInput{};
};

struct RuntimeSnapshot {
    RunSignature runSignature;
    std::uint64_t stateHash = 0;
    StateHeader stateHeader{};
    std::uint64_t payloadBytes = 0;
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
    void start();
    void step();
    void stop();
    [[nodiscard]] RuntimeCheckpoint createCheckpoint(const std::string& label) const;
    void loadCheckpoint(const RuntimeCheckpoint& checkpoint);

    [[nodiscard]] RuntimeStatus status() const noexcept { return status_; }
    [[nodiscard]] const RuntimeSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    void allocateCanonicalFields();
    [[nodiscard]] static std::string stableHashForStringSet(const std::vector<std::string>& orderedValues);

    RuntimeConfig config_;
    RuntimeStatus status_ = RuntimeStatus::Created;
    ProfileResolver profileResolver_;
    RunSignatureService runSignatureService_;
    ModelProfile resolvedProfile_;
    StateStore stateStore_;
    Scheduler scheduler_;
    RuntimeSnapshot snapshot_;
};

} // namespace ws
