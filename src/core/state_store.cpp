#include "ws/core/state_store.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ws {

StateStore::WriteSession::WriteSession(StateStore& stateStore, std::string ownerName, std::vector<std::string> allowedVariables)
    : stateStore_(stateStore), ownerName_(std::move(ownerName)), allowedVariables_(std::move(allowedVariables)) {
    std::sort(allowedVariables_.begin(), allowedVariables_.end());
    allowedVariables_.erase(std::unique(allowedVariables_.begin(), allowedVariables_.end()), allowedVariables_.end());

    if (ownerName_.empty()) {
        throw std::invalid_argument("WriteSession owner name must not be empty");
    }
}

void StateStore::WriteSession::setScalar(const std::string& variableName, const Cell cell, const float value) {
    if (!isAllowed(variableName)) {
        throw std::runtime_error("Write denied for variable '" + variableName + "' by owner '" + ownerName_ + "'");
    }
    stateStore_.setScalarInternal(variableName, cell, value);
}


void StateStore::WriteSession::setOverlayScalar(const std::string& variableName, const Cell cell, const float value) {
    if (!isAllowed(variableName)) {
        throw std::runtime_error("Overlay write denied for variable '" + variableName + "' by owner '" + ownerName_ + "'");
    }
    stateStore_.setOverlayScalarInternal(variableName, cell, value);
}

void StateStore::WriteSession::clearOverlayScalar(const std::string& variableName, const Cell cell) {
    if (!isAllowed(variableName)) {
        throw std::runtime_error("Overlay clear denied for variable '" + variableName + "' by owner '" + ownerName_ + "'");
    }
    stateStore_.clearOverlayScalarInternal(variableName, cell);
}

void StateStore::WriteSession::invalidateScalar(const std::string& variableName, const Cell cell) {
    if (!isAllowed(variableName)) {
        throw std::runtime_error("Invalidate denied for variable '" + variableName + "' by owner '" + ownerName_ + "'");
    }
    stateStore_.invalidateScalarInternal(variableName, cell);
}
void StateStore::WriteSession::fillScalar(const std::string& variableName, const float value) {
    if (!isAllowed(variableName)) {
        throw std::runtime_error("Fill denied for variable '" + variableName + "' by owner '" + ownerName_ + "'");
    }
    stateStore_.fillScalarInternal(variableName, value);
}

bool StateStore::WriteSession::isAllowed(const std::string& variableName) const {
    return std::binary_search(allowedVariables_.begin(), allowedVariables_.end(), variableName);
}

void StateStore::setAccessObserver(AccessObserver observer) {
    accessObserver_ = std::move(observer);
}

void StateStore::clearAccessObserver() {
    accessObserver_ = nullptr;
}

void StateStore::emitAccess(const std::string& variableName, const AccessKind kind) const {
    if (accessObserver_) {
        accessObserver_(variableName, kind);
    }
}

StateStore::StateStore(
    const GridSpec grid,
    const BoundaryMode boundaryMode,
    const GridTopologyBackend topologyBackend,
    const MemoryLayoutPolicy memoryLayoutPolicy)
    : grid_(grid),
      boundaryMode_(boundaryMode),
      topologyBackend_(topologyBackend),
      memoryLayoutPolicy_(memoryLayoutPolicy) {
    grid_.validate();
    memoryLayoutPolicy_.validate();

    if (topologyBackend_ != GridTopologyBackend::Cartesian2D) {
        throw std::invalid_argument("Unsupported GridTopologyBackend");
    }

    layout_.grid = grid_;
    layout_.policy = memoryLayoutPolicy_;
}

std::uint64_t StateStore::roundUpToMultiple(const std::uint64_t value, const std::uint64_t multiple) {
    if (multiple == 0) {
        throw std::invalid_argument("roundUpToMultiple requires non-zero multiple");
    }
    const std::uint64_t remainder = value % multiple;
    if (remainder == 0) {
        return value;
    }
    return value + (multiple - remainder);
}

void StateStore::allocateScalarField(const VariableSpec& spec) {
    (void)addVariable(spec);
}

StateStore::FieldHandle StateStore::addVariable(const VariableSpec& spec) {
    if (spec.name.empty()) {
        throw std::invalid_argument("VariableSpec.name must not be empty");
    }

    if (spec.dataType != VariableDataType::Float32) {
        throw std::invalid_argument("StateStore currently supports Float32 scalar fields only");
    }

    if (fieldNameToIndex_.find(spec.name) != fieldNameToIndex_.end()) {
        throw std::invalid_argument("Field already allocated for variable: " + spec.name);
    }

    const std::uint64_t logicalCellCount = grid_.cellCount();
    if (logicalCellCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::overflow_error("Grid cell count exceeds max addressable size_t");
    }

    const std::uint64_t tileCells = memoryLayoutPolicy_.tileCellCount();
    const std::uint64_t paddedCellCount = roundUpToMultiple(logicalCellCount, tileCells);
    if (paddedCellCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::overflow_error("Padded cell count exceeds max addressable size_t");
    }

    ScalarFieldStorage storage;
    storage.spec = spec;
    storage.logicalCellCount = logicalCellCount;
    storage.paddedCellCount = paddedCellCount;
    storage.values.assign(static_cast<std::size_t>(paddedCellCount), 0.0f);
    storage.validityMask.assign(static_cast<std::size_t>(paddedCellCount), 0u);

    MemoryLayout::FieldLayout layoutEntry;
    layoutEntry.spec = spec;
    layoutEntry.logicalCellCount = logicalCellCount;
    layoutEntry.paddedCellCount = paddedCellCount;
    layoutEntry.alignmentBytes = memoryLayoutPolicy_.alignmentBytes;
    layoutEntry.valuesOffsetBytes = roundUpToMultiple(layout_.valuesBufferBytes, memoryLayoutPolicy_.alignmentBytes);
    layoutEntry.validityMaskOffsetBytes = roundUpToMultiple(layout_.validityMaskBufferBytes, memoryLayoutPolicy_.alignmentBytes);

    layout_.valuesBufferBytes = layoutEntry.valuesOffsetBytes + paddedCellCount * sizeof(float);
    layout_.validityMaskBufferBytes = layoutEntry.validityMaskOffsetBytes + paddedCellCount * sizeof(std::uint8_t);
    layout_.fields.push_back(layoutEntry);

    fieldNameToIndex_[spec.name] = scalarFields_.size();
    scalarFields_.push_back(std::move(storage));
    variableOrder_.push_back(spec);

    return fieldNameToIndex_.at(spec.name);
}

std::optional<float> StateStore::trySampleScalar(const std::string& name, const CellSigned cell) const {
    emitAccess(name, AccessKind::Read);
    const auto& field = fieldForRead(name);
    const std::uint64_t idx = indexOf(cell);

    if (field.validityMask.at(static_cast<std::size_t>(idx)) == 0u) {
        return std::nullopt;
    }

    const auto overlayIterator = field.sparseOverlay.find(idx);
    if (overlayIterator != field.sparseOverlay.end()) {
        return overlayIterator->second;
    }

    return field.values.at(static_cast<std::size_t>(idx));
}

std::vector<FieldStorageMetadata> StateStore::fieldMetadata() const {
    std::vector<FieldStorageMetadata> result;
    result.reserve(variableOrder_.size());

    for (const auto& variable : variableOrder_) {
        const auto& field = fieldForRead(variable.name);
        result.push_back(FieldStorageMetadata{
            .spec = field.spec,
            .logicalCellCount = field.logicalCellCount,
            .paddedCellCount = field.paddedCellCount,
            .valuesOffsetBytes = layout_.fields.at(result.size()).valuesOffsetBytes,
            .validityMaskOffsetBytes = layout_.fields.at(result.size()).validityMaskOffsetBytes,
            .alignmentBytes = memoryLayoutPolicy_.alignmentBytes,
            .tileWidth = memoryLayoutPolicy_.tileWidth,
            .tileHeight = memoryLayoutPolicy_.tileHeight,
            .overlayEntryCount = static_cast<std::uint64_t>(field.sparseOverlay.size())});
    }

    return result;
}

bool StateStore::hasField(const std::string& name) const noexcept {
    return fieldNameToIndex_.find(name) != fieldNameToIndex_.end();
}

std::vector<std::string> StateStore::variableNames() const {
    std::vector<std::string> names;
    names.reserve(variableOrder_.size());
    for (const auto& variable : variableOrder_) {
        names.push_back(variable.name);
    }
    return names;
}

std::uint64_t StateStore::logicalCellCount(const std::string& name) const {
    return fieldForRead(name).logicalCellCount;
}

const std::vector<float>& StateStore::scalarField(const std::string& name) const {
    emitAccess(name, AccessKind::Read);
    return fieldForRead(name).values;
}

std::uint64_t StateStore::stateHash() const noexcept {
    std::uint64_t hash = DeterministicHash::offsetBasis;
    hash = DeterministicHash::combine(hash, DeterministicHash::hashPod(header_.stepIndex));
    hash = DeterministicHash::combine(hash, DeterministicHash::hashPod(header_.timestampTicks));
    hash = DeterministicHash::combine(hash, static_cast<std::uint64_t>(header_.status));

    for (const auto& variable : variableOrder_) {
        hash = DeterministicHash::combine(hash, DeterministicHash::hashString(variable.name));

        const auto& storage = scalarFields_[fieldNameToIndex_.at(variable.name)];
        for (std::size_t i = 0; i < static_cast<std::size_t>(storage.logicalCellCount); ++i) {
            hash = DeterministicHash::combine(hash, DeterministicHash::hashPod(storage.values[i]));
        }

        for (std::size_t i = 0; i < static_cast<std::size_t>(storage.logicalCellCount); ++i) {
            hash = DeterministicHash::combine(hash, DeterministicHash::hashPod(storage.validityMask[i]));
        }

        std::vector<std::pair<std::uint64_t, float>> sortedOverlay(storage.sparseOverlay.begin(), storage.sparseOverlay.end());
        std::sort(sortedOverlay.begin(), sortedOverlay.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        for (const auto& [idx, value] : sortedOverlay) {
            hash = DeterministicHash::combine(hash, DeterministicHash::hashPod(idx));
            hash = DeterministicHash::combine(hash, DeterministicHash::hashPod(value));
        }
    }

    return hash;
}

std::uint64_t StateStore::indexOf(const Cell cell) const {
    if (cell.x >= grid_.width || cell.y >= grid_.height) {
        throw std::out_of_range("Cell coordinate out of grid bounds");
    }
    return static_cast<std::uint64_t>(cell.y) * static_cast<std::uint64_t>(grid_.width) + static_cast<std::uint64_t>(cell.x);
}

Cell StateStore::resolveBoundary(const CellSigned cell) const {
    const auto width = static_cast<std::int64_t>(grid_.width);
    const auto height = static_cast<std::int64_t>(grid_.height);

    auto resolveAxis = [](const std::int64_t value, const std::int64_t extent, const BoundaryMode mode) -> std::uint32_t {
        if (mode == BoundaryMode::Clamp) {
            if (value < 0) {
                return 0;
            }
            if (value >= extent) {
                return static_cast<std::uint32_t>(extent - 1);
            }
            return static_cast<std::uint32_t>(value);
        }

        const std::int64_t wrapped = ((value % extent) + extent) % extent;
        return static_cast<std::uint32_t>(wrapped);
    };

    return Cell{
        .x = resolveAxis(cell.x, width, boundaryMode_),
        .y = resolveAxis(cell.y, height, boundaryMode_)};
}

std::uint64_t StateStore::indexOf(const CellSigned cell) const {
    return indexOf(resolveBoundary(cell));
}

Cell StateStore::cellFromIndex(const std::uint64_t index) const {
    const std::uint64_t totalCells = grid_.cellCount();
    if (index >= totalCells) {
        throw std::out_of_range("Cell index out of range");
    }

    const std::uint32_t x = static_cast<std::uint32_t>(index % grid_.width);
    const std::uint32_t y = static_cast<std::uint32_t>(index / grid_.width);
    return Cell{x, y};
}

StateStore::ScalarFieldStorage& StateStore::fieldForWrite(const std::string& variableName) {
    auto iterator = fieldNameToIndex_.find(variableName);
    if (iterator == fieldNameToIndex_.end()) {
        throw std::out_of_range("Unknown field write target: " + variableName);
    }
    return scalarFields_[iterator->second];
}

const StateStore::ScalarFieldStorage& StateStore::fieldForRead(const std::string& variableName) const {
    const auto iterator = fieldNameToIndex_.find(variableName);
    if (iterator == fieldNameToIndex_.end()) {
        throw std::out_of_range("Unknown field: " + variableName);
    }
    return scalarFields_[iterator->second];
}

void StateStore::setScalarInternal(const std::string& variableName, const Cell cell, const float value) {
    emitAccess(variableName, AccessKind::Write);
    if (!std::isfinite(value)) {
        throw std::runtime_error("StateStore rejected non-finite scalar write for variable: " + variableName);
    }
    auto& field = fieldForWrite(variableName);
    const std::uint64_t idx = indexOf(cell);
    field.values.at(static_cast<std::size_t>(idx)) = value;
    field.validityMask.at(static_cast<std::size_t>(idx)) = 1u;
}

void StateStore::fillScalarInternal(const std::string& variableName, const float value) {
    emitAccess(variableName, AccessKind::Write);
    if (!std::isfinite(value)) {
        throw std::runtime_error("StateStore rejected non-finite scalar fill for variable: " + variableName);
    }
    auto& field = fieldForWrite(variableName);
    const auto logicalCount = static_cast<std::size_t>(field.logicalCellCount);

    std::fill(field.values.begin(), field.values.begin() + static_cast<std::ptrdiff_t>(logicalCount), value);
    std::fill(field.validityMask.begin(), field.validityMask.begin() + static_cast<std::ptrdiff_t>(logicalCount), 1u);
}

void StateStore::setOverlayScalarInternal(const std::string& variableName, const Cell cell, const float value) {
    emitAccess(variableName, AccessKind::Write);
    if (!std::isfinite(value)) {
        throw std::runtime_error("StateStore rejected non-finite overlay write for variable: " + variableName);
    }
    auto& field = fieldForWrite(variableName);
    const std::uint64_t idx = indexOf(cell);
    field.sparseOverlay.insert_or_assign(idx, value);
    field.validityMask.at(static_cast<std::size_t>(idx)) = 1u;
}

void StateStore::clearOverlayScalarInternal(const std::string& variableName, const Cell cell) {
    emitAccess(variableName, AccessKind::Write);
    auto& field = fieldForWrite(variableName);
    const std::uint64_t idx = indexOf(cell);
    field.sparseOverlay.erase(idx);
}

void StateStore::invalidateScalarInternal(const std::string& variableName, const Cell cell) {
    emitAccess(variableName, AccessKind::Write);
    auto& field = fieldForWrite(variableName);
    const std::uint64_t idx = indexOf(cell);
    field.validityMask.at(static_cast<std::size_t>(idx)) = 0u;
}

StateStoreSnapshot StateStore::createSnapshot(
    const std::uint64_t runIdentityHash,
    const std::uint64_t profileFingerprint,
    std::string checkpointLabel,
    bool computeHash) const {
    if (checkpointLabel.empty()) {
        checkpointLabel = "state_checkpoint";
    }

    StateStoreSnapshot snapshot;
    snapshot.header = header_;
    snapshot.grid = grid_;
    snapshot.boundaryMode = boundaryMode_;
    snapshot.topologyBackend = topologyBackend_;
    snapshot.memoryLayout = memoryLayoutPolicy_;
    snapshot.runIdentityHash = runIdentityHash;
    snapshot.profileFingerprint = profileFingerprint;
    snapshot.checkpointLabel = std::move(checkpointLabel);
    snapshot.fields.reserve(variableOrder_.size());

    std::uint64_t payloadBytes = 0;
    for (const auto& spec : variableOrder_) {
        const auto& field = fieldForRead(spec.name);

        StateStoreSnapshot::FieldPayload payload;
        payload.spec = field.spec;
        payload.values.assign(
            field.values.begin(),
            field.values.begin() + static_cast<std::ptrdiff_t>(field.logicalCellCount));
        payload.validityMask.assign(
            field.validityMask.begin(),
            field.validityMask.begin() + static_cast<std::ptrdiff_t>(field.logicalCellCount));
        payload.sparseOverlay.assign(field.sparseOverlay.begin(), field.sparseOverlay.end());
        std::sort(payload.sparseOverlay.begin(), payload.sparseOverlay.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });

        payloadBytes += static_cast<std::uint64_t>(payload.values.size()) * sizeof(float);
        payloadBytes += static_cast<std::uint64_t>(payload.validityMask.size()) * sizeof(std::uint8_t);
        payloadBytes += static_cast<std::uint64_t>(payload.sparseOverlay.size()) * (sizeof(std::uint64_t) + sizeof(float));

        snapshot.fields.push_back(std::move(payload));
    }

    snapshot.payloadBytes = payloadBytes;
    if (computeHash) {
        snapshot.stateHash = stateHash();
    } else {
        snapshot.stateHash = 0; // Explicitly zero when not computed
    }
    return snapshot;
}

void StateStore::loadSnapshot(
    const StateStoreSnapshot& snapshot,
    const std::uint64_t expectedRunIdentityHash,
    const std::uint64_t expectedProfileFingerprint) {
    if (snapshot.runIdentityHash != expectedRunIdentityHash) {
        throw std::invalid_argument("Snapshot run identity hash mismatch");
    }

    if (snapshot.profileFingerprint != expectedProfileFingerprint) {
        throw std::invalid_argument("Snapshot profile fingerprint mismatch");
    }

    if (snapshot.grid.width != grid_.width || snapshot.grid.height != grid_.height) {
        throw std::invalid_argument("Snapshot grid does not match active StateStore grid");
    }

    if (snapshot.boundaryMode != boundaryMode_) {
        throw std::invalid_argument("Snapshot boundary mode does not match active StateStore boundary mode");
    }

    if (snapshot.topologyBackend != topologyBackend_) {
        throw std::invalid_argument("Snapshot topology backend does not match active StateStore topology backend");
    }

    scalarFields_.clear();
    fieldNameToIndex_.clear();
    variableOrder_.clear();
    layout_.fields.clear();
    layout_.valuesBufferBytes = 0;
    layout_.validityMaskBufferBytes = 0;

    for (const auto& fieldPayload : snapshot.fields) {
        allocateScalarField(fieldPayload.spec);
        auto& field = fieldForWrite(fieldPayload.spec.name);

        if (fieldPayload.values.size() != static_cast<std::size_t>(field.logicalCellCount)) {
            throw std::invalid_argument("Snapshot field payload size mismatch for variable: " + fieldPayload.spec.name);
        }

        if (fieldPayload.validityMask.size() != static_cast<std::size_t>(field.logicalCellCount)) {
            throw std::invalid_argument("Snapshot validity mask size mismatch for variable: " + fieldPayload.spec.name);
        }

        std::copy(fieldPayload.values.begin(), fieldPayload.values.end(), field.values.begin());
        std::copy(fieldPayload.validityMask.begin(), fieldPayload.validityMask.end(), field.validityMask.begin());

        field.sparseOverlay.clear();
        for (const auto& [idx, value] : fieldPayload.sparseOverlay) {
            if (idx >= field.logicalCellCount) {
                throw std::invalid_argument("Snapshot sparse overlay index out of range for variable: " + fieldPayload.spec.name);
            }
            field.sparseOverlay.insert_or_assign(idx, value);
        }
    }

    header_ = snapshot.header;

    const std::uint64_t loadedHash = stateHash();
    if (loadedHash != snapshot.stateHash) {
        throw std::runtime_error("Snapshot integrity check failed: loaded state hash does not match snapshot hash");
    }
}


StateStore::FieldHandle StateStore::WriteSession::getFieldHandle(const std::string& variableName) const noexcept {
    if (!isAllowed(variableName)) return StateStore::InvalidHandle;
    return stateStore_.getFieldHandle(variableName);
}

void StateStore::WriteSession::setScalarFast(FieldHandle handle, Cell cell, float value) {
    if (handle == StateStore::InvalidHandle || handle >= stateStore_.scalarFields_.size()) return;
    if (!std::isfinite(value)) {
        throw std::runtime_error("StateStore rejected non-finite fast scalar write");
    }
    auto& field = stateStore_.scalarFields_[handle];
    const std::uint64_t idx = stateStore_.indexOf(cell);
    field.values[static_cast<std::size_t>(idx)] = value;
    field.validityMask[static_cast<std::size_t>(idx)] = 1u;
}

StateStore::FieldHandle StateStore::getFieldHandle(const std::string& name) const noexcept {
    auto it = fieldNameToIndex_.find(name);
    if (it != fieldNameToIndex_.end()) return it->second;
    return InvalidHandle;
}

std::optional<float> StateStore::trySampleScalarFast(FieldHandle handle, const CellSigned cell) const {
    if (handle == InvalidHandle || handle >= scalarFields_.size()) return std::nullopt;
    const auto& field = scalarFields_[handle];
    const std::uint64_t idx = indexOf(cell);
    if (field.validityMask[static_cast<std::size_t>(idx)] == 0u) return std::nullopt;
    const auto overlayIt = field.sparseOverlay.find(idx);
    if (overlayIt != field.sparseOverlay.end()) return overlayIt->second;
    return field.values[static_cast<std::size_t>(idx)];
}

const std::vector<float>& StateStore::scalarFieldFast(FieldHandle handle) const {
    if (handle == InvalidHandle || handle >= scalarFields_.size()) {
        throw std::out_of_range("Invalid fast handle");
    }
    return scalarFields_[handle].values;
}

const float* StateStore::scalarFieldRawPtr(FieldHandle handle) const noexcept {
    if (handle == InvalidHandle || handle >= scalarFields_.size()) return nullptr;
    return scalarFields_[handle].values.data();
}

float* StateStore::scalarFieldRawPtrMut(FieldHandle handle) const noexcept {
    if (handle == InvalidHandle || handle >= scalarFields_.size()) return nullptr;
    return const_cast<float*>(scalarFields_[handle].values.data());
}

const std::uint8_t* StateStore::validityMaskRawPtr(FieldHandle handle) const noexcept {
    if (handle == InvalidHandle || handle >= scalarFields_.size()) return nullptr;
    return scalarFields_[handle].validityMask.data();
}
} // namespace ws
