#pragma once

#include <imgui.h>

namespace ws::gui::main_window::detail {

ImU32 colormapTurboLike(float t01);
ImU32 colormapGrayscale(float t01);
ImU32 colormapDiverging(float t01);
ImU32 colormapWater(float t01);

float applyDisplayTransfer(float t, float brightness, float contrast, float gamma, bool invertColors);

} // namespace ws::gui::main_window::detail
