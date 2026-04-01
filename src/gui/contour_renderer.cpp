#include "ws/gui/contour_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ws::gui {
namespace {

[[nodiscard]] static bool crosses(const float a, const float b, const float level) {
    return (a <= level && b > level) || (a > level && b <= level);
}

[[nodiscard]] static ImVec2 lerpEdge(const ImVec2& a, const ImVec2& b, const float va, const float vb, const float level) {
    const float t = (std::fabs(vb - va) <= 1e-8f) ? 0.5f : std::clamp((level - va) / (vb - va), 0.0f, 1.0f);
    return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

} // namespace

void ContourRenderer::draw(ImDrawList& drawList,
                           const ImVec2 clipMin,
                           const ImVec2 clipMax,
                           const ImVec2 contentMin,
                           const float cellW,
                           const float cellH,
                           const GridSpec& grid,
                           const std::vector<float>& values,
                           const float minValue,
                           const float maxValue,
                           const ContourRenderConfig& config) const {
    if (grid.width < 2 || grid.height < 2 || values.size() < static_cast<std::size_t>(grid.width) * grid.height) {
        return;
    }

    const float span = std::max(1e-6f, maxValue - minValue);
    const float step = std::max(config.interval, span / static_cast<float>(std::max(2, config.maxLevels)));

    drawList.PushClipRect(clipMin, clipMax, true);
    for (float level = minValue; level <= maxValue; level += step) {
        const float t = (level - minValue) / span;
        const ImU32 color = IM_COL32(static_cast<int>(120 + 80 * t), static_cast<int>(180 + 50 * t), 255, 200);

        for (std::uint32_t y = 0; y + 1 < grid.height; ++y) {
            for (std::uint32_t x = 0; x + 1 < grid.width; ++x) {
                const std::size_t i00 = static_cast<std::size_t>(y) * grid.width + x;
                const std::size_t i10 = i00 + 1;
                const std::size_t i01 = i00 + grid.width;
                const std::size_t i11 = i01 + 1;

                const float v00 = values[i00];
                const float v10 = values[i10];
                const float v01 = values[i01];
                const float v11 = values[i11];
                if (!std::isfinite(v00) || !std::isfinite(v10) || !std::isfinite(v01) || !std::isfinite(v11)) {
                    continue;
                }

                const ImVec2 p00(contentMin.x + static_cast<float>(x) * cellW,
                                 contentMin.y + static_cast<float>(y) * cellH);
                const ImVec2 p10(p00.x + cellW, p00.y);
                const ImVec2 p01(p00.x, p00.y + cellH);
                const ImVec2 p11(p00.x + cellW, p00.y + cellH);

                ImVec2 intersections[4];
                int count = 0;
                if (crosses(v00, v10, level)) intersections[count++] = lerpEdge(p00, p10, v00, v10, level);
                if (crosses(v10, v11, level)) intersections[count++] = lerpEdge(p10, p11, v10, v11, level);
                if (crosses(v11, v01, level)) intersections[count++] = lerpEdge(p11, p01, v11, v01, level);
                if (crosses(v01, v00, level)) intersections[count++] = lerpEdge(p01, p00, v01, v00, level);

                if (count == 2) {
                    drawList.AddLine(intersections[0], intersections[1], color, 1.0f);
                } else if (count == 4) {
                    drawList.AddLine(intersections[0], intersections[1], color, 1.0f);
                    drawList.AddLine(intersections[2], intersections[3], color, 1.0f);
                }
            }
        }
    }
    drawList.PopClipRect();
}

} // namespace ws::gui
