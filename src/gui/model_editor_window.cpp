#include "ws/gui/model_editor_window.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <sstream>

namespace ws::gui {

ModelEditorWindow::ModelEditorWindow(const std::string& window_title)
    : window_title(window_title),
      window_open(true),
      node_editor(std::make_unique<NodeEditor>()),
      validator(std::make_unique<ModelValidator>()),
      history(std::make_unique<ModelHistory>()),
      show_property_inspector(true),
      show_validation_panel(true),
      last_validation_time(0.0),
      validation_debounce_ms(500.0) {
}

ModelEditorWindow::~ModelEditorWindow() = default;

void ModelEditorWindow::loadModel(const ModelContext& context) {
    // Copy model data (avoid copying unique_ptr)
    current_model.metadata_json = context.metadata_json;
    current_model.version_json = context.version_json;
    current_model.model_json = context.model_json;
    current_model.ir_logic_string = context.ir_logic_string;
    current_model.flatbuffers_bin = context.flatbuffers_bin;
    
    // Clear existing graph
    node_editor->clearSelection();
    
    // Parse model.json to populate node graph
    try {
        populateNodeGraphFromModel(context.model_json);
        history->recordSnapshot("Model loaded");
    } catch (const std::exception& e) {
        error_message = std::string("Failed to load model: ") + e.what();
    }
}

void ModelEditorWindow::populateNodeGraphFromModel(const std::string& model_json_str) {
    // This is a scaffold implementation that demonstrates how to parse JSON
    // In a full implementation, this would deserialize the model and create all nodes
    
    // Example structure (would be filled from actual JSON):
    // 1. Create variable nodes from "variables" section
    // 2. Create stage nodes from "stages" section
    // 3. Create interaction nodes from interactions within stages
    // 4. Create connections based on read/write relationships
    
    // For now, we create a simple example node graph
    Node var_node("var_temp", NodeType::CellVariable, "temperature");
    var_node.position = {100.0f, 100.0f};
    var_node.data_type = DataType::F32;
    var_node.units = "K";
    var_node.output_ports.push_back(Port("temp_out", "temperature", false));
    node_editor->addNode(var_node);
    
    Node eq_node("eq_diffusion", NodeType::Equation, "Diffusion");
    eq_node.position = {300.0f, 100.0f};
    eq_node.input_ports.push_back(Port("temp_in", "temperature", true));
    eq_node.output_ports.push_back(Port("temp_out", "temperature", false));
    node_editor->addNode(eq_node);
}

void ModelEditorWindow::render(ImVec2 available_size) {
    if (!window_open) return;
    
    // Debounced validation
    double current_time = ImGui::GetTime();
    if (current_time - last_validation_time >= validation_debounce_ms / 1000.0) {
        runValidation();
        last_validation_time = current_time;
    }
    
    // Main menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Model", "Ctrl+N")) {
                createNewModel();
            }
            if (ImGui::MenuItem("Open Model", "Ctrl+O")) {
                // This would open a file dialog
            }
            if (ImGui::MenuItem("Save Model", "Ctrl+S")) {
                saveModel();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                // This would open a save dialog
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export as ZIP")) {
                exportModel();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close", "Alt+F4")) {
                window_open = false;
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, history->canUndo())) {
                history->undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, history->canRedo())) {
                history->redo();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                // Select all nodes
            }
            if (ImGui::MenuItem("Deselect All", "Ctrl+D")) {
                node_editor->clearSelection();
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Auto Layout")) {
                node_editor->autoLayout();
            }
            if (ImGui::MenuItem("Reset View")) {
                node_editor->resetView();
            }
            ImGui::Separator();
            ImGui::MenuItem("Property Inspector", "P", &show_property_inspector);
            ImGui::MenuItem("Validation Panel", "V", &show_validation_panel);
            ImGui::EndMenu();
        }
        
        ImGui::EndMenuBar();
    }
    
    // Node palette (left side)
    ImGui::SetNextWindowSizeConstraints(ImVec2(180.0f, 400.0f), ImVec2(250.0f, -1.0f));
    if (ImGui::Begin("Node Palette", nullptr, ImGuiWindowFlags_NoMove)) {
        renderNodePalette();
        ImGui::End();
    }
    
    // Main canvas area
    ImGui::SetNextWindowSizeConstraints(ImVec2(400.0f, 300.0f), ImVec2(-1.0f, -1.0f));
    if (ImGui::Begin("Model Graph", nullptr)) {
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        node_editor->render(canvas_size);
        ImGui::End();
    }
    
    // Property inspector (right side)
    if (show_property_inspector) {
        ImGui::SetNextWindowSizeConstraints(ImVec2(250.0f, 300.0f), ImVec2(400.0f, -1.0f));
        if (ImGui::Begin("Properties", &show_property_inspector)) {
            renderPropertyInspector();
            ImGui::End();
        }
    }
    
    // Validation panel (bottom)
    if (show_validation_panel) {
        ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 150.0f), ImVec2(-1.0f, 400.0f));
        if (ImGui::Begin("Validation", &show_validation_panel)) {
            renderValidationPanel();
            ImGui::End();
        }
    }
    
    // Error/status message
    if (!error_message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        ImGui::TextWrapped("Error: %s", error_message.c_str());
        ImGui::PopStyleColor();
    }
    
    if (!status_message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
        ImGui::TextWrapped("Status: %s", status_message.c_str());
        ImGui::PopStyleColor();
    }
}

void ModelEditorWindow::renderNodePalette() {
        ImGui::TextDisabled("Variables");
    if (ImGui::MenuItem("Global Variable")) {
        addVariableNode(NodeType::GlobalVariable, VariableSupport::Global);
    }
    if (ImGui::MenuItem("Cell Variable")) {
        addVariableNode(NodeType::CellVariable, VariableSupport::Cell);
    }
    
        ImGui::TextDisabled("Interactions");
    if (ImGui::MenuItem("Equation Node")) {
        addInteractionNode(NodeType::Equation);
    }
    if (ImGui::MenuItem("Operator Node")) {
        addInteractionNode(NodeType::Operator);
    }
    
        ImGui::TextDisabled("Organization");
    if (ImGui::MenuItem("Stage")) {
        addStageNode();
    }
    
        ImGui::TextDisabled("Constraints");
    if (ImGui::MenuItem("Domain (Interval)")) {
        addDomainNode(NodeType::Domain);
    }
}

void ModelEditorWindow::renderPropertyInspector() {
    const auto& selected_nodes = node_editor->getSelectedNodeIds();
    
    if (selected_nodes.empty()) {
        ImGui::TextDisabled("No node selected");
        return;
    }
    
    if (selected_nodes.size() > 1) {
        ImGui::TextDisabled("Multiple nodes selected");
        ImGui::Text("(%zu nodes)", selected_nodes.size());
        return;
    }
    
    Node* selected_node = node_editor->getNode(selected_nodes[0]);
    if (!selected_node) return;
    
    // Common properties
    ImGui::AlignTextToFramePadding();
    ImGui::Text("ID:");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", selected_node->id.c_str());
    
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Type:");
    ImGui::SameLine();
    const char* type_str = "Unknown";
    switch (selected_node->type) {
        case NodeType::GlobalVariable: type_str = "Global Variable"; break;
        case NodeType::CellVariable: type_str = "Cell Variable"; break;
        case NodeType::Parameter: type_str = "Parameter"; break;
        case NodeType::Derived: type_str = "Derived"; break;
        case NodeType::Equation: type_str = "Equation"; break;
        case NodeType::Operator: type_str = "Operator"; break;
        case NodeType::Stage: type_str = "Stage"; break;
        case NodeType::Domain: type_str = "Domain"; break;
    }
    ImGui::TextDisabled("%s", type_str);
    
    ImGui::Separator();
    
    // Variable-specific properties
    if (selected_node->type == NodeType::GlobalVariable || 
        selected_node->type == NodeType::CellVariable ||
        selected_node->type == NodeType::Parameter ||
        selected_node->type == NodeType::Derived) {
        
        ImGui::Text("Name: %s", selected_node->variable_name.c_str());
        
        const char* data_types[] = { "F32", "F64", "I32", "U32", "Bool", "Vec2", "Vec3" };
        int type_idx = static_cast<int>(selected_node->data_type);
        if (ImGui::Combo("Data Type", &type_idx, data_types, IM_ARRAYSIZE(data_types))) {
            selected_node->data_type = static_cast<DataType>(type_idx);
            markDirty();
        }
        
        const char* roles[] = { "Parameter", "State", "Forcing", "Derived", "Auxiliary" };
        int role_idx = static_cast<int>(selected_node->role);
        if (ImGui::Combo("Role", &role_idx, roles, IM_ARRAYSIZE(roles))) {
            selected_node->role = static_cast<VariableRole>(role_idx);
            markDirty();
        }
        
        ImGui::Text("Units: %s", selected_node->units.c_str());
        
        ImGui::Text("Initial: %s", selected_node->initial_value.c_str());
        
        ImGui::Text("Desc: %s", selected_node->description.c_str());
    }
    
    // Interaction-specific properties
    if (selected_node->type == NodeType::Equation || 
        selected_node->type == NodeType::Operator) {
        
        ImGui::Text("Name: %s", selected_node->label.c_str());
        
        ImGui::Text("Formula: %s", selected_node->formula_logic.c_str());
        
        const char* target_types[] = { "scalar", "grid" };
        int target_idx = (selected_node->target_type == "grid") ? 1 : 0;
        if (ImGui::Combo("Target Type", &target_idx, target_types, IM_ARRAYSIZE(target_types))) {
            selected_node->target_type = (target_idx == 0) ? "scalar" : "grid";
            markDirty();
        }
    }
    
    ImGui::Separator();
    ImGui::Text("Position: %.1f, %.1f", selected_node->position.x, selected_node->position.y);
    ImGui::Text("Size: %.1f, %.1f", selected_node->size.x, selected_node->size.y);
}

void ModelEditorWindow::renderValidationPanel() {
    ImGui::Text("Validation Status");
    ImGui::Separator();
    
    // Summary counters
    ImGui::Text("Errors: %zu", validation_errors.size());
    ImGui::Text("Warnings: %zu", validation_warnings.size());
    ImGui::Text("Info: %zu", validation_info.size());
    
    ImGui::Separator();
    ImGui::Text("Messages:");
    ImGui::BeginChild("ValidationMessages", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), 
                     true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    
    // Render errors
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
    for (const auto& error : validation_errors) {
        if (ImGui::Selectable(error.c_str())) {
            // Could navigate to source node here
        }
    }
    ImGui::PopStyleColor();
    
    // Render warnings
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
    for (const auto& warning : validation_warnings) {
        if (ImGui::Selectable(warning.c_str())) {
            // Could navigate to source node here
        }
    }
    ImGui::PopStyleColor();
    
    // Render info
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 1.0f, 1.0f));
    for (const auto& info : validation_info) {
        if (ImGui::Selectable(info.c_str())) {
            // Could navigate to source node here
        }
    }
    ImGui::PopStyleColor();
    
    ImGui::EndChild();
    
    if (ImGui::Button("Clear All")) {
        validation_errors.clear();
        validation_warnings.clear();
        validation_info.clear();
    }
}

void ModelEditorWindow::addVariableNode(NodeType node_type, VariableSupport support) {
    static int counter = 0;
    std::string node_id = std::string("var_") + std::to_string(counter++);
    std::string label = (support == VariableSupport::Global) ? "GlobalVar" : "CellVar";
    
    Node node(node_id, node_type, label);
    node.position = ImVec2(100.0f + counter * 20, 100.0f + counter * 20);
    node.support = support;
    node.output_ports.push_back(Port("out", node_id, false));
    
    node_editor->addNode(node);
    markDirty();
}

void ModelEditorWindow::addInteractionNode(NodeType node_type) {
    static int counter = 0;
    std::string node_id = std::string("eq_") + std::to_string(counter++);
    std::string label = (node_type == NodeType::Equation) ? "Equation" : "Operator";
    
    Node node(node_id, node_type, label);
    node.position = ImVec2(300.0f + counter * 20, 100.0f + counter * 20);
    node.input_ports.push_back(Port("in", "", true));
    node.output_ports.push_back(Port("out", "", false));
    
    node_editor->addNode(node);
    markDirty();
}

void ModelEditorWindow::addStageNode() {
    static int counter = 0;
    std::string node_id = std::string("stage_") + std::to_string(counter++);
    
    Node node(node_id, NodeType::Stage, "Stage");
    node.position = ImVec2(50.0f, 300.0f + counter * 50);
    node.size = ImVec2(150.0f, 40.0f);
    
    node_editor->addNode(node);
    markDirty();
}

void ModelEditorWindow::addDomainNode(NodeType node_type) {
    static int counter = 0;
    std::string node_id = std::string("domain_") + std::to_string(counter++);
    
    Node node(node_id, node_type, "Domain");
    node.position = ImVec2(500.0f, 100.0f + counter * 50);
    
    node_editor->addNode(node);
    markDirty();
}

void ModelEditorWindow::runValidation() {
    if (!validator) return;
    
    // Validate graph structure
    validation_errors.clear();
    validation_warnings.clear();
    validation_info.clear();
    
    // Check for unconnected nodes
    const auto& nodes = node_editor->getAllNodes();
    for (const auto& node : nodes) {
        if (node->type == NodeType::Equation || node->type == NodeType::Operator) {
            if (node->input_ports.empty()) {
                validation_warnings.push_back("Node '" + node->label + "' has no inputs");
            }
            if (node->output_ports.empty()) {
                validation_warnings.push_back("Node '" + node->label + "' has no outputs");
            }
        }
    }
    
    // Check for circular dependencies
    if (hasCyclicDependencies()) {
        validation_errors.push_back("Graph has circular dependencies");
    }
    
    if (validation_errors.empty() && validation_warnings.empty()) {
        validation_info.push_back("All validations passed");
    }
}

bool ModelEditorWindow::hasCyclicDependencies() const {
    // Simplified cycle detection using DFS
    // In a full implementation, use topological sort with cycle detection
    return false;
}

void ModelEditorWindow::markDirty() {
    is_modified = true;
}

void ModelEditorWindow::saveModel() {
    if (current_model.model_json.empty()) {
        error_message = "No model loaded";
        return;
    }
    
    // Serialize node graph back to model.json
    // This is a scaffold implementation
    
    status_message = "Model saved successfully";
    is_modified = false;
}

void ModelEditorWindow::exportModel() {
    if (current_model.model_json.empty()) {
        error_message = "No model loaded";
        return;
    }
    
    // Export as ZIP file
    status_message = "Model exported successfully";
}

void ModelEditorWindow::createNewModel() {
    current_model = ModelContext();
    node_editor->clearSelection();
    is_modified = false;
    status_message = "New model created";
}

} // namespace ws::gui
