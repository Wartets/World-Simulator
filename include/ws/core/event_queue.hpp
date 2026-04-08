#pragma once

// Core dependencies
#include "ws/core/types.hpp"

// Standard library
#include <cstdint>
#include <cstddef>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace ws {

// =============================================================================
// Scalar Write Patch
// =============================================================================

// A single cell modification to apply to a variable.
struct ScalarWritePatch {
    std::string variableName;
    Cell cell;
    float value = 0.0f;
};

// =============================================================================
// Runtime Input Frame
// =============================================================================

// Collection of patches to apply as user input during a simulation step.
struct RuntimeInputFrame {
    std::vector<ScalarWritePatch> scalarPatches;
};

// =============================================================================
// Runtime Event
// =============================================================================

// A named event containing patches to apply during simulation.
struct RuntimeEvent {
    std::string eventName;
    std::vector<ScalarWritePatch> scalarPatches;
};

// =============================================================================
// Runtime Event Record
// =============================================================================

// Record of an event that was applied, with timing information.
struct RuntimeEventRecord {
    std::uint64_t stepIndex = 0;
    std::uint64_t ordinalInStep = 0;
    RuntimeEvent event;
};

// =============================================================================
// Manual Event Kind
// =============================================================================

// Classification of manually triggered events.
enum class ManualEventKind : std::uint8_t {
    ParameterUpdate = 0,
    CellEdit = 1,
    Perturbation = 2
};

// =============================================================================
// Manual Event Record
// =============================================================================

// Record of a manual (user-initiated) event for auditing and undo.
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

// =============================================================================
// Event Queue
// =============================================================================

// Manages input frames and events for simulation execution.
// Handles queuing, processing, and manual event recording.
class EventQueue {
public:
    // Enqueues an input frame to be processed in the next step.
    void enqueueInput(RuntimeInputFrame inputFrame);
    // Enqueues an event to be processed in the next step.
    void enqueueEvent(RuntimeEvent event);

    // Returns true if there is pending input to process.
    [[nodiscard]] bool hasInput() const noexcept { return !pendingInputs_.empty(); }
    // Returns true if there are pending events to process.
    [[nodiscard]] bool hasEvent() const noexcept { return !pendingEvents_.empty(); }
    // Returns count of pending input frames.
    [[nodiscard]] std::size_t pendingInputFrameCount() const noexcept { return pendingInputs_.size(); }
    // Returns count of pending scalar input patches across all queued input frames.
    [[nodiscard]] std::size_t pendingInputPatchCount() const noexcept;
    // Returns count of pending events.
    [[nodiscard]] std::size_t pendingEventCount() const noexcept { return pendingEvents_.size(); }

    // Removes and returns the next input frame.
    RuntimeInputFrame popInput();
    // Removes and returns the next event.
    RuntimeEvent popEvent();

    // Clears all pending inputs and events (transient state).
    void clearTransient();

    // Records a manual event for audit trail.
    void recordManualEvent(const ManualEventRecord& eventRecord);
    // Returns all recorded manual events.
    [[nodiscard]] const std::vector<ManualEventRecord>& manualEvents() const noexcept { return manualEvents_; }
    // Sets the complete manual events list (for checkpoint restore).
    void setManualEvents(std::vector<ManualEventRecord> manualEvents);
    // Removes and returns the last manual event.
    bool popLastManualEvent(ManualEventRecord& out);
    // Removes and returns the last manual event of the specified kind.
    bool popLastManualEventOfKind(ManualEventKind kind, ManualEventRecord& out);

private:
    std::deque<RuntimeInputFrame> pendingInputs_;
    std::deque<RuntimeEvent> pendingEvents_;
    std::vector<ManualEventRecord> manualEvents_;
};

} // namespace ws
