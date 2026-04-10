#include "ws/gui/vector_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ws::gui {

// Renders vector field as arrow glyphs on ImDrawList.
// Draws arrows at stride intervals, scaled by magnitude with arrowheads.
// @param drawList ImGui draw list to render onto
// @param clipMin Clipping rectangle minimum
// @param clipMax Clipping rectangle maximum
// @param contentMin Content area minimum (grid origin)
// @param cellW Width of each cell in pixels
// @param cellH Height of each cell in pixels
// @param grid Grid specification
// @param xValues X-component of vector field
// @param yValues Y-component of vector field
// @param config Vector rendering configuration
void VectorRenderer::draw(ImDrawList& drawList,
                          const ImVec2 clipMin,
                          const ImVec2 clipMax,
                          const ImVec2 contentMin,
                          const float cellW,
                          const float cellH,
                          const GridSpec& grid,
                          const std::vector<float>& xValues,
                          const std::vector<float>& yValues,
                          const VectorRenderConfig& config) const {
    if (grid.width == 0 || grid.height == 0) {
        return;
    }

    const int stride = std::max(1, config.stride);

    float minX = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < xValues.size() && i < yValues.size(); ++i) {
        if (std::isfinite(xValues[i]) && std::isfinite(yValues[i])) {
            minX = std::min(minX, xValues[i]);
            maxX = std::max(maxX, xValues[i]);
            minY = std::min(minY, yValues[i]);
            maxY = std::max(maxY, yValues[i]);
        }
    }
    if (!std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(minY) || !std::isfinite(maxY)) {
        return;
    }

    drawList.PushClipRect(clipMin, clipMax, true);
    for (std::uint32_t y = 0; y < grid.height; y += static_cast<std::uint32_t>(stride)) {
        for (std::uint32_t x = 0; x < grid.width; x += static_cast<std::uint32_t>(stride)) {
            const std::size_t index = static_cast<std::size_t>(y) * grid.width + x;
            if (index >= xValues.size() || index >= yValues.size()) {
                continue;
            }
            if (!std::isfinite(xValues[index]) || !std::isfinite(yValues[index])) {
                continue;
            }

            const float nx = ((xValues[index] - minX) / std::max(1e-6f, maxX - minX) - 0.5f) * 2.0f;
            const float ny = ((yValues[index] - minY) / std::max(1e-6f, maxY - minY) - 0.5f) * 2.0f;
            const float mag = std::sqrt(nx * nx + ny * ny);

            const ImVec2 center(contentMin.x + (static_cast<float>(x) + 0.5f) * cellW,
                                contentMin.y + (static_cast<float>(y) + 0.5f) * cellH);
            const float len = std::min(cellW, cellH) * static_cast<float>(stride) * config.scale;
            const ImVec2 tip(center.x + nx * len, center.y + ny * len);

            const int r = static_cast<int>(std::clamp(90.0f + 150.0f * mag, 90.0f, 240.0f));
            const int g = static_cast<int>(std::clamp(240.0f - 130.0f * mag, 80.0f, 240.0f));
            const ImU32 color = IM_COL32(r, g, 100, 220);
            drawList.AddLine(center, tip, color, 1.5f);

            if (mag > 0.05f && len > 4.0f) {
                const float invMag = 1.0f / std::max(1e-6f, mag);
                const float ax = nx * invMag;
                const float ay = ny * invMag;
                const float head = std::min(8.0f, len * 0.35f);
                const ImVec2 h1(tip.x - ax * head + ay * head * 0.45f,
                                tip.y - ay * head - ax * head * 0.45f);
                const ImVec2 h2(tip.x - ax * head - ay * head * 0.45f,
                                tip.y - ay * head + ax * head * 0.45f);
                drawList.AddTriangleFilled(tip, h1, h2, color);
            }
        }
    }
    drawList.PopClipRect();
}

} // namespace ws::gui
