#pragma once

#include "ws/core/runtime.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace ws {

struct ReplayFrame {
    std::uint64_t stepIndex = 0;
    std::vector<RuntimeInputFrame> inputs;
    std::vector<RuntimeEvent> events;
};

struct ReplayPlan {
    RuntimeCheckpoint checkpoint{};
    std::vector<ReplayFrame> frames;
    std::uint64_t stepCount = 0;
    std::uint64_t expectedFinalStateHash = 0;
};

struct ReplayResult {
    bool runIdentityMatch = false;
    bool deterministicMatch = false;
    std::uint64_t replayStateHash = 0;
    std::vector<std::uint64_t> trajectoryStateHashes;
    std::optional<RuntimeSnapshot> finalSnapshot;
};

struct RunComparison {
    bool runIdentityEqual = false;
    bool stateHashEqual = false;
    std::int64_t stepIndexDelta = 0;
};

class ReplayRunner {
public:
    ReplayRunner(RuntimeConfig config, std::function<std::vector<std::shared_ptr<ISubsystem>>()> subsystemFactory);

    [[nodiscard]] ReplayResult run(const ReplayPlan& plan) const;
    [[nodiscard]] static RunComparison compareSnapshots(const RuntimeSnapshot& lhs, const RuntimeSnapshot& rhs);

private:
    RuntimeConfig config_;
    std::function<std::vector<std::shared_ptr<ISubsystem>>()> subsystemFactory_;
};

} // namespace ws
