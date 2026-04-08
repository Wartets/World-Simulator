#include "ws/core/event_queue.hpp"

#include <stdexcept>
#include <utility>

namespace ws {

// Adds an input frame to the pending inputs queue.
void EventQueue::enqueueInput(RuntimeInputFrame inputFrame) {
    pendingInputs_.push_back(std::move(inputFrame));
}

// Adds a runtime event to the pending events queue.
void EventQueue::enqueueEvent(RuntimeEvent event) {
    pendingEvents_.push_back(std::move(event));
}

std::size_t EventQueue::pendingInputPatchCount() const noexcept {
    std::size_t count = 0;
    for (const auto& frame : pendingInputs_) {
        count += frame.scalarPatches.size();
    }
    return count;
}

// Removes and returns the next input frame from the queue.
// Throws std::runtime_error if the queue is empty.
RuntimeInputFrame EventQueue::popInput() {
    if (pendingInputs_.empty()) {
        throw std::runtime_error("EventQueue input queue underflow");
    }

    RuntimeInputFrame frame = std::move(pendingInputs_.front());
    pendingInputs_.pop_front();
    return frame;
}

// Removes and returns the next runtime event from the queue.
// Throws std::runtime_error if the queue is empty.
RuntimeEvent EventQueue::popEvent() {
    if (pendingEvents_.empty()) {
        throw std::runtime_error("EventQueue event queue underflow");
    }

    RuntimeEvent event = std::move(pendingEvents_.front());
    pendingEvents_.pop_front();
    return event;
}

// Clears all pending input frames and events (transient data).
void EventQueue::clearTransient() {
    pendingInputs_.clear();
    pendingEvents_.clear();
}

// Records a manual event to the persistent event history.
void EventQueue::recordManualEvent(const ManualEventRecord& eventRecord) {
    manualEvents_.push_back(eventRecord);
}

// Sets the complete list of manual events, replacing any existing history.
void EventQueue::setManualEvents(std::vector<ManualEventRecord> manualEvents) {
    manualEvents_ = std::move(manualEvents);
}

// Removes and returns the most recent manual event.
// Returns false if no events remain.
bool EventQueue::popLastManualEvent(ManualEventRecord& out) {
    if (manualEvents_.empty()) {
        return false;
    }

    out = manualEvents_.back();
    manualEvents_.pop_back();
    return true;
}

// Removes and returns the most recent manual event of the specified kind.
// Returns false if no matching events are found.
bool EventQueue::popLastManualEventOfKind(const ManualEventKind kind, ManualEventRecord& out) {
    for (auto it = manualEvents_.rbegin(); it != manualEvents_.rend(); ++it) {
        if (it->kind != kind) {
            continue;
        }

        out = *it;
        manualEvents_.erase(std::next(it).base());
        return true;
    }

    return false;
}

} // namespace ws
