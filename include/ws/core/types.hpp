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

} // namespace ws
