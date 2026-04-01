#pragma once

#include "ws/core/runtime.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ws {

struct ReplayExecutionResult {
    bool success = false;
    bool deterministicMatch = false;
    std::uint64_t finalStateHash = 0;
    std::vector<std::uint64_t> trajectoryStateHashes;
};

class ReplayEngine {
public:
    static ReplayExecutionResult replayToStep(
        Runtime& runtime,
        const RuntimeCheckpoint& checkpoint,
        std::uint64_t targetStep,
        std::optional<std::uint64_t> expectedStateHash,
        std::string& message);
};

} // namespace ws
