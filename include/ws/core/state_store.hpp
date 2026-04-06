#pragma once

// Core dependencies
#include "ws/core/determinism.hpp"
#include "ws/core/types.hpp"

// Standard library
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ws {

// =============================================================================
// State Header
// =============================================================================

// Metadata for the current simulation state.
struct StateHeader {
    std::uint64_t stepIndex = 0;
    std::uint64_t timestampTicks = 0;
    RuntimeStatus status = RuntimeStatus::Created;
};

// =============================================================================
// Field Storage Metadata
// =============================================================================

// Metadata describing how a field is stored in memory.
struct FieldStorageMetadata {
    VariableSpec spec;
    std::uint64_t logicalCellCount = 0;
    std::uint64_t paddedCellCount = 0;
    std::uint64_t valuesOffsetBytes = 0;
    std::uint64_t validityMaskOffsetBytes = 0;
    std::uint32_t alignmentBytes = 0;
    std::uint32_t tileWidth = 0;
    std::uint32_t tileHeight = 0;
    std::uint64_t overlayEntryCount = 0;
};

// =============================================================================
// Memory Layout
// =============================================================================

// Describes the complete memory layout for all fields in the state store.
struct MemoryLayout {
    // Layout information for a single field.
    struct FieldLayout {
        VariableSpec spec;
        std::uint64_t logicalCellCount = 0;
        std::uint64_t paddedCellCount = 0;
        std::uint64_t valuesOffsetBytes = 0;
        std::uint64_t validityMaskOffsetBytes = 0;
        std::uint32_t alignmentBytes = 0;
        std::uint32_t valueStrideBytes = static_cast<std::uint32_t>(sizeof(float));
        std::uint32_t validityStrideBytes = static_cast<std::uint32_t>(sizeof(std::uint8_t));
    };

    GridSpec grid{};
    MemoryLayoutPolicy policy{};
    std::uint64_t valuesBufferBytes = 0;
    std::uint64_t validityMaskBufferBytes = 0;
        std::vector<FieldLayout> fields;
};

// =============================================================================
// State Store Snapshot
// =============================================================================

// Complete snapshot of state store for checkpointing and serialization.
struct StateStoreSnapshot {
    // Data payload for a single field in the snapshot.
    struct FieldPayload {
        VariableSpec spec;
        std::vector<float> values;
        std::vector<std::uint8_t> validityMask;
        std::vector<std::pair<std::uint64_t, float>> sparseOverlay;
    };

    StateHeader header{};
    GridSpec grid{};
    BoundaryMode boundaryMode = BoundaryMode::Clamp;
    GridTopologyBackend topologyBackend = GridTopologyBackend::Cartesian2D;
    MemoryLayoutPolicy memoryLayout{};
    std::uint64_t runIdentityHash = 0;
    std::uint64_t profileFingerprint = 0;
    std::string checkpointLabel;
    std::uint64_t payloadBytes = 0;
    std::uint64_t stateHash = 0;
    std::vector<FieldPayload> fields;
};

// =============================================================================
// State Store Class
// =============================================================================

// Manages simulation field data including allocation, access, and persistence.
// Provides field storage with support for sparse overlays, validity masks,
// and boundary handling.
class StateStore {
public:
    // Kind of access performed on a field.
    enum class AccessKind : std::uint8_t {
        Read = 0,
        Write = 1
    };

    // Observer callback type for field access monitoring.
    using AccessObserver = std::function<void(const std::string&, AccessKind)>;

    // Handle type for fast field access.
    using FieldHandle = std::size_t;
    // Invalid handle value for error handling.
    static constexpr FieldHandle InvalidHandle = static_cast<std::size_t>(-1);

    // Write session for scoped field modification with access control.
    class WriteSession {
    public:
        // Constructs a write session for the given state store.
        // The ownerName identifies the caller for access tracking.
        // Only the specified variables can be written.
        WriteSession(StateStore& stateStore, std::string ownerName, std::vector<std::string> allowedVariables);

        void setScalar(const std::string& variableName, Cell cell, float value);
        void fillScalar(const std::string& variableName, float value);
        void setOverlayScalar(const std::string& variableName, Cell cell, float value);
        void clearOverlayScalar(const std::string& variableName, Cell cell);
        // Invalidates a scalar value at the given cell, marking it as invalid.
        void invalidateScalar(const std::string& variableName, Cell cell);

        FieldHandle getFieldHandle(const std::string& variableName) const noexcept;
        void setScalarFast(FieldHandle handle, Cell cell, float value);

    private:
        // Checks if the given variable name is in the allowed set.
        [[nodiscard]] bool isAllowed(const std::string& variableName) const;

        StateStore& stateStore_;
        std::string ownerName_;
        std::vector<std::string> allowedVariables_;
    };

    // Constructs a state store with the given grid and configuration.
    explicit StateStore(
        GridSpec grid,
        BoundaryMode boundaryMode = BoundaryMode::Clamp,
        GridTopologyBackend topologyBackend = GridTopologyBackend::Cartesian2D,
        MemoryLayoutPolicy memoryLayoutPolicy = MemoryLayoutPolicy{});

    [[nodiscard]] FieldHandle addVariable(const VariableSpec& spec);
    // Allocates storage for a new scalar field.
    void allocateScalarField(const VariableSpec& spec);
    // Returns true if a field with the given name exists.
    [[nodiscard]] bool hasField(const std::string& name) const noexcept;
    // Returns the list of all variable names in the store.
    [[nodiscard]] std::vector<std::string> variableNames() const;
    // Registers an alias for a semantic key to a variable name.
    void registerFieldAlias(const std::string& semanticKey, const std::string& variableName);
    // Returns true if an alias exists for the semantic key.
    [[nodiscard]] bool hasFieldAlias(const std::string& semanticKey) const noexcept;
    // Resolves a semantic key to its corresponding variable name, if an alias exists.
    [[nodiscard]] std::optional<std::string> resolveFieldAlias(const std::string& semanticKey) const;
    // Returns all registered field aliases.
    [[nodiscard]] const std::unordered_map<std::string, std::string>& fieldAliases() const noexcept { return fieldAliases_; }
    // Clears all registered field aliases.
    void clearFieldAliases() noexcept;
    // Returns the logical number of cells for a field.
    [[nodiscard]] std::uint64_t logicalCellCount(const std::string& name) const;
    // Returns a reference to the scalar field data.
    [[nodiscard]] const std::vector<float>& scalarField(const std::string& name) const;
    // Attempts to sample a scalar value at the given cell position.
    // Returns std::nullopt if the cell is out of bounds or invalid.
    [[nodiscard]] std::optional<float> trySampleScalar(const std::string& name, CellSigned cell) const;
    // Clamps all values in a field to the given range.
    void clampField(const std::string& name, float minValue, float maxValue);
    // Returns metadata for all fields in the store.
    [[nodiscard]] std::vector<FieldStorageMetadata> fieldMetadata() const;
    // Computes a hash of the current state for determinism checking.
    [[nodiscard]] std::uint64_t stateHash() const noexcept;

    // Fast raw-buffer accessors for performance-critical inner loops.
    // Returns a pointer to the flat float array for the given handle.
    // The pointer is valid as long as no allocateScalarField() calls are made.
    [[nodiscard]] const float* scalarFieldRawPtr(FieldHandle handle) const noexcept;
    [[nodiscard]] float* scalarFieldRawPtrMut(FieldHandle handle) const noexcept;
    [[nodiscard]] const std::uint8_t* validityMaskRawPtr(FieldHandle handle) const noexcept;

    // Sets an observer callback for field access monitoring.
    void setAccessObserver(AccessObserver observer);
    // Clears the access observer.
    void clearAccessObserver();

    // Converts cell coordinates to linear array index.
    [[nodiscard]] std::uint64_t indexOf(Cell cell) const;
    // Converts signed cell coordinates to linear array index.
    [[nodiscard]] std::uint64_t indexOf(CellSigned cell) const;
    // Converts linear index back to cell coordinates.
    [[nodiscard]] Cell cellFromIndex(std::uint64_t index) const;
    // Resolves out-of-bounds coordinates according to the boundary mode.
    [[nodiscard]] Cell resolveBoundary(CellSigned cell) const;
    // Returns the grid specification.
    [[nodiscard]] const GridSpec& grid() const noexcept { return grid_; }
    // Returns the boundary mode.
    [[nodiscard]] BoundaryMode boundaryMode() const noexcept { return boundaryMode_; }
    // Returns the grid topology backend.
    [[nodiscard]] GridTopologyBackend topologyBackend() const noexcept { return topologyBackend_; }
    // Returns the memory layout policy.
    [[nodiscard]] const MemoryLayoutPolicy& memoryLayoutPolicy() const noexcept { return memoryLayoutPolicy_; }
    // Returns the computed memory layout.
    [[nodiscard]] const MemoryLayout& getLayout() const noexcept { return layout_; }

    // Returns the current state header.
    [[nodiscard]] const StateHeader& header() const noexcept { return header_; }
    // Updates the state header.
    void setHeader(const StateHeader& header) noexcept { header_ = header; }

    // Creates a snapshot of the current state for checkpointing.
    // The computeHash parameter controls whether to compute the state hash.
    // Set computeHash=false for display-only snapshots, true for persistence.
    [[nodiscard]] StateStoreSnapshot createSnapshot(
        std::uint64_t runIdentityHash,
        std::uint64_t profileFingerprint,
        std::string checkpointLabel,
        bool computeHash = true) const;
    // Loads a snapshot, restoring state from a checkpoint.
    // Validates that the run identity and profile fingerprints match.
    void loadSnapshot(const StateStoreSnapshot& snapshot, std::uint64_t expectedRunIdentityHash, std::uint64_t expectedProfileFingerprint);

    // Returns the field handle for a variable name.
    [[nodiscard]] FieldHandle getFieldHandle(const std::string& name) const noexcept;
    // Fast scalar sampling using field handle instead of name.
    [[nodiscard]] std::optional<float> trySampleScalarFast(FieldHandle handle, CellSigned cell) const;
    // Returns the scalar field data for a handle.
    [[nodiscard]] const std::vector<float>& scalarFieldFast(FieldHandle handle) const;

private:
    // WriteSession needs access to private members for field modification.
    friend class WriteSession;

    // Emits an access event to the observer callback.
    void emitAccess(const std::string& variableName, AccessKind kind) const;

    // Internal implementation for setting a scalar value.
    void setScalarInternal(const std::string& variableName, Cell cell, float value);
    // Internal implementation for filling a field with a constant value.
    void fillScalarInternal(const std::string& variableName, float value);
    // Internal implementation for setting a sparse overlay value.
    void setOverlayScalarInternal(const std::string& variableName, Cell cell, float value);
    // Internal implementation for clearing a sparse overlay entry.
    void clearOverlayScalarInternal(const std::string& variableName, Cell cell);
    // Internal implementation for invalidating a cell value.
    void invalidateScalarInternal(const std::string& variableName, Cell cell);

    // Storage for a single scalar field including values, validity mask, and sparse overlay.
    struct ScalarFieldStorage {
        VariableSpec spec;
        std::uint64_t logicalCellCount = 0;
        std::uint64_t paddedCellCount = 0;
        std::vector<float> values;
        std::vector<std::uint8_t> validityMask;
        std::unordered_map<std::uint64_t, float> sparseOverlay;
    };

    // Rounds a value up to the nearest multiple.
    [[nodiscard]] static std::uint64_t roundUpToMultiple(std::uint64_t value, std::uint64_t multiple);
    // Gets the field storage reference for writing (non-const).
    [[nodiscard]] ScalarFieldStorage& fieldForWrite(const std::string& variableName);
    // Gets the field storage reference for reading (const).
    [[nodiscard]] const ScalarFieldStorage& fieldForRead(const std::string& variableName) const;

    GridSpec grid_;
    BoundaryMode boundaryMode_ = BoundaryMode::Clamp;
    GridTopologyBackend topologyBackend_ = GridTopologyBackend::Cartesian2D;
    MemoryLayoutPolicy memoryLayoutPolicy_{};
    StateHeader header_{};
    std::vector<VariableSpec> variableOrder_;
    std::vector<ScalarFieldStorage> scalarFields_;
        std::unordered_map<std::string, std::size_t> fieldNameToIndex_;
        std::unordered_map<std::string, std::string> fieldAliases_;
        MemoryLayout layout_{};
    AccessObserver accessObserver_;
};

} // namespace ws
