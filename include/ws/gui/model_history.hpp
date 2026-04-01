#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace ws::gui {

class ModelHistory {
public:
    struct Snapshot {
        // In a full implementation, this would contain a deep copy of the model state
        std::string description;
        uint64_t timestamp;
    };
    
    ModelHistory();
    ~ModelHistory();
    
    // Record a snapshot of the current state
    // Implementation note: would need access to model state to implement fully
    void recordSnapshot(const std::string& description);
    
    // Undo/Redo operations
    bool undo();
    bool redo();
    
    // Query state
    bool canUndo() const { return !undo_stack.empty(); }
    bool canRedo() const { return !redo_stack.empty(); }
    
    // Get current action description
    std::string getLastActionDescription() const;
    
    // Clear history
    void clear();
    
    // Get history size
    size_t getUndoCount() const { return undo_stack.size(); }
    size_t getRedoCount() const { return redo_stack.size(); }
    
private:
    std::vector<Snapshot> undo_stack;
    std::vector<Snapshot> redo_stack;
    
    Snapshot createSnapshot(const std::string& description);
};

} // namespace ws::gui
