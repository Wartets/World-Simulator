#pragma once

#include "ws/gui/paint_tools.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace ws::gui {

enum class ViewportTool : std::uint8_t {
    Brush = 0,
    Fill = 1,
    Smooth = 2,
    Eyedropper = 3,
    Eraser = 4,
};

struct ViewportPaintState {
    ViewportTool tool = ViewportTool::Brush;
    PaintBlendMode blend = PaintBlendMode::Set;
    float brushRadius = 5.0f;
    float brushStrength = 0.5f;
    float value = 0.5f;
    float fillTolerance = 0.01f;
};

class WorldViewport {
public:
    WorldViewport(std::size_t gridWidth, std::size_t gridHeight, float domainMin, float domainMax);

    void setCanvasGeometry(int viewportX, int viewportY, int viewportWidth, int viewportHeight);
    void setPaintState(ViewportPaintState state);

    void onMouseClick(int x, int y, std::vector<float>& values);
    void onMouseDrag(int x, int y, std::vector<float>& values);
    void onMouseRelease(std::vector<float>& values);

    bool undo(std::vector<float>& values);
    bool redo(std::vector<float>& values);

    [[nodiscard]] ViewportPaintState paintState() const { return paintState_; }

private:
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
