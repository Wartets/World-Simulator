#include "ws/gui/main_window/overlay_rendering.hpp"
#include <imgui.h>
#include <algorithm>

namespace ws::gui::overlay_rendering {

void drawPlaybackOverlay(
    main_window::OverlayState& overlay,
    bool reduceMotion,
    float dt) {
    if (reduceMotion) {
        overlay.alpha = 0.0f;
        return;
    }
    overlay.alpha = std::max(0.0f, overlay.alpha - 1.2f * dt);
        if (overlay.alpha <= 0.0f || overlay.icon == main_window::OverlayIcon::None) {
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 center(ImGui::GetIO().DisplaySize.x - 44.0f, 44.0f);
    const float r = 22.0f;
    const int   a = static_cast<int>(overlay.alpha * 255.0f);
    dl->AddCircleFilled(center, r, IM_COL32(35, 45, 70, a));
    dl->AddCircle(center, r, IM_COL32(130, 170, 255, a), 24, 2.0f);
    if (overlay.icon == main_window::OverlayIcon::Play) {
        dl->AddTriangleFilled({center.x - 6, center.y - 8}, {center.x - 6, center.y + 8},
                               {center.x + 9, center.y}, IM_COL32(240, 245, 255, a));
    } else {
        dl->AddRectFilled({center.x - 8, center.y - 8}, {center.x - 2, center.y + 8}, IM_COL32(240, 245, 255, a));
        dl->AddRectFilled({center.x + 2, center.y - 8}, {center.x + 8, center.y + 8}, IM_COL32(240, 245, 255, a));
    }
}

} // namespace ws::gui::overlay_rendering
