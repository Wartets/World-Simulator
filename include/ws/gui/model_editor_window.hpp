#pragma once

#include "ws/gui/node_editor.hpp"
#include "ws/gui/model_validator.hpp"
#include "ws/gui/model_history.hpp"
#include "ws/core/model_parser.hpp"

#include <string>
#include <vector>
#include <memory>
#include <filesystem>

namespace ws::gui {

// Visual editor for creating and modifying simulation models.
// Provides node-based model construction with validation.
class ModelEditorWindow {
public:
    explicit ModelEditorWindow(const std::string& window_title = "Model Editor");
    ~ModelEditorWindow();
    
    // Model loading and management
    void loadModel(const ModelContext& context);
    void createNewModel();
    void saveModel();
    void exportModel();
    void setActiveModelPath(const std::filesystem::path& modelPath);
    
    // Rendering
    void render(ImVec2 available_size);

    void open() { window_open = true; }
    
    // Accessors
    bool isOpen() const { return window_open; }
    bool isModified() const { return is_modified; }
    void close() { window_open = false; }
    
    // Get the node editor for advanced operations
    NodeEditor* getNodeEditor() { return node_editor.get(); }
    const NodeEditor* getNodeEditor() const { return node_editor.get(); }
    
    // Get validation results
    const std::vector<std::string>& getValidationErrors() const { return validation_errors; }
    const std::vector<std::string>& getValidationWarnings() const { return validation_warnings; }
    
private:
    // Window state
    std::string window_title;
    bool window_open;
    bool is_modified{false};
    
    // Components
    std::unique_ptr<NodeEditor> node_editor;
    std::unique_ptr<ModelValidator> validator;
    std::unique_ptr<ModelHistory> history;
    
    // Model data
    ModelContext current_model;
    
    // UI state
    bool show_property_inspector;
    bool show_validation_panel;
    std::vector<std::string> status_details;
    std::string palette_filter;
    char open_model_path_buffer[512]{};
    char save_model_path_buffer[512]{};
    
    // Validation state
    double last_validation_time;
    double validation_debounce_ms;
    std::vector<std::string> validation_errors;
    std::vector<std::string> validation_warnings;
    std::vector<std::string> validation_info;
    
    // Messages
    std::string error_message;
    std::string status_message;
    
    // Rendering helpers
    void renderNodePalette();
    void renderPropertyInspector();
    void renderValidationPanel();
    
    // UI helpers
    void selectAllNodes();
    void deleteSelectedNodes();
    void duplicateSelectedNodes();
    void showFileActionPopups();
    void appendStatusDetail(const std::string& line);
    
    // Model graph operations
    void populateNodeGraphFromModel(const std::string& model_json_str);
    void addVariableNode(NodeType node_type, VariableSupport support);
    void addInteractionNode(NodeType node_type);
    void addStageNode();
    void addDomainNode(NodeType node_type);
    
    // Validation
    void runValidation();
    bool hasCyclicDependencies() const;
    void markDirty();
};

} // namespace ws::gui
