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

// Types of nodes in the model graph.
enum class NodeType {
    GlobalVariable,  // Global scalar variable
    CellVariable,    // Per-cell variable
    Parameter,        // Model parameter
    Derived,          // Derived/computed variable
    Equation,         // Mathematical equation
    Operator,         // Operator node
    Stage,            // Simulation stage
    Domain            // Domain specification
};

// Role of a variable in the model.
enum class VariableRole {
    Parameter,   // Model parameter (settable)
    State,       // State variable (evolves)
    Forcing,     // External forcing
    Derived,     // Derived from other variables
    Auxiliary    // Auxiliary/computed
};

// Spatial support of a variable.
enum class VariableSupport {
    Global,  // Single global value
    Cell     // Per-cell values
};

// Data type for variables.
enum class DataType {
    F32,    // 32-bit float
    F64,    // 64-bit float
    I32,    // 32-bit integer
    U32,    // 32-bit unsigned
    Bool,   // Boolean
    Vec2,   // 2D vector
    Vec3    // 3D vector
};

// === Port Types ===

// Connection point on a node.
struct Port {
    std::string name;
    std::string variableId;
    bool isInput{true};
    ImVec2 position;
    
    Port() = default;
    Port(const std::string& n, const std::string& vid, bool is_in) 
        : name(n), variableId(vid), isInput(is_in) {}
};

// === Node Definition ===

// A node in the model graph representing a variable or operation.
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
        const float half_w = std::max(24.0f, size.x * 0.5f - 6.0f);
        const float half_h = std::max(14.0f, size.y * 0.5f - 6.0f);
        const float shape_n = 4.0f; // superellipse exponent ~ rounded rectangle
        const std::size_t hash = std::hash<std::string>{}(port.name + "|" + port.variableId + (port.isInput ? "|in" : "|out"));
        const float hash01 = static_cast<float>(hash & 0xFFFFu) / 65535.0f;
        const float angle = hash01 * 6.28318530718f;
        const float dx = std::cos(angle);
        const float dy = std::sin(angle);
        const float norm = std::pow(std::pow(std::fabs(dx) / half_w, shape_n) +
                                    std::pow(std::fabs(dy) / half_h, shape_n),
                                    1.0f / shape_n);
        const float scale = (norm > 0.000001f) ? (1.0f / norm) : 1.0f;
        return ImVec2(center.x + dx * scale,
                      center.y + dy * scale);
    }

    ImVec2 getConnectionAnchorWorldPosition(const ImVec2& other_center) const {
        const ImVec2 center(position.x + size.x * 0.5f, position.y + size.y * 0.5f);
        const float dx = other_center.x - center.x;
        const float dy = other_center.y - center.y;
        const float half_w = std::max(20.0f, size.x * 0.5f - 4.0f);
        const float half_h = std::max(12.0f, size.y * 0.5f - 4.0f);
        const float shape_n = 4.0f;
        const float norm = std::pow(std::pow(std::fabs(dx) / half_w, shape_n) +
                                    std::pow(std::fabs(dy) / half_h, shape_n),
                                    1.0f / shape_n);
        const float scale = (norm > 0.000001f) ? (1.0f / norm) : 1.0f;
        return ImVec2(center.x + dx * scale, center.y + dy * scale);
    }
};

// === Connection (Edge) ===

// A connection between two nodes.
struct Connection {
    std::string fromNodeId;
    std::string toNodeId;
    std::string fromPortName;
    std::string toPortName;
    
    Connection() = default;
    Connection(const std::string& from, const std::string& to,
               const std::string& from_p, const std::string& to_p)
                : fromNodeId(from), toNodeId(to), 
                    fromPortName(from_p), toPortName(to_p) {}
};

// === Node Editor State ===

// Interactive node-based model editor.
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
    
    // Layout relaxation periodic update counter
    // Allows smooth graph optimization without calling relaxCircularLayout() every frame
    int layout_relax_counter{0};
    static constexpr int LAYOUT_RELAX_INTERVAL = 3;  // Update every 3 frames (~50ms at 60fps)
    static constexpr double LAYOUT_INTERACTION_COOLDOWN_SEC = 0.22;
    double last_user_interaction_time{-1.0};
    int repulsion_phase{0};
    
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
