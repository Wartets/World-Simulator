#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <cmath>
#include <imgui.h>

namespace ws::gui {

// === Node Types ===

enum class NodeType {
    GlobalVariable,
    CellVariable,
    Parameter,
    Derived,
    Equation,
    Operator,
    Stage,
    Domain
};

enum class VariableRole {
    Parameter,
    State,
    Forcing,
    Derived,
    Auxiliary
};

enum class VariableSupport {
    Global,
    Cell
};

enum class DataType {
    F32,
    F64,
    I32,
    U32,
    Bool,
    Vec2,
    Vec3
};

// === Port Types ===

struct Port {
    std::string name;
    std::string variable_id;
    bool is_input{true};
    ImVec2 position;
    
    Port() = default;
    Port(const std::string& n, const std::string& vid, bool is_in) 
        : name(n), variable_id(vid), is_input(is_in) {}
};

// === Node Definition ===

struct Node {
    std::string id;
    NodeType type;
    std::string label;
    ImVec2 position;
    ImVec2 size{132.0f, 66.0f};
    
    // Variable node properties
    std::string variable_name;
    DataType data_type{DataType::F32};
    VariableRole role{VariableRole::State};
    VariableSupport support{VariableSupport::Global};
    std::string units;
    std::string domain_ref;
    std::string initial_value;
    std::string description;
    
    // Interaction node properties
    std::string formula_logic;
    std::string target_type; // "scalar" or "grid"
    
    // Computed/display properties
    std::vector<Port> input_ports;
    std::vector<Port> output_ports;
    bool is_selected{false};
    bool is_hovered{false};
    ImU32 color_tint{0xFFFFFFFF}; // Default white
    
    Node() = default;
    explicit Node(const std::string& n_id, NodeType n_type, const std::string& n_label)
        : id(n_id), type(n_type), label(n_label) {}
    
    // Get color based on node type and role
    ImU32 getColorU32() const;
    
    // Render this node at the given screen position and scale
    void render(ImDrawList* draw_list, ImVec2 screen_pos, float scale, bool is_hovered_node = false);
    
    // Check if point (world space) is inside this node
    bool contains(ImVec2 point) const;
    
    // Get port position in world space
    ImVec2 getPortWorldPosition(const Port& port) const {
        const ImVec2 center(position.x + size.x * 0.5f, position.y + size.y * 0.5f);
        const float radius = std::max(24.0f, 0.5f * std::min(size.x, size.y) - 8.0f);
        const std::size_t hash = std::hash<std::string>{}(port.name + "|" + port.variable_id + (port.is_input ? "|in" : "|out"));
        const float hash01 = static_cast<float>(hash & 0xFFFFu) / 65535.0f;
        const float angle = hash01 * 6.28318530718f;
        return ImVec2(center.x + std::cos(angle) * radius,
                      center.y + std::sin(angle) * radius);
    }

    ImVec2 getConnectionAnchorWorldPosition(const ImVec2& other_center) const {
        const ImVec2 center(position.x + size.x * 0.5f, position.y + size.y * 0.5f);
        const float dx = other_center.x - center.x;
        const float dy = other_center.y - center.y;
        const float rx = std::max(20.0f, size.x * 0.5f - 4.0f);
        const float ry = std::max(20.0f, size.y * 0.5f - 4.0f);
        const float sx = std::max(0.001f, std::fabs(dx) / rx);
        const float sy = std::max(0.001f, std::fabs(dy) / ry);
        const float scale = 1.0f / std::max(sx, sy);
        return ImVec2(center.x + dx * scale, center.y + dy * scale);
    }
};

// === Connection (Edge) ===

struct Connection {
    std::string from_node_id;
    std::string to_node_id;
    std::string from_port_name;
    std::string to_port_name;
    
    Connection() = default;
    Connection(const std::string& from, const std::string& to,
               const std::string& from_p, const std::string& to_p)
        : from_node_id(from), to_node_id(to), 
          from_port_name(from_p), to_port_name(to_p) {}
};

// === Node Editor State ===

class NodeEditor {
public:
    NodeEditor();
    ~NodeEditor();
    
    // Node management
    std::string addNode(const Node& node);
    void removeNode(const std::string& node_id);
    Node* getNode(const std::string& node_id);
    const Node* getNode(const std::string& node_id) const;
    
    const std::vector<std::unique_ptr<Node>>& getAllNodes() const { return nodes; }
    
    // Connection management
    void addConnection(const Connection& conn);
    void removeConnection(const Connection& conn);
    const std::vector<Connection>& getConnections() const { return connections; }
    
    // Selection
    void selectNode(const std::string& node_id, bool multi_select = false);
    void clearSelection();
    const std::vector<std::string>& getSelectedNodeIds() const { return selected_nodes; }
    
    // Rendering
    void render(ImVec2 canvas_size);
    
    // Interaction (called by parent window)
    void handleMouseInput(ImVec2 mouse_pos, bool lmb_down, bool rmb_down);
    void handlePanning(ImVec2 delta);
    void handleZoom(float delta, ImVec2 mouse_screen_pos);
    
    // Layout
    void autoLayout();
    void relaxCircularLayout();
    void resetView();
    void fitAllNodes(ImVec2 canvas_size);
    
    // Queries
    Node* getNodeAtPosition(ImVec2 pos);
    
    // Coordinate transforms
    ImVec2 worldToScreen(ImVec2 world) const;
    ImVec2 screenToWorld(ImVec2 screen) const;
    
    // State accessors
    ImVec2 getViewOffset() const { return view_offset; }
    float getZoom() const { return zoom; }
    void setViewOffset(ImVec2 offset) { view_offset = offset; }
    void setZoom(float z) { zoom = std::max(0.1f, std::min(z, 5.0f)); }
    
private:
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<Connection> connections;
    std::vector<std::string> selected_nodes;
    
    ImVec2 canvas_origin{0.0f, 0.0f};
    ImVec2 canvas_size{0.0f, 0.0f};
    ImVec2 view_offset{0.0f, 0.0f};
    float zoom{1.0f};
    bool needs_fit{true};
    
    // Interaction state
    std::string dragging_node_id;
    ImVec2 drag_offset;
    bool panning{false};
    ImVec2 pan_start;
    
    // Connection preview (when user is dragging to create connection)
    bool creating_connection{false};
    Port* connection_source_port{nullptr};
    ImVec2 connection_preview_end;
    
    // Rendering helpers
    void renderNodes(ImDrawList* draw_list);
    void renderConnections(ImDrawList* draw_list);
    void renderConnectionPreview(ImDrawList* draw_list);
    void renderGrid(ImDrawList* draw_list, ImVec2 canvas_size);
    
    // Port layout computation
    void computePortPositions(Node& node);
};

} // namespace ws::gui
