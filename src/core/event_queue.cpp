#include "ws/core/event_queue.hpp"

#include <stdexcept>
#include <utility>

namespace ws {

void EventQueue::enqueueInput(RuntimeInputFrame inputFrame) {
    pendingInputs_.push_back(std::move(inputFrame));
}

void EventQueue::enqueueEvent(RuntimeEvent event) {
    pendingEvents_.push_back(std::move(event));
}

RuntimeInputFrame EventQueue::popInput() {
    if (pendingInputs_.empty()) {
        throw std::runtime_error("EventQueue input queue underflow");
    }

    RuntimeInputFrame frame = std::move(pendingInputs_.front());
    pendingInputs_.pop_front();
    return frame;
}

RuntimeEvent EventQueue::popEvent() {
    if (pendingEvents_.empty()) {
        throw std::runtime_error("EventQueue event queue underflow");
    }

    RuntimeEvent event = std::move(pendingEvents_.front());
    pendingEvents_.pop_front();
    return event;
}

void EventQueue::clearTransient() {
    pendingInputs_.clear();
    pendingEvents_.clear();
}

void EventQueue::recordManualEvent(const ManualEventRecord& eventRecord) {
    manualEvents_.push_back(eventRecord);
}

void EventQueue::setManualEvents(std::vector<ManualEventRecord> manualEvents) {
    manualEvents_ = std::move(manualEvents);
}

bool EventQueue::popLastManualEvent(ManualEventRecord& out) {
    if (manualEvents_.empty()) {
        return false;
    }

    out = manualEvents_.back();
    manualEvents_.pop_back();
    return true;
}

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
