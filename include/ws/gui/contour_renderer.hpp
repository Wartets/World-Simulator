#pragma once

#include "ws/core/state_store.hpp"

#include <imgui.h>

#include <vector>

namespace ws::gui {

// =============================================================================
// Contour Render Config
// =============================================================================

// Configuration for contour line rendering.
struct ContourRenderConfig {
    float interval = 0.1f;   // Interval between contour levels.
    int maxLevels = 24;      // Maximum number of contour levels.
};

// =============================================================================
// Contour Renderer
// =============================================================================

// Renders contour lines on a grid for visualization.
class ContourRenderer {
public:
    // Draws contour lines on the given ImDrawList.
    void draw(ImDrawList& drawList,
              ImVec2 clipMin,
              ImVec2 clipMax,
              ImVec2 contentMin,
              float cellW,
              float cellH,
              const GridSpec& grid,
              const std::vector<float>& values,
              float minValue,
              float maxValue,
              const ContourRenderConfig& config) const;
};

} // namespace ws::gui
