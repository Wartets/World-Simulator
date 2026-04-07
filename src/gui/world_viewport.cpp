#include "ws/gui/world_viewport.hpp"

#include <algorithm>

namespace ws::gui {

// Constructs viewport with grid dimensions and value domain.
// Initializes paint tools with session configuration matching grid.
// @param gridWidth Number of cells horizontally
// @param gridHeight Number of cells vertically
// @param domainMin Minimum value in simulation domain
// @param domainMax Maximum value in simulation domain
WorldViewport::WorldViewport(
    const std::size_t gridWidth,
    const std::size_t gridHeight,
    const float domainMin,
    const float domainMax)
        : gridWidth_(std::max<std::size_t>(1, gridWidth)),
            gridHeight_(std::max<std::size_t>(1, gridHeight)),
            paintTools_(PaintSessionConfig{std::max<std::size_t>(1, gridWidth), std::max<std::size_t>(1, gridHeight), domainMin, domainMax}) {}

// Sets viewport geometry for coordinate transformations.
// @param viewportX Left edge of viewport in window coordinates
// @param viewportY Top edge of viewport in window coordinates
// @param viewportWidth Width of viewport in pixels
// @param viewportHeight Height of viewport in pixels
void WorldViewport::setCanvasGeometry(const int viewportX, const int viewportY, const int viewportWidth, const int viewportHeight) {
    viewportX_ = viewportX;
    viewportY_ = viewportY;
    viewportWidth_ = std::max(1, viewportWidth);
    viewportHeight_ = std::max(1, viewportHeight);
}

// Sets current paint state including tool, brush settings, and values.
// @param state Viewport paint state configuration
void WorldViewport::setPaintState(ViewportPaintState state) {
    paintState_ = state;
}

// Converts window coordinates to grid indices.
// Clamps output to valid grid range.
// @param x Window X coordinate
// @param y Window Y coordinate
// @param outGridX Output grid X coordinate
// @param outGridY Output grid Y coordinate
// @return true if conversion successful (point within viewport)
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

// Handles mouse click for painting/interaction at grid position.
// Begins stroke if not already dragging, applies tool operation.
// @param x Mouse X coordinate
// @param y Mouse Y coordinate
// @param values Field values array to modify
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

// Handles mouse drag for continuous painting.
// Only processes brush, smooth, and eraser tools during drag.
// @param x Mouse X coordinate
// @param y Mouse Y coordinate
// @param values Field values array to modify
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

// Handles mouse release to end paint stroke.
// Commits stroke to paint tools and resets drag state.
// @param values Field values array to finalize
void WorldViewport::onMouseRelease(std::vector<float>& values) {
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    paintTools_.endStroke(values);
}

// Undoes last paint stroke operation.
// @param values Field values array to revert
// @return true if undo successful
bool WorldViewport::undo(std::vector<float>& values) {
    return paintTools_.undo(values);
}

// Redoes previously undone paint stroke operation.
// @param values Field values array to reapply
// @return true if redo successful
bool WorldViewport::redo(std::vector<float>& values) {
    return paintTools_.redo(values);
}

} // namespace ws::gui
