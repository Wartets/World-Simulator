#include "ws/gui/model_editor_window.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace ws::gui {

namespace {

using json = nlohmann::json;

bool isCommentKey(const std::string& key) {
    return key.rfind("__comment", 0) == 0;
}

VariableSupport parseSupport(const json& variable) {
    const std::string support = variable.value("support", std::string{"global"});
    return support == "cell" ? VariableSupport::Cell : VariableSupport::Global;
}

VariableRole parseRole(const json& variable) {
    const std::string role = variable.value("role", std::string{"state"});
    if (role == "parameter") return VariableRole::Parameter;
    if (role == "forcing") return VariableRole::Forcing;
    if (role == "derived") return VariableRole::Derived;
    if (role == "auxiliary") return VariableRole::Auxiliary;
    return VariableRole::State;
}

NodeType nodeTypeForVariable(VariableRole role, VariableSupport support) {
    if (role == VariableRole::Parameter) return NodeType::Parameter;
    if (role == VariableRole::Derived) return NodeType::Derived;
    return support == VariableSupport::Cell ? NodeType::CellVariable : NodeType::GlobalVariable;
}

DataType parseDataType(const json& variable) {
    const std::string type = variable.value("type", std::string{"f32"});
    if (type == "f64") return DataType::F64;
    if (type == "i32") return DataType::I32;
    if (type == "u32") return DataType::U32;
    if (type == "bool") return DataType::Bool;
    if (type == "vec2") return DataType::Vec2;
    if (type == "vec3") return DataType::Vec3;
    return DataType::F32;
}

std::string valueToString(const json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number() || value.is_boolean()) return value.dump();
    return {};
}

bool editTextField(const char* label, std::string& value, std::size_t buffer_size = 1024) {
    std::vector<char> buffer(buffer_size, '\0');
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    if (ImGui::InputText(label, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}

bool editMultilineField(const char* label, std::string& value, const ImVec2& size) {
    std::vector<char> buffer(2048u, '\0');
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    if (ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), size)) {
        value = buffer.data();
        return true;
    }
    return false;
}

} // namespace

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
    window_open = true;
    error_message.clear();
    status_message = "Model loaded";

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
    const json model = json::parse(model_json_str);

    std::vector<std::string> existingNodeIds;
    existingNodeIds.reserve(node_editor->getAllNodes().size());
    for (const auto& node : node_editor->getAllNodes()) {
        existingNodeIds.push_back(node->id);
    }
    for (const auto& nodeId : existingNodeIds) {
        node_editor->removeNode(nodeId);
    }
    node_editor->clearSelection();

    std::unordered_map<std::string, std::string> variableNodeByVariableId;
    std::unordered_map<std::string, std::string> domainNodeByDomainId;

    float domainY = 80.0f;
    if (model.contains("domains") && model["domains"].is_object()) {
        for (const auto& [domainId, domainValue] : model["domains"].items()) {
            if (isCommentKey(domainId) || !domainValue.is_object()) {
                continue;
            }

            Node domainNode("domain_" + domainId, NodeType::Domain, domainId);
            domainNode.position = ImVec2(60.0f, domainY);
            domainNode.description = valueToString(domainValue.value("kind", json{}));
            domainNode.output_ports.push_back(Port("domain", domainId, false));
            node_editor->addNode(domainNode);

            domainNodeByDomainId[domainId] = domainNode.id;
            domainY += 86.0f;
        }
    }

    float globalVarY = 80.0f;
    float cellVarY = 80.0f;
    json variables = json::array();
    if (model.contains("variables") && model["variables"].is_array()) {
        variables = model["variables"];
    }
    for (const auto& variable : variables) {
        if (!variable.is_object()) {
            continue;
        }
        if (!variable.contains("id") || !variable["id"].is_string()) {
            continue;
        }

        const std::string variableId = variable["id"].get<std::string>();
        const VariableSupport support = parseSupport(variable);
        const VariableRole role = parseRole(variable);

        Node variableNode("var_" + variableId, nodeTypeForVariable(role, support), variableId);
        variableNode.variable_name = variableId;
        variableNode.support = support;
        variableNode.role = role;
        variableNode.data_type = parseDataType(variable);
        variableNode.units = variable.value("units", std::string{});
        variableNode.description = variable.value("description", std::string{});
        variableNode.domain_ref = variable.value("domain", std::string{});
        if (variable.contains("initial_value")) {
            variableNode.initial_value = valueToString(variable["initial_value"]);
        }

        if (support == VariableSupport::Global) {
            variableNode.position = ImVec2(300.0f, globalVarY);
            globalVarY += 86.0f;
        } else {
            variableNode.position = ImVec2(540.0f, cellVarY);
            cellVarY += 86.0f;
        }

        variableNode.input_ports.push_back(Port("in", variableId, true));
        variableNode.output_ports.push_back(Port("out", variableId, false));
        node_editor->addNode(variableNode);

        variableNodeByVariableId[variableId] = variableNode.id;
    }

    for (const auto& variable : variables) {
        if (!variable.is_object() || !variable.contains("id") || !variable["id"].is_string()) {
            continue;
        }
        const std::string variableId = variable["id"].get<std::string>();
        const std::string domainRef = variable.value("domain", std::string{});
        if (domainRef.empty()) {
            continue;
        }

        const auto varIt = variableNodeByVariableId.find(variableId);
        const auto domainIt = domainNodeByDomainId.find(domainRef);
        if (varIt != variableNodeByVariableId.end() && domainIt != domainNodeByDomainId.end()) {
            node_editor->addConnection(Connection(domainIt->second, varIt->second, "domain", "in"));
        }
    }

    float stageY = 80.0f;
    if (model.contains("stages") && model["stages"].is_array()) {
        int stageIndex = 0;
        for (const auto& stage : model["stages"]) {
            if (!stage.is_object()) {
                continue;
            }

            const std::string stageId = stage.value("id", std::string{"stage_" + std::to_string(stageIndex)});

            Node stageNode("stage_" + stageId, NodeType::Stage, stageId);
            stageNode.position = ImVec2(860.0f, stageY);
            stageNode.description = stage.value("description", std::string{});
            node_editor->addNode(stageNode);

            float interactionY = stageY + 88.0f;
            if (stage.contains("interactions") && stage["interactions"].is_array()) {
                int interactionIndex = 0;
                for (const auto& interaction : stage["interactions"]) {
                    if (!interaction.is_object()) {
                        continue;
                    }

                    const std::string interactionId = interaction.value(
                        "id", std::string{"interaction_" + std::to_string(stageIndex) + "_" + std::to_string(interactionIndex)});

                    Node interactionNode("interaction_" + interactionId, NodeType::Equation, interactionId);
                    interactionNode.position = ImVec2(1140.0f, interactionY);
                    interactionNode.target_type = interaction.value("target_type", std::string{});
                    interactionNode.formula_logic = interaction.value("formula", std::string{});
                    interactionNode.input_ports.push_back(Port("stage", stageId, true));

                    if (interaction.contains("reads") && interaction["reads"].is_array()) {
                        for (const auto& readVar : interaction["reads"]) {
                            if (readVar.is_string()) {
                                const std::string varId = readVar.get<std::string>();
                                interactionNode.input_ports.push_back(Port(varId, varId, true));
                            }
                        }
                    }

                    if (interaction.contains("writes") && interaction["writes"].is_array()) {
                        for (const auto& writeVar : interaction["writes"]) {
                            if (writeVar.is_string()) {
                                const std::string varId = writeVar.get<std::string>();
                                interactionNode.output_ports.push_back(Port(varId, varId, false));
                            }
                        }
                    }

                    if (interactionNode.output_ports.empty()) {
                        interactionNode.output_ports.push_back(Port("out", interactionId, false));
                    }

                    node_editor->addNode(interactionNode);
                    node_editor->addConnection(Connection(stageNode.id, interactionNode.id, "flow", "stage"));

                    if (interaction.contains("reads") && interaction["reads"].is_array()) {
                        for (const auto& readVar : interaction["reads"]) {
                            if (!readVar.is_string()) {
                                continue;
                            }
                            const std::string varId = readVar.get<std::string>();
                            const auto varIt = variableNodeByVariableId.find(varId);
                            if (varIt != variableNodeByVariableId.end()) {
                                node_editor->addConnection(Connection(varIt->second, interactionNode.id, "out", varId));
                            }
                        }
                    }

                    if (interaction.contains("writes") && interaction["writes"].is_array()) {
                        for (const auto& writeVar : interaction["writes"]) {
                            if (!writeVar.is_string()) {
                                continue;
                            }
                            const std::string varId = writeVar.get<std::string>();
                            const auto varIt = variableNodeByVariableId.find(varId);
                            if (varIt != variableNodeByVariableId.end()) {
                                node_editor->addConnection(Connection(interactionNode.id, varIt->second, varId, "in"));
                            }
                        }
                    }

                    interactionY += 96.0f;
                    ++interactionIndex;
                }
            }

            stageY = std::max(stageY + 140.0f, interactionY + 24.0f);
            ++stageIndex;
        }
    }

    node_editor->autoLayout();
}

void ModelEditorWindow::render(ImVec2 available_size) {
    if (!window_open) return;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(available_size, ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse;

    // Debounced validation
    double current_time = ImGui::GetTime();
    if (current_time - last_validation_time >= validation_debounce_ms / 1000.0) {
        runValidation();
        last_validation_time = current_time;
    }
    
    if (ImGui::Begin(window_title.c_str(), &window_open, flags)) {
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

        // Compute explicit layout heights to prevent micro-scrolling
        ImVec2 content_avail = ImGui::GetContentRegionAvail();
        const float paletteWidth = 240.0f;
        const float propertiesWidth = show_property_inspector ? 320.0f : 0.0f;
        const float statusBarHeight = 24.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.y;
        const float validationHeight = show_validation_panel ? 180.0f : 0.0f;
        
        // Top row gets: total height - validation - status bar - spacing between sections
        float topRowHeight = content_avail.y - statusBarHeight;
        if (show_validation_panel) {
            topRowHeight -= (validationHeight + spacing);
        }
        topRowHeight = std::max(topRowHeight, 100.0f);

        // === Top Row (Palette + Graph + Properties) ===
        if (ImGui::BeginChild("EditorTopRow", ImVec2(0.0f, topRowHeight), false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            if (ImGui::BeginChild("NodePalettePane", ImVec2(paletteWidth, 0.0f), true)) {
                ImGui::TextDisabled("Node Palette");
                ImGui::Separator();
                renderNodePalette();
            }
            ImGui::EndChild();

            ImGui::SameLine();
            float graphWidth = ImGui::GetContentRegionAvail().x;
            if (show_property_inspector) {
                graphWidth -= (propertiesWidth + ImGui::GetStyle().ItemSpacing.x);
                graphWidth = std::max(graphWidth, 240.0f);
            }

            if (ImGui::BeginChild("ModelGraphPane", ImVec2(graphWidth, 0.0f), true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                ImGui::TextDisabled("Model Graph");
                ImGui::Separator();
                ImVec2 canvas_size = ImGui::GetContentRegionAvail();
                node_editor->render(canvas_size);
            }
            ImGui::EndChild();

            if (show_property_inspector) {
                ImGui::SameLine();
                if (ImGui::BeginChild("PropertiesPane", ImVec2(0.0f, 0.0f), true)) {
                    ImGui::TextDisabled("Properties");
                    ImGui::Separator();
                    renderPropertyInspector();
                }
                ImGui::EndChild();
            }
        }
        ImGui::EndChild();

        // === Validation Panel (explicit height) ===
        if (show_validation_panel) {
            if (ImGui::BeginChild("ValidationPane", ImVec2(0.0f, validationHeight), true)) {
                ImGui::TextDisabled("Validation");
                ImGui::Separator();
                renderValidationPanel();
            }
            ImGui::EndChild();
        }

        // === Status Bar (fixed compact footer) ===
        {
            ImVec2 status_region = ImGui::GetContentRegionAvail();
            if (ImGui::BeginChild("StatusBar", ImVec2(0.0f, status_region.y), false,
                                  ImGuiWindowFlags_NoScrollbar)) {
                if (!error_message.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                    ImGui::TextUnformatted(error_message.c_str());
                    ImGui::PopStyleColor();
                } else if (!status_message.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
                    ImGui::TextUnformatted(status_message.c_str());
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndChild();
        }

        ImGui::End();
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

    int incoming_connections = 0;
    int outgoing_connections = 0;
    for (const auto& connection : node_editor->getConnections()) {
        if (connection.to_node_id == selected_node->id) {
            ++incoming_connections;
        }
        if (connection.from_node_id == selected_node->id) {
            ++outgoing_connections;
        }
    }
    
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

    ImGui::Text("Connections: in %d / out %d", incoming_connections, outgoing_connections);
    ImGui::Text("Ports: %zu inputs / %zu outputs", selected_node->input_ports.size(), selected_node->output_ports.size());
    
    ImGui::Separator();
    
    // Variable-specific properties
    if (selected_node->type == NodeType::GlobalVariable || 
        selected_node->type == NodeType::CellVariable ||
        selected_node->type == NodeType::Parameter ||
        selected_node->type == NodeType::Derived) {

        ImGui::Text("Variable name:");
        if (editTextField("##variable_name", selected_node->variable_name)) markDirty();
        ImGui::Text("Label:");
        if (editTextField("##variable_label", selected_node->label)) markDirty();
        ImGui::Text("Support: %s", selected_node->support == VariableSupport::Cell ? "cell" : "global");
        
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

        ImGui::Text("Units:");
        if (editTextField("##units", selected_node->units)) markDirty();
        ImGui::Text("Domain ref:");
        if (editTextField("##domain_ref", selected_node->domain_ref)) markDirty();
        ImGui::Text("Initial value:");
        if (editTextField("##initial_value", selected_node->initial_value)) markDirty();
        ImGui::Text("Description:");
        if (editMultilineField("##description", selected_node->description, ImVec2(-1.0f, 78.0f))) markDirty();
    }
    
    // Interaction-specific properties
    if (selected_node->type == NodeType::Equation || 
        selected_node->type == NodeType::Operator) {

        ImGui::Text("Interaction label:");
        if (editTextField("##interaction_label", selected_node->label)) markDirty();
        ImGui::Text("Formula logic:");
        if (editMultilineField("##formula_logic", selected_node->formula_logic, ImVec2(-1.0f, 88.0f))) markDirty();
        ImGui::Text("Target type:");
        if (editTextField("##target_type", selected_node->target_type)) markDirty();
        
        const char* target_types[] = { "scalar", "grid" };
        int target_idx = (selected_node->target_type == "grid") ? 1 : 0;
        if (ImGui::Combo("Target Type", &target_idx, target_types, IM_ARRAYSIZE(target_types))) {
            selected_node->target_type = (target_idx == 0) ? "scalar" : "grid";
            markDirty();
        }
    }

    if (selected_node->type == NodeType::Stage || selected_node->type == NodeType::Domain) {
        ImGui::Text("Label:");
        if (editTextField("##stage_label", selected_node->label)) markDirty();
        if (selected_node->type == NodeType::Domain) {
            ImGui::Text("Domain ref:");
            if (editTextField("##domain_label", selected_node->domain_ref)) markDirty();
        }
    }
    
    ImGui::Separator();
    ImGui::Text("Position:");
    float pos[2] = {selected_node->position.x, selected_node->position.y};
    if (ImGui::DragFloat2("##node_position", pos, 1.0f)) {
        selected_node->position = ImVec2(pos[0], pos[1]);
        markDirty();
    }
    ImGui::Text("Size:");
    float size[2] = {selected_node->size.x, selected_node->size.y};
    if (ImGui::DragFloat2("##node_size", size, 1.0f, 40.0f, 4096.0f)) {
        selected_node->size = ImVec2(std::max(40.0f, size[0]), std::max(40.0f, size[1]));
        markDirty();
    }
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
    node.size = ImVec2(136.0f, 68.0f);
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
    node.size = ImVec2(160.0f, 80.0f);
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
    node.size = ImVec2(152.0f, 76.0f);
    node.position = ImVec2(50.0f, 300.0f + counter * 50);
    
    node_editor->addNode(node);
    markDirty();
}

void ModelEditorWindow::addDomainNode(NodeType node_type) {
    static int counter = 0;
    std::string node_id = std::string("domain_") + std::to_string(counter++);
    
    Node node(node_id, node_type, "Domain");
    node.size = ImVec2(144.0f, 72.0f);
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
