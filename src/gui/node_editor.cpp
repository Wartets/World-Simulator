#include "ws/gui/node_editor.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace ws::gui {

// === Node Implementation ===

ImU32 Node::getColorU32() const {
    ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // Default white
    
    switch (type) {
        case NodeType::GlobalVariable:
            color = ImVec4(0.3f, 0.5f, 0.9f, 1.0f); // Blue
            break;
        case NodeType::CellVariable:
            color = ImVec4(0.4f, 0.8f, 0.4f, 1.0f); // Green
            break;
        case NodeType::Parameter:
            color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); // Gray
            break;
        case NodeType::Derived:
            color = ImVec4(0.7f, 0.4f, 0.8f, 1.0f); // Purple
            break;
        case NodeType::Equation:
        case NodeType::Operator:
            color = ImVec4(0.9f, 0.6f, 0.3f, 1.0f); // Orange
            break;
        case NodeType::Stage:
            color = ImVec4(0.8f, 0.8f, 0.6f, 1.0f); // Light yellow
            break;
        case NodeType::Domain:
            color = ImVec4(0.5f, 0.7f, 0.9f, 1.0f); // Light blue
            break;
    }
    
    if (is_selected) {
        color.x = std::min(1.0f, color.x + 0.2f);
        color.y = std::min(1.0f, color.y + 0.2f);
        color.z = std::min(1.0f, color.z + 0.2f);
    }
    
    return ImGui::GetColorU32(color);
}


bool Node::contains(ImVec2 point) const {
    return point.x >= position.x && point.x <= position.x + size.x &&
           point.y >= position.y && point.y <= position.y + size.y;
}

void Node::render(ImDrawList* draw_list, bool is_hovered_node) {
    ImU32 bg_color = getColorU32();
    ImU32 border_color = is_hovered_node 
        ? ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 1.0f))
        : ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    float border_thickness = is_hovered_node ? 2.5f : 1.5f;
    
    ImVec2 min_pos = position;
    ImVec2 max_pos = ImVec2(position.x + size.x, position.y + size.y);
    
    // Draw background
    draw_list->AddRectFilled(min_pos, max_pos, bg_color, 4.0f);
    
    // Draw border
    draw_list->AddRect(min_pos, max_pos, border_color, 4.0f, 0, border_thickness);
    
    // Draw label
    ImVec2 text_pos = ImVec2(position.x + 6.0f, position.y + 8.0f);
    ImU32 text_color = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    
    // Truncate long labels
    std::string display_label = label;
    if (display_label.length() > 14) {
        display_label = display_label.substr(0, 11) + "...";
    }
    
    draw_list->AddText(text_pos, text_color, display_label.c_str());
    
    // Draw input ports (left side)
    for (const auto& port : input_ports) {
        ImVec2 port_pos = getPortWorldPosition(port);
        ImU32 port_color = ImGui::GetColorU32(ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
        draw_list->AddCircleFilled(port_pos, 4.0f, port_color);
    }
    
    // Draw output ports (right side)
    for (const auto& port : output_ports) {
        ImVec2 port_pos = getPortWorldPosition(port);
        ImU32 port_color = ImGui::GetColorU32(ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        draw_list->AddCircleFilled(port_pos, 4.0f, port_color);
    }
}

// === NodeEditor Implementation ===

NodeEditor::NodeEditor() 
    : dragging_node_id(""), panning(false), creating_connection(false),
      connection_source_port(nullptr) {
}

NodeEditor::~NodeEditor() = default;

std::string NodeEditor::addNode(const Node& node) {
    auto new_node = std::make_unique<Node>(node);
    std::string node_id = node.id;
    nodes.push_back(std::move(new_node));
    return node_id;
}

void NodeEditor::removeNode(const std::string& node_id) {
    nodes.erase(
        std::remove_if(nodes.begin(), nodes.end(),
            [&node_id](const std::unique_ptr<Node>& n) { return n->id == node_id; }),
        nodes.end()
    );
    
    // Remove all connections to/from this node
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [&node_id](const Connection& c) {
                return c.from_node_id == node_id || c.to_node_id == node_id;
            }),
        connections.end()
    );
    
    // Remove from selection
    selected_nodes.erase(
        std::remove(selected_nodes.begin(), selected_nodes.end(), node_id),
        selected_nodes.end()
    );
}

Node* NodeEditor::getNode(const std::string& node_id) {
    for (auto& node : nodes) {
        if (node->id == node_id) return node.get();
    }
    return nullptr;
}

const Node* NodeEditor::getNode(const std::string& node_id) const {
    for (const auto& node : nodes) {
        if (node->id == node_id) return node.get();
    }
    return nullptr;
}

void NodeEditor::addConnection(const Connection& conn) {
    // Check if already exists
    for (const auto& c : connections) {
        if (c.from_node_id == conn.from_node_id && 
            c.to_node_id == conn.to_node_id &&
            c.from_port_name == conn.from_port_name &&
            c.to_port_name == conn.to_port_name) {
            return; // Already exists
        }
    }
    connections.push_back(conn);
}

void NodeEditor::removeConnection(const Connection& conn) {
    connections.erase(
        std::remove_if(connections.begin(), connections.end(),
            [&conn](const Connection& c) {
                return c.from_node_id == conn.from_node_id && 
                       c.to_node_id == conn.to_node_id &&
                       c.from_port_name == conn.from_port_name &&
                       c.to_port_name == conn.to_port_name;
            }),
        connections.end()
    );
}

void NodeEditor::selectNode(const std::string& node_id, bool multi_select) {
    if (!multi_select) {
        clearSelection();
    }
    
    // Check if already selected
    if (std::find(selected_nodes.begin(), selected_nodes.end(), node_id) != selected_nodes.end()) {
        return;
    }
    
    selected_nodes.push_back(node_id);
    Node* node = getNode(node_id);
    if (node) node->is_selected = true;
}

void NodeEditor::clearSelection() {
    for (const auto& node_id : selected_nodes) {
        Node* node = getNode(node_id);
        if (node) node->is_selected = false;
    }
    selected_nodes.clear();
}

void NodeEditor::render(ImVec2 canvas_size) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    
    // Create a canvas region for clipping
    ImGui::InvisibleButton("canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    
    if (ImGui::IsItemHovered()) {
        handleMouseInput(ImGui::GetMousePos(), ImGui::IsMouseDown(ImGuiMouseButton_Left), 
                        ImGui::IsMouseDown(ImGuiMouseButton_Right));
    }
    
    // Handle panning
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
        handlePanning(delta);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
    }
    
    // Handle zoom with mouse wheel
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
        handleZoom(ImGui::GetIO().MouseWheel);
    }
    
    // Set up clipping region
    draw_list->PushClipRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), true);
    
    // Render grid
    renderGrid(draw_list, canvas_size);
    
    // Render connections
    renderConnections(draw_list);
    
    // Render connection preview
    renderConnectionPreview(draw_list);
    
    // Render nodes
    renderNodes(draw_list);
    
    draw_list->PopClipRect();
}

void NodeEditor::handleMouseInput(ImVec2 mouse_pos, bool lmb_down, bool rmb_down) {
    if (!lmb_down && !dragging_node_id.empty()) {
        dragging_node_id.clear();
    }
    
    if (lmb_down && dragging_node_id.empty()) {
        Node* node = getNodeAtPosition(mouse_pos);
        if (node) {
            dragging_node_id = node->id;
            drag_offset = ImVec2(node->position.x - mouse_pos.x, node->position.y - mouse_pos.y);
        }
    }
    
    if (!dragging_node_id.empty()) {
        Node* node = getNode(dragging_node_id);
        if (node) {
            node->position = ImVec2(mouse_pos.x + drag_offset.x, mouse_pos.y + drag_offset.y);
        }
    }
}

void NodeEditor::handlePanning(ImVec2 delta) {
    view_offset = ImVec2(view_offset.x + delta.x, view_offset.y + delta.y);
}

void NodeEditor::handleZoom(float delta) {
    float zoom_factor = delta > 0.0f ? 1.1f : 0.9f;
    setZoom(zoom * zoom_factor);
}

void NodeEditor::autoLayout() {
    // Simple hierarchical layout (depth-first)
    int columns = static_cast<int>(std::ceil(std::sqrt(nodes.size())));
    int row = 0, col = 0;
    
    for (auto& node : nodes) {
        node->position = ImVec2(col * 160.0f, row * 100.0f);
        
        col++;
        if (col >= columns) {
            col = 0;
            row++;
        }
    }
}

void NodeEditor::resetView() {
    view_offset = ImVec2(0.0f, 0.0f);
    zoom = 1.0f;
}

Node* NodeEditor::getNodeAtPosition(ImVec2 pos) {
    for (auto& node : nodes) {
            if (node->contains(pos)) {
            return node.get();
        }
    }
    return nullptr;
}

void NodeEditor::renderNodes(ImDrawList* draw_list) {
    // Clear hovered state
    for (auto& node : nodes) {
        node->is_hovered = false;
    }
    
    // Update hovered state
    ImVec2 mouse_pos = ImGui::GetMousePos();
    for (auto& node : nodes) {
            if (node->contains(mouse_pos)) {
            node->is_hovered = true;
        }
    }
    
    // Render all nodes
    for (auto& node : nodes) {
        node->render(draw_list, node->is_hovered);
    }
}

void NodeEditor::renderConnections(ImDrawList* draw_list) {
    ImU32 line_color = ImGui::GetColorU32(ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    
    for (const auto& conn : connections) {
        Node* from_node = getNode(conn.from_node_id);
        Node* to_node = getNode(conn.to_node_id);
        
        if (!from_node || !to_node) continue;
        
        // Find ports by name
        Port* from_port = nullptr;
        Port* to_port = nullptr;
        
        for (auto& port : from_node->output_ports) {
            if (port.name == conn.from_port_name) {
                from_port = &port;
                break;
            }
        }
        
        for (auto& port : to_node->input_ports) {
            if (port.name == conn.to_port_name) {
                to_port = &port;
                break;
            }
        }
        
        if (from_port && to_port) {
            ImVec2 from_pos = from_node->getPortWorldPosition(*from_port);
            ImVec2 to_pos = to_node->getPortWorldPosition(*to_port);
            
            // Draw Bezier curve
            ImVec2 control1 = ImVec2(from_pos.x + 50.0f, from_pos.y);
            ImVec2 control2 = ImVec2(to_pos.x - 50.0f, to_pos.y);
            
            draw_list->AddBezierCubic(from_pos, control1, control2, to_pos, line_color, 2.0f, 20);
        }
    }
}

void NodeEditor::renderConnectionPreview(ImDrawList* draw_list) {
    if (creating_connection && connection_source_port) {
        // Draw preview line from source port to current mouse position
        ImU32 preview_color = ImGui::GetColorU32(ImVec4(1.0f, 0.7f, 0.2f, 0.8f));
        draw_list->AddLine(ImGui::GetMousePos(), connection_preview_end, preview_color, 2.0f);
    }
}

void NodeEditor::renderGrid(ImDrawList* draw_list, ImVec2 canvas_size) {
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImU32 grid_color = ImGui::GetColorU32(ImVec4(0.4f, 0.4f, 0.4f, 0.2f));
    const float grid_size = 16.0f;
    
    for (float x = fmodf(view_offset.x, grid_size); x < canvas_size.x; x += grid_size) {
        draw_list->AddLine(
            ImVec2(canvas_pos.x + x, canvas_pos.y),
            ImVec2(canvas_pos.x + x, canvas_pos.y + canvas_size.y),
            grid_color, 0.5f
        );
    }
    
    for (float y = fmodf(view_offset.y, grid_size); y < canvas_size.y; y += grid_size) {
        draw_list->AddLine(
            ImVec2(canvas_pos.x, canvas_pos.y + y),
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + y),
            grid_color, 0.5f
        );
    }
}

} // namespace ws::gui
