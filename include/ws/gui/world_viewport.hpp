#pragma once

#include "ws/gui/paint_tools.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace ws::gui {

// Available tools for viewport interaction.
enum class ViewportTool : std::uint8_t {
    Brush = 0,      // Paint brush tool
    Fill = 1,       // Flood fill tool
    Smooth = 2,     // Smoothing tool
    Eyedropper = 3, // Value sampling tool
    Eraser = 4      // Erasure tool
};

// Current state of viewport painting operations.
struct ViewportPaintState {
    ViewportTool tool = ViewportTool::Brush;
    PaintBlendMode blend = PaintBlendMode::Set;
    float brushRadius = 5.0f;
    float brushStrength = 0.5f;
    float value = 0.5f;
    float fillTolerance = 0.01f;
};

// Interactive viewport for viewing and painting on simulation fields.
class WorldViewport {
public:
    WorldViewport(std::size_t gridWidth, std::size_t gridHeight, float domainMin, float domainMax);

    // Set viewport geometry in screen coordinates.
    void setCanvasGeometry(int viewportX, int viewportY, int viewportWidth, int viewportHeight);
    // Update the current paint state.
    void setPaintState(ViewportPaintState state);

    // Handle mouse input for painting.
    void onMouseClick(int x, int y, std::vector<float>& values);
    void onMouseDrag(int x, int y, std::vector<float>& values);
    void onMouseRelease(std::vector<float>& values);

    // Undo/redo support.
    bool undo(std::vector<float>& values);
    bool redo(std::vector<float>& values);

    [[nodiscard]] ViewportPaintState paintState() const { return paintState_; }

private:
    // Convert screen coordinates to grid coordinates.
    bool windowToGrid(int x, int y, int& outGridX, int& outGridY) const;

    std::size_t gridWidth_ = 1;
    std::size_t gridHeight_ = 1;

    int viewportX_ = 0;
    int viewportY_ = 0;
    int viewportWidth_ = 1;
    int viewportHeight_ = 1;

    ViewportPaintState paintState_{};
    PaintTools paintTools_;
    bool dragging_ = false;
};

} // namespace ws::gui
