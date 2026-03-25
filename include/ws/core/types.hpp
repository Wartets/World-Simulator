#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ws {

enum class ModelTier : std::uint8_t {
    A = 0,
    B = 1,
    C = 2
};

enum class BoundaryMode : std::uint8_t {
    Clamp = 0,
    Wrap = 1
};

enum class GridTopologyBackend : std::uint8_t {
    Cartesian2D = 0
};

enum class VariableDataType : std::uint8_t {
    Float32 = 0
};

enum class UnitRegime : std::uint8_t {
    Normalized = 0,
    Dimensioned = 1
};

enum class TemporalPolicy : std::uint8_t {
    UniformA = 0,
    PhasedB = 1,
    MultiRateC = 2
};

enum class ExecutionPolicyMode : std::uint8_t {
    StrictDeterministic = 0,
    DeterministicReduced = 1,
    ThroughputPriority = 2
};

enum class ReproducibilityClass : std::uint8_t {
    Strict = 0,
    BoundedDivergence = 1,
    Exploratory = 2
};

enum class EscalationAction : std::uint8_t {
    None = 0,
    Damping = 1,
    ControlledFallback = 2,
    SafeAbort = 3
};

enum class RuntimeStatus : std::uint8_t {
    Created = 0,
    Initialized = 1,
    Running = 2,
    Terminated = 3,
    Error = 4
};

struct GridSpec {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] std::uint64_t cellCount() const noexcept {
        return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    }

    void validate() const {
        if (width == 0 || height == 0) {
            throw std::invalid_argument("GridSpec dimensions must be non-zero");
        }
    }
};

struct Cell {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

struct CellSigned {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

struct MemoryLayoutPolicy {
    std::uint32_t alignmentBytes = 64;
    std::uint32_t tileWidth = 64;
    std::uint32_t tileHeight = 1;

    [[nodiscard]] std::uint64_t tileCellCount() const noexcept {
        return static_cast<std::uint64_t>(tileWidth) * static_cast<std::uint64_t>(tileHeight);
    }

    void validate() const {
        if (alignmentBytes == 0 || (alignmentBytes & (alignmentBytes - 1u)) != 0) {
            throw std::invalid_argument("MemoryLayoutPolicy.alignmentBytes must be a non-zero power of two");
        }
        if (tileWidth == 0 || tileHeight == 0) {
            throw std::invalid_argument("MemoryLayoutPolicy tile dimensions must be non-zero");
        }
    }
};

struct VariableSpec {
    std::uint32_t id = 0;
    std::string name;
    VariableDataType dataType = VariableDataType::Float32;
};

using VariableRegistry = std::vector<VariableSpec>;

[[nodiscard]] inline std::string toString(const ModelTier tier) {
    switch (tier) {
        case ModelTier::A: return "A";
        case ModelTier::B: return "B";
        case ModelTier::C: return "C";
    }
    return "Unknown";
}

[[nodiscard]] inline std::string toString(const ReproducibilityClass value) {
    switch (value) {
        case ReproducibilityClass::Strict: return "strict";
        case ReproducibilityClass::BoundedDivergence: return "bounded_divergence";
        case ReproducibilityClass::Exploratory: return "exploratory";
    }
    return "unknown";
}

[[nodiscard]] inline std::string toString(const EscalationAction value) {
    switch (value) {
        case EscalationAction::None: return "none";
        case EscalationAction::Damping: return "damping";
        case EscalationAction::ControlledFallback: return "controlled_fallback";
        case EscalationAction::SafeAbort: return "safe_abort";
    }
    return "unknown";
}

[[nodiscard]] inline std::string toString(const ExecutionPolicyMode value) {
    switch (value) {
        case ExecutionPolicyMode::StrictDeterministic: return "strict_deterministic";
        case ExecutionPolicyMode::DeterministicReduced: return "deterministic_reduced";
        case ExecutionPolicyMode::ThroughputPriority: return "throughput_priority";
    }
    return "unknown";
}

} // namespace ws
