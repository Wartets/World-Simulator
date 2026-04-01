#include "ws/gui/world_viewport.hpp"

#include <algorithm>

namespace ws::gui {

WorldViewport::WorldViewport(
    const std::size_t gridWidth,
    const std::size_t gridHeight,
    const float domainMin,
    const float domainMax)
        : gridWidth_(std::max<std::size_t>(1, gridWidth)),
            gridHeight_(std::max<std::size_t>(1, gridHeight)),
            paintTools_(PaintSessionConfig{std::max<std::size_t>(1, gridWidth), std::max<std::size_t>(1, gridHeight), domainMin, domainMax}) {}

void WorldViewport::setCanvasGeometry(const int viewportX, const int viewportY, const int viewportWidth, const int viewportHeight) {
    viewportX_ = viewportX;
    viewportY_ = viewportY;
    viewportWidth_ = std::max(1, viewportWidth);
    viewportHeight_ = std::max(1, viewportHeight);
}

void WorldViewport::setPaintState(ViewportPaintState state) {
    paintState_ = state;
}

bool WorldViewport::windowToGrid(const int x, const int y, int& outGridX, int& outGridY) const {
    if (x < viewportX_ || y < viewportY_ || x >= viewportX_ + viewportWidth_ || y >= viewportY_ + viewportHeight_) {
        return false;
    }

    const float nx = static_cast<float>(x - viewportX_) / static_cast<float>(std::max(1, viewportWidth_));
    const float ny = static_cast<float>(y - viewportY_) / static_cast<float>(std::max(1, viewportHeight_));
    outGridX = std::clamp(static_cast<int>(nx * static_cast<float>(gridWidth_)), 0, static_cast<int>(gridWidth_ - 1));
    outGridY = std::clamp(static_cast<int>(ny * static_cast<float>(gridHeight_)), 0, static_cast<int>(gridHeight_ - 1));
    return true;
}

void WorldViewport::onMouseClick(const int x, const int y, std::vector<float>& values) {
    if (values.size() != gridWidth_ * gridHeight_) {
        return;
    }

    int gx = 0;
    int gy = 0;
    if (!windowToGrid(x, y, gx, gy)) {
        return;
    }

    if (!dragging_) {
        paintTools_.beginStroke(values);
        dragging_ = true;
    }

    switch (paintState_.tool) {
        case ViewportTool::Brush:
            paintTools_.brush(values, gx, gy, paintState_.brushRadius, paintState_.brushStrength, paintState_.value, paintState_.blend);
            break;
        case ViewportTool::Fill:
            paintTools_.fill(values, gx, gy, paintState_.fillTolerance, paintState_.value);
            break;
        case ViewportTool::Smooth:
            paintTools_.smooth(values, gx, gy, paintState_.brushRadius, paintState_.brushStrength);
            break;
        case ViewportTool::Eyedropper:
            paintTools_.eyedropper(values, gx, gy, paintState_.value);
            break;
        case ViewportTool::Eraser:
            paintTools_.erase(values, gx, gy);
            break;
    }
}

void WorldViewport::onMouseDrag(const int x, const int y, std::vector<float>& values) {
    if (!dragging_) {
        return;
    }

    if (paintState_.tool != ViewportTool::Brush && paintState_.tool != ViewportTool::Smooth && paintState_.tool != ViewportTool::Eraser) {
        return;
    }

    int gx = 0;
    int gy = 0;
    if (!windowToGrid(x, y, gx, gy)) {
        return;
    }

    switch (paintState_.tool) {
        case ViewportTool::Brush:
            paintTools_.brush(values, gx, gy, paintState_.brushRadius, paintState_.brushStrength, paintState_.value, paintState_.blend);
            break;
        case ViewportTool::Smooth:
            paintTools_.smooth(values, gx, gy, paintState_.brushRadius, paintState_.brushStrength);
            break;
        case ViewportTool::Eraser:
            paintTools_.erase(values, gx, gy);
            break;
        default:
            break;
    }
}

void WorldViewport::onMouseRelease(std::vector<float>& values) {
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    paintTools_.endStroke(values);
}

bool WorldViewport::undo(std::vector<float>& values) {
    return paintTools_.undo(values);
}

bool WorldViewport::redo(std::vector<float>& values) {
    return paintTools_.redo(values);
}

} // namespace ws::gui
