#pragma once

#include <imgui.h>

namespace ws::gui::main_window::detail {

// Turbo-like colormap (perceptually uniform rainbow).
ImU32 colormapTurboLike(float t01);
// Grayscale colormap.
ImU32 colormapGrayscale(float t01);
// Diverging colormap (blue-white-red).
ImU32 colormapDiverging(float t01);
// Water-themed colormap.
ImU32 colormapWater(float t01);

// Applies display transfer function to a value.
float applyDisplayTransfer(float t, float brightness, float contrast, float gamma, bool invertColors);

} // namespace ws::gui::main_window::detail
