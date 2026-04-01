#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
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
    ImVec2 size{120.0f, 60.0f};
    
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
    
    // Render this node using ImGui draw list
    void render(ImDrawList* draw_list, bool is_hovered_node = false);
    
    // Check if point is inside this node
    bool contains(ImVec2 point) const;
    
    // Get port position in world space
    ImVec2 getPortWorldPosition(const Port& port) const {
        ImVec2 port_offset = port.is_input 
            ? ImVec2(-8.0f, size.y * 0.5f) 
            : ImVec2(size.x + 8.0f, size.y * 0.5f);
        return ImVec2(position.x + port_offset.x, position.y + port_offset.y);
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
    void handleZoom(float delta);
    
    // Layout
    void autoLayout();
    void resetView();
    
    // Queries
    Node* getNodeAtPosition(ImVec2 pos);
    
    // State accessors
    ImVec2 getViewOffset() const { return view_offset; }
    float getZoom() const { return zoom; }
    void setViewOffset(ImVec2 offset) { view_offset = offset; }
    void setZoom(float z) { zoom = std::max(0.1f, std::min(z, 5.0f)); }
    
private:
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<Connection> connections;
    std::vector<std::string> selected_nodes;
    
    ImVec2 view_offset{0.0f, 0.0f};
    float zoom{1.0f};
    
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
