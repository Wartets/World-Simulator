#pragma once

#include "ws/core/state_store.hpp"

#include <imgui.h>

#include <vector>

namespace ws::gui {

// =============================================================================
// Vector Render Config
// =============================================================================

// Configuration for vector field rendering.
struct VectorRenderConfig {
    int stride = 8;           // Sampling stride for vector grid.
    float scale = 0.45f;     // Scale factor for vector length.
};

// =============================================================================
// Vector Renderer
// =============================================================================

// Renders vector fields (e.g., wind) as arrows on the grid.
class VectorRenderer {
public:
    // Draws vector arrows on the given ImDrawList.
    void draw(ImDrawList& drawList,
              ImVec2 clipMin,
              ImVec2 clipMax,
              ImVec2 contentMin,
              float cellW,
              float cellH,
              const GridSpec& grid,
              const std::vector<float>& xValues,
              const std::vector<float>& yValues,
              const VectorRenderConfig& config) const;
};

} // namespace ws::gui
