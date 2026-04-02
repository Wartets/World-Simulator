#pragma once

#include "ws/core/determinism.hpp"
#include "ws/core/types.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ws {

struct StateHeader {
    std::uint64_t stepIndex = 0;
    std::uint64_t timestampTicks = 0;
    RuntimeStatus status = RuntimeStatus::Created;
};

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

struct MemoryLayout {
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

struct StateStoreSnapshot {
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

class StateStore {
public:
    enum class AccessKind : std::uint8_t {
        Read = 0,
        Write = 1
    };

    using AccessObserver = std::function<void(const std::string&, AccessKind)>;

    using FieldHandle = std::size_t;
    static constexpr FieldHandle InvalidHandle = static_cast<std::size_t>(-1);

    class WriteSession {
    public:
        WriteSession(StateStore& stateStore, std::string ownerName, std::vector<std::string> allowedVariables);

        void setScalar(const std::string& variableName, Cell cell, float value);
        void fillScalar(const std::string& variableName, float value);
        void setOverlayScalar(const std::string& variableName, Cell cell, float value);
        void clearOverlayScalar(const std::string& variableName, Cell cell);
        void invalidateScalar(const std::string& variableName, Cell cell);

        FieldHandle getFieldHandle(const std::string& variableName) const noexcept;
        void setScalarFast(FieldHandle handle, Cell cell, float value);

    private:
        [[nodiscard]] bool isAllowed(const std::string& variableName) const;

        StateStore& stateStore_;
        std::string ownerName_;
        std::vector<std::string> allowedVariables_;
    };

    explicit StateStore(
        GridSpec grid,
        BoundaryMode boundaryMode = BoundaryMode::Clamp,
        GridTopologyBackend topologyBackend = GridTopologyBackend::Cartesian2D,
        MemoryLayoutPolicy memoryLayoutPolicy = MemoryLayoutPolicy{});

    [[nodiscard]] FieldHandle addVariable(const VariableSpec& spec);
    void allocateScalarField(const VariableSpec& spec);
    [[nodiscard]] bool hasField(const std::string& name) const noexcept;
    [[nodiscard]] std::vector<std::string> variableNames() const;
    void registerFieldAlias(const std::string& semanticKey, const std::string& variableName);
    [[nodiscard]] bool hasFieldAlias(const std::string& semanticKey) const noexcept;
    [[nodiscard]] std::optional<std::string> resolveFieldAlias(const std::string& semanticKey) const;
    void clearFieldAliases() noexcept;
    [[nodiscard]] std::uint64_t logicalCellCount(const std::string& name) const;
    [[nodiscard]] const std::vector<float>& scalarField(const std::string& name) const;
    [[nodiscard]] std::optional<float> trySampleScalar(const std::string& name, CellSigned cell) const;
    void clampField(const std::string& name, float minValue, float maxValue);
    [[nodiscard]] std::vector<FieldStorageMetadata> fieldMetadata() const;
    [[nodiscard]] std::uint64_t stateHash() const noexcept;

    // Fast raw-buffer accessors for performance-critical inner loops.
    // Returns a pointer to the flat float array for the given handle.
    // The pointer is valid as long as no allocateScalarField() calls are made.
    [[nodiscard]] const float* scalarFieldRawPtr(FieldHandle handle) const noexcept;
    [[nodiscard]] float* scalarFieldRawPtrMut(FieldHandle handle) const noexcept;
    [[nodiscard]] const std::uint8_t* validityMaskRawPtr(FieldHandle handle) const noexcept;

    void setAccessObserver(AccessObserver observer);
    void clearAccessObserver();

    [[nodiscard]] std::uint64_t indexOf(Cell cell) const;
    [[nodiscard]] std::uint64_t indexOf(CellSigned cell) const;
    [[nodiscard]] Cell cellFromIndex(std::uint64_t index) const;
    [[nodiscard]] Cell resolveBoundary(CellSigned cell) const;
    [[nodiscard]] const GridSpec& grid() const noexcept { return grid_; }
    [[nodiscard]] BoundaryMode boundaryMode() const noexcept { return boundaryMode_; }
    [[nodiscard]] GridTopologyBackend topologyBackend() const noexcept { return topologyBackend_; }
    [[nodiscard]] const MemoryLayoutPolicy& memoryLayoutPolicy() const noexcept { return memoryLayoutPolicy_; }
    [[nodiscard]] const MemoryLayout& getLayout() const noexcept { return layout_; }

    [[nodiscard]] const StateHeader& header() const noexcept { return header_; }
    void setHeader(const StateHeader& header) noexcept { header_ = header; }

    // computeHash=false skips the O(fields*cells) state hash — use for display
    // snapshots. Set computeHash=true only when saving to disk or running
    // determinism checks.
    [[nodiscard]] StateStoreSnapshot createSnapshot(
        std::uint64_t runIdentityHash,
        std::uint64_t profileFingerprint,
        std::string checkpointLabel,
        bool computeHash = true) const;
    void loadSnapshot(const StateStoreSnapshot& snapshot, std::uint64_t expectedRunIdentityHash, std::uint64_t expectedProfileFingerprint);

    [[nodiscard]] FieldHandle getFieldHandle(const std::string& name) const noexcept;
    [[nodiscard]] std::optional<float> trySampleScalarFast(FieldHandle handle, CellSigned cell) const;
    [[nodiscard]] const std::vector<float>& scalarFieldFast(FieldHandle handle) const;

private:
    friend class WriteSession;

    void emitAccess(const std::string& variableName, AccessKind kind) const;

    void setScalarInternal(const std::string& variableName, Cell cell, float value);
    void fillScalarInternal(const std::string& variableName, float value);
    void setOverlayScalarInternal(const std::string& variableName, Cell cell, float value);
    void clearOverlayScalarInternal(const std::string& variableName, Cell cell);
    void invalidateScalarInternal(const std::string& variableName, Cell cell);

    struct ScalarFieldStorage {
        VariableSpec spec;
        std::uint64_t logicalCellCount = 0;
        std::uint64_t paddedCellCount = 0;
        std::vector<float> values;
        std::vector<std::uint8_t> validityMask;
        std::unordered_map<std::uint64_t, float> sparseOverlay;
    };

    [[nodiscard]] static std::uint64_t roundUpToMultiple(std::uint64_t value, std::uint64_t multiple);
    [[nodiscard]] ScalarFieldStorage& fieldForWrite(const std::string& variableName);
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
