#pragma once

#include "ws/gui/main_window/app_state.hpp"

namespace ws::gui::overlay_rendering {

/// @brief Renders a playback control indicator overlay in the top-right corner of the screen.
/// Shows a play/pause icon that fades out over time. Respects accessibility settings.
/// @param overlay State container with icon type and alpha/fade values
/// @param reduceMotion If true, immediately hides the overlay (respects accessibility)
/// @param dt Delta time since last frame, used for fade-out animation
void drawPlaybackOverlay(
    main_window::OverlayState& overlay,
    bool reduceMotion,
    float dt);

} // namespace ws::gui::overlay_rendering
