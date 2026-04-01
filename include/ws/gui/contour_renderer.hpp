#pragma once

#include "ws/core/state_store.hpp"

#include <imgui.h>

#include <vector>

namespace ws::gui {

struct ContourRenderConfig {
    float interval = 0.1f;
    int maxLevels = 24;
};

class ContourRenderer {
public:
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
