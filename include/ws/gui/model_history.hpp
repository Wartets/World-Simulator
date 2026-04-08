#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace ws::gui {

// =============================================================================
// Model History
// =============================================================================

// Snapshot of model state for undo/redo functionality.
class ModelHistory {
public:
    struct Snapshot {
        std::string description;   // Description of the action.
        uint64_t timestamp;       // Unix timestamp of snapshot.
        std::string serializedState; // Serialized model state payload.
    };
    
    ModelHistory();
    ~ModelHistory();
    
    // Records a snapshot of the current state.
    void recordSnapshot(const std::string& description, const std::string& serializedState);
    
    // Reverts to the previous state.
    bool undo(std::string& restoredState);
    // Re-applies a reverted state.
    bool redo(std::string& restoredState);
    
    // Returns whether undo is available.
    bool canUndo() const { return !undo_stack.empty(); }
    // Returns whether redo is available.
    bool canRedo() const { return !redo_stack.empty(); }
    
    // Gets the description of the last action.
    std::string getLastActionDescription() const;

    // Gets the serialized state of the current snapshot.
    std::string getCurrentState() const;
    
    // Clears all history.
    void clear();
    
    // Gets the number of available undo operations.
    size_t getUndoCount() const { return undo_stack.size(); }
    // Gets the number of available redo operations.
    size_t getRedoCount() const { return redo_stack.size(); }
    
private:
    std::vector<Snapshot> undo_stack;
    std::vector<Snapshot> redo_stack;
    
    Snapshot createSnapshot(const std::string& description, const std::string& serializedState);
};

} // namespace ws::gui
