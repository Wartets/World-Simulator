#pragma once

#include "ws/core/types.hpp"

#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace ws {

struct ScalarWritePatch {
    std::string variableName;
    Cell cell;
    float value = 0.0f;
};

struct RuntimeInputFrame {
    std::vector<ScalarWritePatch> scalarPatches;
};

struct RuntimeEvent {
    std::string eventName;
    std::vector<ScalarWritePatch> scalarPatches;
};

struct RuntimeEventRecord {
    std::uint64_t stepIndex = 0;
    std::uint64_t ordinalInStep = 0;
    RuntimeEvent event;
};

enum class ManualEventKind : std::uint8_t {
    ParameterUpdate = 0,
    CellEdit = 1,
    Perturbation = 2
};

struct ManualEventRecord {
    std::uint64_t step = 0;
    float time = 0.0f;
    std::string variable;
    std::uint64_t cellIndex = std::numeric_limits<std::uint64_t>::max();
    float oldValue = 0.0f;
    float newValue = 0.0f;
    std::string description;
    std::uint64_t timestamp = 0;
    ManualEventKind kind = ManualEventKind::CellEdit;
};

class EventQueue {
public:
    void enqueueInput(RuntimeInputFrame inputFrame);
    void enqueueEvent(RuntimeEvent event);

    [[nodiscard]] bool hasInput() const noexcept { return !pendingInputs_.empty(); }
    [[nodiscard]] bool hasEvent() const noexcept { return !pendingEvents_.empty(); }

    RuntimeInputFrame popInput();
    RuntimeEvent popEvent();

    void clearTransient();

    void recordManualEvent(const ManualEventRecord& eventRecord);
    [[nodiscard]] const std::vector<ManualEventRecord>& manualEvents() const noexcept { return manualEvents_; }
    void setManualEvents(std::vector<ManualEventRecord> manualEvents);
    bool popLastManualEvent(ManualEventRecord& out);
    bool popLastManualEventOfKind(ManualEventKind kind, ManualEventRecord& out);

private:
    std::deque<RuntimeInputFrame> pendingInputs_;
    std::deque<RuntimeEvent> pendingEvents_;
    std::vector<ManualEventRecord> manualEvents_;
};

} // namespace ws
