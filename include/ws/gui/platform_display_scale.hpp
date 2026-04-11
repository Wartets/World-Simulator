#pragma once

struct GLFWwindow;

namespace ws::gui::platform {

// Computes effective display scale for the active window at startup.
// Returns a finite clamped scale in [0.75, 3.0].
float computeEffectiveDisplayScale(GLFWwindow* window, float fallbackScale = 1.0f);

} // namespace ws::gui::platform
