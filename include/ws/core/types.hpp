#pragma once

// Standard library headers
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ws {

// =============================================================================
// Model Tier Classification
// =============================================================================

// Classification tiers for simulation models based on computational complexity
// and feature requirements. Used by the profile resolver to select appropriate
// execution strategies.
enum class ModelTier : std::uint8_t {
    A = 0,
    B = 1,
    C = 2
};

// =============================================================================
// Boundary Handling
// =============================================================================

// Specifies how the simulation grid handles out-of-bounds cell access.
// Clamp: Returns edge cell values for out-of-bounds indices
// Wrap: Wraps indices around the grid (toroidal topology)
enum class BoundaryMode : std::uint8_t {
    Clamp = 0,
    Wrap = 1
};

// =============================================================================
// Grid Topology
// =============================================================================

// Backend implementation for grid topology. Currently supports 2D Cartesian
// grids; extension to 3D or other topologies would add new enum values.
enum class GridTopologyBackend : std::uint8_t {
    Cartesian2D = 0
};

// =============================================================================
// Variable Data Types
// =============================================================================

// Data type for simulation variables. Currently limited to Float32 for
// compatibility with most GPU implementations and to ensure deterministic
// floating-point behavior.
enum class VariableDataType : std::uint8_t {
    Float32 = 0
};

// =============================================================================
// Unit Systems
// =============================================================================

// Unit regime for physical quantities in the simulation.
// Normalized: Values in [0, 1] range, suitable for dimensionless models
// Dimensioned: Values with physical units (meters, seconds, etc.)
enum class UnitRegime : std::uint8_t {
    Normalized = 0,
    Dimensioned = 1
};

// =============================================================================
// Temporal Integration Policies
// =============================================================================

// Temporal stepping strategy for multi-rate simulations.
// UniformA: Single global time step for all components
// PhasedB: Multiple phases per step with different dt values
// MultiRateC: Different components update at different rates
enum class TemporalPolicy : std::uint8_t {
    UniformA = 0,
    PhasedB = 1,
    MultiRateC = 2
};

// =============================================================================
// Execution Policies
// =============================================================================

// Controls the trade-off between determinism guarantees and throughput.
// StrictDeterministic: Bit-exact reproducibility across runs
// DeterministicReduced: Deterministic but with relaxed ordering guarantees
// ThroughputPriority: Maximum performance, may have non-deterministic order
enum class ExecutionPolicyMode : std::uint8_t {
    StrictDeterministic = 0,
    DeterministicReduced = 1,
    ThroughputPriority = 2
};

// =============================================================================
// Reproducibility Classes
// =============================================================================

// Classification of simulation reproducibility guarantees.
// Strict: Bit-exact reproducibility required
// BoundedDivergence: Results may diverge within known bounds
// Exploratory: No reproducibility guarantee, for rapid prototyping
enum class ReproducibilityClass : std::uint8_t {
    Strict = 0,
    BoundedDivergence = 1,
    Exploratory = 2
};

// =============================================================================
// Numerical Stability Escalation
// =============================================================================

// Action taken when numerical stability violations are detected.
// None: No action, continue simulation
// Damping: Apply numerical damping to reduce instability
// ControlledFallback: Switch to more stable (but slower) algorithm
// SafeAbort: Terminate simulation gracefully
enum class EscalationAction : std::uint8_t {
    None = 0,
    Damping = 1,
    ControlledFallback = 2,
    SafeAbort = 3
};

// =============================================================================
// Runtime Lifecycle States
// =============================================================================

// Current state of the simulation runtime.
// Created: Runtime object constructed but not initialized
// Initialized: Grid and fields allocated, ready to run
// Running: Simulation actively executing steps
// Terminated: Simulation completed or stopped cleanly
// Error: Simulation terminated due to an error condition
enum class RuntimeStatus : std::uint8_t {
    Created = 0,
    Initialized = 1,
    Running = 2,
    Terminated = 3,
    Error = 4
};

// =============================================================================
// Grid Specification
// =============================================================================

// Dimensions of the simulation grid in cells.
struct GridSpec {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    // Returns the total number of cells in the grid.
    [[nodiscard]] std::uint64_t cellCount() const noexcept {
        return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    }

    // Validates that the grid has non-zero dimensions.
    // Throws std::invalid_argument if either dimension is zero.
    void validate() const {
        if (width == 0 || height == 0) {
            throw std::invalid_argument("GridSpec dimensions must be non-zero");
        }
    }
};

// =============================================================================
// Cell Coordinates
// =============================================================================

// Unsigned cell coordinates for array indexing.
// Values must be within [0, grid.width) and [0, grid.height).
struct Cell {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

// Signed cell coordinates for intermediate calculations and neighbor lookups.
// Allows representation of relative positions and negative offsets.
struct CellSigned {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

// =============================================================================
// Memory Layout Configuration
// =============================================================================

// Policy for field storage memory layout. Controls alignment and tiling
// for optimal cache performance on the target architecture.
struct MemoryLayoutPolicy {
    std::uint32_t alignmentBytes = 64;
    std::uint32_t tileWidth = 64;
    std::uint32_t tileHeight = 1;

    // Returns the number of cells per tile.
    [[nodiscard]] std::uint64_t tileCellCount() const noexcept {
        return static_cast<std::uint64_t>(tileWidth) * static_cast<std::uint64_t>(tileHeight);
    }

    // Validates alignment is a power of two and tile dimensions are non-zero.
    void validate() const {
        if (alignmentBytes == 0 || (alignmentBytes & (alignmentBytes - 1u)) != 0) {
            throw std::invalid_argument("MemoryLayoutPolicy.alignmentBytes must be a non-zero power of two");
        }
        if (tileWidth == 0 || tileHeight == 0) {
            throw std::invalid_argument("MemoryLayoutPolicy tile dimensions must be non-zero");
        }
    }
};

// =============================================================================
// Variable Specification
// =============================================================================

// Defines a simulation variable (field) with its identifier and data type.
struct VariableSpec {
    std::uint32_t id = 0;
    std::string name;
    VariableDataType dataType = VariableDataType::Float32;
};

// Collection of variable specifications for model definition.
using VariableRegistry = std::vector<VariableSpec>;

// =============================================================================
// String Conversion Utilities
// =============================================================================

// Converts ModelTier enum to human-readable string.
[[nodiscard]] inline std::string toString(const ModelTier tier) {
    switch (tier) {
        case ModelTier::A: return "A";
        case ModelTier::B: return "B";
        case ModelTier::C: return "C";
    }
    return "Unknown";
}

// Converts ReproducibilityClass enum to human-readable string.
[[nodiscard]] inline std::string toString(const ReproducibilityClass value) {
    switch (value) {
        case ReproducibilityClass::Strict: return "strict";
        case ReproducibilityClass::BoundedDivergence: return "bounded_divergence";
        case ReproducibilityClass::Exploratory: return "exploratory";
    }
    return "unknown";
}

// Converts EscalationAction enum to human-readable string.
[[nodiscard]] inline std::string toString(const EscalationAction value) {
    switch (value) {
        case EscalationAction::None: return "none";
        case EscalationAction::Damping: return "damping";
        case EscalationAction::ControlledFallback: return "controlled_fallback";
        case EscalationAction::SafeAbort: return "safe_abort";
    }
    return "unknown";
}

// Converts ExecutionPolicyMode enum to human-readable string.
[[nodiscard]] inline std::string toString(const ExecutionPolicyMode value) {
    switch (value) {
        case ExecutionPolicyMode::StrictDeterministic: return "strict_deterministic";
        case ExecutionPolicyMode::DeterministicReduced: return "deterministic_reduced";
        case ExecutionPolicyMode::ThroughputPriority: return "throughput_priority";
    }
    return "unknown";
}

} // namespace ws
