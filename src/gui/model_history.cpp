#include "ws/gui/model_history.hpp"

#include <chrono>

namespace ws::gui {

// Default constructor; initializes empty history.
ModelHistory::ModelHistory() = default;

// Destructor; default.
ModelHistory::~ModelHistory() = default;

// Creates a timestamped snapshot with description.
ModelHistory::Snapshot ModelHistory::createSnapshot(const std::string& description, const std::string& serializedState) {
    auto now = std::chrono::system_clock::now();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    
    return Snapshot{description, timestamp, serializedState};
}

// Records a new action snapshot; clears redo stack.
void ModelHistory::recordSnapshot(const std::string& description, const std::string& serializedState) {
    if (!undo_stack.empty() && undo_stack.back().serializedState == serializedState) {
        return;
    }

    // When a new action is recorded, clear the redo stack
    redo_stack.clear();
    
    // Add to undo stack
    undo_stack.push_back(createSnapshot(description, serializedState));
    
    // Limit undo stack size to prevent excessive memory usage
    const size_t max_history = 200;
    if (undo_stack.size() > max_history) {
        undo_stack.erase(undo_stack.begin());
    }
}

// Pops from undo stack and pushes to redo stack.
// Returns false if nothing to undo.
bool ModelHistory::undo(std::string& restoredState) {
    if (undo_stack.size() < 2u) {
        return false;
    }
    
    // Move current snapshot from undo to redo and restore previous snapshot.
    Snapshot snapshot = undo_stack.back();
    undo_stack.pop_back();
    redo_stack.push_back(snapshot);

    restoredState = undo_stack.back().serializedState;
    
    return true;
}

// Pops from redo stack and pushes to undo stack.
// Returns false if nothing to redo.
bool ModelHistory::redo(std::string& restoredState) {
    if (redo_stack.empty()) {
        return false;
    }
    
    // Move the restored snapshot from redo to undo.
    Snapshot snapshot = redo_stack.back();
    redo_stack.pop_back();
    undo_stack.push_back(snapshot);

    restoredState = undo_stack.back().serializedState;
    
    return true;
}

// Returns description of most recent action.
std::string ModelHistory::getLastActionDescription() const {
    if (undo_stack.empty()) {
        return "No actions";
    }
    return undo_stack.back().description;
}

std::string ModelHistory::getCurrentState() const {
    if (undo_stack.empty()) {
        return {};
    }
    return undo_stack.back().serializedState;
}

// Clears both undo and redo stacks.
void ModelHistory::clear() {
    undo_stack.clear();
    redo_stack.clear();
}

} // namespace ws::gui
