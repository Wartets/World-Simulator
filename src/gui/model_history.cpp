#include "ws/gui/model_history.hpp"

#include <chrono>

namespace ws::gui {

ModelHistory::ModelHistory() = default;

ModelHistory::~ModelHistory() = default;

ModelHistory::Snapshot ModelHistory::createSnapshot(const std::string& description) {
    auto now = std::chrono::system_clock::now();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    
    return Snapshot{description, timestamp};
}

void ModelHistory::recordSnapshot(const std::string& description) {
    // When a new action is recorded, clear the redo stack
    redo_stack.clear();
    
    // Add to undo stack
    undo_stack.push_back(createSnapshot(description));
    
    // Limit undo stack size to prevent excessive memory usage
    const size_t max_history = 100;
    if (undo_stack.size() > max_history) {
        undo_stack.erase(undo_stack.begin());
    }
}

bool ModelHistory::undo() {
    if (undo_stack.empty()) {
        return false;
    }
    
    // Move from undo to redo
    Snapshot snapshot = undo_stack.back();
    undo_stack.pop_back();
    redo_stack.push_back(snapshot);
    
    // In a full implementation, restore the model state from snapshot
    
    return true;
}

bool ModelHistory::redo() {
    if (redo_stack.empty()) {
        return false;
    }
    
    // Move from redo to undo
    Snapshot snapshot = redo_stack.back();
    redo_stack.pop_back();
    undo_stack.push_back(snapshot);
    
    // In a full implementation, restore the model state from snapshot
    
    return true;
}

std::string ModelHistory::getLastActionDescription() const {
    if (undo_stack.empty()) {
        return "No actions";
    }
    return undo_stack.back().description;
}

void ModelHistory::clear() {
    undo_stack.clear();
    redo_stack.clear();
}

} // namespace ws::gui
