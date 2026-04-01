#pragma once

#include "ws/core/state_store.hpp"

#include <imgui.h>

#include <vector>

namespace ws::gui {

struct VectorRenderConfig {
    int stride = 8;
    float scale = 0.45f;
};

class VectorRenderer {
public:
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
