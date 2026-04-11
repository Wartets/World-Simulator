// =============================================================================
// State Store Public API
// =============================================================================
/// This module provides deterministic storage and access for simulation fields.
///
/// ## Public API Contract
/// All public classes, functions, structs, and enums are stable for external use.
/// Client code may depend on these interfaces without breaking on patch releases.
/// Internal field storage layout, compression strategies, and allocation policies
/// may be refactored without changing public contracts.
///
/// ## Core Responsibilities
/// 1. **Field Lifecycle**: Allocate, store, and free scalar fields on a regular grid.
/// 2. **Deterministic Hashing**: Compute bitwise-reproducible state hashes.
/// 3. **Sparse Overlays**: Support efficient cell-level modifications on dense fields.
/// 4. **Validity Tracking**: Mark individual cells as valid/invalid for robustness.
/// 5. **Checkpointing**: Serialize and deserialize state for replay and persistence.
/// 6. **Boundary Modes**: Handle out-of-bounds access via Clamp, Wrap, or Reflect.
/// 7. **Write Sessions**: Enforce per-caller access control for field modifications.
///
/// ## Memory and Ownership
/// - The StateStore owns all allocated field data.
/// - StateStoreSnapshot holds a deep copy of all fields for serialization.
/// - StateStoreSnapshot and MemoryLayout describe the memory layout but do not own data.
/// - Field pointers from scalarFieldRawPtr() are valid until next allocateScalarField().
///
/// ## Determinism Invariants
/// - State hashing is deterministic across identical seed + config inputs.
/// - Field values must be finite (NaN and Inf invalid); invalid cells use validity mask.
/// - Boundary resolution (resolveBoundary) must be deterministic for the mode.
/// - Sparse overlay operations preserve replay semantics.
/// - All internal allocation and indexing algorithms must be bitwise reproducible.
///
/// ## Thread Safety
/// - Read-only operations (scalarField, trySampleScalar) are thread-safe.
/// - WriteSession is exclusive to a single writer at a time.
/// - Multiple WriteSession instances cannot exist concurrently for the same StateStore.
/// - Field handle lookups (getFieldHandle) are thread-safe.
/// - Access observers (setAccessObserver) are called synchronously and must be thread-safe.
///
/// ## Error Semantics
/// - Field lookup (scalarField) throws if field does not exist.
/// - Boundary modes are applied transparently via resolveBoundary().
/// - Out-of-bounds reads in trySampleScalar return std::nullopt rather than throwing.
/// - Invalid cells (marked in validity mask) may sample NaN or user-provided sentinel.
/// - Snapshot load validates run identity + profile fingerprint; throws on mismatch.
///
// =============================================================================

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

/// Metadata for the current simulation state.
/// Tracks runtime step count, wall-clock ticks, and execution status.
struct StateHeader {
    std::uint64_t stepIndex = 0;         ///< Current simulation step number (incremented by runtime).
    std::uint64_t timestampTicks = 0;    ///< Wall-clock timestamp in ticks (when last stepped).
    RuntimeStatus status = RuntimeStatus::Created; ///< Current runtime status (Created, Running, Paused, Stopped).
};

// =============================================================================
// Field Storage Metadata
// =============================================================================

/// Metadata describing how a field is stored in memory.
/// Provides memory layout information for introspection and debugging.
struct FieldStorageMetadata {
    VariableSpec spec;                     ///< Variable specification including name, bounds, units.
    std::uint64_t logicalCellCount = 0;    ///< Logical grid cells (width * height).
    std::uint64_t paddedCellCount = 0;     ///< Actual allocated cells (includes alignment/tiling padding).
    std::uint64_t valuesOffsetBytes = 0;   ///< Byte offset of values buffer from buffer start.
    std::uint64_t validityMaskOffsetBytes = 0; ///< Byte offset of validity mask from buffer start.
    std::uint32_t alignmentBytes = 0;      ///< Memory alignment requirement for the field (e.g., 32, 64).
    std::uint32_t tileWidth = 0;           ///< Tile width if tiling is used (0 means no tiling).
    std::uint32_t tileHeight = 0;          ///< Tile height if tiling is used (0 means no tiling).
    std::uint64_t overlayEntryCount = 0;   ///< Number of entries in sparse overlay (0 if no overlay).
};

// =============================================================================
// Memory Layout
// =============================================================================

/// Describes the complete memory layout for all fields in the state store.
/// Used for introspection, optimization, and serialization format documentation.
struct MemoryLayout {
    /// Layout information for a single field.
    struct FieldLayout {
        VariableSpec spec;                 ///< Variable specification.
        std::uint64_t logicalCellCount = 0;    ///< Logical grid cells.
        std::uint64_t paddedCellCount = 0;     ///< Padded cell count for alignment/tiling.
        std::uint64_t valuesOffsetBytes = 0;   ///< Offset of values within buffer.
        std::uint64_t validityMaskOffsetBytes = 0; ///< Offset of validity mask.
        std::uint32_t alignmentBytes = 0;  ///< Required alignment boundary.
        std::uint32_t valueStrideBytes = static_cast<std::uint32_t>(sizeof(float)); ///< Bytes between consecutive values.
        std::uint32_t validityStrideBytes = static_cast<std::uint32_t>(sizeof(std::uint8_t)); ///< Bytes between validity entries.
    };

    GridSpec grid{};                       ///< Grid specification (width, height, depth).
    MemoryLayoutPolicy policy{};           ///< Layout policy (contiguous, tiled, interleaved, etc.).
    std::uint64_t valuesBufferBytes = 0;   ///< Total size of values buffer.
    std::uint64_t validityMaskBufferBytes = 0; ///< Total size of validity mask buffer.
    std::vector<FieldLayout> fields;       ///< Layout info for each field in registration order.
};

// =============================================================================
// State Store Snapshot
// =============================================================================

/// Complete snapshot of state store for checkpointing and serialization.
/// Contains all field data, metadata, and configuration needed to restore state.
/// Used for replay, persistence, and multi-model synchronization.
struct StateStoreSnapshot {
    /// Data payload for a single field in the snapshot.
    struct FieldPayload {
        VariableSpec spec;                                  ///< Variable spec (name, units, bounds, etc.).
        std::vector<float> values;                          ///< Dense array of field values (one per logical cell).
        std::vector<std::uint8_t> validityMask;             ///< Validity mask (1 byte per cell; 0=invalid, 1=valid).
        std::vector<std::pair<std::uint64_t, float>> sparseOverlay; ///< Sparse overlay (cell index -> modified value).
    };

    StateHeader header{};                  ///< Simulation state metadata (step index, status, etc.).
    GridSpec grid{};                       ///< Grid configuration (width, height, depth).
    BoundaryMode boundaryMode = BoundaryMode::Clamp; ///< Boundary handling mode (Clamp, Wrap, Reflect).
    GridTopologyBackend topologyBackend = GridTopologyBackend::Cartesian2D; ///< Grid topology (Cartesian2D, etc.).
    MemoryLayoutPolicy memoryLayout{};     ///< Memory layout policy (for introspection).
    std::uint64_t runIdentityHash = 0;     ///< Hash of run configuration (validated on load).
    std::uint64_t profileFingerprint = 0;  ///< Hash of profile definition (validated on load).
    std::string checkpointLabel;           ///< Label for this snapshot (e.g., "step_1000").
    std::uint64_t payloadBytes = 0;        ///< Total serialized size estimate.
    std::uint64_t stateHash = 0;           ///< Hash of all field values (for determinism checking).
    std::vector<FieldPayload> fields;      ///< All field data payloads.
};

// =============================================================================
// State Store Class
// =============================================================================

/// Manages simulation field data including allocation, access, and persistence.
/// Provides field storage with support for sparse overlays, validity masks,
/// and boundary handling. Thread-safe for reads; exclusive write via WriteSession.
///
/// ## Field Lifecycle
/// 1. Create StateStore with grid spec.
/// 2. Call addVariable() or allocateScalarField() for each field.
/// 3. Use scalarField() or WriteSession for read/write access.
/// 4. Call createSnapshot() to checkpoint state.
/// 5. Call loadSnapshot() to restore state.
///
/// ## Write Access Control
/// Modifications to fields must go through WriteSession, which enforces
/// per-caller access control. Multiple subsystems can write to different
/// fields concurrently via separate WriteSession instances (future work).
///
/// ## Performance Notes
/// - scalarFieldRawPtr() provides fast pointer access for inner loops.
/// - Field handles (getFieldHandle) cache field lookups for performance.
/// - Sparse overlays allow efficient cell-level modifications without full copies.
/// - Validity masks track invalid cells efficiently (1 byte per cell).
///
class StateStore {
public:
    /// Kind of access performed on a field for monitoring/auditing.
    enum class AccessKind : std::uint8_t {
        Read = 0,   ///< Read-only field access.
        Write = 1   ///< Modification to field.
    };

    /// Observer callback type for field access monitoring.
    /// Called synchronously during field access for logging/auditing.
    using AccessObserver = std::function<void(const std::string&, AccessKind)>;

    /// Handle type for fast field access (opaque, use for lookup caching).
    using FieldHandle = std::size_t;
    /// Invalid handle value for error handling.
    static constexpr FieldHandle InvalidHandle = static_cast<std::size_t>(-1);

    /// Write session for scoped field modification with access control.
    /// Ensures only the caller can modify their allowed fields.
    /// Only one WriteSession can be active per StateStore at a time.
    class WriteSession {
    public:
        /// Constructs a write session for the given state store.
        /// - stateStore: target store to modify.
        /// - ownerName: identifier for the caller (for access logging).
        /// - allowedVariables: list of variable names this session can write.
        /// Precondition: allowedVariables must be valid field names in the store.
        /// Postcondition: Only these variables can be modified via this session.
        /// Failure: Attempting to write disallowed variables silently fails (emits warning).
        WriteSession(StateStore& stateStore, std::string ownerName, std::vector<std::string> allowedVariables);

        /// Sets a scalar value at the given cell.
        /// Precondition: variableName must be in allowedVariables.
        /// Postcondition: Cell is marked valid; sparse overlay updated if needed.
        void setScalar(const std::string& variableName, Cell cell, float value);

        /// Fills the entire field with a constant value.
        /// Precondition: variableName must be in allowedVariables.
        /// Postcondition: All cells marked valid and set to the given value.
        void fillScalar(const std::string& variableName, float value);

        /// Sets a sparse overlay value (for efficient cell-level modifications).
        /// Precondition: variableName must be in allowedVariables.
        /// Postcondition: Cell overlay entry created/updated; underlying dense value unchanged.
        void setOverlayScalar(const std::string& variableName, Cell cell, float value);

        /// Clears a sparse overlay entry for the given cell.
        /// Precondition: variableName must be in allowedVariables.
        /// Postcondition: Overlay entry removed; reads will use underlying dense value.
        void clearOverlayScalar(const std::string& variableName, Cell cell);

        /// Marks a scalar value as invalid at the given cell.
        /// Precondition: variableName must be in allowedVariables.
        /// Postcondition: Cell validity mask set to 0; sampling may return std::nullopt.
        void invalidateScalar(const std::string& variableName, Cell cell);

        /// Returns a field handle for fast access (caches lookup).
        /// Returns InvalidHandle if the field does not exist.
        [[nodiscard]] FieldHandle getFieldHandle(const std::string& variableName) const noexcept;

        /// Fast scalar setting using field handle instead of name (for inner loops).
        /// Precondition: handle must be valid (from getFieldHandle).
        /// Postcondition: Cell is marked valid and updated.
        void setScalarFast(FieldHandle handle, Cell cell, float value);

    private:
        // Checks if the given variable name is in the allowed set.
        [[nodiscard]] bool isAllowed(const std::string& variableName) const;

        StateStore& stateStore_;
        std::string ownerName_;
        std::vector<std::string> allowedVariables_;
    };

    /// Constructs a state store with the given grid and configuration.
    /// - grid: grid specification (width, height, depth).
    /// - boundaryMode: boundary handling (Clamp, Wrap, Reflect).
    /// - topologyBackend: grid topology (Cartesian2D, Hexagonal2D, etc.).
    /// - memoryLayoutPolicy: memory layout policy (for optimization).
    /// Postcondition: Empty store with no fields allocated.
    explicit StateStore(
        GridSpec grid,
        BoundaryMode boundaryMode = BoundaryMode::Clamp,
        GridTopologyBackend topologyBackend = GridTopologyBackend::Cartesian2D,
        MemoryLayoutPolicy memoryLayoutPolicy = MemoryLayoutPolicy{});

    /// Adds a new variable with the given specification.
    /// Returns a field handle for fast access.
    /// Precondition: variableName must be unique.
    /// Postcondition: New field allocated and ready for access.
    /// Failure: Returns InvalidHandle if name is duplicate or invalid.
    [[nodiscard]] FieldHandle addVariable(const VariableSpec& spec);

    /// Allocates storage for a new scalar field.
    /// Legacy method (use addVariable() for new code).
    /// Precondition: field name must be unique.
    /// Postcondition: New field allocated with zero values and all cells valid.
    void allocateScalarField(const VariableSpec& spec);

    /// Returns true if a field with the given name exists.
    /// Thread-safe for concurrent reads.
    [[nodiscard]] bool hasField(const std::string& name) const noexcept;

    /// Returns the list of all variable names in the store.
    /// Returned in registration order.
    [[nodiscard]] std::vector<std::string> variableNames() const;

    /// Registers an alias for a semantic key to a variable name.
    /// Used for resolving generic keys (e.g., "elevation") to field names.
    /// Precondition: variableName must exist in the store.
    /// Postcondition: Alias registered; can be resolved via resolveFieldAlias().
    void registerFieldAlias(const std::string& semanticKey, const std::string& variableName);

    /// Returns true if an alias exists for the semantic key.
    [[nodiscard]] bool hasFieldAlias(const std::string& semanticKey) const noexcept;

    /// Resolves a semantic key to its corresponding variable name, if an alias exists.
    /// Returns std::nullopt if no alias is registered.
    [[nodiscard]] std::optional<std::string> resolveFieldAlias(const std::string& semanticKey) const;

    /// Returns all registered field aliases (semantic key -> variable name).
    [[nodiscard]] const std::unordered_map<std::string, std::string>& fieldAliases() const noexcept { return fieldAliases_; }

    /// Clears all registered field aliases.
    void clearFieldAliases() noexcept;

    /// Returns the logical number of cells for a field.
    /// This is the product of grid dimensions; typically width * height.
    [[nodiscard]] std::uint64_t logicalCellCount(const std::string& name) const;

    /// Returns a const reference to the scalar field data vector.
    /// Precondition: field must exist.
    /// Throws std::out_of_range if field does not exist.
    /// The returned reference is valid until next allocateScalarField() call.
    [[nodiscard]] const std::vector<float>& scalarField(const std::string& name) const;

    /// Attempts to sample a scalar value at the given cell position.
    /// Handles boundary modes transparently.
    /// Returns std::nullopt if:
    /// - Cell is out of bounds (after boundary resolution fails).
    /// - Cell is marked invalid in the validity mask.
    /// Otherwise, returns the cell value (with sparse overlay applied if present).
    [[nodiscard]] std::optional<float> trySampleScalar(const std::string& name, CellSigned cell) const;

    /// Clamps all values in a field to the given range [minValue, maxValue].
    /// Any value outside the range is clamped to the nearest boundary.
    /// Applies to dense field; sparse overlay entries are also clamped.
    void clampField(const std::string& name, float minValue, float maxValue);

    /// Returns metadata for all fields in the store.
    /// Includes offsets, alignments, and overlay counts.
    [[nodiscard]] std::vector<FieldStorageMetadata> fieldMetadata() const;

    /// Computes a hash of the current state for determinism checking.
    /// All field values, validity masks, and sparse overlays are included.
    /// Returns a deterministic 64-bit hash suitable for replay validation.
    /// Must produce bitwise-identical results for identical field states.
    [[nodiscard]] std::uint64_t stateHash() const noexcept;

    /// Returns a pointer to the flat float array for the given handle.
    /// For performance-critical inner loops; avoid boundary/validity checks.
    /// The pointer is valid as long as no allocateScalarField() calls are made.
    /// Precondition: handle must be valid (from getFieldHandle).
    /// Returns nullptr if handle is invalid.
    [[nodiscard]] const float* scalarFieldRawPtr(FieldHandle handle) const noexcept;

    /// Returns a mutable pointer to the flat float array for the given handle.
    /// For performance-critical inner loops; direct modification (bypasses WriteSession).
    /// Use only if you manage access control externally.
    /// The pointer is valid as long as no allocateScalarField() calls are made.
    /// Precondition: handle must be valid (from getFieldHandle).
    /// Returns nullptr if handle is invalid.
    [[nodiscard]] float* scalarFieldRawPtrMut(FieldHandle handle) const noexcept;

    /// Returns a pointer to the validity mask array for the given handle.
    /// One byte per logical cell; 0=invalid, 1=valid.
    /// The pointer is valid as long as no allocateScalarField() calls are made.
    /// Precondition: handle must be valid (from getFieldHandle).
    /// Returns nullptr if handle is invalid.
    [[nodiscard]] const std::uint8_t* validityMaskRawPtr(FieldHandle handle) const noexcept;

    /// Sets an observer callback for field access monitoring.
    /// Called synchronously when fields are read or written.
    /// Used for profiling, logging, and determinism validation.
    void setAccessObserver(AccessObserver observer);

    /// Clears the access observer callback.
    void clearAccessObserver();

    /// Converts cell coordinates to linear array index.
    /// Precondition: cell must be in bounds (0 <= x < width, 0 <= y < height).
    [[nodiscard]] std::uint64_t indexOf(Cell cell) const;

    /// Converts signed cell coordinates to linear array index.
    /// Applies boundary mode to handle out-of-bounds coordinates.
    /// Precondition: boundary mode must be defined (Clamp, Wrap, Reflect).
    [[nodiscard]] std::uint64_t indexOf(CellSigned cell) const;

    /// Converts linear index back to cell coordinates.
    /// Inverse of indexOf(Cell).
    /// Precondition: index must be < logicalCellCount().
    [[nodiscard]] Cell cellFromIndex(std::uint64_t index) const;

    /// Resolves out-of-bounds coordinates according to the boundary mode.
    /// - Clamp: clamps to [0, width-1] x [0, height-1].
    /// - Wrap: wraps around using modulo arithmetic.
    /// - Reflect: reflects at boundaries.
    /// Returns the resolved in-bounds cell, or throws if boundary mode is invalid.
    [[nodiscard]] Cell resolveBoundary(CellSigned cell) const;

    /// Returns the grid specification (width, height, depth, etc.).
    [[nodiscard]] const GridSpec& grid() const noexcept { return grid_; }

    /// Returns the boundary mode (Clamp, Wrap, Reflect).
    [[nodiscard]] BoundaryMode boundaryMode() const noexcept { return boundaryMode_; }

    /// Returns the grid topology backend (Cartesian2D, Hexagonal2D, etc.).
    [[nodiscard]] GridTopologyBackend topologyBackend() const noexcept { return topologyBackend_; }

    /// Returns the memory layout policy (for introspection).
    [[nodiscard]] const MemoryLayoutPolicy& memoryLayoutPolicy() const noexcept { return memoryLayoutPolicy_; }

    /// Returns the computed memory layout for all fields.
    /// Includes offsets, alignments, and field layout details.
    [[nodiscard]] const MemoryLayout& getLayout() const noexcept { return layout_; }

    /// Returns the current state header (step index, timestamp, status).
    [[nodiscard]] const StateHeader& header() const noexcept { return header_; }

    /// Updates the state header (step index, timestamp, status).
    void setHeader(const StateHeader& header) noexcept { header_ = header; }

    /// Creates a snapshot of the current state for checkpointing.
    /// - runIdentityHash: hash of run configuration (for validation on load).
    /// - profileFingerprint: hash of profile definition (for validation on load).
    /// - checkpointLabel: human-readable label (e.g., "step_1000").
    /// - computeHash: if true, computes stateHash for determinism validation; if false, stateHash is 0 (for display-only snapshots).
    /// Returns: StateStoreSnapshot with all fields, metadata, and hashes.
    /// Precondition: All field values must be finite; invalid cells use validity mask.
    /// Postcondition: Snapshot is independent of StateStore and can be serialized/persisted.
    [[nodiscard]] StateStoreSnapshot createSnapshot(
        std::uint64_t runIdentityHash,
        std::uint64_t profileFingerprint,
        std::string checkpointLabel,
        bool computeHash = true) const;

    /// Loads a snapshot, restoring state from a checkpoint.
    /// - snapshot: source snapshot to load.
    /// - expectedRunIdentityHash: expected run identity hash (for validation).
    /// - expectedProfileFingerprint: expected profile fingerprint (for validation).
    /// Precondition: Grid and fields must be compatible with snapshot.
    /// Postcondition: All field values, validity masks, and overlays restored.
    /// Failure: Throws std::runtime_error if run identity or profile mismatch.
    void loadSnapshot(const StateStoreSnapshot& snapshot, std::uint64_t expectedRunIdentityHash, std::uint64_t expectedProfileFingerprint);

    /// Returns the field handle for a variable name (for fast access).
    /// Returns InvalidHandle if the field does not exist.
    /// Handles are stable as long as no new fields are allocated.
    [[nodiscard]] FieldHandle getFieldHandle(const std::string& name) const noexcept;

    /// Fast scalar sampling using field handle instead of name.
    /// Avoids hash map lookup; ideal for inner loops.
    /// Precondition: handle must be valid (from getFieldHandle).
    /// Returns std::nullopt if cell is invalid or out of bounds.
    [[nodiscard]] std::optional<float> trySampleScalarFast(FieldHandle handle, CellSigned cell) const;

    /// Returns the scalar field data vector for a handle (fast access).
    /// Precondition: handle must be valid (from getFieldHandle).
    /// Throws std::out_of_range if handle is invalid.
    [[nodiscard]] const std::vector<float>& scalarFieldFast(FieldHandle handle) const;

private:
    // WriteSession needs access to private members for field modification.
    friend class WriteSession;

    // Emits an access event to the observer callback (if registered).
    void emitAccess(const std::string& variableName, AccessKind kind) const;

    // Internal implementation for setting a scalar value (used by WriteSession).
    void setScalarInternal(const std::string& variableName, Cell cell, float value);
    // Internal implementation for filling a field with a constant value.
    void fillScalarInternal(const std::string& variableName, float value);
    // Internal implementation for setting a sparse overlay value.
    void setOverlayScalarInternal(const std::string& variableName, Cell cell, float value);
    // Internal implementation for clearing a sparse overlay entry.
    void clearOverlayScalarInternal(const std::string& variableName, Cell cell);
    // Internal implementation for invalidating a cell value.
    void invalidateScalarInternal(const std::string& variableName, Cell cell);

    /// Storage for a single scalar field including values, validity mask, and sparse overlay.
    struct ScalarFieldStorage {
        VariableSpec spec;                                  ///< Variable specification.
        std::uint64_t logicalCellCount = 0;                 ///< Number of logical cells.
        std::uint64_t paddedCellCount = 0;                  ///< Padded cell count for alignment.
        std::vector<float> values;                          ///< Dense field values (one per cell).
        std::vector<std::uint8_t> validityMask;             ///< Validity mask (0=invalid, 1=valid).
        std::unordered_map<std::uint64_t, float> sparseOverlay; ///< Sparse overlay (cell index -> modified value).
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
