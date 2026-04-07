#include "ws/gui/node_editor.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <limits>

namespace {

// Brightens ImGui color by multiplication factor.
// @param color Original color as packed U32
// @param factor Brightening factor (>1 brightens, <1 darkens)
// @return Brightened color as packed U32
ImU32 brighten(ImU32 color, float factor) {
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.x = std::clamp(rgba.x * factor, 0.0f, 1.0f);
    rgba.y = std::clamp(rgba.y * factor, 0.0f, 1.0f);
    rgba.z = std::clamp(rgba.z * factor, 0.0f, 1.0f);
    return ImGui::GetColorU32(rgba);
}

// Linear interpolation between two 2D points.
// @param a First point
// @param b Second point
// @param t Interpolation factor [0, 1]
// @return Interpolated point
ImVec2 lerp(const ImVec2& a, const ImVec2& b, float t) {
    return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

// Clamps vector length to maximum while preserving direction.
// @param v Input vector
// @param maxLen Maximum length
// @return Clamped vector
ImVec2 clampLength(const ImVec2& v, float maxLen) {
    const float lenSq = v.x * v.x + v.y * v.y;
    if (lenSq <= maxLen * maxLen || lenSq <= 0.000001f) {
        return v;
    }
    const float len = std::sqrt(lenSq);
    const float scale = maxLen / len;
    return ImVec2(v.x * scale, v.y * scale);
}

std::string normalizeNodeLabel(const std::string& label) {
    std::string out;
    out.reserve(label.size());
    bool prev_space = false;
    for (char c : label) {
        const bool make_space = (c == '-' || c == '_' || c == '/');
        const char mapped = make_space ? ' ' : c;
        if (mapped == ' ') {
            if (!prev_space) {
                out.push_back(' ');
            }
            prev_space = true;
        } else {
            out.push_back(mapped);
            prev_space = false;
        }
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string wrapLabelToWidth(const std::string& text, float max_width, int max_lines) {
    if (text.empty() || max_width <= 8.0f || max_lines <= 1) {
        return text;
    }

    std::vector<std::string> words;
    std::string token;
    for (char c : text) {
        if (c == ' ' || c == '\n' || c == '\t') {
            if (!token.empty()) {
                words.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) {
        words.push_back(token);
    }
    if (words.empty()) {
        return text;
    }

    std::vector<std::string> lines;
    std::string current;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string candidate = current.empty() ? words[i] : (current + " " + words[i]);
        if (ImGui::CalcTextSize(candidate.c_str()).x <= max_width || current.empty()) {
            current = candidate;
            continue;
        }

        lines.push_back(current);
        current = words[i];
        if (static_cast<int>(lines.size()) >= max_lines - 1) {
            break;
        }
    }

    if (!current.empty()) {
        lines.push_back(current);
    }

    if (static_cast<int>(lines.size()) > max_lines) {
        lines.resize(static_cast<std::size_t>(max_lines));
    }
    if (lines.empty()) {
        return text;
    }

    std::string wrapped;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        wrapped += lines[i];
        if (i + 1 < lines.size()) {
            wrapped += '\n';
        }
    }
    return wrapped;
}

const char* nodeSymbol(ws::gui::NodeType type) {
    switch (type) {
        case ws::gui::NodeType::GlobalVariable: return "G";
        case ws::gui::NodeType::CellVariable: return "C";
        case ws::gui::NodeType::Parameter: return "P";
        case ws::gui::NodeType::Derived: return "D";
        case ws::gui::NodeType::Equation: return "Eq";
        case ws::gui::NodeType::Operator: return "Op";
        case ws::gui::NodeType::Stage: return "S";
        case ws::gui::NodeType::Domain: return "Dm";
    }
    return "?";
}

ImVec2 normalizedOr(const ImVec2& v, const ImVec2& fallback) {
    const float lenSq = v.x * v.x + v.y * v.y;
    if (lenSq <= 0.000001f) {
        return fallback;
    }
    const float invLen = 1.0f / std::sqrt(lenSq);
    return ImVec2(v.x * invLen, v.y * invLen);
}

float dot(const ImVec2& a, const ImVec2& b) {
    return a.x * b.x + a.y * b.y;
}

ImVec2 superellipsePoint(const ws::gui::Node& node, float angle, float inset = 4.0f) {
    const ImVec2 center(node.position.x + node.size.x * 0.5f, node.position.y + node.size.y * 0.5f);
    const float half_w = std::max(20.0f, node.size.x * 0.5f - inset);
    const float half_h = std::max(12.0f, node.size.y * 0.5f - inset);
    const float n = 8.0f;
    const float dx = std::cos(angle);
    const float dy = std::sin(angle);
    const float norm = std::pow(std::pow(std::fabs(dx) / half_w, n) +
                                std::pow(std::fabs(dy) / half_h, n),
                                1.0f / n);
    const float scale = (norm > 0.000001f) ? (1.0f / norm) : 1.0f;
    return ImVec2(center.x + dx * scale, center.y + dy * scale);
}

ImVec2 connectionAnchorForNode(const ws::gui::Node& node,
                               const ImVec2& other_center,
                               const ws::gui::Connection& conn,
                               bool source_side) {
    const ImVec2 center(node.position.x + node.size.x * 0.5f, node.position.y + node.size.y * 0.5f);
    const float base_angle = std::atan2(other_center.y - center.y, other_center.x - center.x);

    const std::string hashKey = source_side
        ? (conn.from_node_id + "|" + conn.from_port_name + "|" + conn.to_node_id + "|" + conn.to_port_name)
        : (conn.to_node_id + "|" + conn.to_port_name + "|" + conn.from_node_id + "|" + conn.from_port_name);
    const std::size_t h = std::hash<std::string>{}(hashKey);
    const float u = static_cast<float>(h & 0xFFFFu) / 65535.0f; // [0,1]
    const float signed_u = (u * 2.0f) - 1.0f; // [-1,1]

    const float dist = std::sqrt((other_center.x - center.x) * (other_center.x - center.x) +
                                 (other_center.y - center.y) * (other_center.y - center.y));
    const float spread = std::clamp(0.16f + 80.0f / std::max(80.0f, dist), 0.18f, 0.52f);
    const float angle = base_angle + signed_u * spread;
    return superellipsePoint(node, angle, 4.5f);
}

ImVec2 connectionAnchorForLabelBox(const ws::gui::Node& node,
                                    const ImVec2& other_center,
                                    const ws::gui::Connection& conn,
                                    bool source_side) {
    const ImVec2 center(node.position.x + node.size.x * 0.5f, node.position.y + node.size.y * 0.5f);
    const ImVec2 dx_vec(other_center.x - center.x, other_center.y - center.y);
    const float dist_to_other = std::sqrt(dx_vec.x * dx_vec.x + dx_vec.y * dx_vec.y);
    
    if (dist_to_other < 0.001f) {
        // If other node is at same position, use default anchor
        return ImVec2(center.x + node.size.x * 0.5f, center.y);
    }
    
    const float base_angle = std::atan2(dx_vec.y, dx_vec.x);
    
    // Hash-based angular spread to distribute multiple edges around the perimeter
    const std::string hashKey = source_side
        ? (conn.from_node_id + "|" + conn.from_port_name + "|" + conn.to_node_id + "|" + conn.to_port_name)
        : (conn.to_node_id + "|" + conn.to_port_name + "|" + conn.from_node_id + "|" + conn.from_port_name);
    const std::size_t h = std::hash<std::string>{}(hashKey);
    const float u = static_cast<float>(h & 0xFFFFu) / 65535.0f;
    const float signed_u = (u * 2.0f) - 1.0f;
    
    // Spread angle based on distance to other node
    const float spread = std::clamp(0.12f + 60.0f / std::max(80.0f, dist_to_other), 0.12f, 0.48f);
    const float final_angle = base_angle + signed_u * spread;
    
    // Exact label dimensions from Node::render():
    // - max_label_width = scaled_size.x - 18.0f (in world space: node.size.x - 18.0f)
    // - Vertical: full node height (text centered in entire node)
    const float half_width = std::max(10.0f, (node.size.x - 18.0f) * 0.5f);
    const float half_height = node.size.y * 0.5f;
    
    // Rounded rectangle perimeter intersection using superellipse (n=4)
    const float abs_cos = std::fabs(std::cos(final_angle));
    const float abs_sin = std::fabs(std::sin(final_angle));
    const float shape_n = 8.0f;
    
    const float norm = std::pow(std::pow(abs_cos / half_width, shape_n) +
                                std::pow(abs_sin / half_height, shape_n),
                                1.0f / shape_n);
    
    const float scale = (norm > 0.000001f) ? (1.0f / norm) : 1.0f;
    
    const ImVec2 anchor(center.x + std::cos(final_angle) * scale,
                       center.y + std::sin(final_angle) * scale);
    return anchor;
}

void drawArrowHead(ImDrawList* draw_list, const ImVec2& from, const ImVec2& to, ImU32 color, float size) {
    const ImVec2 dir(to.x - from.x, to.y - from.y);
    const float len = std::max(1.0f, std::sqrt(dir.x * dir.x + dir.y * dir.y));
    const ImVec2 n(dir.x / len, dir.y / len);
    const ImVec2 p(-n.y, n.x);
    const ImVec2 tip = to;
    const ImVec2 base(to.x - n.x * size, to.y - n.y * size);
    const ImVec2 left(base.x + p.x * (size * 0.45f), base.y + p.y * (size * 0.45f));
    const ImVec2 right(base.x - p.x * (size * 0.45f), base.y - p.y * (size * 0.45f));
    draw_list->AddTriangleFilled(tip, left, right, color);
}

} // namespace

namespace ws::gui {

// === Node Implementation ===

ImU32 Node::getColorU32() const {
    ImVec4 color = ImVec4(0.82f, 0.86f, 0.95f, 1.0f);
    
    switch (type) {
        case NodeType::GlobalVariable:
            color = ImVec4(0.27f, 0.49f, 0.90f, 1.0f);
            break;
        case NodeType::CellVariable:
            color = ImVec4(0.30f, 0.76f, 0.50f, 1.0f);
            break;
        case NodeType::Parameter:
            color = ImVec4(0.55f, 0.58f, 0.67f, 1.0f);
            break;
        case NodeType::Derived:
            color = ImVec4(0.70f, 0.45f, 0.88f, 1.0f);
            break;
        case NodeType::Equation:
        case NodeType::Operator:
            color = ImVec4(0.93f, 0.66f, 0.32f, 1.0f);
            break;
        case NodeType::Stage:
            color = ImVec4(0.80f, 0.76f, 0.45f, 1.0f);
            break;
        case NodeType::Domain:
            color = ImVec4(0.45f, 0.72f, 0.90f, 1.0f);
            break;
    }
    
    if (is_selected) {
        color.x = std::min(1.0f, color.x + 0.14f);
        color.y = std::min(1.0f, color.y + 0.14f);
        color.z = std::min(1.0f, color.z + 0.14f);
    }
    
    return ImGui::GetColorU32(color);
}


bool Node::contains(ImVec2 point) const {
    const ImVec2 center(position.x + size.x * 0.5f, position.y + size.y * 0.5f);
    const float rx = std::max(18.0f, size.x * 0.5f);
    const float ry = std::max(10.0f, size.y * 0.5f);
    const float n = 4.0f;
    const float dx = std::fabs(point.x - center.x) / rx;
    const float dy = std::fabs(point.y - center.y) / ry;
    return std::pow(dx, n) + std::pow(dy, n) <= 1.0f;
}

void Node::render(ImDrawList* draw_list, ImVec2 screen_pos, float scale, bool is_hovered_node) {
    ImU32 bg_color = getColorU32();
    ImU32 border_color = is_hovered_node
        ? brighten(bg_color, 1.40f)
        : brighten(bg_color, 0.55f);
    const float border_thickness = (is_hovered_node ? 3.0f : 1.8f) * scale;

    const ImVec2 scaled_size(size.x * scale, size.y * scale);
    const ImVec2 min_pos = screen_pos;
    const ImVec2 max_pos(screen_pos.x + scaled_size.x, screen_pos.y + scaled_size.y);
    const float rounding = std::max(18.0f * scale, std::min(scaled_size.x, scaled_size.y) * 0.24f);

    draw_list->AddRectFilled(min_pos, max_pos, bg_color, rounding);
    draw_list->AddRect(min_pos, max_pos, border_color, rounding, 0, border_thickness);
    draw_list->AddRectFilled(ImVec2(min_pos.x + 2.0f * scale, min_pos.y + 2.0f * scale),
                             ImVec2(max_pos.x - 2.0f * scale, max_pos.y - 2.0f * scale),
                             ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f)),
                             rounding * 0.75f);

    draw_list->AddRectFilled(ImVec2(min_pos.x + 1.0f * scale, min_pos.y + 1.0f * scale),
                             ImVec2(max_pos.x - 1.0f * scale, min_pos.y + 14.0f * scale),
                             ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.08f)),
                             rounding,
                             ImDrawFlags_RoundCornersTop);

    const ImVec2 center(screen_pos.x + scaled_size.x * 0.5f, screen_pos.y + scaled_size.y * 0.5f);

    // Draw label (hide when zoomed out too far)
    if (scale > 0.34f) {
        ImU32 text_color = ImGui::GetColorU32(ImVec4(0.05f, 0.06f, 0.08f, 1.0f));
        std::string display_label = normalizeNodeLabel(label);
        if (display_label.empty()) {
            display_label = nodeSymbol(type);
        }

        float original_font_size = ImGui::GetFont()->FontSize;
        float scaled_font_size = original_font_size * std::max(0.92f, scale * 1.10f);
        scaled_font_size = std::max(scaled_font_size, 12.0f);

        const float max_label_width = std::max(24.0f, scaled_size.x - 18.0f * scale);
        const int max_lines = std::max(1, static_cast<int>((scaled_size.y - 10.0f * scale) / (scaled_font_size * 1.1f)));
        display_label = wrapLabelToWidth(display_label, max_label_width, std::min(3, max_lines));

        const ImVec2 text_size = ImGui::CalcTextSize(display_label.c_str());
        const ImVec2 text_pos(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
        draw_list->AddText(ImGui::GetFont(), scaled_font_size, text_pos, text_color, display_label.c_str());
    } else {
        const char* symbol = nodeSymbol(type);
        const float icon_size = std::max(10.0f, ImGui::GetFont()->FontSize * std::max(0.95f, scale * 1.8f));
        const ImVec2 sym_size = ImGui::CalcTextSize(symbol);
        const ImVec2 sym_pos(center.x - sym_size.x * 0.5f, center.y - sym_size.y * 0.5f);
        draw_list->AddText(ImGui::GetFont(), icon_size, sym_pos, ImGui::GetColorU32(ImVec4(0.07f, 0.08f, 0.10f, 0.95f)), symbol);
    }
}

// === NodeEditor Implementation ===

NodeEditor::NodeEditor() 
    : dragging_node_id(""), panning(false), creating_connection(false),
      connection_source_port(nullptr) {
}

NodeEditor::~NodeEditor() = default;

// === Coordinate Transforms ===

ImVec2 NodeEditor::worldToScreen(ImVec2 world) const {
    return ImVec2(
        canvas_origin.x + (world.x + view_offset.x) * zoom,
        canvas_origin.y + (world.y + view_offset.y) * zoom
    );
}

ImVec2 NodeEditor::screenToWorld(ImVec2 screen) const {
    return ImVec2(
        (screen.x - canvas_origin.x) / zoom - view_offset.x,
        (screen.y - canvas_origin.y) / zoom - view_offset.y
    );
}

// === Node Management ===

std::string NodeEditor::addNode(const Node& node) {
    auto new_node = std::make_unique<Node>(node);
    std::string node_id = node.id;
    nodes.push_back(std::move(new_node));
    needs_fit = true;
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

// === Main Render ===

void NodeEditor::render(ImVec2 avail_size) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    canvas_origin = ImGui::GetCursorScreenPos();
    canvas_size = avail_size;
    
    // Auto-fit on first render with content
    if (needs_fit && !nodes.empty()) {
        fitAllNodes(canvas_size);
        needs_fit = false;
    }
    
    // Create a canvas region for interaction
    ImGui::InvisibleButton("canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool canvas_hovered = ImGui::IsItemHovered();
    
    if (canvas_hovered) {
        ImVec2 mouse_pos = ImGui::GetMousePos();
        
        // Handle node interaction (left mouse button)
        handleMouseInput(mouse_pos, ImGui::IsMouseDown(ImGuiMouseButton_Left), 
                        ImGui::IsMouseDown(ImGuiMouseButton_Right));

        if (panning && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
            handlePanning(delta);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }
        
        // Handle panning (right mouse drag)
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
            handlePanning(delta);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
        }
        
        // Handle zoom with mouse wheel (centered on cursor)
        if (ImGui::GetIO().MouseWheel != 0.0f) {
            handleZoom(ImGui::GetIO().MouseWheel, mouse_pos);
        }
    } else {
        // Release drag if mouse leaves canvas
        if (!dragging_node_id.empty()) {
            last_user_interaction_time = ImGui::GetTime();
            dragging_node_id.clear();
        }
        panning = false;
        layout_relax_counter = 0;
    }
    
    // Layout relaxation is not called during render to prevent instability
    // when user is dragging nodes. The graph remains responsive and stable.
    // Use autoLayout() only at initialization if needed.
    
    // Periodic smooth graph layout updates for dynamic optimization.
    // Pause briefly after direct user interaction to avoid contention and UI stalls.
    const double now = ImGui::GetTime();
    const ImGuiIO& io = ImGui::GetIO();
    const bool mouse_buttons_down = io.MouseDown[ImGuiMouseButton_Left] || io.MouseDown[ImGuiMouseButton_Right];
    const bool user_interacting_now = mouse_buttons_down || std::fabs(io.MouseWheel) > 0.0f;
    const bool in_cooldown = last_user_interaction_time >= 0.0 &&
                             (now - last_user_interaction_time) < LAYOUT_INTERACTION_COOLDOWN_SEC;
    const int adaptive_interval = std::clamp(
        LAYOUT_RELAX_INTERVAL + static_cast<int>(nodes.size() / 36u),
        LAYOUT_RELAX_INTERVAL,
        20
    );

    if (!nodes.empty() && dragging_node_id.empty() && !panning && !in_cooldown && !user_interacting_now) {
        layout_relax_counter++;
        if (layout_relax_counter >= adaptive_interval) {
            relaxCircularLayout();
            layout_relax_counter = 0;
        }
    } else {
        layout_relax_counter = 0;
    }
    
    // Set up clipping region
    draw_list->PushClipRect(canvas_origin, ImVec2(canvas_origin.x + canvas_size.x, canvas_origin.y + canvas_size.y), true);
    
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

// === Input Handling ===

void NodeEditor::handleMouseInput(ImVec2 mouse_pos, bool lmb_down, bool rmb_down) {
    // Convert mouse to world space for hit testing
    ImVec2 world_mouse = screenToWorld(mouse_pos);
    
    if (!lmb_down && !dragging_node_id.empty()) {
        last_user_interaction_time = ImGui::GetTime();
        dragging_node_id.clear();
    }

    if (!lmb_down) {
        panning = false;
    }
    
    if (lmb_down && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // Check for node click
        Node* clicked_node = nullptr;
        for (auto& node : nodes) {
            if (node->contains(world_mouse)) {
                clicked_node = node.get();
            }
        }
        
        if (clicked_node) {
            dragging_node_id = clicked_node->id;
            drag_offset = ImVec2(clicked_node->position.x - world_mouse.x, 
                                 clicked_node->position.y - world_mouse.y);
            last_user_interaction_time = ImGui::GetTime();
            
            // Select the node
            bool multi = ImGui::GetIO().KeyCtrl;
            selectNode(clicked_node->id, multi);
            panning = false;
        } else {
            // Clicked empty space — deselect and pan by grabbing the background
            clearSelection();
            panning = true;
            pan_start = mouse_pos;
            last_user_interaction_time = ImGui::GetTime();
        }
    }
    
    if (!dragging_node_id.empty() && lmb_down) {
        Node* node = getNode(dragging_node_id);
        if (node) {
            node->position = ImVec2(world_mouse.x + drag_offset.x, world_mouse.y + drag_offset.y);
            last_user_interaction_time = ImGui::GetTime();
        } else {
            // Node was removed or invalidated while dragging
            last_user_interaction_time = ImGui::GetTime();
            dragging_node_id.clear();
        }
    }

    if (rmb_down && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        panning = true;
        pan_start = mouse_pos;
        last_user_interaction_time = ImGui::GetTime();
    }
}

void NodeEditor::handlePanning(ImVec2 delta) {
    // Delta is in screen pixels; convert to world offset change
    view_offset = ImVec2(view_offset.x + delta.x / zoom, view_offset.y + delta.y / zoom);
    last_user_interaction_time = ImGui::GetTime();
}

void NodeEditor::handleZoom(float delta, ImVec2 mouse_screen_pos) {
    // Zoom centered on the mouse cursor position
    ImVec2 world_before = screenToWorld(mouse_screen_pos);
    
    float zoom_factor = delta > 0.0f ? 1.12f : (1.0f / 1.12f);
    setZoom(zoom * zoom_factor);
    
    // After zoom, the same world point should be under the cursor
    // world_before = (mouse_screen_pos - canvas_origin) / zoom_new - view_offset_new
    // => view_offset_new = (mouse_screen_pos - canvas_origin) / zoom_new - world_before
    ImVec2 new_view_offset = ImVec2(
        (mouse_screen_pos.x - canvas_origin.x) / zoom - world_before.x,
        (mouse_screen_pos.y - canvas_origin.y) / zoom - world_before.y
    );
    view_offset = new_view_offset;
    last_user_interaction_time = ImGui::GetTime();
}

// === Layout ===

void NodeEditor::autoLayout() {
    if (nodes.empty()) {
        return;
    }
    const float baseRadius = std::max(260.0f, 118.0f * std::sqrt(static_cast<float>(nodes.size())));
    const ImVec2 center(0.5f * baseRadius, 0.5f * baseRadius);

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const std::size_t h = std::hash<std::string>{}(nodes[i]->id);
        const float angle = static_cast<float>((h % 1000u) / 1000.0) * 6.28318530718f;
        const float radius = baseRadius * (0.45f + 0.72f * static_cast<float>(((h >> 10) % 1000u) / 1000.0));
        const float x = center.x + std::cos(angle) * radius;
        const float y = center.y + std::sin(angle) * radius;
        nodes[i]->position = ImVec2(x - nodes[i]->size.x * 0.5f, y - nodes[i]->size.y * 0.5f);
    }

    if (canvas_size.x > 0.0f && canvas_size.y > 0.0f) {
        fitAllNodes(canvas_size);
    }
}

void NodeEditor::relaxCircularLayout() {
    if (nodes.size() < 2 || !dragging_node_id.empty() || panning) {
        return;
    }

    const std::size_t node_count = nodes.size();
    std::size_t repulsion_stride = 1;
    if (node_count > 260u) {
        repulsion_stride = (node_count / 260u) + 1u;
    }

    // Build node ID -> index map to avoid O(n) lookups in connection processing
    std::unordered_map<std::string, std::size_t> nodeIdToIndex;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        nodeIdToIndex[nodes[i]->id] = i;
    }

    std::vector<ImVec2> forces(nodes.size(), ImVec2(0.0f, 0.0f));
    std::vector<ImVec2> centers(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        centers[i] = ImVec2(nodes[i]->position.x + nodes[i]->size.x * 0.5f,
                            nodes[i]->position.y + nodes[i]->size.y * 0.5f);
    }

    const float repulsion = 128000.0f;
    const float idealEdgeLength = 320.0f;
    const float centerPull = 0.006f;

    // Pairwise repulsion: prevent node overlap
    const std::size_t phase = static_cast<std::size_t>(repulsion_phase) % repulsion_stride;
    for (std::size_t i = 0; i < node_count; ++i) {
        std::size_t j = i + 1 + phase;
        for (; j < node_count; j += repulsion_stride) {
            ImVec2 delta(centers[i].x - centers[j].x, centers[i].y - centers[j].y);
            float distSq = delta.x * delta.x + delta.y * delta.y + 0.01f;
            float dist = std::sqrt(distSq);
            ImVec2 dir(delta.x / dist, delta.y / dist);
            float push = repulsion / distSq;
            forces[i].x += dir.x * push;
            forces[i].y += dir.y * push;
            forces[j].x -= dir.x * push;
            forces[j].y -= dir.y * push;
        }
    }

    repulsion_phase = static_cast<int>((phase + 1u) % repulsion_stride);

    // Edge spring forces: use pre-built index map to avoid O(n) searches per edge
    for (const auto& conn : connections) {
        auto fitIter = nodeIdToIndex.find(conn.from_node_id);
        auto tiIter = nodeIdToIndex.find(conn.to_node_id);
        if (fitIter == nodeIdToIndex.end() || tiIter == nodeIdToIndex.end()) {
            continue;  // Skip invalid edges
        }

        const std::size_t fi = fitIter->second;
        const std::size_t ti = tiIter->second;

        ImVec2 delta(centers[ti].x - centers[fi].x, centers[ti].y - centers[fi].y);
        float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y) + 0.01f;
        ImVec2 dir(delta.x / dist, delta.y / dist);
        float pull = (dist - idealEdgeLength) * 0.062f;
        forces[fi].x += dir.x * pull;
        forces[fi].y += dir.y * pull;
        forces[ti].x -= dir.x * pull;
        forces[ti].y -= dir.y * pull;
    }

    // Center-of-mass pull to keep graph centered
    ImVec2 centerOfMass(0.0f, 0.0f);
    for (const auto& c : centers) {
        centerOfMass.x += c.x;
        centerOfMass.y += c.y;
    }
    centerOfMass.x /= static_cast<float>(nodes.size());
    centerOfMass.y /= static_cast<float>(nodes.size());

    // Apply forces to move nodes with damping for smooth updates
    // Damping prevents oscillation and creates smooth, natural motion
    const float damping = 0.065f;  // Reduced from 0.090f for gentler, smoother motion
    const float maxStep = 55.0f;   // Reduced from 62.0f to limit per-frame movement
    
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        ImVec2 toCenter(centerOfMass.x - centers[i].x, centerOfMass.y - centers[i].y);
        forces[i].x += toCenter.x * centerPull;
        forces[i].y += toCenter.y * centerPull;

        ImVec2 step = clampLength(forces[i], maxStep);
        nodes[i]->position.x += step.x * damping;
        nodes[i]->position.y += step.y * damping;
    }
}

void NodeEditor::resetView() {
    if (!nodes.empty() && canvas_size.x > 0.0f && canvas_size.y > 0.0f) {
        fitAllNodes(canvas_size);
    } else {
        view_offset = ImVec2(0.0f, 0.0f);
        zoom = 1.0f;
    }
}

void NodeEditor::fitAllNodes(ImVec2 fit_canvas_size) {
    if (nodes.empty()) {
        view_offset = ImVec2(0.0f, 0.0f);
        zoom = 1.0f;
        return;
    }
    
    // Compute bounding box in world space
    float min_x = nodes[0]->position.x;
    float min_y = nodes[0]->position.y;
    float max_x = nodes[0]->position.x + nodes[0]->size.x;
    float max_y = nodes[0]->position.y + nodes[0]->size.y;
    
    for (const auto& node : nodes) {
        min_x = std::min(min_x, node->position.x);
        min_y = std::min(min_y, node->position.y);
        max_x = std::max(max_x, node->position.x + node->size.x);
        max_y = std::max(max_y, node->position.y + node->size.y);
    }
    
    float world_width = max_x - min_x;
    float world_height = max_y - min_y;
    
    // Add padding (10% on each side)
    float pad_x = std::max(world_width * 0.1f, 40.0f);
    float pad_y = std::max(world_height * 0.1f, 40.0f);
    min_x -= pad_x;
    min_y -= pad_y;
    world_width += 2.0f * pad_x;
    world_height += 2.0f * pad_y;
    
    if (world_width <= 0.0f) world_width = 1.0f;
    if (world_height <= 0.0f) world_height = 1.0f;
    
    // Compute zoom to fit
    float zoom_x = fit_canvas_size.x / world_width;
    float zoom_y = fit_canvas_size.y / world_height;
    zoom = std::min(zoom_x, zoom_y);
    zoom = std::max(0.1f, std::min(zoom, 3.0f)); // Clamp
    
    // Center the content: view_offset is such that the center of the AABB maps to the center of the canvas
    float center_world_x = min_x + world_width * 0.5f;
    float center_world_y = min_y + world_height * 0.5f;
    float center_canvas_x = fit_canvas_size.x * 0.5f;
    float center_canvas_y = fit_canvas_size.y * 0.5f;
    
    // worldToScreen: screen = canvas_origin + (world + view_offset) * zoom
    // We want center_world to map to center_canvas (relative to canvas_origin):
    // center_canvas = (center_world + view_offset) * zoom
    // => view_offset = center_canvas / zoom - center_world
    view_offset = ImVec2(
        center_canvas_x / zoom - center_world_x,
        center_canvas_y / zoom - center_world_y
    );
}

// === Queries ===

Node* NodeEditor::getNodeAtPosition(ImVec2 pos) {
    // pos is in world space
    for (auto& node : nodes) {
        if (node->contains(pos)) {
            return node.get();
        }
    }
    return nullptr;
}

// === Rendering ===

void NodeEditor::renderNodes(ImDrawList* draw_list) {
    // Clear hovered state
    for (auto& node : nodes) {
        node->is_hovered = false;
    }
    
    // Update hovered state (convert mouse to world space)
    ImVec2 mouse_pos = ImGui::GetMousePos();
    ImVec2 world_mouse = screenToWorld(mouse_pos);
    for (auto& node : nodes) {
        if (node->contains(world_mouse)) {
            node->is_hovered = true;
        }
    }
    
    // Render all nodes at their screen-space positions
    for (auto& node : nodes) {
        ImVec2 screen_pos = worldToScreen(node->position);
        node->render(draw_list, screen_pos, zoom, node->is_hovered);
    }
}

void NodeEditor::renderConnections(ImDrawList* draw_list) {
    const float line_width = std::max(1.6f, 2.6f * std::sqrt(std::max(0.15f, zoom)));
    
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
            const ImVec2 from_center_world(from_node->position.x + from_node->size.x * 0.5f,
                                           from_node->position.y + from_node->size.y * 0.5f);
            const ImVec2 to_center_world(to_node->position.x + to_node->size.x * 0.5f,
                                         to_node->position.y + to_node->size.y * 0.5f);

            // Use intelligent label-box aware anchors that distribute edges around the perimeter
            const ImVec2 from_world = connectionAnchorForLabelBox(*from_node, to_center_world, conn, true);
            const ImVec2 to_world = connectionAnchorForLabelBox(*to_node, from_center_world, conn, false);

            const ImVec2 from_screen = worldToScreen(from_world);
            const ImVec2 to_screen = worldToScreen(to_world);

            ImU32 line_color = ImGui::GetColorU32(ImVec4(0.68f, 0.76f, 0.90f, 0.85f));
            if (from_node->type == NodeType::Stage || to_node->type == NodeType::Stage) {
                line_color = ImGui::GetColorU32(ImVec4(0.88f, 0.80f, 0.34f, 0.92f));
            } else if (from_node->type == NodeType::Domain || to_node->type == NodeType::Domain) {
                line_color = ImGui::GetColorU32(ImVec4(0.47f, 0.73f, 0.94f, 0.88f));
            } else if (from_port->is_input || to_port->is_input) {
                line_color = ImGui::GetColorU32(ImVec4(0.36f, 0.90f, 0.64f, 0.88f));
            }

            const ImVec2 delta(to_screen.x - from_screen.x, to_screen.y - from_screen.y);
            const float direct = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            if (direct < 1.0f) {
                continue;
            }

            // Zoom-stable Bézier control point calculation (screen-space only)
            // to prevent exaggerated loops when zooming out.
            float base_handle = direct * 0.34f;
            base_handle = std::clamp(base_handle, 14.0f, 180.0f);
            
            // Direction vectors for control points - perpendicular to connection axis
            const ImVec2 axis = normalizedOr(delta, ImVec2(1.0f, 0.0f));
            const ImVec2 perp = ImVec2(-axis.y, axis.x);
            
            // Calculate node direction vectors at anchor points
            const ImVec2 from_center_screen = worldToScreen(from_center_world);
            const ImVec2 to_center_screen = worldToScreen(to_center_world);
            const ImVec2 from_dir = normalizedOr(
                ImVec2(from_screen.x - from_center_screen.x, from_screen.y - from_center_screen.y),
                axis);
            const ImVec2 to_dir = normalizedOr(
                ImVec2(to_screen.x - to_center_screen.x, to_screen.y - to_center_screen.y),
                ImVec2(-axis.x, -axis.y));
            
            // Control handle length varies based on directional alignment.
            const float from_align = std::max(0.0f, dot(from_dir, axis));
            const float to_align = std::max(0.0f, dot(to_dir, ImVec2(-axis.x, -axis.y)));
            
            // Keep handles proportional to the visible segment to avoid self-loops.
            const float alignment_factor = 0.58f + 0.42f * (from_align + to_align) * 0.5f;
            const float handle_length = std::min(base_handle * alignment_factor, direct * 0.46f);
            
            // Perpendicular offset to separate nearby edges - context-aware edge bundling
            // Uses connection hash to offset each edge slightly, creating natural separation
            const std::string edge_hash_key = conn.from_node_id + "|" + conn.to_node_id + "|" + conn.from_port_name + "|" + conn.to_port_name;
            const std::size_t edge_hash = std::hash<std::string>{}(edge_hash_key);
            const float hash01 = static_cast<float>(edge_hash & 0xFFFFu) / 65535.0f;
            const float edge_offset = (hash01 - 0.5f) * std::min(24.0f, direct * 0.12f);
            
            // Control points: one on source node direction, one on target node direction
            // With perpendicular offset for edge separation
            const ImVec2 control1(from_screen.x + from_dir.x * handle_length + perp.x * edge_offset,
                                  from_screen.y + from_dir.y * handle_length + perp.y * edge_offset);
            const ImVec2 control2(to_screen.x + to_dir.x * handle_length + perp.x * edge_offset,
                                  to_screen.y + to_dir.y * handle_length + perp.y * edge_offset);

            draw_list->AddBezierCubic(from_screen, control1, control2, to_screen, line_color, line_width, 40);

            const ImVec2 tail = lerp(control2, to_screen, 0.88f);
            drawArrowHead(draw_list, tail, to_screen, brighten(line_color, 1.08f), std::max(7.0f, 7.0f * zoom));
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

void NodeEditor::renderGrid(ImDrawList* draw_list, ImVec2 grid_canvas_size) {
    ImU32 grid_color = ImGui::GetColorU32(ImVec4(0.40f, 0.40f, 0.40f, 0.11f));
    ImU32 grid_color_major = ImGui::GetColorU32(ImVec4(0.40f, 0.40f, 0.40f, 0.24f));

    // Half the previous density: 32 -> 64 world units.
    const float base_grid_world = 64.0f;
    const float major_every = 4.0f;

    const float scaled_grid = base_grid_world * zoom;
    if (scaled_grid < 6.0f) {
        return;
    }

    const ImVec2 world_min = screenToWorld(canvas_origin);
    const ImVec2 world_max = screenToWorld(ImVec2(canvas_origin.x + grid_canvas_size.x,
                                                  canvas_origin.y + grid_canvas_size.y));

    const float left = std::min(world_min.x, world_max.x);
    const float right = std::max(world_min.x, world_max.x);
    const float top = std::min(world_min.y, world_max.y);
    const float bottom = std::max(world_min.y, world_max.y);

    const float first_x = std::floor(left / base_grid_world) * base_grid_world;
    const float first_y = std::floor(top / base_grid_world) * base_grid_world;

    for (float xw = first_x; xw <= right + base_grid_world; xw += base_grid_world) {
        const ImVec2 a = worldToScreen(ImVec2(xw, top));
        const long long major_index = static_cast<long long>(std::llround(xw / base_grid_world));
        const bool major = (major_index % static_cast<long long>(major_every)) == 0;
        draw_list->AddLine(ImVec2(a.x, canvas_origin.y),
                           ImVec2(a.x, canvas_origin.y + grid_canvas_size.y),
                           major ? grid_color_major : grid_color,
                           major ? 1.0f : 0.6f);
    }

    for (float yw = first_y; yw <= bottom + base_grid_world; yw += base_grid_world) {
        const ImVec2 a = worldToScreen(ImVec2(left, yw));
        const long long major_index = static_cast<long long>(std::llround(yw / base_grid_world));
        const bool major = (major_index % static_cast<long long>(major_every)) == 0;
        draw_list->AddLine(ImVec2(canvas_origin.x, a.y),
                           ImVec2(canvas_origin.x + grid_canvas_size.x, a.y),
                           major ? grid_color_major : grid_color,
                           major ? 1.0f : 0.6f);
    }
}

} // namespace ws::gui
